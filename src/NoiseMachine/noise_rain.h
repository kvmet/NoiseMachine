#ifndef NOISE_RAIN_H
#define NOISE_RAIN_H

#include "noise_dsp.h"
#include "noise_spatial.h"
#include "noise_types.h"
#include "noise_weather.h"

#define NOISE_MAX_DROPLETS 128u

typedef enum impact_surface {
  WATER = 0,
  DIRT,
  LEAF,
  CONCRETE,
  GLASS,
  METAL,
  PLASTIC,
  ASPHALT,
  ASPHALT_ROOF,
  NOISE_SURFACE_COUNT
} impact_surface;

typedef struct droplet {
  impact_surface surface;
  float radius_m;
  float velocity_m_s;
  float bubble_radius_m; /* Zero disables the bubble; nonzero requires WATER. */
  position_polar position;
} droplet;

typedef struct noise_water_config {
  float impact_gain_min;
  float impact_gain_max;
  float bubble_probability;
  float bubble_radius_min_m;
  float bubble_radius_max_m;
  float bubble_gain_min;
  float bubble_gain_max;
  float bubble_decay_min;
  float bubble_decay_max;
} noise_water_config;

typedef struct noise_rain_config {
  float gain;
  float max_drops_per_s;
  float surface_weight[NOISE_SURFACE_COUNT]; /* Nonnegative relative weights. */
  float min_distance_m;
  float max_distance_m;
  float fall_height_m;
  noise_water_config water;
} noise_rain_config;

typedef struct noise_drop_voice {
  noise_mode mode[3];
  noise_spatial spatial;
  float material_lowpass_alpha;
  float material_lowpass_state[2];
  unsigned filter_tail;
} noise_drop_voice;

typedef struct noise_rain {
  uint32_t arrival_rng;
  uint32_t drop_rng;
  float surface_cdf[NOISE_SURFACE_COUNT];
  noise_drop_voice voice[NOISE_MAX_DROPLETS];
} noise_rain;

#ifdef __cplusplus
extern "C" {
#endif

int noise_rain_config_valid(const noise_rain_config *c);
void noise_rain_config_default(noise_rain_config *c);
void noise_rain_init(noise_rain *rain, uint32_t seed);
void noise_rain_configure(noise_rain *rain, const noise_rain_config *c);
int noise_drop_valid(const droplet *drop);
noise_result noise_rain_start_drop(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                               const noise_listener_config *listener, const droplet *drop);
/* Returns the reverb send; the direct sound goes to the bus. */
float noise_rain_next(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_weather_config *weather,
                       const noise_listener_config *listener, noise_bus *bus);

#ifdef __cplusplus
}
#endif

#endif
