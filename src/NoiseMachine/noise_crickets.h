#ifndef NOISE_CRICKETS_H
#define NOISE_CRICKETS_H

#include "noise_dsp.h"
#include "noise_spatial.h"

#define NOISE_CRICKET_VOICES 4u

typedef struct noise_cricket_config {
  float gain;
  float call_rate_scale; /* Times the chirp rate Dolbear's law gives for the temperature. */
  float pitch_hz;
  float pitch_variation;
  noise_placement placement;
  /* Weather the crickets sing in. */
  float min_temperature_c;
  float max_rain_mm_h;
  float max_wind_m_s; /* Mean wind; gusts do not count. */
} noise_cricket_config;

/* One persistent cricket; its offsets scale with the live config at each chirp. */
typedef struct noise_cricket_voice {
  noise_oscillator oscillator;
  noise_spatial spatial;
  float glide; /* Oscillator coefficient step per frame within a pulse. */
  float pitch_offset; /* -1..1 */
  float angle_offset; /* -1..1, times pi times stereo width. */
  float distance_offset; /* 0..1, area-uniform between the distance bounds. */
  float period_scale; /* Chirp period relative to 1 / call rate. */
  unsigned pulses; /* Per chirp. */
  unsigned singing;
  uint32_t pulse_samples;
  uint32_t sounding_samples;
  uint32_t chirp_samples; /* Frames since the chirp started. */
  uint32_t until_chirp;
  uint32_t bout_samples; /* Frames left in the singing or silent bout. */
} noise_cricket_voice;

typedef struct noise_crickets {
  uint32_t rng;
  unsigned started;
  unsigned quiet; /* NOISE_QUIET_* flags; while any is set, no new chirps. */
  float call_rate_hz;
  noise_cricket_voice voice[NOISE_CRICKET_VOICES];
} noise_crickets;

#ifdef __cplusplus
extern "C" {
#endif

int noise_cricket_config_valid(const noise_cricket_config *c);
void noise_cricket_config_default(noise_cricket_config *c);
void noise_crickets_init(noise_crickets *crickets, uint32_t seed);
/* Sets the chirp rate from temperature and silences the crickets in bad weather. */
void noise_crickets_follow(noise_crickets *crickets, const noise_cricket_config *c,
                           const noise_weather *weather);
/* Returns the reverb send; the direct sound goes to the bus. */
float noise_crickets_next(noise_crickets *crickets, const noise_cricket_config *c,
                           const noise_listener_config *listener, noise_bus *bus);

#ifdef __cplusplus
}
#endif

#endif
