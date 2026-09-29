#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "noise_core.h"

#define TEST_PI 3.14159265358979323846

/* Shared engines and a one-second stereo buffer; every test initializes what it uses. */
extern noise_gen a, b;
extern int16_t audio[2 * NOISE_SAMPLE_RATE_HZ];

noise_config silent_config(void);
void clear_surface_weights(noise_config *c);
droplet water_drop(void);
double channel_amplitude(double frequency, unsigned channel);
double spectral_amplitude(double frequency);
double band_power(unsigned center);

void run_engine_tests(void);
void run_ambient_tests(void);
void run_wind_tests(void);
void run_crickets_tests(void);
void run_cicadas_tests(void);
void run_thunder_tests(void);
void run_rain_tests(void);
void run_spatial_tests(void);
void run_weather_tests(void);
void run_reverb_tests(void);

#endif
