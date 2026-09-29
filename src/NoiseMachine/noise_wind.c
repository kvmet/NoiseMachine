#include "noise_wind.h"

#include <math.h>

#include "noise_internal.h"

int noise_wind_config_valid(const noise_wind_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->brightness, 0.0f, 1.0f) &&
         in_range(c->gust_depth, 0.0f, 1.0f) &&
         in_range(c->gust_rate_hz, 0.01f, 2.0f) &&
         in_range(c->stereo_width, 0.0f, 1.0f);
}

void noise_wind_config_default(noise_wind_config *c) {
  c->brightness = 0.5f;
  c->gust_depth = 0.6f;
  c->gust_rate_hz = 0.12f;
  c->stereo_width = 0.5f;
}

void noise_wind_init(noise_wind *wind, uint32_t seed) {
  wind->rng = stream_seed(seed, 0x1715609du);
  wind->gust = 0.5f;
  wind->gust_target = 0.5f;
}

void noise_wind_configure(noise_wind *wind, const noise_wind_config *c) {
  wind->gust_alpha = -expm1f(-2.0f * NOISE_PI * c->gust_rate_hz / NOISE_SAMPLE_RATE_HZ);
  float cutoff = 400.0f * powf(20.0f, c->brightness);
  wind->air_alpha = -expm1f(-2.0f * NOISE_PI * cutoff / NOISE_SAMPLE_RATE_HZ);
}

void noise_wind_next(noise_wind *wind, const noise_wind_config *c, float *left, float *right) {
  if (c->gain <= 0.0f) return;
  if (wind->gust_samples == 0) {
    wind->gust_target = random_unit(&wind->rng);
    wind->gust_samples = (uint32_t)(NOISE_SAMPLE_RATE_HZ / c->gust_rate_hz);
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
