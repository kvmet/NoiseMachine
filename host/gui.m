#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>

#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "noise_core.h"

typedef NS_ENUM(NSInteger, NoiseControl) {
  NoiseControlWhite,
  NoiseControlPink,
  NoiseControlHum50,
  NoiseControlHum60,
  NoiseControlWindGain,
  NoiseControlWindBrightness,
  NoiseControlWindGustDepth,
  NoiseControlWindGustRate,
  NoiseControlWindWidth,
  NoiseControlCricketGain,
  NoiseControlCricketCallRate,
  NoiseControlCricketPitch,
  NoiseControlCricketPitchVariation,
  NoiseControlCricketWidth,
  NoiseControlCricketMinDistance,
  NoiseControlCricketMaxDistance,
  NoiseControlCicadaGain,
  NoiseControlCicadaPitch,
  NoiseControlCicadaClickRate,
  NoiseControlCicadaChorus,
  NoiseControlCicadaWidth,
  NoiseControlCicadaMinDistance,
  NoiseControlCicadaMaxDistance,
  NoiseControlThunderGain,
  NoiseControlThunderRate,
  NoiseControlThunderMinDistance,
  NoiseControlThunderMaxDistance,
  NoiseControlThunderReverbGain,
  NoiseControlThunderReverbDecay,
  NoiseControlRainGain,
  NoiseControlMaster,
  NoiseControlRainIntensity,
  NoiseControlMinRain,
  NoiseControlMaxRain,
  NoiseControlWeatherStep,
  NoiseControlRainSlew,
  NoiseControlDropRate,
  NoiseControlFallHeight,
  NoiseControlWaterWeight,
  NoiseControlDirtWeight,
  NoiseControlLeafWeight,
  NoiseControlConcreteWeight,
  NoiseControlGlassWeight,
  NoiseControlMetalWeight,
  NoiseControlPlasticWeight,
  NoiseControlAsphaltWeight,
  NoiseControlAsphaltRoofWeight,
  NoiseControlMinDistance,
  NoiseControlMaxDistance,
  NoiseControlStereoWidth,
  NoiseControlHead,
  NoiseControlRear,
  NoiseControlReverb,
  NoiseControlModArrival,
  NoiseControlModSize,
  NoiseControlModRainGain,
  NoiseControlModReverb,
  NoiseControlModFallHeight,
  NoiseControlModMinDistance,
  NoiseControlModMaxDistance,
  NoiseControlModWater,
  NoiseControlModDirt,
  NoiseControlModLeaf,
  NoiseControlModConcrete,
  NoiseControlModGlass,
  NoiseControlModMetal,
  NoiseControlModPlastic,
  NoiseControlModAsphalt,
  NoiseControlModAsphaltRoof,
  NoiseControlWaterImpactMin,
  NoiseControlWaterImpactMax,
  NoiseControlWaterBubbleProbability,
  NoiseControlWaterBubbleRadiusMin,
  NoiseControlWaterBubbleRadiusMax,
  NoiseControlWaterBubbleGainMin,
  NoiseControlWaterBubbleGainMax,
  NoiseControlWaterBubbleDecayMin,
  NoiseControlWaterBubbleDecayMax,
  NoiseControlCount
};

/* Listening settings the GUI opens with; the engine keeps its own defaults. */
static void apply_startup_settings(noise_config *c) {
  memset(c->ambient_gain, 0, sizeof(c->ambient_gain));
  c->wind.gain = 0.10f;
  c->crickets.gain = 0.09f;
  c->rain.gain = 0.91f;
  c->master_gain = 0.80f;
  c->wind.brightness = 0.22f;
  c->wind.gust_depth = 0.94f;
  c->wind.gust_rate_hz = 0.19f;
  c->wind.stereo_width = 0.78f;
  c->crickets.call_rate_hz = 0.51f;
  c->crickets.pitch_hz = 4500.0f;
  c->crickets.pitch_variation = 0.24f;
  c->crickets.placement.stereo_width = 1.00f;
  c->cicadas.gain = 0.26f;
  c->cicadas.species = CICADA_HIGURASHI;
  c->cicadas.pitch_hz = 5000.0f;
  c->cicadas.click_rate_scale = 0.51f;
  c->cicadas.chorus = 0.69f;
  c->cicadas.placement.stereo_width = 1.00f;
  c->cicadas.placement.min_distance_m = 1.205f;
  c->cicadas.placement.max_distance_m = 13.346f;
  c->weather.vary = 1;
  c->weather.intensity = 0.84f;
  c->weather.min_intensity = 0.15f;
  c->weather.max_intensity = 1.00f;
  c->weather.step_s = 8.0f;
  c->weather.slew_s = 2.0f;
  c->rain.max_drops_per_s = 2000.0f;
  c->rain.fall_height_m = 1000.0f;
  c->rain.surface_weight[WATER] = 6.05624e-05f;
  c->rain.surface_weight[DIRT] = 4.94011e-05f;
  c->rain.surface_weight[LEAF] = 3.04537e-06f;
  c->rain.surface_weight[CONCRETE] = 1.68416e-05f;
  c->rain.surface_weight[GLASS] = 2.29582e-07f;
  c->rain.surface_weight[METAL] = 2.68089e-07f;
  c->rain.surface_weight[PLASTIC] = 3.93724e-06f;
  c->rain.surface_weight[ASPHALT] = 0.002557f;
  c->rain.surface_weight[ASPHALT_ROOF] = 0.001559f;
  c->rain.water.impact_gain_min = 0.16f;
  c->rain.water.impact_gain_max = 0.50f;
  c->rain.water.bubble_probability = 0.51f;
  c->rain.water.bubble_radius_min_m = 0.00023f;
  c->rain.water.bubble_radius_max_m = 0.00069f;
  c->rain.water.bubble_gain_min = 0.36f;
  c->rain.water.bubble_gain_max = 2.50f;
  c->rain.water.bubble_decay_min = 0.31f;
  c->rain.water.bubble_decay_max = 0.58f;
}

#define EXPORT_BATCH 4096u
#define EXPORT_BIT_RATE 256000u /* Broadband noise is the hardest case for AAC. */

static OSStatus export_m4a(NSURL *url, const noise_config *config, uint32_t seed,
                           uint32_t frames, void (^progress)(uint32_t done)) {
  noise_gen *gen = malloc(sizeof(*gen));
  if (!gen) return kAudio_MemFullError;
  if (noise_init(gen, config, seed) != NOISE_OK) {
    free(gen);
    return kAudio_ParamError;
  }
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
  ExtAudioFileRef file = NULL;
  OSStatus status = ExtAudioFileCreateWithURL((__bridge CFURLRef)url, kAudioFileM4AType, &aac,
                                              NULL, kAudioFileFlags_EraseFile, &file);
  if (status == noErr) {
    status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat,
                                     sizeof(pcm), &pcm);
  }
  if (status == noErr) {
    AudioConverterRef converter = NULL;
    UInt32 size = sizeof(converter);
    status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_AudioConverter,
                                     &size, &converter);
    UInt32 bitRate = EXPORT_BIT_RATE;
    if (status == noErr) {
      status = AudioConverterSetProperty(converter, kAudioConverterEncodeBitRate,
                                         sizeof(bitRate), &bitRate);
    }
    CFArrayRef noConfig = NULL;
    if (status == noErr) {
      status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ConverterConfig,
                                       sizeof(noConfig), &noConfig);
    }
  }
  static int16_t batch[EXPORT_BATCH * NOISE_CHANNELS];
  uint32_t done = 0;
  while (status == noErr && done < frames) {
    uint32_t count = frames - done < EXPORT_BATCH ? frames - done : EXPORT_BATCH;
    noise_fill(gen, batch, count);
    AudioBufferList buffers = {1, {{NOISE_CHANNELS, count * pcm.mBytesPerFrame, batch}}};
    status = ExtAudioFileWrite(file, count, &buffers);
    done += count;
    if (done % (10u * NOISE_SAMPLE_RATE_HZ) < count || done == frames) progress(done);
  }
  if (file) {
    OSStatus closed = ExtAudioFileDispose(file);
    if (status == noErr) status = closed;
  }
  free(gen);
  return status;
}

@interface NoiseAppDelegate : NSObject <NSApplicationDelegate, NSTextFieldDelegate>
@end

@implementation NoiseAppDelegate {
  NSWindow *_window;
  NSButton *_playButton;
  NSButton *_varyButton;
  NSButton *_exportButton;
  NSTextField *_statusLabel;
  NSTextField *_seedField;
  NSSlider *_sliders[NoiseControlCount];
  NSTextField *_valueFields[NoiseControlCount];
  double _minimum[NoiseControlCount];
  double _maximum[NoiseControlCount];
  BOOL _logarithmic[NoiseControlCount];
  AudioUnit _audioUnit;
  noise_gen _generator;
  _Atomic(float) _controls[NoiseControlCount];
  _Atomic(int) _varyRain;
  _Atomic(int) _cicadaSpecies;
  _Atomic(float) _strikeDistance;
  _Atomic(float) _strikeAngle;
  _Atomic(int) _strikeRequested;
  float _appliedRainControl;
  int _appliedVary;
  BOOL _playing;
}

static OSStatus render_audio(void *context, AudioUnitRenderActionFlags *flags,
                             const AudioTimeStamp *timestamp, UInt32 bus,
                             UInt32 frames, AudioBufferList *buffers) {
  (void)flags;
  (void)timestamp;
  (void)bus;
  NoiseAppDelegate *app = (__bridge NoiseAppDelegate *)context;
  return [app renderFrames:frames into:buffers];
}

- (float)controlValue:(NoiseControl)control {
  return atomic_load_explicit(&_controls[control], memory_order_relaxed);
}

- (noise_config)configFromControls {
  noise_config config;
  noise_config_default(&config);
  config.ambient_gain[NOISE_KIND_WHITE] = [self controlValue:NoiseControlWhite];
  config.ambient_gain[NOISE_KIND_PINK] = [self controlValue:NoiseControlPink];
  config.ambient_gain[NOISE_KIND_HUM_50HZ] = [self controlValue:NoiseControlHum50];
  config.ambient_gain[NOISE_KIND_HUM_60HZ] = [self controlValue:NoiseControlHum60];
  config.wind.gain = [self controlValue:NoiseControlWindGain];
  config.wind.brightness = [self controlValue:NoiseControlWindBrightness];
  config.wind.gust_depth = [self controlValue:NoiseControlWindGustDepth];
  config.wind.gust_rate_hz = [self controlValue:NoiseControlWindGustRate];
  config.wind.stereo_width = [self controlValue:NoiseControlWindWidth];
  config.crickets.gain = [self controlValue:NoiseControlCricketGain];
  config.crickets.call_rate_hz = [self controlValue:NoiseControlCricketCallRate];
  config.crickets.pitch_hz = [self controlValue:NoiseControlCricketPitch];
  config.crickets.pitch_variation = [self controlValue:NoiseControlCricketPitchVariation];
  config.crickets.placement.stereo_width = [self controlValue:NoiseControlCricketWidth];
  config.crickets.placement.min_distance_m = [self controlValue:NoiseControlCricketMinDistance];
  config.crickets.placement.max_distance_m = [self controlValue:NoiseControlCricketMaxDistance];
  config.cicadas.gain = [self controlValue:NoiseControlCicadaGain];
  config.cicadas.pitch_hz = [self controlValue:NoiseControlCicadaPitch];
  config.cicadas.species =
      (cicada_species)atomic_load_explicit(&_cicadaSpecies, memory_order_relaxed);
  config.cicadas.click_rate_scale = [self controlValue:NoiseControlCicadaClickRate];
  config.cicadas.chorus = [self controlValue:NoiseControlCicadaChorus];
  config.cicadas.placement.stereo_width = [self controlValue:NoiseControlCicadaWidth];
  config.cicadas.placement.min_distance_m = [self controlValue:NoiseControlCicadaMinDistance];
  config.cicadas.placement.max_distance_m = [self controlValue:NoiseControlCicadaMaxDistance];
  config.thunder.gain = [self controlValue:NoiseControlThunderGain];
  config.thunder.rate_per_min = [self controlValue:NoiseControlThunderRate];
  config.thunder.min_distance_m = [self controlValue:NoiseControlThunderMinDistance];
  config.thunder.max_distance_m = [self controlValue:NoiseControlThunderMaxDistance];
  config.thunder.reverb_gain = [self controlValue:NoiseControlThunderReverbGain];
  config.thunder.reverb_decay_s = [self controlValue:NoiseControlThunderReverbDecay];
  config.rain.gain = [self controlValue:NoiseControlRainGain];
  config.master_gain = [self controlValue:NoiseControlMaster];
  config.weather.intensity = [self controlValue:NoiseControlRainIntensity];
  config.weather.min_intensity = [self controlValue:NoiseControlMinRain];
  config.weather.max_intensity = [self controlValue:NoiseControlMaxRain];
  config.weather.vary = atomic_load_explicit(&_varyRain, memory_order_relaxed);
  config.weather.step_s = [self controlValue:NoiseControlWeatherStep];
  config.weather.slew_s = [self controlValue:NoiseControlRainSlew];
  config.rain.max_drops_per_s = [self controlValue:NoiseControlDropRate];
  config.rain.fall_height_m = [self controlValue:NoiseControlFallHeight];
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    config.rain.surface_weight[i] = [self controlValue:NoiseControlWaterWeight + i];
  }
  config.rain.min_distance_m = [self controlValue:NoiseControlMinDistance];
  config.rain.max_distance_m = [self controlValue:NoiseControlMaxDistance];
  config.listener.stereo_width_m = [self controlValue:NoiseControlStereoWidth];
  config.listener.head_amount = [self controlValue:NoiseControlHead];
  config.listener.rear_amount = [self controlValue:NoiseControlRear];
  config.reverb_gain = [self controlValue:NoiseControlReverb];
  config.weather.mod_amount[WEATHER_MOD_ARRIVAL_RATE] =
      [self controlValue:NoiseControlModArrival];
  config.weather.mod_amount[WEATHER_MOD_DROP_SIZE] =
      [self controlValue:NoiseControlModSize];
  config.weather.mod_amount[WEATHER_MOD_RAIN_GAIN] =
      [self controlValue:NoiseControlModRainGain];
  config.weather.mod_amount[WEATHER_MOD_REVERB_GAIN] =
      [self controlValue:NoiseControlModReverb];
  config.weather.mod_amount[WEATHER_MOD_FALL_HEIGHT] =
      [self controlValue:NoiseControlModFallHeight];
  config.weather.mod_amount[WEATHER_MOD_MIN_DISTANCE] =
      [self controlValue:NoiseControlModMinDistance];
  config.weather.mod_amount[WEATHER_MOD_MAX_DISTANCE] =
      [self controlValue:NoiseControlModMaxDistance];
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    config.weather.mod_amount[WEATHER_MOD_WATER_WEIGHT + i] =
        [self controlValue:NoiseControlModWater + i];
  }
  config.rain.water.impact_gain_min = [self controlValue:NoiseControlWaterImpactMin];
  config.rain.water.impact_gain_max = [self controlValue:NoiseControlWaterImpactMax];
  config.rain.water.bubble_probability = [self controlValue:NoiseControlWaterBubbleProbability];
  config.rain.water.bubble_radius_min_m =
      0.001f * [self controlValue:NoiseControlWaterBubbleRadiusMin];
  config.rain.water.bubble_radius_max_m =
      0.001f * [self controlValue:NoiseControlWaterBubbleRadiusMax];
  config.rain.water.bubble_gain_min = [self controlValue:NoiseControlWaterBubbleGainMin];
  config.rain.water.bubble_gain_max = [self controlValue:NoiseControlWaterBubbleGainMax];
  config.rain.water.bubble_decay_min = [self controlValue:NoiseControlWaterBubbleDecayMin];
  config.rain.water.bubble_decay_max = [self controlValue:NoiseControlWaterBubbleDecayMax];
  return config;
}

- (void)loadControlsIntoGenerator {
  noise_config config = [self configFromControls];
  _generator.config = config;
  uint32_t weatherPeriod = (uint32_t)(config.weather.step_s * NOISE_SAMPLE_RATE_HZ);
  if (_generator.weather.samples >= weatherPeriod) _generator.weather.samples = 0;
  _generator.weather.period = weatherPeriod;
  _generator.weather.slew = -expm1f(-1.0f / (config.weather.slew_s * NOISE_SAMPLE_RATE_HZ));

  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) sum += config.rain.surface_weight[i];
  float cumulative = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    cumulative += config.rain.surface_weight[i];
    _generator.rain.surface_cdf[i] = cumulative / sum;
  }

  float rain = config.weather.intensity;
  if (rain != _appliedRainControl || config.weather.vary != _appliedVary) {
    if (config.weather.vary) {
      rain = fminf(config.weather.max_intensity, fmaxf(config.weather.min_intensity, rain));
      float span = config.weather.max_intensity - config.weather.min_intensity;
      float relative = span > 0.0f ? (rain - config.weather.min_intensity) / span : 0.0f;
      _generator.state.weather_state = relative < 0.25f ? 0u : (relative < 0.75f ? 1u : 2u);
      _generator.weather.samples = 0;
    }
    _generator.state.rain_intensity = rain;
    _generator.state.rain_target = rain;
    _appliedRainControl = config.weather.intensity;
    _appliedVary = config.weather.vary;
  } else if (!config.weather.vary) {
    _generator.state.rain_intensity = rain;
    _generator.state.rain_target = rain;
  } else {
    _generator.state.rain_intensity = fminf(config.weather.max_intensity,
        fmaxf(config.weather.min_intensity, _generator.state.rain_intensity));
    _generator.state.rain_target = fminf(config.weather.max_intensity,
        fmaxf(config.weather.min_intensity, _generator.state.rain_target));
  }
}

- (OSStatus)renderFrames:(UInt32)frames into:(AudioBufferList *)buffers {
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
  [self loadControlsIntoGenerator];
  if (atomic_exchange_explicit(&_strikeRequested, 0, memory_order_acquire)) {
    thunder_strike strike = {{
        atomic_load_explicit(&_strikeDistance, memory_order_relaxed),
        atomic_load_explicit(&_strikeAngle, memory_order_relaxed)}};
    noise_trigger_thunder(&_generator, &strike);
  }
  noise_fill(&_generator, buffers->mBuffers[0].mData, frames);
  buffers->mBuffers[0].mDataByteSize = (UInt32)bytes;
  return noErr;
}

- (OSStatus)setupAudio {
  AudioComponentDescription description = {
      .componentType = kAudioUnitType_Output,
      .componentSubType = kAudioUnitSubType_DefaultOutput,
      .componentManufacturer = kAudioUnitManufacturer_Apple,
      .componentFlags = 0,
      .componentFlagsMask = 0};
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component) return kAudio_ParamError;
  OSStatus status = AudioComponentInstanceNew(component, &_audioUnit);
  if (status != noErr) return status;

  AudioStreamBasicDescription format = {
      .mSampleRate = NOISE_SAMPLE_RATE_HZ,
      .mFormatID = kAudioFormatLinearPCM,
      .mFormatFlags = kAudioFormatFlagIsSignedInteger |
                      kAudioFormatFlagIsPacked |
                      kAudioFormatFlagsNativeEndian,
      .mBytesPerPacket = NOISE_CHANNELS * sizeof(int16_t),
      .mFramesPerPacket = 1,
      .mBytesPerFrame = NOISE_CHANNELS * sizeof(int16_t),
      .mChannelsPerFrame = NOISE_CHANNELS,
      .mBitsPerChannel = 16};
  status = AudioUnitSetProperty(_audioUnit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format, sizeof(format));
  if (status != noErr) return status;
  AURenderCallbackStruct callback = {
      .inputProc = render_audio,
      .inputProcRefCon = (__bridge void *)self};
  status = AudioUnitSetProperty(_audioUnit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  if (status != noErr) return status;
  return AudioUnitInitialize(_audioUnit);
}

- (NSString *)formattedValue:(double)value control:(NoiseControl)control {
  if (control == NoiseControlDropRate) return [NSString stringWithFormat:@"%.0f", value];
  if (control >= NoiseControlWaterWeight && control <= NoiseControlAsphaltRoofWeight) {
    return [NSString stringWithFormat:@"%.6g", value];
  }
  if (control == NoiseControlThunderMinDistance || control == NoiseControlThunderMaxDistance) {
    return [NSString stringWithFormat:@"%.0f", value];
  }
  if (control == NoiseControlStereoWidth || control == NoiseControlMinDistance ||
      control == NoiseControlMaxDistance || control == NoiseControlCricketMinDistance ||
      control == NoiseControlCricketMaxDistance || control == NoiseControlCicadaMinDistance ||
      control == NoiseControlCicadaMaxDistance) {
    return [NSString stringWithFormat:@"%.3f", value];
  }
  return [NSString stringWithFormat:@"%.2f", value];
}

- (void)storeControl:(NoiseControl)control value:(double)value {
  value = fmin(_maximum[control], fmax(_minimum[control], value));
  atomic_store_explicit(&_controls[control], (float)value, memory_order_relaxed);
  if (control >= NoiseControlWaterWeight && control <= NoiseControlAsphaltRoofWeight) {
    _sliders[control].doubleValue = value > 0.0 ? fmax(-7.0, log10(value)) : -7.0;
  } else {
    _sliders[control].doubleValue = _logarithmic[control] ? log(value) : value;
  }
  _valueFields[control].stringValue = [self formattedValue:value control:control];
}

- (void)setControl:(NoiseControl)control value:(double)value {
  value = fmin(_maximum[control], fmax(_minimum[control], value));
  if (control >= NoiseControlWaterWeight && control <= NoiseControlAsphaltRoofWeight &&
      value == 0.0) {
    double other = 0.0;
    for (NoiseControl i = NoiseControlWaterWeight; i <= NoiseControlAsphaltRoofWeight; ++i) {
      if (i != control) other += [self controlValue:i];
    }
    if (other == 0.0) {
      value = 0.001;
      _statusLabel.stringValue = @"At least one surface weight must be above zero";
    }
  }
  [self storeControl:control value:value];

  if (control == NoiseControlMinRain && value > [self controlValue:NoiseControlMaxRain]) {
    [self storeControl:NoiseControlMaxRain value:value];
  } else if (control == NoiseControlMaxRain &&
             value < [self controlValue:NoiseControlMinRain]) {
    [self storeControl:NoiseControlMinRain value:value];
  } else if (control == NoiseControlMinDistance &&
             value > [self controlValue:NoiseControlMaxDistance]) {
    [self storeControl:NoiseControlMaxDistance value:value];
  } else if (control == NoiseControlMaxDistance &&
             value < [self controlValue:NoiseControlMinDistance]) {
    [self storeControl:NoiseControlMinDistance value:value];
  } else if (control == NoiseControlCricketMinDistance &&
             value > [self controlValue:NoiseControlCricketMaxDistance]) {
    [self storeControl:NoiseControlCricketMaxDistance value:value];
  } else if (control == NoiseControlCricketMaxDistance &&
             value < [self controlValue:NoiseControlCricketMinDistance]) {
    [self storeControl:NoiseControlCricketMinDistance value:value];
  } else if (control == NoiseControlCicadaMinDistance &&
             value > [self controlValue:NoiseControlCicadaMaxDistance]) {
    [self storeControl:NoiseControlCicadaMaxDistance value:value];
  } else if (control == NoiseControlCicadaMaxDistance &&
             value < [self controlValue:NoiseControlCicadaMinDistance]) {
    [self storeControl:NoiseControlCicadaMinDistance value:value];
  } else if (control == NoiseControlThunderMinDistance &&
             value > [self controlValue:NoiseControlThunderMaxDistance]) {
    [self storeControl:NoiseControlThunderMaxDistance value:value];
  } else if (control == NoiseControlThunderMaxDistance &&
             value < [self controlValue:NoiseControlThunderMinDistance]) {
    [self storeControl:NoiseControlThunderMinDistance value:value];
  } else if (control == NoiseControlWaterImpactMin &&
             value > [self controlValue:NoiseControlWaterImpactMax]) {
    [self storeControl:NoiseControlWaterImpactMax value:value];
  } else if (control == NoiseControlWaterImpactMax &&
             value < [self controlValue:NoiseControlWaterImpactMin]) {
    [self storeControl:NoiseControlWaterImpactMin value:value];
  } else if (control == NoiseControlWaterBubbleRadiusMin &&
             value > [self controlValue:NoiseControlWaterBubbleRadiusMax]) {
    [self storeControl:NoiseControlWaterBubbleRadiusMax value:value];
  } else if (control == NoiseControlWaterBubbleRadiusMax &&
             value < [self controlValue:NoiseControlWaterBubbleRadiusMin]) {
    [self storeControl:NoiseControlWaterBubbleRadiusMin value:value];
  } else if (control == NoiseControlWaterBubbleGainMin &&
             value > [self controlValue:NoiseControlWaterBubbleGainMax]) {
    [self storeControl:NoiseControlWaterBubbleGainMax value:value];
  } else if (control == NoiseControlWaterBubbleGainMax &&
             value < [self controlValue:NoiseControlWaterBubbleGainMin]) {
    [self storeControl:NoiseControlWaterBubbleGainMin value:value];
  } else if (control == NoiseControlWaterBubbleDecayMin &&
             value > [self controlValue:NoiseControlWaterBubbleDecayMax]) {
    [self storeControl:NoiseControlWaterBubbleDecayMax value:value];
  } else if (control == NoiseControlWaterBubbleDecayMax &&
             value < [self controlValue:NoiseControlWaterBubbleDecayMin]) {
    [self storeControl:NoiseControlWaterBubbleDecayMin value:value];
  }

  if (atomic_load_explicit(&_varyRain, memory_order_relaxed) &&
      (control == NoiseControlMinRain || control == NoiseControlMaxRain)) {
    double rain = [self controlValue:NoiseControlRainIntensity];
    rain = fmin([self controlValue:NoiseControlMaxRain],
                fmax([self controlValue:NoiseControlMinRain], rain));
    [self storeControl:NoiseControlRainIntensity value:rain];
  }
}

- (NSView *)sliderRow:(NSString *)name control:(NoiseControl)control
                 value:(double)value minimum:(double)minimum maximum:(double)maximum
           logarithmic:(BOOL)logarithmic {
  _minimum[control] = minimum;
  _maximum[control] = maximum;
  _logarithmic[control] = logarithmic;
  atomic_init(&_controls[control], (float)value);

  NSTextField *label = [NSTextField labelWithString:name];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:145.0].active = YES;

  BOOL surfaceWeight = control >= NoiseControlWaterWeight &&
                       control <= NoiseControlAsphaltRoofWeight;
  double sliderValue = surfaceWeight ? (value > 0.0 ? log10(value) : -7.0) :
                                       (logarithmic ? log(value) : value);
  double sliderMinimum = surfaceWeight ? -7.0 : (logarithmic ? log(minimum) : minimum);
  double sliderMaximum = surfaceWeight ? 0.0 : (logarithmic ? log(maximum) : maximum);
  NSSlider *slider = [NSSlider sliderWithValue:sliderValue
                                      minValue:sliderMinimum
                                      maxValue:sliderMaximum
                                        target:self action:@selector(sliderChanged:)];
  slider.tag = control;
  slider.continuous = YES;
  if (minimum == -1.0 && maximum == 1.0) {
    slider.numberOfTickMarks = 3;
    slider.allowsTickMarkValuesOnly = NO;
  }
  [slider.widthAnchor constraintEqualToConstant:280.0].active = YES;
  _sliders[control] = slider;

  NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
  field.font = [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular];
  field.alignment = NSTextAlignmentRight;
  field.controlSize = NSControlSizeSmall;
  field.target = self;
  field.action = @selector(valueFieldChanged:);
  field.delegate = self;
  field.tag = control;
  field.stringValue = [self formattedValue:value control:control];
  /* %.6g weights such as 6.05624e-05 need 11 characters. */
  [field.widthAnchor constraintEqualToConstant:surfaceWeight ? 100.0 : 72.0].active = YES;
  _valueFields[control] = field;

  NSStackView *row = [NSStackView stackViewWithViews:@[label, slider, field]];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.alignment = NSLayoutAttributeCenterY;
  row.spacing = 10.0;
  return row;
}

- (void)sliderChanged:(NSSlider *)sender {
  NoiseControl control = (NoiseControl)sender.tag;
  double value;
  if (control >= NoiseControlWaterWeight && control <= NoiseControlAsphaltRoofWeight) {
    value = sender.doubleValue <= -7.0 ? 0.0 : pow(10.0, sender.doubleValue);
  } else {
    value = _logarithmic[control] ? exp(sender.doubleValue) : sender.doubleValue;
  }
  [self setControl:control value:value];
}

- (void)valueFieldChanged:(NSTextField *)sender {
  NoiseControl control = (NoiseControl)sender.tag;
  char *end;
  errno = 0;
  const char *text = sender.stringValue.UTF8String;
  double value = strtod(text, &end);
  if (!text[0] || end == text || *end || errno == ERANGE || !isfinite(value)) {
    sender.stringValue = [self formattedValue:[self controlValue:control] control:control];
    _statusLabel.stringValue = @"Enter a finite number";
    return;
  }
  [self setControl:control value:value];
}

- (void)controlTextDidEndEditing:(NSNotification *)notification {
  NSTextField *field = notification.object;
  if (field.tag >= 0 && field.tag < NoiseControlCount) [self valueFieldChanged:field];
}

- (void)cicadaSpeciesChanged:(NSPopUpButton *)sender {
  atomic_store_explicit(&_cicadaSpecies, (int)sender.indexOfSelectedItem, memory_order_relaxed);
}

- (void)varyChanged:(NSButton *)sender {
  int vary = sender.state == NSControlStateValueOn;
  atomic_store_explicit(&_varyRain, vary, memory_order_relaxed);
  if (vary) {
    double rain = [self controlValue:NoiseControlRainIntensity];
    rain = fmin([self controlValue:NoiseControlMaxRain],
                fmax([self controlValue:NoiseControlMinRain], rain));
    [self storeControl:NoiseControlRainIntensity value:rain];
  }
}

- (void)strikeThunder:(NSButton *)sender {
  (void)sender;
  double near = [self controlValue:NoiseControlThunderMinDistance];
  double far = [self controlValue:NoiseControlThunderMaxDistance];
  double u = arc4random() / 4294967296.0;
  double distance = sqrt(near * near + u * (far * far - near * near));
  double angle = 2.0 * M_PI * (arc4random() / 4294967296.0);
  atomic_store_explicit(&_strikeDistance, (float)distance, memory_order_relaxed);
  atomic_store_explicit(&_strikeAngle, (float)angle, memory_order_relaxed);
  atomic_store_explicit(&_strikeRequested, 1, memory_order_release);
  _statusLabel.stringValue = _playing ?
      [NSString stringWithFormat:@"Strike at %.0f m", distance] :
      @"Strike queued until playback starts";
}

- (BOOL)parseSeed:(uint32_t *)seed {
  const char *text = _seedField.stringValue.UTF8String;
  char *end;
  errno = 0;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (!text[0] || text[0] < '0' || text[0] > '9' || *end || errno == ERANGE ||
      parsed > UINT32_MAX) {
    _statusLabel.stringValue = @"Seed must be an integer from 0 to 4294967295";
    return NO;
  }
  *seed = (uint32_t)parsed;
  return YES;
}

- (void)exportAudio:(NSButton *)sender {
  (void)sender;
  uint32_t seed;
  if (![self parseSeed:&seed]) return;
  noise_config config = [self configFromControls];

  NSTextField *durationLabel = [NSTextField labelWithString:@"Duration (s)"];
  NSTextField *durationField = [NSTextField textFieldWithString:@"60"];
  [durationField.widthAnchor constraintEqualToConstant:80.0].active = YES;
  NSStackView *accessory = [NSStackView stackViewWithViews:@[durationLabel, durationField]];
  accessory.edgeInsets = NSEdgeInsetsMake(8.0, 8.0, 8.0, 8.0);
  NSSavePanel *panel = [NSSavePanel savePanel];
  panel.nameFieldStringValue = @"noise.m4a";
  panel.accessoryView = accessory;
  if ([panel runModal] != NSModalResponseOK) return;

  char *end;
  const char *text = durationField.stringValue.UTF8String;
  double seconds = strtod(text, &end);
  if (!text[0] || *end || !(seconds >= 1.0 && seconds <= 86400.0)) {
    _statusLabel.stringValue = @"Duration must be from 1 to 86400 seconds";
    return;
  }
  uint32_t frames = (uint32_t)(seconds * NOISE_SAMPLE_RATE_HZ);
  NSURL *url = panel.URL;
  if (![url.pathExtension.lowercaseString isEqualToString:@"m4a"]) {
    url = [url URLByAppendingPathExtension:@"m4a"];
  }
  _exportButton.enabled = NO;
  _statusLabel.stringValue = @"Exporting…";
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    OSStatus status = export_m4a(url, &config, seed, frames, ^(uint32_t done) {
      dispatch_async(dispatch_get_main_queue(), ^{
        self->_statusLabel.stringValue = [NSString stringWithFormat:@"Exporting… %.0f%%",
                                          100.0 * done / frames];
      });
    });
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_exportButton.enabled = YES;
      self->_statusLabel.stringValue = status == noErr ?
          [NSString stringWithFormat:@"Exported %.0f s to %@", seconds, url.lastPathComponent] :
          [NSString stringWithFormat:@"Export error: %d", status];
    });
  });
}

- (void)resetGenerator:(NSButton *)sender {
  (void)sender;
  uint32_t seed;
  if (![self parseSeed:&seed]) return;

  BOOL resume = _playing;
  if (resume) AudioOutputUnitStop(_audioUnit);
  noise_config config = [self configFromControls];
  noise_result result = noise_init(&_generator, &config, seed);
  _appliedRainControl = config.weather.intensity;
  _appliedVary = config.weather.vary;
  if (result != NOISE_OK) {
    _playing = NO;
    _playButton.title = @"Start";
    _statusLabel.stringValue = @"Current parameters are invalid";
    return;
  }
  if (resume) AudioOutputUnitStart(_audioUnit);
  _statusLabel.stringValue = resume ? @"Playing from new seed" : @"Generator reset";
}

- (void)togglePlayback:(NSButton *)sender {
  (void)sender;
  OSStatus status;
  if (_playing) {
    status = AudioOutputUnitStop(_audioUnit);
    if (status == noErr) {
      _playing = NO;
      _playButton.title = @"Start";
      _statusLabel.stringValue = @"Stopped";
    }
  } else {
    status = AudioOutputUnitStart(_audioUnit);
    if (status == noErr) {
      _playing = YES;
      _playButton.title = @"Stop";
      _statusLabel.stringValue = @"Playing";
    }
  }
  if (status != noErr) {
    _statusLabel.stringValue = [NSString stringWithFormat:@"Audio error: %d", status];
  }
}

- (NSTextField *)sectionLabel:(NSString *)text {
  NSTextField *label = [NSTextField labelWithString:text];
  label.font = [NSFont boldSystemFontOfSize:13.0];
  return label;
}

- (NSView *)tabViewWithRows:(NSArray<NSView *> *)rows {
  NSView *view = [[NSView alloc] initWithFrame:NSZeroRect];
  NSStackView *stack = [NSStackView stackViewWithViews:rows];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 6.0;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
      [stack.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:14.0],
      [stack.topAnchor constraintEqualToAnchor:view.topAnchor constant:14.0]
  ]];
  return view;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  noise_config defaults;
  noise_config_default(&defaults);
  apply_startup_settings(&defaults);
  if (noise_init(&_generator, &defaults, 1) != NOISE_OK) {
    [NSApp terminate:nil];
    return;
  }
  atomic_init(&_varyRain, defaults.weather.vary);
  atomic_init(&_cicadaSpecies, (int)defaults.cicadas.species);
  atomic_init(&_strikeDistance, defaults.thunder.min_distance_m);
  atomic_init(&_strikeAngle, 0.0f);
  atomic_init(&_strikeRequested, 0);
  _appliedRainControl = defaults.weather.intensity;
  _appliedVary = defaults.weather.vary;

  _window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0, 650.0, 770.0)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _window.title = @"Noise Machine";
  [_window center];

  NSTextField *title = [NSTextField labelWithString:@"Noise Machine"];
  title.font = [NSFont boldSystemFontOfSize:22.0];
  NSTextField *subtitle = [NSTextField labelWithString:
      @"All engine parameters. Type exact values or use the sliders."];
  subtitle.textColor = NSColor.secondaryLabelColor;

  NSView *mixer = [self tabViewWithRows:@[
      [self sliderRow:@"White noise gain" control:NoiseControlWhite
                 value:defaults.ambient_gain[NOISE_KIND_WHITE] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Pink noise gain" control:NoiseControlPink
                 value:defaults.ambient_gain[NOISE_KIND_PINK] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"50 Hz hum gain" control:NoiseControlHum50
                 value:defaults.ambient_gain[NOISE_KIND_HUM_50HZ] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"60 Hz hum gain" control:NoiseControlHum60
                 value:defaults.ambient_gain[NOISE_KIND_HUM_60HZ] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Rain gain" control:NoiseControlRainGain
                 value:defaults.rain.gain minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Master gain" control:NoiseControlMaster
                 value:defaults.master_gain minimum:0 maximum:1 logarithmic:NO]
  ]];

  NSView *wind = [self tabViewWithRows:@[
      [self sectionLabel:@"Synthesized wind"],
      [self sliderRow:@"Gain" control:NoiseControlWindGain
                 value:defaults.wind.gain
               minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Brightness" control:NoiseControlWindBrightness
                 value:defaults.wind.brightness minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Gust depth" control:NoiseControlWindGustDepth
                 value:defaults.wind.gust_depth minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Gust rate (Hz)" control:NoiseControlWindGustRate
                 value:defaults.wind.gust_rate_hz minimum:0.01 maximum:2 logarithmic:YES],
      [self sliderRow:@"Stereo width" control:NoiseControlWindWidth
                 value:defaults.wind.stereo_width minimum:0 maximum:1 logarithmic:NO]
  ]];

  NSTextField *speciesLabel = [NSTextField labelWithString:@"Species"];
  speciesLabel.alignment = NSTextAlignmentRight;
  [speciesLabel.widthAnchor constraintEqualToConstant:145.0].active = YES;
  NSPopUpButton *speciesMenu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [speciesMenu addItemsWithTitles:@[@"Dog-day", @"Minminzemi", @"Higurashi"]];
  [speciesMenu selectItemAtIndex:defaults.cicadas.species];
  speciesMenu.target = self;
  speciesMenu.action = @selector(cicadaSpeciesChanged:);
  NSStackView *cicadaSpeciesRow = [NSStackView stackViewWithViews:@[speciesLabel, speciesMenu]];
  cicadaSpeciesRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  cicadaSpeciesRow.spacing = 10.0;

  NSView *insects = [self tabViewWithRows:@[
      [self sectionLabel:@"Crickets"],
      [self sliderRow:@"Gain" control:NoiseControlCricketGain
                 value:defaults.crickets.gain
               minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Chirps/s each" control:NoiseControlCricketCallRate
                 value:defaults.crickets.call_rate_hz minimum:0.05 maximum:10 logarithmic:YES],
      [self sliderRow:@"Pitch (Hz)" control:NoiseControlCricketPitch
                 value:defaults.crickets.pitch_hz minimum:2000 maximum:8000 logarithmic:YES],
      [self sliderRow:@"Pitch variation" control:NoiseControlCricketPitchVariation
                 value:defaults.crickets.pitch_variation minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Angular spread" control:NoiseControlCricketWidth
                 value:defaults.crickets.placement.stereo_width minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Min distance (m)" control:NoiseControlCricketMinDistance
                 value:defaults.crickets.placement.min_distance_m minimum:0.25 maximum:100 logarithmic:YES],
      [self sliderRow:@"Max distance (m)" control:NoiseControlCricketMaxDistance
                 value:defaults.crickets.placement.max_distance_m minimum:0.25 maximum:100 logarithmic:YES],
      [self sectionLabel:@"Cicadas"],
      cicadaSpeciesRow,
      [self sliderRow:@"Gain" control:NoiseControlCicadaGain
                 value:defaults.cicadas.gain
               minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Pitch (Hz)" control:NoiseControlCicadaPitch
                 value:defaults.cicadas.pitch_hz minimum:2000 maximum:10000 logarithmic:YES],
      [self sliderRow:@"Click rate ×" control:NoiseControlCicadaClickRate
                 value:defaults.cicadas.click_rate_scale minimum:0.5 maximum:1.5 logarithmic:NO],
      [self sliderRow:@"Distant chorus" control:NoiseControlCicadaChorus
                 value:defaults.cicadas.chorus minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Angular spread" control:NoiseControlCicadaWidth
                 value:defaults.cicadas.placement.stereo_width minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Min distance (m)" control:NoiseControlCicadaMinDistance
                 value:defaults.cicadas.placement.min_distance_m minimum:0.25 maximum:100 logarithmic:YES],
      [self sliderRow:@"Max distance (m)" control:NoiseControlCicadaMaxDistance
                 value:defaults.cicadas.placement.max_distance_m minimum:0.25 maximum:100 logarithmic:YES]
  ]];

  NSButton *strikeButton = [NSButton buttonWithTitle:@"Strike"
                                              target:self action:@selector(strikeThunder:)];
  NSView *thunder = [self tabViewWithRows:@[
      [self sectionLabel:@"Thunder"],
      [self sliderRow:@"Gain" control:NoiseControlThunderGain
                 value:defaults.thunder.gain minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Strikes per minute" control:NoiseControlThunderRate
                 value:defaults.thunder.rate_per_min minimum:0 maximum:20 logarithmic:NO],
      [self sliderRow:@"Minimum distance (m)" control:NoiseControlThunderMinDistance
                 value:defaults.thunder.min_distance_m
               minimum:200 maximum:15000 logarithmic:YES],
      [self sliderRow:@"Maximum distance (m)" control:NoiseControlThunderMaxDistance
                 value:defaults.thunder.max_distance_m
               minimum:200 maximum:15000 logarithmic:YES],
      [self sliderRow:@"Reverb gain" control:NoiseControlThunderReverbGain
                 value:defaults.thunder.reverb_gain minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Reverb decay (s)" control:NoiseControlThunderReverbDecay
                 value:defaults.thunder.reverb_decay_s minimum:0.5 maximum:10 logarithmic:YES],
      strikeButton
  ]];

  _varyButton = [NSButton checkboxWithTitle:@"Vary rain automatically"
                                     target:self action:@selector(varyChanged:)];
  _varyButton.state = defaults.weather.vary ? NSControlStateValueOn : NSControlStateValueOff;
  NSTextField *seedLabel = [NSTextField labelWithString:@"Seed"];
  seedLabel.alignment = NSTextAlignmentRight;
  [seedLabel.widthAnchor constraintEqualToConstant:145.0].active = YES;
  _seedField = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _seedField.stringValue = @"1";
  [_seedField.widthAnchor constraintEqualToConstant:130.0].active = YES;
  NSButton *resetButton = [NSButton buttonWithTitle:@"Reset generator"
                                             target:self action:@selector(resetGenerator:)];
  NSStackView *seedRow = [NSStackView stackViewWithViews:@[seedLabel, _seedField, resetButton]];
  seedRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  seedRow.alignment = NSLayoutAttributeCenterY;
  seedRow.spacing = 10.0;

  NSView *rain = [self tabViewWithRows:@[
      _varyButton,
      [self sectionLabel:@"Weather"],
      [self sliderRow:@"Rain intensity" control:NoiseControlRainIntensity
                 value:defaults.weather.intensity minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Minimum intensity" control:NoiseControlMinRain
                 value:defaults.weather.min_intensity minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Maximum intensity" control:NoiseControlMaxRain
                 value:defaults.weather.max_intensity minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Weather interval (s)" control:NoiseControlWeatherStep
                 value:defaults.weather.step_s minimum:0.1 maximum:3600 logarithmic:YES],
      [self sliderRow:@"Rain slew (s)" control:NoiseControlRainSlew
                 value:defaults.weather.slew_s minimum:0.01 maximum:60 logarithmic:YES],
      [self sliderRow:@"Drops/s at full rain" control:NoiseControlDropRate
                 value:defaults.rain.max_drops_per_s minimum:0 maximum:2000 logarithmic:NO],
      [self sliderRow:@"Fall height (m)" control:NoiseControlFallHeight
                 value:defaults.rain.fall_height_m minimum:0.01 maximum:1000 logarithmic:YES],
      [self sectionLabel:@"Surface weights"],
      [self sliderRow:@"Water" control:NoiseControlWaterWeight
                 value:defaults.rain.surface_weight[WATER] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Dirt" control:NoiseControlDirtWeight
                 value:defaults.rain.surface_weight[DIRT] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Leaf" control:NoiseControlLeafWeight
                 value:defaults.rain.surface_weight[LEAF] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Concrete" control:NoiseControlConcreteWeight
                 value:defaults.rain.surface_weight[CONCRETE] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Glass" control:NoiseControlGlassWeight
                 value:defaults.rain.surface_weight[GLASS] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Metal" control:NoiseControlMetalWeight
                 value:defaults.rain.surface_weight[METAL] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Plastic" control:NoiseControlPlasticWeight
                 value:defaults.rain.surface_weight[PLASTIC] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Asphalt" control:NoiseControlAsphaltWeight
                 value:defaults.rain.surface_weight[ASPHALT] minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Asphalt roof" control:NoiseControlAsphaltRoofWeight
                 value:defaults.rain.surface_weight[ASPHALT_ROOF] minimum:0 maximum:1 logarithmic:NO],
      seedRow
  ]];

  NSView *spatial = [self tabViewWithRows:@[
      [self sliderRow:@"Minimum distance (m)" control:NoiseControlMinDistance
                 value:defaults.rain.min_distance_m minimum:0.25 maximum:100 logarithmic:YES],
      [self sliderRow:@"Maximum distance (m)" control:NoiseControlMaxDistance
                 value:defaults.rain.max_distance_m minimum:0.25 maximum:100 logarithmic:YES],
      [self sliderRow:@"Stereo width (m)" control:NoiseControlStereoWidth
                 value:defaults.listener.stereo_width_m minimum:0 maximum:0.5 logarithmic:NO],
      [self sliderRow:@"Head effect" control:NoiseControlHead
                 value:defaults.listener.head_amount minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Rear filter" control:NoiseControlRear
                 value:defaults.listener.rear_amount minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Reverb gain" control:NoiseControlReverb
                 value:defaults.reverb_gain minimum:0 maximum:1 logarithmic:NO]
  ]];

  NSView *water = [self tabViewWithRows:@[
      [self sectionLabel:@"Random range per water drop"],
      [self sliderRow:@"Impact gain minimum" control:NoiseControlWaterImpactMin
                 value:defaults.rain.water.impact_gain_min minimum:0 maximum:2 logarithmic:NO],
      [self sliderRow:@"Impact gain maximum" control:NoiseControlWaterImpactMax
                 value:defaults.rain.water.impact_gain_max minimum:0 maximum:2 logarithmic:NO],
      [self sliderRow:@"Bubble probability" control:NoiseControlWaterBubbleProbability
                 value:defaults.rain.water.bubble_probability minimum:0 maximum:1 logarithmic:NO],
      [self sliderRow:@"Bubble radius min (mm)" control:NoiseControlWaterBubbleRadiusMin
                 value:1000.0 * defaults.rain.water.bubble_radius_min_m
               minimum:0.16 maximum:4 logarithmic:YES],
      [self sliderRow:@"Bubble radius max (mm)" control:NoiseControlWaterBubbleRadiusMax
                 value:1000.0 * defaults.rain.water.bubble_radius_max_m
               minimum:0.16 maximum:4 logarithmic:YES],
      [self sliderRow:@"Bubble gain minimum" control:NoiseControlWaterBubbleGainMin
                 value:defaults.rain.water.bubble_gain_min minimum:0 maximum:8 logarithmic:NO],
      [self sliderRow:@"Bubble gain maximum" control:NoiseControlWaterBubbleGainMax
                 value:defaults.rain.water.bubble_gain_max minimum:0 maximum:8 logarithmic:NO],
      [self sliderRow:@"Decay scale minimum" control:NoiseControlWaterBubbleDecayMin
                 value:defaults.rain.water.bubble_decay_min minimum:0.25 maximum:20 logarithmic:YES],
      [self sliderRow:@"Decay scale maximum" control:NoiseControlWaterBubbleDecayMax
                 value:defaults.rain.water.bubble_decay_max minimum:0.25 maximum:20 logarithmic:YES]
  ]];

  NSView *weatherMod = [self tabViewWithRows:@[
      [self sectionLabel:@"Weather intensity attenuverters"],
      [NSTextField labelWithString:@"+ follows intensity     0 disconnects     - inverts"],
      [self sliderRow:@"Arrival density" control:NoiseControlModArrival
                 value:defaults.weather.mod_amount[WEATHER_MOD_ARRIVAL_RATE]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Drop size" control:NoiseControlModSize
                 value:defaults.weather.mod_amount[WEATHER_MOD_DROP_SIZE]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Rain gain" control:NoiseControlModRainGain
                 value:defaults.weather.mod_amount[WEATHER_MOD_RAIN_GAIN]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Reverb gain" control:NoiseControlModReverb
                 value:defaults.weather.mod_amount[WEATHER_MOD_REVERB_GAIN]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Fall height" control:NoiseControlModFallHeight
                 value:defaults.weather.mod_amount[WEATHER_MOD_FALL_HEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Minimum distance" control:NoiseControlModMinDistance
                 value:defaults.weather.mod_amount[WEATHER_MOD_MIN_DISTANCE]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Maximum distance" control:NoiseControlModMaxDistance
                 value:defaults.weather.mod_amount[WEATHER_MOD_MAX_DISTANCE]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sectionLabel:@"Surface weights"],
      [self sliderRow:@"Water" control:NoiseControlModWater
                 value:defaults.weather.mod_amount[WEATHER_MOD_WATER_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Dirt" control:NoiseControlModDirt
                 value:defaults.weather.mod_amount[WEATHER_MOD_DIRT_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Leaf" control:NoiseControlModLeaf
                 value:defaults.weather.mod_amount[WEATHER_MOD_LEAF_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Concrete" control:NoiseControlModConcrete
                 value:defaults.weather.mod_amount[WEATHER_MOD_CONCRETE_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Glass" control:NoiseControlModGlass
                 value:defaults.weather.mod_amount[WEATHER_MOD_GLASS_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Metal" control:NoiseControlModMetal
                 value:defaults.weather.mod_amount[WEATHER_MOD_METAL_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Plastic" control:NoiseControlModPlastic
                 value:defaults.weather.mod_amount[WEATHER_MOD_PLASTIC_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Asphalt" control:NoiseControlModAsphalt
                 value:defaults.weather.mod_amount[WEATHER_MOD_ASPHALT_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO],
      [self sliderRow:@"Asphalt roof" control:NoiseControlModAsphaltRoof
                 value:defaults.weather.mod_amount[WEATHER_MOD_ASPHALT_ROOF_WEIGHT]
               minimum:-1 maximum:1 logarithmic:NO]
  ]];

  NSTabView *tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
  for (NSArray *item in @[@[@"Mixer", mixer], @[@"Wind", wind], @[@"Insects", insects],
                           @[@"Thunder", thunder], @[@"Rain", rain], @[@"Water", water],
                           @[@"Weather Mod", weatherMod], @[@"Spatial", spatial]]) {
    NSTabViewItem *tab = [[NSTabViewItem alloc] initWithIdentifier:item[0]];
    tab.label = item[0];
    tab.view = item[1];
    [tabs addTabViewItem:tab];
  }
  [tabs.widthAnchor constraintEqualToConstant:610.0].active = YES;
  [tabs.heightAnchor constraintEqualToConstant:620.0].active = YES;

  _playButton = [NSButton buttonWithTitle:@"Start" target:self
                                   action:@selector(togglePlayback:)];
  _playButton.bezelStyle = NSBezelStyleRounded;
  _statusLabel = [NSTextField labelWithString:@"Ready"];
  _statusLabel.textColor = NSColor.secondaryLabelColor;
  _exportButton = [NSButton buttonWithTitle:@"Export…" target:self
                                     action:@selector(exportAudio:)];
  NSStackView *playback = [NSStackView stackViewWithViews:@[_playButton, _exportButton,
                                                            _statusLabel]];
  playback.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  playback.alignment = NSLayoutAttributeCenterY;
  playback.spacing = 12.0;

  NSStackView *root = [NSStackView stackViewWithViews:@[title, subtitle, tabs, playback]];
  root.orientation = NSUserInterfaceLayoutOrientationVertical;
  root.alignment = NSLayoutAttributeLeading;
  root.spacing = 8.0;
  root.translatesAutoresizingMaskIntoConstraints = NO;
  [_window.contentView addSubview:root];
  [NSLayoutConstraint activateConstraints:@[
      [root.leadingAnchor constraintEqualToAnchor:_window.contentView.leadingAnchor
                                          constant:20.0],
      [root.topAnchor constraintEqualToAnchor:_window.contentView.topAnchor
                                      constant:16.0]
  ]];

  OSStatus status = [self setupAudio];
  if (status != noErr) {
    _playButton.enabled = NO;
    _statusLabel.stringValue = [NSString stringWithFormat:@"Audio setup error: %d", status];
  }
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  if (_playing) AudioOutputUnitStop(_audioUnit);
  if (_audioUnit) {
    AudioUnitUninitialize(_audioUnit);
    AudioComponentInstanceDispose(_audioUnit);
  }
}

@end

int main(void) {
  @autoreleasepool {
    NSApplication *application = [NSApplication sharedApplication];
    application.activationPolicy = NSApplicationActivationPolicyRegular;
    NoiseAppDelegate *delegate = [[NoiseAppDelegate alloc] init];
    application.delegate = delegate;
    [application run];
  }
  return 0;
}
