#ifndef NOISE_RAIN_H
#define NOISE_RAIN_H

#include "noise_dsp.h"
#include "noise_spatial.h"
#include "noise_types.h"

#define NOISE_MAX_DROPLETS 128u
#define NOISE_MAX_SURFACES 9u
#define NOISE_SURFACE_NAME_SIZE 16u
#define NOISE_RAIN_SIZE_BINS 50u /* 0.1 mm diameter bins from 0.8 to 5.8 mm. */
#define NOISE_BED_BANDS 15u
#define NOISE_BED_STAGES 2u /* Cascaded band-passes per band, for steep skirts. */

/* Positions in the surface list from noise_rain_config_default. */
typedef enum default_surface {
  WATER = 0,
  DIRT,
  LEAF,
  CONCRETE,
  GLASS,
  METAL,
  PLASTIC,
  ASPHALT,
  ASPHALT_ROOF
} default_surface;

typedef struct droplet {
  unsigned surface; /* Index below noise_rain_config.surface_count. */
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
  char name[NOISE_SURFACE_NAME_SIZE]; /* NUL-terminated. */
  float coverage; /* Relative share of the ground's area. */
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
  float max_drops_per_s; /* Drops played one by one; the bed plays the rest. */
  float bed_gain; /* Bed relative to the drops it stands in for; 1 matches them, 0..4. */
  float min_distance_m;
  float max_distance_m;
  unsigned surface_count; /* 1..NOISE_MAX_SURFACES. */
  noise_surface surface[NOISE_MAX_SURFACES]; /* Used coverage sums above zero; later entries are ignored. */
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

/* Noise matched band by band to the played drops, standing in for the rain too far
   out to play one drop at a time. */
typedef struct noise_rain_bed {
  uint32_t rng;
  float ratio; /* Power of the rain beyond near_m over the played rain's power. */
  noise_biquad analysis[NOISE_BED_BANDS][NOISE_BED_STAGES];
  noise_biquad synthesis[NOISE_CHANNELS][NOISE_BED_BANDS][NOISE_BED_STAGES];
  float unit_power[NOISE_BED_BANDS]; /* Band power of unit-variance white noise. */
  float plateau; /* Summed power gain of all bands across the middle of their span. */
  float power[NOISE_BED_BANDS]; /* Played rain per band, smoothed. */
  float gain[NOISE_BED_BANDS];
  float gain_scale[NOISE_BED_BANDS]; /* Squared gain per unit of measured power. */
} noise_rain_bed;

typedef struct noise_rain {
  uint32_t arrival_rng;
  uint32_t drop_rng;
  float surface_cdf[NOISE_MAX_SURFACES];
  float rain_mm_h; /* Rate size_cdf was built for; negative before the first build. */
  float flux_per_m2_s; /* Drops from 0.8 to 5.8 mm landing on a square metre. */
  float size_cdf[NOISE_RAIN_SIZE_BINS];
  float arrivals_per_s; /* Drops landing between the distance bounds. */
  float arrival_probability; /* Played drops per frame. */
  float near_m; /* Played drops land between min_distance_m and this. */
  noise_bus bus; /* Played drops before rain.gain, where the bed measures them. */
  noise_rain_bed bed;
  noise_drop_voice voice[NOISE_MAX_DROPLETS];
} noise_rain;

#ifdef __cplusplus
extern "C" {
#endif

int noise_rain_config_valid(const noise_rain_config *c);
void noise_rain_config_default(noise_rain_config *c);
void noise_rain_init(noise_rain *rain, uint32_t seed);
void noise_rain_configure(noise_rain *rain, const noise_rain_config *c);
/* Sets arrival rate, drop sizes, and the bed's level from the weather and configuration. */
void noise_rain_follow(noise_rain *rain, const noise_rain_config *c, const noise_weather *weather);
/* Matches the bed to the rain measured since the last call; call once per weather update. */
void noise_rain_update_bed(noise_rain *rain);
int noise_drop_valid(const noise_rain_config *c, const droplet *drop);
noise_result noise_rain_start_drop(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                               const noise_listener_config *listener, const droplet *drop);
/* Returns the reverb send; the direct sound goes to the bus. */
float noise_rain_next(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_listener_config *listener, noise_bus *bus);

#ifdef __cplusplus
}
#endif

#endif
