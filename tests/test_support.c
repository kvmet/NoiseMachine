#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <string.h>

noise_gen a, b;
int16_t audio[2 * NOISE_SAMPLE_RATE_HZ];

noise_config silent_config(void) {
  noise_config c;
  noise_config_default(&c);
  memset(c.ambient_gain, 0, sizeof(c.ambient_gain));
  c.reverb_gain = 0.0f;
  c.thunder.reverb_gain = 0.0f;
  c.master_gain = 1.0f;
  c.rain.sheet_depth = 0.0f; /* Steady rain; sheet tests turn it on. */
  return c;
}

void clear_surface_coverage(noise_config *c) {
  for (unsigned i = 0; i < c->rain.surface_count; ++i) c->rain.surface[i].coverage = 0.0f;
}

void set_cricket_rate(noise_config *c, float hz) {
  c->crickets.call_rate_scale = 0.5f;
  c->storm.fixed.temperature_c = (60.0f * hz / 0.5f + 32.0f) / 7.2f;
}

droplet water_drop(void) {
  droplet drop = {WATER, 0.0005f, 4.0f, 0.0004f, {1.0f, 0.0f}};
  return drop;
}

double channel_amplitude(double frequency, unsigned channel) {
  double real = 0.0, imaginary = 0.0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    double phase = 2.0 * TEST_PI * frequency * n / NOISE_SAMPLE_RATE_HZ;
    real += audio[2 * n + channel] * cos(phase);
    imaginary += audio[2 * n + channel] * sin(phase);
  }
  return 2.0 * hypot(real, imaginary) / NOISE_SAMPLE_RATE_HZ;
}

double spectral_amplitude(double frequency) {
  return channel_amplitude(frequency, 0);
}

double band_power(unsigned center) {
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

typedef struct k_biquad {
  double b0, b1, b2, a1, a2;
  double x1, x2, y1, y2;
} k_biquad;

static double k_biquad_next(k_biquad *f, double x) {
  double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
  f->x2 = f->x1;
  f->x1 = x;
  f->y2 = f->y1;
  f->y1 = y;
  return y;
}

/* BS.1770 K-weighting: a +4 dB shelf above 1.7 kHz, then a 38 Hz high-pass. */
static void k_weighting(k_biquad stage[2]) {
  const double rate = NOISE_SAMPLE_RATE_HZ;
  double k = tan(TEST_PI * 1681.9744509555319 / rate), q = 0.7071752369554193;
  double vh = pow(10.0, 3.99984385397 / 20.0), vb = pow(vh, 0.4996667741545416);
  double a0 = 1.0 + k / q + k * k;
  stage[0] = (k_biquad){(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0,
                        (vh - vb * k / q + k * k) / a0, 2.0 * (k * k - 1.0) / a0,
                        (1.0 - k / q + k * k) / a0, 0, 0, 0, 0};
  k = tan(TEST_PI * 38.13547087613982 / rate);
  q = 0.5003270373253953;
  a0 = 1.0 + k / q + k * k;
  stage[1] = (k_biquad){1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0,
                        0, 0, 0, 0};
}

static double lufs(double power) {
  return -0.691 + 10.0 * log10(power);
}

loudness measure_loudness(noise_gen *gen, double seconds) {
  enum { STEP_FRAMES = NOISE_SAMPLE_RATE_HZ / 10, MAX_STEPS = 1200 };
  static double step_power[MAX_STEPS];
  unsigned steps = (unsigned)(seconds * 10.0);
  assert(steps >= 4 && steps <= MAX_STEPS);
  k_biquad filter[2][2];
  k_weighting(filter[0]);
  k_weighting(filter[1]);
  for (unsigned step = 0; step < steps; ++step) {
    noise_fill(gen, audio, STEP_FRAMES);
    double power = 0.0;
    for (unsigned n = 0; n < STEP_FRAMES; ++n) {
      for (unsigned channel = 0; channel < 2; ++channel) {
        double x = audio[2 * n + channel] / 32768.0;
        double y = k_biquad_next(&filter[channel][1], k_biquad_next(&filter[channel][0], x));
        power += y * y;
      }
    }
    step_power[step] = power / STEP_FRAMES;
  }
  /* 400 ms blocks with 75% overlap; gates at -70 LUFS, then 10 LU under the mean. */
  unsigned blocks = steps - 3;
  double sum = 0.0, max_block = 0.0;
  unsigned count = 0;
  for (unsigned i = 0; i < blocks; ++i) {
    double block = 0.25 * (step_power[i] + step_power[i + 1] + step_power[i + 2] +
                           step_power[i + 3]);
    if (block > max_block) max_block = block;
    if (block > 0.0 && lufs(block) > -70.0) {
      sum += block;
      ++count;
    }
  }
  loudness result = {-INFINITY, max_block > 0.0 ? lufs(max_block) : -INFINITY};
  if (!count) return result;
  double relative_gate = lufs(sum / count) - 10.0;
  double gated = 0.0;
  count = 0;
  for (unsigned i = 0; i < blocks; ++i) {
    double block = 0.25 * (step_power[i] + step_power[i + 1] + step_power[i + 2] +
                           step_power[i + 3]);
    if (block > 0.0 && lufs(block) > -70.0 && lufs(block) > relative_gate) {
      gated += block;
      ++count;
    }
  }
  result.integrated = lufs(gated / count);
  return result;
}
