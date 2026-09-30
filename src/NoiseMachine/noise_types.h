#ifndef NOISE_TYPES_H
#define NOISE_TYPES_H

#include <stdint.h>

#define NOISE_SAMPLE_RATE_HZ 44100u
#define NOISE_CHANNELS 2u
#define NOISE_CONTROL_FRAMES 441u /* Weather updates 100 times a second. */

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

/* Why an insect layer is silent; flags combine. */
enum {
  NOISE_QUIET_COLD = 1u,
  NOISE_QUIET_RAIN = 2u,
  NOISE_QUIET_WIND = 4u
};

/* Weather every sound module reads; the storm simulation updates it 100 times a second. */
typedef struct noise_weather {
  float rain_mm_h;
  float wind_mean_m_s;
  float wind_m_s; /* Mean plus the current gust; never negative. */
  float wind_bearing_rad; /* Direction the wind comes from: 0 front, pi/2 right. */
  float temperature_c;
  float lightning_per_min; /* Flashes of the nearest storm cell. */
  position_polar cell; /* Nearest storm core; distance zero when there is none. */
} noise_weather;

typedef struct noise_state {
  noise_weather weather;
  unsigned active_drops;
  unsigned peak_active_drops;
  uint64_t generated_drops;
  uint64_t dropped_drops;
  uint64_t clipped_samples;
  uint64_t generated_thunder;
  uint64_t dropped_thunder;
} noise_state;

#endif
