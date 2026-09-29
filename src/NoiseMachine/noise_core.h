#ifndef NOISE_CORE_H
#define NOISE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOISE_SAMPLE_RATE_HZ 44100u
#define NOISE_CHANNELS 2u
#define NOISE_MAX_DROPLETS 128u
#define NOISE_REVERB_LINES 6u
#define NOISE_REVERB_SAMPLES 7304u
#define NOISE_DIRECT_SAMPLES 128u

typedef enum noise_kind {
  NOISE_KIND_WHITE = 0,
  NOISE_KIND_PINK,
  HUM_50HZ,
  HUM_60HZ,
  NOISE_KIND_COUNT
} noise_kind;

typedef enum impact_surface {
  WATER = 0,
  DIRT,
  LEAF,
  CONCRETE,
  GLASS,
  METAL,
  NOISE_SURFACE_COUNT
} impact_surface;

typedef enum noise_result {
  NOISE_OK = 0,
  NOISE_INVALID_CONFIG,
  NOISE_INVALID_DROP,
  NOISE_VOICE_LIMIT
} noise_result;

typedef struct position_polar {
  float distance_m;
  float angle_rad; /* 0 front, pi/2 right, pi behind, 3pi/2 left. */
} position_polar;

typedef struct droplet {
  impact_surface surface;
  float radius_m;
  float velocity_m_s;
  float bubble_radius_m; /* Zero disables the bubble; nonzero requires WATER. */
  position_polar position;
} droplet;

typedef struct noise_config {
  float ambient_gain[NOISE_KIND_COUNT]; /* Independent linear gains, each 0..1. */
  float master_gain;
  float rain_gain;
  float rain_intensity; /* Initial intensity, 0..1; zero means no arrivals. */
  float min_rain_intensity;
  float max_rain_intensity;
  int vary_rain;
  float weather_step_s;
  float rain_slew_s;
  float max_drops_per_s;
  float surface_weight[NOISE_SURFACE_COUNT]; /* Nonnegative relative weights. */
  float min_distance_m;
  float max_distance_m;
  float fall_height_m;
  float stereo_width_m; /* Ear spacing; sphere radius is half this width. */
  float head_amount; /* 0: spaced microphones, 1: spherical head. */
  float rear_amount; /* 0: bypass rear filter, 1: full rear filter. */
  float reverb_gain; /* Rain send is before distance attenuation. */
} noise_config;

typedef struct noise_mode {
  float previous;
  float current;
  float coefficient;
  float radius_squared;
  uint32_t remaining;
  uint32_t delay;
} noise_mode;

typedef struct noise_drop_voice {
  noise_mode mode[3];
  float ear_gain[2];
  unsigned ear_delay[2];
  float delay_weight[2][4];
  float head_b0[2];
  float head_b1[2];
  float head_feedback;
  float head_state[2];
  float head_previous_input;
  float lowpass_alpha;
  float lowpass_state;
  unsigned filter_tail;
} noise_drop_voice;

typedef struct noise_state {
  float rain_intensity;
  float rain_target;
  unsigned weather_state;
  unsigned active_drops;
  unsigned peak_active_drops;
  uint64_t generated_drops;
  uint64_t dropped_drops;
  uint64_t clipped_samples;
} noise_state;

/* Caller-owned storage. Treat all fields except the read-only state as private. */
typedef struct noise_gen {
  noise_config config;
  noise_state state;
  uint32_t ambient_rng;
  uint32_t arrival_rng;
  uint32_t drop_rng;
  uint32_t weather_rng;
  float pink_b[7];
  uint32_t hum_sample;
  float hum_table[882];
  noise_drop_voice voices[NOISE_MAX_DROPLETS];
  float surface_cdf[NOISE_SURFACE_COUNT];
  uint32_t weather_samples;
  uint32_t weather_period;
  float rain_slew;
  float rain_slew_error;
  float reverb[NOISE_REVERB_SAMPLES];
  unsigned reverb_position[NOISE_REVERB_LINES];
  float reverb_damping[NOISE_REVERB_LINES];
  float reverb_feedback[NOISE_REVERB_LINES];
  float direct[2][NOISE_DIRECT_SAMPLES];
  unsigned direct_position;
} noise_gen;

void noise_config_default(noise_config *config);
/* Rejects invalid values without modifying gen. Seed zero aliases seed one. */
noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed);
/* Starts an arrival at the listener. Call between fills on the audio thread. */
noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop);
/* Writes 2 * frames interleaved int16 samples (L, R); returns frames. */
size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
