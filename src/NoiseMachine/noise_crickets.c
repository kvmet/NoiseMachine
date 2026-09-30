#include "noise_crickets.h"

#include <math.h>

#include "noise_internal.h"

#define CRICKET_PULSE_DROP 0.03f /* Carrier falls through each pulse as the wing slows. */
#define CRICKET_SINGING_S 30.0f /* Mean bout lengths. */
#define CRICKET_SILENT_S 10.0f
/* Gain 1 at the reference condition in test_mix.c reads -24 LUFS. */
#define CRICKET_CALIBRATION 1.315f

int noise_cricket_config_valid(const noise_cricket_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->call_rate_scale, 0.1f, 2.0f) &&
         in_range(c->pitch_hz, 2000.0f, 8000.0f) &&
         in_range(c->pitch_variation, 0.0f, 1.0f) &&
         in_range(c->min_temperature_c, -10.0f, 45.0f) &&
         in_range(c->max_rain_mm_h, 0.0f, 200.0f) &&
         in_range(c->max_wind_m_s, 0.0f, 40.0f) &&
         noise_placement_valid(&c->placement);
}

void noise_cricket_config_default(noise_cricket_config *c) {
  c->call_rate_scale = 0.5f;
  c->pitch_hz = 4500.0f;
  c->pitch_variation = 0.35f;
  c->placement.stereo_width = 0.8f;
  c->placement.min_distance_m = 2.0f;
  c->placement.max_distance_m = 15.0f;
  c->min_temperature_c = 13.0f;
  c->max_rain_mm_h = 0.5f;
  c->max_wind_m_s = 8.0f;
}

void noise_crickets_init(noise_crickets *crickets, uint32_t seed) {
  crickets->rng = stream_seed(seed, 0xb54cda58u);
}

void noise_crickets_follow(noise_crickets *crickets, const noise_cricket_config *c,
                           const noise_weather *weather) {
  float t = weather->temperature_c;
  /* Dolbear's law: chirps per minute = 4 T_F - 160, which is 7.2 T_C - 32. */
  float rate = c->call_rate_scale * (7.2f * t - 32.0f) / 60.0f;
  crickets->call_rate_hz = fminf(10.0f, fmaxf(0.05f, rate));
  crickets->quiet = (t < c->min_temperature_c ? NOISE_QUIET_COLD : 0u) |
                    (weather->rain_mm_h > c->max_rain_mm_h ? NOISE_QUIET_RAIN : 0u) |
                    (weather->wind_mean_m_s > c->max_wind_m_s ? NOISE_QUIET_WIND : 0u);
}

static uint32_t cricket_bout(uint32_t *rng, unsigned singing) {
  float mean_s = singing ? CRICKET_SINGING_S : CRICKET_SILENT_S;
  return 1u + (uint32_t)(-mean_s * NOISE_SAMPLE_RATE_HZ * logf(1.0f - random_unit(rng)));
}

static uint32_t cricket_period(uint32_t *rng, const noise_cricket_voice *voice,
                               float call_rate_hz) {
  float period = voice->period_scale * random_between(rng, 0.97f, 1.03f) *
                 NOISE_SAMPLE_RATE_HZ / call_rate_hz;
  uint32_t chirp = voice->pulses * voice->pulse_samples;
  return period > (float)chirp ? (uint32_t)period : chirp;
}

static void cricket_init(uint32_t *rng, noise_cricket_voice *voice, float call_rate_hz) {
  voice->pitch_offset = random_between(rng, -1.0f, 1.0f);
  voice->angle_offset = random_between(rng, -1.0f, 1.0f);
  voice->distance_offset = random_unit(rng);
  voice->period_scale = random_between(rng, 0.9f, 1.1f);
  voice->pulses = 3u + random_u32(rng) % 3u;
  voice->pulse_samples = (uint32_t)(NOISE_SAMPLE_RATE_HZ * random_between(rng, 0.026f, 0.036f));
  voice->sounding_samples = (uint32_t)(voice->pulse_samples * random_between(rng, 0.55f, 0.70f));
  voice->chirp_samples = voice->pulses * voice->pulse_samples;
  voice->singing = random_unit(rng) < CRICKET_SINGING_S / (CRICKET_SINGING_S + CRICKET_SILENT_S);
  voice->bout_samples = cricket_bout(rng, voice->singing);
  float phase = random_unit(rng);
  voice->until_chirp = (uint32_t)(phase * (float)cricket_period(rng, voice, call_rate_hz));
}

static void cricket_place(noise_cricket_voice *voice, const noise_cricket_config *c,
                          const noise_listener_config *listener) {
  position_polar position =
      noise_placement_position(&c->placement, voice->distance_offset, voice->angle_offset);
  noise_spatial_init(&voice->spatial, listener, position);
}

/* Returns the reverb send; the direct sound goes through the spatial model. */
static float cricket_next(noise_crickets *crickets, noise_cricket_voice *voice,
                          const noise_cricket_config *c,
                          const noise_listener_config *listener, noise_bus *bus) {
  uint32_t *rng = &crickets->rng;
  if (--voice->bout_samples == 0) {
    voice->singing = !voice->singing;
    voice->bout_samples = cricket_bout(rng, voice->singing);
  }
  if (voice->until_chirp == 0) {
    voice->until_chirp = cricket_period(rng, voice, crickets->call_rate_hz);
    if (voice->singing && !crickets->quiet) {
      voice->chirp_samples = 0;
      cricket_place(voice, c, listener);
    }
  }
  --voice->until_chirp;
  float sample = 0.0f;
  if (voice->chirp_samples < voice->pulses * voice->pulse_samples) {
    uint32_t within_pulse = voice->chirp_samples % voice->pulse_samples;
    ++voice->chirp_samples;
    if (within_pulse == 0) {
      float pitch = c->pitch_hz * (1.0f + 0.3f * c->pitch_variation * voice->pitch_offset);
      oscillator_init(&voice->oscillator, pitch);
      float end = 2.0f * cosf(2.0f * NOISE_PI * pitch * (1.0f - CRICKET_PULSE_DROP) /
                              NOISE_SAMPLE_RATE_HZ);
      voice->glide = (end - voice->oscillator.coefficient) / (float)voice->sounding_samples;
    }
    if (within_pulse < voice->sounding_samples) {
      float carrier = oscillator_next(&voice->oscillator);
      voice->oscillator.coefficient += voice->glide;
      uint32_t attack = voice->sounding_samples / 4u;
      float envelope = within_pulse < attack ?
          (float)within_pulse / (float)attack :
          (float)(voice->sounding_samples - within_pulse) /
          (float)(voice->sounding_samples - attack);
      float tone = carrier - 0.22f * carrier * carrier * carrier;
      sample = CRICKET_CALIBRATION * c->gain * envelope * tone;
    }
  }
  /* Runs between chirps too, so filter tails decay instead of holding. */
  return noise_spatial_next(&voice->spatial, listener, bus, sample);
}

float noise_crickets_next(noise_crickets *crickets, const noise_cricket_config *c,
                           const noise_listener_config *listener, noise_bus *bus) {
  if (c->gain <= 0.0f) return 0.0f;
  if (!crickets->started) {
    crickets->started = 1;
    for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
      cricket_init(&crickets->rng, &crickets->voice[i], crickets->call_rate_hz);
      cricket_place(&crickets->voice[i], c, listener);
    }
    /* The layer is audible from its first frame. */
    crickets->voice[0].singing = 1;
    crickets->voice[0].until_chirp = 0;
  }
  float send = 0.0f;
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
    send += cricket_next(crickets, &crickets->voice[i], c, listener, bus);
  }
  return send;
}
