#import "export_audio.h"

#include <stdlib.h>

#define EXPORT_BATCH 4096u
#define EXPORT_BIT_RATE 256000u /* Broadband noise is the hardest case for AAC. */

static OSStatus open_file(NSURL *url, ExtAudioFileRef *file) {
  AudioStreamBasicDescription aac = {
      .mSampleRate = NOISE_SAMPLE_RATE_HZ,
      .mFormatID = kAudioFormatMPEG4AAC,
      .mChannelsPerFrame = NOISE_CHANNELS};
  AudioStreamBasicDescription pcm = {
      .mSampleRate = NOISE_SAMPLE_RATE_HZ,
      .mFormatID = kAudioFormatLinearPCM,
      .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked |
                      kAudioFormatFlagsNativeEndian,
      .mBytesPerPacket = NOISE_CHANNELS * sizeof(int16_t),
      .mFramesPerPacket = 1,
      .mBytesPerFrame = NOISE_CHANNELS * sizeof(int16_t),
      .mChannelsPerFrame = NOISE_CHANNELS,
      .mBitsPerChannel = 16};
  OSStatus status = ExtAudioFileCreateWithURL((__bridge CFURLRef)url, kAudioFileM4AType, &aac,
                                              NULL, kAudioFileFlags_EraseFile, file);
  if (status != noErr) return status;
  status = ExtAudioFileSetProperty(*file, kExtAudioFileProperty_ClientDataFormat,
                                   sizeof(pcm), &pcm);
  if (status != noErr) return status;
  AudioConverterRef converter = NULL;
  UInt32 size = sizeof(converter);
  status = ExtAudioFileGetProperty(*file, kExtAudioFileProperty_AudioConverter,
                                   &size, &converter);
  if (status != noErr) return status;
  UInt32 bitRate = EXPORT_BIT_RATE;
  status = AudioConverterSetProperty(converter, kAudioConverterEncodeBitRate,
                                     sizeof(bitRate), &bitRate);
  if (status != noErr) return status;
  CFArrayRef noConfig = NULL;
  return ExtAudioFileSetProperty(*file, kExtAudioFileProperty_ConverterConfig,
                                 sizeof(noConfig), &noConfig);
}

OSStatus export_m4a(NSURL *url, const noise_config *config, uint32_t seed, uint32_t frames,
                    void (^progress)(uint32_t done)) {
  noise_gen *gen = malloc(sizeof(*gen));
  int16_t *batch = malloc(EXPORT_BATCH * NOISE_CHANNELS * sizeof(int16_t));
  if (!gen || !batch) {
    free(gen);
    free(batch);
    return kAudio_MemFullError;
  }
  if (noise_init(gen, config, seed) != NOISE_OK) {
    free(gen);
    free(batch);
    return kAudio_ParamError;
  }
  ExtAudioFileRef file = NULL;
  OSStatus status = open_file(url, &file);
  uint32_t done = 0;
  while (status == noErr && done < frames) {
    uint32_t count = frames - done < EXPORT_BATCH ? frames - done : EXPORT_BATCH;
    noise_fill(gen, batch, count);
    UInt32 bytes = count * NOISE_CHANNELS * sizeof(int16_t);
    AudioBufferList buffers = {1, {{NOISE_CHANNELS, bytes, batch}}};
    status = ExtAudioFileWrite(file, count, &buffers);
    done += count;
    if (done % (10u * NOISE_SAMPLE_RATE_HZ) < count || done == frames) progress(done);
  }
  if (file) {
    OSStatus closed = ExtAudioFileDispose(file);
    if (status == noErr) status = closed;
  }
  free(gen);
  free(batch);
  return status;
}
