#include "noise_reverb.h"

#include <math.h>

#include "noise_internal.h"

static const unsigned reverb_length[NOISE_REVERB_LINES] = {
  739, 953, 1151, 1327, 1471, 1663
};
static const unsigned reverb_offset[NOISE_REVERB_LINES] = {
  0, 739, 1692, 2843, 4170, 5641
};

void noise_fdn_next(float *buffer, const unsigned *length, const unsigned *offset,
                     noise_fdn *fdn, float damping_alpha, float send, float out[2]) {
  float delay[NOISE_REVERB_LINES];
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    float value = buffer[offset[i] + fdn->position[i]];
    fdn->damping[i] += damping_alpha * (value - fdn->damping[i]);
    delay[i] = fdn->damping[i];
  }
  const float scatter = 0.447213595f;
  float feedback[NOISE_REVERB_LINES] = {
    scatter * (delay[1] + delay[2] + delay[3] + delay[4] + delay[5]),
    scatter * (delay[0] + delay[2] - delay[3] - delay[4] + delay[5]),
    scatter * (delay[0] + delay[1] + delay[3] - delay[4] - delay[5]),
    scatter * (delay[0] - delay[1] + delay[2] + delay[4] - delay[5]),
    scatter * (delay[0] - delay[1] - delay[2] + delay[3] + delay[5]),
    scatter * (delay[0] + delay[1] - delay[2] - delay[3] + delay[4])
  };
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    buffer[offset[i] + fdn->position[i]] = 0.408248290f * send + fdn->feedback[i] * feedback[i];
    if (++fdn->position[i] == length[i]) fdn->position[i] = 0;
  }
  out[0] = 0.577350269f * delay[0] + 0.288675135f * delay[1] -
           0.288675135f * delay[2] - 0.577350269f * delay[3] -
           0.288675135f * delay[4] + 0.288675135f * delay[5];
  out[1] = 0.5f * delay[1] + 0.5f * delay[2] - 0.5f * delay[4] - 0.5f * delay[5];
}

void noise_reverb_init(noise_reverb *reverb) {
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    reverb->fdn.feedback[i] = powf(0.001f,
        (float)reverb_length[i] / (0.65f * NOISE_SAMPLE_RATE_HZ));
  }
}

void noise_reverb_next(noise_reverb *reverb, float send, float gain,
                        float *left, float *right) {
  float out[2];
  noise_fdn_next(reverb->buffer, reverb_length, reverb_offset, &reverb->fdn, 0.16f, send, out);
  *left += gain * out[0];
  *right += gain * out[1];
}
