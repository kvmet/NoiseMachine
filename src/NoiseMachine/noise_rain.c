#include "noise_rain.h"
#include "noise_weather.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

#define NOISE_GRAVITY 9.81f
#define NOISE_WATER_DENSITY 1000.0f
#define NOISE_PRESSURE_PA 101325.0f

#define BUBBLE_RADIUS_MIN_M 0.00016f
#define BUBBLE_RADIUS_MAX_M 0.004f

/* Every preset shares the click from [1, section 4.1.1]: 1 to 16 kHz, damping 2f. */
#define CLICK .click_frequency_min_hz = 1000.0f, .click_frequency_max_hz = 16000.0f, \
              .click_damping_ratio = 2.0f
/* Bubble fields a surface without bubbles still needs to be valid. */
#define NO_BUBBLE .bubble_radius_min_m = 0.00035f, .bubble_radius_max_m = 0.0016f, \
                  .bubble_gain_min = 1.0f, .bubble_gain_max = 1.0f, \
                  .bubble_decay_min = 1.0f, .bubble_decay_max = 1.0f, .bubble_delay_s = 0.002f
#define SOLID(click, f1, d1, f2, d2, resonance, lowpass) { \
    .click_gain_min = click, .click_gain_max = click, CLICK, \
    .mode = {{f1, d1, resonance}, {f2, d2, 0.5f * resonance}}, \
    .detune = 0.15f, .lowpass_hz = lowpass, NO_BUBBLE}

static const noise_surface presets[NOISE_SURFACE_PRESET_COUNT] = {
  [WATER] = {
    .click_gain_min = 0.15f, .click_gain_max = 0.5f, CLICK,
    .mode = {{1000.0f, 1000.0f, 0.0f}, {1000.0f, 1000.0f, 0.0f}},
    .bubble_probability = 0.85f,
    .bubble_radius_min_m = 0.00035f, .bubble_radius_max_m = 0.0016f,
    .bubble_gain_min = 1.2f, .bubble_gain_max = 2.5f,
    .bubble_decay_min = 3.0f, .bubble_decay_max = 8.0f,
    .bubble_delay_s = 0.002f},
  [DIRT] = SOLID(1.0f, 450.0f, 1200.0f, 1100.0f, 1800.0f, 0.35f, 0.0f),
  [LEAF] = SOLID(1.0f, 1800.0f, 800.0f, 4200.0f, 1400.0f, 0.5f, 0.0f),
  [CONCRETE] = SOLID(1.0f, 1400.0f, 1400.0f, 3700.0f, 2200.0f, 0.45f, 0.0f),
  [GLASS] = SOLID(1.0f, 3200.0f, 160.0f, 7100.0f, 260.0f, 0.325f, 0.0f),
  [METAL] = SOLID(1.0f, 1700.0f, 90.0f, 4300.0f, 150.0f, 0.4f, 0.0f),
  [PLASTIC] = SOLID(0.5f, 220.0f, 110.0f, 650.0f, 220.0f, 0.65f, 1600.0f),
  [ASPHALT] = SOLID(0.3f, 300.0f, 1600.0f, 900.0f, 2600.0f, 0.25f, 0.0f),
  [ASPHALT_ROOF] = SOLID(0.25f, 140.0f, 300.0f, 420.0f, 700.0f, 0.4f, 900.0f),
};

void noise_surface_preset(noise_surface *surface, surface_preset preset) {
  float weight = surface->weight;
  *surface = presets[preset];
  surface->weight = weight;
}

static int mode_valid(const noise_surface_mode *m) {
  return in_range(m->frequency_hz, 20.0f, 20000.0f) &&
         in_range(m->damping_per_s, 1.0f, 20000.0f) &&
         in_range(m->gain, 0.0f, 4.0f);
}

static int surface_valid(const noise_surface *s) {
  return in_range(s->weight, 0.0f, 1000.0f) &&
         in_range(s->click_gain_min, 0.0f, 2.0f) &&
         in_range(s->click_gain_max, s->click_gain_min, 2.0f) &&
         in_range(s->click_frequency_min_hz, 20.0f, 20000.0f) &&
         in_range(s->click_frequency_max_hz, s->click_frequency_min_hz, 20000.0f) &&
         in_range(s->click_damping_ratio, 0.05f, 50.0f) &&
         mode_valid(&s->mode[0]) && mode_valid(&s->mode[1]) &&
         in_range(s->detune, 0.0f, 0.5f) &&
         (s->lowpass_hz == 0.0f || in_range(s->lowpass_hz, 20.0f, 20000.0f)) &&
         in_range(s->bubble_probability, 0.0f, 1.0f) &&
         in_range(s->bubble_radius_min_m, BUBBLE_RADIUS_MIN_M, BUBBLE_RADIUS_MAX_M) &&
         in_range(s->bubble_radius_max_m, s->bubble_radius_min_m, BUBBLE_RADIUS_MAX_M) &&
         in_range(s->bubble_gain_min, 0.0f, 8.0f) &&
         in_range(s->bubble_gain_max, s->bubble_gain_min, 8.0f) &&
         in_range(s->bubble_decay_min, 0.25f, 20.0f) &&
         in_range(s->bubble_decay_max, s->bubble_decay_min, 20.0f) &&
         in_range(s->bubble_delay_s, 0.0f, 0.1f);
}

int noise_rain_config_valid(const noise_rain_config *c) {
  if (!in_range(c->gain, 0.0f, 1.0f) ||
      !in_range(c->max_drops_per_s, 0.0f, 2000.0f) ||
      !in_range(c->min_distance_m, 0.25f, 100.0f) ||
      !in_range(c->max_distance_m, c->min_distance_m, 100.0f) ||
      !in_range(c->fall_height_m, 0.01f, 1000.0f)) {
    return 0;
  }
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) {
    if (!surface_valid(&c->surface[i])) return 0;
    sum += c->surface[i].weight;
  }
  return sum > 0.0f;
}

void noise_rain_config_default(noise_rain_config *c) {
  static const float weights[NOISE_SURFACE_SLOTS] = {
    [WATER] = 0.37f, [DIRT] = 0.21f, [LEAF] = 0.26f, [CONCRETE] = 0.15f,
    [GLASS] = 0.005f, [METAL] = 0.005f};
  c->gain = 0.5f;
  c->max_drops_per_s = 900.0f;
  c->min_distance_m = 0.75f;
  c->max_distance_m = 5.0f;
  c->fall_height_m = 20.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) {
    noise_surface_preset(&c->surface[i], (surface_preset)i);
    c->surface[i].weight = weights[i];
  }
}

void noise_rain_init(noise_rain *rain, uint32_t seed) {
  rain->arrival_rng = stream_seed(seed, 0x3c6ef372u);
  rain->drop_rng = stream_seed(seed, 0xdaa66d2bu);
}

void noise_rain_configure(noise_rain *rain, const noise_rain_config *c) {
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) sum += c->surface[i].weight;
  float cumulative = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) {
    cumulative += c->surface[i].weight;
    rain->surface_cdf[i] = cumulative / sum;
  }
}

int noise_drop_valid(const droplet *drop) {
  return drop->surface < NOISE_SURFACE_SLOTS &&
         in_range(drop->radius_m, 0.0004f, 0.0029f) &&
         in_range(drop->velocity_m_s, 0.0f, 12.0f) &&
         in_range(drop->position.distance_m, 0.25f, 100.0f) &&
         in_range(drop->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         (drop->bubble_radius_m == 0.0f ||
          in_range(drop->bubble_radius_m, BUBBLE_RADIUS_MIN_M, BUBBLE_RADIUS_MAX_M));
}

/* Skips the draw for an empty range so fixed values leave the stream untouched. */
static float sample_range(uint32_t *rng, float low, float high) {
  return low < high ? random_between(rng, low, high) : low;
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
  const noise_surface *s = &c->surface[drop->surface];
  float radius_ratio = drop->radius_m / 0.0005f;
  float amplitude = 0.035f * sqrtf(radius_ratio * radius_ratio * radius_ratio) *
                    drop->velocity_m_s / 4.0f;
  float click_gain = sample_range(&rain->drop_rng, s->click_gain_min, s->click_gain_max);
  voice->material_lowpass_alpha = s->lowpass_hz > 0.0f ?
      -expm1f(-2.0f * NOISE_PI * s->lowpass_hz / NOISE_SAMPLE_RATE_HZ) : 1.0f;
  float click_frequency = sample_range(&rain->drop_rng, s->click_frequency_min_hz,
                                       s->click_frequency_max_hz);
  mode_init(&voice->mode[NOISE_DROP_CLICK], click_frequency,
            s->click_damping_ratio * click_frequency, amplitude * click_gain, 0);
  if (s->mode[0].gain > 0.0f || s->mode[1].gain > 0.0f) {
    float tuning = sample_range(&rain->drop_rng, 1.0f - s->detune, 1.0f + s->detune);
    for (unsigned m = 0; m < 2; ++m) {
      const noise_surface_mode *mode = &s->mode[m];
      if (mode->gain > 0.0f) {
        mode_init(&voice->mode[NOISE_DROP_RESONANCE + m], mode->frequency_hz * tuning,
                  mode->damping_per_s, amplitude * mode->gain, 0);
      }
    }
  }
  if (drop->bubble_radius_m > 0.0f) {
    float r = drop->bubble_radius_m;
    float frequency = sqrtf(3.0f * 1.4f * NOISE_PRESSURE_PA / NOISE_WATER_DENSITY) /
                      (2.0f * NOISE_PI * r);
    float damping = 0.13f / r + 0.0072f / (r * sqrtf(r));
    float decay = sample_range(&rain->drop_rng, s->bubble_decay_min, s->bubble_decay_max);
    float bubble_gain = sample_range(&rain->drop_rng, s->bubble_gain_min, s->bubble_gain_max);
    mode_init(&voice->mode[NOISE_DROP_BUBBLE], frequency, damping / decay,
              bubble_gain * click_gain * amplitude,
              (uint32_t)(s->bubble_delay_s * NOISE_SAMPLE_RATE_HZ));
  }
  noise_spatial_init(&voice->spatial, listener, drop->position);
  voice->filter_tail = 256;
  return NOISE_OK;
}

static unsigned choose_surface(noise_rain *rain, const noise_rain_config *c,
                               const noise_weather_config *weather, float intensity) {
  const float *mod_amount = &weather->mod_amount[WEATHER_MOD_SURFACE_WEIGHT];
  int modulated = 0;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) modulated |= mod_amount[i] != 0.0f;
  float choice = random_unit(&rain->drop_rng);
  if (!modulated) {
    unsigned surface = 0;
    while (surface + 1 < NOISE_SURFACE_SLOTS && choice >= rain->surface_cdf[surface]) ++surface;
    return surface;
  }

  float source = 2.0f * intensity - 1.0f;
  float weight[NOISE_SURFACE_SLOTS];
  float total = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) {
    float scale = fmaxf(0.001f, 1.0f + mod_amount[i] * source);
    weight[i] = c->surface[i].weight * scale;
    total += weight[i];
  }
  float target = choice * total;
  float cumulative = 0.0f;
  unsigned surface = 0;
  while (surface + 1 < NOISE_SURFACE_SLOTS) {
    cumulative += weight[surface];
    if (target < cumulative) break;
    ++surface;
  }
  return surface;
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
  const noise_surface *s = &c->surface[drop.surface];
  if (s->bubble_probability > 0.0f && random_unit(&rain->drop_rng) < s->bubble_probability) {
    drop.bubble_radius_m = s->bubble_radius_min_m < s->bubble_radius_max_m ?
        random_log_between(&rain->drop_rng, s->bubble_radius_min_m, s->bubble_radius_max_m) :
        s->bubble_radius_min_m;
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
    for (unsigned m = 0; m < NOISE_DROP_MODES; ++m) {
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
