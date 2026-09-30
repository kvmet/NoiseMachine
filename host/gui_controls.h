#ifndef GUI_CONTROLS_H
#define GUI_CONTROLS_H

#include <stddef.h>

#include "noise_core.h"

/* One slider per float field of noise_config that the GUI edits. */
typedef enum gui_control_id {
  CONTROL_WHITE,
  CONTROL_PINK,
  CONTROL_HUM_50HZ,
  CONTROL_HUM_60HZ,
  CONTROL_MASTER_GAIN,
  CONTROL_REVERB_GAIN,
  CONTROL_STEREO_WIDTH,
  CONTROL_HEAD,
  CONTROL_REAR,
  CONTROL_STORM_TIME_SCALE,
  CONTROL_STORM_TEMPERATURE,
  CONTROL_STORM_MIN_SEVERITY,
  CONTROL_STORM_MAX_SEVERITY,
  CONTROL_STORMS_PER_HOUR,
  CONTROL_STORM_CELL_SPEED,
  CONTROL_STORM_BREEZE,
  /* Weather held while storm.manual is set. */
  CONTROL_FIXED_RAIN,
  CONTROL_FIXED_WIND,
  CONTROL_FIXED_WIND_BEARING,
  CONTROL_FIXED_TEMPERATURE,
  CONTROL_FIXED_LIGHTNING,
  CONTROL_FIXED_CELL_DISTANCE,
  CONTROL_FIXED_CELL_BEARING,
  /* Storm shape; like the storm controls above, used only while simulating. */
  CONTROL_SHAPE_PEAK_RAIN_MIN,
  CONTROL_SHAPE_PEAK_RAIN_MAX,
  CONTROL_SHAPE_CORE_ALONG,
  CONTROL_SHAPE_CORE_ACROSS,
  CONTROL_SHAPE_TAIL_SHARE,
  CONTROL_SHAPE_TAIL_LENGTH,
  CONTROL_SHAPE_TAIL_WIDTH,
  CONTROL_SHAPE_FRONT_MIN,
  CONTROL_SHAPE_FRONT_MAX,
  CONTROL_SHAPE_FRONT_EDGE,
  CONTROL_SHAPE_OUTFLOW_MIN,
  CONTROL_SHAPE_OUTFLOW_MAX,
  CONTROL_SHAPE_OUTFLOW_DECAY,
  CONTROL_SHAPE_OUTFLOW_WIDTH,
  CONTROL_SHAPE_COOLING_MIN,
  CONTROL_SHAPE_COOLING_MAX,
  CONTROL_SHAPE_COOLING_DECAY,
  CONTROL_SHAPE_COOLING_WIDTH,
  CONTROL_SHAPE_COOLING_TIME,
  CONTROL_SHAPE_WARMING_TIME,
  CONTROL_SHAPE_LIGHTNING_MIN,
  CONTROL_SHAPE_LIGHTNING_MAX,
  CONTROL_SHAPE_BUILD,
  CONTROL_SHAPE_DECAY,
  CONTROL_SHAPE_APPROACH,
  CONTROL_SHAPE_MISS,
  CONTROL_SHAPE_HEADING_SPREAD,
  CONTROL_GUST_INTENSITY,
  CONTROL_GUST_TIME,
  CONTROL_WIND_GAIN,
  CONTROL_WIND_WIDTH,
  CONTROL_WIND_BRIGHTNESS,
  CONTROL_WIND_RUMBLE,
  CONTROL_WIND_BALANCE,
  CONTROL_CRICKET_GAIN,
  CONTROL_CRICKET_CALL_RATE,
  CONTROL_CRICKET_PITCH,
  CONTROL_CRICKET_PITCH_VARIATION,
  CONTROL_CRICKET_WIDTH,
  CONTROL_CRICKET_MIN_DISTANCE,
  CONTROL_CRICKET_MAX_DISTANCE,
  CONTROL_CRICKET_MIN_TEMPERATURE,
  CONTROL_CRICKET_MAX_RAIN,
  CONTROL_CRICKET_MAX_WIND,
  CONTROL_CICADA_GAIN,
  CONTROL_CICADA_PITCH,
  CONTROL_CICADA_CLICK_RATE,
  CONTROL_CICADA_CHORUS,
  CONTROL_CICADA_WIDTH,
  CONTROL_CICADA_MIN_DISTANCE,
  CONTROL_CICADA_MAX_DISTANCE,
  CONTROL_CICADA_MIN_TEMPERATURE,
  CONTROL_CICADA_MAX_RAIN,
  CONTROL_THUNDER_GAIN,
  CONTROL_THUNDER_REVERB_GAIN,
  CONTROL_THUNDER_REVERB_DECAY,
  CONTROL_THUNDER_SCATTER,
  CONTROL_RAIN_GAIN,
  CONTROL_BED_GAIN,
  CONTROL_DROP_RATE,
  CONTROL_RAIN_MIN_DISTANCE,
  CONTROL_RAIN_MAX_DISTANCE,
  CONTROL_SHEET_DEPTH,
  /* Fields of the selected surface. */
  CONTROL_SURFACE_COVERAGE,
  CONTROL_CLICK_GAIN_MIN,
  CONTROL_CLICK_GAIN_MAX,
  CONTROL_CLICK_FREQUENCY_MIN,
  CONTROL_CLICK_FREQUENCY_MAX,
  CONTROL_CLICK_DAMPING,
  CONTROL_MODE_1_FREQUENCY,
  CONTROL_MODE_1_DAMPING,
  CONTROL_MODE_1_GAIN,
  CONTROL_MODE_2_FREQUENCY,
  CONTROL_MODE_2_DAMPING,
  CONTROL_MODE_2_GAIN,
  CONTROL_DETUNE,
  CONTROL_LOWPASS,
  CONTROL_BUBBLE_PROBABILITY,
  CONTROL_BUBBLE_RADIUS_MIN,
  CONTROL_BUBBLE_RADIUS_MAX,
  CONTROL_BUBBLE_GAIN_MIN,
  CONTROL_BUBBLE_GAIN_MAX,
  CONTROL_BUBBLE_DECAY_MIN,
  CONTROL_BUBBLE_DECAY_MAX,
  CONTROL_BUBBLE_DELAY,
  CONTROL_COUNT
} gui_control_id;

typedef enum gui_scale {
  GUI_SCALE_LINEAR,
  GUI_SCALE_LOG,
  GUI_SCALE_LOG_OFF /* Log slider from smallest to maximum whose bottom position means zero. */
} gui_scale;

typedef enum gui_scope {
  GUI_SCOPE_CONFIG,  /* offset is into noise_config. */
  GUI_SCOPE_SURFACE  /* offset is into the selected noise_surface. */
} gui_scope;

typedef struct gui_control {
  const char *label;
  gui_scope scope;
  size_t offset;        /* Float field. */
  float minimum;        /* Range in config units, inside what the engine accepts. */
  float maximum;
  float smallest;       /* GUI_SCALE_LOG_OFF only: smallest nonzero value. */
  gui_scale scale;
  float display_factor; /* Shown value is display_factor times the config value. */
  const char *format;   /* printf format for the shown value. */
} gui_control;

extern const gui_control gui_controls[CONTROL_COUNT];

/* Listening settings the GUI opens with; the engine keeps its own defaults. */
void gui_startup_config(noise_config *config);

/* surface selects the entry for GUI_SCOPE_SURFACE controls; others ignore it. */
float gui_control_get(const noise_config *config, unsigned surface, gui_control_id id);
/* Clamps value to the control's range, stores it, and moves any field that must
   follow so a valid config stays valid. Returns a note for the user when the
   stored value differs from the clamped request, or NULL. */
const char *gui_control_set(noise_config *config, unsigned surface, gui_control_id id,
                            float value);
/* Surface list edits. Each keeps a valid config valid and returns a note for the
   user when it changes less than asked, or NULL. */
/* Appends a copy of surface from with coverage zero. */
const char *gui_surface_add(noise_config *config, unsigned from);
/* Removes surface and shifts later ones down. */
const char *gui_surface_delete(noise_config *config, unsigned surface);
/* Stores name, shortened at a UTF-8 character boundary to fit. */
const char *gui_surface_rename(noise_config *config, unsigned surface, const char *name);

void gui_slider_range(gui_control_id id, double *minimum, double *maximum);
double gui_slider_position(gui_control_id id, float value);
float gui_slider_value(gui_control_id id, double position);

void gui_control_format(gui_control_id id, float value, char *text, size_t size);
/* Reads a shown value. Returns 0 and leaves value unchanged unless text is one
   finite number. */
int gui_control_parse(gui_control_id id, const char *text, float *value);

/* Layers whose sound the weather adjusts. */
typedef enum gui_layer {
  GUI_LAYER_RAIN,
  GUI_LAYER_BED,
  GUI_LAYER_WIND,
  GUI_LAYER_CRICKETS,
  GUI_LAYER_CICADAS,
  GUI_LAYER_THUNDER,
  GUI_LAYER_COUNT
} gui_layer;

/* One line on what the weather is doing to layer. */
void gui_layer_status(gui_layer layer, const noise_status *status, const noise_config *config,
                      char *text, size_t size);
/* One line of the current weather. */
void gui_weather_summary(const noise_status *status, char *text, size_t size);
/* Names each audible layer the weather silences, with its reasons; empty when none. */
void gui_silenced_summary(const noise_status *status, const noise_config *config,
                          char *text, size_t size);

#endif
