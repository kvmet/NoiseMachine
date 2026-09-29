#ifndef NOISE_WIND_H
#define NOISE_WIND_H

#include "noise_types.h"

typedef struct noise_wind_config {
  float gain;
  float brightness;
  float gust_depth;
  float gust_rate_hz;
  float stereo_width;
} noise_wind_config;

typedef struct noise_wind {
  uint32_t rng;
  float air[2];
  float rumble[2];
  float gust;
  float gust_target;
  float air_alpha;
  float gust_alpha;
  uint32_t gust_samples;
} noise_wind;

#ifdef __cplusplus
extern "C" {
#endif

int noise_wind_config_valid(const noise_wind_config *c);
void noise_wind_config_default(noise_wind_config *c);
void noise_wind_init(noise_wind *wind, uint32_t seed);
void noise_wind_configure(noise_wind *wind, const noise_wind_config *c);
void noise_wind_next(noise_wind *wind, const noise_wind_config *c, float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
