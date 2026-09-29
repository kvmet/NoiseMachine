#include "noise_weather.h"

#include <math.h>

#include "noise_internal.h"

int noise_weather_config_valid(const noise_weather_config *c) {
  if (!in_range(c->intensity, 0.0f, 1.0f) ||
      !in_range(c->min_intensity, 0.0f, 1.0f) ||
      !in_range(c->max_intensity, c->min_intensity, 1.0f) ||
      (c->vary != 0 && c->vary != 1) ||
      !in_range(c->step_s, 0.1f, 3600.0f) ||
      !in_range(c->slew_s, 0.01f, 60.0f)) {
    return 0;
  }
  if (c->vary && !in_range(c->intensity, c->min_intensity, c->max_intensity)) return 0;
  for (unsigned i = 0; i < NOISE_WEATHER_MOD_COUNT; ++i) {
    if (!in_range(c->mod_amount[i], -1.0f, 1.0f)) return 0;
  }
  return 1;
}

void noise_weather_config_default(noise_weather_config *c) {
  c->min_intensity = 0.15f;
  c->max_intensity = 0.85f;
  c->step_s = 8.0f;
  c->slew_s = 2.0f;
  c->mod_amount[WEATHER_MOD_ARRIVAL_RATE] = 1.0f;
  c->mod_amount[WEATHER_MOD_DROP_SIZE] = 1.0f;
}

/* Jumps to the configured intensity and the Markov state nearest it. */
static void weather_start(noise_weather *weather, noise_state *state,
                          const noise_weather_config *c) {
  state->rain_intensity = c->intensity;
  state->rain_target = c->intensity;
  float span = c->max_intensity - c->min_intensity;
  float relative = span > 0.0f ? (c->intensity - c->min_intensity) / span : 0.0f;
  state->weather_state = relative < 0.25f ? 0u : (relative < 0.75f ? 1u : 2u);
  weather->samples = 0;
}

void noise_weather_init(noise_weather *weather, noise_state *state,
                         const noise_weather_config *c, uint32_t seed) {
  weather->rng = stream_seed(seed, 0x78dde6e4u);
  weather_start(weather, state, c);
}

void noise_weather_configure(noise_weather *weather, noise_state *state,
                              const noise_weather_config *previous,
                              const noise_weather_config *c) {
  weather->period = (uint32_t)(c->step_s * NOISE_SAMPLE_RATE_HZ);
  /* noise_weather_next only steps when samples reaches period exactly. */
  if (weather->samples >= weather->period) weather->samples = 0;
  weather->slew = -expm1f(-1.0f / (c->slew_s * NOISE_SAMPLE_RATE_HZ));
  if (c->intensity != previous->intensity || c->vary != previous->vary) {
    weather_start(weather, state, c);
  } else if (c->vary) {
    state->rain_intensity = fminf(c->max_intensity, fmaxf(c->min_intensity, state->rain_intensity));
    state->rain_target = fminf(c->max_intensity, fmaxf(c->min_intensity, state->rain_target));
  }
}

void noise_weather_next(noise_weather *weather, const noise_weather_config *c,
                         noise_state *state) {
  if (!c->vary) return;
  if (++weather->samples == weather->period) {
    static const float cdf[3][2] = {{0.85f, 1.0f}, {0.10f, 0.90f}, {0.0f, 0.15f}};
    weather->samples = 0;
    float choice = random_unit(&weather->rng);
    unsigned from = state->weather_state;
    unsigned to = choice < cdf[from][0] ? 0u : (choice < cdf[from][1] ? 1u : 2u);
    state->weather_state = to;
    state->rain_target = c->min_intensity + 0.5f * (float)to *
        (c->max_intensity - c->min_intensity);
  }
  /* Carry sub-ULP steps so long time constants still reach their target. */
  float step = weather->slew * (state->rain_target - state->rain_intensity) +
               weather->slew_error;
  float next = state->rain_intensity + step;
  weather->slew_error = step - (next - state->rain_intensity);
  state->rain_intensity = next;
}

float noise_weather_mod_linear(float base, float amount, float intensity,
                                float low, float high) {
  float value = base + amount * (intensity - 0.5f) * (high - low);
  return fminf(high, fmaxf(low, value));
}

float noise_weather_mod_log(float base, float amount, float intensity,
                             float low, float high) {
  float value = logf(base) + amount * (intensity - 0.5f) * (logf(high) - logf(low));
  return fminf(high, fmaxf(low, expf(value)));
}

float noise_weather_arrival_level(float intensity, float amount) {
  if (amount >= 0.0f) return 1.0f - amount + amount * intensity;
  return 1.0f + amount - amount * (1.0f - intensity);
}
