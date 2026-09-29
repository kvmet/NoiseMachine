#include "noise_core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NOISE_PI 3.14159265358979323846f
#define NOISE_GRAVITY 9.81f
#define NOISE_WATER_DENSITY 1000.0f
#define NOISE_PRESSURE_PA 101325.0f
#define THUNDER_REFERENCE_M 1000.0f
#define THUNDER_FINE_STEP_M 3.0f
#define THUNDER_ROUGHNESS 0.3f
#define THUNDER_END_FADE 0.4f /* Fraction of a part that fades toward a late-arriving end. */
#define THUNDER_DIRECT_BANDS 3u
#define CRICKET_PULSE_DROP 0.03f /* Carrier falls through each pulse as the wing slows. */
#define CRICKET_SINGING_S 30.0f /* Mean bout lengths. */
#define CRICKET_SILENT_S 10.0f
#define CICADA_LEVEL 2.0f
#define CICADA_CHORUS_LEVEL 0.015f
#define CICADA_CLICK 0.5f /* Band-pass ring amplitude is twice the impulse. */
#define CICADA_JITTER 0.01f /* Click interval spread. */
#define CICADA_DROP 0.15f /* Pitch and click rate fall through a held note's wind-down. */
#define CICADA_SWELL_ALPHA (1.0f / (2.0f * NOISE_SAMPLE_RATE_HZ)) /* 2 s time constant. */

static const unsigned reverb_length[NOISE_REVERB_LINES] = {
  739, 953, 1151, 1327, 1471, 1663
};
static const unsigned reverb_offset[NOISE_REVERB_LINES] = {
  0, 739, 1692, 2843, 4170, 5641
};
/* Thunder reverb lines at quarter rate: 50 to 146 ms, echo spacing like terrain. */
#define THUNDER_REVERB_DECIMATION 4u
static const unsigned thunder_reverb_length[NOISE_REVERB_LINES] = {
  557, 719, 887, 1063, 1297, 1609
};
static const unsigned thunder_reverb_offset[NOISE_REVERB_LINES] = {
  0, 557, 1276, 2163, 3226, 4523
};

/* ---- Random streams and DSP primitives ---- */

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

static float random_between(uint32_t *state, float low, float high) {
  return low + (high - low) * random_unit(state);
}

static float random_log_between(uint32_t *state, float low, float high) {
  return expf(logf(low) + (logf(high) - logf(low)) * random_unit(state));
}

/* Irwin-Hall sum of four uniforms, scaled to unit variance. */
static float random_gaussian(uint32_t *rng) {
  float sum = random_unit(rng) + random_unit(rng) + random_unit(rng) + random_unit(rng);
  return 1.7320508f * (sum - 2.0f);
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

/* Radius between near and far that is uniform over the annulus area for uniform u. */
static float area_uniform_distance(float near, float far, float u) {
  return sqrtf(near * near + u * (far * far - near * near));
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

static void oscillator_init(noise_oscillator *oscillator, float frequency) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  oscillator->previous = -sinf(phase);
  oscillator->current = 0.0f;
  oscillator->coefficient = 2.0f * cosf(phase);
}

static float oscillator_next(noise_oscillator *oscillator) {
  float value = oscillator->current;
  float next = oscillator->coefficient * oscillator->current - oscillator->previous;
  oscillator->previous = oscillator->current;
  oscillator->current = next;
  return value;
}

static void resonator_tune(noise_resonator *resonator, float frequency, float q) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  float radius = expf(-NOISE_PI * frequency / (q * NOISE_SAMPLE_RATE_HZ));
  resonator->coefficient = 2.0f * radius * cosf(phase);
  resonator->radius_squared = radius * radius;
}

static float resonator_next(noise_resonator *resonator, float input) {
  float output = resonator->coefficient * resonator->state[0] -
                 resonator->radius_squared * resonator->state[1] +
                 input - resonator->input[1];
  resonator->input[1] = resonator->input[0];
  resonator->input[0] = input;
  resonator->state[1] = resonator->state[0];
  resonator->state[0] = output;
  return output;
}

/* RBJ cookbook coefficients; the band-pass has 0 dB peak gain. */
static void biquad_tune(noise_biquad *filter, int bandpass, float frequency, float q) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  float cosine = cosf(phase);
  float alpha = sinf(phase) / (2.0f * q);
  float norm = 1.0f / (1.0f + alpha);
  if (bandpass) {
    filter->b0 = alpha * norm;
    filter->b1 = 0.0f;
    filter->b2 = -alpha * norm;
  } else {
    filter->b0 = 0.5f * (1.0f - cosine) * norm;
    filter->b1 = (1.0f - cosine) * norm;
    filter->b2 = filter->b0;
  }
  filter->a1 = -2.0f * cosine * norm;
  filter->a2 = (1.0f - alpha) * norm;
}

static float biquad_next(noise_biquad *filter, float input) {
  float output = filter->b0 * input + filter->state[0];
  filter->state[0] = filter->b1 * input - filter->a1 * output + filter->state[1];
  filter->state[1] = filter->b2 * input - filter->a2 * output;
  return output;
}

/* ---- Spatial model and direct bus ---- */

static int listener_config_valid(const noise_listener_config *c) {
  return in_range(c->stereo_width_m, 0.0f, 0.5f) &&
         in_range(c->head_amount, 0.0f, 1.0f) &&
         in_range(c->rear_amount, 0.0f, 1.0f);
}

static void spatial_init(noise_spatial *voice, const noise_listener_config *listener,
                         position_polar position) {
  float radius = 0.5f * listener->stereo_width_m;
  float distance = position.distance_m;
  float lateral = sinf(position.angle_rad);
  float path[2];
  float k = NOISE_SAMPLE_RATE_HZ * radius / 343.0f;
  int head_enabled = radius > 0.0f && listener->head_amount > 0.0f;
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
        path[ear] += listener->head_amount * (around - straight);
      }
      /* Brown-Duda head shelf, bilinear transform, pole at 2c/a. */
      float alpha = 1.05f + 0.95f * cosf(theta * 1.2f);
      alpha = 1.0f + listener->head_amount * (alpha - 1.0f);
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
}

static void spatial_next(noise_spatial *voice, const noise_listener_config *listener,
                         noise_bus *bus, float source) {
  voice->lowpass_state += voice->lowpass_alpha * (source - voice->lowpass_state);
  float direct = source + listener->rear_amount * (voice->lowpass_state - source);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float filtered = voice->head_b0[ear] * direct +
        voice->head_b1[ear] * voice->head_previous_input +
        voice->head_feedback * voice->head_state[ear];
    voice->head_state[ear] = filtered;
    unsigned position = bus->position + voice->ear_delay[ear];
    for (unsigned tap = 0; tap < 4; ++tap) {
      bus->direct[ear][(position + tap) % NOISE_DIRECT_SAMPLES] +=
          filtered * voice->ear_gain[ear] * voice->delay_weight[ear][tap];
    }
  }
  voice->head_previous_input = direct;
}

/* Reads and clears the current frame, then advances. */
static void bus_next(noise_bus *bus, float *left, float *right) {
  *left = bus->direct[0][bus->position];
  *right = bus->direct[1][bus->position];
  bus->direct[0][bus->position] = 0.0f;
  bus->direct[1][bus->position] = 0.0f;
  if (++bus->position == NOISE_DIRECT_SAMPLES) bus->position = 0;
}

static int placement_valid(const noise_placement *p) {
  return in_range(p->stereo_width, 0.0f, 1.0f) &&
         in_range(p->min_distance_m, 0.25f, 100.0f) &&
         in_range(p->max_distance_m, p->min_distance_m, 100.0f);
}

/* distance_offset is 0..1 across the area; angle_offset is -1..1 across the width. */
static position_polar placement_position(const noise_placement *p, float distance_offset,
                                         float angle_offset) {
  position_polar position = {
    area_uniform_distance(p->min_distance_m, p->max_distance_m, distance_offset),
    NOISE_PI * p->stereo_width * angle_offset
  };
  return position;
}

/* ---- Reverb ---- */

/* Six-line FDN step: damped reads, conference-matrix scatter, stereo taps. */
static void fdn_next(float *buffer, const unsigned *length, const unsigned *offset,
                     noise_fdn *fdn, float damping_alpha, float send, float out[2]) {
  float delay[NOISE_REVERB_LINES];
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    float value = buffer[offset[i] + fdn->position[i]];
    fdn->damping[i] += damping_alpha * (value - fdn->damping[i]);
    delay[i] = fdn->damping[i];
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
    buffer[offset[i] + fdn->position[i]] = 0.408248290f * send + fdn->feedback[i] * feedback[i];
    if (++fdn->position[i] == length[i]) fdn->position[i] = 0;
  }
  out[0] = 0.577350269f * delay[0] + 0.288675135f * delay[1] -
           0.288675135f * delay[2] - 0.577350269f * delay[3] -
           0.288675135f * delay[4] + 0.288675135f * delay[5];
  out[1] = 0.5f * delay[1] + 0.5f * delay[2] - 0.5f * delay[4] - 0.5f * delay[5];
}

static void reverb_init(noise_reverb *reverb) {
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    reverb->fdn.feedback[i] = powf(0.001f,
        (float)reverb_length[i] / (0.65f * NOISE_SAMPLE_RATE_HZ));
  }
}

static void reverb_next(noise_reverb *reverb, float send, float gain,
                        float *left, float *right) {
  float out[2];
  fdn_next(reverb->buffer, reverb_length, reverb_offset, &reverb->fdn, 0.16f, send, out);
  *left += gain * out[0];
  *right += gain * out[1];
}

/* ---- Ambient noise and hum ---- */

static void ambient_init(noise_ambient *ambient, uint32_t seed) {
  ambient->rng = stream_seed(seed, 0x9e3779b9u);
  for (unsigned i = 0; i < NOISE_HUM_TABLE_SAMPLES; ++i) {
    float phase = 2.0f * NOISE_PI * (float)i / (float)NOISE_HUM_TABLE_SAMPLES;
    ambient->hum_table[i] = (sinf(phase) + 0.3f * sinf(2.0f * phase) +
                             0.12f * sinf(3.0f * phase)) / 1.42f;
  }
}

static float ambient_next(noise_ambient *ambient, const float gain[NOISE_KIND_COUNT]) {
  float sample = 0.0f;
  if (gain[NOISE_KIND_WHITE] > 0.0f) {
    sample += gain[NOISE_KIND_WHITE] * (2.0f * random_unit(&ambient->rng) - 1.0f);
  }
  if (gain[NOISE_KIND_PINK] > 0.0f) {
    float white = 2.0f * random_unit(&ambient->rng) - 1.0f;
    float *b = ambient->pink;
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
  const float *table = ambient->hum_table;
  if (gain[NOISE_KIND_HUM_50HZ] > 0.0f) {
    sample += gain[NOISE_KIND_HUM_50HZ] * table[ambient->hum_sample % NOISE_HUM_TABLE_SAMPLES];
  }
  if (gain[NOISE_KIND_HUM_60HZ] > 0.0f) {
    /* 60 Hz has 735 samples/period; interpolate the common periodic table. */
    unsigned phase = (ambient->hum_sample % 735) * 6;
    unsigned index = phase / 5;
    float fraction = (float)(phase % 5) * 0.2f;
    sample += gain[NOISE_KIND_HUM_60HZ] * (table[index] + fraction *
        (table[(index + 1) % NOISE_HUM_TABLE_SAMPLES] - table[index]));
  }
  /* 4410 frames hold whole periods of both hums. */
  if (++ambient->hum_sample == 4410) ambient->hum_sample = 0;
  return sample;
}

/* ---- Wind ---- */

static int wind_config_valid(const noise_wind_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->brightness, 0.0f, 1.0f) &&
         in_range(c->gust_depth, 0.0f, 1.0f) &&
         in_range(c->gust_rate_hz, 0.01f, 2.0f) &&
         in_range(c->stereo_width, 0.0f, 1.0f);
}

static void wind_config_default(noise_wind_config *c) {
  c->brightness = 0.5f;
  c->gust_depth = 0.6f;
  c->gust_rate_hz = 0.12f;
  c->stereo_width = 0.5f;
}

static void wind_init(noise_wind *wind, uint32_t seed) {
  wind->rng = stream_seed(seed, 0x1715609du);
  wind->gust = 0.5f;
  wind->gust_target = 0.5f;
  wind->brightness_cache = -1.0f;
  wind->gust_rate_cache = -1.0f;
}

static void wind_next(noise_wind *wind, const noise_wind_config *c, float *left, float *right) {
  if (c->gain <= 0.0f) return;
  float rate = c->gust_rate_hz;
  if (rate != wind->gust_rate_cache) {
    wind->gust_rate_cache = rate;
    wind->gust_alpha = -expm1f(-2.0f * NOISE_PI * rate / NOISE_SAMPLE_RATE_HZ);
  }
  float brightness = c->brightness;
  if (brightness != wind->brightness_cache) {
    wind->brightness_cache = brightness;
    float cutoff = 400.0f * powf(20.0f, brightness);
    wind->air_alpha = -expm1f(-2.0f * NOISE_PI * cutoff / NOISE_SAMPLE_RATE_HZ);
  }
  if (wind->gust_samples == 0) {
    wind->gust_target = random_unit(&wind->rng);
    wind->gust_samples = (uint32_t)(NOISE_SAMPLE_RATE_HZ / rate);
  }
  --wind->gust_samples;
  wind->gust += wind->gust_alpha * (wind->gust_target - wind->gust);
  float depth = c->gust_depth;
  float envelope = 1.0f - depth + depth * (0.35f + 1.3f * wind->gust);
  const float rumble_alpha = 0.0169533f;
  float common = 2.0f * random_unit(&wind->rng) - 1.0f;
  float width = c->stereo_width;
  float *output[2] = {left, right};
  for (unsigned channel = 0; channel < 2; ++channel) {
    float side = 2.0f * random_unit(&wind->rng) - 1.0f;
    float input = (1.0f - width) * common + width * side;
    wind->air[channel] += wind->air_alpha * (input - wind->air[channel]);
    wind->rumble[channel] += rumble_alpha * (input - wind->rumble[channel]);
    *output[channel] += c->gain * envelope *
                        (0.55f * wind->air[channel] + 0.30f * wind->rumble[channel]);
  }
}

/* ---- Crickets ---- */

static int cricket_config_valid(const noise_cricket_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->call_rate_hz, 0.05f, 10.0f) &&
         in_range(c->pitch_hz, 2000.0f, 8000.0f) &&
         in_range(c->pitch_variation, 0.0f, 1.0f) &&
         placement_valid(&c->placement);
}

static void cricket_config_default(noise_cricket_config *c) {
  c->call_rate_hz = 1.2f;
  c->pitch_hz = 4500.0f;
  c->pitch_variation = 0.35f;
  c->placement.stereo_width = 0.8f;
  c->placement.min_distance_m = 2.0f;
  c->placement.max_distance_m = 15.0f;
}

static void crickets_init(noise_crickets *crickets, uint32_t seed) {
  crickets->rng = stream_seed(seed, 0xb54cda58u);
}

static uint32_t cricket_bout(uint32_t *rng, unsigned singing) {
  float mean_s = singing ? CRICKET_SINGING_S : CRICKET_SILENT_S;
  return 1u + (uint32_t)(-mean_s * NOISE_SAMPLE_RATE_HZ * logf(1.0f - random_unit(rng)));
}

static uint32_t cricket_period(uint32_t *rng, const noise_cricket_voice *voice,
                               float call_rate_hz) {
  float period = voice->period_scale * random_between(rng, 0.97f, 1.03f) *
                 NOISE_SAMPLE_RATE_HZ / call_rate_hz;
  uint32_t chirp = voice->pulses * voice->pulse_samples;
  return period > (float)chirp ? (uint32_t)period : chirp;
}

static void cricket_init(uint32_t *rng, noise_cricket_voice *voice, float call_rate_hz) {
  voice->pitch_offset = random_between(rng, -1.0f, 1.0f);
  voice->angle_offset = random_between(rng, -1.0f, 1.0f);
  voice->distance_offset = random_unit(rng);
  voice->period_scale = random_between(rng, 0.9f, 1.1f);
  voice->pulses = 3u + random_u32(rng) % 3u;
  voice->pulse_samples = (uint32_t)(NOISE_SAMPLE_RATE_HZ * random_between(rng, 0.026f, 0.036f));
  voice->sounding_samples = (uint32_t)(voice->pulse_samples * random_between(rng, 0.55f, 0.70f));
  voice->chirp_samples = voice->pulses * voice->pulse_samples;
  voice->singing = random_unit(rng) < CRICKET_SINGING_S / (CRICKET_SINGING_S + CRICKET_SILENT_S);
  voice->bout_samples = cricket_bout(rng, voice->singing);
  voice->until_chirp = (uint32_t)(random_unit(rng) * (float)cricket_period(rng, voice, call_rate_hz));
}

static void cricket_place(noise_cricket_voice *voice, const noise_cricket_config *c,
                          const noise_listener_config *listener) {
  spatial_init(&voice->spatial, listener,
               placement_position(&c->placement, voice->distance_offset, voice->angle_offset));
}

/* Returns the reverb send; the direct sound goes through the spatial model. */
static float cricket_next(uint32_t *rng, noise_cricket_voice *voice,
                          const noise_cricket_config *c,
                          const noise_listener_config *listener, noise_bus *bus) {
  if (--voice->bout_samples == 0) {
    voice->singing = !voice->singing;
    voice->bout_samples = cricket_bout(rng, voice->singing);
  }
  if (voice->until_chirp == 0) {
    voice->until_chirp = cricket_period(rng, voice, c->call_rate_hz);
    if (voice->singing) {
      voice->chirp_samples = 0;
      cricket_place(voice, c, listener);
    }
  }
  --voice->until_chirp;
  float sample = 0.0f;
  if (voice->chirp_samples < voice->pulses * voice->pulse_samples) {
    uint32_t within_pulse = voice->chirp_samples % voice->pulse_samples;
    ++voice->chirp_samples;
    if (within_pulse == 0) {
      float pitch = c->pitch_hz * (1.0f + 0.3f * c->pitch_variation * voice->pitch_offset);
      oscillator_init(&voice->oscillator, pitch);
      float end = 2.0f * cosf(2.0f * NOISE_PI * pitch * (1.0f - CRICKET_PULSE_DROP) /
                              NOISE_SAMPLE_RATE_HZ);
      voice->glide = (end - voice->oscillator.coefficient) / (float)voice->sounding_samples;
    }
    if (within_pulse < voice->sounding_samples) {
      float carrier = oscillator_next(&voice->oscillator);
      voice->oscillator.coefficient += voice->glide;
      uint32_t attack = voice->sounding_samples / 4u;
      float envelope = within_pulse < attack ?
          (float)within_pulse / (float)attack :
          (float)(voice->sounding_samples - within_pulse) /
          (float)(voice->sounding_samples - attack);
      float tone = carrier - 0.22f * carrier * carrier * carrier;
      sample = 0.35f * c->gain * envelope * tone;
    }
  }
  /* Runs between chirps too, so filter tails decay instead of holding. */
  spatial_next(&voice->spatial, listener, bus, sample);
  return sample;
}

/* Returns the reverb send; the direct sound goes to the bus. */
static float crickets_next(noise_crickets *crickets, const noise_cricket_config *c,
                           const noise_listener_config *listener, noise_bus *bus) {
  if (c->gain <= 0.0f) return 0.0f;
  if (!crickets->started) {
    crickets->started = 1;
    for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
      cricket_init(&crickets->rng, &crickets->voice[i], c->call_rate_hz);
      cricket_place(&crickets->voice[i], c, listener);
    }
    /* The layer is audible from its first frame. */
    crickets->voice[0].singing = 1;
    crickets->voice[0].until_chirp = 0;
  }
  float send = 0.0f;
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
    send += cricket_next(&crickets->rng, &crickets->voice[i], c, listener, bus);
  }
  return send;
}

/* ---- Cicadas ---- */

typedef struct cicada_song {
  float click_rate_hz;
  float q; /* Body resonance; a high Q rings across clicks and sounds tonal. */
  float syllable_hz[2]; /* Syllable rate at the phrase start and end. */
  float duty; /* Sounding fraction of each syllable period. */
  float syllable_drop; /* Pitch fall through each syllable; negative rises. */
  float syllables[2]; /* Syllables per phrase, inclusive range. */
  float fade; /* Last syllable level relative to the first. */
  float hold_s[2]; /* Held final note length range. */
  float throb; /* Held-note pulsing depth. */
  float gap_s; /* Mean silence between calls. */
} cicada_song;

/* Starting values from descriptions of each song, not fitted to recordings. */
static const cicada_song cicada_songs[NOISE_CICADA_SPECIES_COUNT] = {
  /* Dog-day: one long buzz that swells, pulses, and winds down. */
  {300.0f, 6.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {10.0f, 18.0f}, 0.4f, 20.0f},
  /* Minminzemi: rising "min" syllables, then a long falling "miiin". */
  {400.0f, 20.0f, {3.0f, 3.0f}, 0.7f, -0.04f, {5.0f, 15.0f}, 1.0f, {1.0f, 2.0f}, 0.2f, 8.0f},
  /* Higurashi: tonal falling "kana" pulses that slow and fade. */
  {500.0f, 30.0f, {8.0f, 6.0f}, 0.5f, 0.05f, {20.0f, 40.0f}, 0.3f, {0.0f, 0.0f}, 0.0f, 15.0f}
};

static int cicada_config_valid(const noise_cicada_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         (unsigned)c->species < NOISE_CICADA_SPECIES_COUNT &&
         in_range(c->pitch_hz, 2000.0f, 10000.0f) &&
         in_range(c->click_rate_scale, 0.5f, 1.5f) &&
         in_range(c->chorus, 0.0f, 1.0f) &&
         placement_valid(&c->placement);
}

static void cicada_config_default(noise_cicada_config *c) {
  c->pitch_hz = 5000.0f;
  c->click_rate_scale = 1.0f;
  c->chorus = 0.35f;
  c->placement.stereo_width = 0.75f;
  c->placement.min_distance_m = 5.0f;
  c->placement.max_distance_m = 30.0f;
}

static void cicadas_init(noise_cicadas *cicadas, uint32_t seed) {
  cicadas->rng = stream_seed(seed, 0x94d049bbu);
  cicadas->pitch_cache = -1.0f;
  cicadas->species_cache = -1;
  cicadas->swell = 0.5f;
  cicadas->swell_target = 0.5f;
}

static uint32_t cicada_swell(uint32_t hold) {
  uint32_t limit = NOISE_SAMPLE_RATE_HZ;
  return hold / 4u < limit ? hold / 4u : limit;
}

static uint32_t cicada_wind_down(uint32_t hold) {
  uint32_t limit = 2u * NOISE_SAMPLE_RATE_HZ;
  return hold / 2u < limit ? hold / 2u : limit;
}

/* Tunes the body to this cicada's pitch and glides by drop over the given frames. */
static void cicada_tune(noise_cicada_voice *voice, const noise_cicada_config *c,
                        float drop, uint32_t frames) {
  const cicada_song *song = &cicada_songs[c->species];
  float pitch = c->pitch_hz * (1.0f + 0.05f * voice->pitch_offset);
  resonator_tune(&voice->body, pitch, song->q);
  float radius = sqrtf(voice->body.radius_squared);
  float end = 2.0f * radius * cosf(2.0f * NOISE_PI * pitch * (1.0f - drop) /
                                   NOISE_SAMPLE_RATE_HZ);
  voice->glide = frames ? (end - voice->body.coefficient) / (float)frames : 0.0f;
}

static void cicada_rest(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c) {
  float gap_s = cicada_songs[c->species].gap_s;
  voice->note_length = 0;
  voice->until_call = 1u + (uint32_t)(-gap_s * NOISE_SAMPLE_RATE_HZ *
                                      logf(1.0f - random_unit(rng)));
}

/* Starts syllable number voice->syllable, the held note after the last, or rest. */
static void cicada_note(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c) {
  const cicada_song *song = &cicada_songs[c->species];
  voice->note_samples = 0;
  if (voice->syllable < voice->syllables) {
    float progress = voice->syllables > 1 ?
        (float)voice->syllable / (float)(voice->syllables - 1) : 0.0f;
    float rate = song->syllable_hz[0] + progress * (song->syllable_hz[1] - song->syllable_hz[0]);
    voice->note_length = (uint32_t)(NOISE_SAMPLE_RATE_HZ / rate);
    voice->sounding = (uint32_t)(song->duty * (float)voice->note_length);
    voice->holding = 0;
    cicada_tune(voice, c, song->syllable_drop, voice->sounding);
    return;
  }
  float hold_s = random_between(rng, song->hold_s[0], song->hold_s[1]);
  if (hold_s <= 0.0f) {
    cicada_rest(rng, voice, c);
    return;
  }
  voice->note_length = (uint32_t)(hold_s * NOISE_SAMPLE_RATE_HZ);
  voice->sounding = voice->note_length;
  voice->holding = 1;
  cicada_tune(voice, c, CICADA_DROP, cicada_wind_down(voice->note_length));
  oscillator_init(&voice->throb, random_between(rng, 2.0f, 4.0f));
}

static void cicada_call(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c,
                        const noise_listener_config *listener) {
  const cicada_song *song = &cicada_songs[c->species];
  spatial_init(&voice->spatial, listener,
               placement_position(&c->placement, voice->distance_offset, voice->angle_offset));
  float span = song->syllables[1] - song->syllables[0] + 1.0f;
  voice->syllables = (unsigned)(song->syllables[0] + span * random_unit(rng));
  voice->syllable = 0;
  memset(&voice->body, 0, sizeof(voice->body));
  voice->until_click = 0.0f;
  cicada_note(rng, voice, c);
}

/* Returns the reverb send; the direct sound goes through the spatial model. */
static float cicada_next(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c,
                         const noise_listener_config *listener, noise_bus *bus) {
  if (!voice->note_length && --voice->until_call == 0) cicada_call(rng, voice, c, listener);
  float sample = 0.0f;
  if (voice->note_length) {
    const cicada_song *song = &cicada_songs[c->species];
    uint32_t t = voice->note_samples++;
    float envelope = 0.0f;
    float click_rate = 1.0f;
    float impulse = 0.0f;
    if (t < voice->sounding) {
      if (voice->holding) {
        float swell = (float)cicada_swell(voice->sounding);
        float wind_down = (float)cicada_wind_down(voice->sounding);
        float remaining = (float)(voice->sounding - voice->note_samples);
        envelope = (float)t < swell ? (float)t / swell : 1.0f;
        if (remaining < wind_down) {
          float fraction = remaining / wind_down;
          envelope *= fraction;
          click_rate -= CICADA_DROP * (1.0f - fraction);
          voice->body.coefficient += voice->glide;
        }
        envelope *= 1.0f - song->throb * 0.5f * (1.0f + oscillator_next(&voice->throb));
      } else {
        float x = ((float)t + 0.5f) / (float)voice->sounding;
        float level = voice->syllables > 1 ? 1.0f + (song->fade - 1.0f) *
            (float)voice->syllable / (float)(voice->syllables - 1) : 1.0f;
        envelope = 4.0f * x * (1.0f - x) * level;
        voice->body.coefficient += voice->glide;
      }
      voice->until_click -= click_rate;
      if (voice->until_click <= 0.0f) {
        impulse = CICADA_CLICK;
        voice->until_click += NOISE_SAMPLE_RATE_HZ /
            (song->click_rate_hz * c->click_rate_scale) *
            random_between(rng, 1.0f - CICADA_JITTER, 1.0f + CICADA_JITTER);
      }
    }
    sample = CICADA_LEVEL * c->gain * envelope * resonator_next(&voice->body, impulse);
    if (voice->note_samples == voice->note_length) {
      if (voice->holding) {
        cicada_rest(rng, voice, c);
      } else {
        ++voice->syllable;
        cicada_note(rng, voice, c);
      }
    }
  }
  /* Runs between calls too, so filter tails decay instead of holding. */
  spatial_next(&voice->spatial, listener, bus, sample);
  return sample;
}

static void cicada_chorus_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                               noise_bus *bus) {
  uint32_t *rng = &cicadas->rng;
  /* A crowd at spread pitches blurs into a band wider than one body. */
  float q = fmaxf(3.0f, 0.5f * cicada_songs[c->species].q);
  if (c->pitch_hz != cicadas->pitch_cache || (int)c->species != cicadas->species_cache) {
    cicadas->pitch_cache = c->pitch_hz;
    cicadas->species_cache = (int)c->species;
    for (unsigned ear = 0; ear < 2; ++ear) {
      resonator_tune(&cicadas->chorus[ear], c->pitch_hz, q);
    }
  }
  if (cicadas->swell_samples == 0) {
    cicadas->swell_target = random_between(rng, 0.3f, 1.0f);
    cicadas->swell_samples = 4u * NOISE_SAMPLE_RATE_HZ;
  }
  --cicadas->swell_samples;
  cicadas->swell += CICADA_SWELL_ALPHA * (cicadas->swell_target - cicadas->swell);
  /* Band-passed noise power grows with Q; this holds the level at Q 3. */
  float level = CICADA_CHORUS_LEVEL * c->gain * c->chorus * cicadas->swell * sqrtf(3.0f / q);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float noise = 2.0f * random_unit(rng) - 1.0f;
    bus->direct[ear][bus->position] += level * resonator_next(&cicadas->chorus[ear], noise);
  }
}

/* Returns the reverb send; the direct sound goes to the bus. */
static float cicadas_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_listener_config *listener, noise_bus *bus) {
  if (c->gain <= 0.0f) return 0.0f;
  uint32_t *rng = &cicadas->rng;
  if (!cicadas->started) {
    cicadas->started = 1;
    for (unsigned i = 0; i < NOISE_CICADA_VOICES; ++i) {
      noise_cicada_voice *voice = &cicadas->voice[i];
      voice->pitch_offset = random_between(rng, -1.0f, 1.0f);
      voice->angle_offset = random_between(rng, -1.0f, 1.0f);
      voice->distance_offset = random_unit(rng);
      cicada_rest(rng, voice, c);
    }
    /* The layer is audible from its first frame. */
    cicadas->voice[0].until_call = 1;
  }
  float send = 0.0f;
  for (unsigned i = 0; i < NOISE_CICADA_VOICES; ++i) {
    send += cicada_next(rng, &cicadas->voice[i], c, listener, bus);
  }
  cicada_chorus_next(cicadas, c, bus);
  return send;
}

/* ---- Thunder ---- */

typedef struct thunder_build {
  noise_thunder_voice *voice;
  uint32_t *rng;
  float step_m; /* Mean segment length. */
  float centroid[3]; /* Weighted by weight * length until start_thunder divides. */
  float centroid_weight;
} thunder_build;

enum { FADE_NONE, FADE_BOTH_ENDS, FADE_TIP_END };

static int thunder_config_valid(const noise_thunder_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->rate_per_min, 0.0f, 20.0f) &&
         in_range(c->min_distance_m, 200.0f, 15000.0f) &&
         in_range(c->max_distance_m, c->min_distance_m, 15000.0f) &&
         in_range(c->reverb_gain, 0.0f, 1.0f) &&
         in_range(c->reverb_decay_s, 0.5f, 10.0f);
}

static void thunder_config_default(noise_thunder_config *c) {
  c->rate_per_min = 2.0f;
  c->min_distance_m = 1000.0f;
  c->max_distance_m = 8000.0f;
  c->reverb_gain = 0.5f;
  c->reverb_decay_s = 3.5f;
}

static void thunder_init(noise_thunder *thunder, uint32_t seed) {
  thunder->rng = stream_seed(seed, 0x2545f491u);
  thunder->echo_rng = stream_seed(seed, 0x6a09e667u);
  uint32_t terrain_rng = stream_seed(seed, 0xbb67ae85u);
  for (unsigned i = 0; i < NOISE_THUNDER_ECHOES; ++i) {
    noise_reflector *reflector = &thunder->reflector[i];
    float distance = random_between(&terrain_rng, 300.0f, 2500.0f);
    float angle = 2.0f * NOISE_PI * random_unit(&terrain_rng);
    reflector->position[0] = distance * sinf(angle);
    reflector->position[1] = distance * cosf(angle);
    reflector->reflectivity = random_between(&terrain_rng, 0.3f, 0.6f);
    reflector->smear_s = random_between(&terrain_rng, 0.1f, 0.4f);
  }
  thunder->reverb.decay_cache = -1.0f;
}

static float length3(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void add_thunder_segment(thunder_build *b, const float from[3], const float to[3],
                                float length_m, float weight) {
  noise_thunder_voice *voice = b->voice;
  float mid[3] = {0.5f * (from[0] + to[0]), 0.5f * (from[1] + to[1]),
                  0.5f * (from[2] + to[2])};
  float near = length3(from), far = length3(to);
  if (near > far) {
    float swap = near;
    near = far;
    far = swap;
  }
  float frames_per_m = NOISE_SAMPLE_RATE_HZ / 343.0f;
  noise_thunder_segment *segment = &voice->segment[voice->segments++];
  for (unsigned k = 0; k < 3; ++k) b->centroid[k] += weight * length_m * mid[k];
  b->centroid_weight += weight * length_m;
  segment->start = near * frames_per_m;
  segment->band = length3(mid); /* Range for now; start_thunder maps it to a band. */
  /* Fine steps wander in range by a random walk, so even a side-on segment spreads. */
  float wander_m = 0.28f * sqrtf(length_m * THUNDER_FINE_STEP_M);
  float spread_m = far - near;
  segment->width = sqrtf(spread_m * spread_m + wander_m * wander_m) * frames_per_m;
  float amplitude = 8.9f * weight * length_m * THUNDER_REFERENCE_M / length3(mid) /
                    segment->width;
  float horizontal = sqrtf(mid[0] * mid[0] + mid[1] * mid[1]);
  float pan = 0.25f * NOISE_PI * (1.0f + (horizontal > 0.0f ? mid[0] / horizontal : 0.0f));
  segment->gain[0] = amplitude * cosf(pan);
  segment->gain[1] = amplitude * sinf(pan);
  /* Fine steps each leave a random pulse; at one per 3 m their Poisson sum over a frame
     has variance roughness^2 * overlap. */
  float steps_per_frame = length_m / (THUNDER_FINE_STEP_M * segment->width);
  segment->roughness = THUNDER_ROUGHNESS / sqrtf(steps_per_frame);
}

/* Random walk with Gaussian direction changes, pulled toward a preferred direction. */
static void walk_thunder(thunder_build *b, float position[3], float direction[3],
                         const float preferred[3], float path_m, float weight, int fade) {
  float travelled = 0.0f;
  unsigned from = b->voice->segments;
  while (travelled < path_m && b->voice->segments < NOISE_THUNDER_SEGMENTS) {
    float step = b->step_m * random_between(b->rng, 0.5f, 1.5f);
    /* Measured: mean direction change 16.0 degrees (Hill), mean lean 28 degrees. */
    for (unsigned k = 0; k < 3; ++k) {
      direction[k] += 0.25f * random_gaussian(b->rng) + 0.2f * preferred[k];
    }
    float norm = 1.0f / length3(direction);
    float next[3];
    for (unsigned k = 0; k < 3; ++k) {
      direction[k] *= norm;
      next[k] = position[k] + step * direction[k];
    }
    add_thunder_segment(b, position, next, step, weight);
    memcpy(position, next, sizeof(next));
    travelled += step;
  }
  /* An end whose arrivals are still getting later is where this part's sound stops.
     Parts meeting there at full strength would stop together as a cut. */
  unsigned to = b->voice->segments;
  if (fade == FADE_NONE || to - from < 2u) return;
  noise_thunder_segment *segment = b->voice->segment;
  unsigned near_root = from + 3u < to ? from + 3u : to - 1u;
  unsigned near_tip = to >= from + 4u ? to - 4u : from;
  int root_late = fade == FADE_BOTH_ENDS && segment[from].start > segment[near_root].start;
  int tip_late = segment[to - 1u].start > segment[near_tip].start;
  for (unsigned k = from; k < to; ++k) {
    float done = ((float)(k - from) + 0.5f) / (float)(to - from);
    float gain = fminf(1.0f, fminf(root_late ? done : 1.0f, tip_late ? 1.0f - done : 1.0f) /
                             THUNDER_END_FADE);
    segment[k].gain[0] *= gain;
    segment[k].gain[1] *= gain;
  }
}

static int compare_thunder_segments(const void *a, const void *b) {
  float left = ((const noise_thunder_segment *)a)->start;
  float right = ((const noise_thunder_segment *)b)->start;
  return (left > right) - (left < right);
}

static noise_result start_thunder(noise_thunder *thunder, noise_state *state,
                                  position_polar position) {
  noise_thunder_voice *voice = NULL;
  for (unsigned i = 0; i < NOISE_THUNDER_VOICES && !voice; ++i) {
    if (!thunder->voice[i].length) voice = &thunder->voice[i];
  }
  if (!voice) {
    ++state->dropped_thunder;
    return NOISE_VOICE_LIMIT;
  }
  memset(voice, 0, sizeof(*voice));
  ++state->generated_thunder;
  uint32_t *rng = &thunder->rng;
  float distance = position.distance_m;

  float height = random_between(rng, 1500.0f, 4000.0f);
  float main_path = 1.15f * height;
  float cloud_path = random_between(rng, 1500.0f, 5000.0f);
  unsigned branches = 1u + random_u32(rng) % 3u;
  float branch_at[3], branch_path[3];
  unsigned arms = 2u + random_u32(rng) % 2u;
  float total = main_path + (float)arms * cloud_path;
  for (unsigned i = 0; i < branches; ++i) {
    branch_at[i] = random_between(rng, 0.2f, 0.9f);
    branch_path[i] = random_between(rng, 200.0f, 1200.0f);
    total += branch_path[i];
  }
  for (unsigned i = 1; i < branches; ++i) {
    for (unsigned j = i; j > 0 && branch_at[j] < branch_at[j - 1]; --j) {
      float swap = branch_at[j];
      branch_at[j] = branch_at[j - 1];
      branch_at[j - 1] = swap;
    }
  }
  /* Size segments so the whole channel fits the pool with a little spare. */
  thunder_build build = {voice, rng, total / (0.95f * NOISE_THUNDER_SEGMENTS), {0.0f}, 0.0f};

  /* Listener at the origin, x right, y front, z up. */
  float point[3] = {distance * sinf(position.angle_rad),
                    distance * cosf(position.angle_rad), 0.0f};
  float direction[3] = {0.0f, 0.0f, 1.0f};
  const float up[3] = {0.0f, 0.0f, 1.0f};
  float branch_point[3][3], branch_direction[3][3];
  float walked = 0.0f;
  for (unsigned i = 0; i < branches; ++i) {
    walk_thunder(&build, point, direction, up, branch_at[i] * main_path - walked, 1.0f,
                 FADE_NONE);
    walked = branch_at[i] * main_path;
    memcpy(branch_point[i], point, sizeof(point));
    memcpy(branch_direction[i], direction, sizeof(direction));
  }
  walk_thunder(&build, point, direction, up, main_path - walked, 1.0f, FADE_TIP_END);

  /* In-cloud arms spread in heading, so some arm usually arrives after the channel top.
     They add incoherently, so each gets weight / sqrt(arms). */
  float heading = 2.0f * NOISE_PI * random_unit(rng);
  float top[3];
  memcpy(top, point, sizeof(top));
  for (unsigned arm = 0; arm < arms; ++arm) {
    float h = heading + 2.0f * NOISE_PI * ((float)arm + random_between(rng, -0.25f, 0.25f)) /
              (float)arms;
    float level[3] = {cosf(h), sinf(h), 0.0f};
    memcpy(point, top, sizeof(top));
    memcpy(direction, level, sizeof(level));
    walk_thunder(&build, point, direction, level, cloud_path, 0.6f / sqrtf((float)arms),
                 FADE_BOTH_ENDS);
  }

  for (unsigned i = 0; i < branches; ++i) {
    float outward = 2.0f * NOISE_PI * random_unit(rng);
    float down[3] = {0.64f * cosf(outward), 0.64f * sinf(outward), -0.77f};
    walk_thunder(&build, branch_point[i], branch_direction[i], down, branch_path[i], 0.4f,
                 FADE_BOTH_ENDS);
  }

  qsort(voice->segment, voice->segments, sizeof(voice->segment[0]),
        compare_thunder_segments);
  float first = voice->segment[0].start;
  float last = 0.0f;
  float nearest = voice->segment[0].band, farthest = nearest;
  for (unsigned i = 0; i < voice->segments; ++i) {
    voice->segment[i].start -= first;
    float end = voice->segment[i].start + voice->segment[i].width;
    if (end > last) last = end;
    nearest = fminf(nearest, voice->segment[i].band);
    farthest = fmaxf(farthest, voice->segment[i].band);
  }
  /* Direct bands are evenly spaced in log range. */
  float span = logf(farthest / nearest);
  float bands_per_log = span > 0.0f ? (THUNDER_DIRECT_BANDS - 1u) / span : 0.0f;
  for (unsigned i = 0; i < voice->segments; ++i) {
    voice->segment[i].band = bands_per_log * logf(voice->segment[i].band / nearest);
  }
  voice->span_log = span;

  /* Each reflector returns the whole strike, delayed by its extra path from the channel's
     centroid, from its own direction, with spherical spreading over the longer path. */
  float centre[3];
  for (unsigned k = 0; k < 3; ++k) centre[k] = build.centroid[k] / build.centroid_weight;
  float direct_m = length3(centre);
  float widest = 1.0f, latest = 0.0f;
  for (unsigned i = 0; i < NOISE_THUNDER_ECHOES; ++i) {
    const noise_reflector *reflector = &thunder->reflector[i];
    noise_thunder_echo *echo = &voice->echo[i];
    float ground[3] = {reflector->position[0], reflector->position[1], 0.0f};
    float out[3] = {centre[0] - ground[0], centre[1] - ground[1], centre[2]};
    float path_m = length3(out) + length3(ground);
    echo->delay = (path_m - direct_m) * NOISE_SAMPLE_RATE_HZ / 343.0f;
    echo->smear = reflector->smear_s * NOISE_SAMPLE_RATE_HZ;
    echo->range_log = logf(path_m / direct_m);
    float amplitude = reflector->reflectivity * direct_m / path_m;
    float pan = 0.25f * NOISE_PI * (1.0f + ground[0] / length3(ground));
    echo->gain[0] = amplitude * cosf(pan);
    echo->gain[1] = amplitude * sinf(pan);
    widest = fmaxf(widest, path_m / direct_m);
    latest = fmaxf(latest, echo->delay + echo->smear);
  }
  voice->echo_span_log = logf(widest);
  /* Later arrivals travel farther, so the tail is darker and its N-waves longer.
     N-waves last 6 to 14 ms at 1 km and lengthen with the fourth root of distance. */
  float period_1km_s = 0.001f * random_between(rng, 6.0f, 14.0f);
  for (unsigned band = 0; band < NOISE_THUNDER_BANDS; ++band) {
    float band_m = band < THUNDER_DIRECT_BANDS ?
        distance * expf(span * band / (THUNDER_DIRECT_BANDS - 1u)) :
        distance * expf(span + voice->echo_span_log);
    float period_s = period_1km_s * sqrtf(sqrtf(band_m / THUNDER_REFERENCE_M));
    float air_cutoff = fminf(6000.0f, fmaxf(150.0f, 1000.0f *
        powf(THUNDER_REFERENCE_M / band_m, 0.6f)));
    for (unsigned channel = 0; channel < 2; ++channel) {
      biquad_tune(&voice->pulse[band][channel], 1, 1.0f / period_s, 0.7f);
      biquad_tune(&voice->air[band][channel][0], 0, air_cutoff, 0.5411961f);
      biquad_tune(&voice->air[band][channel][1], 0, air_cutoff, 1.3065630f);
    }
  }
  /* 4096 frames let the filters ring out after the last arrival. */
  voice->length = (uint32_t)(last + latest) + 4096u;
  return NOISE_OK;
}

/* Adds a segment's box to the two bands nearest its range position. */
static void add_to_bands(float excitation[NOISE_THUNDER_BANDS][2], float position,
                         const float amount[2]) {
  position = fminf(fmaxf(position, 0.0f), NOISE_THUNDER_BANDS - 1.0f);
  unsigned band = (unsigned)position;
  if (band > NOISE_THUNDER_BANDS - 2u) band = NOISE_THUNDER_BANDS - 2u;
  float upper = position - (float)band;
  for (unsigned channel = 0; channel < 2; ++channel) {
    excitation[band][channel] += (1.0f - upper) * amount[channel];
    excitation[band + 1][channel] += upper * amount[channel];
  }
}

/* Echo paths inside the direct span use the direct bands; longer ones reach band 3. */
static float echo_band(const noise_thunder_voice *voice, float band, float range_log) {
  float span = voice->span_log;
  float u = band * span / (THUNDER_DIRECT_BANDS - 1u) + range_log;
  if (u <= span) return span > 0.0f ? (THUNDER_DIRECT_BANDS - 1u) * u / span : 0.0f;
  return (THUNDER_DIRECT_BANDS - 1u) + (u - span) / voice->echo_span_log;
}

static void thunder_voice_next(uint32_t *rng, uint32_t *echo_rng, noise_thunder_voice *voice,
                               float out[2]) {
  float t = (float)voice->elapsed;
  const noise_thunder_segment *segment = voice->segment;
  while (voice->next < voice->segments && segment[voice->next].start < t + 1.0f) {
    ++voice->next;
  }
  while (voice->first < voice->next &&
         t >= segment[voice->first].start + segment[voice->first].width) {
    ++voice->first;
  }
  /* Each segment adds a box over its arrival spread; the filters shape it into pulses. */
  float excitation[NOISE_THUNDER_BANDS][2] = {{0.0f}};
  for (unsigned i = voice->first; i < voice->next; ++i) {
    float x = t - segment[i].start;
    float overlap = fminf(x + 1.0f, segment[i].width) - fmaxf(x, 0.0f);
    if (overlap <= 0.0f) continue;
    float share = overlap + segment[i].roughness * random_gaussian(rng) * sqrtf(overlap);
    float amount[2] = {segment[i].gain[0] * share, segment[i].gain[1] * share};
    add_to_bands(excitation, segment[i].band, amount);
  }
  /* An echo spreads each box over its smear at the same total energy. */
  for (unsigned e = 0; e < NOISE_THUNDER_ECHOES; ++e) {
    noise_thunder_echo *echo = &voice->echo[e];
    float te = t - echo->delay;
    while (echo->next < voice->segments && segment[echo->next].start < te + 1.0f) {
      ++echo->next;
    }
    while (echo->first < echo->next &&
           te >= segment[echo->first].start + segment[echo->first].width + echo->smear) {
      ++echo->first;
    }
    for (unsigned i = echo->first; i < echo->next; ++i) {
      float width = segment[i].width + echo->smear;
      float x = te - segment[i].start;
      float overlap = fminf(x + 1.0f, width) - fmaxf(x, 0.0f);
      if (overlap <= 0.0f) continue;
      float share = segment[i].width / width *
          (overlap + segment[i].roughness * random_gaussian(echo_rng) * sqrtf(overlap));
      float level = hypotf(segment[i].gain[0], segment[i].gain[1]) * share;
      float amount[2] = {echo->gain[0] * level, echo->gain[1] * level};
      add_to_bands(excitation, echo_band(voice, segment[i].band, echo->range_log), amount);
    }
  }
  for (unsigned channel = 0; channel < 2; ++channel) {
    out[channel] = 0.0f;
    for (unsigned band = 0; band < NOISE_THUNDER_BANDS; ++band) {
      float pulse = biquad_next(&voice->pulse[band][channel], excitation[band][channel]);
      out[channel] += biquad_next(&voice->air[band][channel][1],
                                  biquad_next(&voice->air[band][channel][0], pulse));
    }
  }
  if (++voice->elapsed == voice->length) voice->length = 0;
}

/* Unity below 0.5, then a tanh knee toward 1: near booms have about 25 dB crest. */
static float thunder_limit(float x) {
  float magnitude = fabsf(x);
  if (magnitude <= 0.5f) return x;
  return copysignf(0.5f + 0.5f * tanhf(2.0f * (magnitude - 0.5f)), x);
}

/* Runs at a quarter rate: thunder is mostly below 2 kHz, and memory drops by four. */
static void thunder_reverb_next(noise_thunder_reverb *reverb, float decay_s, float send,
                                float wet[2]) {
  if (decay_s != reverb->decay_cache) {
    reverb->decay_cache = decay_s;
    float rate = (float)NOISE_SAMPLE_RATE_HZ / THUNDER_REVERB_DECIMATION;
    for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
      reverb->fdn.feedback[i] = powf(0.001f, (float)thunder_reverb_length[i] / (decay_s * rate));
    }
  }
  reverb->input += send;
  if (++reverb->phase == THUNDER_REVERB_DECIMATION) {
    reverb->phase = 0;
    float *previous = reverb->output[0];
    float *current = reverb->output[1];
    previous[0] = current[0];
    previous[1] = current[1];
    /* Damping alpha 0.5 at 11025 Hz: about a 1.2 kHz loop low-pass. */
    fdn_next(reverb->buffer, thunder_reverb_length, thunder_reverb_offset, &reverb->fdn, 0.5f,
             reverb->input / THUNDER_REVERB_DECIMATION, current);
    reverb->input = 0.0f;
  }
  float blend = (float)reverb->phase / THUNDER_REVERB_DECIMATION;
  for (unsigned channel = 0; channel < 2; ++channel) {
    wet[channel] = reverb->output[0][channel] + blend *
        (reverb->output[1][channel] - reverb->output[0][channel]);
  }
}

static void thunder_next(noise_thunder *thunder, const noise_thunder_config *c,
                         noise_state *state, float *left, float *right) {
  if (c->gain > 0.0f && c->rate_per_min > 0.0f) {
    int trigger = !thunder->started;
    thunder->started = 1;
    if (!trigger) {
      /* 32-bit comparison resolves rates far below 0.01 strikes/min. */
      float probability = c->rate_per_min / (60.0f * NOISE_SAMPLE_RATE_HZ);
      trigger = random_u32(&thunder->rng) < (uint32_t)(probability * 4294967296.0f);
    }
    if (trigger) {
      position_polar position;
      position.distance_m = area_uniform_distance(c->min_distance_m, c->max_distance_m,
                                                   random_unit(&thunder->rng));
      position.angle_rad = 2.0f * NOISE_PI * random_unit(&thunder->rng);
      /* Capacity losses are recorded by start_thunder for both paths. */
      (void)start_thunder(thunder, state, position);
    }
  }
  float sum[2] = {0.0f, 0.0f};
  for (unsigned i = 0; i < NOISE_THUNDER_VOICES; ++i) {
    if (!thunder->voice[i].length) continue;
    float out[2];
    thunder_voice_next(&thunder->rng, &thunder->echo_rng, &thunder->voice[i], out);
    sum[0] += c->gain * out[0];
    sum[1] += c->gain * out[1];
  }
  if (c->reverb_gain > 0.0f) {
    float wet[2];
    /* Unity send: gain 0.5 puts the wet about 3 dB under the dry roll. */
    thunder_reverb_next(&thunder->reverb, c->reverb_decay_s, sum[0] + sum[1], wet);
    sum[0] += c->reverb_gain * wet[0];
    sum[1] += c->reverb_gain * wet[1];
  }
  *left += thunder_limit(sum[0]);
  *right += thunder_limit(sum[1]);
}

/* ---- Weather ---- */

static int weather_config_valid(const noise_weather_config *c) {
  if (!in_range(c->intensity, 0.0f, 1.0f) ||
      !in_range(c->min_intensity, 0.0f, 1.0f) ||
      !in_range(c->max_intensity, c->min_intensity, 1.0f) ||
      (c->vary != 0 && c->vary != 1) ||
      !in_range(c->step_s, 0.1f, 3600.0f) ||
      !in_range(c->slew_s, 0.01f, 60.0f)) {
    return 0;
  }
  if (c->vary && !in_range(c->intensity, c->min_intensity, c->max_intensity)) return 0;
  for (unsigned i = 0; i < NOISE_WEATHER_MOD_COUNT; ++i) {
    if (!in_range(c->mod_amount[i], -1.0f, 1.0f)) return 0;
  }
  return 1;
}

static void weather_config_default(noise_weather_config *c) {
  c->min_intensity = 0.15f;
  c->max_intensity = 0.85f;
  c->step_s = 8.0f;
  c->slew_s = 2.0f;
  c->mod_amount[WEATHER_MOD_ARRIVAL_RATE] = 1.0f;
  c->mod_amount[WEATHER_MOD_DROP_SIZE] = 1.0f;
}

static void weather_init(noise_weather *weather, noise_state *state,
                         const noise_weather_config *c, uint32_t seed) {
  weather->rng = stream_seed(seed, 0x78dde6e4u);
  state->rain_intensity = c->intensity;
  state->rain_target = c->intensity;
  float span = c->max_intensity - c->min_intensity;
  float relative = span > 0.0f ? (c->intensity - c->min_intensity) / span : 0.0f;
  state->weather_state = relative < 0.25f ? 0u : (relative < 0.75f ? 1u : 2u);
  weather->period = (uint32_t)(c->step_s * NOISE_SAMPLE_RATE_HZ);
  weather->slew = -expm1f(-1.0f / (c->slew_s * NOISE_SAMPLE_RATE_HZ));
}

static void weather_next(noise_weather *weather, const noise_weather_config *c,
                         noise_state *state) {
  if (!c->vary) return;
  if (++weather->samples == weather->period) {
    static const float cdf[3][2] = {{0.85f, 1.0f}, {0.10f, 0.90f}, {0.0f, 0.15f}};
    weather->samples = 0;
    float choice = random_unit(&weather->rng);
    unsigned from = state->weather_state;
    unsigned to = choice < cdf[from][0] ? 0u : (choice < cdf[from][1] ? 1u : 2u);
    state->weather_state = to;
    state->rain_target = c->min_intensity + 0.5f * (float)to *
        (c->max_intensity - c->min_intensity);
  }
  /* Carry sub-ULP steps so long time constants still reach their target. */
  float step = weather->slew * (state->rain_target - state->rain_intensity) +
               weather->slew_error;
  float next = state->rain_intensity + step;
  weather->slew_error = step - (next - state->rain_intensity);
  state->rain_intensity = next;
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

/* ---- Rain ---- */

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

static int rain_config_valid(const noise_rain_config *c) {
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

static void rain_config_default(noise_rain_config *c) {
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

static void rain_init(noise_rain *rain, const noise_rain_config *c, uint32_t seed) {
  rain->arrival_rng = stream_seed(seed, 0x3c6ef372u);
  rain->drop_rng = stream_seed(seed, 0xdaa66d2bu);
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) sum += c->surface_weight[i];
  float cumulative = 0.0f;
  for (unsigned i = 0; i < NOISE_SURFACE_COUNT; ++i) {
    cumulative += c->surface_weight[i];
    rain->surface_cdf[i] = cumulative / sum;
  }
}

static int drop_valid(const droplet *drop) {
  return (unsigned)drop->surface < NOISE_SURFACE_COUNT &&
         in_range(drop->radius_m, 0.0004f, 0.0029f) &&
         in_range(drop->velocity_m_s, 0.0f, 12.0f) &&
         in_range(drop->position.distance_m, 0.25f, 100.0f) &&
         in_range(drop->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         (drop->bubble_radius_m == 0.0f ||
          (drop->surface == WATER && in_range(drop->bubble_radius_m, 0.00016f, 0.004f)));
}

static noise_result start_drop(noise_rain *rain, noise_state *state, const noise_config *config,
                               const droplet *drop) {
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
  const noise_water_config *water = &config->rain.water;
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
  spatial_init(&voice->spatial, &config->listener, drop->position);
  voice->filter_tail = 256;
  return NOISE_OK;
}

static impact_surface choose_surface(noise_rain *rain, const noise_config *config,
                                     float intensity) {
  const float *mod_amount = &config->weather.mod_amount[WEATHER_MOD_WATER_WEIGHT];
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
    weight[i] = config->rain.surface_weight[i] * scale;
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

static void spawn_rain(noise_rain *rain, noise_state *state, const noise_config *config) {
  const float *mod_amount = config->weather.mod_amount;
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
  float fall_height = weather_mod_log(config->rain.fall_height_m,
      mod_amount[WEATHER_MOD_FALL_HEIGHT], intensity, 0.01f, 1000.0f);
  drop.velocity_m_s = terminal * sqrtf(-expm1f(
      -2.0f * NOISE_GRAVITY * fall_height / (terminal * terminal)));
  drop.surface = choose_surface(rain, config, intensity);
  drop.bubble_radius_m = 0.0f;
  const noise_water_config *water = &config->rain.water;
  if (drop.surface == WATER && random_unit(&rain->drop_rng) < water->bubble_probability) {
    drop.bubble_radius_m = random_log_between(&rain->drop_rng,
        water->bubble_radius_min_m, water->bubble_radius_max_m);
  }
  float near = weather_mod_log(config->rain.min_distance_m,
      mod_amount[WEATHER_MOD_MIN_DISTANCE], intensity, 0.25f, 100.0f);
  float far = weather_mod_log(config->rain.max_distance_m,
      mod_amount[WEATHER_MOD_MAX_DISTANCE], intensity, 0.25f, 100.0f);
  if (near > far) {
    float swap = near;
    near = far;
    far = swap;
  }
  drop.position.distance_m = area_uniform_distance(near, far, random_unit(&rain->drop_rng));
  drop.position.angle_rad = 2.0f * NOISE_PI * random_unit(&rain->drop_rng);
  /* Capacity losses are recorded by start_drop for both arrival paths. */
  (void)start_drop(rain, state, config, &drop);
}

/* Returns the reverb send; the direct sound goes to the bus. */
static float rain_next(noise_rain *rain, noise_state *state, const noise_config *config,
                       noise_bus *bus) {
  const float *mod_amount = config->weather.mod_amount;
  float intensity = state->rain_intensity;
  float gain = weather_mod_linear(config->rain.gain, mod_amount[WEATHER_MOD_RAIN_GAIN],
                                  intensity, 0.0f, 1.0f);
  float density = arrival_level(intensity, mod_amount[WEATHER_MOD_ARRIVAL_RATE]);
  if (gain > 0.0f && density > 0.0f) {
    float probability = density * config->rain.max_drops_per_s / NOISE_SAMPLE_RATE_HZ;
    if (random_unit(&rain->arrival_rng) < probability) spawn_rain(rain, state, config);
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
    spatial_next(&voice->spatial, &config->listener, bus, source);
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

/* ---- Engine ---- */

static int config_valid(const noise_config *c) {
  if (!c || !in_range(c->master_gain, 0.0f, 1.0f) || !in_range(c->reverb_gain, 0.0f, 1.0f)) {
    return 0;
  }
  for (unsigned i = 0; i < NOISE_KIND_COUNT; ++i) {
    if (!in_range(c->ambient_gain[i], 0.0f, 1.0f)) return 0;
  }
  return listener_config_valid(&c->listener) &&
         weather_config_valid(&c->weather) &&
         rain_config_valid(&c->rain) &&
         wind_config_valid(&c->wind) &&
         cricket_config_valid(&c->crickets) &&
         cicada_config_valid(&c->cicadas) &&
         thunder_config_valid(&c->thunder);
}

void noise_config_default(noise_config *c) {
  memset(c, 0, sizeof(*c));
  c->master_gain = 0.8f;
  c->ambient_gain[NOISE_KIND_PINK] = 0.3f;
  c->reverb_gain = 0.12f;
  c->listener.stereo_width_m = 0.18f;
  c->listener.head_amount = 1.0f;
  c->listener.rear_amount = 1.0f;
  weather_config_default(&c->weather);
  rain_config_default(&c->rain);
  wind_config_default(&c->wind);
  cricket_config_default(&c->crickets);
  cicada_config_default(&c->cicadas);
  thunder_config_default(&c->thunder);
}

noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed) {
  if (!gen || !config_valid(config)) return NOISE_INVALID_CONFIG;
  noise_config copy = *config;
  memset(gen, 0, sizeof(*gen));
  gen->config = copy;
  seed = seed ? seed : 1u;
  ambient_init(&gen->ambient, seed);
  wind_init(&gen->wind, seed);
  crickets_init(&gen->crickets, seed);
  cicadas_init(&gen->cicadas, seed);
  thunder_init(&gen->thunder, seed);
  weather_init(&gen->weather, &gen->state, &gen->config.weather, seed);
  rain_init(&gen->rain, &gen->config.rain, seed);
  reverb_init(&gen->reverb);
  return NOISE_OK;
}

noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop) {
  if (!gen || !drop || !drop_valid(drop)) return NOISE_INVALID_DROP;
  return start_drop(&gen->rain, &gen->state, &gen->config, drop);
}

noise_result noise_trigger_thunder(noise_gen *gen, const thunder_strike *strike) {
  if (!gen || !strike ||
      !in_range(strike->position.distance_m, 200.0f, 15000.0f) ||
      !in_range(strike->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI)) {
    return NOISE_INVALID_STRIKE;
  }
  return start_thunder(&gen->thunder, &gen->state, strike->position);
}

static int16_t to_sample(noise_state *state, float value) {
  if (value > 1.0f) {
    ++state->clipped_samples;
    return INT16_MAX;
  }
  if (value < -1.0f) {
    ++state->clipped_samples;
    return INT16_MIN;
  }
  return (int16_t)(value * 32767.0f);
}

size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames) {
  const noise_config *c = &gen->config;
  for (size_t frame = 0; frame < frames; ++frame) {
    weather_next(&gen->weather, &c->weather, &gen->state);
    float send = rain_next(&gen->rain, &gen->state, c, &gen->bus);
    send += crickets_next(&gen->crickets, &c->crickets, &c->listener, &gen->bus);
    send += cicadas_next(&gen->cicadas, &c->cicadas, &c->listener, &gen->bus);
    float left, right;
    bus_next(&gen->bus, &left, &right);
    thunder_next(&gen->thunder, &c->thunder, &gen->state, &left, &right);
    if (c->reverb_gain > 0.0f || c->weather.mod_amount[WEATHER_MOD_REVERB_GAIN] != 0.0f) {
      float reverb_gain = weather_mod_linear(c->reverb_gain,
          c->weather.mod_amount[WEATHER_MOD_REVERB_GAIN], gen->state.rain_intensity,
          0.0f, 1.0f);
      reverb_next(&gen->reverb, send, reverb_gain, &left, &right);
    }
    float ambient = ambient_next(&gen->ambient, c->ambient_gain);
    left += ambient;
    right += ambient;
    wind_next(&gen->wind, &c->wind, &left, &right);
    out[2 * frame] = to_sample(&gen->state, left * c->master_gain);
    out[2 * frame + 1] = to_sample(&gen->state, right * c->master_gain);
  }
  return frames;
}
