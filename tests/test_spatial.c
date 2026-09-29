#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static double delay_center(const noise_drop_voice *voice, unsigned ear) {
  double center = voice->spatial.ear_delay[ear];
  double sum = 0.0;
  for (unsigned tap = 0; tap < 4; ++tap) {
    center += tap * voice->spatial.delay_weight[ear][tap];
    sum += voice->spatial.delay_weight[ear][tap];
  }
  assert(fabs(sum - 1.0) < 1e-6);
  return center;
}

static void test_spatial_geometry(void) {
  noise_config c = silent_config();
  c.listener.head_amount = 0.0f;
  c.listener.rear_amount = 0.0f;
  droplet drop = water_drop();
  drop.position.distance_m = 2.0f;
  drop.position.angle_rad = (float)(TEST_PI / 2.0);
  assert(noise_init(&a, &c, 9) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double expected_delay = c.listener.stereo_width_m * NOISE_SAMPLE_RATE_HZ / 343.0;
  assert(fabs(delay_center(&a.rain.voice[0], 0) - delay_center(&a.rain.voice[0], 1) - expected_delay) < 0.0001);
  double expected_ratio = (2.0 - c.listener.stereo_width_m / 2.0) / (2.0 + c.listener.stereo_width_m / 2.0);
  const noise_spatial *s = &a.rain.voice[0].spatial;
  assert(fabs(s->ear_gain[0] / s->ear_gain[1] - expected_ratio) < 1e-6);

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
  double phase_error = phase_difference + 2.0 * TEST_PI * 1000.0 * c.listener.stereo_width_m / 343.0;
  assert(fabs(remainder(phase_error, 2.0 * TEST_PI)) < 0.03);

  c.listener.head_amount = 1.0f;
  assert(noise_init(&a, &c, 9) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double radius = c.listener.stereo_width_m / 2.0;
  double far_path = sqrt(4.0 - radius * radius) + radius * (TEST_PI - acos(radius / 2.0));
  double around_delay = (far_path - (2.0 - radius)) * NOISE_SAMPLE_RATE_HZ / 343.0;
  assert(fabs(delay_center(&a.rain.voice[0], 0) - delay_center(&a.rain.voice[0], 1) - around_delay) < 0.001);
  assert(around_delay > expected_delay);
  for (unsigned ear = 0; ear < 2; ++ear) {
    const noise_spatial *v = &a.rain.voice[0].spatial;
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
  c.rain.water.impact_gain_min = 1.0f;
  c.rain.water.impact_gain_max = 1.0f;
  c.rain.water.bubble_decay_min = 1.0f;
  c.rain.water.bubble_decay_max = 1.0f;
  c.listener.stereo_width_m = 0.0f;
  droplet drop = water_drop();
  drop.position.angle_rad = 0.7f;
  drop.position.distance_m = 2.0f;
  assert(noise_init(&a, &c, 3) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  c.listener.head_amount = 0.0f;
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

  c.listener.rear_amount = 0.0f;
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
    c.listener.stereo_width_m = widths[w];
    assert(noise_init(&a, &c, 9) == NOISE_OK);
    droplet drop = water_drop();
    drop.position.distance_m = 0.25f;
    for (unsigned angle = 0; angle < 36; ++angle) {
      drop.position.angle_rad = (float)(angle * TEST_PI / 18.0);
      assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
      noise_drop_voice *v = &a.rain.voice[a.state.active_drops - 1];
      for (unsigned ear = 0; ear < 2; ++ear) {
        assert(isfinite(v->spatial.ear_gain[ear]));
        assert(v->spatial.ear_delay[ear] + 3 < NOISE_DIRECT_SAMPLES);
      }
    }
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    assert(a.state.active_drops == 0);
    for (unsigned i = 0; i < NOISE_DIRECT_SAMPLES; ++i) {
      assert(a.bus.direct[0][i] == 0.0f && a.bus.direct[1][i] == 0.0f);
    }
  }
}

void run_spatial_tests(void) {
  test_spatial_geometry();
  test_spatial_bypass_and_distance();
  test_spatial_extremes();
}
