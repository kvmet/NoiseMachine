#ifndef NOISE_AMBIENT_H
#define NOISE_AMBIENT_H

#include "noise_types.h"

#define NOISE_HUM_TABLE_SAMPLES 882u /* One 50 Hz period. */

typedef enum noise_kind {
  NOISE_KIND_WHITE = 0,
  NOISE_KIND_PINK,
  NOISE_KIND_HUM_50HZ,
  NOISE_KIND_HUM_60HZ,
  NOISE_KIND_COUNT
} noise_kind;

typedef struct noise_ambient {
  uint32_t rng;
  float pink[7];
  uint32_t hum_sample;
  float hum_table[NOISE_HUM_TABLE_SAMPLES];
} noise_ambient;

#ifdef __cplusplus
extern "C" {
#endif

void noise_ambient_init(noise_ambient *ambient, uint32_t seed);
float noise_ambient_next(noise_ambient *ambient, const float gain[NOISE_KIND_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
