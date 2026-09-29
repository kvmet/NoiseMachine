#include "noise_core.h"

#include <math.h>
#include <string.h>

#define NOISE_PI 3.14159265358979323846f
#define NOISE_GRAVITY 9.81f
#define NOISE_WATER_DENSITY 1000.0f
#define NOISE_PRESSURE_PA 101325.0f

static const unsigned reverb_length[NOISE_REVERB_LINES] = {
  739, 953, 1151, 1327, 1471, 1663
};
static const unsigned reverb_offset[NOISE_REVERB_LINES] = {
  0, 739, 1692, 2843, 4170, 5641
};

static uint32_t random_u32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static float random_unit(uint32_t *state) {
  return (float)(random_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static uint32_t stream_seed(uint32_t seed, uint32_t tag) {
  /* Avalanche the tags so streams do not start at adjacent xorshift positions. */
  uint32_t x = seed + tag;
  x = (x ^ (x >> 16)) * 0x85ebca6bu;
  x = (x ^ (x >> 13)) * 0xc2b2ae35u;
  x ^= x >> 16;
  return x ? x : tag;
}

static int in_range(float value, float low, float high) {
  return isfinite(value) && value >= low && value <= high;
}

static int valid_config(const noise_config *c) {
  if (!c || !in_range(c->master_gain, 0.0f, 1.0f) ||
      !in_range(c->rain_gain, 0.0f, 1.0f) ||
      !in_range(c->rain_intensity, 0.0f, 1.0f) ||
      !in_range(c->min_rain_intensity, 0.0f, 1.0f) ||
      !in_range(c->max_rain_intensity, c->min_rain_intensity, 1.0f) ||
      (c->vary_rain != 0 && c->vary_rain != 1) ||
      !in_range(c->weather_step_s, 0.1f, 3600.0f) ||
      !in_range(c->rain_slew_s, 0.01f, 60.0f) ||
      !in_range(c->max_drops_per_s, 0.0f, 2000.0f) ||
      !in_range(c->min_distance_m, 0.25f, 100.0f) ||
      !in_range(c->max_distance_m, c->min_distance_m, 100.0f) ||
      !in_range(c->fall_height_m, 0.01f, 1000.0f) ||
      !in_range(c->stereo_width_m, 0.0f, 0.5f) ||
      !in_range(c->head_amount, 0.0f, 1.0f) ||
      !in_range(c->rear_amount, 0.0f, 1.0f) ||
      !in_range(c->reverb_gain, 0.0f, 1.0f)) {
    return 0;
  }
  if (c->vary_rain && !in_range(c->rain_intensity,
                               c->min_rain_intensity, c->max_rain_intensity)) {
    return 0;
  }
  for (unsigned i = 0; i < NOISE_KIND_COUNT; ++i) {
    if (!in_range(c->ambient_gain[i], 0.0f, 1.0f)) return 0;
  }
  for (unsigned i = 0; i < NOISE_WEATHER_MOD_COUNT; ++i) {
    if (!in_range(c->weather_mod_amount[i], -1.0f, 1.0f)) return 0;
  }
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    if (!in_range(c->surface_weight[i], 0.0f, 1000.0f)) return 0;
    sum += c->surface_weight[i];
  }
  return sum > 0.0f;
}

void noise_config_default(noise_config *c) {
  memset(c, 0, sizeof(*c));
  c->ambient_gain[NOISE_KIND_PINK] = 0.3f;
  c->master_gain = 0.8f;
  c->rain_gain = 0.5f;
  c->min_rain_intensity = 0.15f;
  c->max_rain_intensity = 0.85f;
  c->weather_step_s = 8.0f;
  c->rain_slew_s = 2.0f;
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
  c->stereo_width_m = 0.18f;
  c->head_amount = 1.0f;
  c->rear_amount = 1.0f;
  c->reverb_gain = 0.12f;
  c->weather_mod_amount[WEATHER_MOD_ARRIVAL_RATE] = 1.0f;
  c->weather_mod_amount[WEATHER_MOD_DROP_SIZE] = 1.0f;
}

noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed) {
  if (!gen || !valid_config(config)) return NOISE_INVALID_CONFIG;
  noise_config copy = *config;
  memset(gen, 0, sizeof(*gen));
  gen->config = copy;
  seed = seed ? seed : 1u;
  gen->ambient_rng = stream_seed(seed, 0x9e3779b9u);
  gen->arrival_rng = stream_seed(seed, 0x3c6ef372u);
  gen->drop_rng = stream_seed(seed, 0xdaa66d2bu);
  gen->weather_rng = stream_seed(seed, 0x78dde6e4u);
  gen->state.rain_intensity = copy.rain_intensity;
  gen->state.rain_target = copy.rain_intensity;
  float span = copy.max_rain_intensity - copy.min_rain_intensity;
  float relative = span > 0.0f ?
      (copy.rain_intensity - copy.min_rain_intensity) / span : 0.0f;
  gen->state.weather_state = relative < 0.25f ? 0u : (relative < 0.75f ? 1u : 2u);
  gen->weather_period = (uint32_t)(copy.weather_step_s * NOISE_SAMPLE_RATE_HZ);
  gen->rain_slew = -expm1f(-1.0f / (copy.rain_slew_s * NOISE_SAMPLE_RATE_HZ));
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) sum += copy.surface_weight[i];
  float cumulative = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    cumulative += copy.surface_weight[i];
    gen->surface_cdf[i] = cumulative / sum;
  }
  for (unsigned i = 0; i < 882; ++i) {
    float phase = 2.0f * NOISE_PI * (float)i / 882.0f;
    gen->hum_table[i] = (sinf(phase) + 0.3f * sinf(2.0f * phase) +
                         0.12f * sinf(3.0f * phase)) / 1.42f;
  }
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    gen->reverb_feedback[i] = powf(0.001f,
        (float)reverb_length[i] / (0.65f * NOISE_SAMPLE_RATE_HZ));
  }
  return NOISE_OK;
}

static void mode_init(noise_mode *mode, float frequency, float damping,
                      float amplitude, uint32_t delay) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  float radius = expf(-damping / NOISE_SAMPLE_RATE_HZ);
  mode->coefficient = 2.0f * radius * cosf(phase);
  mode->radius_squared = radius * radius;
  mode->current = 0.0f;
  mode->previous = -amplitude * sinf(phase) / radius;
  mode->remaining = (uint32_t)ceilf(9.210340372f * NOISE_SAMPLE_RATE_HZ / damping);
  mode->delay = delay;
}

static float mode_next(noise_mode *mode) {
  if (mode->delay) {
    mode->delay -= 1;
    return 0.0f;
  }
  if (!mode->remaining) return 0.0f;
  float value = mode->current;
  float next = mode->coefficient * mode->current -
               mode->radius_squared * mode->previous;
  mode->previous = mode->current;
  mode->current = next;
  mode->remaining -= 1;
  return value;
}

/* Preset frequencies/damping are sound design values, documented in docs/index.md. */
static const float material_modes[NOISE_SURFACE_COUNT][5] = {
  {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
  {450.0f, 1200.0f, 1100.0f, 1800.0f, 0.35f},
  {1800.0f, 800.0f, 4200.0f, 1400.0f, 0.5f},
  {1400.0f, 1400.0f, 3700.0f, 2200.0f, 0.45f},
  {3200.0f, 160.0f, 7100.0f, 260.0f, 0.325f},
  {1700.0f, 90.0f, 4300.0f, 150.0f, 0.4f}
};

static void spatial_init(noise_drop_voice *voice, const noise_config *config,
                         position_polar position) {
  float radius = 0.5f * config->stereo_width_m;
  float distance = position.distance_m;
  float lateral = sinf(position.angle_rad);
  float path[2];
  float k = NOISE_SAMPLE_RATE_HZ * radius / 343.0f;
  int head_enabled = radius > 0.0f && config->head_amount > 0.0f;
  voice->head_feedback = head_enabled ? (k - 1.0f) / (k + 1.0f) : 0.0f;
  for (unsigned ear = 0; ear < 2; ++ear) {
    float cosine = ear == 0 ? -lateral : lateral;
    float gap = distance - radius;
    float straight = sqrtf(gap * gap + 2.0f * distance * radius * (1.0f - cosine));
    path[ear] = straight;
    voice->ear_gain[ear] = 0.707106781f / fmaxf(1.0f, straight);
    voice->head_b0[ear] = 1.0f;
    if (head_enabled) {
      float theta = acosf(cosine);
      float tangent_angle = acosf(radius / distance);
      if (theta > tangent_angle) {
        float around = sqrtf((distance - radius) * (distance + radius)) +
                       radius * (theta - tangent_angle);
        path[ear] += config->head_amount * (around - straight);
      }
      /* Brown-Duda head shelf, bilinear transform, pole at 2c/a. */
      float alpha = 1.05f + 0.95f * cosf(theta * 1.2f);
      alpha = 1.0f + config->head_amount * (alpha - 1.0f);
      voice->head_b0[ear] = (1.0f + alpha * k) / (1.0f + k);
      voice->head_b1[ear] = (1.0f - alpha * k) / (1.0f + k);
    }
  }
  float first_path = fminf(path[0], path[1]);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float delay = (path[ear] - first_path) * (NOISE_SAMPLE_RATE_HZ / 343.0f);
    voice->ear_delay[ear] = (unsigned)delay;
    float f = delay - (float)voice->ear_delay[ear];
    /* Four-tap Lagrange delay adds one common frame of causal latency. */
    voice->delay_weight[ear][0] = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    voice->delay_weight[ear][1] = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
    voice->delay_weight[ear][2] = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
    voice->delay_weight[ear][3] = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
  }
  float rear = 0.5f * (1.0f - cosf(position.angle_rad));
  float cutoff = 18000.0f - 15000.0f * rear;
  voice->lowpass_alpha = -expm1f(-2.0f * NOISE_PI * cutoff / NOISE_SAMPLE_RATE_HZ);
  voice->filter_tail = 256;
}

static noise_result start_drop(noise_gen *gen, const droplet *drop) {
  if (drop->velocity_m_s == 0.0f) return NOISE_OK;
  if (gen->state.active_drops == NOISE_MAX_DROPLETS) {
    ++gen->state.dropped_drops;
    return NOISE_VOICE_LIMIT;
  }
  noise_drop_voice *voice = &gen->voices[gen->state.active_drops++];
  memset(voice, 0, sizeof(*voice));
  ++gen->state.generated_drops;
  if (gen->state.active_drops > gen->state.peak_active_drops) {
    gen->state.peak_active_drops = gen->state.active_drops;
  }
  float radius_ratio = drop->radius_m / 0.0005f;
  float amplitude = 0.035f * sqrtf(radius_ratio * radius_ratio * radius_ratio) *
                    drop->velocity_m_s / 4.0f;
  float impact_frequency = 1000.0f + 15000.0f * random_unit(&gen->drop_rng);
  mode_init(&voice->mode[0], impact_frequency, 2.0f * impact_frequency, amplitude, 0);
  if (drop->surface == WATER && drop->bubble_radius_m > 0.0f) {
    float r = drop->bubble_radius_m;
    float frequency = sqrtf(3.0f * 1.4f * NOISE_PRESSURE_PA / NOISE_WATER_DENSITY) /
                      (2.0f * NOISE_PI * r);
    float damping = 0.13f / r + 0.0072f / (r * sqrtf(r));
    /* Bubble onset follows the impact; 2 ms is an audible-design choice. */
    mode_init(&voice->mode[1], frequency, damping, 2.0f * amplitude,
              (uint32_t)(0.002f * NOISE_SAMPLE_RATE_HZ));
  } else if (drop->surface != WATER) {
    const float *m = material_modes[drop->surface];
    float tuning = 0.85f + 0.3f * random_unit(&gen->drop_rng);
    mode_init(&voice->mode[1], m[0] * tuning, m[1], amplitude * m[4], 0);
    mode_init(&voice->mode[2], m[2] * tuning, m[3], amplitude * m[4] * 0.5f, 0);
  }
  spatial_init(voice, &gen->config, drop->position);
  return NOISE_OK;
}

noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop) {
  if (!gen || !drop || (unsigned)drop->surface >= NOISE_SURFACE_COUNT ||
      !in_range(drop->radius_m, 0.0004f, 0.0029f) ||
      !in_range(drop->velocity_m_s, 0.0f, 12.0f) ||
      !in_range(drop->position.distance_m, 0.25f, 100.0f) ||
      !in_range(drop->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) ||
      (drop->bubble_radius_m != 0.0f &&
       (drop->surface != WATER || !in_range(drop->bubble_radius_m, 0.00016f, 0.004f)))) {
    return NOISE_INVALID_DROP;
  }
  return start_drop(gen, drop);
}

static float weather_mod_linear(float base, float amount, float intensity,
                                float low, float high) {
  float value = base + amount * (intensity - 0.5f) * (high - low);
  return fminf(high, fmaxf(low, value));
}

static float weather_mod_log(float base, float amount, float intensity,
                             float low, float high) {
  float value = logf(base) + amount * (intensity - 0.5f) * (logf(high) - logf(low));
  return fminf(high, fmaxf(low, expf(value)));
}

static float arrival_level(float intensity, float amount) {
  if (amount >= 0.0f) return 1.0f - amount + amount * intensity;
  return 1.0f + amount - amount * (1.0f - intensity);
}

static impact_surface choose_surface(noise_gen *gen, float intensity) {
  int modulated = 0;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    modulated |= gen->config.weather_mod_amount[WEATHER_MOD_WATER_WEIGHT + i] != 0.0f;
  }
  float choice = random_unit(&gen->drop_rng);
  if (!modulated) {
    unsigned surface = WATER;
    while (surface + 1 < NOISE_SURFACE_COUNT && choice >= gen->surface_cdf[surface]) ++surface;
    return (impact_surface)surface;
  }

  float source = 2.0f * intensity - 1.0f;
  float weight[NOISE_SURFACE_COUNT];
  float total = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    float amount = gen->config.weather_mod_amount[WEATHER_MOD_WATER_WEIGHT + i];
    float scale = fmaxf(0.001f, 1.0f + amount * source);
    weight[i] = gen->config.surface_weight[i] * scale;
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

static void spawn_rain(noise_gen *gen) {
  float intensity = gen->state.rain_intensity;
  float size_intensity = 0.5f +
      gen->config.weather_mod_amount[WEATHER_MOD_DROP_SIZE] * (intensity - 0.5f);
  float blend = size_intensity < 0.5f ? size_intensity * 2.0f :
      (size_intensity - 0.5f) * 2.0f;
  unsigned lo = size_intensity < 0.5f ? 0u : 1u;
  static const float distribution[3][2] = {{0.84f, 0.16f}, {0.32f, 0.61f}, {0.24f, 0.52f}};
  float small = distribution[lo][0] + blend * (distribution[lo + 1][0] - distribution[lo][0]);
  float medium = distribution[lo][1] + blend * (distribution[lo + 1][1] - distribution[lo][1]);
  float size = random_unit(&gen->drop_rng);
  float u = random_unit(&gen->drop_rng);
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
  float fall_height = weather_mod_log(gen->config.fall_height_m,
      gen->config.weather_mod_amount[WEATHER_MOD_FALL_HEIGHT], intensity, 0.01f, 1000.0f);
  drop.velocity_m_s = terminal * sqrtf(-expm1f(
      -2.0f * NOISE_GRAVITY * fall_height / (terminal * terminal)));
  drop.surface = choose_surface(gen, intensity);
  drop.bubble_radius_m = drop.surface == WATER && size < small ?
      0.00016f + 0.00031f * random_unit(&gen->drop_rng) : 0.0f;
  float near = weather_mod_log(gen->config.min_distance_m,
      gen->config.weather_mod_amount[WEATHER_MOD_MIN_DISTANCE], intensity, 0.25f, 100.0f);
  float far = weather_mod_log(gen->config.max_distance_m,
      gen->config.weather_mod_amount[WEATHER_MOD_MAX_DISTANCE], intensity, 0.25f, 100.0f);
  if (near > far) {
    float swap = near;
    near = far;
    far = swap;
  }
  drop.position.distance_m = sqrtf(near * near + random_unit(&gen->drop_rng) *
                                   (far * far - near * near));
  drop.position.angle_rad = 2.0f * NOISE_PI * random_unit(&gen->drop_rng);
  /* Capacity losses are recorded by start_drop for both arrival paths. */
  (void)start_drop(gen, &drop);
}

static void weather_next(noise_gen *gen) {
  if (!gen->config.vary_rain) return;
  if (++gen->weather_samples == gen->weather_period) {
    static const float cdf[3][2] = {{0.85f, 1.0f}, {0.10f, 0.90f}, {0.0f, 0.15f}};
    gen->weather_samples = 0;
    float choice = random_unit(&gen->weather_rng);
    unsigned state = gen->state.weather_state;
    state = choice < cdf[state][0] ? 0u : (choice < cdf[state][1] ? 1u : 2u);
    gen->state.weather_state = state;
    gen->state.rain_target = gen->config.min_rain_intensity + 0.5f * (float)state *
        (gen->config.max_rain_intensity - gen->config.min_rain_intensity);
  }
  /* Carry sub-ULP steps so long time constants still reach their target. */
  float step = gen->rain_slew * (gen->state.rain_target - gen->state.rain_intensity) +
               gen->rain_slew_error;
  float next = gen->state.rain_intensity + step;
  gen->rain_slew_error = step - (next - gen->state.rain_intensity);
  gen->state.rain_intensity = next;
}

static float ambient_next(noise_gen *gen) {
  const float *gain = gen->config.ambient_gain;
  float sample = 0.0f;
  if (gain[NOISE_KIND_WHITE] > 0.0f) {
    sample += gain[NOISE_KIND_WHITE] * (2.0f * random_unit(&gen->ambient_rng) - 1.0f);
  }
  if (gain[NOISE_KIND_PINK] > 0.0f) {
    float white = 2.0f * random_unit(&gen->ambient_rng) - 1.0f;
    float *b = gen->pink_b;
    b[0] = 0.99886f * b[0] + white * 0.0555179f;
    b[1] = 0.99332f * b[1] + white * 0.0750759f;
    b[2] = 0.96900f * b[2] + white * 0.1538520f;
    b[3] = 0.86650f * b[3] + white * 0.3104856f;
    b[4] = 0.55000f * b[4] + white * 0.5329522f;
    b[5] = -0.7616f * b[5] - white * 0.0168980f;
    float pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
    b[6] = white * 0.115926f;
    sample += gain[NOISE_KIND_PINK] * pink * 0.11f;
  }
  if (gain[HUM_50HZ] > 0.0f) sample += gain[HUM_50HZ] * gen->hum_table[gen->hum_sample % 882];
  if (gain[HUM_60HZ] > 0.0f) {
    /* 60 Hz has 735 samples/period; interpolate the common periodic table. */
    unsigned phase = (gen->hum_sample % 735) * 6;
    unsigned index = phase / 5;
    float fraction = (float)(phase % 5) * 0.2f;
    sample += gain[HUM_60HZ] * (gen->hum_table[index] + fraction *
        (gen->hum_table[(index + 1) % 882] - gen->hum_table[index]));
  }
  if (++gen->hum_sample == 4410) gen->hum_sample = 0;
  return sample;
}

static void reverb_next(noise_gen *gen, float send, float gain,
                        float *left, float *right) {
  float delay[NOISE_REVERB_LINES];
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    float value = gen->reverb[reverb_offset[i] + gen->reverb_position[i]];
    gen->reverb_damping[i] += 0.16f * (value - gen->reverb_damping[i]);
    delay[i] = gen->reverb_damping[i];
  }
  const float scatter = 0.447213595f;
  float feedback[NOISE_REVERB_LINES] = {
    scatter * (delay[1] + delay[2] + delay[3] + delay[4] + delay[5]),
    scatter * (delay[0] + delay[2] - delay[3] - delay[4] + delay[5]),
    scatter * (delay[0] + delay[1] + delay[3] - delay[4] - delay[5]),
    scatter * (delay[0] - delay[1] + delay[2] + delay[4] - delay[5]),
    scatter * (delay[0] - delay[1] - delay[2] + delay[3] + delay[5]),
    scatter * (delay[0] + delay[1] - delay[2] - delay[3] + delay[4])
  };
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    gen->reverb[reverb_offset[i] + gen->reverb_position[i]] =
        0.408248290f * send + gen->reverb_feedback[i] * feedback[i];
    if (++gen->reverb_position[i] == reverb_length[i]) gen->reverb_position[i] = 0;
  }
  *left += gain * (0.577350269f * delay[0] + 0.288675135f * delay[1] -
                   0.288675135f * delay[2] - 0.577350269f * delay[3] -
                   0.288675135f * delay[4] + 0.288675135f * delay[5]);
  *right += gain * (0.5f * delay[1] + 0.5f * delay[2] -
                    0.5f * delay[4] - 0.5f * delay[5]);
}

static int16_t to_sample(noise_gen *gen, float value) {
  if (value > 1.0f) {
    ++gen->state.clipped_samples;
    return INT16_MAX;
  }
  if (value < -1.0f) {
    ++gen->state.clipped_samples;
    return INT16_MIN;
  }
  return (int16_t)(value * 32767.0f);
}

size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames) {
  for (size_t frame = 0; frame < frames; ++frame) {
    weather_next(gen);
    float intensity = gen->state.rain_intensity;
    float rain_gain = weather_mod_linear(gen->config.rain_gain,
        gen->config.weather_mod_amount[WEATHER_MOD_RAIN_GAIN], intensity, 0.0f, 1.0f);
    float reverb_gain = weather_mod_linear(gen->config.reverb_gain,
        gen->config.weather_mod_amount[WEATHER_MOD_REVERB_GAIN], intensity, 0.0f, 1.0f);
    float density = arrival_level(intensity,
        gen->config.weather_mod_amount[WEATHER_MOD_ARRIVAL_RATE]);
    if (rain_gain > 0.0f && density > 0.0f) {
      float probability = density * gen->config.max_drops_per_s / NOISE_SAMPLE_RATE_HZ;
      if (random_unit(&gen->arrival_rng) < probability) spawn_rain(gen);
    }
    float left = 0.0f, right = 0.0f, send = 0.0f;
    unsigned i = 0;
    while (i < gen->state.active_drops) {
      noise_drop_voice *voice = &gen->voices[i];
      float source = 0.0f;
      unsigned remaining = 0;
      for (unsigned m = 0; m < 3; ++m) {
        source += mode_next(&voice->mode[m]);
        remaining += voice->mode[m].remaining + voice->mode[m].delay;
      }
      source *= rain_gain;
      send += source;
      voice->lowpass_state += voice->lowpass_alpha * (source - voice->lowpass_state);
      float direct = source + gen->config.rear_amount * (voice->lowpass_state - source);
      for (unsigned ear = 0; ear < 2; ++ear) {
        float filtered = voice->head_b0[ear] * direct +
            voice->head_b1[ear] * voice->head_previous_input +
            voice->head_feedback * voice->head_state[ear];
        voice->head_state[ear] = filtered;
        unsigned position = gen->direct_position + voice->ear_delay[ear];
        for (unsigned tap = 0; tap < 4; ++tap) {
          gen->direct[ear][(position + tap) % NOISE_DIRECT_SAMPLES] +=
              filtered * voice->ear_gain[ear] * voice->delay_weight[ear][tap];
        }
      }
      voice->head_previous_input = direct;
      if (!remaining) voice->filter_tail -= 1;
      if (!remaining && voice->filter_tail == 0) {
        /* Keep the active prefix dense to avoid scanning idle voices per sample. */
        gen->state.active_drops -= 1;
        gen->voices[i] = gen->voices[gen->state.active_drops];
      } else {
        ++i;
      }
    }
    left = gen->direct[0][gen->direct_position];
    right = gen->direct[1][gen->direct_position];
    gen->direct[0][gen->direct_position] = 0.0f;
    gen->direct[1][gen->direct_position] = 0.0f;
    if (++gen->direct_position == NOISE_DIRECT_SAMPLES) gen->direct_position = 0;
    if (gen->config.reverb_gain > 0.0f ||
        gen->config.weather_mod_amount[WEATHER_MOD_REVERB_GAIN] != 0.0f) {
      reverb_next(gen, send, reverb_gain, &left, &right);
    }
    float ambient = ambient_next(gen);
    out[2 * frame] = to_sample(gen, (left + ambient) * gen->config.master_gain);
    out[2 * frame + 1] = to_sample(gen, (right + ambient) * gen->config.master_gain);
  }
  return frames;
}
