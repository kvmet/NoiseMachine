#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noise_core.h"

#define TEST_PI 3.14159265358979323846

static noise_gen a, b;
static int16_t audio[2 * NOISE_SAMPLE_RATE_HZ];

static noise_config silent_config(void) {
  noise_config c;
  noise_config_default(&c);
  memset(c.ambient_gain, 0, sizeof(c.ambient_gain));
  c.reverb_gain = 0.0f;
  c.master_gain = 1.0f;
  return c;
}

static droplet water_drop(void) {
  droplet drop = {WATER, 0.0005f, 4.0f, 0.0004f, {1.0f, 0.0f}};
  return drop;
}

static void test_validation(void) {
  noise_config c = silent_config();
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  b = a;
  c.master_gain = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  assert(memcmp(&a, &b, sizeof(a)) == 0);
  c = silent_config();
  c.surface_weight[WATER] = -1.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  memset(c.surface_weight, 0, sizeof(c.surface_weight));
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.vary_rain = 1;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c.rain_intensity = 0.5f;
  c.min_rain_intensity = 0.9f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.max_drops_per_s = INFINITY;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.weather_mod_amount[WEATHER_MOD_REVERB_GAIN] = 1.01f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c.weather_mod_amount[WEATHER_MOD_REVERB_GAIN] = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0001f;
  assert(noise_trigger_drop(&a, &drop) == NOISE_INVALID_DROP);
  drop = water_drop();
  drop.surface = METAL;
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
  c.rain_intensity = 0.7f;
  c.vary_rain = 1;
  c.weather_step_s = 0.1f;
  c.reverb_gain = 0.2f;
  c.ambient_gain[NOISE_KIND_PINK] = 0.2f;
  c.ambient_gain[HUM_60HZ] = 0.1f;
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
  assert(a.state.dropped_drops == 0);
  assert(a.state.clipped_samples == 0);
  assert(noise_init(&b, &c, 2) == NOISE_OK);
  noise_fill(&b, block, 257);
  assert(memcmp(block, audio, sizeof(block)) != 0);
}

static double channel_amplitude(double frequency, unsigned channel) {
  double real = 0.0, imaginary = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    double phase = 2.0 * TEST_PI * frequency * n / NOISE_SAMPLE_RATE_HZ;
    real += audio[2 * n + channel] * cos(phase);
    imaginary += audio[2 * n + channel] * sin(phase);
  }
  return 2.0 * hypot(real, imaginary) / NOISE_SAMPLE_RATE_HZ;
}

static double spectral_amplitude(double frequency) {
  return channel_amplitude(frequency, 0);
}

static void test_hum(void) {
  for (unsigned kind = HUM_50HZ; kind <= HUM_60HZ; ++kind) {
    noise_config c = silent_config();
    c.ambient_gain[kind] = 0.5f;
    assert(noise_init(&a, &c, 1) == NOISE_OK);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    double frequency = kind == HUM_50HZ ? 50.0 : 60.0;
    double fundamental = spectral_amplitude(frequency);
    assert(fabs(fundamental - 32767.0 * 0.5 / 1.42) < 2.0);
    assert(fabs(spectral_amplitude(2.0 * frequency) / fundamental - 0.3) < 0.001);
    assert(fabs(spectral_amplitude(3.0 * frequency) / fundamental - 0.12) < 0.001);
    assert(spectral_amplitude(frequency + 1.0) < 1.0);
    for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) assert(audio[2*n] == audio[2*n+1]);
  }
}

static double band_power(unsigned center) {
  double power = 0.0;
  for (unsigned bin = 0; bin < 32; ++bin) {
    double frequency = center * (0.8 + 0.4 * bin / 31.0);
    double coefficient = 2.0 * cos(2.0 * TEST_PI * frequency / NOISE_SAMPLE_RATE_HZ);
    double previous = 0.0, current = 0.0;
    for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
      double next = audio[2 * n] / 32768.0 + coefficient * current - previous;
      previous = current;
      current = next;
    }
    power += current * current + previous * previous - coefficient * current * previous;
  }
  return power;
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

static void test_bubble_physics(void) {
  noise_config c = silent_config();
  droplet drop = water_drop();
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_mode initial = a.voices[0].mode[1];
  double radius = drop.bubble_radius_m;
  double frequency = sqrt(3.0 * 1.4 * 101325.0 / 1000.0) / (2.0 * TEST_PI * radius);
  double damping = 0.13 / radius + 0.0072 / pow(radius, 1.5);
  assert(fabs(frequency - 8208.11) < 0.01);
  assert(fabs(sqrt(initial.radius_squared) - exp(-damping / NOISE_SAMPLE_RATE_HZ)) < 1e-6);
  assert(fabs(initial.coefficient - 2.0 * exp(-damping / NOISE_SAMPLE_RATE_HZ) *
              cos(2.0 * TEST_PI * frequency / NOISE_SAMPLE_RATE_HZ)) < 2e-6);
  unsigned delay = initial.delay;
  int16_t frame[2];
  for (unsigned n = 0; n < delay + 120; ++n) {
    double t = n > delay ? (double)(n - delay) / NOISE_SAMPLE_RATE_HZ : 0.0;
    double expected = 0.07 * exp(-damping * t) * sin(2.0 * TEST_PI * frequency * t);
    assert(fabs(a.voices[0].mode[1].current - expected) < 2e-6);
    noise_fill(&a, frame, 1);
  }
  assert(noise_init(&b, &c, 1) == NOISE_OK);
  drop.bubble_radius_m *= 2.0f;
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_mode larger = b.voices[0].mode[1];
  double measured_frequency = acos(larger.coefficient / (2.0 * sqrt(larger.radius_squared))) *
                              NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  assert(fabs(measured_frequency - frequency / 2.0) < 0.01);
}

static void test_lifetimes_and_capacity(void) {
  noise_config c = silent_config();
  c.reverb_gain = 0.3f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  droplet drop = water_drop();
  drop.surface = METAL;
  drop.bubble_radius_m = 0.0f;
  for (unsigned i = 0; i < NOISE_MAX_DROPLETS; ++i) assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_VOICE_LIMIT);
  assert(a.state.dropped_drops == 1);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.active_drops == 0);
  assert(a.state.peak_active_drops == NOISE_MAX_DROPLETS);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  for (unsigned second = 0; second < 5; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
}

static double delay_center(const noise_drop_voice *voice, unsigned ear) {
  double center = voice->ear_delay[ear];
  double sum = 0.0;
  for (unsigned tap = 0; tap < 4; ++tap) {
    center += tap * voice->delay_weight[ear][tap];
    sum += voice->delay_weight[ear][tap];
  }
  assert(fabs(sum - 1.0) < 1e-6);
  return center;
}

static void test_spatial_geometry(void) {
  noise_config c = silent_config();
  c.head_amount = 0.0f;
  c.rear_amount = 0.0f;
  droplet drop = water_drop();
  drop.position.distance_m = 2.0f;
  drop.position.angle_rad = (float)(TEST_PI / 2.0);
  assert(noise_init(&a, &c, 9) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double expected_delay = c.stereo_width_m * NOISE_SAMPLE_RATE_HZ / 343.0;
  assert(fabs(delay_center(&a.voices[0], 0) - delay_center(&a.voices[0], 1) - expected_delay) < 0.0001);
  double expected_ratio = (2.0 - c.stereo_width_m / 2.0) / (2.0 + c.stereo_width_m / 2.0);
  assert(fabs(a.voices[0].ear_gain[0] / a.voices[0].ear_gain[1] - expected_ratio) < 1e-6);

  /* Check the rendered phase at 1 kHz against path length, including fractional delay. */
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double unshadowed_high[2] = {channel_amplitude(8000.0, 0), channel_amplitude(8000.0, 1)};
  double unshadowed_low[2] = {channel_amplitude(200.0, 0), channel_amplitude(200.0, 1)};
  double real[2] = {0}, imaginary[2] = {0};
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    double phase = 2.0 * TEST_PI * 1000.0 * n / NOISE_SAMPLE_RATE_HZ;
    for (unsigned ear = 0; ear < 2; ++ear) {
      real[ear] += audio[2 * n + ear] * cos(phase);
      imaginary[ear] -= audio[2 * n + ear] * sin(phase);
    }
  }
  double phase_difference = atan2(imaginary[0], real[0]) - atan2(imaginary[1], real[1]);
  double phase_error = phase_difference + 2.0 * TEST_PI * 1000.0 * c.stereo_width_m / 343.0;
  assert(fabs(remainder(phase_error, 2.0 * TEST_PI)) < 0.03);

  c.head_amount = 1.0f;
  assert(noise_init(&a, &c, 9) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double radius = c.stereo_width_m / 2.0;
  double far_path = sqrt(4.0 - radius * radius) + radius * (TEST_PI - acos(radius / 2.0));
  double around_delay = (far_path - (2.0 - radius)) * NOISE_SAMPLE_RATE_HZ / 343.0;
  assert(fabs(delay_center(&a.voices[0], 0) - delay_center(&a.voices[0], 1) - around_delay) < 0.001);
  assert(around_delay > expected_delay);
  for (unsigned ear = 0; ear < 2; ++ear) {
    noise_drop_voice *v = &a.voices[0];
    double dc = (v->head_b0[ear] + v->head_b1[ear]) / (1.0 - v->head_feedback);
    double high = (v->head_b0[ear] - v->head_b1[ear]) / (1.0 + v->head_feedback);
    assert(fabs(dc - 1.0) < 1e-6);
    assert(fabs(high - (ear == 1 ? 2.0 : 1.05 + 0.95 * cos(1.2 * TEST_PI))) < 1e-6);
  }
  assert(noise_init(&b, &c, 9) == NOISE_OK);
  drop.position.angle_rad = -drop.position.angle_rad;
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  int16_t pair[2];
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(channel_amplitude(8000.0, 0) < 0.5 * unshadowed_high[0]);
  assert(channel_amplitude(8000.0, 1) > 1.7 * unshadowed_high[1]);
  for (unsigned ear = 0; ear < 2; ++ear) {
    assert(fabs(channel_amplitude(200.0, ear) / unshadowed_low[ear] - 1.0) < 0.15);
  }
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&b, pair, 1);
    assert(audio[2 * n] == pair[1]);
    assert(audio[2 * n + 1] == pair[0]);
  }
}

static void test_spatial_bypass_and_distance(void) {
  noise_config c = silent_config();
  c.stereo_width_m = 0.0f;
  droplet drop = water_drop();
  drop.position.angle_rad = 0.7f;
  drop.position.distance_m = 2.0f;
  assert(noise_init(&a, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  c.head_amount = 0.0f;
  assert(noise_init(&b, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int16_t frame[2];
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&b, frame, 1);
    assert(audio[2 * n] == audio[2 * n + 1]);
    assert(audio[2 * n] == frame[0]);
  }
  c.reverb_gain = 0.5f;
  assert(noise_init(&a, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  drop.position.distance_m = 4.0f;
  assert(noise_init(&b, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  unsigned wet_samples = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&b, frame, 1);
    if (n < 500) assert(abs(audio[2 * n] - 2 * frame[0]) <= 1);
    if (n > 2000) {
      assert(audio[2 * n] == frame[0]);
      assert(audio[2 * n + 1] == frame[1]);
      wet_samples += frame[0] != 0;
    }
  }
  assert(wet_samples > 100);
  c.reverb_gain = 0.0f;
  drop.position.angle_rad = 0.0f;
  assert(noise_init(&a, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  drop.position.angle_rad = (float)TEST_PI;
  assert(noise_init(&b, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double front_high = channel_amplitude(8000.0, 0);
  double rear_real = 0.0, rear_imaginary = 0.0;
  double front_energy = 0, rear_energy = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&b, frame, 1);
    front_energy += (double)audio[2 * n] * audio[2 * n];
    rear_energy += (double)frame[0] * frame[0];
    double phase = 2.0 * TEST_PI * 8000.0 * n / NOISE_SAMPLE_RATE_HZ;
    rear_real += frame[0] * cos(phase);
    rear_imaginary += frame[0] * sin(phase);
  }
  assert(rear_energy < 0.8 * front_energy);
  double rear_high = 2.0 * hypot(rear_real, rear_imaginary) / NOISE_SAMPLE_RATE_HZ;
  assert(rear_high < 0.45 * front_high);

  c.rear_amount = 0.0f;
  assert(noise_init(&a, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  drop.position.angle_rad = 0.0f;
  assert(noise_init(&b, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&b, frame, 1);
    assert(audio[2 * n] == frame[0]);
    assert(audio[2 * n + 1] == frame[1]);
  }
}

static void test_spatial_extremes(void) {
  const float widths[] = {0.0f, 1e-20f, 0.001f, 0.18f, 0.49999997f, 0.5f};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
    noise_config c = silent_config();
    c.stereo_width_m = widths[w];
    assert(noise_init(&a, &c, 9) == NOISE_OK);
    droplet drop = water_drop();
    drop.position.distance_m = 0.25f;
    for (unsigned angle = 0; angle < 36; ++angle) {
      drop.position.angle_rad = (float)(angle * TEST_PI / 18.0);
      assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
      noise_drop_voice *v = &a.voices[a.state.active_drops - 1];
      for (unsigned ear = 0; ear < 2; ++ear) {
        assert(isfinite(v->ear_gain[ear]));
        assert(v->ear_delay[ear] + 3 < NOISE_DIRECT_SAMPLES);
      }
    }
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    assert(a.state.active_drops == 0);
    for (unsigned i = 0; i < NOISE_DIRECT_SAMPLES; ++i) {
      assert(a.direct[0][i] == 0.0f && a.direct[1][i] == 0.0f);
    }
  }
}

static void test_weather_and_arrivals(void) {
  noise_config c = silent_config();
  c.rain_intensity = 0.5f;
  c.max_drops_per_s = 800.0f;
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
  c.vary_rain = 1;
  c.weather_step_s = 0.1f;
  c.rain_slew_s = 0.01f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  c.ambient_gain[NOISE_KIND_WHITE] = 0.2f;
  assert(noise_init(&b, &c, 5) == NOISE_OK);
  int16_t other[2];
  unsigned visited = 0;
  unsigned previous_state = a.state.weather_state;
  for (unsigned n = 0; n < 20 * NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&a, audio, 1);
    noise_fill(&b, other, 1);
    assert(a.state.rain_intensity >= c.min_rain_intensity);
    assert(a.state.rain_intensity <= c.max_rain_intensity);
    assert(a.state.rain_intensity == b.state.rain_intensity);
    assert(a.state.generated_drops == b.state.generated_drops);
    assert(abs((int)a.state.weather_state - (int)previous_state) <= 1);
    previous_state = a.state.weather_state;
    visited |= 1u << a.state.weather_state;
  }
  assert(visited == 7);
}

static void test_random_stream_separation(void) {
  noise_config c = silent_config();
  c.rain_intensity = 1.0f;
  c.ambient_gain[NOISE_KIND_WHITE] = 0.1f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  unsigned adjacent_matches = 0;
  for (unsigned i = 0; i < 16; ++i) {
    uint32_t arrival = a.arrival_rng;
    noise_fill(&a, audio, 1);
    adjacent_matches += a.ambient_rng == arrival;
  }
  assert(adjacent_matches < 2);
}

static void test_slow_weather_slew(void) {
  noise_config c = silent_config();
  c.vary_rain = 1;
  c.min_rain_intensity = 0.0f;
  c.max_rain_intensity = 1.0f;
  c.rain_slew_s = 60.0f;
  c.weather_step_s = 3600.0f;
  c.max_drops_per_s = 0.0f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  /* Hold the internal control target fixed for an analytic step-response check. */
  a.state.rain_target = 1.0f;
  for (unsigned second = 0; second < 300; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(fabs(a.state.rain_intensity - (1.0 - exp(-5.0))) < 1e-6);
}

static void test_weather_modulation(void) {
  noise_config c = silent_config();
  c.rain_intensity = 0.0f;
  c.max_drops_per_s = 100.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops == 0);

  c.weather_mod_amount[WEATHER_MOD_ARRIVAL_RATE] = 0.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops > 50);

  c.rain_intensity = 1.0f;
  c.weather_mod_amount[WEATHER_MOD_ARRIVAL_RATE] = -1.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops == 0);

  c.max_drops_per_s = 0.0f;
  c.rain_intensity = 0.0f;
  c.rain_gain = 0.5f;
  c.weather_mod_amount[WEATHER_MOD_RAIN_GAIN] = 1.0f;
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, 1024);
  for (unsigned i = 0; i < 2048; ++i) assert(audio[i] == 0);

  c.rain_intensity = 1.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_fill(&a, audio, 1024);
  unsigned nonzero = 0;
  for (unsigned i = 0; i < 2048; ++i) nonzero += audio[i] != 0;
  assert(nonzero > 0);
}

static void test_output_saturation(void) {
  noise_config c = silent_config();
  c.rain_gain = 1.0f;
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

static double reverb_energy(const noise_gen *gen) {
  double energy = 0.0;
  for (unsigned i = 0; i < NOISE_REVERB_SAMPLES; ++i) {
    energy += (double)gen->reverb[i] * gen->reverb[i];
  }
  return energy;
}

static void test_reverb_decay(void) {
  noise_config c = silent_config();
  c.rain_gain = 1.0f;
  c.reverb_gain = 1.0f;
  c.stereo_width_m = 0.0f;
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

int main(void) {
  test_validation();
  test_silence_and_chunks();
  test_hum();
  test_noise_spectra();
  test_bubble_physics();
  test_lifetimes_and_capacity();
  test_spatial_geometry();
  test_spatial_bypass_and_distance();
  test_spatial_extremes();
  test_weather_and_arrivals();
  test_random_stream_separation();
  test_slow_weather_slew();
  test_weather_modulation();
  test_output_saturation();
  test_reverb_decay();
  printf("core checks passed; engine size: %zu bytes\n", sizeof(noise_gen));
  return 0;
}
