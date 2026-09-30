#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define REFERENCE_LUFS -24.0
#define THUNDER_REFERENCE_LUFS -18.0 /* Loudest 400 ms; thunder stands out over the beds. */

/* Every layer silent, default reverb, fixed calm weather. */
static noise_config reference_config(void) {
  noise_config c;
  noise_config_default(&c);
  memset(c.ambient_gain, 0, sizeof(c.ambient_gain));
  c.rain.gain = 0.0f;
  c.master_gain = 1.0f;
  c.storm.manual = 1;
  c.storm.fixed.rain_mm_h = 0.0f;
  c.storm.fixed.wind_m_s = 2.0f;
  c.storm.fixed.temperature_c = 20.0f;
  c.storm.fixed.lightning_per_min = 0.0f;
  return c;
}

#define SEEDS 8u

/* Seeds fix where insects sit and how strikes branch, so levels are power means over
   seeds. strike_m > 0 starts one strike there and reads its loudest 400 ms. */
static double measure(const noise_config *c, double seconds, float strike_m) {
  double power = 0.0;
  for (unsigned seed = 1; seed <= SEEDS; ++seed) {
    assert(noise_init(&a, c, seed) == NOISE_OK);
    if (strike_m > 0.0f) {
      thunder_strike strike = {{strike_m, 0.0f}};
      assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    }
    loudness result = measure_loudness(&a, seconds);
    assert(a.state.limited_frames == 0);
    power += pow(10.0, (strike_m > 0.0f ? result.max_momentary : result.integrated) / 10.0);
  }
  return 10.0 * log10(power / SEEDS);
}

static void check(const char *layer, double measured, double target) {
  if (fabs(measured - target) > 1.0) {
    fprintf(stderr, "%s: %.1f LUFS, want %.1f\n", layer, measured, target);
    assert(0);
  }
}

/* Gain 1 of every layer at its reference condition has the same loudness, so the
   mixer's gains compare directly. */
static void test_reference_levels(void) {
  noise_config c = reference_config();
  c.rain.gain = 1.0f;
  c.storm.fixed.rain_mm_h = 10.0f;
  check("rain", measure(&c, 10.0, 0.0f), REFERENCE_LUFS);

  c = reference_config();
  c.wind.gain = 1.0f;
  c.storm.fixed.wind_m_s = 10.0f;
  check("wind", measure(&c, 10.0, 0.0f), REFERENCE_LUFS);

  c = reference_config();
  c.crickets.gain = 1.0f;
  c.storm.fixed.temperature_c = 25.0f;
  check("crickets", measure(&c, 60.0, 0.0f), REFERENCE_LUFS);

  c = reference_config();
  c.cicadas.gain = 1.0f;
  c.storm.fixed.temperature_c = 30.0f;
  check("cicadas", measure(&c, 60.0, 0.0f), REFERENCE_LUFS);

  static const char *kinds[NOISE_KIND_COUNT] = {"white", "pink", "hum50", "hum60"};
  for (unsigned kind = 0; kind < NOISE_KIND_COUNT; ++kind) {
    c = reference_config();
    c.ambient_gain[kind] = 1.0f;
    check(kinds[kind], measure(&c, 3.0, 0.0f), REFERENCE_LUFS);
  }

  c = reference_config();
  c.thunder.gain = 1.0f;
  check("thunder", measure(&c, 20.0, 2000.0f), THUNDER_REFERENCE_LUFS);
}

void run_mix_tests(void) {
  test_reference_levels();
}
