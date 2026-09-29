#include "noise_core.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

static int config_valid(const noise_config *c) {
  if (!c || !in_range(c->master_gain, 0.0f, 1.0f) || !in_range(c->reverb_gain, 0.0f, 1.0f)) {
    return 0;
  }
  for (unsigned i = 0; i < NOISE_KIND_COUNT; ++i) {
    if (!in_range(c->ambient_gain[i], 0.0f, 1.0f)) return 0;
  }
  return noise_listener_config_valid(&c->listener) &&
         noise_weather_config_valid(&c->weather) &&
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
  noise_weather_config_default(&c->weather);
  noise_rain_config_default(&c->rain);
  noise_wind_config_default(&c->wind);
  noise_cricket_config_default(&c->crickets);
  noise_cicada_config_default(&c->cicadas);
  noise_thunder_config_default(&c->thunder);
}

/* Derives every coefficient that depends on gen->config. */
static void configure(noise_gen *gen, const noise_config *previous) {
  const noise_config *c = &gen->config;
  noise_wind_configure(&gen->wind, &c->wind);
  noise_cicadas_configure(&gen->cicadas, &c->cicadas);
  noise_thunder_configure(&gen->thunder, &c->thunder);
  noise_weather_configure(&gen->weather, &gen->state, &previous->weather, &c->weather);
  noise_rain_configure(&gen->rain, &c->rain);
}

noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed) {
  if (!gen || !config_valid(config)) return NOISE_INVALID_CONFIG;
  noise_config copy = *config;
  memset(gen, 0, sizeof(*gen));
  gen->config = copy;
  seed = seed ? seed : 1u;
  noise_ambient_init(&gen->ambient, seed);
  noise_wind_init(&gen->wind, seed);
  noise_crickets_init(&gen->crickets, seed);
  noise_cicadas_init(&gen->cicadas, seed);
  noise_thunder_init(&gen->thunder, seed);
  noise_weather_init(&gen->weather, &gen->state, &gen->config.weather, seed);
  noise_rain_init(&gen->rain, seed);
  noise_reverb_init(&gen->reverb);
  configure(gen, &gen->config);
  return NOISE_OK;
}

noise_result noise_set_config(noise_gen *gen, const noise_config *config) {
  if (!gen || !config_valid(config)) return NOISE_INVALID_CONFIG;
  noise_config previous = gen->config;
  gen->config = *config;
  configure(gen, &previous);
  return NOISE_OK;
}

noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop) {
  if (!gen || !drop || !noise_drop_valid(drop)) return NOISE_INVALID_DROP;
  return noise_rain_start_drop(&gen->rain, &gen->state, &gen->config.rain,
                               &gen->config.listener, drop);
}

noise_result noise_trigger_thunder(noise_gen *gen, const thunder_strike *strike) {
  if (!gen || !strike ||
      !in_range(strike->position.distance_m, 200.0f, 15000.0f) ||
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
    noise_weather_next(&gen->weather, &c->weather, &gen->state);
    float send = noise_rain_next(&gen->rain, &gen->state, &c->rain, &c->weather, &c->listener,
                                 &gen->bus);
    send += noise_crickets_next(&gen->crickets, &c->crickets, &c->listener, &gen->bus);
    send += noise_cicadas_next(&gen->cicadas, &c->cicadas, &c->listener, &gen->bus);
    float left, right;
    noise_bus_next(&gen->bus, &left, &right);
    noise_thunder_next(&gen->thunder, &c->thunder, &gen->state, &left, &right);
    if (c->reverb_gain > 0.0f || c->weather.mod_amount[WEATHER_MOD_REVERB_GAIN] != 0.0f) {
      float reverb_gain = noise_weather_mod_linear(c->reverb_gain,
          c->weather.mod_amount[WEATHER_MOD_REVERB_GAIN], gen->state.rain_intensity,
          0.0f, 1.0f);
      noise_reverb_next(&gen->reverb, send, reverb_gain, &left, &right);
    }
    float ambient = noise_ambient_next(&gen->ambient, c->ambient_gain);
    left += ambient;
    right += ambient;
    noise_wind_next(&gen->wind, &c->wind, &left, &right);
    out[2 * frame] = to_sample(&gen->state, left * c->master_gain);
    out[2 * frame + 1] = to_sample(&gen->state, right * c->master_gain);
  }
  return frames;
}
