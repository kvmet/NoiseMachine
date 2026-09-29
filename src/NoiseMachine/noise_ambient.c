#include "noise_ambient.h"

#include <math.h>

#include "noise_internal.h"

void noise_ambient_init(noise_ambient *ambient, uint32_t seed) {
  ambient->rng = stream_seed(seed, 0x9e3779b9u);
  for (unsigned i = 0; i < NOISE_HUM_TABLE_SAMPLES; ++i) {
    float phase = 2.0f * NOISE_PI * (float)i / (float)NOISE_HUM_TABLE_SAMPLES;
    ambient->hum_table[i] = (sinf(phase) + 0.3f * sinf(2.0f * phase) +
                             0.12f * sinf(3.0f * phase)) / 1.42f;
  }
}

float noise_ambient_next(noise_ambient *ambient, const float gain[NOISE_KIND_COUNT]) {
  float sample = 0.0f;
  if (gain[NOISE_KIND_WHITE] > 0.0f) {
    sample += gain[NOISE_KIND_WHITE] * (2.0f * random_unit(&ambient->rng) - 1.0f);
  }
  if (gain[NOISE_KIND_PINK] > 0.0f) {
    float white = 2.0f * random_unit(&ambient->rng) - 1.0f;
    float *b = ambient->pink;
    b[0] = 0.99886f * b[0] + white * 0.0555179f;
    b[1] = 0.99332f * b[1] + white * 0.0750759f;
    b[2] = 0.96900f * b[2] + white * 0.1538520f;
    b[3] = 0.86650f * b[3] + white * 0.3104856f;
    b[4] = 0.55000f * b[4] + white * 0.5329522f;
    b[5] = -0.7616f * b[5] - white * 0.0168980f;
    float pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
    b[6] = white * 0.115926f;
    sample += gain[NOISE_KIND_PINK] * pink * 0.11f;
  }
  const float *table = ambient->hum_table;
  if (gain[NOISE_KIND_HUM_50HZ] > 0.0f) {
    sample += gain[NOISE_KIND_HUM_50HZ] * table[ambient->hum_sample % NOISE_HUM_TABLE_SAMPLES];
  }
  if (gain[NOISE_KIND_HUM_60HZ] > 0.0f) {
    /* 60 Hz has 735 samples/period; interpolate the common periodic table. */
    unsigned phase = (ambient->hum_sample % 735) * 6;
    unsigned index = phase / 5;
    float fraction = (float)(phase % 5) * 0.2f;
    sample += gain[NOISE_KIND_HUM_60HZ] * (table[index] + fraction *
        (table[(index + 1) % NOISE_HUM_TABLE_SAMPLES] - table[index]));
  }
  /* 4410 frames hold whole periods of both hums. */
  if (++ambient->hum_sample == 4410) ambient->hum_sample = 0;
  return sample;
}
