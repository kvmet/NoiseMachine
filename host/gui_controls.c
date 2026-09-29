#include "gui_controls.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(member) GUI_SCOPE_CONFIG, offsetof(noise_config, member)
#define SURFACE(member) GUI_SCOPE_SURFACE, offsetof(noise_surface, member)
#define LINEAR_IN(label, field, minimum, maximum, format) \
  {label, field, minimum, maximum, 0.0f, GUI_SCALE_LINEAR, 1.0f, format}
#define LOG_IN(label, field, minimum, maximum, format) \
  {label, field, minimum, maximum, 0.0f, GUI_SCALE_LOG, 1.0f, format}
#define LINEAR(label, member, minimum, maximum, format) \
  LINEAR_IN(label, FIELD(member), minimum, maximum, format)
#define LOG(label, member, minimum, maximum, format) \
  LOG_IN(label, FIELD(member), minimum, maximum, format)
#define GAIN(label, member) LINEAR(label, member, 0.0f, 1.0f, "%.2f")
#define MOD(label, route) LINEAR(label, weather.mod_amount[route], -1.0f, 1.0f, "%.2f")
#define MODE(number, index) \
  [CONTROL_MODE_##number##_FREQUENCY] = LOG_IN("Mode " #number " frequency (Hz)", \
      SURFACE(mode[index].frequency_hz), 20.0f, 20000.0f, "%.0f"), \
  [CONTROL_MODE_##number##_DAMPING] = LOG_IN("Mode " #number " damping (/s)", \
      SURFACE(mode[index].damping_per_s), 1.0f, 20000.0f, "%.0f"), \
  [CONTROL_MODE_##number##_GAIN] = LINEAR_IN("Mode " #number " gain", \
      SURFACE(mode[index].gain), 0.0f, 4.0f, "%.3f")
/* Bubble radii are stored in metres and shown in millimetres. */
#define RADIUS(label, member) \
  {label, SURFACE(member), 0.00016f, 0.004f, 0.0f, GUI_SCALE_LOG, 1000.0f, "%.2f"}

/* Weight given to the last nonzero surface when the user zeroes or deletes it. */
#define WEIGHT_KEPT 0.001f
#define WEIGHT_KEPT_NOTE "At least one surface weight must be above zero"

const gui_control gui_controls[CONTROL_COUNT] = {
  [CONTROL_WHITE] = GAIN("White noise gain", ambient_gain[NOISE_KIND_WHITE]),
  [CONTROL_PINK] = GAIN("Pink noise gain", ambient_gain[NOISE_KIND_PINK]),
  [CONTROL_HUM_50HZ] = GAIN("50 Hz hum gain", ambient_gain[NOISE_KIND_HUM_50HZ]),
  [CONTROL_HUM_60HZ] = GAIN("60 Hz hum gain", ambient_gain[NOISE_KIND_HUM_60HZ]),
  [CONTROL_MASTER_GAIN] = GAIN("Master gain", master_gain),
  [CONTROL_REVERB_GAIN] = GAIN("Reverb gain", reverb_gain),
  [CONTROL_STEREO_WIDTH] = LINEAR("Stereo width (m)", listener.stereo_width_m,
                                  0.0f, 0.5f, "%.3f"),
  [CONTROL_HEAD] = GAIN("Head effect", listener.head_amount),
  [CONTROL_REAR] = GAIN("Rear filter", listener.rear_amount),

  [CONTROL_WIND_GAIN] = GAIN("Gain", wind.gain),
  [CONTROL_WIND_BRIGHTNESS] = GAIN("Brightness", wind.brightness),
  [CONTROL_WIND_GUST_DEPTH] = GAIN("Gust depth", wind.gust_depth),
  [CONTROL_WIND_GUST_RATE] = LOG("Gust rate (Hz)", wind.gust_rate_hz, 0.01f, 2.0f, "%.2f"),
  [CONTROL_WIND_WIDTH] = GAIN("Stereo width", wind.stereo_width),

  [CONTROL_CRICKET_GAIN] = GAIN("Gain", crickets.gain),
  [CONTROL_CRICKET_CALL_RATE] = LOG("Chirps/s each", crickets.call_rate_hz,
                                    0.05f, 10.0f, "%.2f"),
  [CONTROL_CRICKET_PITCH] = LOG("Pitch (Hz)", crickets.pitch_hz, 2000.0f, 8000.0f, "%.2f"),
  [CONTROL_CRICKET_PITCH_VARIATION] = GAIN("Pitch variation", crickets.pitch_variation),
  [CONTROL_CRICKET_WIDTH] = GAIN("Angular spread", crickets.placement.stereo_width),
  [CONTROL_CRICKET_MIN_DISTANCE] = LOG("Min distance (m)", crickets.placement.min_distance_m,
                                       0.25f, 100.0f, "%.3f"),
  [CONTROL_CRICKET_MAX_DISTANCE] = LOG("Max distance (m)", crickets.placement.max_distance_m,
                                       0.25f, 100.0f, "%.3f"),

  [CONTROL_CICADA_GAIN] = GAIN("Gain", cicadas.gain),
  [CONTROL_CICADA_PITCH] = LOG("Pitch (Hz)", cicadas.pitch_hz, 2000.0f, 10000.0f, "%.2f"),
  [CONTROL_CICADA_CLICK_RATE] = LINEAR("Click rate ×", cicadas.click_rate_scale,
                                       0.5f, 1.5f, "%.2f"),
  [CONTROL_CICADA_CHORUS] = GAIN("Distant chorus", cicadas.chorus),
  [CONTROL_CICADA_WIDTH] = GAIN("Angular spread", cicadas.placement.stereo_width),
  [CONTROL_CICADA_MIN_DISTANCE] = LOG("Min distance (m)", cicadas.placement.min_distance_m,
                                      0.25f, 100.0f, "%.3f"),
  [CONTROL_CICADA_MAX_DISTANCE] = LOG("Max distance (m)", cicadas.placement.max_distance_m,
                                      0.25f, 100.0f, "%.3f"),

  [CONTROL_THUNDER_GAIN] = GAIN("Gain", thunder.gain),
  [CONTROL_THUNDER_RATE] = LINEAR("Strikes per minute", thunder.rate_per_min,
                                  0.0f, 20.0f, "%.2f"),
  [CONTROL_THUNDER_MIN_DISTANCE] = LOG("Minimum distance (m)", thunder.min_distance_m,
                                       200.0f, 15000.0f, "%.0f"),
  [CONTROL_THUNDER_MAX_DISTANCE] = LOG("Maximum distance (m)", thunder.max_distance_m,
                                       200.0f, 15000.0f, "%.0f"),
  [CONTROL_THUNDER_REVERB_GAIN] = GAIN("Reverb gain", thunder.reverb_gain),
  [CONTROL_THUNDER_REVERB_DECAY] = LOG("Reverb decay (s)", thunder.reverb_decay_s,
                                       0.5f, 10.0f, "%.2f"),

  [CONTROL_RAIN_INTENSITY] = GAIN("Rain intensity", weather.intensity),
  [CONTROL_MIN_INTENSITY] = GAIN("Minimum intensity", weather.min_intensity),
  [CONTROL_MAX_INTENSITY] = GAIN("Maximum intensity", weather.max_intensity),
  [CONTROL_WEATHER_STEP] = LOG("Weather interval (s)", weather.step_s, 0.1f, 3600.0f, "%.2f"),
  [CONTROL_RAIN_SLEW] = LOG("Rain slew (s)", weather.slew_s, 0.01f, 60.0f, "%.2f"),
  [CONTROL_RAIN_GAIN] = GAIN("Rain gain", rain.gain),
  [CONTROL_DROP_RATE] = LINEAR("Drops/s at full rain", rain.max_drops_per_s,
                               0.0f, 2000.0f, "%.0f"),
  [CONTROL_FALL_HEIGHT] = LOG("Fall height (m)", rain.fall_height_m, 0.01f, 1000.0f, "%.2f"),
  [CONTROL_RAIN_MIN_DISTANCE] = LOG("Minimum distance (m)", rain.min_distance_m,
                                    0.25f, 100.0f, "%.3f"),
  [CONTROL_RAIN_MAX_DISTANCE] = LOG("Maximum distance (m)", rain.max_distance_m,
                                    0.25f, 100.0f, "%.3f"),

  /* The weight slider spans 1e-7 to 1; its bottom position means zero. */
  [CONTROL_SURFACE_WEIGHT] = {"Weight", SURFACE(weight), 0.0f, 1.0f, 1e-7f,
                              GUI_SCALE_LOG_OFF, 1.0f, "%.6g"},
  [CONTROL_SURFACE_WEIGHT_MOD] = LINEAR_IN("Weight weather mod", SURFACE(weight_mod),
                                           -1.0f, 1.0f, "%.2f"),
  [CONTROL_CLICK_GAIN_MIN] = LINEAR_IN("Click gain minimum", SURFACE(click_gain_min),
                                       0.0f, 2.0f, "%.2f"),
  [CONTROL_CLICK_GAIN_MAX] = LINEAR_IN("Click gain maximum", SURFACE(click_gain_max),
                                       0.0f, 2.0f, "%.2f"),
  [CONTROL_CLICK_FREQUENCY_MIN] = LOG_IN("Click frequency min (Hz)",
      SURFACE(click_frequency_min_hz), 20.0f, 20000.0f, "%.0f"),
  [CONTROL_CLICK_FREQUENCY_MAX] = LOG_IN("Click frequency max (Hz)",
      SURFACE(click_frequency_max_hz), 20.0f, 20000.0f, "%.0f"),
  [CONTROL_CLICK_DAMPING] = LOG_IN("Click damping × f", SURFACE(click_damping_ratio),
                                   0.05f, 50.0f, "%.2f"),
  MODE(1, 0),
  MODE(2, 1),
  [CONTROL_DETUNE] = LINEAR_IN("Detune ±", SURFACE(detune), 0.0f, 0.5f, "%.3f"),
  [CONTROL_LOWPASS] = {"Low-pass (Hz)", SURFACE(lowpass_hz), 0.0f, 20000.0f, 20.0f,
                       GUI_SCALE_LOG_OFF, 1.0f, "%.0f"},
  [CONTROL_BUBBLE_PROBABILITY] = LINEAR_IN("Bubble probability", SURFACE(bubble_probability),
                                           0.0f, 1.0f, "%.2f"),
  [CONTROL_BUBBLE_RADIUS_MIN] = RADIUS("Bubble radius min (mm)", bubble_radius_min_m),
  [CONTROL_BUBBLE_RADIUS_MAX] = RADIUS("Bubble radius max (mm)", bubble_radius_max_m),
  [CONTROL_BUBBLE_GAIN_MIN] = LINEAR_IN("Bubble gain minimum", SURFACE(bubble_gain_min),
                                        0.0f, 8.0f, "%.2f"),
  [CONTROL_BUBBLE_GAIN_MAX] = LINEAR_IN("Bubble gain maximum", SURFACE(bubble_gain_max),
                                        0.0f, 8.0f, "%.2f"),
  [CONTROL_BUBBLE_DECAY_MIN] = LOG_IN("Decay scale minimum", SURFACE(bubble_decay_min),
                                      0.25f, 20.0f, "%.2f"),
  [CONTROL_BUBBLE_DECAY_MAX] = LOG_IN("Decay scale maximum", SURFACE(bubble_decay_max),
                                      0.25f, 20.0f, "%.2f"),
  [CONTROL_BUBBLE_DELAY] = {"Bubble delay (ms)", SURFACE(bubble_delay_s), 0.0f, 0.1f, 0.0f,
                            GUI_SCALE_LINEAR, 1000.0f, "%.1f"},

  [CONTROL_WEATHER_MOD + WEATHER_MOD_ARRIVAL_RATE] = MOD("Arrival density",
                                                         WEATHER_MOD_ARRIVAL_RATE),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_DROP_SIZE] = MOD("Drop size", WEATHER_MOD_DROP_SIZE),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_RAIN_GAIN] = MOD("Rain gain", WEATHER_MOD_RAIN_GAIN),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_REVERB_GAIN] = MOD("Reverb gain", WEATHER_MOD_REVERB_GAIN),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_FALL_HEIGHT] = MOD("Fall height", WEATHER_MOD_FALL_HEIGHT),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_MIN_DISTANCE] = MOD("Minimum distance",
                                                         WEATHER_MOD_MIN_DISTANCE),
  [CONTROL_WEATHER_MOD + WEATHER_MOD_MAX_DISTANCE] = MOD("Maximum distance",
                                                         WEATHER_MOD_MAX_DISTANCE),
};

/* Each lower bound stays at or below its upper bound. */
static const gui_control_id bound_pairs[][2] = {
  {CONTROL_MIN_INTENSITY, CONTROL_MAX_INTENSITY},
  {CONTROL_RAIN_MIN_DISTANCE, CONTROL_RAIN_MAX_DISTANCE},
  {CONTROL_CRICKET_MIN_DISTANCE, CONTROL_CRICKET_MAX_DISTANCE},
  {CONTROL_CICADA_MIN_DISTANCE, CONTROL_CICADA_MAX_DISTANCE},
  {CONTROL_THUNDER_MIN_DISTANCE, CONTROL_THUNDER_MAX_DISTANCE},
  {CONTROL_CLICK_GAIN_MIN, CONTROL_CLICK_GAIN_MAX},
  {CONTROL_CLICK_FREQUENCY_MIN, CONTROL_CLICK_FREQUENCY_MAX},
  {CONTROL_BUBBLE_RADIUS_MIN, CONTROL_BUBBLE_RADIUS_MAX},
  {CONTROL_BUBBLE_GAIN_MIN, CONTROL_BUBBLE_GAIN_MAX},
  {CONTROL_BUBBLE_DECAY_MIN, CONTROL_BUBBLE_DECAY_MAX},
};

void gui_startup_config(noise_config *c) {
  noise_config_default(c);
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
  c->rain.surface[WATER].weight = 6.05624e-05f;
  c->rain.surface[DIRT].weight = 4.94011e-05f;
  c->rain.surface[LEAF].weight = 3.04537e-06f;
  c->rain.surface[CONCRETE].weight = 1.68416e-05f;
  c->rain.surface[GLASS].weight = 2.29582e-07f;
  c->rain.surface[METAL].weight = 2.68089e-07f;
  c->rain.surface[PLASTIC].weight = 3.93724e-06f;
  c->rain.surface[ASPHALT].weight = 0.002557f;
  c->rain.surface[ASPHALT_ROOF].weight = 0.001559f;
  c->rain.surface[WATER].click_gain_min = 0.16f;
  c->rain.surface[WATER].click_gain_max = 0.50f;
  c->rain.surface[WATER].bubble_probability = 0.51f;
  c->rain.surface[WATER].bubble_radius_min_m = 0.00023f;
  c->rain.surface[WATER].bubble_radius_max_m = 0.00069f;
  c->rain.surface[WATER].bubble_gain_min = 0.36f;
  c->rain.surface[WATER].bubble_gain_max = 2.50f;
  c->rain.surface[WATER].bubble_decay_min = 0.31f;
  c->rain.surface[WATER].bubble_decay_max = 0.58f;
}

static float *field(noise_config *config, unsigned surface, gui_control_id id) {
  const gui_control *control = &gui_controls[id];
  char *base = control->scope == GUI_SCOPE_SURFACE ? (char *)&config->rain.surface[surface] :
                                                     (char *)config;
  return (float *)(base + control->offset);
}

float gui_control_get(const noise_config *config, unsigned surface, gui_control_id id) {
  const gui_control *control = &gui_controls[id];
  const char *base = control->scope == GUI_SCOPE_SURFACE ?
      (const char *)&config->rain.surface[surface] : (const char *)config;
  return *(const float *)(base + control->offset);
}

static void clamp_intensity(noise_config *c) {
  if (c->weather.vary) {
    c->weather.intensity = fminf(c->weather.max_intensity,
                                 fmaxf(c->weather.min_intensity, c->weather.intensity));
  }
}

static int only_nonzero_surface(const noise_config *c, unsigned surface) {
  for (unsigned i = 0; i < c->rain.surface_count; ++i) {
    if (i != surface && c->rain.surface[i].weight > 0.0f) return 0;
  }
  return 1;
}

static float clamp(const gui_control *control, float value) {
  value = fminf(control->maximum, fmaxf(control->minimum, value));
  if (control->scale == GUI_SCALE_LOG_OFF && value > 0.0f) {
    value = fmaxf(control->smallest, value);
  }
  return value;
}

const char *gui_control_set(noise_config *config, unsigned surface, gui_control_id id,
                            float value) {
  value = clamp(&gui_controls[id], value);
  const char *note = NULL;
  if (id == CONTROL_SURFACE_WEIGHT && value == 0.0f && only_nonzero_surface(config, surface)) {
    value = WEIGHT_KEPT;
    note = WEIGHT_KEPT_NOTE;
  }
  *field(config, surface, id) = value;
  for (size_t i = 0; i < sizeof(bound_pairs) / sizeof(bound_pairs[0]); ++i) {
    float *lower = field(config, surface, bound_pairs[i][0]);
    float *upper = field(config, surface, bound_pairs[i][1]);
    if (id == bound_pairs[i][0] && *upper < value) *upper = value;
    if (id == bound_pairs[i][1] && *lower > value) *lower = value;
  }
  clamp_intensity(config);
  return note;
}

/* Copies at most size - 1 bytes of name, backing off to a UTF-8 character start.
   Returns whether all of name fit. */
static int copy_name(char *out, size_t size, const char *name) {
  size_t length = strlen(name);
  size_t kept = length < size ? length : size - 1;
  while (kept < length && kept > 0 && ((unsigned char)name[kept] & 0xC0) == 0x80) --kept;
  memcpy(out, name, kept);
  out[kept] = '\0';
  return kept == length;
}

const char *gui_surface_add(noise_config *config, unsigned from) {
  noise_rain_config *rain = &config->rain;
  if (rain->surface_count == NOISE_MAX_SURFACES) return "The surface list is full";
  noise_surface *added = &rain->surface[rain->surface_count];
  *added = rain->surface[from];
  added->weight = 0.0f;
  added->weight_mod = 0.0f;
  char name[2 * NOISE_SURFACE_NAME_SIZE];
  snprintf(name, sizeof(name), "%s copy", rain->surface[from].name);
  copy_name(added->name, sizeof(added->name), name);
  ++rain->surface_count;
  return NULL;
}

const char *gui_surface_delete(noise_config *config, unsigned surface) {
  noise_rain_config *rain = &config->rain;
  if (rain->surface_count == 1) return "At least one surface is required";
  memmove(&rain->surface[surface], &rain->surface[surface + 1],
          (rain->surface_count - surface - 1) * sizeof(rain->surface[0]));
  --rain->surface_count;
  float sum = 0.0f;
  for (unsigned i = 0; i < rain->surface_count; ++i) sum += rain->surface[i].weight;
  if (sum > 0.0f) return NULL;
  rain->surface[0].weight = WEIGHT_KEPT;
  return WEIGHT_KEPT_NOTE;
}

const char *gui_surface_rename(noise_config *config, unsigned surface, const char *name) {
  if (!name[0]) return "Enter a name";
  noise_surface *s = &config->rain.surface[surface];
  return copy_name(s->name, sizeof(s->name), name) ? NULL : "Name shortened to fit";
}

void gui_set_vary(noise_config *config, int vary) {
  config->weather.vary = vary ? 1 : 0;
  clamp_intensity(config);
}

void gui_slider_range(gui_control_id id, double *minimum, double *maximum) {
  const gui_control *control = &gui_controls[id];
  *minimum = control->scale == GUI_SCALE_LOG_OFF ? log(control->smallest) :
                                                   gui_slider_position(id, control->minimum);
  *maximum = gui_slider_position(id, control->maximum);
}

double gui_slider_position(gui_control_id id, float value) {
  const gui_control *control = &gui_controls[id];
  switch (control->scale) {
    case GUI_SCALE_LOG:
      return log(value);
    case GUI_SCALE_LOG_OFF:
      return log(fmaxf(control->smallest, value));
    case GUI_SCALE_LINEAR:
      break;
  }
  return value;
}

float gui_slider_value(gui_control_id id, double position) {
  const gui_control *control = &gui_controls[id];
  switch (control->scale) {
    case GUI_SCALE_LOG:
      return (float)exp(position);
    case GUI_SCALE_LOG_OFF:
      return position <= log(control->smallest) ? 0.0f : (float)exp(position);
    case GUI_SCALE_LINEAR:
      break;
  }
  return (float)position;
}

void gui_control_format(gui_control_id id, float value, char *text, size_t size) {
  const gui_control *control = &gui_controls[id];
  snprintf(text, size, control->format, (double)(control->display_factor * value));
}

int gui_control_parse(gui_control_id id, const char *text, float *value) {
  char *end;
  errno = 0;
  double shown = strtod(text, &end);
  if (!text[0] || end == text || *end || errno == ERANGE || !isfinite(shown)) return 0;
  *value = (float)(shown / gui_controls[id].display_factor);
  return 1;
}
