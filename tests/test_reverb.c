#include "test_support.h"

#include <assert.h>
#include <math.h>

static double reverb_energy(const noise_gen *gen) {
  double energy = 0.0;
  for (unsigned i = 0; i < NOISE_REVERB_SAMPLES; ++i) {
    energy += (double)gen->reverb.buffer[i] * gen->reverb.buffer[i];
  }
  return energy;
}

static void test_reverb_decay(void) {
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  c.reverb_gain = 1.0f;
  c.listener.stereo_width_m = 0.0f;
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 4);
  double early = reverb_energy(&a);
  unsigned stereo_differences = 0;
  for (unsigned i = 0; i < NOISE_SAMPLE_RATE_HZ / 4; ++i) {
    stereo_differences += audio[2 * i] != audio[2 * i + 1];
  }
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 4);
  double middle = reverb_energy(&a);
  for (unsigned i = 0; i < 8; ++i) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 4);
  double late = reverb_energy(&a);
  assert(early > 0.0);
  assert(middle < early);
  assert(late < middle * 1e-6);
  assert(stereo_differences > 100);
}

void run_reverb_tests(void) {
  test_reverb_decay();
}
