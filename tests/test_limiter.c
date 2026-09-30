#include "test_support.h"

#include <assert.h>
#include <math.h>

static noise_limiter limiter;

static float next_unit(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return (float)(*state >> 8) / 8388608.0f - 1.0f;
}

/* Under the ceiling the output is the input delayed by the lookahead, unchanged. */
static void test_transparent(void) {
  noise_limiter_init(&limiter);
  static float input[2][4096];
  uint32_t rng = 1;
  for (unsigned n = 0; n < 4096; ++n) {
    input[0][n] = NOISE_LIMITER_CEILING * next_unit(&rng);
    input[1][n] = NOISE_LIMITER_CEILING * next_unit(&rng);
    float left = input[0][n], right = input[1][n];
    assert(!noise_limiter_next(&limiter, &left, &right));
    float expected[2] = {0.0f, 0.0f};
    if (n >= NOISE_LIMITER_FRAMES) {
      expected[0] = input[0][n - NOISE_LIMITER_FRAMES];
      expected[1] = input[1][n - NOISE_LIMITER_FRAMES];
    }
    assert(left == expected[0] && right == expected[1]);
  }
}

/* Lone spikes, steps, and loud noise never pass the ceiling, then the gain returns
   exactly to 1; a 20x overload takes about 0.65 s to recover. */
static void test_ceiling_and_recovery(void) {
  noise_limiter_init(&limiter);
  uint32_t rng = 7;
  unsigned limited = 0;
  for (unsigned n = 0; n < 5u * NOISE_SAMPLE_RATE_HZ; ++n) {
    float left = 0.0f, right = 0.0f;
    unsigned phase = n % NOISE_SAMPLE_RATE_HZ;
    if (phase == 1000u) left = 50.0f;
    if (phase >= 3000u && phase < 3400u) right = -3.0f;
    if (phase >= 5000u && phase < 6000u) {
      left = 20.0f * next_unit(&rng);
      right = 0.5f * next_unit(&rng);
    }
    limited += (unsigned)noise_limiter_next(&limiter, &left, &right);
    assert(fabsf(left) <= NOISE_LIMITER_CEILING && fabsf(right) <= NOISE_LIMITER_CEILING);
    if (phase > 40000u) assert(limiter.gain == 1.0f);
  }
  assert(limited > 0);
}

void run_limiter_tests(void) {
  test_transparent();
  test_ceiling_and_recovery();
}
