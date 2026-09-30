#ifndef NOISE_CORE_H
#define NOISE_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "noise_types.h"
#include "noise_ambient.h"
#include "noise_spatial.h"
#include "noise_storm.h"
#include "noise_rain.h"
#include "noise_wind.h"
#include "noise_crickets.h"
#include "noise_cicadas.h"
#include "noise_thunder.h"
#include "noise_reverb.h"
#include "noise_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct noise_config {
  float master_gain;
  float ambient_gain[NOISE_KIND_COUNT]; /* Independent linear gains, each 0..1. */
  float reverb_gain; /* Rain and insect sends fade as 1/sqrt(distance). */
  noise_listener_config listener;
  noise_storm_config storm;
  noise_rain_config rain;
  noise_wind_config wind;
  noise_cricket_config crickets;
  noise_cicada_config cicadas;
  noise_thunder_config thunder;
} noise_config;

/* Caller-owned storage. Treat all fields except the read-only state as private. */
typedef struct noise_gen {
  noise_config config;
  noise_state state;
  noise_bus bus;
  noise_ambient ambient;
  noise_wind wind;
  noise_crickets crickets;
  noise_cicadas cicadas;
  noise_thunder thunder;
  noise_storm storm;
  noise_rain rain;
  noise_reverb reverb;
  noise_limiter limiter;
} noise_gen;

/* What the weather is doing to each layer, for display. */
typedef struct noise_status {
  noise_weather weather;
  float rain_arrivals_per_s; /* Drops landing between the rain distance bounds. */
  float rain_played_per_s; /* Of those, drops played one by one. */
  float bed_share; /* Share of rain power the bed carries at bed_gain 1. */
  float wind_level; /* Wind level over wind.gain at the current speed. */
  float cricket_rate_hz; /* Chirps per second per cricket. */
  unsigned cricket_quiet; /* NOISE_QUIET_* flags; zero when the weather allows chirps. */
  unsigned crickets_singing; /* Crickets in a singing bout, 0..NOISE_CRICKET_VOICES. */
  unsigned cicada_quiet; /* NOISE_QUIET_* flags. */
  float cicada_activity; /* Chorus level 0..1; glides toward 0 while quiet. */
} noise_status;

void noise_config_default(noise_config *config);
/* Returns 1 when noise_init and noise_set_config would accept config. */
int noise_config_valid(const noise_config *config);
/* Rejects invalid values without modifying gen. Seed zero aliases seed one. */
noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed);
/* Applies a new configuration while playing, keeping voices and random streams.
   Rejects invalid values without modifying gen. Call between fills on the audio thread. */
noise_result noise_set_config(noise_gen *gen, const noise_config *config);
/* Starts an arrival at the listener. Call between fills on the audio thread. */
noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop);
/* Starts a strike at the listener. Call between fills on the audio thread. */
noise_result noise_trigger_thunder(noise_gen *gen, const thunder_strike *strike);
/* Writes 2 * frames interleaved int16 samples (L, R); returns frames. A lookahead limiter
   keeps peaks at -1 dBFS and delays the output by NOISE_LIMITER_FRAMES. */
size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames);
void noise_get_status(const noise_gen *gen, noise_status *status);

#ifdef __cplusplus
}
#endif

#endif
