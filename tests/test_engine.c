#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void test_validation(void) {
  noise_config c = silent_config();
  assert(noise_config_valid(&c));
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  b = a;
  c.master_gain = NAN;
  assert(!noise_config_valid(&c));
  assert(!noise_config_valid(NULL));
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  assert(memcmp(&a, &b, sizeof(a)) == 0);
  c = silent_config();
  c.rain.surface[WATER].weight = -1.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  clear_surface_weights(&c);
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.weather.vary = 1;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c.weather.intensity = 0.5f;
  c.weather.min_intensity = 0.9f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.max_drops_per_s = INFINITY;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.weather.mod_amount[WEATHER_MOD_REVERB_GAIN] = 1.01f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c.weather.mod_amount[WEATHER_MOD_REVERB_GAIN] = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[WATER].click_gain_min = 0.6f;
  c.rain.surface[WATER].click_gain_max = 0.5f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[WATER].bubble_radius_min_m = 0.002f;
  c.rain.surface[WATER].bubble_radius_max_m = 0.001f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[WATER].bubble_decay_max = INFINITY;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[METAL].mode[1].damping_per_s = 0.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[GLASS].lowpass_hz = 10.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.surface[DIRT].click_frequency_max_hz = 900.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.wind.gust_rate_hz = 0.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.wind.stereo_width = 1.01f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.crickets.call_rate_hz = 0.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.crickets.pitch_hz = 8001.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.cicadas.click_rate_scale = 1.6f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.cicadas.species = NOISE_CICADA_SPECIES_COUNT;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.cicadas.placement.min_distance_m = 40.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.cicadas.chorus = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0001f;
  assert(noise_trigger_drop(&a, &drop) == NOISE_INVALID_DROP);
  drop = water_drop();
  drop.surface = NOISE_SURFACE_SLOTS;
  assert(noise_trigger_drop(&a, &drop) == NOISE_INVALID_DROP);
  drop = water_drop();
  drop.radius_m = NAN;
  assert(noise_trigger_drop(&a, &drop) == NOISE_INVALID_DROP);
  assert(memcmp(&a, &b, sizeof(a)) == 0);
}

static void test_silence_and_chunks(void) {
  noise_config c = silent_config();
  assert(noise_init(&a, &c, 0) == NOISE_OK);
  b = a;
  assert(noise_fill(&a, NULL, 0) == 0);
  assert(memcmp(&a, &b, sizeof(a)) == 0);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
  c.weather.intensity = 0.7f;
  c.weather.vary = 1;
  c.weather.step_s = 0.1f;
  c.reverb_gain = 0.2f;
  c.ambient_gain[NOISE_KIND_PINK] = 0.2f;
  c.ambient_gain[NOISE_KIND_HUM_60HZ] = 0.1f;
  c.wind.gain = 0.1f;
  c.crickets.gain = 0.05f;
  c.cicadas.gain = 0.05f;
  c.thunder.gain = 0.3f;
  assert(noise_init(&a, &c, 0) == NOISE_OK);
  assert(noise_init(&b, &c, 1) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  unsigned done = 0;
  int16_t block[2 * 257];
  while (done < NOISE_SAMPLE_RATE_HZ) {
    unsigned count = 1 + done % 257;
    if (count > NOISE_SAMPLE_RATE_HZ - done) count = NOISE_SAMPLE_RATE_HZ - done;
    noise_fill(&b, block, count);
    assert(memcmp(block, audio + 2 * done, count * 2 * sizeof(int16_t)) == 0);
    done += count;
  }
  assert(memcmp(&a, &b, sizeof(a)) == 0);
  assert(a.state.generated_drops > 100);
  assert(a.state.generated_thunder == 1);
  assert(a.state.dropped_drops == 0);
  assert(a.state.clipped_samples == 0);
  assert(noise_init(&b, &c, 2) == NOISE_OK);
  noise_fill(&b, block, 257);
  assert(memcmp(block, audio, sizeof(block)) != 0);
}

static void test_random_stream_separation(void) {
  noise_config c = silent_config();
  c.weather.intensity = 1.0f;
  c.ambient_gain[NOISE_KIND_WHITE] = 0.1f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  unsigned adjacent_matches = 0;
  for (unsigned i = 0; i < 16; ++i) {
    uint32_t arrival = a.rain.arrival_rng;
    noise_fill(&a, audio, 1);
    adjacent_matches += a.ambient.rng == arrival;
  }
  assert(adjacent_matches < 2);
}

static void test_output_saturation(void) {
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  assert(noise_init(&a, &c, 13) == NOISE_OK);
  droplet drop = water_drop();
  drop.surface = METAL;
  drop.bubble_radius_m = 0.0f;
  drop.radius_m = 0.0029f;
  drop.velocity_m_s = 12.0f;
  for (unsigned i = 0; i < NOISE_MAX_DROPLETS; ++i) assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.clipped_samples > 0);
  uint64_t saturated = 0;
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) {
    saturated += audio[i] == INT16_MAX || audio[i] == INT16_MIN;
  }
  assert(saturated == a.state.clipped_samples);
}

static void test_set_config(void) {
  noise_config c = silent_config();
  c.wind.gain = 0.3f;
  c.cicadas.gain = 0.3f;
  c.thunder.gain = 0.5f;
  c.thunder.reverb_gain = 0.5f;
  c.weather.vary = 1;
  c.weather.intensity = 0.5f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 2);

  /* Rejection leaves the engine untouched. */
  b = a;
  noise_config bad = c;
  bad.weather.min_intensity = 0.9f;
  assert(noise_set_config(&a, &bad) == NOISE_INVALID_CONFIG);
  assert(noise_set_config(&a, NULL) == NOISE_INVALID_CONFIG);
  assert(noise_set_config(NULL, &c) == NOISE_INVALID_CONFIG);
  assert(memcmp(&a, &b, sizeof(a)) == 0);

  /* Reapplying the current configuration changes nothing audible or internal. */
  assert(noise_set_config(&b, &c) == NOISE_OK);
  assert(memcmp(&a, &b, sizeof(a)) == 0);
  static int16_t other[2 * NOISE_SAMPLE_RATE_HZ];
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  noise_fill(&b, other, NOISE_SAMPLE_RATE_HZ);
  assert(memcmp(audio, other, sizeof(audio)) == 0);

  /* A live change matches a fresh engine for every configuration-derived coefficient. */
  noise_config live = c;
  live.wind.brightness = 0.9f;
  live.wind.gust_rate_hz = 1.5f;
  live.cicadas.pitch_hz = 8000.0f;
  live.cicadas.species = CICADA_HIGURASHI;
  live.thunder.reverb_decay_s = 9.0f;
  live.rain.surface[GLASS].weight = 5.0f;
  live.weather.step_s = 1.0f;
  live.weather.slew_s = 0.1f;
  assert(noise_set_config(&a, &live) == NOISE_OK);
  assert(noise_init(&b, &live, 5) == NOISE_OK);
  assert(a.wind.air_alpha == b.wind.air_alpha && a.wind.gust_alpha == b.wind.gust_alpha);
  /* Higurashi body Q 30 halves to a chorus Q of 15. */
  double chorus_radius = exp(-TEST_PI * 8000.0 / (15.0 * NOISE_SAMPLE_RATE_HZ));
  double chorus_coefficient = 2.0 * chorus_radius * cos(2.0 * TEST_PI * 8000.0 /
                                                        NOISE_SAMPLE_RATE_HZ);
  for (unsigned ear = 0; ear < 2; ++ear) {
    assert(fabs(a.cicadas.chorus[ear].coefficient - chorus_coefficient) < 1e-5);
    assert(a.cicadas.chorus[ear].coefficient == b.cicadas.chorus[ear].coefficient);
    assert(a.cicadas.chorus[ear].radius_squared == b.cicadas.chorus[ear].radius_squared);
  }
  assert(memcmp(a.thunder.reverb.fdn.feedback, b.thunder.reverb.fdn.feedback,
                sizeof(a.thunder.reverb.fdn.feedback)) == 0);
  assert(memcmp(a.rain.surface_cdf, b.rain.surface_cdf, sizeof(a.rain.surface_cdf)) == 0);
  assert(a.weather.period == b.weather.period && a.weather.slew == b.weather.slew);
  /* 1.5 s elapsed exceeds the new 1 s step, so the step counter restarts. */
  assert(a.weather.samples < a.weather.period);

  /* Narrowed bounds hold the current intensity without restarting the controller. */
  noise_config narrow = live;
  narrow.weather.min_intensity = 0.45f;
  narrow.weather.max_intensity = 0.5f;
  a.state.rain_intensity = 0.6f;
  a.state.rain_target = 0.3f;
  a.weather.samples = 7;
  assert(noise_set_config(&a, &narrow) == NOISE_OK);
  assert(a.state.rain_intensity == 0.5f && a.state.rain_target == 0.45f);
  assert(a.weather.samples == 7);

  /* A new intensity restarts there, in the nearest Markov state. */
  narrow.weather.intensity = 0.45f;
  assert(noise_set_config(&a, &narrow) == NOISE_OK);
  assert(a.state.rain_intensity == 0.45f && a.state.rain_target == 0.45f);
  assert(a.state.weather_state == 0 && a.weather.samples == 0);

  /* Fixed intensity follows the configuration directly. */
  noise_config fixed = narrow;
  fixed.weather.vary = 0;
  fixed.weather.intensity = 0.8f;
  assert(noise_set_config(&a, &fixed) == NOISE_OK);
  assert(a.state.rain_intensity == 0.8f && a.state.rain_target == 0.8f);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.rain_intensity == 0.8f);
}

void run_engine_tests(void) {
  test_validation();
  test_silence_and_chunks();
  test_random_stream_separation();
  test_output_saturation();
  test_set_config();
}
