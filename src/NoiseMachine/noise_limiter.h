#ifndef NOISE_LIMITER_H
#define NOISE_LIMITER_H

#include <stdint.h>

#define NOISE_LIMITER_FRAMES 88u /* 2 ms lookahead; output is delayed by this much. */
#define NOISE_LIMITER_CEILING 0.891250938f /* -1 dBFS. */

/* Lookahead peak limiter with one gain for both channels. */
typedef struct noise_limiter {
  float delay[2][NOISE_LIMITER_FRAMES];
  uint32_t window_min[NOISE_LIMITER_FRAMES]; /* In 2^-24 steps, so their sum never drifts. */
  uint32_t window_min_sum;
  float queue_gain[NOISE_LIMITER_FRAMES + 1]; /* Sliding minimum of the needed gain. */
  uint32_t queue_frame[NOISE_LIMITER_FRAMES + 1];
  unsigned queue_head;
  unsigned queue_count;
  uint32_t frame;
  unsigned position;
  float gain;
} noise_limiter;

#ifdef __cplusplus
extern "C" {
#endif

void noise_limiter_init(noise_limiter *limiter);
/* Replaces the frame with the one NOISE_LIMITER_FRAMES earlier, scaled so neither
   channel exceeds NOISE_LIMITER_CEILING. Returns 1 when it reduced the gain. */
int noise_limiter_next(noise_limiter *limiter, float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
