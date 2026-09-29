#ifndef NOISE_CORE_H
#define NOISE_CORE_H

#include <stddef.h>
#include <stdint.h>

#define NOISE_SAMPLE_RATE_HZ 44100u
#define PI 3.141592653589793

#define MAX_DROPLETS 1024

// Physical constants
#define GRAVITY 9.81
#define THERMAL_DAMPING 1.6E6
#define SOUND_IN_AIR 343.0
#define SOUND_IN_WATER 1497.0
#define AIR_DENSITY 1.29
#define WATER_DENSITY 1000.0
#define SPECIFIC_HEAT 1.4
#define DEFAULT_ATMOSPHERIC_PRESSURE 101.325

typedef enum noise_kind {
  NOISE_KIND_WHITE = 0,
  NOISE_KIND_PINK = 1,
  HUM_50HZ = 2,
  HUM_60HZ = 3
} noise_kind;

typedef enum impact_surface {
  WATER = 0,
  DIRT = 1,
  LEAF = 2,
  CONCRETE = 3,
  GLASS = 4,
  METAL = 5
} impact_surface;

typedef enum impact_phase {
  IDLE = 0,
  HIT = 1,
  TAIL = 2
} impact_phase;

typedef struct position_polar {
  float distance;
  float angle;
} position_polar;

typedef struct droplet {
  uint32_t phase_sample;
  impact_phase phase;
  impact_surface surface;
  float radius;
  position_polar position;
  float velocity;
} droplet;



typedef struct noise_config {
  float min_rain_intensity = 0.0;
  float max_rain_intensity = 1.0;
  float max_windspeed = 100.0;
  float metal;
} noise_config;

typedef struct noise_state {
  float rain_intensity; // 0..1
  float wind_intensity;

} noise_state;

typedef struct noise_gen {
  noise_kind kind;
  uint32_t rng_state;
  float pink_b[7];
} noise_gen;

void noise_init(noise_gen *gen, noise_kind kind, uint32_t seed);

// Writes exactly count samples and returns count. On the MCU, call this once
// per audio callback with a small on-chip buffer.
size_t noise_fill(noise_gen *gen, int16_t *out, size_t count);

#endif
