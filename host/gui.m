#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>

#include <stdatomic.h>
#include <string.h>

#include "noise_core.h"

typedef NS_ENUM(NSInteger, NoiseControl) {
  NoiseControlWhite,
  NoiseControlPink,
  NoiseControlHum50,
  NoiseControlHum60,
  NoiseControlRainIntensity,
  NoiseControlRainGain,
  NoiseControlDropRate,
  NoiseControlReverb,
  NoiseControlHead,
  NoiseControlRear,
  NoiseControlStereoWidth,
  NoiseControlMaster,
  NoiseControlCount
};

@interface NoiseAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation NoiseAppDelegate {
  NSWindow *_window;
  NSButton *_playButton;
  NSTextField *_statusLabel;
  NSTextField *_valueLabels[NoiseControlCount];
  AudioUnit _audioUnit;
  noise_gen _generator;
  _Atomic(float) _controls[NoiseControlCount];
  _Atomic(int) _surface;
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

- (void)loadControlsIntoGenerator {
  _generator.config.ambient_gain[NOISE_KIND_WHITE] =
      atomic_load_explicit(&_controls[NoiseControlWhite], memory_order_relaxed);
  _generator.config.ambient_gain[NOISE_KIND_PINK] =
      atomic_load_explicit(&_controls[NoiseControlPink], memory_order_relaxed);
  _generator.config.ambient_gain[HUM_50HZ] =
      atomic_load_explicit(&_controls[NoiseControlHum50], memory_order_relaxed);
  _generator.config.ambient_gain[HUM_60HZ] =
      atomic_load_explicit(&_controls[NoiseControlHum60], memory_order_relaxed);
  float rain = atomic_load_explicit(&_controls[NoiseControlRainIntensity],
                                    memory_order_relaxed);
  _generator.config.rain_intensity = rain;
  _generator.state.rain_intensity = rain;
  _generator.state.rain_target = rain;
  _generator.config.rain_gain =
      atomic_load_explicit(&_controls[NoiseControlRainGain], memory_order_relaxed);
  _generator.config.max_drops_per_s =
      atomic_load_explicit(&_controls[NoiseControlDropRate], memory_order_relaxed);
  _generator.config.reverb_gain =
      atomic_load_explicit(&_controls[NoiseControlReverb], memory_order_relaxed);
  _generator.config.head_amount =
      atomic_load_explicit(&_controls[NoiseControlHead], memory_order_relaxed);
  _generator.config.rear_amount =
      atomic_load_explicit(&_controls[NoiseControlRear], memory_order_relaxed);
  _generator.config.stereo_width_m =
      atomic_load_explicit(&_controls[NoiseControlStereoWidth], memory_order_relaxed);
  _generator.config.master_gain =
      atomic_load_explicit(&_controls[NoiseControlMaster], memory_order_relaxed);

  int surface = atomic_load_explicit(&_surface, memory_order_relaxed);
  static const float mixed[NOISE_SURFACE_COUNT] = {
      0.37f, 0.21f, 0.26f, 0.15f, 0.005f, 0.005f};
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    float weight = surface == 0 ? mixed[i] : ((int)i == surface - 1 ? 1.0f : 0.0f);
    _generator.config.surface_weight[i] = weight;
    sum += weight;
    _generator.surface_cdf[i] = sum;
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

- (NSView *)sliderRow:(NSString *)name control:(NoiseControl)control
                 value:(double)value minimum:(double)minimum maximum:(double)maximum {
  NSTextField *label = [NSTextField labelWithString:name];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:120.0].active = YES;

  NSSlider *slider = [NSSlider sliderWithValue:value minValue:minimum maxValue:maximum
                                        target:self action:@selector(controlChanged:)];
  slider.tag = control;
  slider.continuous = YES;
  [slider.widthAnchor constraintEqualToConstant:270.0].active = YES;

  NSTextField *valueLabel = [NSTextField labelWithString:@""];
  valueLabel.font = [NSFont monospacedDigitSystemFontOfSize:12.0
                                                    weight:NSFontWeightRegular];
  [valueLabel.widthAnchor constraintEqualToConstant:62.0].active = YES;
  _valueLabels[control] = valueLabel;
  [self updateValueLabel:control value:value];
  atomic_init(&_controls[control], (float)value);

  NSStackView *row = [NSStackView stackViewWithViews:@[label, slider, valueLabel]];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.alignment = NSLayoutAttributeCenterY;
  row.spacing = 10.0;
  return row;
}

- (void)updateValueLabel:(NoiseControl)control value:(double)value {
  if (control == NoiseControlDropRate) {
    _valueLabels[control].stringValue = [NSString stringWithFormat:@"%.0f", value];
  } else if (control == NoiseControlStereoWidth) {
    _valueLabels[control].stringValue = [NSString stringWithFormat:@"%.3f", value];
  } else {
    _valueLabels[control].stringValue = [NSString stringWithFormat:@"%.2f", value];
  }
}

- (void)controlChanged:(NSSlider *)sender {
  NoiseControl control = (NoiseControl)sender.tag;
  atomic_store_explicit(&_controls[control], (float)sender.doubleValue,
                        memory_order_relaxed);
  [self updateValueLabel:control value:sender.doubleValue];
}

- (void)surfaceChanged:(NSPopUpButton *)sender {
  atomic_store_explicit(&_surface, (int)sender.indexOfSelectedItem,
                        memory_order_relaxed);
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

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  noise_config config;
  noise_config_default(&config);
  config.rain_intensity = 0.5f;
  if (noise_init(&_generator, &config, 1) != NOISE_OK) {
    [NSApp terminate:nil];
    return;
  }
  atomic_init(&_surface, 0);

  _window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0, 520.0, 610.0)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _window.title = @"Noise Machine";
  [_window center];

  NSTextField *title = [NSTextField labelWithString:@"Noise Machine"];
  title.font = [NSFont boldSystemFontOfSize:22.0];
  NSTextField *subtitle = [NSTextField labelWithString:
      @"Changes apply continuously. Head and surface changes affect new drops."];
  subtitle.textColor = NSColor.secondaryLabelColor;

  NSStackView *controls = [NSStackView stackViewWithViews:@[
      title,
      subtitle,
      [self sliderRow:@"White noise" control:NoiseControlWhite
                 value:config.ambient_gain[NOISE_KIND_WHITE] minimum:0 maximum:1],
      [self sliderRow:@"Pink noise" control:NoiseControlPink
                 value:config.ambient_gain[NOISE_KIND_PINK] minimum:0 maximum:1],
      [self sliderRow:@"50 Hz hum" control:NoiseControlHum50
                 value:config.ambient_gain[HUM_50HZ] minimum:0 maximum:1],
      [self sliderRow:@"60 Hz hum" control:NoiseControlHum60
                 value:config.ambient_gain[HUM_60HZ] minimum:0 maximum:1],
      [self sliderRow:@"Rain intensity" control:NoiseControlRainIntensity
                 value:config.rain_intensity minimum:0 maximum:1],
      [self sliderRow:@"Rain gain" control:NoiseControlRainGain
                 value:config.rain_gain minimum:0 maximum:1],
      [self sliderRow:@"Drop rate" control:NoiseControlDropRate
                 value:config.max_drops_per_s minimum:0 maximum:2000],
      [self sliderRow:@"Reverb" control:NoiseControlReverb
                 value:config.reverb_gain minimum:0 maximum:1],
      [self sliderRow:@"Head effect" control:NoiseControlHead
                 value:config.head_amount minimum:0 maximum:1],
      [self sliderRow:@"Rear filter" control:NoiseControlRear
                 value:config.rear_amount minimum:0 maximum:1],
      [self sliderRow:@"Stereo width (m)" control:NoiseControlStereoWidth
                 value:config.stereo_width_m minimum:0 maximum:0.5],
      [self sliderRow:@"Master gain" control:NoiseControlMaster
                 value:config.master_gain minimum:0 maximum:1]
  ]];
  controls.orientation = NSUserInterfaceLayoutOrientationVertical;
  controls.alignment = NSLayoutAttributeLeading;
  controls.spacing = 8.0;

  NSTextField *surfaceLabel = [NSTextField labelWithString:@"Surface"];
  surfaceLabel.alignment = NSTextAlignmentRight;
  [surfaceLabel.widthAnchor constraintEqualToConstant:120.0].active = YES;
  NSPopUpButton *surface = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [surface addItemsWithTitles:@[@"Mixed", @"Water", @"Dirt", @"Leaf",
                                @"Concrete", @"Glass", @"Metal"]];
  surface.target = self;
  surface.action = @selector(surfaceChanged:);
  [surface.widthAnchor constraintEqualToConstant:180.0].active = YES;
  NSStackView *surfaceRow = [NSStackView stackViewWithViews:@[surfaceLabel, surface]];
  surfaceRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  surfaceRow.alignment = NSLayoutAttributeCenterY;
  surfaceRow.spacing = 10.0;
  [controls addArrangedSubview:surfaceRow];

  _playButton = [NSButton buttonWithTitle:@"Start" target:self
                                   action:@selector(togglePlayback:)];
  _playButton.bezelStyle = NSBezelStyleRounded;
  _statusLabel = [NSTextField labelWithString:@"Ready"];
  _statusLabel.textColor = NSColor.secondaryLabelColor;
  NSStackView *playback = [NSStackView stackViewWithViews:@[_playButton, _statusLabel]];
  playback.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  playback.alignment = NSLayoutAttributeCenterY;
  playback.spacing = 12.0;
  [controls addArrangedSubview:playback];

  controls.translatesAutoresizingMaskIntoConstraints = NO;
  [_window.contentView addSubview:controls];
  [NSLayoutConstraint activateConstraints:@[
      [controls.leadingAnchor constraintEqualToAnchor:_window.contentView.leadingAnchor
                                             constant:20.0],
      [controls.topAnchor constraintEqualToAnchor:_window.contentView.topAnchor
                                          constant:18.0]
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
