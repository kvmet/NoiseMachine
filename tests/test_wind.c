#include "test_support.h"

#include <assert.h>
#include <math.h>

static double ear_energy(unsigned channel) {
  double energy = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    energy += (double)audio[2 * n + channel] * audio[2 * n + channel];
  }
  return energy;
}

static void test_wind(void) {
  noise_config c = silent_config();
  c.wind.gain = 0.5f;
  c.wind.stereo_width = 0.0f;
  c.storm.fixed.wind_m_s = 5.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int nonzero = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    nonzero |= audio[2 * n] != 0;
    assert(audio[2 * n] == audio[2 * n + 1]);
  }
  assert(nonzero);
  double calm_ratio = band_power(6400) / band_power(200);

  c.storm.fixed.wind_m_s = 30.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(band_power(6400) / band_power(200) > 20.0 * calm_ratio);
  assert(a.state.limited_frames == 0);

  /* Level is proportional to speed, relative to 10 m/s. */
  c.storm.fixed.wind_m_s = 10.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  assert(fabsf(a.wind.relative_level - a.state.weather.wind_m_s / 10.0f) < 1e-6f);
  assert(a.wind.target[1] == a.wind.target[0]);
  float level_per_m_s = a.wind.target[0] / a.state.weather.wind_m_s;
  c.storm.fixed.wind_m_s = 20.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  assert(fabsf(a.wind.target[0] / a.state.weather.wind_m_s / level_per_m_s - 1.0f) < 1e-5f);

  /* Wind from the right is louder in the right ear. */
  c.storm.fixed.wind_bearing_rad = 0.5f * (float)TEST_PI;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(ear_energy(1) > 2.0 * ear_energy(0));

  /* Balance zero keeps both ears level whatever the bearing. */
  c.wind.balance = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  assert(a.wind.target[0] == a.wind.target[1]);
  c.wind.balance = 0.5f;

  /* Brightness scales the cutoff the speed sets. */
  float alpha = a.wind.air_alpha;
  c.wind.brightness = 2.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  float cutoff = -logf(1.0f - alpha) * NOISE_SAMPLE_RATE_HZ / (2.0f * (float)TEST_PI);
  float doubled = -logf(1.0f - a.wind.air_alpha) * NOISE_SAMPLE_RATE_HZ / (2.0f * (float)TEST_PI);
  assert(fabsf(doubled / cutoff - 2.0f) < 1e-3f);
  c.wind.brightness = 1.0f;

  c.wind.stereo_width = 1.0f;
  c.storm.fixed.wind_bearing_rad = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int stereo = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    stereo |= audio[2 * n] != audio[2 * n + 1];
  }
  assert(stereo);
}

void run_wind_tests(void) {
  test_wind();
}
