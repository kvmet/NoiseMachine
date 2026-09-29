#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int compare_double(const void *left, const void *right) {
  double x = *(const double *)left, y = *(const double *)right;
  return (x > y) - (x < y);
}

/* The dry rumble fades out instead of stopping at full strength. */
static void test_thunder_ending(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.0f;
  double stop[12];
  unsigned loud = 0;
  for (unsigned s = 0; s < 12; ++s) {
    assert(noise_init(&a, &c, 40 + s) == NOISE_OK);
    thunder_strike strike = {{500.0f * powf(16.0f, s / 11.0f), 0.5f * (float)s}};
    assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    static double block[2][600]; /* 50 ms blocks over 30 s. */
    unsigned blocks = 0;
    for (unsigned second = 0; second < 30; ++second) {
      noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
      for (unsigned b = 0; b < 20; ++b, ++blocks) {
        for (unsigned ch = 0; ch < 2; ++ch) {
          double energy = 0.0;
          for (unsigned n = 2205 * b; n < 2205 * (b + 1); ++n) {
            energy += (double)audio[2 * n + ch] * audio[2 * n + ch];
          }
          block[ch][blocks] = 10.0 * log10(energy / 2205.0 + 1e-9);
        }
      }
    }
    stop[s] = -INFINITY;
    for (unsigned ch = 0; ch < 2; ++ch) {
      double peak = -INFINITY;
      unsigned end = 0;
      for (unsigned b = 0; b < blocks; ++b) {
        peak = fmax(peak, block[ch][b]);
        if (block[ch][b] > 0.0) end = b; /* Above one LSB. */
      }
      assert(end > 10 && end + 1 < blocks);
      double before = 0.0;
      for (unsigned b = end - 10; b < end - 1; ++b) before += block[ch][b] / 9.0;
      stop[s] = fmax(stop[s], before - peak);
    }
    loud += stop[s] > -30.0;
  }
  qsort(stop, 12, sizeof(stop[0]), compare_double);
  assert(0.5 * (stop[5] + stop[6]) < -40.0 && loud <= 2);
}

/* Mean square slope over mean square: rises with the spectral centroid squared. */
static double brightness(const int16_t *x, unsigned frames) {
  double slope = 0.0, energy = 0.0;
  for (unsigned n = 1; n < frames; ++n) {
    double mid = 0.5 * ((double)x[2 * n] + x[2 * n + 1]);
    double previous = 0.5 * ((double)x[2 * n - 2] + x[2 * n - 1]);
    slope += (mid - previous) * (mid - previous);
    energy += mid * mid;
  }
  return slope / energy;
}

/* Later arrivals travel farther, so the rumble 3 to 8 s in is darker than its onset. */
static void test_thunder_tail(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.0f;
  static int16_t strike_audio[2 * 9 * NOISE_SAMPLE_RATE_HZ];
  double ratio[8];
  for (unsigned s = 0; s < 8; ++s) {
    assert(noise_init(&a, &c, 80 + s) == NOISE_OK);
    thunder_strike strike = {{700.0f * powf(8.0f, s / 7.0f), 0.8f * (float)s}};
    assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    noise_fill(&a, strike_audio, 9 * NOISE_SAMPLE_RATE_HZ);
    unsigned onset = 0;
    while (!strike_audio[2 * onset] && !strike_audio[2 * onset + 1]) ++onset;
    assert(onset < NOISE_SAMPLE_RATE_HZ);
    double early = brightness(strike_audio + 2 * onset, 3 * NOISE_SAMPLE_RATE_HZ / 2);
    double late = brightness(strike_audio + 2 * (onset + 3 * NOISE_SAMPLE_RATE_HZ),
                             5 * NOISE_SAMPLE_RATE_HZ - onset);
    ratio[s] = sqrt(late / early);
  }
  qsort(ratio, 8, sizeof(ratio[0]), compare_double);
  assert(0.5 * (ratio[3] + ratio[4]) < 1.0);
}

/* After the last direct arrival the filters ring out within 250 ms; what follows is echo. */
static void test_thunder_echoes(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.0f;
  static int16_t strike_audio[2 * 40 * NOISE_SAMPLE_RATE_HZ];
  for (unsigned s = 0; s < 8; ++s) {
    assert(noise_init(&a, &c, 120 + s) == NOISE_OK);
    thunder_strike strike = {{700.0f * powf(8.0f, s / 7.0f), 0.8f * (float)s}};
    assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    const noise_thunder_voice *v = &a.thunder.voice[0];
    float direct_end = 0.0f;
    for (unsigned i = 0; i < v->segments; ++i) {
      direct_end = fmaxf(direct_end, v->segment[i].start + v->segment[i].width);
    }
    unsigned from = (unsigned)direct_end + NOISE_SAMPLE_RATE_HZ / 4, to = v->length - 4096u;
    assert(to < 40 * NOISE_SAMPLE_RATE_HZ && to > from + NOISE_SAMPLE_RATE_HZ);
    noise_fill(&a, strike_audio, to);
    double peak = 0.0, echo = 0.0;
    for (unsigned n = 0; n < 2 * to; ++n) {
      peak = fmax(peak, fabs((double)strike_audio[n]));
      if (n >= 2 * from) echo += (double)strike_audio[n] * strike_audio[n];
    }
    double echo_db = 10.0 * log10(echo / (2.0 * (to - from)) + 1e-9) - 20.0 * log10(peak);
    assert(echo_db > -90.0);
  }
}

static void test_thunder(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.01f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.reverb_decay_s = 0.4f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.reverb_gain = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);

  c = silent_config();
  c.thunder.gain = 1.0f;
  assert(noise_init(&a, &c, 61) == NOISE_OK);
  b = a;
  thunder_strike strike = {{100.0f, 0.0f}};
  assert(noise_trigger_thunder(&a, &strike) == NOISE_INVALID_STRIKE);
  strike.position.distance_m = NAN;
  assert(noise_trigger_thunder(&a, &strike) == NOISE_INVALID_STRIKE);
  assert(memcmp(&a, &b, sizeof(a)) == 0);

  /* A strike retires after its last arrival and leaves exact silence. */
  strike.position.distance_m = 1000.0f;
  assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
  assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
  assert(noise_trigger_thunder(&a, &strike) == NOISE_VOICE_LIMIT);
  assert(a.state.generated_thunder == 2 && a.state.dropped_thunder == 1);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double low = band_power(60), high = band_power(3200);
  assert(low > 100.0 * high);
  for (unsigned second = 1; second < 24; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.thunder.voice[0].length == 0 && a.thunder.voice[1].length == 0);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
  assert(a.state.clipped_samples == 0);

  /* Distance does not change the random draws, so only its filters and gains differ. */
  double clap_ratio[2];
  const float distances[2] = {1000.0f, 8000.0f};
  for (unsigned d = 0; d < 2; ++d) {
    assert(noise_init(&a, &c, 62) == NOISE_OK);
    strike.position.distance_m = distances[d];
    assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    clap_ratio[d] = band_power(800) / band_power(60);
  }
  assert(clap_ratio[1] < 0.5 * clap_ratio[0]);

  /* Segments pan by their own azimuth, so a strike to the left is mostly left. */
  strike.position.distance_m = 2000.0f;
  strike.position.angle_rad = (float)(-TEST_PI / 2.0);
  assert(noise_init(&a, &c, 63) == NOISE_OK);
  assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double energy[2] = {0.0, 0.0};
  for (unsigned n = 0; n < 2 * NOISE_SAMPLE_RATE_HZ; ++n) energy[n % 2] += (double)audio[n] * audio[n];
  assert(energy[0] > 4.0 * energy[1]);

  /* One hand-placed segment of fixed area. End-on, its box edges give a positive then
     a negative pulse and a lower peak than side-on. A fractional start keeps the area. */
  const float widths[3] = {0.25f, 0.25f, 600.0f};
  const float starts[3] = {10.0f, 10.5f, 10.0f};
  double peak[3] = {0.0, 0.0, 0.0}, total_energy[3] = {0.0, 0.0, 0.0};
  for (unsigned w = 0; w < 3; ++w) {
    assert(noise_init(&a, &c, 66) == NOISE_OK);
    strike.position.distance_m = 200.0f;
    assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
    noise_thunder_voice *v = &a.thunder.voice[0];
    v->segments = 1;
    v->segment[0].start = starts[w];
    v->segment[0].width = widths[w];
    v->segment[0].gain[0] = v->segment[0].gain[1] = 0.05f / widths[w];
    v->segment[0].roughness = 0.0f;
    v->length = 4000;
    noise_fill(&a, audio, 4000);
    int positive = 0, negative = 0;
    for (unsigned n = 0; n < 4000; ++n) {
      double x = audio[2 * n];
      if (fabs(x) > peak[w]) peak[w] = fabs(x);
      total_energy[w] += x * x;
      if (n < 300 && x > positive) positive = (int)x;
      if (n >= 600 && n < 900 && x < negative) negative = (int)x;
    }
    if (w == 2) {
      assert(positive > 0.5 * peak[2]);
      assert(negative < -0.5 * peak[2]);
    }
  }
  assert(peak[2] < 0.5 * peak[0]);
  assert(fabs(peak[1] / peak[0] - 1.0) < 0.05);
  assert(fabs(total_energy[1] / total_energy[0] - 1.0) < 0.05);

  /* With the voice stopped, the thunder reverb decays at its configured rate:
     60 dB per decay time, so about 17 dB per second at 3.5 s. */
  c.thunder.reverb_gain = 1.0f;
  c.thunder.reverb_decay_s = 3.5f;
  strike.position.distance_m = 1000.0f;
  strike.position.angle_rad = 0.0f;
  assert(noise_init(&a, &c, 67) == NOISE_OK);
  assert(noise_trigger_thunder(&a, &strike) == NOISE_OK);
  for (unsigned second = 0; second < 3; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  a.thunder.voice[0].length = 0;
  double tail[2];
  for (unsigned second = 0; second < 2; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    tail[second] = 0.0;
    for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) {
      tail[second] += (double)audio[i] * audio[i];
    }
  }
  assert(tail[0] > 0.0);
  double decay_db = 10.0 * log10(tail[0] / tail[1]);
  assert(decay_db > 12.0 && decay_db < 22.0);
  c.thunder.reverb_gain = 0.0f;

  /* Automatic strikes: one at start, then about rate/min, including capacity losses. */
  c.storm.fixed.lightning_per_min = 20.0f;
  assert(noise_init(&a, &c, 64) == NOISE_OK);
  for (unsigned second = 0; second < 120; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  uint64_t strikes = a.state.generated_thunder + a.state.dropped_thunder;
  assert(strikes > 25 && strikes < 57);
  assert(a.state.dropped_thunder > 0);

  /* Thunder draws from its own stream, so rain is unchanged by it. */
  c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.storm.fixed.lightning_per_min = 20.0f;
  c.reverb_gain = 0.2f;
  assert(noise_init(&a, &c, 65) == NOISE_OK);
  c.thunder.gain = 1.0f;
  assert(noise_init(&b, &c, 65) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  noise_fill(&b, audio, NOISE_SAMPLE_RATE_HZ);
  assert(b.state.generated_thunder >= 1);
  assert(a.state.generated_drops == b.state.generated_drops);
  assert(a.rain.drop_rng == b.rain.drop_rng && a.rain.arrival_rng == b.rain.arrival_rng);
}

void run_thunder_tests(void) {
  test_thunder();
  test_thunder_ending();
  test_thunder_tail();
  test_thunder_echoes();
}
