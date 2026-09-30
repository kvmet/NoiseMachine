#ifndef NOISE_CICADAS_H
#define NOISE_CICADAS_H

#include "noise_dsp.h"
#include "noise_spatial.h"

#define NOISE_CICADA_VOICES 4u

typedef enum cicada_species {
  CICADA_DOG_DAY = 0,
  CICADA_MINMINZEMI,
  CICADA_HIGURASHI,
  CICADA_ABURAZEMI,
  CICADA_NIINIIZEMI,
  CICADA_KUMAZEMI,
  CICADA_PHARAOH,
  CICADA_SCISSOR_GRINDER,
  CICADA_CIGALE_GRISE,
  CICADA_GREEN_GROCER,
  NOISE_CICADA_SPECIES_COUNT
} cicada_species;

typedef struct noise_cicada_species_info {
  const char *name; /* Display name with the typical region. */
  const char *key; /* Lowercase, hyphenated; for command lines. */
  float pitch_hz; /* Typical body pitch. */
} noise_cicada_species_info;

extern const noise_cicada_species_info noise_cicada_species[NOISE_CICADA_SPECIES_COUNT];

typedef struct noise_cicada_config {
  float gain;
  cicada_species species;
  float pitch_hz;
  float click_rate_scale; /* 0.5..1.5 times the species' tymbal click rate. */
  float chorus; /* Level of the distant chorus under the individuals. */
  noise_placement placement;
  /* Weather the cicadas sing in. */
  float min_temperature_c;
  float max_rain_mm_h;
} noise_cicada_config;

/* One persistent cicada; its offsets scale with the live config at each call. */
typedef struct noise_cicada_voice {
  noise_resonator body; /* Abdomen resonance rung by each tymbal click. */
  noise_oscillator throb;
  noise_spatial spatial;
  float glide; /* Body coefficient step per frame while the pitch moves. */
  float pitch_offset; /* -1..1 */
  float angle_offset; /* -1..1, times pi times stereo width. */
  float distance_offset; /* 0..1, area-uniform between the distance bounds. */
  float until_click; /* Frames. */
  unsigned syllable; /* Index in the phrase. */
  unsigned syllables; /* In the phrase; the held note follows the last. */
  unsigned holding; /* The current note is the held note. */
  uint32_t note_samples; /* Frames into the current syllable or held note. */
  uint32_t note_length; /* Zero while silent. */
  uint32_t sounding; /* Frames of the note that sound; the rest is a gap. */
  uint32_t until_call;
} noise_cicada_voice;

typedef struct noise_cicadas {
  uint32_t rng;
  unsigned started;
  unsigned quiet; /* NOISE_QUIET_* flags; while any is set, no new calls and the chorus fades. */
  float activity; /* Chorus level, gliding toward 0 while quiet and 1 otherwise. */
  noise_cicada_voice voice[NOISE_CICADA_VOICES];
  noise_resonator chorus[2]; /* Independent per ear. */
  float swell;
  float swell_target;
  uint32_t swell_samples;
} noise_cicadas;

#ifdef __cplusplus
extern "C" {
#endif

int noise_cicada_config_valid(const noise_cicada_config *c);
void noise_cicada_config_default(noise_cicada_config *c);
/* Selects species and its typical pitch. */
void noise_cicada_set_species(noise_cicada_config *c, cicada_species species);
void noise_cicadas_init(noise_cicadas *cicadas, uint32_t seed);
void noise_cicadas_configure(noise_cicadas *cicadas, const noise_cicada_config *c);
/* Silences the cicadas when it is too cool or wet for the configuration. */
void noise_cicadas_follow(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_weather *weather);
/* Returns the reverb send; the direct sound goes to the bus. */
float noise_cicadas_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_listener_config *listener, noise_bus *bus);

#ifdef __cplusplus
}
#endif

#endif
