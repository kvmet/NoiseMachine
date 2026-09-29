#include "test_support.h"

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
  return c;
}

void clear_surface_weights(noise_config *c) {
  for (unsigned i = 0; i < NOISE_SURFACE_SLOTS; ++i) c->rain.surface[i].weight = 0.0f;
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
