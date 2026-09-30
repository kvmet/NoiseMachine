#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double terminal_speed(double d) {
  double cm_per_s = d <= 1.4 ?
      -17.8951 + d * (448.9498 + d * (16.3719 - 45.9516 * d)) :
       24.1660 + d * (448.8336 + d * (-75.6265 + 4.2695 * d));
  return 0.01 * cm_per_s;
}

/* Marshall-Palmer drops of 0.8 mm up to diameter landing per square metre per second. */
static double flux_below(double rain_mm_h, double diameter_mm) {
  double lambda = 4.1 * pow(rain_mm_h, -0.21);
  double sum = 0.0, step = 0.001;
  for (double d = 0.8 + 0.5 * step; d < diameter_mm; d += step) {
    sum += 8000.0 * exp(-lambda * d) * terminal_speed(d) * step;
  }
  return sum;
}

/* Marshall-Palmer drops of 0.8 mm up to diameter in a cubic metre of air. */
static double concentration_below(double rain_mm_h, double diameter_mm) {
  double lambda = 4.1 * pow(rain_mm_h, -0.21);
  double sum = 0.0, step = 0.001;
  for (double d = 0.8 + 0.5 * step; d < diameter_mm; d += step) {
    sum += 8000.0 * exp(-lambda * d) * step;
  }
  return sum;
}

static void test_rain_arrivals(void) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 2.0f;
  c.rain.min_distance_m = 0.75f;
  c.rain.max_distance_m = 1.0f;
  c.rain.max_drops_per_s = 2000.0f;
  assert(noise_init(&a, &c, 47) == NOISE_OK);
  double expected = flux_below(2.0, 5.8) * TEST_PI * (1.0 - 0.75 * 0.75);
  assert(expected > 500.0 && expected < 2000.0);
  assert(a.rain.bed.ratio == 0.0f && a.rain.near_m == 1.0f);
  for (unsigned second = 0; second < 20; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  double measured = a.state.generated_drops / 20.0;
  assert(fabs(measured / expected - 1.0) < 0.03);
  assert(a.state.dropped_drops == 0);

  /* Past the budget, played drops keep to the near ring and the bed carries the rest. */
  c.rain.max_distance_m = 5.0f;
  c.rain.max_drops_per_s = 500.0f;
  assert(noise_init(&a, &c, 47) == NOISE_OK);
  assert(a.rain.near_m > 0.75f && a.rain.near_m < 5.0f);
  double near_area = TEST_PI * (a.rain.near_m * a.rain.near_m - 0.75 * 0.75);
  assert(fabs(flux_below(2.0, 5.8) * near_area / 500.0 - 1.0) < 0.01);
  assert(a.rain.bed.ratio > 1.0f);
}

/* The size table matches the analytic flux-weighted Marshall-Palmer distribution. */
static void test_drop_sizes(void) {
  static const float rates[] = {0.5f, 10.0f, 150.0f};
  float small_share[3]; /* Flux of drops up to 1.8 mm. */
  for (unsigned r = 0; r < 3; ++r) {
    noise_config c = silent_config();
    c.storm.fixed.rain_mm_h = rates[r];
    assert(noise_init(&a, &c, 3) == NOISE_OK);
    double total = flux_below(rates[r], 5.8);
    assert(fabs(a.rain.flux_per_m2_s / total - 1.0) < 0.01);
    for (unsigned bin = 4; bin < NOISE_RAIN_SIZE_BINS; bin += 5) {
      double edge = 0.8 + 0.1 * (bin + 1);
      assert(fabs(a.rain.size_cdf[bin] - flux_below(rates[r], edge) / total) < 0.01);
    }
    small_share[r] = a.rain.size_cdf[9];
  }
  /* Heavier rain brings more large drops. */
  assert(small_share[0] > small_share[1] && small_share[1] > small_share[2]);
}

/* Mean squared slope over mean square: rises with brightness. */
static double brightness(double *level_db) {
  double slope = 0.0, energy = 0.0;
  for (unsigned n = 1; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    double x = audio[2 * n], previous = audio[2 * n - 2];
    slope += (x - previous) * (x - previous);
    energy += x * x;
  }
  *level_db += 10.0 * log10(energy);
  return slope / energy;
}

/* Rain rendered with the bed matches rain with every drop played, in level and spectrum. */
static void test_bed_matches_played_rain(void) {
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  c.rain.max_distance_m = 1.5f;
  c.rain.surface[WATER].coverage = 0.0f;
  c.storm.fixed.rain_mm_h = 1.0f;
  double level[2] = {0.0, 0.0}, bright[2] = {0.0, 0.0};
  static const float budgets[2] = {2000.0f, 100.0f};
  for (unsigned run = 0; run < 2; ++run) {
    c.rain.max_drops_per_s = budgets[run];
    assert(noise_init(&a, &c, 9) == NOISE_OK);
    assert(run == 0 ? a.rain.bed.ratio == 0.0f : a.rain.bed.ratio > 5.0f);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned second = 0; second < 10; ++second) {
      noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
      bright[run] += brightness(&level[run]) / 10.0;
    }
    level[run] /= 10.0;
    assert(a.state.dropped_drops == 0);
  }
  assert(fabs(level[1] - level[0]) < 1.5);
  assert(fabs(bright[1] / bright[0] - 1.0) < 0.1);
}

/* A wall facing the wind takes the drops the wind carries into it, and none in calm air. */
static void test_driving_rain(void) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.storm.fixed.wind_m_s = 0.0f;
  c.storm.gust_intensity = 0.0f;
  c.rain.surface_count = 2;
  c.rain.surface[0] = c.rain.surface[CONCRETE];
  c.rain.surface[1] = c.rain.surface[METAL];
  c.rain.surface[0].coverage = 1.0f;
  c.rain.surface[1].coverage = 1.0f;
  c.rain.surface[1].vertical = 1;
  assert(noise_init(&a, &c, 83) == NOISE_OK);
  double area = TEST_PI * (5.0 * 5.0 - 0.75 * 0.75);
  double ground = flux_below(10.0, 5.8) * area / 2.0;
  assert(a.rain.surface_cdf[0] == 1.0f);
  assert(fabs(a.rain.arrivals_per_s / ground - 1.0) < 0.01);

  c.storm.fixed.wind_m_s = 10.0f;
  assert(noise_set_config(&a, &c) == NOISE_OK);
  double wall = 10.0 * concentration_below(10.0, 5.8) / flux_below(10.0, 5.8);
  assert(wall > 1.5 && wall < 4.0);
  assert(fabs(a.rain.surface_cdf[0] - 1.0 / (1.0 + wall)) < 0.01);
  assert(fabs(a.rain.arrivals_per_s / (ground * (1.0 + wall)) - 1.0) < 0.01);
  /* Wind-driven sizes follow N(D) alone, so walls take more small drops. */
  double total = concentration_below(10.0, 5.8);
  for (unsigned bin = 4; bin < NOISE_RAIN_SIZE_BINS; bin += 5) {
    double edge = 0.8 + 0.1 * (bin + 1);
    assert(fabs(a.rain.vertical_size_cdf[bin] - concentration_below(10.0, edge) / total) < 0.01);
  }
  assert(a.rain.vertical_size_cdf[9] > a.rain.size_cdf[9] + 0.03f);

  /* A wall alone is silent in calm air. */
  c.rain.surface[0].coverage = 0.0f;
  c.storm.fixed.wind_m_s = 0.0f;
  assert(noise_init(&a, &c, 83) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.rain.arrivals_per_s == 0.0f && a.state.generated_drops == 0);
}

/* Rain on a wall alone, every drop played. */
static double wall_power(float wind_m_s) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 1.0f;
  c.storm.fixed.wind_m_s = wind_m_s;
  c.storm.gust_intensity = 0.0f;
  c.rain.max_distance_m = 1.0f;
  c.rain.max_drops_per_s = 2000.0f;
  c.rain.surface_count = 1;
  c.rain.surface[0] = c.rain.surface[GLASS];
  c.rain.surface[0].vertical = 1;
  assert(noise_init(&a, &c, 89) == NOISE_OK);
  assert(a.rain.bed.ratio == 0.0f);
  double power = 0.0;
  for (unsigned second = 0; second < 10; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) power += (double)audio[i] * audio[i];
  }
  assert(a.state.dropped_drops == 0 && a.state.clipped_samples == 0);
  return power;
}

/* Walls meet drops at the wind speed, so doubling it doubles the hits and each hit's
   amplitude: eight times the power. At terminal speed it would only double. */
static void test_wall_impact_speed(void) {
  double ratio = wall_power(10.0f) / wall_power(5.0f);
  assert(ratio > 5.0 && ratio < 12.0);
}

/* With sheets off, gusts leave the rain untouched; with them on, gusts change it. */
static void test_sheet_depth(void) {
  static int16_t out[3][2 * NOISE_SAMPLE_RATE_HZ];
  static const float gusts[3] = {0.0f, 0.5f, 0.5f};
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.storm.fixed.wind_m_s = 10.0f;
  for (unsigned run = 0; run < 3; ++run) {
    c.storm.gust_intensity = gusts[run];
    c.rain.sheet_depth = run == 2 ? 1.0f : 0.0f;
    assert(noise_init(&a, &c, 97) == NOISE_OK);
    for (unsigned second = 0; second < 3; ++second) noise_fill(&a, out[run], NOISE_SAMPLE_RATE_HZ);
  }
  assert(memcmp(out[0], out[1], sizeof(out[0])) == 0);
  assert(memcmp(out[1], out[2], sizeof(out[1])) != 0);
}

/* Seconds by which the right ear's rain level leads the left's, from 50 ms windows. */
static double sheet_lead_s(float wind_bearing_rad) {
  enum { WINDOW = NOISE_SAMPLE_RATE_HZ / 20, WINDOWS = 2400, MAX_LAG = 20 };
  static double power[2][WINDOWS];
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  c.rain.bed_gain = 0.0f;
  c.rain.sheet_depth = 2.0f;
  c.rain.max_distance_m = 8.0f;
  c.rain.max_drops_per_s = 2000.0f;
  c.storm.fixed.rain_mm_h = 0.5f;
  c.storm.fixed.wind_m_s = 4.0f;
  c.storm.fixed.wind_bearing_rad = wind_bearing_rad;
  c.storm.gust_intensity = 0.5f;
  c.storm.gust_time_s = 2.0f;
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  double mean[2] = {0.0, 0.0};
  for (unsigned w = 0; w < WINDOWS; ++w) {
    noise_fill(&a, audio, WINDOW);
    for (unsigned ear = 0; ear < 2; ++ear) {
      power[ear][w] = 0.0;
      for (unsigned n = 0; n < WINDOW; ++n) {
        power[ear][w] += (double)audio[2 * n + ear] * audio[2 * n + ear];
      }
      mean[ear] += power[ear][w] / WINDOWS;
    }
  }
  int best = 0;
  double best_correlation = -1.0;
  for (int lag = -MAX_LAG; lag <= MAX_LAG; ++lag) {
    double product = 0.0, right = 0.0, left = 0.0;
    for (int w = MAX_LAG; w < WINDOWS - MAX_LAG; ++w) {
      double r = power[1][w] - mean[1], l = power[0][w + lag] - mean[0];
      product += r * l;
      right += r * r;
      left += l * l;
    }
    double correlation = product / sqrt(right * left);
    if (correlation > best_correlation) {
      best_correlation = correlation;
      best = lag;
    }
  }
  assert(best_correlation > 0.2);
  return best / 20.0;
}

/* Sheets reach the windward ear first. Across the 2.2 m played ring at 4 m/s, the
   half-ring centroids are about 0.45 s apart. */
static void test_sheets_travel_with_wind(void) {
  double from_right = sheet_lead_s(0.5f * (float)TEST_PI);
  double from_left = sheet_lead_s(-0.5f * (float)TEST_PI);
  double from_front = sheet_lead_s(0.0f);
  assert(from_right > 0.15 && from_right < 0.8);
  assert(from_left < -0.15 && from_left > -0.8);
  assert(fabs(from_front) <= 0.1);
}

/* Each ear's bed follows that ear's drops. */
static void test_bed_follows_each_ear(void) {
  noise_config c = silent_config();
  c.rain.max_drops_per_s = 1.0f;
  c.storm.fixed.rain_mm_h = 10.0f;
  assert(noise_init(&a, &c, 101) == NOISE_OK);
  assert(a.rain.bed.ratio > 0.0f);
  droplet drop = water_drop();
  drop.position.angle_rad = 0.5f * (float)TEST_PI;
  for (unsigned n = 0; n < 100; ++n) {
    assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 50);
  }
  double gain[2] = {0.0, 0.0};
  for (unsigned ear = 0; ear < 2; ++ear) {
    for (unsigned band = 0; band < NOISE_BED_BANDS; ++band) gain[ear] += a.rain.bed.gain[ear][band];
  }
  assert(gain[1] > 1.5 * gain[0]);
}

/* Places a severity 1 cell 30 km in front, heading straight over the listener. */
static noise_storm_cell *place_head_on(noise_gen *gen) {
  noise_storm_cell *cell = &gen->storm.cell[0];
  cell->active = 1;
  cell->severity = 1.0f;
  cell->heading[0] = 0.0f;
  cell->heading[1] = -1.0f;
  cell->position[0] = 0.0f;
  cell->position[1] = 30000.0f;
  cell->travelled_m = 10000.0f;
  return cell;
}

/* A storm heading straight at the listener: gust front and cooling before the heaviest rain. */
static void test_storm_passage(void) {
  noise_config c = silent_config();
  c.storm.manual = 0;
  c.storm.storms_per_hour = 0.0f;
  c.storm.time_scale = 600.0f;
  assert(noise_init(&a, &c, 11) == NOISE_OK);
  assert(a.state.weather.rain_mm_h == 0.0f && a.state.weather.lightning_per_min == 0.0f);
  noise_storm_cell *cell = place_head_on(&a);
  float breeze = c.storm.breeze_m_s;
  double front_s = -1.0, cool_s = -1.0, peak_s = 0.0, peak_rain = 0.0, last_distance = 1e9;
  for (unsigned tenth = 0; tenth < 100; ++tenth) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 10);
    const noise_weather *w = &a.state.weather;
    double t = tenth * 0.1 * c.storm.time_scale;
    if (front_s < 0.0 && w->wind_mean_m_s > breeze + 10.0f) front_s = t;
    if (cool_s < 0.0 && w->temperature_c < c.storm.temperature_c - 2.0f) cool_s = t;
    if (w->rain_mm_h > peak_rain) {
      peak_rain = w->rain_mm_h;
      peak_s = t;
    }
    if (cell->position[1] > 0.0f) {
      /* The core approaches from the front, and thunder is placed toward it. */
      assert(w->cell.distance_m < last_distance);
      assert(fabsf(w->cell.angle_rad) < 0.01f);
      last_distance = w->cell.distance_m;
      assert(w->lightning_per_min > 0.0f);
    }
  }
  assert(front_s > 0.0 && cool_s > 0.0);
  assert(front_s < peak_s - 120.0 && cool_s < peak_s);
  assert(peak_rain > 100.0 && peak_rain <= 150.0);
}

/* The shape's severity 1 values bound what a head-on storm delivers. */
static void test_storm_shape(void) {
  noise_config c = silent_config();
  c.storm.manual = 0;
  c.storm.storms_per_hour = 0.0f;
  c.storm.time_scale = 600.0f;
  c.storm.shape.peak_rain_max_mm_h = 40.0f;
  c.storm.shape.outflow_max_m_s = 8.0f;
  c.storm.shape.cooling_min_c = 1.0f;
  c.storm.shape.cooling_max_c = 2.0f;
  c.storm.shape.lightning_max_per_min = 3.0f;
  assert(noise_init(&a, &c, 11) == NOISE_OK);
  place_head_on(&a);
  double peak_rain = 0.0, peak_wind = 0.0, peak_lightning = 0.0, lowest = 100.0;
  for (unsigned tenth = 0; tenth < 100; ++tenth) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 10);
    const noise_weather *w = &a.state.weather;
    peak_rain = fmax(peak_rain, w->rain_mm_h);
    peak_wind = fmax(peak_wind, w->wind_mean_m_s);
    peak_lightning = fmax(peak_lightning, w->lightning_per_min);
    lowest = fmin(lowest, w->temperature_c);
  }
  assert(peak_rain > 30.0 && peak_rain <= 40.0 * (1.0 + c.storm.shape.tail_share));
  assert(peak_wind > 6.0 && peak_wind <= 8.0 + c.storm.breeze_m_s);
  assert(peak_lightning > 2.0 && peak_lightning <= 3.0);
  assert(lowest > c.storm.temperature_c - 2.0 && lowest < c.storm.temperature_c - 1.0);
}

/* Bed gain scales only the bed: drops are identical at every setting. */
static void test_bed_gain(void) {
  static int16_t out[3][2 * NOISE_SAMPLE_RATE_HZ];
  noise_config c = silent_config();
  c.rain.gain = 1.0f;
  c.rain.max_drops_per_s = 100.0f;
  c.storm.fixed.rain_mm_h = 30.0f;
  for (unsigned k = 0; k < 3; ++k) {
    c.rain.bed_gain = (float)k;
    assert(noise_init(&a, &c, 61) == NOISE_OK);
    noise_fill(&a, out[k], NOISE_SAMPLE_RATE_HZ);
    assert(a.state.clipped_samples == 0);
  }
  double bed = 0.0;
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) {
    int once = out[1][i] - out[0][i];
    int twice = out[2][i] - out[0][i];
    /* Three roundings to int16 differ by at most two steps. */
    assert(abs(twice - 2 * once) <= 2);
    bed += (double)once * once;
  }
  assert(bed > 0.0);
}

static void test_status(void) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.storm.fixed.temperature_c = 10.0f;
  c.storm.fixed.wind_m_s = 20.0f;
  c.rain.max_drops_per_s = 500.0f;
  assert(noise_init(&a, &c, 71) == NOISE_OK);
  noise_status s;
  noise_get_status(&a, &s);
  assert(s.cricket_quiet == (NOISE_QUIET_COLD | NOISE_QUIET_RAIN | NOISE_QUIET_WIND));
  assert(s.cicada_quiet == (NOISE_QUIET_COLD | NOISE_QUIET_RAIN));
  assert(fabsf(s.rain_played_per_s - 500.0f) < 0.1f);
  assert(s.rain_arrivals_per_s > 20.0f * s.rain_played_per_s);
  assert(s.bed_share > 0.9f && s.bed_share < 1.0f);
  assert(fabsf(s.wind_level - s.weather.wind_m_s / 10.0f) < 1e-5f);

  /* Each threshold is the configured one. */
  c.crickets.min_temperature_c = 5.0f;
  c.crickets.max_rain_mm_h = 20.0f;
  c.crickets.max_wind_m_s = 25.0f;
  c.cicadas.min_temperature_c = 5.0f;
  c.cicadas.max_rain_mm_h = 20.0f;
  assert(noise_set_config(&a, &c) == NOISE_OK);
  noise_get_status(&a, &s);
  assert(s.cricket_quiet == 0 && s.cicada_quiet == 0);
  c.crickets.max_wind_m_s = 15.0f;
  c.cicadas.max_rain_mm_h = 5.0f;
  assert(noise_set_config(&a, &c) == NOISE_OK);
  noise_get_status(&a, &s);
  assert(s.cricket_quiet == NOISE_QUIET_WIND && s.cicada_quiet == NOISE_QUIET_RAIN);

  c.storm.fixed.rain_mm_h = 0.0f;
  assert(noise_set_config(&a, &c) == NOISE_OK);
  noise_get_status(&a, &s);
  assert(s.rain_arrivals_per_s == 0.0f && s.rain_played_per_s == 0.0f && s.bed_share == 0.0f);
}

static void test_time_scale(void) {
  noise_config c = silent_config();
  c.storm.manual = 0;
  c.storm.time_scale = 120.0f;
  assert(noise_init(&a, &c, 21) == NOISE_OK);
  assert(a.storm.cell[0].active);
  float start = a.storm.cell[0].travelled_m;
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  /* 100 weather updates of 10 ms each, 120 times faster. */
  float moved = a.storm.cell[0].travelled_m - start;
  assert(fabsf(moved - c.storm.cell_speed_m_s * 120.0f) < 1.0f);
  /* Manual weather holds the storms still. */
  c.storm.manual = 1;
  assert(noise_set_config(&a, &c) == NOISE_OK);
  start = a.storm.cell[0].travelled_m;
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.storm.cell[0].travelled_m == start);
}

static void test_gusts(void) {
  noise_config c = silent_config();
  c.storm.fixed.wind_m_s = 10.0f;
  assert(noise_init(&a, &c, 31) == NOISE_OK);
  double sum = 0.0, sum_squared = 0.0;
  unsigned count = 0;
  for (unsigned second = 0; second < 600; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    double w = a.state.weather.wind_m_s;
    sum += w;
    sum_squared += w * w;
    ++count;
    assert(a.state.weather.wind_mean_m_s == 10.0f);
  }
  double mean = sum / count;
  double spread = sqrt(sum_squared / count - mean * mean);
  assert(fabs(mean - 10.0) < 1.0);
  assert(spread > 2.4 && spread < 3.6);

  c.storm.gust_intensity = 0.0f;
  assert(noise_init(&a, &c, 31) == NOISE_OK);
  for (unsigned second = 0; second < 10; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    assert(a.state.weather.wind_m_s == 10.0f);
  }
}

static void test_insects_follow_weather(void) {
  noise_config c = silent_config();
  c.crickets.gain = 0.5f;
  c.crickets.call_rate_scale = 1.0f;
  c.storm.fixed.temperature_c = 25.0f;
  assert(noise_init(&a, &c, 41) == NOISE_OK);
  /* Dolbear: 4 x 77 F - 160 = 148 chirps a minute. */
  assert(fabsf(a.crickets.call_rate_hz - 148.0f / 60.0f) < 1e-3f);
  assert(!a.crickets.quiet && !a.cicadas.quiet);

  noise_config wet = c;
  wet.storm.fixed.rain_mm_h = 1.0f;
  assert(noise_set_config(&a, &wet) == NOISE_OK);
  assert(a.crickets.quiet && a.cicadas.quiet);
  noise_config windy = c;
  windy.storm.fixed.wind_m_s = 9.0f;
  assert(noise_set_config(&a, &windy) == NOISE_OK);
  assert(a.crickets.quiet && !a.cicadas.quiet);
  noise_config cool = c;
  cool.storm.fixed.temperature_c = 18.0f;
  assert(noise_set_config(&a, &cool) == NOISE_OK);
  assert(!a.crickets.quiet && a.cicadas.quiet);
  cool.storm.fixed.temperature_c = 12.0f;
  assert(noise_set_config(&a, &cool) == NOISE_OK);
  assert(a.crickets.quiet);

  /* Cicadas stay silent through cool weather, chorus included. */
  noise_config cicadas = silent_config();
  cicadas.cicadas.gain = 1.0f;
  cicadas.storm.fixed.temperature_c = 18.0f;
  assert(noise_init(&a, &cicadas, 43) == NOISE_OK);
  for (unsigned second = 0; second < 5; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
  }

  /* Crickets that are singing go quiet once rain starts. */
  noise_config crickets = silent_config();
  crickets.crickets.gain = 0.5f;
  set_cricket_rate(&crickets, 2.0f);
  assert(noise_init(&a, &crickets, 45) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  crickets.storm.fixed.rain_mm_h = 5.0f;
  crickets.rain.gain = 0.0f;
  assert(noise_set_config(&a, &crickets) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ); /* Chirps in progress finish. */
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
}

static void test_thunder_follows_cell(void) {
  noise_config c = silent_config();
  c.thunder.gain = 1.0f;
  c.storm.fixed.lightning_per_min = 20.0f;
  c.storm.fixed.cell.distance_m = 30000.0f;
  assert(noise_init(&a, &c, 51) == NOISE_OK);
  for (unsigned second = 0; second < 60; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_thunder == 0 && a.state.dropped_thunder == 0);

  c.storm.fixed.cell.distance_m = 6000.0f;
  c.storm.fixed.cell.angle_rad = 0.5f * (float)TEST_PI;
  assert(noise_init(&a, &c, 51) == NOISE_OK);
  double ear[2] = {0.0, 0.0};
  for (unsigned second = 0; second < 60; ++second) {
    noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
    for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
      ear[0] += (double)audio[2 * n] * audio[2 * n];
      ear[1] += (double)audio[2 * n + 1] * audio[2 * n + 1];
    }
  }
  assert(a.state.generated_thunder > 5);
  /* Strikes cluster on the listener's right. */
  assert(ear[1] > 1.5 * ear[0]);
}

void run_storm_tests(void) {
  test_rain_arrivals();
  test_drop_sizes();
  test_bed_matches_played_rain();
  test_driving_rain();
  test_wall_impact_speed();
  test_sheet_depth();
  test_sheets_travel_with_wind();
  test_bed_follows_each_ear();
  test_storm_passage();
  test_storm_shape();
  test_bed_gain();
  test_status();
  test_time_scale();
  test_gusts();
  test_insects_follow_weather();
  test_thunder_follows_cell();
}
