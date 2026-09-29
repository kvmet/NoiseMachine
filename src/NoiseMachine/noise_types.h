#ifndef NOISE_TYPES_H
#define NOISE_TYPES_H

#include <stdint.h>

#define NOISE_SAMPLE_RATE_HZ 44100u
#define NOISE_CHANNELS 2u

typedef enum noise_result {
  NOISE_OK = 0,
  NOISE_INVALID_CONFIG,
  NOISE_INVALID_DROP,
  NOISE_VOICE_LIMIT,
  NOISE_INVALID_STRIKE
} noise_result;

typedef struct position_polar {
  float distance_m;
  float angle_rad; /* 0 front, pi/2 right, pi behind, 3pi/2 left. */
} position_polar;

typedef struct noise_state {
  float rain_intensity;
  float rain_target;
  unsigned weather_state;
  unsigned active_drops;
  unsigned peak_active_drops;
  uint64_t generated_drops;
  uint64_t dropped_drops;
  uint64_t clipped_samples;
  uint64_t generated_thunder;
  uint64_t dropped_thunder;
} noise_state;

#endif
