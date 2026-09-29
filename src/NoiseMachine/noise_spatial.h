#ifndef NOISE_SPATIAL_H
#define NOISE_SPATIAL_H

#include "noise_types.h"

#define NOISE_DIRECT_SAMPLES 128u

/* Crickets and cicadas share this placement; each voice keeps fixed offsets within it. */
typedef struct noise_placement {
  float stereo_width; /* Angular spread: 0 all in front, 1 all around. */
  float min_distance_m;
  float max_distance_m;
} noise_placement;

typedef struct noise_listener_config {
  float stereo_width_m; /* Ear spacing; sphere radius is half this width. */
  float head_amount; /* 0: spaced microphones, 1: spherical head. */
  float rear_amount; /* 0: bypass rear filter, 1: full rear filter. */
} noise_listener_config;

/* Distance, ear delay, head shadow, and rear filter for one point source. */
typedef struct noise_spatial {
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
} noise_spatial;

/* Direct-path mix that point sources write ahead into at their ear delays. */
typedef struct noise_bus {
  float direct[2][NOISE_DIRECT_SAMPLES];
  unsigned position;
} noise_bus;

#ifdef __cplusplus
extern "C" {
#endif

int noise_listener_config_valid(const noise_listener_config *c);
void noise_spatial_init(noise_spatial *voice, const noise_listener_config *listener,
                         position_polar position);
void noise_spatial_next(noise_spatial *voice, const noise_listener_config *listener,
                         noise_bus *bus, float source);
/* Reads and clears the current frame, then advances. */
void noise_bus_next(noise_bus *bus, float *left, float *right);
int noise_placement_valid(const noise_placement *p);
/* distance_offset is 0..1 across the area; angle_offset is -1..1 across the width. */
position_polar noise_placement_position(const noise_placement *p, float distance_offset,
                                         float angle_offset);

#ifdef __cplusplus
}
#endif

#endif
