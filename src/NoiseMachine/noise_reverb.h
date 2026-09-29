#ifndef NOISE_REVERB_H
#define NOISE_REVERB_H
#define NOISE_REVERB_LINES 6u
#define NOISE_REVERB_SAMPLES 7304u

/* Six-line feedback delay network; the caller owns the delay-line buffer. */
typedef struct noise_fdn {
  unsigned position[NOISE_REVERB_LINES];
  float damping[NOISE_REVERB_LINES];
  float feedback[NOISE_REVERB_LINES];
} noise_fdn;

typedef struct noise_reverb {
  float buffer[NOISE_REVERB_SAMPLES];
  noise_fdn fdn;
} noise_reverb;

#ifdef __cplusplus
extern "C" {
#endif

/* Six-line FDN step: damped reads, conference-matrix scatter, stereo taps. */
void noise_fdn_next(float *buffer, const unsigned *length, const unsigned *offset,
                     noise_fdn *fdn, float damping_alpha, float send, float out[2]);
void noise_reverb_init(noise_reverb *reverb);
void noise_reverb_next(noise_reverb *reverb, float send, float gain,
                        float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
