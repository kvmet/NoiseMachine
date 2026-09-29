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
  c.thunder.reverb_gain = 0.0f;
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
  c.rain.surface_weight[WATER] = -1.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  memset(c.rain.surface_weight, 0, sizeof(c.rain.surface_weight));
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
  c.rain.water.impact_gain_min = 0.6f;
  c.rain.water.impact_gain_max = 0.5f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.water.bubble_radius_min_m = 0.002f;
  c.rain.water.bubble_radius_max_m = 0.001f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.rain.water.bubble_decay_max = INFINITY;
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

static void test_wind(void) {
  noise_config c = silent_config();
  c.wind.gain = 0.5f;
  c.wind.gust_depth = 0.0f;
  c.wind.brightness = 0.2f;
  c.wind.stereo_width = 0.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int nonzero = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    nonzero |= audio[2 * n] != 0;
    assert(audio[2 * n] == audio[2 * n + 1]);
  }
  assert(nonzero);
  double dark_high = band_power(6400);

  c.wind.brightness = 1.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(band_power(6400) > 20.0 * dark_high);

  c.wind.stereo_width = 1.0f;
  c.wind.gust_depth = 1.0f;
  c.wind.gust_rate_hz = 2.0f;
  assert(noise_init(&a, &c, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  int stereo = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    stereo |= audio[2 * n] != audio[2 * n + 1];
  }
  assert(stereo);
  assert(a.wind.gust != 0.5f);
  assert(a.state.clipped_samples == 0);
}

static void test_insects(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.7f;
  c.crickets.call_rate_hz = 1.0f;
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

  c = silent_config();
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

static void test_cricket_rhythm(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.5f;
  c.crickets.call_rate_hz = 2.0f;
  c.crickets.pitch_variation = 1.0f;
  c.crickets.placement.stereo_width = 1.0f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  uint32_t last_onset[NOISE_CRICKET_VOICES] = {0};
  float pan[NOISE_CRICKET_VOICES] = {0};
  double shortest[NOISE_CRICKET_VOICES], longest[NOISE_CRICKET_VOICES] = {0};
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) shortest[i] = INFINITY;
  unsigned intervals = 0, singing = 0, checks = 0;
  double gap = 1.5 * 1.1 * NOISE_SAMPLE_RATE_HZ / c.crickets.call_rate_hz;
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
  c.crickets.call_rate_hz = 2.0f;
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
  c.crickets.call_rate_hz = 2.0f;
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

static int compare_double(const void *left, const void *right) {
  double x = *(const double *)left, y = *(const double *)right;
  return (x > y) - (x < y);
}

/* The dry rumble fades out instead of stopping at full strength. */
static void test_thunder_ending(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.0f;
  c.thunder.rate_per_min = 0.0f;
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
  c.thunder.rate_per_min = 0.0f;
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
  c.thunder.rate_per_min = 0.0f;
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
  c.thunder.rate_per_min = 21.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.min_distance_m = 199.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.max_distance_m = 900.0f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.reverb_decay_s = 0.4f;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);
  c = silent_config();
  c.thunder.reverb_gain = NAN;
  assert(noise_init(&a, &c, 1) == NOISE_INVALID_CONFIG);

  c = silent_config();
  c.thunder.gain = 1.0f;
  c.thunder.rate_per_min = 0.0f;
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
  c.thunder.rate_per_min = 20.0f;
  assert(noise_init(&a, &c, 64) == NOISE_OK);
  for (unsigned second = 0; second < 120; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  uint64_t strikes = a.state.generated_thunder + a.state.dropped_thunder;
  assert(strikes > 25 && strikes < 57);
  assert(a.state.dropped_thunder > 0);

  /* Thunder draws from its own stream, so rain is unchanged by it. */
  c = silent_config();
  c.weather.intensity = 0.8f;
  c.reverb_gain = 0.2f;
  assert(noise_init(&a, &c, 65) == NOISE_OK);
  c.thunder.gain = 1.0f;
  c.thunder.rate_per_min = 20.0f;
  assert(noise_init(&b, &c, 65) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  noise_fill(&b, audio, NOISE_SAMPLE_RATE_HZ);
  assert(b.state.generated_thunder >= 1);
  assert(a.state.generated_drops == b.state.generated_drops);
  assert(a.rain.drop_rng == b.rain.drop_rng && a.rain.arrival_rng == b.rain.arrival_rng);
}

static void test_bubble_physics(void) {
  noise_config c = silent_config();
  c.rain.water.impact_gain_min = 1.0f;
  c.rain.water.impact_gain_max = 1.0f;
  c.rain.water.bubble_gain_min = 2.0f;
  c.rain.water.bubble_gain_max = 2.0f;
  c.rain.water.bubble_decay_min = 1.0f;
  c.rain.water.bubble_decay_max = 1.0f;
  droplet drop = water_drop();
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_mode initial = a.rain.voice[0].mode[1];
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
    assert(fabs(a.rain.voice[0].mode[1].current - expected) < 2e-6);
    noise_fill(&a, frame, 1);
  }
  assert(noise_init(&b, &c, 1) == NOISE_OK);
  drop.bubble_radius_m *= 2.0f;
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_mode larger = b.rain.voice[0].mode[1];
  double measured_frequency = acos(larger.coefficient / (2.0 * sqrt(larger.radius_squared))) *
                              NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  assert(fabs(measured_frequency - frequency / 2.0) < 0.01);
}

static void test_water_controls(void) {
  noise_config c = silent_config();
  c.rain.water.impact_gain_min = 0.25f;
  c.rain.water.impact_gain_max = 0.25f;
  c.rain.water.bubble_gain_min = 1.0f;
  c.rain.water.bubble_gain_max = 1.0f;
  c.rain.water.bubble_decay_min = 4.0f;
  c.rain.water.bubble_decay_max = 4.0f;
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0008f;
  assert(noise_init(&a, &c, 31) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);

  c.rain.water.impact_gain_min = 0.5f;
  c.rain.water.impact_gain_max = 0.5f;
  c.rain.water.bubble_gain_min = 2.0f;
  c.rain.water.bubble_gain_max = 2.0f;
  assert(noise_init(&b, &c, 31) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  assert(fabs(b.rain.voice[0].mode[0].previous / a.rain.voice[0].mode[0].previous - 2.0) < 1e-6);
  assert(fabs(b.rain.voice[0].mode[1].previous / a.rain.voice[0].mode[1].previous - 4.0) < 1e-6);

  double r = drop.bubble_radius_m;
  double damping = (0.13 / r + 0.0072 / pow(r, 1.5)) / 4.0;
  double expected_radius = exp(-damping / NOISE_SAMPLE_RATE_HZ);
  assert(fabs(sqrt(a.rain.voice[0].mode[1].radius_squared) - expected_radius) < 1e-6);
}

static void test_automatic_water_bubbles(void) {
  noise_config c = silent_config();
  c.weather.intensity = 1.0f;
  c.rain.max_drops_per_s = 2000.0f;
  memset(c.rain.surface_weight, 0, sizeof(c.rain.surface_weight));
  c.rain.surface_weight[WATER] = 1.0f;
  c.rain.water.bubble_probability = 1.0f;
  c.rain.water.bubble_radius_min_m = 0.0006f;
  c.rain.water.bubble_radius_max_m = 0.0012f;
  assert(noise_init(&a, &c, 41) == NOISE_OK);
  int16_t frame[2];
  for (unsigned i = 0; i < 10000 && a.state.generated_drops == 0; ++i) {
    noise_fill(&a, frame, 1);
  }
  assert(a.state.generated_drops == 1);
  noise_mode bubble = a.rain.voice[0].mode[1];
  assert(bubble.remaining > 0);
  double q = sqrt(bubble.radius_squared);
  double frequency = acos(bubble.coefficient / (2.0 * q)) *
                     NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  double radius = sqrt(3.0 * 1.4 * 101325.0 / 1000.0) /
                  (2.0 * TEST_PI * frequency);
  assert(radius >= c.rain.water.bubble_radius_min_m);
  assert(radius <= c.rain.water.bubble_radius_max_m);

  c.rain.water.bubble_probability = 0.0f;
  assert(noise_init(&a, &c, 41) == NOISE_OK);
  for (unsigned i = 0; i < 10000 && a.state.generated_drops == 0; ++i) {
    noise_fill(&a, frame, 1);
  }
  assert(a.state.generated_drops == 1);
  assert(a.rain.voice[0].mode[1].remaining == 0);
}

static void test_roof_surfaces(void) {
  noise_config c = silent_config();
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0f;
  drop.surface = PLASTIC;
  assert(noise_init(&a, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double plastic_damping = -log(sqrt(a.rain.voice[0].mode[1].radius_squared)) *
                           NOISE_SAMPLE_RATE_HZ;
  float plastic_lowpass = a.rain.voice[0].material_lowpass_alpha;
  assert(fabs(plastic_damping - 110.0) < 0.01);
  assert(fabs(plastic_lowpass - (-expm1(-2.0 * TEST_PI * 1600.0 /
                                        NOISE_SAMPLE_RATE_HZ))) < 1e-6);

  drop.surface = ASPHALT;
  assert(noise_init(&b, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  double asphalt_damping = -log(sqrt(b.rain.voice[0].mode[1].radius_squared)) *
                           NOISE_SAMPLE_RATE_HZ;
  assert(fabs(asphalt_damping - 1600.0) < 0.1);
  assert(b.rain.voice[0].material_lowpass_alpha == 1.0f);

  drop.surface = ASPHALT_ROOF;
  assert(noise_init(&b, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  double roof_damping = -log(sqrt(b.rain.voice[0].mode[1].radius_squared)) *
                        NOISE_SAMPLE_RATE_HZ;
  assert(fabs(roof_damping - 300.0) < 0.1);
  assert(b.rain.voice[0].material_lowpass_alpha < plastic_lowpass);
  assert(b.rain.voice[0].material_lowpass_alpha > 0.0f);

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

static double reverb_energy(const noise_gen *gen) {
  double energy = 0.0;
  for (unsigned i = 0; i < NOISE_REVERB_SAMPLES; ++i) {
    energy += (double)gen->reverb.buffer[i] * gen->reverb.buffer[i];
  }
  return energy;
}

static void test_reverb_decay(void) {
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  c.reverb_gain = 1.0f;
  c.listener.stereo_width_m = 0.0f;
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
  live.rain.surface_weight[GLASS] = 5.0f;
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

int main(void) {
  test_validation();
  test_silence_and_chunks();
  test_hum();
  test_noise_spectra();
  test_wind();
  test_insects();
  test_cricket_rhythm();
  test_cricket_pitch();
  test_cricket_space();
  test_cicada_buzz();
  test_cicada_calls();
  test_cicada_space();
  test_cicada_songs();
  test_thunder();
  test_thunder_ending();
  test_thunder_tail();
  test_thunder_echoes();
  test_bubble_physics();
  test_water_controls();
  test_automatic_water_bubbles();
  test_roof_surfaces();
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
  test_set_config();
  printf("core checks passed; engine size: %zu bytes\n", sizeof(noise_gen));
  return 0;
}
