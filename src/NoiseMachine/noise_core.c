#include "noise_core.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

int noise_config_valid(const noise_config *c) {
  if (!c || !in_range(c->master_gain, 0.0f, 1.0f) || !in_range(c->reverb_gain, 0.0f, 1.0f)) {
    return 0;
  }
  for (unsigned i = 0; i < NOISE_KIND_COUNT; ++i) {
    if (!in_range(c->ambient_gain[i], 0.0f, 1.0f)) return 0;
  }
  return noise_listener_config_valid(&c->listener) &&
         noise_storm_config_valid(&c->storm) &&
         noise_rain_config_valid(&c->rain) &&
         noise_wind_config_valid(&c->wind) &&
         noise_cricket_config_valid(&c->crickets) &&
         noise_cicada_config_valid(&c->cicadas) &&
         noise_thunder_config_valid(&c->thunder);
}

void noise_config_default(noise_config *c) {
  memset(c, 0, sizeof(*c));
  c->master_gain = 0.8f;
  c->ambient_gain[NOISE_KIND_PINK] = 0.3f;
  c->reverb_gain = 0.12f;
  c->listener.stereo_width_m = 0.18f;
  c->listener.head_amount = 1.0f;
  c->listener.rear_amount = 1.0f;
  noise_storm_config_default(&c->storm);
  noise_rain_config_default(&c->rain);
  noise_wind_config_default(&c->wind);
  noise_cricket_config_default(&c->crickets);
  noise_cicada_config_default(&c->cicadas);
  noise_thunder_config_default(&c->thunder);
}

/* Hands the current weather to every module that depends on it. */
static void follow_weather(noise_gen *gen) {
  const noise_config *c = &gen->config;
  const noise_weather *weather = &gen->state.weather;
  noise_wind_follow(&gen->wind, &c->wind, weather);
  noise_crickets_follow(&gen->crickets, &c->crickets, weather);
  noise_cicadas_follow(&gen->cicadas, &c->cicadas, weather);
  noise_rain_follow(&gen->rain, &c->rain, weather);
}

/* Derives every coefficient that depends on gen->config. */
static void configure(noise_gen *gen, const noise_config *previous) {
  const noise_config *c = &gen->config;
  noise_cicadas_configure(&gen->cicadas, &c->cicadas);
  noise_thunder_configure(&gen->thunder, &c->thunder);
  noise_storm_configure(&gen->storm, &previous->storm, &c->storm, &gen->state.weather);
  noise_rain_configure(&gen->rain, &c->rain);
  follow_weather(gen);
}

noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed) {
  if (!gen || !noise_config_valid(config)) return NOISE_INVALID_CONFIG;
  noise_config copy = *config;
  memset(gen, 0, sizeof(*gen));
  gen->config = copy;
  seed = seed ? seed : 1u;
  noise_ambient_init(&gen->ambient, seed);
  noise_wind_init(&gen->wind, seed);
  noise_crickets_init(&gen->crickets, seed);
  noise_cicadas_init(&gen->cicadas, seed);
  noise_thunder_init(&gen->thunder, seed);
  noise_storm_init(&gen->storm, &gen->config.storm, &gen->state.weather, seed);
  noise_rain_init(&gen->rain, seed);
  noise_reverb_init(&gen->reverb);
  configure(gen, &gen->config);
  return NOISE_OK;
}

noise_result noise_set_config(noise_gen *gen, const noise_config *config) {
  if (!gen || !noise_config_valid(config)) return NOISE_INVALID_CONFIG;
  noise_config previous = gen->config;
  gen->config = *config;
  configure(gen, &previous);
  return NOISE_OK;
}

noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop) {
  if (!gen || !drop || !noise_drop_valid(&gen->config.rain, drop)) return NOISE_INVALID_DROP;
  return noise_rain_start_drop(&gen->rain, &gen->state, &gen->config.rain,
                               &gen->config.listener, drop);
}

noise_result noise_trigger_thunder(noise_gen *gen, const thunder_strike *strike) {
  if (!gen || !strike ||
      !in_range(strike->position.distance_m, 200.0f, NOISE_THUNDER_MAX_DISTANCE_M) ||
      !in_range(strike->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI)) {
    return NOISE_INVALID_STRIKE;
  }
  return noise_thunder_start(&gen->thunder, &gen->state, strike->position);
}

static int16_t to_sample(noise_state *state, float value) {
  if (value > 1.0f) {
    ++state->clipped_samples;
    return INT16_MAX;
  }
  if (value < -1.0f) {
    ++state->clipped_samples;
    return INT16_MIN;
  }
  return (int16_t)(value * 32767.0f);
}

size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames) {
  const noise_config *c = &gen->config;
  for (size_t frame = 0; frame < frames; ++frame) {
    if (noise_storm_next(&gen->storm, &c->storm, &gen->state.weather)) {
      follow_weather(gen);
      noise_rain_update_bed(&gen->rain);
    }
    float send = noise_rain_next(&gen->rain, &gen->state, &c->rain, &c->listener, &gen->bus);
    send += noise_crickets_next(&gen->crickets, &c->crickets, &c->listener, &gen->bus);
    send += noise_cicadas_next(&gen->cicadas, &c->cicadas, &c->listener, &gen->bus);
    float left, right;
    noise_bus_next(&gen->bus, &left, &right);
    noise_thunder_next(&gen->thunder, &c->thunder, &gen->state, &left, &right);
    if (c->reverb_gain > 0.0f) noise_reverb_next(&gen->reverb, send, c->reverb_gain, &left, &right);
    float ambient = noise_ambient_next(&gen->ambient, c->ambient_gain);
    left += ambient;
    right += ambient;
    noise_wind_next(&gen->wind, &c->wind, &left, &right);
    out[2 * frame] = to_sample(&gen->state, left * c->master_gain);
    out[2 * frame + 1] = to_sample(&gen->state, right * c->master_gain);
  }
  return frames;
}

void noise_get_status(const noise_gen *gen, noise_status *status) {
  const noise_rain *rain = &gen->rain;
  status->weather = gen->state.weather;
  status->rain_arrivals_per_s = rain->arrivals_per_s;
  status->rain_played_per_s = rain->arrival_probability * NOISE_SAMPLE_RATE_HZ;
  status->bed_share = rain->bed.ratio / (1.0f + rain->bed.ratio);
  status->wind_level = gen->wind.relative_level;
  status->cricket_rate_hz = gen->crickets.call_rate_hz;
  status->cricket_quiet = gen->crickets.quiet;
  status->crickets_singing = 0;
  for (unsigned i = 0; i < NOISE_CRICKET_VOICES; ++i) {
    status->crickets_singing += gen->crickets.voice[i].singing != 0;
  }
  status->cicada_quiet = gen->cicadas.quiet;
  status->cicada_activity = gen->cicadas.activity;
}
