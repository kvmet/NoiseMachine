#include "noise_storm.h"

#include <math.h>

#include "noise_internal.h"

#define CONTROL_S ((float)NOISE_CONTROL_FRAMES / NOISE_SAMPLE_RATE_HZ)

static int shape_valid(const noise_storm_shape *s) {
  return in_range(s->peak_rain_min_mm_h, 0.1f, 200.0f) &&
         in_range(s->peak_rain_max_mm_h, s->peak_rain_min_mm_h, 200.0f) &&
         in_range(s->core_along_m, 500.0f, 20000.0f) &&
         in_range(s->core_across_m, 500.0f, 50000.0f) &&
         in_range(s->tail_share, 0.0f, 1.0f) &&
         in_range(s->tail_length_m, 1000.0f, 50000.0f) &&
         in_range(s->tail_width_m, 1000.0f, 50000.0f) &&
         in_range(s->front_min_m, 0.0f, 20000.0f) &&
         in_range(s->front_max_m, s->front_min_m, 20000.0f) &&
         in_range(s->front_edge_m, 100.0f, 10000.0f) &&
         in_range(s->outflow_min_m_s, 0.0f, 40.0f) &&
         in_range(s->outflow_max_m_s, s->outflow_min_m_s, 40.0f) &&
         in_range(s->outflow_decay_m, 500.0f, 50000.0f) &&
         in_range(s->outflow_width_m, 1000.0f, 50000.0f) &&
         in_range(s->cooling_min_c, 0.0f, 20.0f) &&
         in_range(s->cooling_max_c, s->cooling_min_c, 20.0f) &&
         in_range(s->cooling_decay_m, 1000.0f, 100000.0f) &&
         in_range(s->cooling_width_m, 1000.0f, 50000.0f) &&
         in_range(s->cooling_s, 10.0f, 3600.0f) &&
         in_range(s->warming_s, 60.0f, 36000.0f) &&
         in_range(s->lightning_min_per_min, 0.0f, 30.0f) &&
         in_range(s->lightning_max_per_min, s->lightning_min_per_min, 30.0f) &&
         in_range(s->build_share, 0.05f, 0.9f) &&
         in_range(s->decay_share, 0.05f, 0.95f) &&
         s->build_share + s->decay_share <= 1.0f &&
         in_range(s->approach_m, 10000.0f, 100000.0f) &&
         in_range(s->miss_m, 0.0f, 30000.0f) &&
         in_range(s->heading_spread_rad, 0.0f, NOISE_PI);
}

int noise_storm_config_valid(const noise_storm_config *c) {
  const noise_fixed_weather *f = &c->fixed;
  return (c->manual == 0 || c->manual == 1) &&
         in_range(f->rain_mm_h, 0.0f, 200.0f) &&
         in_range(f->wind_m_s, 0.0f, 40.0f) &&
         in_range(f->wind_bearing_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         in_range(f->temperature_c, -10.0f, 45.0f) &&
         in_range(f->lightning_per_min, 0.0f, 30.0f) &&
         in_range(f->cell.distance_m, 200.0f, 30000.0f) &&
         in_range(f->cell.angle_rad, -2.0f * NOISE_PI, 2.0f * NOISE_PI) &&
         in_range(c->time_scale, 1.0f, 600.0f) &&
         in_range(c->temperature_c, -10.0f, 45.0f) &&
         in_range(c->min_severity, 0.0f, 1.0f) &&
         in_range(c->max_severity, c->min_severity, 1.0f) &&
         in_range(c->storms_per_hour, 0.0f, 4.0f) &&
         in_range(c->cell_speed_m_s, 3.0f, 30.0f) &&
         in_range(c->breeze_m_s, 0.0f, 15.0f) &&
         in_range(c->gust_intensity, 0.0f, 1.0f) &&
         in_range(c->gust_time_s, 0.5f, 30.0f) &&
         shape_valid(&c->shape);
}

void noise_storm_config_default(noise_storm_config *c) {
  c->manual = 1;
  c->fixed.rain_mm_h = 0.0f;
  c->fixed.wind_m_s = 3.0f;
  c->fixed.wind_bearing_rad = 0.0f;
  c->fixed.temperature_c = 25.0f;
  c->fixed.lightning_per_min = 0.0f;
  c->fixed.cell.distance_m = 5000.0f;
  c->fixed.cell.angle_rad = 0.0f;
  c->time_scale = 1.0f;
  c->temperature_c = 25.0f;
  c->min_severity = 0.2f;
  c->max_severity = 0.8f;
  c->storms_per_hour = 0.5f;
  c->cell_speed_m_s = 10.0f;
  c->breeze_m_s = 2.0f;
  c->gust_intensity = 0.3f;
  c->gust_time_s = 4.0f;
  /* A squall line: heavy rain in a band wider than it is deep. */
  c->shape = (noise_storm_shape){
    .peak_rain_min_mm_h = 2.0f, .peak_rain_max_mm_h = 150.0f,
    .core_along_m = 3000.0f, .core_across_m = 10000.0f,
    .tail_share = 0.1f, .tail_length_m = 15000.0f, .tail_width_m = 15000.0f,
    .front_min_m = 4000.0f, .front_max_m = 8000.0f, .front_edge_m = 1500.0f,
    .outflow_min_m_s = 4.0f, .outflow_max_m_s = 24.0f,
    .outflow_decay_m = 6000.0f, .outflow_width_m = 12000.0f,
    .cooling_min_c = 3.0f, .cooling_max_c = 10.0f,
    .cooling_decay_m = 25000.0f, .cooling_width_m = 15000.0f,
    .cooling_s = 240.0f, .warming_s = 2400.0f,
    .lightning_min_per_min = 0.5f, .lightning_max_per_min = 12.0f,
    .build_share = 0.3f, .decay_share = 0.35f,
    .approach_m = 40000.0f, .miss_m = 10000.0f, .heading_spread_rad = 0.35f};
}

static float square(float x) {
  return x * x;
}

static float smoothstep(float x) {
  x = fminf(1.0f, fmaxf(0.0f, x));
  return x * x * (3.0f - 2.0f * x);
}

static float wrap_angle(float angle) {
  return angle < 0.0f ? angle + 2.0f * NOISE_PI : angle;
}

static float cell_stage(const noise_storm_shape *s, const noise_storm_cell *cell) {
  float u = cell->travelled_m / (2.0f * s->approach_m);
  return u < s->build_share ? smoothstep(u / s->build_share) :
                              smoothstep((1.0f - u) / s->decay_share);
}

typedef struct cell_effect {
  float rain_mm_h;
  float wind[2]; /* Air velocity, x right and y front. */
  float cooling_c;
  float lightning_per_min;
} cell_effect;

/* Leading heavy core, trailing light rain, and a gust front of cold outflow ahead. */
static cell_effect cell_effect_at_listener(const noise_storm_shape *s,
                                           const noise_storm_cell *cell) {
  const float *p = cell->position;
  const float *h = cell->heading;
  float along = -(p[0] * h[0] + p[1] * h[1]); /* Positive while the core approaches. */
  float across = p[0] * h[1] - p[1] * h[0];
  float stage = cell_stage(s, cell);
  float severity = cell->severity;
  cell_effect e;

  float peak_mm_h = s->peak_rain_min_mm_h *
                    powf(s->peak_rain_max_mm_h / s->peak_rain_min_mm_h, severity);
  float core = expf(-square(along) / (2.0f * square(s->core_along_m)) -
                    square(across) / (2.0f * square(s->core_across_m)));
  float tail = 0.0f;
  if (along < 0.0f) {
    tail = s->tail_share * -expm1f(-square(along) / (2.0f * square(s->core_along_m))) *
           expf(along / s->tail_length_m) *
           expf(-square(across) / (2.0f * square(s->tail_width_m)));
  }
  e.rain_mm_h = stage * peak_mm_h * (core + tail);

  float front = s->front_min_m + (s->front_max_m - s->front_min_m) * severity;
  float lead = along > front ? expf(-square((along - front) / s->front_edge_m)) : 1.0f;
  float outflow = along >= 0.0f ? lead : expf(along / s->outflow_decay_m);
  float outflow_m_s = s->outflow_min_m_s + (s->outflow_max_m_s - s->outflow_min_m_s) * severity;
  float speed = stage * outflow_m_s * outflow *
                expf(-square(across) / (2.0f * square(s->outflow_width_m)));
  float distance = fmaxf(1.0f, hypotf(p[0], p[1]));
  e.wind[0] = -speed * p[0] / distance;
  e.wind[1] = -speed * p[1] / distance;

  float cool = along > front ? lead : expf(fminf(0.0f, along) / s->cooling_decay_m);
  float cooling_c = s->cooling_min_c + (s->cooling_max_c - s->cooling_min_c) * severity;
  e.cooling_c = stage * cooling_c * cool *
                expf(-square(across) / (2.0f * square(s->cooling_width_m)));
  e.lightning_per_min = stage * stage *
      (s->lightning_min_per_min +
       (s->lightning_max_per_min - s->lightning_min_per_min) * severity * severity);
  return e;
}

/* Fills weather from the current state without advancing it. Returns the cooling
   the storms impose on the climate temperature. */
static float storm_weather(const noise_storm *storm, const noise_storm_config *c,
                           noise_weather *w) {
  float cooling = 0.0f;
  if (c->manual) {
    const noise_fixed_weather *f = &c->fixed;
    w->rain_mm_h = f->rain_mm_h;
    w->wind_mean_m_s = f->wind_m_s;
    w->wind_bearing_rad = f->wind_bearing_rad;
    w->temperature_c = f->temperature_c;
    w->lightning_per_min = f->lightning_per_min;
    w->cell = f->cell;
  } else {
    float rain = 0.0f;
    float wind[2] = {c->breeze_m_s * storm->prevailing[0], c->breeze_m_s * storm->prevailing[1]};
    float nearest = INFINITY;
    w->lightning_per_min = 0.0f;
    w->cell.distance_m = 0.0f;
    w->cell.angle_rad = 0.0f;
    for (unsigned i = 0; i < NOISE_STORM_CELLS; ++i) {
      const noise_storm_cell *cell = &storm->cell[i];
      if (!cell->active) continue;
      cell_effect e = cell_effect_at_listener(&c->shape, cell);
      rain += e.rain_mm_h;
      wind[0] += e.wind[0];
      wind[1] += e.wind[1];
      cooling = fmaxf(cooling, e.cooling_c);
      float distance = fmaxf(1.0f, hypotf(cell->position[0], cell->position[1]));
      if (distance < nearest) {
        nearest = distance;
        w->lightning_per_min = e.lightning_per_min;
        w->cell.distance_m = distance;
        w->cell.angle_rad = wrap_angle(atan2f(cell->position[0], cell->position[1]));
      }
    }
    w->rain_mm_h = fminf(200.0f, rain);
    w->wind_mean_m_s = hypotf(wind[0], wind[1]);
    w->wind_bearing_rad = w->wind_mean_m_s > 0.0f ?
        wrap_angle(atan2f(-wind[0], -wind[1])) : 0.0f;
    w->temperature_c = storm->temperature_c;
  }
  w->wind_m_s = fmaxf(0.0f, w->wind_mean_m_s * (1.0f + storm->gust));
  return cooling;
}

/* Places a storm along_m before the point where its track passes the listener. */
static void spawn(noise_storm *storm, const noise_storm_config *c, noise_storm_cell *cell,
                  float along_m, float miss_limit_m) {
  float heading = atan2f(storm->prevailing[0], storm->prevailing[1]) +
                  c->shape.heading_spread_rad * random_gaussian(&storm->rng);
  float miss = random_between(&storm->rng, -miss_limit_m, miss_limit_m);
  cell->severity = random_between(&storm->rng, c->min_severity, c->max_severity);
  cell->heading[0] = sinf(heading);
  cell->heading[1] = cosf(heading);
  cell->position[0] = -along_m * cell->heading[0] + miss * cell->heading[1];
  cell->position[1] = -along_m * cell->heading[1] - miss * cell->heading[0];
  cell->travelled_m = c->shape.approach_m - along_m;
  cell->active = 1;
}

void noise_storm_init(noise_storm *storm, const noise_storm_config *c, noise_weather *weather,
                      uint32_t seed) {
  storm->rng = stream_seed(seed, 0x78dde6e4u);
  storm->gust_rng = stream_seed(seed, 0x510e527fu);
  float prevailing = 2.0f * NOISE_PI * random_unit(&storm->rng);
  storm->prevailing[0] = sinf(prevailing);
  storm->prevailing[1] = cosf(prevailing);
  if (c->storms_per_hour > 0.0f) {
    float along = random_between(&storm->rng, -4000.0f, 1000.0f);
    spawn(storm, c, &storm->cell[0], along, fminf(2000.0f, c->shape.miss_m));
  }
  storm->temperature_c = c->temperature_c;
  storm->temperature_c -= storm_weather(storm, c, weather);
  storm_weather(storm, c, weather);
}

void noise_storm_configure(noise_storm *storm, const noise_storm_config *previous,
                           const noise_storm_config *c, noise_weather *weather) {
  storm->temperature_c += c->temperature_c - previous->temperature_c;
  storm_weather(storm, c, weather);
}

static void advance_storms(noise_storm *storm, const noise_storm_config *c, float dt) {
  float step = c->cell_speed_m_s * dt;
  for (unsigned i = 0; i < NOISE_STORM_CELLS; ++i) {
    noise_storm_cell *cell = &storm->cell[i];
    if (!cell->active) continue;
    cell->position[0] += step * cell->heading[0];
    cell->position[1] += step * cell->heading[1];
    cell->travelled_m += step;
    if (cell->travelled_m >= 2.0f * c->shape.approach_m) cell->active = 0;
  }
  /* One draw per update keeps the stream position independent of the outcome. */
  if (random_unit(&storm->rng) < c->storms_per_hour * dt / 3600.0f) {
    for (unsigned i = 0; i < NOISE_STORM_CELLS; ++i) {
      if (storm->cell[i].active) continue;
      spawn(storm, c, &storm->cell[i], c->shape.approach_m, c->shape.miss_m);
      break;
    }
  }
}

int noise_storm_next(noise_storm *storm, const noise_storm_config *c, noise_weather *weather) {
  if (++storm->frame < NOISE_CONTROL_FRAMES) return 0;
  storm->frame = 0;
  float dt = c->time_scale * CONTROL_S;
  if (!c->manual) advance_storms(storm, c, dt);
  float cooling = storm_weather(storm, c, weather);
  if (!c->manual) {
    float target = c->temperature_c - cooling;
    float time = target < storm->temperature_c ? c->shape.cooling_s : c->shape.warming_s;
    storm->temperature_c += -expm1f(-dt / time) * (target - storm->temperature_c);
    weather->temperature_c = storm->temperature_c;
  }
  float keep = expf(-CONTROL_S / c->gust_time_s);
  float spread = c->gust_intensity * sqrtf(1.0f - keep * keep);
  storm->gust = keep * storm->gust + spread * random_gaussian(&storm->gust_rng);
  weather->wind_m_s = fmaxf(0.0f, weather->wind_mean_m_s * (1.0f + storm->gust));
  return 1;
}
