#include "noise_limiter.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

#define LIMITER_QUEUE (NOISE_LIMITER_FRAMES + 1u)
#define LIMITER_UNIT 16777216.0f
#define LIMITER_RELEASE (1.0f / (0.1f * NOISE_SAMPLE_RATE_HZ)) /* 100 ms recovery. */
/* Lets the release land exactly on the target instead of approaching it forever. */
#define LIMITER_RELEASE_FLOOR 1e-6f

void noise_limiter_init(noise_limiter *limiter) {
  memset(limiter, 0, sizeof(*limiter));
  for (unsigned i = 0; i < NOISE_LIMITER_FRAMES; ++i) limiter->window_min[i] = (uint32_t)LIMITER_UNIT;
  limiter->window_min_sum = NOISE_LIMITER_FRAMES * (uint32_t)LIMITER_UNIT;
  limiter->gain = 1.0f;
}

/* A peak entering now leaves the delay NOISE_LIMITER_FRAMES later. Every minimum in the
   average at that moment spans the peak's frame, so the gain has reached its need. */
int noise_limiter_next(noise_limiter *limiter, float *left, float *right) {
  float peak = fmaxf(fabsf(*left), fabsf(*right));
  float needed = peak > NOISE_LIMITER_CEILING ? NOISE_LIMITER_CEILING / peak : 1.0f;

  while (limiter->queue_count) {
    unsigned last = (limiter->queue_head + limiter->queue_count - 1u) % LIMITER_QUEUE;
    if (limiter->queue_gain[last] < needed) break;
    --limiter->queue_count;
  }
  unsigned tail = (limiter->queue_head + limiter->queue_count) % LIMITER_QUEUE;
  limiter->queue_gain[tail] = needed;
  limiter->queue_frame[tail] = limiter->frame;
  ++limiter->queue_count;
  while (limiter->frame - limiter->queue_frame[limiter->queue_head] > NOISE_LIMITER_FRAMES) {
    limiter->queue_head = (limiter->queue_head + 1u) % LIMITER_QUEUE;
    --limiter->queue_count;
  }

  unsigned position = limiter->position;
  uint32_t window_min = (uint32_t)(limiter->queue_gain[limiter->queue_head] * LIMITER_UNIT);
  limiter->window_min_sum += window_min - limiter->window_min[position];
  limiter->window_min[position] = window_min;
  float target = (float)limiter->window_min_sum / (NOISE_LIMITER_FRAMES * LIMITER_UNIT);
  float gain = limiter->gain;
  gain = target < gain ? target :
      fminf(target, gain + LIMITER_RELEASE * (target - gain) + LIMITER_RELEASE_FLOOR);
  limiter->gain = gain;

  float delayed[2] = {limiter->delay[0][position], limiter->delay[1][position]};
  limiter->delay[0][position] = *left;
  limiter->delay[1][position] = *right;
  limiter->position = position + 1u == NOISE_LIMITER_FRAMES ? 0u : position + 1u;
  ++limiter->frame;
  *left = gain * delayed[0];
  *right = gain * delayed[1];
  return gain < 1.0f;
}
