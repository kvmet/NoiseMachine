#ifndef NOISE_THUNDER_H
#define NOISE_THUNDER_H

#include "noise_dsp.h"
#include "noise_reverb.h"
#include "noise_types.h"

#define NOISE_THUNDER_VOICES 2u
#define NOISE_THUNDER_SEGMENTS 256u
#define NOISE_THUNDER_BANDS 4u /* Three span the channel's range; one holds echoes. */
#define NOISE_THUNDER_ECHOES 6u
#define NOISE_THUNDER_REVERB_SAMPLES 6132u

typedef struct thunder_strike {
  position_polar position; /* Distance 200..15000 m. */
} thunder_strike;

typedef struct noise_thunder_config {
  float gain;
  float rate_per_min; /* Automatic strikes; zero allows only manual strikes. */
  float min_distance_m;
  float max_distance_m;
  float reverb_gain;
  float reverb_decay_s; /* Time to fall 60 dB. */
} noise_thunder_config;

typedef struct noise_thunder_segment {
  float start; /* Frames after the strike's first arrival. */
  float width; /* Arrival spread between the segment's ends, frames. */
  float gain[2]; /* Per channel, divided by width. */
  float roughness; /* Fine-tortuosity noise per sqrt(frame), relative to gain. */
  float band; /* Direct range position, 0 to 2; fractions blend neighbours. */
} noise_thunder_segment;

typedef struct noise_reflector {
  float position[2]; /* Ground point, x right and y front, metres. */
  float reflectivity; /* Pressure ratio. */
  float smear_s; /* Arrival spread added by terrain roughness. */
} noise_reflector;

typedef struct noise_thunder_echo {
  float delay; /* Frames after the direct arrival. */
  float smear; /* Frames added to each segment's arrival spread. */
  float range_log; /* Log of echo path over direct path. */
  float gain[2];
  unsigned first;
  unsigned next;
} noise_thunder_echo;

typedef struct noise_thunder_voice {
  noise_thunder_segment segment[NOISE_THUNDER_SEGMENTS]; /* Sorted by start. */
  unsigned segments;
  unsigned first; /* Segments before this index have ended. */
  unsigned next; /* Segments from this index have not arrived. */
  uint32_t elapsed;
  uint32_t length; /* Zero marks a free voice. */
  /* Per range band: band-pass at 1/period shapes excitation into N-waves, then a
     fourth-order Butterworth air low-pass per channel. */
  noise_biquad pulse[NOISE_THUNDER_BANDS][2];
  noise_biquad air[NOISE_THUNDER_BANDS][2][2];
  noise_thunder_echo echo[NOISE_THUNDER_ECHOES];
  float span_log; /* Log of farthest over nearest direct segment range. */
  float echo_span_log; /* Log of the widest echo path ratio; band 3 sits there. */
} noise_thunder_voice;

/* Runs at a quarter of the sample rate and interpolates its output. */
typedef struct noise_thunder_reverb {
  float buffer[NOISE_THUNDER_REVERB_SAMPLES];
  noise_fdn fdn;
  float input;
  unsigned phase;
  float output[2][2]; /* Previous and current quarter-rate outputs. */
} noise_thunder_reverb;

typedef struct noise_thunder {
  uint32_t rng;
  uint32_t echo_rng;
  unsigned started;
  noise_thunder_voice voice[NOISE_THUNDER_VOICES];
  noise_reflector reflector[NOISE_THUNDER_ECHOES]; /* Fixed per seed: every strike echoes off the same terrain. */
  noise_thunder_reverb reverb;
} noise_thunder;

#ifdef __cplusplus
extern "C" {
#endif

int noise_thunder_config_valid(const noise_thunder_config *c);
void noise_thunder_config_default(noise_thunder_config *c);
void noise_thunder_init(noise_thunder *thunder, uint32_t seed);
void noise_thunder_configure(noise_thunder *thunder, const noise_thunder_config *c);
noise_result noise_thunder_start(noise_thunder *thunder, noise_state *state,
                                  position_polar position);
void noise_thunder_next(noise_thunder *thunder, const noise_thunder_config *c,
                         noise_state *state, float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
