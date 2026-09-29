#import <Cocoa/Cocoa.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "audio_output.h"
#include "export_audio.h"
#include "gui_controls.h"
#include "noise_core.h"

@interface NoiseAppDelegate : NSObject <NSApplicationDelegate, NSTextFieldDelegate>
@end

@implementation NoiseAppDelegate {
  NSWindow *_window;
  NSButton *_playButton;
  NSButton *_exportButton;
  NSTextField *_statusLabel;
  NSTextField *_seedField;
  NSSlider *_sliders[CONTROL_COUNT];
  NSTextField *_valueFields[CONTROL_COUNT];
  noise_config _config; /* The settings shown; published whole to _output on each edit. */
  audio_output *_output;
  BOOL _playing;
}

- (void)publishConfig {
  if (!_output) return;
  if (audio_output_set_config(_output, &_config) != NOISE_OK) {
    _statusLabel.stringValue = @"Settings are invalid; playback keeps the last valid settings";
  }
}

- (NSString *)formattedControl:(gui_control_id)control {
  char text[32];
  gui_control_format(control, gui_control_get(&_config, control), text, sizeof(text));
  return @(text);
}

- (void)showControl:(gui_control_id)control {
  _sliders[control].doubleValue = gui_slider_position(control, gui_control_get(&_config, control));
  _valueFields[control].stringValue = [self formattedControl:control];
}

/* Shows every control whose value differs from previous, and always shows edited
   so its field reflects clamping. */
- (void)showChangesFrom:(const noise_config *)previous edited:(gui_control_id)edited {
  for (gui_control_id control = 0; control < CONTROL_COUNT; ++control) {
    if (control == edited ||
        gui_control_get(previous, control) != gui_control_get(&_config, control)) {
      [self showControl:control];
    }
  }
}

- (void)setControl:(gui_control_id)control value:(float)value {
  noise_config previous = _config;
  const char *note = gui_control_set(&_config, control, value);
  if (note) _statusLabel.stringValue = @(note);
  [self showChangesFrom:&previous edited:control];
  [self publishConfig];
}

- (NSView *)row:(gui_control_id)control {
  const gui_control *info = &gui_controls[control];
  NSTextField *label = [NSTextField labelWithString:@(info->label)];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:145.0].active = YES;

  double minimum, maximum;
  gui_slider_range(control, &minimum, &maximum);
  NSSlider *slider = [NSSlider sliderWithValue:minimum minValue:minimum maxValue:maximum
                                        target:self action:@selector(sliderChanged:)];
  slider.tag = control;
  slider.continuous = YES;
  if (info->minimum == -1.0f && info->maximum == 1.0f) {
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
  /* %.6g weights such as 6.05624e-05 need 11 characters. */
  BOOL wide = info->scale == GUI_SCALE_WEIGHT;
  [field.widthAnchor constraintEqualToConstant:wide ? 100.0 : 72.0].active = YES;
  _valueFields[control] = field;
  [self showControl:control];

  NSStackView *row = [NSStackView stackViewWithViews:@[label, slider, field]];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.alignment = NSLayoutAttributeCenterY;
  row.spacing = 10.0;
  return row;
}

- (NSArray<NSView *> *)rowsFrom:(gui_control_id)first count:(unsigned)count {
  NSMutableArray<NSView *> *rows = [NSMutableArray arrayWithCapacity:count];
  for (unsigned i = 0; i < count; ++i) [rows addObject:[self row:first + i]];
  return rows;
}

- (void)sliderChanged:(NSSlider *)sender {
  gui_control_id control = (gui_control_id)sender.tag;
  [self setControl:control value:gui_slider_value(control, sender.doubleValue)];
}

- (void)valueFieldChanged:(NSTextField *)sender {
  gui_control_id control = (gui_control_id)sender.tag;
  float value;
  if (!gui_control_parse(control, sender.stringValue.UTF8String, &value)) {
    sender.stringValue = [self formattedControl:control];
    _statusLabel.stringValue = @"Enter a finite number";
    return;
  }
  [self setControl:control value:value];
}

- (void)controlTextDidEndEditing:(NSNotification *)notification {
  NSTextField *field = notification.object;
  if (field.tag >= 0 && field.tag < CONTROL_COUNT) [self valueFieldChanged:field];
}

- (void)cicadaSpeciesChanged:(NSPopUpButton *)sender {
  _config.cicadas.species = (cicada_species)sender.indexOfSelectedItem;
  [self publishConfig];
}

- (void)varyChanged:(NSButton *)sender {
  noise_config previous = _config;
  gui_set_vary(&_config, sender.state == NSControlStateValueOn);
  [self showChangesFrom:&previous edited:CONTROL_RAIN_INTENSITY];
  [self publishConfig];
}

- (void)strikeThunder:(NSButton *)sender {
  (void)sender;
  if (!_output) return;
  double near = _config.thunder.min_distance_m;
  double far = _config.thunder.max_distance_m;
  double u = arc4random() / 4294967296.0;
  double distance = sqrt(near * near + u * (far * far - near * near));
  double angle = 2.0 * M_PI * (arc4random() / 4294967296.0);
  audio_output_strike(_output, (position_polar){(float)distance, (float)angle});
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
  noise_config config = _config;

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
  if (!_output || ![self parseSeed:&seed]) return;
  audio_output_reset(_output, seed);
  _statusLabel.stringValue = _playing ? @"Playing from new seed" : @"Generator reset";
}

- (void)togglePlayback:(NSButton *)sender {
  (void)sender;
  OSStatus status;
  if (_playing) {
    status = audio_output_stop(_output);
    if (status == noErr) {
      _playing = NO;
      _playButton.title = @"Start";
      _statusLabel.stringValue = @"Stopped";
    }
  } else {
    status = audio_output_start(_output);
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

- (NSView *)cicadaSpeciesRow {
  NSTextField *label = [NSTextField labelWithString:@"Species"];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:145.0].active = YES;
  NSPopUpButton *menu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [menu addItemsWithTitles:@[@"Dog-day", @"Minminzemi", @"Higurashi"]];
  [menu selectItemAtIndex:_config.cicadas.species];
  menu.target = self;
  menu.action = @selector(cicadaSpeciesChanged:);
  NSStackView *row = [NSStackView stackViewWithViews:@[label, menu]];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.spacing = 10.0;
  return row;
}

- (NSView *)seedRow {
  NSTextField *label = [NSTextField labelWithString:@"Seed"];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:145.0].active = YES;
  _seedField = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _seedField.stringValue = @"1";
  [_seedField.widthAnchor constraintEqualToConstant:130.0].active = YES;
  NSButton *reset = [NSButton buttonWithTitle:@"Reset generator"
                                       target:self action:@selector(resetGenerator:)];
  NSStackView *row = [NSStackView stackViewWithViews:@[label, _seedField, reset]];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.alignment = NSLayoutAttributeCenterY;
  row.spacing = 10.0;
  return row;
}

- (NSTabView *)tabs {
  NSView *mixer = [self tabViewWithRows:@[
      [self row:CONTROL_WHITE], [self row:CONTROL_PINK], [self row:CONTROL_HUM_50HZ],
      [self row:CONTROL_HUM_60HZ], [self row:CONTROL_RAIN_GAIN], [self row:CONTROL_MASTER_GAIN]
  ]];

  NSMutableArray<NSView *> *windRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Synthesized wind"]];
  [windRows addObjectsFromArray:[self rowsFrom:CONTROL_WIND_GAIN count:5]];
  NSView *wind = [self tabViewWithRows:windRows];

  NSMutableArray<NSView *> *insectRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Crickets"]];
  [insectRows addObjectsFromArray:[self rowsFrom:CONTROL_CRICKET_GAIN count:7]];
  [insectRows addObject:[self sectionLabel:@"Cicadas"]];
  [insectRows addObject:[self cicadaSpeciesRow]];
  [insectRows addObjectsFromArray:[self rowsFrom:CONTROL_CICADA_GAIN count:7]];
  NSView *insects = [self tabViewWithRows:insectRows];

  NSMutableArray<NSView *> *thunderRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Thunder"]];
  [thunderRows addObjectsFromArray:[self rowsFrom:CONTROL_THUNDER_GAIN count:6]];
  [thunderRows addObject:[NSButton buttonWithTitle:@"Strike"
                                            target:self action:@selector(strikeThunder:)]];
  NSView *thunder = [self tabViewWithRows:thunderRows];

  NSButton *vary = [NSButton checkboxWithTitle:@"Vary rain automatically"
                                        target:self action:@selector(varyChanged:)];
  vary.state = _config.weather.vary ? NSControlStateValueOn : NSControlStateValueOff;
  NSMutableArray<NSView *> *rainRows = [NSMutableArray arrayWithObjects:
      vary, [self sectionLabel:@"Weather"], nil];
  [rainRows addObjectsFromArray:[self rowsFrom:CONTROL_RAIN_INTENSITY count:5]];
  [rainRows addObject:[self row:CONTROL_DROP_RATE]];
  [rainRows addObject:[self row:CONTROL_FALL_HEIGHT]];
  [rainRows addObject:[self sectionLabel:@"Surface weights"]];
  [rainRows addObjectsFromArray:[self rowsFrom:CONTROL_SURFACE_WEIGHT count:NOISE_SURFACE_COUNT]];
  [rainRows addObject:[self seedRow]];
  NSView *rain = [self tabViewWithRows:rainRows];

  NSMutableArray<NSView *> *waterRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Random range per water drop"]];
  [waterRows addObjectsFromArray:[self rowsFrom:CONTROL_WATER_IMPACT_MIN count:9]];
  NSView *water = [self tabViewWithRows:waterRows];

  NSMutableArray<NSView *> *modRows = [NSMutableArray arrayWithObjects:
      [self sectionLabel:@"Weather intensity attenuverters"],
      [NSTextField labelWithString:@"+ follows intensity     0 disconnects     - inverts"], nil];
  [modRows addObjectsFromArray:[self rowsFrom:CONTROL_WEATHER_MOD count:WEATHER_MOD_WATER_WEIGHT]];
  [modRows addObject:[self sectionLabel:@"Surface weights"]];
  [modRows addObjectsFromArray:[self rowsFrom:CONTROL_WEATHER_MOD + WEATHER_MOD_WATER_WEIGHT
                                        count:NOISE_SURFACE_COUNT]];
  NSView *weatherMod = [self tabViewWithRows:modRows];

  NSView *spatial = [self tabViewWithRows:@[
      [self row:CONTROL_RAIN_MIN_DISTANCE], [self row:CONTROL_RAIN_MAX_DISTANCE],
      [self row:CONTROL_STEREO_WIDTH], [self row:CONTROL_HEAD], [self row:CONTROL_REAR],
      [self row:CONTROL_REVERB_GAIN]
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
  return tabs;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  gui_startup_config(&_config);

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
  NSTabView *tabs = [self tabs];

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

  OSStatus status = audio_output_create(&_output, &_config, 1);
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
  audio_output_destroy(_output);
  _output = NULL;
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
