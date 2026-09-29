#ifndef NOISE_WEATHER_H
#define NOISE_WEATHER_H

#include "noise_types.h"

typedef enum weather_mod_destination {
  WEATHER_MOD_ARRIVAL_RATE = 0,
  WEATHER_MOD_DROP_SIZE,
  WEATHER_MOD_RAIN_GAIN,
  WEATHER_MOD_REVERB_GAIN,
  WEATHER_MOD_FALL_HEIGHT,
  WEATHER_MOD_MIN_DISTANCE,
  WEATHER_MOD_MAX_DISTANCE,
  WEATHER_MOD_SURFACE_WEIGHT, /* One route per surface slot. */
  NOISE_WEATHER_MOD_COUNT = WEATHER_MOD_SURFACE_WEIGHT + NOISE_SURFACE_SLOTS
} weather_mod_destination;

typedef struct noise_weather_config {
  int vary;
  float intensity; /* Initial intensity, 0..1; zero means no arrivals. */
  float min_intensity;
  float max_intensity;
  float step_s;
  float slew_s;
  float mod_amount[NOISE_WEATHER_MOD_COUNT]; /* Bipolar depths, -1..1. */
} noise_weather_config;

typedef struct noise_weather {
  uint32_t rng;
  uint32_t samples;
  uint32_t period;
  float slew;
  float slew_error;
} noise_weather;

#ifdef __cplusplus
extern "C" {
#endif

int noise_weather_config_valid(const noise_weather_config *c);
void noise_weather_config_default(noise_weather_config *c);
void noise_weather_init(noise_weather *weather, noise_state *state,
                         const noise_weather_config *c, uint32_t seed);
/* A changed intensity or vary setting restarts from it; otherwise the current
   intensity and target stay, held within the new bounds while varying. */
void noise_weather_configure(noise_weather *weather, noise_state *state,
                              const noise_weather_config *previous,
                              const noise_weather_config *c);
void noise_weather_next(noise_weather *weather, const noise_weather_config *c,
                         noise_state *state);
float noise_weather_mod_linear(float base, float amount, float intensity,
                                float low, float high);
float noise_weather_mod_log(float base, float amount, float intensity,
                             float low, float high);
float noise_weather_arrival_level(float intensity, float amount);

#ifdef __cplusplus
}
#endif

#endif
