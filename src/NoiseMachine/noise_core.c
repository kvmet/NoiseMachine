#include "noise_core.h"

#include <string.h>

#define NOISE_Q15_SCALE 32767.0f
#define NOISE_Q15_MIN (-32768)
#define NOISE_PINK_UNIFORM_SCALE (1.0f / 2147483647.0f)

droplet droplets[MAX_DROPLETS];

static float noise_next_unit(noise_gen *gen) {
  uint32_t x = gen->rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  gen->rng_state = x;
  int32_t signed_x = (int32_t)x;
  return (float)signed_x * NOISE_PINK_UNIFORM_SCALE;
}

static int16_t noise_to_sample(float value) {
  float scaled = value * NOISE_Q15_SCALE;
  if (scaled > NOISE_Q15_SCALE) {
    return (int16_t)NOISE_Q15_SCALE;
  }
  if (scaled < (float)NOISE_Q15_MIN) {
    return (int16_t)NOISE_Q15_MIN;
  }
  return (int16_t)scaled;
}


static int16_t droplet_next_sample(droplet *dl) {
  float sample = 0.0;

  switch(dl.phase) {
    case impact_phase.IDLE :
      break;
      case impact_phase.
  }

  return (int16_t)sample;
}

static int16_t noise_next_sample(noise_gen *gen) {
  if (gen->kind == NOISE_KIND_WHITE) {
    return noise_to_sample(noise_next_unit(gen));
  }

  float white = noise_next_unit(gen);
  float *b = gen->pink_b;
  b[0] = 0.99886f * b[0] + white * 0.0555179f;
  b[1] = 0.99332f * b[1] + white * 0.0750759f;
  b[2] = 0.96900f * b[2] + white * 0.1538520f;
  b[3] = 0.86650f * b[3] + white * 0.3104856f;
  b[4] = 0.55000f * b[4] + white * 0.5329522f;
  b[5] = -0.7616f * b[5] - white * 0.0168980f;
  float pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
  b[6] = white * 0.115926f;
  return noise_to_sample(pink * 0.11f);
}

void noise_init(noise_gen *gen, noise_kind kind, uint32_t seed) {
  memset(gen, 0, sizeof(*gen));
  gen->kind = kind;
  gen->rng_state = seed ? seed : 1u;
}

void noise_set_kind(noise_gen *gen, noise_kind kind) {
  gen->kind = kind;
}

size_t noise_fill(noise_gen *gen, int16_t *out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = noise_next_sample(gen);
  }
  return count;
}
