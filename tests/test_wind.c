#include "test_support.h"

#include <assert.h>
#include <math.h>

static void test_wind(void) {
  noise_config c = silent_config();
  c.wind.gain = 0.5f;
  c.wind.gust_depth = 0.0f;
  c.wind.brightness = 0.2f;
  c.wind.stereo_width = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int nonzero = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    nonzero |= audio[2 * n] != 0;
    assert(audio[2 * n] == audio[2 * n + 1]);
  }
  assert(nonzero);
  double dark_high = band_power(6400);

  c.wind.brightness = 1.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(band_power(6400) > 20.0 * dark_high);

  c.wind.stereo_width = 1.0f;
  c.wind.gust_depth = 1.0f;
  c.wind.gust_rate_hz = 2.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int stereo = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    stereo |= audio[2 * n] != audio[2 * n + 1];
  }
  assert(stereo);
  assert(a.wind.gust != 0.5f);
  assert(a.state.clipped_samples == 0);
}

void run_wind_tests(void) {
  test_wind();
}
