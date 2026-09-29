#include "noise_wind.h"

#include <math.h>

#include "noise_internal.h"

#define WIND_REFERENCE_M_S 20.0f
#define WIND_LOUDEST_M_S 35.0f
#define WIND_BRIGHTEST_M_S 30.0f
#define WIND_GLIDE (1.0f / (0.02f * NOISE_SAMPLE_RATE_HZ)) /* 20 ms level glide. */
#define WIND_HIGHEST_CUTOFF_HZ 18000.0f
#define WIND_RUMBLE 0.30f
#define WIND_AIR 0.55f

int noise_wind_config_valid(const noise_wind_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->stereo_width, 0.0f, 1.0f) &&
         in_range(c->brightness, 0.25f, 4.0f) &&
         in_range(c->rumble, 0.0f, 2.0f) &&
         in_range(c->balance, 0.0f, 1.0f);
}

void noise_wind_config_default(noise_wind_config *c) {
  c->stereo_width = 0.5f;
  c->brightness = 1.0f;
  c->rumble = 1.0f;
  c->balance = 0.5f;
}

void noise_wind_init(noise_wind *wind, uint32_t seed) {
  wind->rng = stream_seed(seed, 0x1715609du);
}

void noise_wind_follow(noise_wind *wind, const noise_wind_config *c, const noise_weather *weather) {
  float speed = weather->wind_m_s;
  /* Amplitude rises 12 dB per doubling of speed: a fitted curve, not a flow model. */
  float relative = fminf(speed, WIND_LOUDEST_M_S) / WIND_REFERENCE_M_S;
  wind->relative_level = relative * relative;
  float level = c->gain * wind->relative_level;
  float lateral = sinf(weather->wind_bearing_rad);
  wind->target[0] = level * sqrtf(1.0f - c->balance * lateral);
  wind->target[1] = level * sqrtf(1.0f + c->balance * lateral);
  float brightness = fminf(1.0f, speed / WIND_BRIGHTEST_M_S);
  float cutoff = fminf(WIND_HIGHEST_CUTOFF_HZ, c->brightness * 400.0f * powf(20.0f, brightness));
  wind->air_alpha = -expm1f(-2.0f * NOISE_PI * cutoff / NOISE_SAMPLE_RATE_HZ);
}

void noise_wind_next(noise_wind *wind, const noise_wind_config *c, float *left, float *right) {
  if (c->gain <= 0.0f) return;
  const float rumble_alpha = 0.0169533f;
  float common = 2.0f * random_unit(&wind->rng) - 1.0f;
  float width = c->stereo_width;
  float *output[2] = {left, right};
  for (unsigned channel = 0; channel < 2; ++channel) {
    float side = 2.0f * random_unit(&wind->rng) - 1.0f;
    float input = (1.0f - width) * common + width * side;
    wind->air[channel] += wind->air_alpha * (input - wind->air[channel]);
    wind->rumble[channel] += rumble_alpha * (input - wind->rumble[channel]);
    wind->level[channel] += WIND_GLIDE * (wind->target[channel] - wind->level[channel]);
    *output[channel] += wind->level[channel] *
        (WIND_AIR * wind->air[channel] + WIND_RUMBLE * c->rumble * wind->rumble[channel]);
  }
}
