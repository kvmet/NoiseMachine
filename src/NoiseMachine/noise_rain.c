#include "noise_rain.h"

#include <math.h>
#include <string.h>

#include "noise_internal.h"

#define NOISE_WATER_DENSITY 1000.0f
#define NOISE_PRESSURE_PA 101325.0f

#define BUBBLE_RADIUS_MIN_M 0.00016f
#define BUBBLE_RADIUS_MAX_M 0.004f

/* Marshall-Palmer: N(D) = N0 exp(-lambda D) drops per cubic metre per mm of diameter. */
#define MP_N0 8000.0f
#define DIAMETER_MIN_MM 0.8f
#define DIAMETER_BIN_MM 0.1f

#define BED_LOWEST_HZ 125.0f
#define BED_SPACING 1.41421356f /* Half an octave between band centres. */
#define BED_Q 1.414f
#define BED_NOISE_VARIANCE (1.0f / 3.0f) /* Uniform draws on -1..1. */
#define BED_SMOOTHING (1.0f / (0.5f * NOISE_SAMPLE_RATE_HZ)) /* 0.5 s power average. */

/* Every default surface shares the click from [1, section 4.1.1]: 1 to 16 kHz, damping 2f. */
#define CLICK .click_frequency_min_hz = 1000.0f, .click_frequency_max_hz = 16000.0f, \
              .click_damping_ratio = 2.0f
/* Bubble fields a surface without bubbles still needs to be valid. */
#define NO_BUBBLE .bubble_radius_min_m = 0.00035f, .bubble_radius_max_m = 0.0016f, \
                  .bubble_gain_min = 1.0f, .bubble_gain_max = 1.0f, \
                  .bubble_decay_min = 1.0f, .bubble_decay_max = 1.0f, .bubble_delay_s = 0.002f
#define SOLID(label, share, click, f1, d1, f2, d2, resonance, lowpass) { \
    .name = label, .coverage = share, \
    .click_gain_min = click, .click_gain_max = click, CLICK, \
    .mode = {{f1, d1, resonance}, {f2, d2, 0.5f * resonance}}, \
    .detune = 0.15f, .lowpass_hz = lowpass, NO_BUBBLE}

static const noise_surface default_surfaces[NOISE_MAX_SURFACES] = {
  [WATER] = {
    .name = "Water", .coverage = 0.37f,
    .click_gain_min = 0.15f, .click_gain_max = 0.5f, CLICK,
    .mode = {{1000.0f, 1000.0f, 0.0f}, {1000.0f, 1000.0f, 0.0f}},
    .bubble_probability = 0.85f,
    .bubble_radius_min_m = 0.00035f, .bubble_radius_max_m = 0.0016f,
    .bubble_gain_min = 1.2f, .bubble_gain_max = 2.5f,
    .bubble_decay_min = 3.0f, .bubble_decay_max = 8.0f,
    .bubble_delay_s = 0.002f},
  [DIRT] = SOLID("Dirt", 0.21f, 1.0f, 450.0f, 1200.0f, 1100.0f, 1800.0f, 0.35f, 0.0f),
  [LEAF] = SOLID("Leaf", 0.26f, 1.0f, 1800.0f, 800.0f, 4200.0f, 1400.0f, 0.5f, 0.0f),
  [CONCRETE] = SOLID("Concrete", 0.15f, 1.0f, 1400.0f, 1400.0f, 3700.0f, 2200.0f, 0.45f, 0.0f),
  [GLASS] = SOLID("Glass", 0.005f, 1.0f, 3200.0f, 160.0f, 7100.0f, 260.0f, 0.325f, 0.0f),
  [METAL] = SOLID("Metal", 0.005f, 1.0f, 1700.0f, 90.0f, 4300.0f, 150.0f, 0.4f, 0.0f),
  [PLASTIC] = SOLID("Plastic", 0.0f, 0.5f, 220.0f, 110.0f, 650.0f, 220.0f, 0.65f, 1600.0f),
  [ASPHALT] = SOLID("Asphalt", 0.0f, 0.3f, 300.0f, 1600.0f, 900.0f, 2600.0f, 0.25f, 0.0f),
  [ASPHALT_ROOF] = SOLID("Asphalt roof", 0.0f, 0.25f, 140.0f, 300.0f, 420.0f, 700.0f, 0.4f, 900.0f),
};

static int mode_valid(const noise_surface_mode *m) {
  return in_range(m->frequency_hz, 20.0f, 20000.0f) &&
         in_range(m->damping_per_s, 1.0f, 20000.0f) &&
         in_range(m->gain, 0.0f, 4.0f);
}

static int surface_valid(const noise_surface *s) {
  return memchr(s->name, '\0', sizeof(s->name)) != NULL &&
         in_range(s->coverage, 0.0f, 1000.0f) &&
         in_range(s->click_gain_min, 0.0f, 2.0f) &&
         in_range(s->click_gain_max, s->click_gain_min, 2.0f) &&
         in_range(s->click_frequency_min_hz, 20.0f, 20000.0f) &&
         in_range(s->click_frequency_max_hz, s->click_frequency_min_hz, 20000.0f) &&
         in_range(s->click_damping_ratio, 0.05f, 50.0f) &&
         mode_valid(&s->mode[0]) && mode_valid(&s->mode[1]) &&
         in_range(s->detune, 0.0f, 0.5f) &&
         (s->lowpass_hz == 0.0f || in_range(s->lowpass_hz, 20.0f, 20000.0f)) &&
         in_range(s->bubble_probability, 0.0f, 1.0f) &&
         in_range(s->bubble_radius_min_m, BUBBLE_RADIUS_MIN_M, BUBBLE_RADIUS_MAX_M) &&
         in_range(s->bubble_radius_max_m, s->bubble_radius_min_m, BUBBLE_RADIUS_MAX_M) &&
         in_range(s->bubble_gain_min, 0.0f, 8.0f) &&
         in_range(s->bubble_gain_max, s->bubble_gain_min, 8.0f) &&
         in_range(s->bubble_decay_min, 0.25f, 20.0f) &&
         in_range(s->bubble_decay_max, s->bubble_decay_min, 20.0f) &&
         in_range(s->bubble_delay_s, 0.0f, 0.1f);
}

int noise_rain_config_valid(const noise_rain_config *c) {
  if (!in_range(c->gain, 0.0f, 1.0f) ||
      !in_range(c->max_drops_per_s, 0.0f, 2000.0f) ||
      !in_range(c->bed_gain, 0.0f, 4.0f) ||
      !in_range(c->min_distance_m, 0.25f, 100.0f) ||
      !in_range(c->max_distance_m, c->min_distance_m, 100.0f)) {
    return 0;
  }
  if (c->surface_count < 1 || c->surface_count > NOISE_MAX_SURFACES) return 0;
  float sum = 0.0f;
  for (unsigned i = 0; i < c->surface_count; ++i) {
    if (!surface_valid(&c->surface[i])) return 0;
    sum += c->surface[i].coverage;
  }
  return sum > 0.0f;
}

void noise_rain_config_default(noise_rain_config *c) {
  c->gain = 0.5f;
  c->max_drops_per_s = 900.0f;
  c->bed_gain = 1.0f;
  c->min_distance_m = 0.75f;
  c->max_distance_m = 5.0f;
  c->surface_count = NOISE_MAX_SURFACES;
  memcpy(c->surface, default_surfaces, sizeof(c->surface));
}

static float band_next(noise_biquad stage[NOISE_BED_STAGES], float x) {
  for (unsigned i = 0; i < NOISE_BED_STAGES; ++i) x = biquad_next(&stage[i], x);
  return x;
}

/* Power gain of one biquad at frequency. */
static float biquad_power(const noise_biquad *f, float frequency) {
  float w = 2.0f * NOISE_PI * frequency / NOISE_SAMPLE_RATE_HZ;
  float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);
  float num_re = f->b0 + f->b1 * c1 + f->b2 * c2, num_im = f->b1 * s1 + f->b2 * s2;
  float den_re = 1.0f + f->a1 * c1 + f->a2 * c2, den_im = f->a1 * s1 + f->a2 * s2;
  return (num_re * num_re + num_im * num_im) / (den_re * den_re + den_im * den_im);
}

static void bed_init(noise_rain_bed *bed, uint32_t seed) {
  bed->rng = stream_seed(seed, 0x9b05688cu);
  for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
    float center = BED_LOWEST_HZ * powf(BED_SPACING, (float)band);
    for (unsigned stage = 0; stage < NOISE_BED_STAGES; ++stage) {
      biquad_tune(&bed->analysis[band][stage], 1, center, BED_Q);
      for (unsigned ear = 0; ear < NOISE_CHANNELS; ++ear) {
        biquad_tune(&bed->synthesis[ear][band][stage], 1, center, BED_Q);
      }
    }
    /* Impulse response energy is the band's power for unit-variance white noise. */
    noise_biquad probe[NOISE_BED_STAGES];
    memcpy(probe, bed->analysis[band], sizeof(probe));
    float energy = 0.0f;
    for (unsigned n = 0; n < 8192u; ++n) {
      float y = band_next(probe, n == 0 ? 1.0f : 0.0f);
      energy += y * y;
    }
    bed->unit_power[band] = energy;
  }
  /* Rain sits between the outer bands, where the bank's summed gain is flat within
     2 dB; the thin edges would bias a full-band average low. */
  const unsigned points = 64;
  float low = 2.0f * BED_LOWEST_HZ;
  float high = BED_LOWEST_HZ * powf(BED_SPACING, (float)(NOISE_BED_BANDS - 3));
  bed->plateau = 0.0f;
  for (unsigned i = 0; i < points; ++i) {
    float frequency = low * powf(high / low, (float)i / (float)(points - 1));
    for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
      float gain = 1.0f;
      for (unsigned stage = 0; stage < NOISE_BED_STAGES; ++stage) {
        gain *= biquad_power(&bed->analysis[band][stage], frequency);
      }
      bed->plateau += gain / (float)points;
    }
  }
}

void noise_rain_init(noise_rain *rain, uint32_t seed) {
  rain->arrival_rng = stream_seed(seed, 0x3c6ef372u);
  rain->drop_rng = stream_seed(seed, 0xdaa66d2bu);
  rain->rain_mm_h = -1.0f;
  bed_init(&rain->bed, seed);
}

void noise_rain_configure(noise_rain *rain, const noise_rain_config *c) {
  float sum = 0.0f;
  for (unsigned i = 0; i < c->surface_count; ++i) sum += c->surface[i].coverage;
  float cumulative = 0.0f;
  for (unsigned i = 0; i < c->surface_count; ++i) {
    cumulative += c->surface[i].coverage;
    rain->surface_cdf[i] = cumulative / sum;
  }
}

/* Dingle-Lee fit reproduced in [1, section 4.1.1]; returns m/s. */
static float terminal_speed(float diameter_mm) {
  float d = diameter_mm;
  float cm_per_s = d <= 1.4f ?
      -17.8951f + d * (448.9498f + d * (16.3719f - 45.9516f * d)) :
       24.1660f + d * (448.8336f + d * (-75.6265f + 4.2695f * d));
  return 0.01f * cm_per_s;
}

/* Builds the flux-weighted size distribution: each bin holds N(D) v(D). */
static void build_sizes(noise_rain *rain, float rain_mm_h) {
  rain->rain_mm_h = rain_mm_h;
  if (rain_mm_h <= 0.0f) {
    rain->flux_per_m2_s = 0.0f;
    return;
  }
  float lambda = 4.1f * powf(rain_mm_h, -0.21f);
  float sum = 0.0f;
  for (unsigned i = 0; i < NOISE_RAIN_SIZE_BINS; ++i) {
    float d = DIAMETER_MIN_MM + DIAMETER_BIN_MM * ((float)i + 0.5f);
    sum += expf(-lambda * d) * terminal_speed(d);
    rain->size_cdf[i] = sum;
  }
  for (unsigned i = 0; i < NOISE_RAIN_SIZE_BINS; ++i) rain->size_cdf[i] /= sum;
  rain->size_cdf[NOISE_RAIN_SIZE_BINS - 1] = 1.0f;
  rain->flux_per_m2_s = MP_N0 * DIAMETER_BIN_MM * sum;
}

/* Integral of r times the squared distance gain 1/max(1, r) over radius, without
   the shared constants; power from an even-by-area ring is proportional to its span. */
static float ring_power(float r) {
  return r <= 1.0f ? 0.5f * r * r : 0.5f + logf(r);
}

void noise_rain_follow(noise_rain *rain, const noise_rain_config *c, const noise_weather *weather) {
  if (weather->rain_mm_h != rain->rain_mm_h) build_sizes(rain, weather->rain_mm_h);
  float near = c->min_distance_m, far = c->max_distance_m;
  float arrivals = rain->flux_per_m2_s * NOISE_PI * (far * far - near * near);
  rain->arrivals_per_s = arrivals;
  float played = fminf(arrivals, c->max_drops_per_s);
  rain->arrival_probability = played / NOISE_SAMPLE_RATE_HZ;
  noise_rain_bed *bed = &rain->bed;
  rain->near_m = far;
  bed->ratio = 0.0f;
  if (played > 0.0f && played < arrivals) {
    rain->near_m = fminf(far, sqrtf(near * near + played / (NOISE_PI * rain->flux_per_m2_s)));
    bed->ratio = (ring_power(far) - ring_power(rain->near_m)) /
                 (ring_power(rain->near_m) - ring_power(near));
  }
  for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
    bed->gain_scale[band] = bed->ratio /
        (BED_NOISE_VARIANCE * bed->unit_power[band] * bed->plateau);
  }
}

void noise_rain_update_bed(noise_rain *rain) {
  noise_rain_bed *bed = &rain->bed;
  for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
    bed->gain[band] = sqrtf(bed->gain_scale[band] * bed->power[band]);
  }
}

int noise_drop_valid(const noise_rain_config *c, const droplet *drop) {
  return drop->surface < c->surface_count &&
         in_range(drop->radius_m, 0.0004f, 0.0029f) &&
         in_range(drop->velocity_m_s, 0.0f, 12.0f) &&
         in_range(drop->position.distance_m, 0.25f, 100.0f) &&
         in_range(drop->position.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         (drop->bubble_radius_m == 0.0f ||
          in_range(drop->bubble_radius_m, BUBBLE_RADIUS_MIN_M, BUBBLE_RADIUS_MAX_M));
}

/* Skips the draw for an empty range so fixed values leave the stream untouched. */
static float sample_range(uint32_t *rng, float low, float high) {
  return low < high ? random_between(rng, low, high) : low;
}

noise_result noise_rain_start_drop(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                               const noise_listener_config *listener, const droplet *drop) {
  if (drop->velocity_m_s == 0.0f) return NOISE_OK;
  if (state->active_drops == NOISE_MAX_DROPLETS) {
    ++state->dropped_drops;
    return NOISE_VOICE_LIMIT;
  }
  noise_drop_voice *voice = &rain->voice[state->active_drops++];
  memset(voice, 0, sizeof(*voice));
  ++state->generated_drops;
  if (state->active_drops > state->peak_active_drops) {
    state->peak_active_drops = state->active_drops;
  }
  const noise_surface *s = &c->surface[drop->surface];
  float radius_ratio = drop->radius_m / 0.0005f;
  float amplitude = 0.004375f * sqrtf(radius_ratio * radius_ratio * radius_ratio) *
                    drop->velocity_m_s / 4.0f;
  float click_gain = sample_range(&rain->drop_rng, s->click_gain_min, s->click_gain_max);
  voice->material_lowpass_alpha = s->lowpass_hz > 0.0f ?
      -expm1f(-2.0f * NOISE_PI * s->lowpass_hz / NOISE_SAMPLE_RATE_HZ) : 1.0f;
  float click_frequency = sample_range(&rain->drop_rng, s->click_frequency_min_hz,
                                       s->click_frequency_max_hz);
  mode_init(&voice->mode[NOISE_DROP_CLICK], click_frequency,
            s->click_damping_ratio * click_frequency, amplitude * click_gain, 0);
  if (s->mode[0].gain > 0.0f || s->mode[1].gain > 0.0f) {
    float tuning = sample_range(&rain->drop_rng, 1.0f - s->detune, 1.0f + s->detune);
    for (unsigned m = 0; m < 2; ++m) {
      const noise_surface_mode *mode = &s->mode[m];
      if (mode->gain > 0.0f) {
        mode_init(&voice->mode[NOISE_DROP_RESONANCE + m], mode->frequency_hz * tuning,
                  mode->damping_per_s, amplitude * mode->gain, 0);
      }
    }
  }
  if (drop->bubble_radius_m > 0.0f) {
    float r = drop->bubble_radius_m;
    float frequency = sqrtf(3.0f * 1.4f * NOISE_PRESSURE_PA / NOISE_WATER_DENSITY) /
                      (2.0f * NOISE_PI * r);
    float damping = 0.13f / r + 0.0072f / (r * sqrtf(r));
    float decay = sample_range(&rain->drop_rng, s->bubble_decay_min, s->bubble_decay_max);
    float bubble_gain = sample_range(&rain->drop_rng, s->bubble_gain_min, s->bubble_gain_max);
    mode_init(&voice->mode[NOISE_DROP_BUBBLE], frequency, damping / decay,
              bubble_gain * click_gain * amplitude,
              (uint32_t)(s->bubble_delay_s * NOISE_SAMPLE_RATE_HZ));
  }
  noise_spatial_init(&voice->spatial, listener, drop->position);
  voice->filter_tail = 256;
  return NOISE_OK;
}

static unsigned choose_surface(noise_rain *rain, const noise_rain_config *c) {
  float choice = random_unit(&rain->drop_rng);
  unsigned surface = 0;
  while (surface + 1 < c->surface_count && choice >= rain->surface_cdf[surface]) ++surface;
  return surface;
}

static float sample_diameter_mm(noise_rain *rain) {
  float choice = random_unit(&rain->drop_rng);
  unsigned low = 0, high = NOISE_RAIN_SIZE_BINS - 1;
  while (low < high) {
    unsigned middle = (low + high) / 2;
    if (choice < rain->size_cdf[middle]) {
      high = middle;
    } else {
      low = middle + 1;
    }
  }
  return DIAMETER_MIN_MM + DIAMETER_BIN_MM * ((float)low + random_unit(&rain->drop_rng));
}

static void spawn_rain(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_listener_config *listener) {
  float diameter_mm = sample_diameter_mm(rain);
  droplet drop;
  drop.radius_m = diameter_mm * 0.0005f;
  drop.velocity_m_s = terminal_speed(diameter_mm);
  drop.surface = choose_surface(rain, c);
  drop.bubble_radius_m = 0.0f;
  const noise_surface *s = &c->surface[drop.surface];
  if (s->bubble_probability > 0.0f && random_unit(&rain->drop_rng) < s->bubble_probability) {
    drop.bubble_radius_m = s->bubble_radius_min_m < s->bubble_radius_max_m ?
        random_log_between(&rain->drop_rng, s->bubble_radius_min_m, s->bubble_radius_max_m) :
        s->bubble_radius_min_m;
  }
  drop.position.distance_m = area_uniform_distance(c->min_distance_m, rain->near_m,
                                                   random_unit(&rain->drop_rng));
  drop.position.angle_rad = 2.0f * NOISE_PI * random_unit(&rain->drop_rng);
  /* Capacity losses are recorded by noise_rain_start_drop for both arrival paths. */
  (void)noise_rain_start_drop(rain, state, c, listener, &drop);
}

/* Measures one ear of the played rain per band and adds matched noise to both ears.
   Drops arrive evenly around the listener, so either ear gives the per-ear power. */
static void bed_next(noise_rain_bed *bed, float played, float gain, noise_bus *bus) {
  for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
    float y = band_next(bed->analysis[band], played);
    bed->power[band] += BED_SMOOTHING * (y * y - bed->power[band]);
  }
  for (unsigned ear = 0; ear < NOISE_CHANNELS; ++ear) {
    float sum = 0.0f;
    for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) {
      float noise = 2.0f * random_unit(&bed->rng) - 1.0f;
      sum += bed->gain[band] * band_next(bed->synthesis[ear][band], noise);
    }
    bus->direct[ear][bus->position] += gain * sum;
  }
}

float noise_rain_next(noise_rain *rain, noise_state *state, const noise_rain_config *c,
                       const noise_listener_config *listener, noise_bus *bus) {
  if (c->gain > 0.0f && rain->arrival_probability > 0.0f &&
      random_unit(&rain->arrival_rng) < rain->arrival_probability) {
    spawn_rain(rain, state, c, listener);
  }
  float send = 0.0f;
  unsigned i = 0;
  while (i < state->active_drops) {
    noise_drop_voice *voice = &rain->voice[i];
    float source = 0.0f;
    unsigned remaining = 0;
    for (unsigned m = 0; m < NOISE_DROP_MODES; ++m) {
      source += mode_next(&voice->mode[m]);
      remaining += voice->mode[m].remaining + voice->mode[m].delay;
    }
    if (voice->material_lowpass_alpha < 1.0f) {
      for (unsigned stage = 0; stage < 2; ++stage) {
        voice->material_lowpass_state[stage] += voice->material_lowpass_alpha *
            (source - voice->material_lowpass_state[stage]);
        source = voice->material_lowpass_state[stage];
      }
    }
    send += c->gain * source;
    noise_spatial_next(&voice->spatial, listener, &rain->bus, source);
    if (!remaining) voice->filter_tail -= 1;
    if (!remaining && voice->filter_tail == 0) {
      /* Keep the active prefix dense to avoid scanning idle voices per sample. */
      state->active_drops -= 1;
      rain->voice[i] = rain->voice[state->active_drops];
    } else {
      ++i;
    }
  }
  float played[2];
  noise_bus_next(&rain->bus, &played[0], &played[1]);
  for (unsigned ear = 0; ear < NOISE_CHANNELS; ++ear) {
    bus->direct[ear][bus->position] += c->gain * played[ear];
  }
  float bed_gain = c->gain * c->bed_gain;
  if (rain->bed.ratio > 0.0f && bed_gain > 0.0f) bed_next(&rain->bed, played[0], bed_gain, bus);
  return send;
}
