#include "noise_cicadas.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

#define CICADA_LEVEL 2.0f
#define CICADA_CHORUS_LEVEL 0.015f
/* Gain 1 at the reference condition in test_mix.c reads -24 LUFS. */
#define CICADA_CALIBRATION 1.38f
#define CICADA_CLICK 0.5f /* Band-pass ring amplitude is twice the impulse. */
#define CICADA_JITTER 0.01f /* Click interval spread. */
#define CICADA_DROP 0.15f /* Pitch and click rate fall through a held note's wind-down. */
#define CICADA_SWELL_ALPHA (1.0f / (2.0f * NOISE_SAMPLE_RATE_HZ)) /* 2 s time constant. */
#define CICADA_ACTIVITY_ALPHA (1.0f / (5.0f * NOISE_SAMPLE_RATE_HZ))

typedef struct cicada_song {
  float click_rate_hz;
  float q; /* Body resonance; a high Q rings across clicks and sounds tonal. */
  float syllable_hz[2]; /* Syllable rate at the phrase start and end. */
  float duty; /* Sounding fraction of each syllable period. */
  float syllable_drop; /* Pitch fall through each syllable; negative rises. */
  float syllables[2]; /* Syllables per phrase, inclusive range. */
  float fade; /* Last syllable level relative to the first. */
  float hold_s[2]; /* Held final note length range. */
  float throb; /* Held-note pulsing depth. */
  float gap_s; /* Mean silence between calls. */
} cicada_song;

/* Starting values from descriptions of each song, not fitted to recordings. */
const noise_cicada_species_info noise_cicada_species[NOISE_CICADA_SPECIES_COUNT] = {
  [CICADA_DOG_DAY] = {"Dog-day (US)", "dog-day", 5000.0f},
  [CICADA_MINMINZEMI] = {"Minminzemi (JP)", "minminzemi", 5000.0f},
  [CICADA_HIGURASHI] = {"Higurashi (JP)", "higurashi", 5000.0f},
  [CICADA_ABURAZEMI] = {"Aburazemi (JP)", "aburazemi", 4500.0f},
  [CICADA_NIINIIZEMI] = {"Niiniizemi (JP)", "niiniizemi", 7500.0f},
  [CICADA_KUMAZEMI] = {"Kumazemi (JP)", "kumazemi", 5000.0f},
  [CICADA_PHARAOH] = {"Pharaoh cicada (US)", "pharaoh", 1400.0f},
  [CICADA_SCISSOR_GRINDER] = {"Scissor grinder (US)", "scissor-grinder", 5500.0f},
  [CICADA_CIGALE_GRISE] = {"Cigale grise (FR)", "cigale-grise", 4500.0f},
  [CICADA_GREEN_GROCER] = {"Green grocer (AU)", "green-grocer", 4000.0f},
};

static const cicada_song cicada_songs[NOISE_CICADA_SPECIES_COUNT] = {
  /* Dog-day: one long buzz that swells, pulses, and winds down. */
  {300.0f, 6.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {10.0f, 18.0f}, 0.4f, 20.0f},
  /* Minminzemi: rising "min" syllables, then a long falling "miiin". */
  {400.0f, 20.0f, {3.0f, 3.0f}, 0.7f, -0.04f, {5.0f, 15.0f}, 1.0f, {1.0f, 2.0f}, 0.2f, 8.0f},
  /* Higurashi: tonal falling "kana" pulses that slow and fade. */
  {500.0f, 30.0f, {8.0f, 6.0f}, 0.5f, 0.05f, {20.0f, 40.0f}, 0.3f, {0.0f, 0.0f}, 0.0f, 15.0f},
  /* Aburazemi: a long sizzle like frying oil. */
  {450.0f, 4.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {5.0f, 20.0f}, 0.1f, 10.0f},
  /* Niiniizemi: a long, thin, high "chiii". */
  {500.0f, 15.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {10.0f, 30.0f}, 0.05f, 10.0f},
  /* Kumazemi: loud "shah-shah-shah" bursts. */
  {400.0f, 5.0f, {4.0f, 4.0f}, 0.6f, 0.0f, {20.0f, 40.0f}, 1.0f, {0.0f, 0.0f}, 0.0f, 10.0f},
  /* Pharaoh: a short low buzz that falls at the end, "phaaa-roah". */
  {300.0f, 10.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {1.0f, 3.0f}, 0.0f, 5.0f},
  /* Scissor grinder: a buzz pulsing like a grinding wheel. */
  {300.0f, 6.0f, {5.0f, 5.0f}, 0.8f, 0.0f, {50.0f, 100.0f}, 1.0f, {0.0f, 0.0f}, 0.0f, 20.0f},
  /* Cigale grise: long trains of short chirps. */
  {400.0f, 8.0f, {8.0f, 8.0f}, 0.4f, 0.0f, {80.0f, 200.0f}, 1.0f, {0.0f, 0.0f}, 0.0f, 10.0f},
  /* Green grocer: a long, steady drone. */
  {450.0f, 8.0f, {1.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f}, 1.0f, {15.0f, 30.0f}, 0.15f, 15.0f},
};

int noise_cicada_config_valid(const noise_cicada_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         (unsigned)c->species < NOISE_CICADA_SPECIES_COUNT &&
         in_range(c->pitch_hz, 1000.0f, 10000.0f) &&
         in_range(c->click_rate_scale, 0.5f, 1.5f) &&
         in_range(c->chorus, 0.0f, 1.0f) &&
         noise_placement_valid(&c->placement) &&
         in_range(c->min_temperature_c, -10.0f, 45.0f) &&
         in_range(c->max_rain_mm_h, 0.0f, 200.0f);
}

void noise_cicada_config_default(noise_cicada_config *c) {
  c->pitch_hz = 5000.0f;
  c->click_rate_scale = 1.0f;
  c->chorus = 0.35f;
  c->placement.stereo_width = 0.75f;
  c->placement.min_distance_m = 5.0f;
  c->placement.max_distance_m = 30.0f;
  c->min_temperature_c = 22.0f;
  c->max_rain_mm_h = 0.5f;
}

void noise_cicada_set_species(noise_cicada_config *c, cicada_species species) {
  c->species = species;
  c->pitch_hz = noise_cicada_species[species].pitch_hz;
}

void noise_cicadas_init(noise_cicadas *cicadas, uint32_t seed) {
  cicadas->rng = stream_seed(seed, 0x94d049bbu);
  cicadas->swell = 0.5f;
  cicadas->swell_target = 0.5f;
}

void noise_cicadas_follow(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_weather *weather) {
  cicadas->quiet = (weather->temperature_c < c->min_temperature_c ? NOISE_QUIET_COLD : 0u) |
                   (weather->rain_mm_h > c->max_rain_mm_h ? NOISE_QUIET_RAIN : 0u);
}

static uint32_t cicada_swell(uint32_t hold) {
  uint32_t limit = NOISE_SAMPLE_RATE_HZ;
  return hold / 4u < limit ? hold / 4u : limit;
}

static uint32_t cicada_wind_down(uint32_t hold) {
  uint32_t limit = 2u * NOISE_SAMPLE_RATE_HZ;
  return hold / 2u < limit ? hold / 2u : limit;
}

/* Tunes the body to this cicada's pitch and glides by drop over the given frames. */
static void cicada_tune(noise_cicada_voice *voice, const noise_cicada_config *c,
                        float drop, uint32_t frames) {
  const cicada_song *song = &cicada_songs[c->species];
  float pitch = c->pitch_hz * (1.0f + 0.05f * voice->pitch_offset);
  resonator_tune(&voice->body, pitch, song->q);
  float radius = sqrtf(voice->body.radius_squared);
  float end = 2.0f * radius * cosf(2.0f * NOISE_PI * pitch * (1.0f - drop) /
                                   NOISE_SAMPLE_RATE_HZ);
  voice->glide = frames ? (end - voice->body.coefficient) / (float)frames : 0.0f;
}

static void cicada_rest(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c) {
  float gap_s = cicada_songs[c->species].gap_s;
  voice->note_length = 0;
  voice->until_call = 1u + (uint32_t)(-gap_s * NOISE_SAMPLE_RATE_HZ *
                                      logf(1.0f - random_unit(rng)));
}

/* Starts syllable number voice->syllable, the held note after the last, or rest. */
static void cicada_note(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c) {
  const cicada_song *song = &cicada_songs[c->species];
  voice->note_samples = 0;
  if (voice->syllable < voice->syllables) {
    float progress = voice->syllables > 1 ?
        (float)voice->syllable / (float)(voice->syllables - 1) : 0.0f;
    float rate = song->syllable_hz[0] + progress * (song->syllable_hz[1] - song->syllable_hz[0]);
    voice->note_length = (uint32_t)(NOISE_SAMPLE_RATE_HZ / rate);
    voice->sounding = (uint32_t)(song->duty * (float)voice->note_length);
    voice->holding = 0;
    cicada_tune(voice, c, song->syllable_drop, voice->sounding);
    return;
  }
  float hold_s = random_between(rng, song->hold_s[0], song->hold_s[1]);
  if (hold_s <= 0.0f) {
    cicada_rest(rng, voice, c);
    return;
  }
  voice->note_length = (uint32_t)(hold_s * NOISE_SAMPLE_RATE_HZ);
  voice->sounding = voice->note_length;
  voice->holding = 1;
  cicada_tune(voice, c, CICADA_DROP, cicada_wind_down(voice->note_length));
  oscillator_init(&voice->throb, random_between(rng, 2.0f, 4.0f));
}

static void cicada_call(uint32_t *rng, noise_cicada_voice *voice, const noise_cicada_config *c,
                        const noise_listener_config *listener) {
  const cicada_song *song = &cicada_songs[c->species];
  position_polar position =
      noise_placement_position(&c->placement, voice->distance_offset, voice->angle_offset);
  noise_spatial_init(&voice->spatial, listener, position);
  float span = song->syllables[1] - song->syllables[0] + 1.0f;
  voice->syllables = (unsigned)(song->syllables[0] + span * random_unit(rng));
  voice->syllable = 0;
  memset(&voice->body, 0, sizeof(voice->body));
  voice->until_click = 0.0f;
  cicada_note(rng, voice, c);
}

/* Returns the reverb send; the direct sound goes through the spatial model. */
static float cicada_next(noise_cicadas *cicadas, noise_cicada_voice *voice,
                         const noise_cicada_config *c,
                         const noise_listener_config *listener, noise_bus *bus) {
  uint32_t *rng = &cicadas->rng;
  if (!voice->note_length && --voice->until_call == 0) {
    if (cicadas->quiet) {
      cicada_rest(rng, voice, c);
    } else {
      cicada_call(rng, voice, c, listener);
    }
  }
  float sample = 0.0f;
  if (voice->note_length) {
    const cicada_song *song = &cicada_songs[c->species];
    uint32_t t = voice->note_samples++;
    float envelope = 0.0f;
    float click_rate = 1.0f;
    float impulse = 0.0f;
    if (t < voice->sounding) {
      if (voice->holding) {
        float swell = (float)cicada_swell(voice->sounding);
        float wind_down = (float)cicada_wind_down(voice->sounding);
        float remaining = (float)(voice->sounding - voice->note_samples);
        envelope = (float)t < swell ? (float)t / swell : 1.0f;
        if (remaining < wind_down) {
          float fraction = remaining / wind_down;
          envelope *= fraction;
          click_rate -= CICADA_DROP * (1.0f - fraction);
          voice->body.coefficient += voice->glide;
        }
        envelope *= 1.0f - song->throb * 0.5f * (1.0f + oscillator_next(&voice->throb));
      } else {
        float x = ((float)t + 0.5f) / (float)voice->sounding;
        float level = voice->syllables > 1 ? 1.0f + (song->fade - 1.0f) *
            (float)voice->syllable / (float)(voice->syllables - 1) : 1.0f;
        envelope = 4.0f * x * (1.0f - x) * level;
        voice->body.coefficient += voice->glide;
      }
      voice->until_click -= click_rate;
      if (voice->until_click <= 0.0f) {
        impulse = CICADA_CLICK;
        voice->until_click += NOISE_SAMPLE_RATE_HZ /
            (song->click_rate_hz * c->click_rate_scale) *
            random_between(rng, 1.0f - CICADA_JITTER, 1.0f + CICADA_JITTER);
      }
    }
    sample = CICADA_CALIBRATION * CICADA_LEVEL * c->gain * envelope *
             resonator_next(&voice->body, impulse);
    if (voice->note_samples == voice->note_length) {
      if (voice->holding) {
        cicada_rest(rng, voice, c);
      } else {
        ++voice->syllable;
        cicada_note(rng, voice, c);
      }
    }
  }
  /* Runs between calls too, so filter tails decay instead of holding. */
  return noise_spatial_next(&voice->spatial, listener, bus, sample);
}

/* A crowd at spread pitches blurs into a band wider than one body. */
static float cicada_chorus_q(const noise_cicada_config *c) {
  return fmaxf(3.0f, 0.5f * cicada_songs[c->species].q);
}

void noise_cicadas_configure(noise_cicadas *cicadas, const noise_cicada_config *c) {
  for (unsigned ear = 0; ear < 2; ++ear) {
    resonator_tune(&cicadas->chorus[ear], c->pitch_hz, cicada_chorus_q(c));
  }
}

static void cicada_chorus_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                               noise_bus *bus) {
  uint32_t *rng = &cicadas->rng;
  float q = cicada_chorus_q(c);
  if (cicadas->swell_samples == 0) {
    cicadas->swell_target = random_between(rng, 0.3f, 1.0f);
    cicadas->swell_samples = 4u * NOISE_SAMPLE_RATE_HZ;
  }
  --cicadas->swell_samples;
  cicadas->swell += CICADA_SWELL_ALPHA * (cicadas->swell_target - cicadas->swell);
  cicadas->activity += CICADA_ACTIVITY_ALPHA *
                       ((cicadas->quiet ? 0.0f : 1.0f) - cicadas->activity);
  /* Band-passed noise power grows with Q; this holds the level at Q 3. */
  float level = CICADA_CALIBRATION * CICADA_CHORUS_LEVEL * c->gain * c->chorus * cicadas->swell *
                cicadas->activity * sqrtf(3.0f / q);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float noise = 2.0f * random_unit(rng) - 1.0f;
    bus->direct[ear][bus->position] += level * resonator_next(&cicadas->chorus[ear], noise);
  }
}

float noise_cicadas_next(noise_cicadas *cicadas, const noise_cicada_config *c,
                          const noise_listener_config *listener, noise_bus *bus) {
  if (c->gain <= 0.0f) return 0.0f;
  uint32_t *rng = &cicadas->rng;
  if (!cicadas->started) {
    cicadas->started = 1;
    for (unsigned i = 0; i < NOISE_CICADA_VOICES; ++i) {
      noise_cicada_voice *voice = &cicadas->voice[i];
      voice->pitch_offset = random_between(rng, -1.0f, 1.0f);
      voice->angle_offset = random_between(rng, -1.0f, 1.0f);
      voice->distance_offset = random_unit(rng);
      cicada_rest(rng, voice, c);
    }
    /* The layer is audible from its first frame unless the weather silences it. */
    cicadas->voice[0].until_call = 1;
    cicadas->activity = cicadas->quiet ? 0.0f : 1.0f;
  }
  float send = 0.0f;
  for (unsigned i = 0; i < NOISE_CICADA_VOICES; ++i) {
    send += cicada_next(cicadas, &cicadas->voice[i], c, listener, bus);
  }
  cicada_chorus_next(cicadas, c, bus);
  return send;
}
