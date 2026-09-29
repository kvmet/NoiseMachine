#ifndef NOISE_WIND_H
#define NOISE_WIND_H

#include "noise_types.h"

typedef struct noise_wind_config {
  float gain; /* Level at 20 m/s. */
  float stereo_width;
  float brightness; /* Scales the air cutoff the wind speed sets; 0.25..4. */
  float rumble; /* Low rumble relative to its default share; 0..2. */
  float balance; /* How far level shifts toward the ear facing the wind; 0..1. */
} noise_wind_config;

typedef struct noise_wind {
  uint32_t rng;
  float air[2];
  float rumble[2];
  float air_alpha;
  float relative_level; /* Level over gain for the current wind speed. */
  float level[2]; /* Per ear, gliding toward target so weather steps do not click. */
  float target[2];
} noise_wind;

#ifdef __cplusplus
extern "C" {
#endif

int noise_wind_config_valid(const noise_wind_config *c);
void noise_wind_config_default(noise_wind_config *c);
void noise_wind_init(noise_wind *wind, uint32_t seed);
/* Sets level, brightness, and balance from the wind speed and bearing. */
void noise_wind_follow(noise_wind *wind, const noise_wind_config *c, const noise_weather *weather);
void noise_wind_next(noise_wind *wind, const noise_wind_config *c, float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
