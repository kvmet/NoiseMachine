#include "test_support.h"

#include <assert.h>
#include <math.h>

static void test_cicada_levels(void) {
  noise_config c = silent_config();
  c.cicadas.gain = 0.7f;
  c.cicadas.chorus = 0.0f;
  c.cicadas.placement.stereo_width = 0.0f;
  assert(noise_init(&a, &c, 29) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    assert(audio[2 * n] == audio[2 * n + 1]);
  }

  c.cicadas.chorus = 0.5f;
  assert(noise_init(&a, &c, 29) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  unsigned cicada_stereo = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    cicada_stereo += audio[2 * n] != audio[2 * n + 1];
  }
  assert(cicada_stereo > NOISE_SAMPLE_RATE_HZ / 2);
  assert(a.state.clipped_samples == 0);
}

/* Mean frequency of the loud ringing parts of the left channel, from zero crossings. */
static double ring_frequency(void) {
  double loudest = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    loudest = fmax(loudest, fabs((double)audio[2 * n]));
  }
  double previous = -1.0, gaps = 0.0;
  unsigned count = 0;
  for (unsigned n = 0; n + 1 < NOISE_SAMPLE_RATE_HZ; ++n) {
    double x0 = audio[2 * n], x1 = audio[2 * (n + 1)];
    if ((x0 < 0.0) == (x1 < 0.0) || fmax(fabs(x0), fabs(x1)) < 0.1 * loudest) continue;
    double t = n + x0 / (x0 - x1);
    if (previous >= 0.0 && t - previous < 10.0) {
      gaps += t - previous;
      ++count;
    }
    previous = t;
  }
  assert(count > 300);
  return count / (2.0 * gaps) * NOISE_SAMPLE_RATE_HZ;
}

#define DOG_DAY_CLICKS_HZ 300.0

/* One cicada calling from frame 1, others silent; a nonzero hold sets the dog-day call. */
static void solo_cicada(noise_config c, uint32_t hold_frames) {
  assert(noise_init(&a, &c, 13) == NOISE_OK);
  noise_fill(&a, audio, 1);
  for (unsigned i = 1; i < NOISE_CICADA_VOICES; ++i) a.cicadas.voice[i].until_call = UINT32_MAX;
  if (hold_frames) {
    assert(a.cicadas.voice[0].holding);
    a.cicadas.voice[0].note_length = a.cicadas.voice[0].sounding = hold_frames;
  }
}

static void test_cicada_buzz(void) {
  noise_config c = silent_config();
  c.cicadas.gain = 1.0f;
  c.cicadas.chorus = 0.0f;
  c.cicadas.placement.stereo_width = 0.0f;
  c.cicadas.placement.min_distance_m = 5.0f;
  c.cicadas.placement.max_distance_m = 5.0f;
  solo_cicada(c, 20u * NOISE_SAMPLE_RATE_HZ);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ); /* Past the swell. */
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  /* Clicks at the pulse rate make the energy envelope repeat at one click period. */
  static double power[NOISE_SAMPLE_RATE_HZ];
  double mean = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    power[n] = (double)audio[2 * n] * audio[2 * n];
    mean += power[n] / NOISE_SAMPLE_RATE_HZ;
  }
  unsigned period = (unsigned)lround(NOISE_SAMPLE_RATE_HZ / DOG_DAY_CLICKS_HZ);
  double correlation[3] = {0.0, 0.0, 0.0};
  unsigned lags[3] = {0, period / 2, period};
  for (unsigned k = 0; k < 3; ++k) {
    for (unsigned n = 0; n + period < NOISE_SAMPLE_RATE_HZ; ++n) {
      correlation[k] += (power[n] - mean) * (power[n + lags[k]] - mean);
    }
  }
  assert(correlation[2] > 0.3 * correlation[0]);
  assert(correlation[2] > 3.0 * fabs(correlation[1]));
  /* Band-pass clicks leave little energy far below the body pitch. */
  double total = 0.0, low = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) total += (double)audio[2 * n] * audio[2 * n];
  for (double f = 20.0; f < 2000.0; f += 1.0) low += pow(spectral_amplitude(f), 2.0);
  assert(0.5 * NOISE_SAMPLE_RATE_HZ * low / total < 0.02);

  /* Call: swell 0..1 s, steady 1..2 s, wind-down 2..4 s. */
  solo_cicada(c, 4u * NOISE_SAMPLE_RATE_HZ);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double steady = ring_frequency();
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double late = ring_frequency();
  /* The last second sweeps 7.5% to 15% down. */
  assert(steady > 4500.0 && late / steady > 0.82 && late / steady < 0.94);
  assert(a.state.clipped_samples == 0);
}

static void test_cicada_calls(void) {
  noise_config c = silent_config();
  c.cicadas.gain = 0.5f;
  c.cicadas.chorus = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  uint32_t start[NOISE_CICADA_VOICES] = {0};
  unsigned calls = 0, calling = 0, checks = 0;
  for (uint32_t n = 1; n <= 600u * NOISE_SAMPLE_RATE_HZ; ++n) {
    int16_t frame[2];
    noise_fill(&a, frame, 1);
    for (unsigned i = 0; i < NOISE_CICADA_VOICES; ++i) {
      const noise_cicada_voice *v = &a.cicadas.voice[i];
      if (v->note_length && v->note_samples == 1) start[i] = n;
      if (!v->note_length && start[i]) {
        double seconds = (double)(n - start[i]) / NOISE_SAMPLE_RATE_HZ;
        assert(seconds > 9.9 && seconds < 18.1);
        start[i] = 0;
        ++calls;
      }
      if (n % 4410u == 0) {
        calling += v->note_length != 0;
        ++checks;
      }
    }
  }
  /* Dog-day calls average 14 s and gaps 20 s. */
  double fraction = (double)calling / checks;
  assert(calls > 40 && fraction > 0.25 && fraction < 0.55);
}

static void test_cicada_space(void) {
  noise_config c = silent_config();
  c.cicadas.gain = 1.0f;
  c.cicadas.chorus = 0.0f;
  c.cicadas.placement.stereo_width = 0.0f;
  c.cicadas.placement.min_distance_m = 4.0f;
  c.cicadas.placement.max_distance_m = 4.0f;
  double energy[3];
  for (unsigned run = 0; run < 3; ++run) {
    if (run == 1) {
      c.cicadas.placement.min_distance_m = 32.0f;
      c.cicadas.placement.max_distance_m = 32.0f;
    }
    if (run == 2) c.reverb_gain = 0.5f;
    solo_cicada(c, 5u * NOISE_SAMPLE_RATE_HZ);
    energy[run] = 0.0;
    for (unsigned second = 0; second < 3; ++second) {
      noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
      for (unsigned n = 0; n < 2 * NOISE_SAMPLE_RATE_HZ; ++n) {
        energy[run] += (double)audio[n] * audio[n];
      }
    }
    assert(a.state.clipped_samples == 0);
  }
  /* 8x the distance is 64x less energy; the reverb send is before that loss. */
  assert(energy[0] / energy[1] > 50.0 && energy[0] / energy[1] < 80.0);
  assert(energy[2] > 2.0 * energy[1]);
}

/* Share of the 1 s left channel within 8% of the frequency. */
static double band_share(double frequency) {
  double total = 0.0, band = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) total += (double)audio[2 * n] * audio[2 * n];
  for (double f = 0.92 * frequency; f < 1.08 * frequency; f += 1.0) {
    double amplitude = spectral_amplitude(f);
    band += amplitude * amplitude;
  }
  return 0.5 * NOISE_SAMPLE_RATE_HZ * band / total;
}

static void test_cicada_songs(void) {
  noise_config c = silent_config();
  c.cicadas.gain = 1.0f;
  c.cicadas.chorus = 0.0f;
  c.cicadas.placement.stereo_width = 0.0f;
  c.cicadas.placement.min_distance_m = 5.0f;
  c.cicadas.placement.max_distance_m = 5.0f;
  double share[NOISE_CICADA_SPECIES_COUNT];
  static double power[800]; /* 10 ms blocks. */
  for (unsigned species = 0; species < NOISE_CICADA_SPECIES_COUNT; ++species) {
    c.cicadas.species = (cicada_species)species;
    solo_cicada(c, 0);
    const noise_cicada_voice *v = &a.cicadas.voice[0];
    double pitch = c.cicadas.pitch_hz * (1.0 + 0.05 * v->pitch_offset);
    unsigned syllables = v->syllables;
    unsigned blocks = 0;
    for (unsigned second = 0; second < 8; ++second) {
      noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
      if (second == 1) share[species] = band_share(pitch);
      for (unsigned b = 0; b < 100; ++b, ++blocks) {
        power[blocks] = 0.0;
        for (unsigned n = 441 * b; n < 441 * (b + 1); ++n) {
          power[blocks] += (double)audio[2 * n] * audio[2 * n];
        }
      }
    }
    assert(a.state.clipped_samples == 0);
    if (species == CICADA_DOG_DAY) continue;
    /* Syllable rhythm: the strongest envelope frequency over the syllables. */
    double rate[2] = {3.0, 7.0};
    unsigned span = (unsigned)(100.0 * syllables / rate[species - 1]) - 20;
    double best = 0.0, peak = 0.0;
    for (double f = 1.0; f < 15.0; f += 0.05) {
      double re = 0.0, im = 0.0;
      for (unsigned b = 0; b < span; ++b) {
        re += power[b] * cos(2.0 * TEST_PI * f * b / 100.0);
        im += power[b] * sin(2.0 * TEST_PI * f * b / 100.0);
      }
      if (hypot(re, im) > best) {
        best = hypot(re, im);
        peak = f;
      }
    }
    assert(fabs(peak - rate[species - 1]) < 0.15 * rate[species - 1]);
    if (species == CICADA_HIGURASHI) {
      double first = 0.0, last = 0.0;
      for (unsigned b = 0; b < span / 4; ++b) {
        first += power[b];
        last += power[span - 1 - b];
      }
      assert(first > 3.0 * last);
    }
  }
  /* Syllables fade in rather than start at full level. */
  c.cicadas.species = CICADA_MINMINZEMI;
  solo_cicada(c, 0);
  double onset = 0.0, middle = 0.0;
  for (unsigned n = 0; n < 3u * NOISE_SAMPLE_RATE_HZ; ++n) {
    int16_t frame[2];
    noise_fill(&a, frame, 1);
    const noise_cicada_voice *v = &a.cicadas.voice[0];
    if (v->holding || !v->note_length) continue;
    double x = (double)v->note_samples / v->sounding;
    if (x < 0.05) onset = fmax(onset, fabs((double)frame[0]));
    if (x > 0.4 && x < 0.6) middle = fmax(middle, fabs((double)frame[0]));
  }
  assert(onset < 0.3 * middle);
  /* Tonal species keep more energy near the body pitch than the dog-day buzz. */
  assert(share[CICADA_MINMINZEMI] > share[CICADA_DOG_DAY] + 0.15);
  assert(share[CICADA_HIGURASHI] > share[CICADA_DOG_DAY] + 0.15);
}

void run_cicadas_tests(void) {
  test_cicada_levels();
  test_cicada_buzz();
  test_cicada_calls();
  test_cicada_space();
  test_cicada_songs();
}
