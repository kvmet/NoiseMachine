#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "noise_core.h"

#define TEST_PI 3.14159265358979323846

/* Shared engines and a one-second stereo buffer; every test initializes what it uses. */
extern noise_gen a, b;
extern int16_t audio[2 * NOISE_SAMPLE_RATE_HZ];

noise_config silent_config(void);
void clear_surface_coverage(noise_config *c);
/* Sets the temperature that gives this cricket chirp rate at call_rate_scale 0.5. */
void set_cricket_rate(noise_config *c, float hz);
droplet water_drop(void);
double channel_amplitude(double frequency, unsigned channel);
double spectral_amplitude(double frequency);
double band_power(unsigned center);

/* BS.1770 loudness in LUFS of gen's next seconds of output: gated integrated, and the
   loudest 400 ms block. */
typedef struct loudness {
  double integrated;
  double max_momentary;
} loudness;
loudness measure_loudness(noise_gen *gen, double seconds);

void run_engine_tests(void);
void run_ambient_tests(void);
void run_wind_tests(void);
void run_crickets_tests(void);
void run_cicadas_tests(void);
void run_thunder_tests(void);
void run_rain_tests(void);
void run_spatial_tests(void);
void run_storm_tests(void);
void run_reverb_tests(void);
void run_mix_tests(void);

#endif
