#ifndef NOISE_CICADAS_H
#define NOISE_CICADAS_H

#include "noise_dsp.h"
#include "noise_spatial.h"

#define NOISE_CICADA_VOICES 4u

typedef enum cicada_species {
  CICADA_DOG_DAY = 0,
  CICADA_MINMINZEMI,
  CICADA_HIGURASHI,
  NOISE_CICADA_SPECIES_COUNT
} cicada_species;

typedef struct noise_cicada_config {
  float gain;
  cicada_species species;
  float pitch_hz;
  float click_rate_scale; /* 0.5..1.5 times the species' tymbal click rate. */
  float chorus; /* Level of the distant chorus under the individuals. */
  noise_placement placement;
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
void noise_cicadas_init(noise_cicadas *cicadas, uint32_t seed);
void noise_cicadas_configure(noise_cicadas *cicadas, const noise_cicada_config *c);
/* Returns the reverb send; the direct sound goes to the bus. */
float noise_cicadas_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_listener_config *listener, noise_bus *bus);

#ifdef __cplusplus
}
#endif

#endif
