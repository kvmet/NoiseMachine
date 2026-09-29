#ifndef NOISE_RAIN_H
#define NOISE_RAIN_H

#include "noise_dsp.h"
#include "noise_spatial.h"
#include "noise_types.h"
#include "noise_weather.h"

#define NOISE_MAX_DROPLETS 128u

/* Parameter sets for noise_surface_preset. The default config holds preset i in slot i. */
typedef enum surface_preset {
  WATER = 0,
  DIRT,
  LEAF,
  CONCRETE,
  GLASS,
  METAL,
  PLASTIC,
  ASPHALT,
  ASPHALT_ROOF,
  NOISE_SURFACE_PRESET_COUNT
} surface_preset;

typedef struct droplet {
  unsigned surface; /* Slot in noise_rain_config.surface. */
  float radius_m;
  float velocity_m_s;
  float bubble_radius_m; /* Zero disables the bubble. */
  position_polar position;
} droplet;

/* Damped sinusoid excited by each drop. */
typedef struct noise_surface_mode {
  float frequency_hz;
  float damping_per_s;
  float gain; /* Relative to the drop amplitude; zero disables the mode. */
} noise_surface_mode;

/* Everything that shapes the sound of one drop on a surface. Each min/max pair is
   sampled per drop; equal bounds use the value without a random draw. */
typedef struct noise_surface {
  float weight; /* Relative share of automatic arrivals. */
  float click_gain_min; /* Relative to the drop amplitude. */
  float click_gain_max;
  float click_frequency_min_hz;
  float click_frequency_max_hz;
  float click_damping_ratio; /* Click damping per second is this times its frequency. */
  noise_surface_mode mode[2];
  float detune; /* Both mode frequencies scale by one factor from 1 - detune to 1 + detune. */
  float lowpass_hz; /* Two cascaded one-pole stages on the drop; zero bypasses them. */
  float bubble_probability; /* Chance that an automatic arrival has a bubble. */
  float bubble_radius_min_m;
  float bubble_radius_max_m;
  float bubble_gain_min; /* Relative to the scaled click amplitude. */
  float bubble_gain_max;
  float bubble_decay_min; /* Divides the physical bubble damping. */
  float bubble_decay_max;
  float bubble_delay_s; /* Bubble onset after the click. */
} noise_surface;

typedef struct noise_rain_config {
  float gain;
  float max_drops_per_s;
  float min_distance_m;
  float max_distance_m;
  float fall_height_m;
  noise_surface surface[NOISE_SURFACE_SLOTS]; /* At least one weight above zero. */
} noise_rain_config;

/* Mode slots in each drop voice. */
enum {
  NOISE_DROP_CLICK = 0,
  NOISE_DROP_RESONANCE = 1, /* Two slots, one per noise_surface_mode. */
  NOISE_DROP_BUBBLE = 3,
  NOISE_DROP_MODES = 4
};

typedef struct noise_drop_voice {
  noise_mode mode[NOISE_DROP_MODES];
  noise_spatial spatial;
  float material_lowpass_alpha;
  float material_lowpass_state[2];
  unsigned filter_tail;
} noise_drop_voice;

typedef struct noise_rain {
  uint32_t arrival_rng;
  uint32_t drop_rng;
  float surface_cdf[NOISE_SURFACE_SLOTS];
  noise_drop_voice voice[NOISE_MAX_DROPLETS];
} noise_rain;

#ifdef __cplusplus
extern "C" {
#endif

/* Fills every field except weight. */
void noise_surface_preset(noise_surface *surface, surface_preset preset);
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
