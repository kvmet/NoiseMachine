#include "test_support.h"

#include <assert.h>
#include <math.h>

static void test_cricket_levels(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.7f;
  set_cricket_rate(&c, 1.0f);
  c.crickets.pitch_variation = 0.0f;
  c.crickets.placement.stereo_width = 0.0f;
  assert(noise_init(&a, &c, 29) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  unsigned cricket_samples = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    cricket_samples += audio[2 * n] != 0;
    assert(audio[2 * n] == audio[2 * n + 1]);
  }
  assert(cricket_samples > 1000 && cricket_samples < 15000);

  c.crickets.placement.stereo_width = 1.0f;
  assert(noise_init(&a, &c, 29) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  unsigned cricket_stereo = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    cricket_stereo += audio[2 * n] != audio[2 * n + 1];
  }
  assert(cricket_stereo > 1000);
}

static void test_cricket_rhythm(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.5f;
  set_cricket_rate(&c, 2.0f);
  c.crickets.pitch_variation = 1.0f;
  c.crickets.placement.stereo_width = 1.0f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  uint32_t last_onset[NOISE_CRICKET_VOICES] = {0};
  float pan[NOISE_CRICKET_VOICES] = {0};
  double shortest[NOISE_CRICKET_VOICES], longest[NOISE_CRICKET_VOICES] = {0};
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) shortest[i] = INFINITY;
  unsigned intervals = 0, singing = 0, checks = 0;
  double gap = 1.5 * 1.1 * NOISE_SAMPLE_RATE_HZ / a.crickets.call_rate_hz;
  for (uint32_t n = 1; n <= 600u * NOISE_SAMPLE_RATE_HZ; ++n) {
    int16_t frame[2];
    noise_fill(&a, frame, 1);
    for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
      const noise_cricket_voice *v = &a.crickets.voice[i];
      if (n % 4410u == 0) {
        singing += v->singing;
        ++checks;
      }
      if (v->chirp_samples != 1) continue;
      if (last_onset[i]) {
        assert(v->spatial.ear_gain[0] == pan[i]);
        double interval = n - last_onset[i];
        if (interval < gap) {
          shortest[i] = fmin(shortest[i], interval);
          longest[i] = fmax(longest[i], interval);
          ++intervals;
        }
      }
      last_onset[i] = n;
      pan[i] = v->spatial.ear_gain[0];
    }
  }
  /* Poisson calls would spread intervals over orders of magnitude. */
  assert(intervals > 500);
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) assert(longest[i] / shortest[i] < 1.07);
  double fraction = (double)singing / checks;
  assert(fraction > 0.6 && fraction < 0.9);
  assert(a.state.clipped_samples == 0);
}

static void test_cricket_pitch(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.5f;
  set_cricket_rate(&c, 2.0f);
  c.crickets.pitch_variation = 1.0f;
  c.crickets.placement.stereo_width = 0.0f;
  assert(noise_init(&a, &c, 7) == NOISE_OK);
  noise_fill(&a, audio, 1);
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
    a.crickets.voice[i].singing = i == 0;
    a.crickets.voice[i].bout_samples = UINT32_MAX;
  }
  double start_low = INFINITY, start_high = 0.0, drop = 0.0;
  unsigned pulses = 0;
  for (unsigned second = 0; second < 5; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    unsigned n = 0;
    while (n < NOISE_SAMPLE_RATE_HZ) {
      if (audio[2 * n] == 0) {
        ++n;
        continue;
      }
      unsigned begin = n, zeros = 0;
      while (n < NOISE_SAMPLE_RATE_HZ && zeros < 20) {
        zeros = audio[2 * n] == 0 ? zeros + 1 : 0;
        ++n;
      }
      unsigned end = n - zeros;
      if (begin == 0 || n == NOISE_SAMPLE_RATE_HZ || end - begin < 300) continue;
      /* Rising zero crossings, linearly interpolated, in the first and last thirds. */
      double frequency[2];
      for (unsigned third = 0; third < 2; ++third) {
        unsigned from = third ? end - (end - begin) / 3 : begin;
        unsigned to = third ? end : begin + (end - begin) / 3;
        double first = -1.0, last = -1.0;
        unsigned crossings = 0;
        for (unsigned k = from; k + 1 < to; ++k) {
          double x0 = audio[2 * k], x1 = audio[2 * (k + 1)];
          if (x0 < 0.0 && x1 >= 0.0) {
            double t = k + x0 / (x0 - x1);
            if (first < 0.0) first = t;
            last = t;
            ++crossings;
          }
        }
        assert(crossings > 10);
        frequency[third] = (crossings - 1) * NOISE_SAMPLE_RATE_HZ / (last - first);
      }
      start_low = fmin(start_low, frequency[0]);
      start_high = fmax(start_high, frequency[0]);
      drop += frequency[1] / frequency[0];
      ++pulses;
    }
  }
  /* Third centres sit 2/3 of a pulse apart: a 3% sweep gives about 2%. */
  drop /= pulses;
  assert(pulses > 20 && drop > 0.97 && drop < 0.99);
  assert(start_high / start_low < 1.005);
}

static double cricket_energy(noise_config c, unsigned seconds) {
  assert(noise_init(&a, &c, 11) == NOISE_OK);
  double energy = 0.0;
  for (unsigned second = 0; second < seconds; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned n = 0; n < 2 * NOISE_SAMPLE_RATE_HZ; ++n) {
      energy += (double)audio[n] * audio[n];
    }
  }
  return energy;
}

static void test_cricket_space(void) {
  noise_config c = silent_config();
  c.crickets.gain = 1.0f;
  set_cricket_rate(&c, 2.0f);
  c.crickets.placement.stereo_width = 0.0f;
  c.crickets.placement.min_distance_m = 2.0f;
  c.crickets.placement.max_distance_m = 2.0f;
  double near = cricket_energy(c, 10);
  c.crickets.placement.min_distance_m = 16.0f;
  c.crickets.placement.max_distance_m = 16.0f;
  double far = cricket_energy(c, 10);
  /* 1/r level: 8x the distance is 64x less energy. */
  assert(near / far > 50.0 && near / far < 80.0);

  c.crickets.placement.min_distance_m = 16.0f;
  c.reverb_gain = 0.5f;
  double wet = cricket_energy(c, 10);
  assert(wet > 2.0 * far);

  c.reverb_gain = 0.0f;
  c.crickets.placement.stereo_width = 1.0f;
  c.crickets.placement.min_distance_m = 2.0f;
  c.crickets.placement.max_distance_m = 2.0f;
  assert(noise_init(&a, &c, 11) == NOISE_OK);
  noise_fill(&a, audio, 1);
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
    a.crickets.voice[i].singing = i == 0;
    a.crickets.voice[i].bout_samples = UINT32_MAX;
  }
  a.crickets.voice[0].angle_offset = -0.5f;
  double ear[2] = {0.0, 0.0};
  for (unsigned second = 0; second < 3; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
      ear[0] += (double)audio[2 * n] * audio[2 * n];
      ear[1] += (double)audio[2 * n + 1] * audio[2 * n + 1];
    }
  }
  /* A cricket at the listener's left is louder in the left ear. */
  assert(ear[0] > 2.0 * ear[1]);
}

void run_crickets_tests(void) {
  test_cricket_levels();
  test_cricket_rhythm();
  test_cricket_pitch();
  test_cricket_space();
}
