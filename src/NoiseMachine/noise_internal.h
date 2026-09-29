#ifndef NOISE_INTERNAL_H
#define NOISE_INTERNAL_H

/* Shared by the engine's own sources; not part of the public interface. */

#include <math.h>

#include "noise_dsp.h"
#include "noise_types.h"

#define NOISE_PI 3.14159265358979323846f

static inline uint32_t random_u32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static inline float random_unit(uint32_t *state) {
  return (float)(random_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static inline float random_between(uint32_t *state, float low, float high) {
  return low + (high - low) * random_unit(state);
}

static inline float random_log_between(uint32_t *state, float low, float high) {
  return expf(logf(low) + (logf(high) - logf(low)) * random_unit(state));
}

/* Irwin-Hall sum of four uniforms, scaled to unit variance. */
static inline float random_gaussian(uint32_t *rng) {
  float sum = random_unit(rng) + random_unit(rng) + random_unit(rng) + random_unit(rng);
  return 1.7320508f * (sum - 2.0f);
}

static inline uint32_t stream_seed(uint32_t seed, uint32_t tag) {
  /* Avalanche the tags so streams do not start at adjacent xorshift positions. */
  uint32_t x = seed + tag;
  x = (x ^ (x >> 16)) * 0x85ebca6bu;
  x = (x ^ (x >> 13)) * 0xc2b2ae35u;
  x ^= x >> 16;
  return x ? x : tag;
}

static inline int in_range(float value, float low, float high) {
  return isfinite(value) && value >= low && value <= high;
}

/* Radius between near and far that is uniform over the annulus area for uniform u. */
static inline float area_uniform_distance(float near, float far, float u) {
  return sqrtf(near * near + u * (far * far - near * near));
}

static inline void mode_init(noise_mode *mode, float frequency, float damping,
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

static inline float mode_next(noise_mode *mode) {
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

static inline void oscillator_init(noise_oscillator *oscillator, float frequency) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  oscillator->previous = -sinf(phase);
  oscillator->current = 0.0f;
  oscillator->coefficient = 2.0f * cosf(phase);
}

static inline float oscillator_next(noise_oscillator *oscillator) {
  float value = oscillator->current;
  float next = oscillator->coefficient * oscillator->current - oscillator->previous;
  oscillator->previous = oscillator->current;
  oscillator->current = next;
  return value;
}

static inline void resonator_tune(noise_resonator *resonator, float frequency, float q) {
  float phase = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  float radius = expf(-NOISE_PI * frequency / (q * NOISE_SAMPLE_RATE_HZ));
  resonator->coefficient = 2.0f * radius * cosf(phase);
  resonator->radius_squared = radius * radius;
}

static inline float resonator_next(noise_resonator *resonator, float input) {
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
static inline void biquad_tune(noise_biquad *filter, int bandpass, float frequency, float q) {
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

static inline float biquad_next(noise_biquad *filter, float input) {
  float output = filter->b0 * input + filter->state[0];
  filter->state[0] = filter->b1 * input - filter->a1 * output + filter->state[1];
  filter->state[1] = filter->b2 * input - filter->a2 * output;
  return output;
}

#endif
