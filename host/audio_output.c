#include "audio_output.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "audio_mailbox.h"

/* Everything the control thread asks of the audio thread, sent whole. */
typedef struct audio_request {
  noise_config config;
  uint32_t seed;
  uint32_t reset_count;  /* The audio thread reinitializes when this changes. */
  uint32_t strike_count; /* The audio thread starts `strike` when this changes. */
  thunder_strike strike;
} audio_request;

struct audio_output {
  AudioUnit unit;
  audio_mailbox mailbox;
  audio_request request_slots[3];
  audio_request request; /* Control thread's copy of what it last published. */
  audio_mailbox status_mailbox; /* Render thread to control thread. */
  noise_status status_slots[3];
  noise_status status; /* Render thread's copy before publishing. */
  /* Render thread only. */
  noise_gen generator;
  uint32_t applied_reset_count;
  uint32_t applied_strike_count;
};

static void apply_request(audio_output *output, const audio_request *request) {
  if (request->reset_count != output->applied_reset_count) {
    output->applied_reset_count = request->reset_count;
    noise_result result = noise_init(&output->generator, &request->config, request->seed);
    assert(result == NOISE_OK); /* The control thread validated this config. */
    (void)result;
  } else {
    noise_result result = noise_set_config(&output->generator, &request->config);
    assert(result == NOISE_OK);
    (void)result;
  }
  if (request->strike_count != output->applied_strike_count) {
    output->applied_strike_count = request->strike_count;
    /* NOISE_VOICE_LIMIT is counted in the engine state. */
    noise_result result = noise_trigger_thunder(&output->generator, &request->strike);
    assert(result != NOISE_INVALID_STRIKE);
    (void)result;
  }
}

static OSStatus render(void *context, AudioUnitRenderActionFlags *flags,
                       const AudioTimeStamp *timestamp, UInt32 bus,
                       UInt32 frames, AudioBufferList *buffers) {
  (void)flags;
  (void)timestamp;
  (void)bus;
  audio_output *output = context;
  size_t bytes = (size_t)frames * NOISE_CHANNELS * sizeof(int16_t);
  if (buffers->mNumberBuffers != 1 || !buffers->mBuffers[0].mData ||
      buffers->mBuffers[0].mDataByteSize < bytes) {
    for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i) {
      if (buffers->mBuffers[i].mData) {
        memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
      }
    }
    return noErr;
  }
  const audio_request *request = audio_mailbox_take(&output->mailbox);
  if (request) apply_request(output, request);
  noise_fill(&output->generator, buffers->mBuffers[0].mData, frames);
  noise_get_status(&output->generator, &output->status);
  audio_mailbox_publish(&output->status_mailbox, &output->status);
  buffers->mBuffers[0].mDataByteSize = (UInt32)bytes;
  return noErr;
}

static OSStatus open_unit(audio_output *output) {
  AudioComponentDescription description = {
      .componentType = kAudioUnitType_Output,
      .componentSubType = kAudioUnitSubType_DefaultOutput,
      .componentManufacturer = kAudioUnitManufacturer_Apple};
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component) return kAudio_ParamError;
  OSStatus status = AudioComponentInstanceNew(component, &output->unit);
  if (status != noErr) return status;

  AudioStreamBasicDescription format = {
      .mSampleRate = NOISE_SAMPLE_RATE_HZ,
      .mFormatID = kAudioFormatLinearPCM,
      .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked |
                      kAudioFormatFlagsNativeEndian,
      .mBytesPerPacket = NOISE_CHANNELS * sizeof(int16_t),
      .mFramesPerPacket = 1,
      .mBytesPerFrame = NOISE_CHANNELS * sizeof(int16_t),
      .mChannelsPerFrame = NOISE_CHANNELS,
      .mBitsPerChannel = 16};
  status = AudioUnitSetProperty(output->unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format, sizeof(format));
  if (status != noErr) return status;
  AURenderCallbackStruct callback = {.inputProc = render, .inputProcRefCon = output};
  status = AudioUnitSetProperty(output->unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  if (status != noErr) return status;
  return AudioUnitInitialize(output->unit);
}

OSStatus audio_output_create(audio_output **result, const noise_config *config, uint32_t seed) {
  *result = NULL;
  audio_output *output = calloc(1, sizeof(*output));
  if (!output) return kAudio_MemFullError;
  if (noise_init(&output->generator, config, seed) != NOISE_OK) {
    free(output);
    return kAudio_ParamError;
  }
  output->request.config = *config;
  output->request.seed = seed;
  audio_mailbox_init(&output->mailbox, output->request_slots, sizeof(output->request),
                     &output->request);
  noise_get_status(&output->generator, &output->status);
  audio_mailbox_init(&output->status_mailbox, output->status_slots, sizeof(noise_status),
                     &output->status);
  OSStatus status = open_unit(output);
  if (status != noErr) {
    audio_output_destroy(output);
    return status;
  }
  *result = output;
  return noErr;
}

void audio_output_destroy(audio_output *output) {
  if (!output) return;
  if (output->unit) {
    AudioOutputUnitStop(output->unit);
    AudioUnitUninitialize(output->unit);
    AudioComponentInstanceDispose(output->unit);
  }
  free(output);
}

OSStatus audio_output_start(audio_output *output) {
  return AudioOutputUnitStart(output->unit);
}

OSStatus audio_output_stop(audio_output *output) {
  return AudioOutputUnitStop(output->unit);
}

noise_result audio_output_set_config(audio_output *output, const noise_config *config) {
  if (!noise_config_valid(config)) return NOISE_INVALID_CONFIG;
  output->request.config = *config;
  audio_mailbox_publish(&output->mailbox, &output->request);
  return NOISE_OK;
}

void audio_output_reset(audio_output *output, uint32_t seed) {
  output->request.seed = seed;
  ++output->request.reset_count;
  audio_mailbox_publish(&output->mailbox, &output->request);
}

void audio_output_strike(audio_output *output, position_polar position) {
  output->request.strike.position = position;
  ++output->request.strike_count;
  audio_mailbox_publish(&output->mailbox, &output->request);
}

int audio_output_status(audio_output *output, noise_status *status) {
  const noise_status *latest = audio_mailbox_take(&output->status_mailbox);
  if (!latest) return 0;
  *status = *latest;
  return 1;
}
