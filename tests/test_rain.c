#include "test_support.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double resonance_damping(const noise_gen *gen) {
  return -log(sqrt(gen->rain.voice[0].mode[NOISE_DROP_RESONANCE].radius_squared)) *
         NOISE_SAMPLE_RATE_HZ;
}

static void test_bubble_physics(void) {
  noise_config c = silent_config();
  c.rain.surface[WATER].click_gain_min = 1.0f;
  c.rain.surface[WATER].click_gain_max = 1.0f;
  c.rain.surface[WATER].bubble_gain_min = 2.0f;
  c.rain.surface[WATER].bubble_gain_max = 2.0f;
  c.rain.surface[WATER].bubble_decay_min = 1.0f;
  c.rain.surface[WATER].bubble_decay_max = 1.0f;
  droplet drop = water_drop();
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  noise_mode initial = a.rain.voice[0].mode[NOISE_DROP_BUBBLE];
  double radius = drop.bubble_radius_m;
  double frequency = sqrt(3.0 * 1.4 * 101325.0 / 1000.0) / (2.0 * TEST_PI * radius);
  double damping = 0.13 / radius + 0.0072 / pow(radius, 1.5);
  assert(fabs(frequency - 8208.11) < 0.01);
  assert(fabs(sqrt(initial.radius_squared) - exp(-damping / NOISE_SAMPLE_RATE_HZ)) < 1e-6);
  assert(fabs(initial.coefficient - 2.0 * exp(-damping / NOISE_SAMPLE_RATE_HZ) *
              cos(2.0 * TEST_PI * frequency / NOISE_SAMPLE_RATE_HZ)) < 2e-6);
  unsigned delay = initial.delay;
  int16_t frame[2];
  for (unsigned n = 0; n < delay + 120; ++n) {
    double t = n > delay ? (double)(n - delay) / NOISE_SAMPLE_RATE_HZ : 0.0;
    double expected = 0.00875 * exp(-damping * t) * sin(2.0 * TEST_PI * frequency * t);
    assert(fabs(a.rain.voice[0].mode[NOISE_DROP_BUBBLE].current - expected) < 2.5e-7);
    noise_fill(&a, frame, 1);
  }
  assert(noise_init(&b, &c, 1) == NOISE_OK);
  drop.bubble_radius_m *= 2.0f;
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_mode larger = b.rain.voice[0].mode[NOISE_DROP_BUBBLE];
  double measured_frequency = acos(larger.coefficient / (2.0 * sqrt(larger.radius_squared))) *
                              NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  assert(fabs(measured_frequency - frequency / 2.0) < 0.01);
}

static void test_water_controls(void) {
  noise_config c = silent_config();
  c.rain.surface[WATER].click_gain_min = 0.25f;
  c.rain.surface[WATER].click_gain_max = 0.25f;
  c.rain.surface[WATER].bubble_gain_min = 1.0f;
  c.rain.surface[WATER].bubble_gain_max = 1.0f;
  c.rain.surface[WATER].bubble_decay_min = 4.0f;
  c.rain.surface[WATER].bubble_decay_max = 4.0f;
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0008f;
  assert(noise_init(&a, &c, 31) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);

  c.rain.surface[WATER].click_gain_min = 0.5f;
  c.rain.surface[WATER].click_gain_max = 0.5f;
  c.rain.surface[WATER].bubble_gain_min = 2.0f;
  c.rain.surface[WATER].bubble_gain_max = 2.0f;
  assert(noise_init(&b, &c, 31) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  assert(fabs(b.rain.voice[0].mode[NOISE_DROP_CLICK].previous /
              a.rain.voice[0].mode[NOISE_DROP_CLICK].previous - 2.0) < 1e-6);
  assert(fabs(b.rain.voice[0].mode[NOISE_DROP_BUBBLE].previous /
              a.rain.voice[0].mode[NOISE_DROP_BUBBLE].previous - 4.0) < 1e-6);

  double r = drop.bubble_radius_m;
  double damping = (0.13 / r + 0.0072 / pow(r, 1.5)) / 4.0;
  double expected_radius = exp(-damping / NOISE_SAMPLE_RATE_HZ);
  double bubble_radius = sqrt(a.rain.voice[0].mode[NOISE_DROP_BUBBLE].radius_squared);
  assert(fabs(bubble_radius - expected_radius) < 1e-6);
}

static void test_automatic_water_bubbles(void) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.rain.max_drops_per_s = 2000.0f;
  clear_surface_coverage(&c);
  c.rain.surface[WATER].coverage = 1.0f;
  c.rain.surface[WATER].bubble_probability = 1.0f;
  c.rain.surface[WATER].bubble_radius_min_m = 0.0006f;
  c.rain.surface[WATER].bubble_radius_max_m = 0.0012f;
  assert(noise_init(&a, &c, 41) == NOISE_OK);
  int16_t frame[2];
  for (unsigned i = 0; i < 10000 && a.state.generated_drops == 0; ++i) {
    noise_fill(&a, frame, 1);
  }
  assert(a.state.generated_drops == 1);
  noise_mode bubble = a.rain.voice[0].mode[NOISE_DROP_BUBBLE];
  assert(bubble.remaining > 0);
  double q = sqrt(bubble.radius_squared);
  double frequency = acos(bubble.coefficient / (2.0 * q)) *
                     NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  double radius = sqrt(3.0 * 1.4 * 101325.0 / 1000.0) /
                  (2.0 * TEST_PI * frequency);
  assert(radius >= c.rain.surface[WATER].bubble_radius_min_m);
  assert(radius <= c.rain.surface[WATER].bubble_radius_max_m);

  c.rain.surface[WATER].bubble_probability = 0.0f;
  assert(noise_init(&a, &c, 41) == NOISE_OK);
  for (unsigned i = 0; i < 10000 && a.state.generated_drops == 0; ++i) {
    noise_fill(&a, frame, 1);
  }
  assert(a.state.generated_drops == 1);
  assert(a.rain.voice[0].mode[NOISE_DROP_BUBBLE].remaining == 0);
}

static void test_roof_surfaces(void) {
  noise_config c = silent_config();
  droplet drop = water_drop();
  drop.bubble_radius_m = 0.0f;
  drop.surface = PLASTIC;
  assert(noise_init(&a, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  double plastic_damping = resonance_damping(&a);
  float plastic_lowpass = a.rain.voice[0].material_lowpass_alpha;
  assert(fabs(plastic_damping - 110.0) < 0.01);
  assert(fabs(plastic_lowpass - (-expm1(-2.0 * TEST_PI * 1600.0 /
                                        NOISE_SAMPLE_RATE_HZ))) < 1e-6);

  drop.surface = ASPHALT;
  assert(noise_init(&b, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  double asphalt_damping = resonance_damping(&b);
  assert(fabs(asphalt_damping - 1600.0) < 0.1);
  assert(b.rain.voice[0].material_lowpass_alpha == 1.0f);

  drop.surface = ASPHALT_ROOF;
  assert(noise_init(&b, &c, 51) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  double roof_damping = resonance_damping(&b);
  assert(fabs(roof_damping - 300.0) < 0.1);
  assert(b.rain.voice[0].material_lowpass_alpha < plastic_lowpass);
  assert(b.rain.voice[0].material_lowpass_alpha > 0.0f);
}

static void test_surface_list(void) {
  static const char *names[] = {
    [WATER] = "Water", [DIRT] = "Dirt", [LEAF] = "Leaf", [CONCRETE] = "Concrete",
    [GLASS] = "Glass", [METAL] = "Metal", [PLASTIC] = "Plastic", [ASPHALT] = "Asphalt",
    [ASPHALT_ROOF] = "Asphalt roof"};
  noise_config c = silent_config();
  assert(c.rain.surface_count == NOISE_MAX_SURFACES);
  for (unsigned i = 0; i < NOISE_MAX_SURFACES; ++i) {
    assert(strcmp(c.rain.surface[i].name, names[i]) == 0);
  }
  c.rain.surface_count = 0;
  assert(!noise_config_valid(&c));
  c.rain.surface_count = NOISE_MAX_SURFACES + 1;
  assert(!noise_config_valid(&c));
  c = silent_config();
  memset(c.rain.surface[METAL].name, 'x', sizeof(c.rain.surface[METAL].name));
  assert(!noise_config_valid(&c));

  /* Entries past the count are neither validated nor chosen. */
  noise_config short_list = silent_config();
  short_list.storm.fixed.rain_mm_h = 10.0f;
  short_list.rain.max_drops_per_s = 2000.0f;
  short_list.rain.surface_count = 1;
  for (unsigned i = 1; i < NOISE_MAX_SURFACES; ++i) short_list.rain.surface[i].coverage = 5.0f;
  short_list.rain.surface[LEAF].lowpass_hz = NAN;
  noise_config water_only = short_list;
  water_only.rain.surface_count = NOISE_MAX_SURFACES;
  for (unsigned i = 1; i < NOISE_MAX_SURFACES; ++i) {
    water_only.rain.surface[i] = water_only.rain.surface[WATER];
    water_only.rain.surface[i].coverage = 0.0f;
  }
  static int16_t reference[2 * NOISE_SAMPLE_RATE_HZ];
  assert(noise_init(&a, &short_list, 17) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(noise_init(&b, &water_only, 17) == NOISE_OK);
  noise_fill(&b, reference, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.generated_drops > 1000);
  assert(memcmp(audio, reference, sizeof(audio)) == 0);

  droplet drop = water_drop();
  drop.surface = 1;
  assert(noise_trigger_drop(&a, &drop) == NOISE_INVALID_DROP);
  drop.surface = 0;
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
}

/* Share of voice-samples on unfiltered surface 0 against its filtered copy, surface 1. */
static double unfiltered_share(float coverage) {
  noise_config c = silent_config();
  c.storm.fixed.rain_mm_h = 10.0f;
  c.rain.max_drops_per_s = 2000.0f;
  c.rain.surface_count = 2;
  c.rain.surface[1] = c.rain.surface[WATER];
  c.rain.surface[1].lowpass_hz = 20000.0f;
  c.rain.surface[WATER].coverage = coverage;
  c.rain.surface[1].coverage = 1.0f;
  assert(noise_init(&a, &c, 23) == NOISE_OK);
  unsigned long unfiltered = 0, total = 0;
  int16_t frame[2];
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ; ++n) {
    noise_fill(&a, frame, 1);
    for (unsigned v = 0; v < a.state.active_drops; ++v) {
      unfiltered += a.rain.voice[v].material_lowpass_alpha == 1.0f;
      ++total;
    }
  }
  assert(total > 10000);
  return (double)unfiltered / (double)total;
}

/* Arrivals split by each surface's share of the ground. */
static void test_surface_coverage(void) {
  assert(fabs(unfiltered_share(1.0f) - 0.5) < 0.1);
  assert(fabs(unfiltered_share(2.0f) - 2.0 / 3.0) < 0.1);
  assert(unfiltered_share(0.0f) == 0.0);
}

static void test_bubble_on_solid(void) {
  noise_config c = silent_config();
  clear_surface_coverage(&c);
  c.storm.fixed.rain_mm_h = 10.0f;
  c.rain.max_drops_per_s = 2000.0f;
  c.rain.surface[METAL].coverage = 1.0f;
  c.rain.surface[METAL].bubble_probability = 1.0f;
  assert(noise_init(&a, &c, 61) == NOISE_OK);
  int16_t frame[2];
  for (unsigned i = 0; i < 10000 && a.state.generated_drops == 0; ++i) {
    noise_fill(&a, frame, 1);
  }
  assert(a.state.generated_drops == 1);
  const noise_drop_voice *voice = &a.rain.voice[0];
  assert(voice->mode[NOISE_DROP_RESONANCE].remaining > 0);
  assert(voice->mode[NOISE_DROP_BUBBLE].remaining + voice->mode[NOISE_DROP_BUBBLE].delay > 0);

  droplet drop = water_drop();
  drop.surface = METAL;
  assert(noise_init(&b, &c, 61) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_mode bubble = b.rain.voice[0].mode[NOISE_DROP_BUBBLE];
  double frequency = acos(bubble.coefficient / (2.0 * sqrt(bubble.radius_squared))) *
                     NOISE_SAMPLE_RATE_HZ / (2.0 * TEST_PI);
  assert(fabs(frequency - 8208.11) < 0.01);
}

static void test_custom_click(void) {
  noise_config c = silent_config();
  noise_surface *dirt = &c.rain.surface[DIRT];
  dirt->click_frequency_min_hz = 5000.0f;
  dirt->click_frequency_max_hz = 5000.0f;
  dirt->click_damping_ratio = 3.0f;
  dirt->detune = 0.0f;
  droplet drop = water_drop();
  drop.surface = DIRT;
  drop.bubble_radius_m = 0.0f;
  assert(noise_init(&a, &c, 71) == NOISE_OK);
  uint32_t rng = a.rain.drop_rng;
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  /* Fixed click gain, click frequency, and detune need no random draws. */
  assert(a.rain.drop_rng == rng);
  noise_mode click = a.rain.voice[0].mode[NOISE_DROP_CLICK];
  double radius = exp(-3.0 * 5000.0 / NOISE_SAMPLE_RATE_HZ);
  assert(fabs(sqrt(click.radius_squared) - radius) < 1e-6);
  assert(fabs(click.coefficient -
              2.0 * radius * cos(2.0 * TEST_PI * 5000.0 / NOISE_SAMPLE_RATE_HZ)) < 2e-6);
  assert(fabs(resonance_damping(&a) - 1200.0) < 0.1);
}

static void test_lifetimes_and_capacity(void) {
  noise_config c = silent_config();
  c.reverb_gain = 0.3f;
  assert(noise_init(&a, &c, 1) == NOISE_OK);
  droplet drop = water_drop();
  drop.surface = METAL;
  drop.bubble_radius_m = 0.0f;
  for (unsigned i = 0; i < NOISE_MAX_DROPLETS; ++i) assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_VOICE_LIMIT);
  assert(a.state.dropped_drops == 1);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  assert(a.state.active_drops == 0);
  assert(a.state.peak_active_drops == NOISE_MAX_DROPLETS);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  for (unsigned second = 0; second < 5; ++second) noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ);
  for (unsigned i = 0; i < 2 * NOISE_SAMPLE_RATE_HZ; ++i) assert(audio[i] == 0);
}

/* A surface's gain scales its whole drop; the rain gain may boost past 1. */
static void test_surface_gain(void) {
  noise_config c = silent_config();
  c.rain.gain = 4.0f;
  droplet drop = water_drop();
  assert(noise_init(&a, &c, 5) == NOISE_OK);
  assert(noise_trigger_drop(&a, &drop) == NOISE_OK);
  c.rain.surface[WATER].gain = 0.5f;
  assert(noise_init(&b, &c, 5) == NOISE_OK);
  assert(noise_trigger_drop(&b, &drop) == NOISE_OK);
  noise_fill(&a, audio, NOISE_SAMPLE_RATE_HZ / 10);
  int16_t frame[2];
  int loud = 0;
  for (unsigned n = 0; n < NOISE_SAMPLE_RATE_HZ / 10; ++n) {
    noise_fill(&b, frame, 1);
    assert(abs(audio[2 * n] - 2 * frame[0]) <= 1 && abs(audio[2 * n + 1] - 2 * frame[1]) <= 1);
    if (abs(audio[2 * n]) > loud) loud = abs(audio[2 * n]);
  }
  assert(loud > 100);

  c = silent_config();
  c.rain.gain = 4.0f;
  c.rain.surface[WATER].gain = 4.0f;
  assert(noise_config_valid(&c));
  c.rain.gain = 4.01f;
  assert(!noise_config_valid(&c));
  c.rain.gain = 4.0f;
  c.rain.surface[WATER].gain = 4.01f;
  assert(!noise_config_valid(&c));
}

void run_rain_tests(void) {
  test_surface_gain();
  test_bubble_physics();
  test_water_controls();
  test_automatic_water_bubbles();
  test_roof_surfaces();
  test_surface_list();
  test_surface_coverage();
  test_bubble_on_solid();
  test_custom_click();
  test_lifetimes_and_capacity();
}
