#ifndef NOISE_DSP_H
#define NOISE_DSP_H

#include <stdint.h>

typedef struct noise_mode {
  float previous;
  float current;
  float coefficient;
  float radius_squared;
  uint32_t remaining;
  uint32_t delay;
} noise_mode;

typedef struct noise_oscillator {
  float previous;
  float current;
  float coefficient;
} noise_oscillator;

/* Two-pole band-pass: y = coefficient y1 - radius^2 y2 + x - x2. */
typedef struct noise_resonator {
  float coefficient;
  float radius_squared;
  float state[2];
  float input[2];
} noise_resonator;

typedef struct noise_biquad {
  float b0, b1, b2, a1, a2;
  float state[2];
} noise_biquad;

#endif
