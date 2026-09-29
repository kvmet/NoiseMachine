#import <Cocoa/Cocoa.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "audio_output.h"
#include "export_audio.h"
#include "gui_controls.h"
#include "noise_core.h"

#define LABEL_WIDTH 170.0
#define COMPACT_LABEL_WIDTH 170.0

@interface NoiseAppDelegate : NSObject <NSApplicationDelegate, NSTextFieldDelegate>
@end

@implementation NoiseAppDelegate {
  NSWindow *_window;
  NSButton *_playButton;
  NSButton *_exportButton;
  NSTextField *_statusLabel;
  NSTextField *_weatherLabel;
  NSTextField *_silencedLabel;
  NSTextField *_layerLabels[GUI_LAYER_COUNT];
  NSTimer *_statusTimer;
  noise_status _status; /* Latest from the render thread. */
  BOOL _haveStatus;
  NSTextField *_seedField;
  NSSlider *_sliders[CONTROL_COUNT];
  NSTextField *_valueFields[CONTROL_COUNT];
  noise_config _config; /* The settings shown; published whole to _output on each edit. */
  unsigned _surface;    /* Surface shown by GUI_SCOPE_SURFACE controls. */
  NSMutableArray<NSPopUpButton *> *_surfacePickers;
  NSTextField *_nameField;
  NSButton *_addButton;
  NSButton *_deleteButton;
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
  gui_control_format(control, gui_control_get(&_config, _surface, control), text, sizeof(text));
  return @(text);
}

- (void)showControl:(gui_control_id)control {
  float value = gui_control_get(&_config, _surface, control);
  _sliders[control].doubleValue = gui_slider_position(control, value);
  _valueFields[control].stringValue = [self formattedControl:control];
}

/* Shows every control whose value differs from previous, and always shows edited
   so its field reflects clamping. */
- (void)showChangesFrom:(const noise_config *)previous edited:(gui_control_id)edited {
  for (gui_control_id control = 0; control < CONTROL_COUNT; ++control) {
    if (control == edited ||
        gui_control_get(previous, _surface, control) != gui_control_get(&_config, _surface, control)) {
      [self showControl:control];
    }
  }
}

- (void)showSurfaceControls {
  for (gui_control_id control = 0; control < CONTROL_COUNT; ++control) {
    if (gui_controls[control].scope == GUI_SCOPE_SURFACE) [self showControl:control];
  }
}

- (void)setControl:(gui_control_id)control value:(float)value {
  noise_config previous = _config;
  const char *note = gui_control_set(&_config, _surface, control, value);
  if (note) _statusLabel.stringValue = @(note);
  [self showChangesFrom:&previous edited:control];
  if (control == CONTROL_SURFACE_COVERAGE) [self showSurfaceList];
  [self publishConfig];
}

- (NSTextField *)rowLabel:(NSString *)text width:(CGFloat)width {
  NSTextField *label = [NSTextField labelWithString:text];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:width].active = YES;
  return label;
}

- (NSStackView *)rowWithViews:(NSArray<NSView *> *)views {
  NSStackView *row = [NSStackView stackViewWithViews:views];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.alignment = NSLayoutAttributeCenterY;
  row.spacing = 10.0;
  return row;
}

- (NSArray<NSView *> *)sliderAndField:(gui_control_id)control width:(CGFloat)width {
  const gui_control *info = &gui_controls[control];
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
  [slider.widthAnchor constraintEqualToConstant:width].active = YES;
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
  BOOL wide = info->scale == GUI_SCALE_LOG_OFF && info->smallest < 1e-3f;
  [field.widthAnchor constraintEqualToConstant:wide ? 100.0 : 72.0].active = YES;
  _valueFields[control] = field;
  [self showControl:control];
  return @[slider, field];
}

- (NSView *)row:(gui_control_id)control labelWidth:(CGFloat)labelWidth
    sliderWidth:(CGFloat)sliderWidth {
  NSMutableArray<NSView *> *views = [NSMutableArray arrayWithObject:
      [self rowLabel:@(gui_controls[control].label) width:labelWidth]];
  [views addObjectsFromArray:[self sliderAndField:control width:sliderWidth]];
  return [self rowWithViews:views];
}

- (NSView *)row:(gui_control_id)control {
  return [self row:control labelWidth:LABEL_WIDTH sliderWidth:280.0];
}

/* Narrower rows for tabs with two columns. */
- (NSView *)compactRow:(gui_control_id)control {
  return [self row:control labelWidth:COMPACT_LABEL_WIDTH sliderWidth:150.0];
}

- (NSArray<NSView *> *)rowsFrom:(gui_control_id)first count:(unsigned)count {
  NSMutableArray<NSView *> *rows = [NSMutableArray arrayWithCapacity:count];
  for (unsigned i = 0; i < count; ++i) [rows addObject:[self row:first + i]];
  return rows;
}

- (NSArray<NSView *> *)compactRowsFrom:(gui_control_id)first count:(unsigned)count {
  NSMutableArray<NSView *> *rows = [NSMutableArray arrayWithCapacity:count];
  for (unsigned i = 0; i < count; ++i) [rows addObject:[self compactRow:first + i]];
  return rows;
}

/* A gain row followed by what the weather is doing to its layer. */
- (NSView *)mixerRow:(gui_control_id)control layer:(gui_layer)layer {
  NSMutableArray<NSView *> *views = [NSMutableArray arrayWithObject:
      [self rowLabel:@(gui_controls[control].label) width:LABEL_WIDTH]];
  [views addObjectsFromArray:[self sliderAndField:control width:220.0]];
  NSTextField *status = [NSTextField labelWithString:@""];
  status.font = [NSFont systemFontOfSize:11.0];
  status.textColor = NSColor.secondaryLabelColor;
  status.lineBreakMode = NSLineBreakByTruncatingTail;
  [status.widthAnchor constraintEqualToConstant:360.0].active = YES;
  _layerLabels[layer] = status;
  [views addObject:status];
  return [self rowWithViews:views];
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

/* Pickers list each surface with its share of the ground; duplicate names stay distinct. */
- (void)showSurfaceList {
  const noise_rain_config *rain = &_config.rain;
  float total = 0.0f;
  for (unsigned i = 0; i < rain->surface_count; ++i) total += rain->surface[i].coverage;
  for (NSPopUpButton *picker in _surfacePickers) {
    [picker.menu removeAllItems];
    for (unsigned i = 0; i < rain->surface_count; ++i) {
      NSString *title = [NSString stringWithFormat:@"%s (%.3g%%)", rain->surface[i].name,
                         100.0 * rain->surface[i].coverage / total];
      [picker.menu addItemWithTitle:title action:NULL keyEquivalent:@""];
    }
    [picker selectItemAtIndex:_surface];
  }
  _nameField.stringValue = @(rain->surface[_surface].name);
  _addButton.enabled = rain->surface_count < NOISE_MAX_SURFACES;
  _deleteButton.enabled = rain->surface_count > 1;
}

- (void)showSurface {
  [self showSurfaceList];
  [self showSurfaceControls];
}

- (void)surfaceChanged:(NSPopUpButton *)sender {
  _surface = (unsigned)sender.indexOfSelectedItem;
  [self showSurface];
}

- (void)addSurface:(NSButton *)sender {
  (void)sender;
  const char *note = gui_surface_add(&_config, _surface);
  if (note) {
    _statusLabel.stringValue = @(note);
    return;
  }
  _surface = _config.rain.surface_count - 1;
  _statusLabel.stringValue = [NSString stringWithFormat:@"Added %s",
                              _config.rain.surface[_surface].name];
  [self showSurface];
  [self publishConfig];
}

- (void)deleteSurface:(NSButton *)sender {
  (void)sender;
  NSString *name = @(_config.rain.surface[_surface].name);
  unsigned count = _config.rain.surface_count;
  const char *note = gui_surface_delete(&_config, _surface);
  if (_config.rain.surface_count < count) {
    _statusLabel.stringValue = [NSString stringWithFormat:@"Deleted %@", name];
  }
  if (note) _statusLabel.stringValue = @(note);
  if (_surface >= _config.rain.surface_count) _surface = _config.rain.surface_count - 1;
  [self showSurface];
  [self publishConfig];
}

- (void)renameSurface:(NSTextField *)sender {
  const char *note = gui_surface_rename(&_config, _surface, sender.stringValue.UTF8String);
  if (note) _statusLabel.stringValue = @(note);
  [self showSurfaceList];
  [self publishConfig];
}

- (void)cicadaSpeciesChanged:(NSPopUpButton *)sender {
  _config.cicadas.species = (cicada_species)sender.indexOfSelectedItem;
  [self publishConfig];
}

/* Only the controls of the active weather source respond. */
- (void)showWeatherSource {
  for (gui_control_id control = CONTROL_STORM_TIME_SCALE;
       control <= CONTROL_SHAPE_HEADING_SPREAD; ++control) {
    BOOL fixed = control >= CONTROL_FIXED_RAIN && control <= CONTROL_FIXED_CELL_BEARING;
    BOOL enabled = fixed == (_config.storm.manual != 0);
    _sliders[control].enabled = enabled;
    _valueFields[control].enabled = enabled;
  }
}

- (void)manualChanged:(NSButton *)sender {
  _config.storm.manual = sender.state == NSControlStateValueOn;
  [self showWeatherSource];
  [self publishConfig];
}

/* Redraws every readout from the latest status and the settings shown, so a gain
   edit updates its line even while stopped. */
- (void)showStatus:(NSTimer *)timer {
  (void)timer;
  if (_output && audio_output_status(_output, &_status)) _haveStatus = YES;
  if (!_haveStatus) return;
  char text[256];
  for (gui_layer layer = 0; layer < GUI_LAYER_COUNT; ++layer) {
    gui_layer_status(layer, &_status, &_config, text, sizeof(text));
    _layerLabels[layer].stringValue = @(text);
    _layerLabels[layer].toolTip = @(text);
  }
  gui_weather_summary(&_status, text, sizeof(text));
  _weatherLabel.stringValue = @(text);
  gui_silenced_summary(&_status, &_config, text, sizeof(text));
  _silencedLabel.stringValue = @(text);
}

/* Strikes land uniformly by area between these distances. */
#define STRIKE_NEAR_M 1000.0
#define STRIKE_FAR_M 8000.0

- (void)strikeThunder:(NSButton *)sender {
  (void)sender;
  if (!_output) return;
  double near = STRIKE_NEAR_M;
  double far = STRIKE_FAR_M;
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

- (NSPopUpButton *)surfacePicker {
  NSPopUpButton *picker = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  picker.target = self;
  picker.action = @selector(surfaceChanged:);
  [picker.widthAnchor constraintEqualToConstant:200.0].active = YES;
  [_surfacePickers addObject:picker];
  return picker;
}

- (NSView *)surfacePickerRow {
  return [self rowWithViews:@[[self rowLabel:@"Surface" width:LABEL_WIDTH],
                              [self surfacePicker]]];
}

- (NSView *)columnWithRows:(NSArray<NSView *> *)rows {
  NSStackView *column = [NSStackView stackViewWithViews:rows];
  column.orientation = NSUserInterfaceLayoutOrientationVertical;
  column.alignment = NSLayoutAttributeLeading;
  column.spacing = 6.0;
  return column;
}

- (NSView *)tabViewWithLeft:(NSArray<NSView *> *)left right:(NSArray<NSView *> *)right {
  NSStackView *columns = [NSStackView stackViewWithViews:@[[self columnWithRows:left],
                                                           [self columnWithRows:right]]];
  columns.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  columns.alignment = NSLayoutAttributeTop;
  columns.spacing = 24.0;
  return [self tabViewWithRows:@[columns]];
}

- (NSArray<NSView *> *)surfaceEditorRows {
  _addButton = [NSButton buttonWithTitle:@"Add" target:self action:@selector(addSurface:)];
  _deleteButton = [NSButton buttonWithTitle:@"Delete" target:self
                                     action:@selector(deleteSurface:)];
  NSView *picker = [self rowWithViews:@[[self rowLabel:@"Surface" width:LABEL_WIDTH],
                                        [self surfacePicker], _addButton, _deleteButton]];
  _nameField = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _nameField.target = self;
  _nameField.action = @selector(renameSurface:);
  _nameField.cell.sendsActionOnEndEditing = YES;
  [_nameField.widthAnchor constraintEqualToConstant:200.0].active = YES;
  NSView *name = [self rowWithViews:@[[self rowLabel:@"Name" width:LABEL_WIDTH], _nameField]];
  return @[picker, name, [self row:CONTROL_SURFACE_COVERAGE]];
}

- (NSView *)cicadaSpeciesRow {
  NSTextField *label = [NSTextField labelWithString:@"Species"];
  label.alignment = NSTextAlignmentRight;
  [label.widthAnchor constraintEqualToConstant:LABEL_WIDTH].active = YES;
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

- (NSArray<NSView *> *)seedViews {
  NSTextField *label = [NSTextField labelWithString:@"Seed"];
  _seedField = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _seedField.stringValue = @"1";
  [_seedField.widthAnchor constraintEqualToConstant:100.0].active = YES;
  NSButton *reset = [NSButton buttonWithTitle:@"Reset generator"
                                       target:self action:@selector(resetGenerator:)];
  return @[label, _seedField, reset];
}

- (NSTabView *)tabs {
  NSView *mixer = [self tabViewWithRows:@[
      [self row:CONTROL_MASTER_GAIN], [self row:CONTROL_REVERB_GAIN],
      [self sectionLabel:@"Weather-driven layers"],
      [self mixerRow:CONTROL_RAIN_GAIN layer:GUI_LAYER_RAIN],
      [self mixerRow:CONTROL_BED_GAIN layer:GUI_LAYER_BED],
      [self mixerRow:CONTROL_WIND_GAIN layer:GUI_LAYER_WIND],
      [self mixerRow:CONTROL_CRICKET_GAIN layer:GUI_LAYER_CRICKETS],
      [self mixerRow:CONTROL_CICADA_GAIN layer:GUI_LAYER_CICADAS],
      [self mixerRow:CONTROL_THUNDER_GAIN layer:GUI_LAYER_THUNDER],
      [self sectionLabel:@"Steady noise"],
      [self row:CONTROL_WHITE], [self row:CONTROL_PINK], [self row:CONTROL_HUM_50HZ],
      [self row:CONTROL_HUM_60HZ]
  ]];

  NSButton *manual = [NSButton checkboxWithTitle:@"Hold weather at fixed values"
                                          target:self action:@selector(manualChanged:)];
  manual.state = _config.storm.manual ? NSControlStateValueOn : NSControlStateValueOff;
  NSMutableArray<NSView *> *stormRows = [NSMutableArray arrayWithObjects:
      manual, [self sectionLabel:@"Simulated storms"], nil];
  [stormRows addObjectsFromArray:[self rowsFrom:CONTROL_STORM_TIME_SCALE count:7]];
  [stormRows addObject:[self sectionLabel:@"Fixed weather"]];
  [stormRows addObjectsFromArray:[self rowsFrom:CONTROL_FIXED_RAIN count:7]];
  NSView *storm = [self tabViewWithRows:stormRows];

  NSMutableArray<NSView *> *shapeLeft = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Rain"]];
  [shapeLeft addObjectsFromArray:[self compactRowsFrom:CONTROL_SHAPE_PEAK_RAIN_MIN count:7]];
  [shapeLeft addObject:[self sectionLabel:@"Gust front"]];
  [shapeLeft addObjectsFromArray:[self compactRowsFrom:CONTROL_SHAPE_FRONT_MIN count:7]];
  NSMutableArray<NSView *> *shapeRight = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Cooling"]];
  [shapeRight addObjectsFromArray:[self compactRowsFrom:CONTROL_SHAPE_COOLING_MIN count:6]];
  [shapeRight addObject:[self sectionLabel:@"Lightning"]];
  [shapeRight addObjectsFromArray:[self compactRowsFrom:CONTROL_SHAPE_LIGHTNING_MIN count:2]];
  [shapeRight addObject:[self sectionLabel:@"Track"]];
  [shapeRight addObjectsFromArray:[self compactRowsFrom:CONTROL_SHAPE_BUILD count:5]];
  NSView *shape = [self tabViewWithLeft:shapeLeft right:shapeRight];
  [self showWeatherSource];

  NSMutableArray<NSView *> *windRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Sound"]];
  [windRows addObjectsFromArray:@[[self row:CONTROL_WIND_WIDTH],
                                  [self row:CONTROL_WIND_BRIGHTNESS],
                                  [self row:CONTROL_WIND_RUMBLE],
                                  [self row:CONTROL_WIND_BALANCE]]];
  [windRows addObject:[self sectionLabel:@"Gusts"]];
  [windRows addObjectsFromArray:[self rowsFrom:CONTROL_GUST_INTENSITY count:2]];
  NSView *wind = [self tabViewWithRows:windRows];

  NSMutableArray<NSView *> *cricketRows = [NSMutableArray arrayWithObject:
      [self sectionLabel:@"Crickets"]];
  [cricketRows addObjectsFromArray:[self compactRowsFrom:CONTROL_CRICKET_CALL_RATE count:6]];
  [cricketRows addObjectsFromArray:[self compactRowsFrom:CONTROL_CRICKET_MIN_TEMPERATURE
                                                   count:3]];
  NSMutableArray<NSView *> *cicadaRows = [NSMutableArray arrayWithObjects:
      [self sectionLabel:@"Cicadas"], [self cicadaSpeciesRow], nil];
  [cicadaRows addObjectsFromArray:[self compactRowsFrom:CONTROL_CICADA_PITCH count:6]];
  [cicadaRows addObjectsFromArray:[self compactRowsFrom:CONTROL_CICADA_MIN_TEMPERATURE
                                                  count:2]];
  NSView *insects = [self tabViewWithLeft:cricketRows right:cicadaRows];

  NSMutableArray<NSView *> *thunderRows = [NSMutableArray arrayWithArray:
      [self rowsFrom:CONTROL_THUNDER_REVERB_GAIN count:3]];
  [thunderRows addObject:[NSButton buttonWithTitle:@"Strike"
                                            target:self action:@selector(strikeThunder:)]];
  NSView *thunder = [self tabViewWithRows:thunderRows];

  NSMutableArray<NSView *> *rainRows = [NSMutableArray arrayWithArray:
      [self rowsFrom:CONTROL_DROP_RATE count:3]];
  [rainRows addObject:[self sectionLabel:@"Surfaces"]];
  _surfacePickers = [NSMutableArray array];
  [rainRows addObjectsFromArray:[self surfaceEditorRows]];
  NSView *rain = [self tabViewWithRows:rainRows];

  NSMutableArray<NSView *> *impactRows = [NSMutableArray arrayWithObjects:
      [self surfacePickerRow], [self sectionLabel:@"Click"], nil];
  [impactRows addObjectsFromArray:[self rowsFrom:CONTROL_CLICK_GAIN_MIN count:5]];
  [impactRows addObject:[self sectionLabel:@"Resonances"]];
  [impactRows addObjectsFromArray:[self rowsFrom:CONTROL_MODE_1_FREQUENCY count:8]];
  NSView *impact = [self tabViewWithRows:impactRows];

  NSMutableArray<NSView *> *bubbleRows = [NSMutableArray arrayWithObjects:
      [self surfacePickerRow], [self sectionLabel:@"Random range per bubble"], nil];
  [bubbleRows addObjectsFromArray:[self rowsFrom:CONTROL_BUBBLE_PROBABILITY count:8]];
  NSView *bubbles = [self tabViewWithRows:bubbleRows];

  NSView *spatial = [self tabViewWithRows:@[
      [self row:CONTROL_STEREO_WIDTH], [self row:CONTROL_HEAD], [self row:CONTROL_REAR]
  ]];

  NSTabView *tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
  for (NSArray *item in @[@[@"Mixer", mixer], @[@"Storm", storm], @[@"Storm Shape", shape],
                           @[@"Wind", wind], @[@"Insects", insects], @[@"Thunder", thunder],
                           @[@"Rain", rain], @[@"Impact", impact], @[@"Bubbles", bubbles],
                           @[@"Spatial", spatial]]) {
    NSTabViewItem *tab = [[NSTabViewItem alloc] initWithIdentifier:item[0]];
    tab.label = item[0];
    tab.view = item[1];
    [tabs addTabViewItem:tab];
  }
  [tabs.widthAnchor constraintEqualToConstant:880.0].active = YES;
  [tabs.heightAnchor constraintEqualToConstant:600.0].active = YES;
  return tabs;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  gui_startup_config(&_config);

  _window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0, 920.0, 820.0)
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
  [self showSurfaceList];

  _playButton = [NSButton buttonWithTitle:@"Start" target:self
                                   action:@selector(togglePlayback:)];
  _playButton.bezelStyle = NSBezelStyleRounded;
  _statusLabel = [NSTextField labelWithString:@"Ready"];
  _statusLabel.textColor = NSColor.secondaryLabelColor;
  _exportButton = [NSButton buttonWithTitle:@"Export…" target:self
                                     action:@selector(exportAudio:)];
  NSMutableArray<NSView *> *playbackViews = [NSMutableArray arrayWithObjects:
      _playButton, _exportButton, nil];
  [playbackViews addObjectsFromArray:[self seedViews]];
  [playbackViews addObject:_statusLabel];
  NSStackView *playback = [NSStackView stackViewWithViews:playbackViews];
  playback.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  playback.alignment = NSLayoutAttributeCenterY;
  playback.spacing = 12.0;

  _weatherLabel = [NSTextField labelWithString:@"Weather appears during playback"];
  _weatherLabel.font = [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular];
  _silencedLabel = [NSTextField labelWithString:@""];
  _silencedLabel.textColor = NSColor.systemOrangeColor;
  _statusTimer = [NSTimer scheduledTimerWithTimeInterval:0.25 target:self
                                                selector:@selector(showStatus:)
                                                userInfo:nil repeats:YES];

  NSStackView *root = [NSStackView stackViewWithViews:@[title, subtitle, tabs, _weatherLabel,
                                                        _silencedLabel, playback]];
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
  [_statusTimer invalidate];
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
