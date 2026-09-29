#include "test_support.h"

#include <assert.h>
#include <math.h>

static void test_hum(void) {
  for (unsigned kind = NOISE_KIND_HUM_50HZ; kind <= NOISE_KIND_HUM_60HZ; ++kind) {
    noise_config c = silent_config();
    c.ambient_gain[kind] = 0.5f;
    assert(noise_init(&a, &c, 1) == NOISE_OK);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    double frequency = kind == NOISE_KIND_HUM_50HZ ? 50.0 : 60.0;
    double fundamental = spectral_amplitude(frequency);
    assert(fabs(fundamental - 32767.0 * 0.5 / 1.42) < 2.0);
    assert(fabs(spectral_amplitude(2.0 * frequency) / fundamental - 0.3) < 0.001);
    assert(fabs(spectral_amplitude(3.0 * frequency) / fundamental - 0.12) < 0.001);
    assert(spectral_amplitude(frequency + 1.0) < 1.0);
    for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) assert(audio[2*n] == audio[2*n+1]);
  }
}

static void test_noise_spectra(void) {
  for (unsigned kind = NOISE_KIND_WHITE; kind <= NOISE_KIND_PINK; ++kind) {
    noise_config c = silent_config();
    c.ambient_gain[kind] = 0.5f;
    assert(noise_init(&a, &c, 11) == NOISE_OK);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    double low = 0.0, high = 0.0, mean = 0.0;
    for (unsigned second = 0; second < 8; ++second) {
      noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
      low += band_power(400);
      high += band_power(6400);
      for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) mean += audio[2 * n];
    }
    double slope = log(high / low) / log(16.0);
    double expected = kind == NOISE_KIND_WHITE ? 0.0 : -1.0;
    assert(fabs(slope - expected) < 0.15);
    assert(fabs(mean / (8.0 * NOISE_SAMPLE_RATE_HZ * 32768.0)) < 0.01);
    assert(a.state.clipped_samples == 0);
  }
}

void run_ambient_tests(void) {
  test_hum();
  test_noise_spectra();
}
