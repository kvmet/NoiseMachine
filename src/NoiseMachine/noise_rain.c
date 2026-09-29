#include "noise_rain.h"
#include "noise_weather.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

#define NOISE_GRAVITY 9.81f
#define NOISE_WATER_DENSITY 1000.0f
#define NOISE_PRESSURE_PA 101325.0f

/* Presets add source low-pass cutoff after impact gain; zero bypasses it. */
static const float material_modes[NOISE_SURFACE_COUNT][7] = {
  {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
  {450.0f, 1200.0f, 1100.0f, 1800.0f, 0.35f, 1.0f, 0.0f},
  {1800.0f, 800.0f, 4200.0f, 1400.0f, 0.5f, 1.0f, 0.0f},
  {1400.0f, 1400.0f, 3700.0f, 2200.0f, 0.45f, 1.0f, 0.0f},
  {3200.0f, 160.0f, 7100.0f, 260.0f, 0.325f, 1.0f, 0.0f},
  {1700.0f, 90.0f, 4300.0f, 150.0f, 0.4f, 1.0f, 0.0f},
  {220.0f, 110.0f, 650.0f, 220.0f, 0.65f, 0.5f, 1600.0f},
  {300.0f, 1600.0f, 900.0f, 2600.0f, 0.25f, 0.3f, 0.0f},
  {140.0f, 300.0f, 420.0f, 700.0f, 0.4f, 0.25f, 900.0f}
};

static int water_config_valid(const noise_water_config *c) {
  return in_range(c->impact_gain_min, 0.0f, 2.0f) &&
         in_range(c->impact_gain_max, c->impact_gain_min, 2.0f) &&
         in_range(c->bubble_probability, 0.0f, 1.0f) &&
         in_range(c->bubble_radius_min_m, 0.00016f, 0.004f) &&
         in_range(c->bubble_radius_max_m, c->bubble_radius_min_m, 0.004f) &&
         in_range(c->bubble_gain_min, 0.0f, 8.0f) &&
         in_range(c->bubble_gain_max, c->bubble_gain_min, 8.0f) &&
         in_range(c->bubble_decay_min, 0.25f, 20.0f) &&
         in_range(c->bubble_decay_max, c->bubble_decay_min, 20.0f);
}

int noise_rain_config_valid(const noise_rain_config *c) {
  if (!in_range(c->gain, 0.0f, 1.0f) ||
      !in_range(c->max_drops_per_s, 0.0f, 2000.0f) ||
      !in_range(c->min_distance_m, 0.25f, 100.0f) ||
      !in_range(c->max_distance_m, c->min_distance_m, 100.0f) ||
      !in_range(c->fall_height_m, 0.01f, 1000.0f) ||
      !water_config_valid(&c->water)) {
    return 0;
  }
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    if (!in_range(c->surface_weight[i], 0.0f, 1000.0f)) return 0;
    sum += c->surface_weight[i];
  }
  return sum > 0.0f;
}

void noise_rain_config_default(noise_rain_config *c) {
  c->gain = 0.5f;
  c->max_drops_per_s = 900.0f;
  c->surface_weight[WATER] = 0.37f;
  c->surface_weight[DIRT] = 0.21f;
  c->surface_weight[LEAF] = 0.26f;
  c->surface_weight[CONCRETE] = 0.15f;
  c->surface_weight[GLASS] = 0.005f;
  c->surface_weight[METAL] = 0.005f;
  c->min_distance_m = 0.75f;
  c->max_distance_m = 5.0f;
  c->fall_height_m = 20.0f;
  c->water.impact_gain_min = 0.15f;
  c->water.impact_gain_max = 0.5f;
  c->water.bubble_probability = 0.85f;
  c->water.bubble_radius_min_m = 0.00035f;
  c->water.bubble_radius_max_m = 0.0016f;
  c->water.bubble_gain_min = 1.2f;
  c->water.bubble_gain_max = 2.5f;
  c->water.bubble_decay_min = 3.0f;
  c->water.bubble_decay_max = 8.0f;
}

void noise_rain_init(noise_rain *rain, uint32_t seed) {
  rain->arrival_rng = stream_seed(seed, 0x3c6ef372u);
  rain->drop_rng = stream_seed(seed, 0xdaa66d2bu);
}

void noise_rain_configure(noise_rain *rain, const noise_rain_config *c) {
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) sum += c->surface_weight[i];
  float cumulative = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    cumulative += c->surface_weight[i];
    rain->surface_cdf[i] = cumulative / sum;
  }
}

int noise_drop_valid(const droplet *drop) {
  return (unsigned)drop->surface < NOISE_SURFACE_COUNT &&
         in_range(drop->radius_m, 0.0004f, 0.0029f) &&
         in_range(drop->velocity_m_s, 0.0f, 12.0f) &&
         in_range(drop->position.distance_m, 0.25f, 100.0f) &&
         in_range(drop->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         (drop->bubble_radius_m == 0.0f ||
          (drop->surface == WATER && in_range(drop->bubble_radius_m, 0.00016f, 0.004f)));
}

noise_result noise_rain_start_drop(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                               const noise_listener_config *listener, const droplet *drop) {
  if (drop->velocity_m_s == 0.0f) return NOISE_OK;
  if (state->active_drops == NOISE_MAX_DROPLETS) {
    ++state->dropped_drops;
    return NOISE_VOICE_LIMIT;
  }
  noise_drop_voice *voice = &rain->voice[state->active_drops++];
  memset(voice, 0, sizeof(*voice));
  ++state->generated_drops;
  if (state->active_drops > state->peak_active_drops) {
    state->peak_active_drops = state->active_drops;
  }
  const noise_water_config *water = &c->water;
  float radius_ratio = drop->radius_m / 0.0005f;
  float amplitude = 0.035f * sqrtf(radius_ratio * radius_ratio * radius_ratio) *
                    drop->velocity_m_s / 4.0f;
  float impact_gain = drop->surface == WATER ?
      random_between(&rain->drop_rng, water->impact_gain_min, water->impact_gain_max) :
      material_modes[drop->surface][5];
  float material_cutoff = material_modes[drop->surface][6];
  voice->material_lowpass_alpha = material_cutoff > 0.0f ?
      -expm1f(-2.0f * NOISE_PI * material_cutoff / NOISE_SAMPLE_RATE_HZ) : 1.0f;
  float impact_frequency = 1000.0f + 15000.0f * random_unit(&rain->drop_rng);
  mode_init(&voice->mode[0], impact_frequency, 2.0f * impact_frequency,
            amplitude * impact_gain, 0);
  if (drop->surface == WATER && drop->bubble_radius_m > 0.0f) {
    float r = drop->bubble_radius_m;
    float frequency = sqrtf(3.0f * 1.4f * NOISE_PRESSURE_PA / NOISE_WATER_DENSITY) /
                      (2.0f * NOISE_PI * r);
    float damping = 0.13f / r + 0.0072f / (r * sqrtf(r));
    float decay = random_between(&rain->drop_rng, water->bubble_decay_min,
                                 water->bubble_decay_max);
    float bubble_gain = random_between(&rain->drop_rng, water->bubble_gain_min,
                                       water->bubble_gain_max);
    /* Bubble onset follows the impact; 2 ms is an audible-design choice. */
    mode_init(&voice->mode[1], frequency, damping / decay,
              bubble_gain * impact_gain * amplitude,
              (uint32_t)(0.002f * NOISE_SAMPLE_RATE_HZ));
  } else if (drop->surface != WATER) {
    const float *m = material_modes[drop->surface];
    float tuning = 0.85f + 0.3f * random_unit(&rain->drop_rng);
    mode_init(&voice->mode[1], m[0] * tuning, m[1], amplitude * m[4], 0);
    mode_init(&voice->mode[2], m[2] * tuning, m[3], amplitude * m[4] * 0.5f, 0);
  }
  noise_spatial_init(&voice->spatial, listener, drop->position);
  voice->filter_tail = 256;
  return NOISE_OK;
}

static impact_surface choose_surface(noise_rain *rain, const noise_rain_config *c,
                                     const noise_weather_config *weather, float intensity) {
  const float *mod_amount = &weather->mod_amount[WEATHER_MOD_WATER_WEIGHT];
  int modulated = 0;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) modulated |= mod_amount[i] != 0.0f;
  float choice = random_unit(&rain->drop_rng);
  if (!modulated) {
    unsigned surface = WATER;
    while (surface + 1 < NOISE_SURFACE_COUNT && choice >= rain->surface_cdf[surface]) ++surface;
    return (impact_surface)surface;
  }

  float source = 2.0f * intensity - 1.0f;
  float weight[NOISE_SURFACE_COUNT];
  float total = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    float scale = fmaxf(0.001f, 1.0f + mod_amount[i] * source);
    weight[i] = c->surface_weight[i] * scale;
    total += weight[i];
  }
  float target = choice * total;
  float cumulative = 0.0f;
  unsigned surface = WATER;
  while (surface + 1 < NOISE_SURFACE_COUNT) {
    cumulative += weight[surface];
    if (target < cumulative) break;
    ++surface;
  }
  return (impact_surface)surface;
}

static void spawn_rain(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_weather_config *weather,
                       const noise_listener_config *listener) {
  const float *mod_amount = weather->mod_amount;
  float intensity = state->rain_intensity;
  float size_intensity = 0.5f + mod_amount[WEATHER_MOD_DROP_SIZE] * (intensity - 0.5f);
  float blend = size_intensity < 0.5f ? size_intensity * 2.0f :
      (size_intensity - 0.5f) * 2.0f;
  unsigned lo = size_intensity < 0.5f ? 0u : 1u;
  static const float distribution[3][2] = {{0.84f, 0.16f}, {0.32f, 0.61f}, {0.24f, 0.52f}};
  float small = distribution[lo][0] + blend * (distribution[lo + 1][0] - distribution[lo][0]);
  float medium = distribution[lo][1] + blend * (distribution[lo + 1][1] - distribution[lo][1]);
  float size = random_unit(&rain->drop_rng);
  float u = random_unit(&rain->drop_rng);
  float diameter_mm = size < small ? 0.8f + 0.3f * u :
      (size < small + medium ? 1.1f + 1.1f * u : 2.2f + 3.6f * u);
  /* Dingle-Lee fit uses millimetres and returns centimetres per second. */
  float d = diameter_mm;
  float terminal = d <= 1.4f ?
      -17.8951f + d * (448.9498f + d * (16.3719f - 45.9516f * d)) :
       24.1660f + d * (448.8336f + d * (-75.6265f + 4.2695f * d));
  terminal *= 0.01f;
  droplet drop;
  drop.radius_m = diameter_mm * 0.0005f;
  float fall_height = noise_weather_mod_log(c->fall_height_m,
      mod_amount[WEATHER_MOD_FALL_HEIGHT], intensity, 0.01f, 1000.0f);
  drop.velocity_m_s = terminal * sqrtf(-expm1f(
      -2.0f * NOISE_GRAVITY * fall_height / (terminal * terminal)));
  drop.surface = choose_surface(rain, c, weather, intensity);
  drop.bubble_radius_m = 0.0f;
  const noise_water_config *water = &c->water;
  if (drop.surface == WATER && random_unit(&rain->drop_rng) < water->bubble_probability) {
    drop.bubble_radius_m = random_log_between(&rain->drop_rng,
        water->bubble_radius_min_m, water->bubble_radius_max_m);
  }
  float near = noise_weather_mod_log(c->min_distance_m,
      mod_amount[WEATHER_MOD_MIN_DISTANCE], intensity, 0.25f, 100.0f);
  float far = noise_weather_mod_log(c->max_distance_m,
      mod_amount[WEATHER_MOD_MAX_DISTANCE], intensity, 0.25f, 100.0f);
  if (near > far) {
    float swap = near;
    near = far;
    far = swap;
  }
  drop.position.distance_m = area_uniform_distance(near, far, random_unit(&rain->drop_rng));
  drop.position.angle_rad = 2.0f * NOISE_PI * random_unit(&rain->drop_rng);
  /* Capacity losses are recorded by noise_rain_start_drop for both arrival paths. */
  (void)noise_rain_start_drop(rain, state, c, listener, &drop);
}

float noise_rain_next(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_weather_config *weather,
                       const noise_listener_config *listener, noise_bus *bus) {
  const float *mod_amount = weather->mod_amount;
  float intensity = state->rain_intensity;
  float gain = noise_weather_mod_linear(c->gain, mod_amount[WEATHER_MOD_RAIN_GAIN],
                                        intensity, 0.0f, 1.0f);
  float density = noise_weather_arrival_level(intensity, mod_amount[WEATHER_MOD_ARRIVAL_RATE]);
  if (gain > 0.0f && density > 0.0f) {
    float probability = density * c->max_drops_per_s / NOISE_SAMPLE_RATE_HZ;
    if (random_unit(&rain->arrival_rng) < probability) {
      spawn_rain(rain, state, c, weather, listener);
    }
  }
  float send = 0.0f;
  unsigned i = 0;
  while (i < state->active_drops) {
    noise_drop_voice *voice = &rain->voice[i];
    float source = 0.0f;
    unsigned remaining = 0;
    for (unsigned m = 0; m < 3; ++m) {
      source += mode_next(&voice->mode[m]);
      remaining += voice->mode[m].remaining + voice->mode[m].delay;
    }
    if (voice->material_lowpass_alpha < 1.0f) {
      for (unsigned stage = 0; stage < 2; ++stage) {
        voice->material_lowpass_state[stage] += voice->material_lowpass_alpha *
            (source - voice->material_lowpass_state[stage]);
        source = voice->material_lowpass_state[stage];
      }
    }
    source *= gain;
    send += source;
    noise_spatial_next(&voice->spatial, listener, bus, source);
    if (!remaining) voice->filter_tail -= 1;
    if (!remaining && voice->filter_tail == 0) {
      /* Keep the active prefix dense to avoid scanning idle voices per sample. */
      state->active_drops -= 1;
      rain->voice[i] = rain->voice[state->active_drops];
    } else {
      ++i;
    }
  }
  return send;
}
