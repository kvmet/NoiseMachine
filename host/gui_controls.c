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
/* Distances are stored in metres and shown in kilometres. */
#define KM(label, member, minimum, maximum) \
  {label, FIELD(member), minimum, maximum, 0.0f, GUI_SCALE_LOG, 0.001f, "%.1f"}
#define KM_LINEAR(label, member, minimum, maximum) \
  {label, FIELD(member), minimum, maximum, 0.0f, GUI_SCALE_LINEAR, 0.001f, "%.1f"}
/* Shares are stored as fractions and shown in percent. */
#define PERCENT(label, member, minimum, maximum) \
  {label, FIELD(member), minimum, maximum, 0.0f, GUI_SCALE_LINEAR, 100.0f, "%.0f"}
#define SHAPE(member) storm.shape.member
/* Bearings are stored in radians and shown in degrees. */
#define BEARING(label, member) \
  {label, FIELD(member), -3.14159265f, 3.14159265f, 0.0f, GUI_SCALE_LINEAR, \
   57.2957795f, "%.0f"}
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

/* Coverage given to the last nonzero surface when the user zeroes or deletes it. */
#define COVERAGE_KEPT 0.001f
#define COVERAGE_KEPT_NOTE "At least one surface coverage must be above zero"

const gui_control gui_controls[CONTROL_COUNT] = {
  [CONTROL_WHITE] = GAIN("White noise", ambient_gain[NOISE_KIND_WHITE]),
  [CONTROL_PINK] = GAIN("Pink noise", ambient_gain[NOISE_KIND_PINK]),
  [CONTROL_HUM_50HZ] = GAIN("50 Hz hum", ambient_gain[NOISE_KIND_HUM_50HZ]),
  [CONTROL_HUM_60HZ] = GAIN("60 Hz hum", ambient_gain[NOISE_KIND_HUM_60HZ]),
  [CONTROL_MASTER_GAIN] = GAIN("Master", master_gain),
  [CONTROL_REVERB_GAIN] = GAIN("Reverb", reverb_gain),
  [CONTROL_STEREO_WIDTH] = LINEAR("Stereo width (m)", listener.stereo_width_m,
                                  0.0f, 0.5f, "%.3f"),
  [CONTROL_HEAD] = GAIN("Head effect", listener.head_amount),
  [CONTROL_REAR] = GAIN("Rear filter", listener.rear_amount),

  [CONTROL_STORM_TIME_SCALE] = LOG("Time speed-up ×", storm.time_scale, 1.0f, 600.0f, "%.0f"),
  [CONTROL_STORM_TEMPERATURE] = LINEAR("Clear-sky temp (°C)", storm.temperature_c,
                                       -10.0f, 45.0f, "%.1f"),
  [CONTROL_STORM_MIN_SEVERITY] = GAIN("Minimum severity", storm.min_severity),
  [CONTROL_STORM_MAX_SEVERITY] = GAIN("Maximum severity", storm.max_severity),
  [CONTROL_STORMS_PER_HOUR] = LINEAR("Storms per hour", storm.storms_per_hour,
                                     0.0f, 4.0f, "%.2f"),
  [CONTROL_STORM_CELL_SPEED] = LOG("Storm speed (m/s)", storm.cell_speed_m_s,
                                   3.0f, 30.0f, "%.1f"),
  [CONTROL_STORM_BREEZE] = LINEAR("Breeze (m/s)", storm.breeze_m_s, 0.0f, 15.0f, "%.1f"),

  [CONTROL_FIXED_RAIN] = {"Rain (mm/h)", FIELD(storm.fixed.rain_mm_h), 0.0f, 200.0f, 0.1f,
                          GUI_SCALE_LOG_OFF, 1.0f, "%.1f"},
  [CONTROL_FIXED_WIND] = LINEAR("Wind (m/s)", storm.fixed.wind_m_s, 0.0f, 40.0f, "%.1f"),
  [CONTROL_FIXED_WIND_BEARING] = BEARING("Wind from (°)", storm.fixed.wind_bearing_rad),
  [CONTROL_FIXED_TEMPERATURE] = LINEAR("Temperature (°C)", storm.fixed.temperature_c,
                                       -10.0f, 45.0f, "%.1f"),
  [CONTROL_FIXED_LIGHTNING] = LINEAR("Lightning per minute", storm.fixed.lightning_per_min,
                                     0.0f, 30.0f, "%.1f"),
  [CONTROL_FIXED_CELL_DISTANCE] = LOG("Storm distance (m)", storm.fixed.cell.distance_m,
                                      200.0f, 30000.0f, "%.0f"),
  [CONTROL_FIXED_CELL_BEARING] = BEARING("Storm bearing (°)", storm.fixed.cell.angle_rad),

  [CONTROL_SHAPE_PEAK_RAIN_MIN] = LOG("Peak rain, mild (mm/h)", SHAPE(peak_rain_min_mm_h),
                                     0.1f, 200.0f, "%.1f"),
  [CONTROL_SHAPE_PEAK_RAIN_MAX] = LOG("Peak rain, severe (mm/h)", SHAPE(peak_rain_max_mm_h),
                                     0.1f, 200.0f, "%.1f"),
  [CONTROL_SHAPE_CORE_ALONG] = KM("Core depth (km)", SHAPE(core_along_m), 500.0f, 20000.0f),
  [CONTROL_SHAPE_CORE_ACROSS] = KM("Core width (km)", SHAPE(core_across_m), 500.0f, 50000.0f),
  [CONTROL_SHAPE_TAIL_SHARE] = PERCENT("Tail rain (% of peak)", SHAPE(tail_share), 0.0f, 1.0f),
  [CONTROL_SHAPE_TAIL_LENGTH] = KM("Tail length (km)", SHAPE(tail_length_m), 1000.0f, 50000.0f),
  [CONTROL_SHAPE_TAIL_WIDTH] = KM("Tail width (km)", SHAPE(tail_width_m), 1000.0f, 50000.0f),
  [CONTROL_SHAPE_FRONT_MIN] = KM_LINEAR("Front lead, mild (km)", SHAPE(front_min_m),
                                        0.0f, 20000.0f),
  [CONTROL_SHAPE_FRONT_MAX] = KM_LINEAR("Front lead, severe (km)", SHAPE(front_max_m),
                                        0.0f, 20000.0f),
  [CONTROL_SHAPE_FRONT_EDGE] = KM("Front edge (km)", SHAPE(front_edge_m), 100.0f, 10000.0f),
  [CONTROL_SHAPE_OUTFLOW_MIN] = LINEAR("Outflow, mild (m/s)", SHAPE(outflow_min_m_s),
                                       0.0f, 40.0f, "%.1f"),
  [CONTROL_SHAPE_OUTFLOW_MAX] = LINEAR("Outflow, severe (m/s)", SHAPE(outflow_max_m_s),
                                       0.0f, 40.0f, "%.1f"),
  [CONTROL_SHAPE_OUTFLOW_DECAY] = KM("Outflow behind (km)", SHAPE(outflow_decay_m),
                                     500.0f, 50000.0f),
  [CONTROL_SHAPE_OUTFLOW_WIDTH] = KM("Outflow width (km)", SHAPE(outflow_width_m),
                                     1000.0f, 50000.0f),
  [CONTROL_SHAPE_COOLING_MIN] = LINEAR("Cooling, mild (°C)", SHAPE(cooling_min_c),
                                       0.0f, 20.0f, "%.1f"),
  [CONTROL_SHAPE_COOLING_MAX] = LINEAR("Cooling, severe (°C)", SHAPE(cooling_max_c),
                                       0.0f, 20.0f, "%.1f"),
  [CONTROL_SHAPE_COOLING_DECAY] = KM("Cool air behind (km)", SHAPE(cooling_decay_m),
                                     1000.0f, 100000.0f),
  [CONTROL_SHAPE_COOLING_WIDTH] = KM("Cool air width (km)", SHAPE(cooling_width_m),
                                     1000.0f, 50000.0f),
  [CONTROL_SHAPE_COOLING_TIME] = LOG("Cooling time (s)", SHAPE(cooling_s), 10.0f, 3600.0f, "%.0f"),
  [CONTROL_SHAPE_WARMING_TIME] = LOG("Warming time (s)", SHAPE(warming_s), 60.0f, 36000.0f, "%.0f"),
  [CONTROL_SHAPE_LIGHTNING_MIN] = LINEAR("Flashes/min, mild", SHAPE(lightning_min_per_min),
                                         0.0f, 30.0f, "%.1f"),
  [CONTROL_SHAPE_LIGHTNING_MAX] = LINEAR("Flashes/min, severe", SHAPE(lightning_max_per_min),
                                         0.0f, 30.0f, "%.1f"),
  [CONTROL_SHAPE_BUILD] = PERCENT("Growth (% of track)", SHAPE(build_share), 0.05f, 0.9f),
  [CONTROL_SHAPE_DECAY] = PERCENT("Decay (% of track)", SHAPE(decay_share), 0.05f, 0.95f),
  [CONTROL_SHAPE_APPROACH] = KM("Track half-length (km)", SHAPE(approach_m), 10000.0f, 100000.0f),
  [CONTROL_SHAPE_MISS] = KM_LINEAR("Largest miss (km)", SHAPE(miss_m), 0.0f, 30000.0f),
  [CONTROL_SHAPE_HEADING_SPREAD] = {"Heading spread (°)", FIELD(SHAPE(heading_spread_rad)),
                                    0.0f, 3.14159265f, 0.0f, GUI_SCALE_LINEAR, 57.2957795f, "%.0f"},
  [CONTROL_GUST_INTENSITY] = GAIN("Gust depth", storm.gust_intensity),
  [CONTROL_GUST_TIME] = LOG("Gust time (s)", storm.gust_time_s, 0.5f, 30.0f, "%.1f"),

  [CONTROL_WIND_GAIN] = GAIN("Wind (at 20 m/s)", wind.gain),
  [CONTROL_WIND_WIDTH] = GAIN("Stereo width", wind.stereo_width),
  [CONTROL_WIND_BRIGHTNESS] = LOG("Brightness ×", wind.brightness, 0.25f, 4.0f, "%.2f"),
  [CONTROL_WIND_RUMBLE] = LINEAR("Rumble ×", wind.rumble, 0.0f, 2.0f, "%.2f"),
  [CONTROL_WIND_BALANCE] = GAIN("Lean toward wind", wind.balance),

  [CONTROL_CRICKET_GAIN] = GAIN("Crickets", crickets.gain),
  [CONTROL_CRICKET_CALL_RATE] = LOG("Chirp rate ×", crickets.call_rate_scale,
                                    0.1f, 2.0f, "%.2f"),
  [CONTROL_CRICKET_PITCH] = LOG("Pitch (Hz)", crickets.pitch_hz, 2000.0f, 8000.0f, "%.2f"),
  [CONTROL_CRICKET_PITCH_VARIATION] = GAIN("Pitch variation", crickets.pitch_variation),
  [CONTROL_CRICKET_WIDTH] = GAIN("Angular spread", crickets.placement.stereo_width),
  [CONTROL_CRICKET_MIN_DISTANCE] = LOG("Min distance (m)", crickets.placement.min_distance_m,
                                       0.25f, 100.0f, "%.3f"),
  [CONTROL_CRICKET_MAX_DISTANCE] = LOG("Max distance (m)", crickets.placement.max_distance_m,
                                       0.25f, 100.0f, "%.3f"),

  [CONTROL_CICADA_GAIN] = GAIN("Cicadas", cicadas.gain),
  [CONTROL_CICADA_PITCH] = LOG("Pitch (Hz)", cicadas.pitch_hz, 1000.0f, 10000.0f, "%.2f"),
  [CONTROL_CICADA_CLICK_RATE] = LINEAR("Click rate ×", cicadas.click_rate_scale,
                                       0.5f, 1.5f, "%.2f"),
  [CONTROL_CICADA_CHORUS] = GAIN("Distant chorus", cicadas.chorus),
  [CONTROL_CICADA_WIDTH] = GAIN("Angular spread", cicadas.placement.stereo_width),
  [CONTROL_CICADA_MIN_DISTANCE] = LOG("Min distance (m)", cicadas.placement.min_distance_m,
                                      0.25f, 100.0f, "%.3f"),
  [CONTROL_CICADA_MAX_DISTANCE] = LOG("Max distance (m)", cicadas.placement.max_distance_m,
                                      0.25f, 100.0f, "%.3f"),

  [CONTROL_THUNDER_GAIN] = GAIN("Thunder", thunder.gain),
  [CONTROL_THUNDER_REVERB_GAIN] = GAIN("Reverb gain", thunder.reverb_gain),
  [CONTROL_THUNDER_REVERB_DECAY] = LOG("Reverb decay (s)", thunder.reverb_decay_s,
                                       0.5f, 10.0f, "%.2f"),
  [CONTROL_THUNDER_SCATTER] = KM_LINEAR("Strike scatter (km)", thunder.scatter_m,
                                        0.0f, 10000.0f),
  [CONTROL_CRICKET_MIN_TEMPERATURE] = LINEAR("Sings above (°C)", crickets.min_temperature_c,
                                             -10.0f, 45.0f, "%.1f"),
  [CONTROL_CRICKET_MAX_RAIN] = {"Sings below rain (mm/h)", FIELD(crickets.max_rain_mm_h),
                                0.0f, 200.0f, 0.01f, GUI_SCALE_LOG_OFF, 1.0f, "%.2f"},
  [CONTROL_CRICKET_MAX_WIND] = LINEAR("Sings below wind (m/s)", crickets.max_wind_m_s,
                                      0.0f, 40.0f, "%.1f"),
  [CONTROL_CICADA_MIN_TEMPERATURE] = LINEAR("Sings above (°C)", cicadas.min_temperature_c,
                                            -10.0f, 45.0f, "%.1f"),
  [CONTROL_CICADA_MAX_RAIN] = {"Sings below rain (mm/h)", FIELD(cicadas.max_rain_mm_h),
                               0.0f, 200.0f, 0.01f, GUI_SCALE_LOG_OFF, 1.0f, "%.2f"},

  /* Up to 4 so quiet surfaces can reach the other layers; the bottom position means zero. */
  [CONTROL_RAIN_GAIN] = {"Rain", FIELD(rain.gain), 0.0f, 4.0f, 0.01f, GUI_SCALE_LOG_OFF, 1.0f,
                         "%.2f"},
  [CONTROL_BED_GAIN] = LINEAR("Rain bed ×", rain.bed_gain, 0.0f, 4.0f, "%.2f"),
  [CONTROL_DROP_RATE] = LINEAR("Played drops/s", rain.max_drops_per_s, 0.0f, 2000.0f, "%.0f"),
  [CONTROL_RAIN_MIN_DISTANCE] = LOG("Minimum distance (m)", rain.min_distance_m,
                                    0.25f, 100.0f, "%.3f"),
  [CONTROL_RAIN_MAX_DISTANCE] = LOG("Maximum distance (m)", rain.max_distance_m,
                                    0.25f, 100.0f, "%.3f"),
  [CONTROL_SHEET_DEPTH] = LINEAR("Gust sheet depth", rain.sheet_depth, 0.0f, 2.0f, "%.2f"),

  /* The coverage slider spans 1e-5 to 1; its bottom position means zero. */
  [CONTROL_SURFACE_COVERAGE] = {"Coverage", SURFACE(coverage), 0.0f, 1.0f, 1e-5f,
                                GUI_SCALE_LOG_OFF, 1.0f, "%.3g"},
  [CONTROL_SURFACE_GAIN] = {"Level ×", SURFACE(gain), 0.0f, 4.0f, 0.01f, GUI_SCALE_LOG_OFF,
                            1.0f, "%.2f"},
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

};

/* Each lower bound stays at or below its upper bound. */
static const gui_control_id bound_pairs[][2] = {
  {CONTROL_STORM_MIN_SEVERITY, CONTROL_STORM_MAX_SEVERITY},
  {CONTROL_SHAPE_PEAK_RAIN_MIN, CONTROL_SHAPE_PEAK_RAIN_MAX},
  {CONTROL_SHAPE_FRONT_MIN, CONTROL_SHAPE_FRONT_MAX},
  {CONTROL_SHAPE_OUTFLOW_MIN, CONTROL_SHAPE_OUTFLOW_MAX},
  {CONTROL_SHAPE_COOLING_MIN, CONTROL_SHAPE_COOLING_MAX},
  {CONTROL_SHAPE_LIGHTNING_MIN, CONTROL_SHAPE_LIGHTNING_MAX},
  {CONTROL_RAIN_MIN_DISTANCE, CONTROL_RAIN_MAX_DISTANCE},
  {CONTROL_CRICKET_MIN_DISTANCE, CONTROL_CRICKET_MAX_DISTANCE},
  {CONTROL_CICADA_MIN_DISTANCE, CONTROL_CICADA_MAX_DISTANCE},
  {CONTROL_CLICK_GAIN_MIN, CONTROL_CLICK_GAIN_MAX},
  {CONTROL_CLICK_FREQUENCY_MIN, CONTROL_CLICK_FREQUENCY_MAX},
  {CONTROL_BUBBLE_RADIUS_MIN, CONTROL_BUBBLE_RADIUS_MAX},
  {CONTROL_BUBBLE_GAIN_MIN, CONTROL_BUBBLE_GAIN_MAX},
  {CONTROL_BUBBLE_DECAY_MIN, CONTROL_BUBBLE_DECAY_MAX},
};

void gui_startup_config(noise_config *c) {
  noise_config_default(c);
  memset(c->ambient_gain, 0, sizeof(c->ambient_gain));
  c->storm.manual = 0;
  c->wind.gain = 0.5f;
  c->crickets.gain = 0.5f;
  c->rain.gain = 0.5f;
  c->master_gain = 0.80f;
  c->wind.stereo_width = 0.78f;
  c->crickets.call_rate_scale = 0.21f;
  c->crickets.pitch_hz = 4500.0f;
  c->crickets.pitch_variation = 0.24f;
  c->crickets.placement.stereo_width = 1.00f;
  c->cicadas.gain = 0.5f;
  c->cicadas.species = CICADA_HIGURASHI;
  c->cicadas.pitch_hz = 5000.0f;
  c->cicadas.click_rate_scale = 0.51f;
  c->cicadas.chorus = 0.69f;
  c->cicadas.placement.stereo_width = 1.00f;
  c->cicadas.placement.min_distance_m = 1.205f;
  c->cicadas.placement.max_distance_m = 13.346f;
  c->thunder.gain = 0.5f;
  c->rain.max_drops_per_s = 2000.0f;
  c->rain.surface[WATER].coverage = 0.0142f;
  c->rain.surface[DIRT].coverage = 0.0116f;
  c->rain.surface[LEAF].coverage = 0.000717f;
  c->rain.surface[CONCRETE].coverage = 0.00396f;
  c->rain.surface[GLASS].coverage = 5.40e-05f;
  c->rain.surface[METAL].coverage = 6.31e-05f;
  c->rain.surface[PLASTIC].coverage = 0.000926f;
  c->rain.surface[ASPHALT].coverage = 0.602f;
  c->rain.surface[ASPHALT_ROOF].coverage = 0.367f;
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

static int only_nonzero_surface(const noise_config *c, unsigned surface) {
  for (unsigned i = 0; i < c->rain.surface_count; ++i) {
    if (i != surface && c->rain.surface[i].coverage > 0.0f) return 0;
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
  if (id == CONTROL_SURFACE_COVERAGE && value == 0.0f && only_nonzero_surface(config, surface)) {
    value = COVERAGE_KEPT;
    note = COVERAGE_KEPT_NOTE;
  }
  *field(config, surface, id) = value;
  for (size_t i = 0; i < sizeof(bound_pairs) / sizeof(bound_pairs[0]); ++i) {
    float *lower = field(config, surface, bound_pairs[i][0]);
    float *upper = field(config, surface, bound_pairs[i][1]);
    if (id == bound_pairs[i][0] && *upper < value) *upper = value;
    if (id == bound_pairs[i][1] && *lower > value) *lower = value;
  }
  /* Growth and decay share one track. */
  noise_storm_shape *shape = &config->storm.shape;
  float *other = id == CONTROL_SHAPE_BUILD ? &shape->decay_share :
                 id == CONTROL_SHAPE_DECAY ? &shape->build_share : NULL;
  if (other) {
    *other = fminf(*other, 1.0f - value);
    /* 1 - value can round so the sum lands one step above 1. */
    if (value + *other > 1.0f) *other = nextafterf(*other, 0.0f);
  }
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
  added->coverage = 0.0f;
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
  for (unsigned i = 0; i < rain->surface_count; ++i) sum += rain->surface[i].coverage;
  if (sum > 0.0f) return NULL;
  rain->surface[0].coverage = COVERAGE_KEPT;
  return COVERAGE_KEPT_NOTE;
}

const char *gui_surface_rename(noise_config *config, unsigned surface, const char *name) {
  if (!name[0]) return "Enter a name";
  noise_surface *s = &config->rain.surface[surface];
  return copy_name(s->name, sizeof(s->name), name) ? NULL : "Name shortened to fit";
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

/* Writes a rate such as 310k with three significant figures. */
static void format_rate(float rate, char *text, size_t size) {
  if (rate >= 1e6f) {
    snprintf(text, size, "%.3gM", (double)(rate / 1e6f));
  } else if (rate >= 1e3f) {
    snprintf(text, size, "%.3gk", (double)(rate / 1e3f));
  } else {
    snprintf(text, size, "%.0f", (double)rate);
  }
}

static double degrees(float radians) {
  return fmod(radians * 57.29577951308232 + 360.0, 360.0);
}

/* Appends the reasons in quiet, measured against the thresholds, joined by commas. */
static void append_reasons(unsigned quiet, const noise_weather *w, float min_temperature_c,
                           float max_rain_mm_h, float max_wind_m_s, char *text, size_t size) {
  const char *separator = "";
  size_t used = strlen(text);
  if (quiet & NOISE_QUIET_COLD) {
    used += snprintf(text + used, size - used, "%s%.1f °C, below %.1f °C", separator,
                     (double)w->temperature_c, (double)min_temperature_c);
    separator = "; ";
  }
  if ((quiet & NOISE_QUIET_RAIN) && used < size) {
    used += snprintf(text + used, size - used, "%srain %.2f mm/h, above %.2f", separator,
                     (double)w->rain_mm_h, (double)max_rain_mm_h);
    separator = "; ";
  }
  if ((quiet & NOISE_QUIET_WIND) && used < size) {
    snprintf(text + used, size - used, "%swind %.1f m/s, above %.1f", separator,
             (double)w->wind_mean_m_s, (double)max_wind_m_s);
  }
}

void gui_layer_status(gui_layer layer, const noise_status *status, const noise_config *config,
                      char *text, size_t size) {
  const noise_weather *w = &status->weather;
  char played[16], arrivals[16];
  text[0] = '\0';
  switch (layer) {
    case GUI_LAYER_RAIN:
      if (config->rain.gain <= 0.0f) {
        snprintf(text, size, "Off");
      } else if (w->rain_mm_h <= 0.0f) {
        snprintf(text, size, "No rain");
      } else {
        format_rate(status->rain_played_per_s, played, sizeof(played));
        format_rate(status->rain_arrivals_per_s, arrivals, sizeof(arrivals));
        snprintf(text, size, "%.1f mm/h · %s of %s drops/s played", (double)w->rain_mm_h,
                 played, arrivals);
      }
      break;
    case GUI_LAYER_BED:
      if (config->rain.gain <= 0.0f || w->rain_mm_h <= 0.0f) {
        snprintf(text, size, "No rain");
      } else if (status->bed_share <= 0.0f) {
        snprintf(text, size, "Silent: every drop plays one by one");
      } else {
        snprintf(text, size, "Plays %.0f%% of rain power at ×1",
                 100.0 * status->bed_share);
      }
      break;
    case GUI_LAYER_WIND:
      if (config->wind.gain <= 0.0f) {
        snprintf(text, size, "Off");
      } else {
        snprintf(text, size, "%.1f m/s (mean %.1f) from %.0f° · level ×%.2f",
                 (double)w->wind_m_s, (double)w->wind_mean_m_s, degrees(w->wind_bearing_rad),
                 (double)status->wind_level);
      }
      break;
    case GUI_LAYER_CRICKETS:
      if (config->crickets.gain <= 0.0f) {
        snprintf(text, size, "Off");
      } else if (status->cricket_quiet) {
        snprintf(text, size, "Silent: ");
        append_reasons(status->cricket_quiet, w, config->crickets.min_temperature_c,
                       config->crickets.max_rain_mm_h, config->crickets.max_wind_m_s,
                       text, size);
      } else {
        snprintf(text, size, "%.2f chirps/s · %u of %u in a singing bout",
                 (double)status->cricket_rate_hz, status->crickets_singing,
                 NOISE_CRICKET_VOICES);
      }
      break;
    case GUI_LAYER_CICADAS:
      if (config->cicadas.gain <= 0.0f) {
        snprintf(text, size, "Off");
      } else if (status->cicada_quiet) {
        snprintf(text, size, "Silent: ");
        append_reasons(status->cicada_quiet, w, config->cicadas.min_temperature_c,
                       config->cicadas.max_rain_mm_h, 0.0f, text, size);
        if (status->cicada_activity > 0.01f) {
          size_t used = strlen(text);
          snprintf(text + used, size - used, " · chorus fading, %.0f%%",
                   100.0 * status->cicada_activity);
        }
      } else if (status->cicada_activity < 0.99f) {
        snprintf(text, size, "Singing · chorus fading in, %.0f%%",
                 100.0 * status->cicada_activity);
      } else {
        snprintf(text, size, "Singing");
      }
      break;
    case GUI_LAYER_THUNDER:
      if (config->thunder.gain <= 0.0f) {
        snprintf(text, size, "Off");
      } else if (w->lightning_per_min <= 0.0f) {
        snprintf(text, size, "No lightning");
      } else if (w->cell.distance_m > NOISE_THUNDER_MAX_DISTANCE_M) {
        snprintf(text, size, "%.1f flashes/min · storm %.1f km away; strikes past %.0f km "
                 "are silent", (double)w->lightning_per_min, w->cell.distance_m / 1000.0,
                 NOISE_THUNDER_MAX_DISTANCE_M / 1000.0);
      } else {
        snprintf(text, size, "%.1f flashes/min · storm %.1f km at %.0f°",
                 (double)w->lightning_per_min, w->cell.distance_m / 1000.0,
                 degrees(w->cell.angle_rad));
      }
      break;
    case GUI_LAYER_COUNT:
      break;
  }
}

void gui_weather_summary(const noise_status *status, char *text, size_t size) {
  const noise_weather *w = &status->weather;
  int used = snprintf(text, size, "Rain %.1f mm/h · wind %.1f m/s from %.0f° · %.1f °C · "
                      "%.1f flashes/min", (double)w->rain_mm_h, (double)w->wind_m_s,
                      degrees(w->wind_bearing_rad), (double)w->temperature_c,
                      (double)w->lightning_per_min);
  if (used < 0 || (size_t)used >= size) return;
  if (w->cell.distance_m > 0.0f) {
    snprintf(text + used, size - used, " · storm %.1f km at %.0f°",
             w->cell.distance_m / 1000.0, degrees(w->cell.angle_rad));
  } else {
    snprintf(text + used, size - used, " · no storm");
  }
}

void gui_silenced_summary(const noise_status *status, const noise_config *config,
                          char *text, size_t size) {
  static const struct {
    unsigned flag;
    const char *name;
  } reasons[] = {{NOISE_QUIET_COLD, "cold"}, {NOISE_QUIET_RAIN, "rain"},
                 {NOISE_QUIET_WIND, "wind"}};
  const struct {
    const char *layer;
    float gain;
    unsigned quiet;
  } layers[] = {
    {"crickets", config->crickets.gain, status->cricket_quiet},
    {"cicadas", config->cicadas.gain, status->cicada_quiet},
  };
  size_t used = 0;
  text[0] = '\0';
  for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]) && used < size; ++i) {
    if (layers[i].gain <= 0.0f || !layers[i].quiet) continue;
    used += snprintf(text + used, size - used, "%s%s (", used ? " · " : "Silenced: ",
                     layers[i].layer);
    const char *separator = "";
    for (size_t r = 0; r < sizeof(reasons) / sizeof(reasons[0]) && used < size; ++r) {
      if (!(layers[i].quiet & reasons[r].flag)) continue;
      used += snprintf(text + used, size - used, "%s%s", separator, reasons[r].name);
      separator = ", ";
    }
    if (used < size) used += snprintf(text + used, size - used, ")");
  }
}
