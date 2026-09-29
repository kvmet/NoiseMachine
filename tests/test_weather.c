#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static void test_weather_and_arrivals(void) {
  noise_config c = silent_config();
  c.weather.intensity = 0.5f;
  c.rain.max_drops_per_s = 800.0f;
  assert(noise_init(&a, &c, 47) == NOISE_OK);
  double sum = 0, sum_squared = 0;
  for (unsigned second = 0; second < 60; ++second) {
    uint64_t before = a.state.generated_drops;
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    double count = (double)(a.state.generated_drops - before);
    sum += count;
    sum_squared += count * count;
  }
  double mean = sum / 60.0;
  double variance = sum_squared / 60.0 - mean * mean;
  assert(fabs(mean - 400.0) < 12.0);
  assert(variance > 150.0 && variance < 700.0);
  assert(a.state.dropped_drops == 0);
  c.weather.vary = 1;
  c.weather.step_s = 0.1f;
  c.weather.slew_s = 0.01f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  c.ambient_gain[NOISE_KIND_WHITE] = 0.2f;
  assert(noise_init(&b, &c, 5) == NOISE_OK);
  int16_t other[2];
  unsigned visited = 0;
  unsigned previous_state = a.state.weather_state;
  for (unsigned n = 0; n < 20 * NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&a, audio, 1);
    noise_fill(&b, other, 1);
    assert(a.state.rain_intensity >= c.weather.min_intensity);
    assert(a.state.rain_intensity <= c.weather.max_intensity);
    assert(a.state.rain_intensity == b.state.rain_intensity);
    assert(a.state.generated_drops == b.state.generated_drops);
    assert(abs((int)a.state.weather_state - (int)previous_state) <= 1);
    previous_state = a.state.weather_state;
    visited |= 1u << a.state.weather_state;
  }
  assert(visited == 7);
}

static void test_slow_weather_slew(void) {
  noise_config c = silent_config();
  c.weather.vary = 1;
  c.weather.min_intensity = 0.0f;
  c.weather.max_intensity = 1.0f;
  c.weather.slew_s = 60.0f;
  c.weather.step_s = 3600.0f;
  c.rain.max_drops_per_s = 0.0f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  /* Hold the internal control target fixed for an analytic step-response check. */
  a.state.rain_target = 1.0f;
  for (unsigned second = 0; second < 300; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(fabs(a.state.rain_intensity - (1.0 - exp(-5.0))) < 1e-6);
}

static void test_weather_modulation(void) {
  noise_config c = silent_config();
  c.weather.intensity = 0.0f;
  c.rain.max_drops_per_s = 100.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops == 0);

  c.weather.mod_amount[WEATHER_MOD_ARRIVAL_RATE] = 0.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops > 50);

  c.weather.intensity = 1.0f;
  c.weather.mod_amount[WEATHER_MOD_ARRIVAL_RATE] = -1.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops == 0);

  c.rain.max_drops_per_s = 0.0f;
  c.weather.intensity = 0.0f;
  c.rain.gain = 0.5f;
  c.weather.mod_amount[WEATHER_MOD_RAIN_GAIN] = 1.0f;
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, 1024);
  for (unsigned i = 0; i < 2048; ++i) assert(audio[i] == 0);

  c.weather.intensity = 1.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, 1024);
  unsigned nonzero = 0;
  for (unsigned i = 0; i < 2048; ++i) nonzero += audio[i] != 0;
  assert(nonzero > 0);
}

void run_weather_tests(void) {
  test_weather_and_arrivals();
  test_slow_weather_slew();
  test_weather_modulation();
}
