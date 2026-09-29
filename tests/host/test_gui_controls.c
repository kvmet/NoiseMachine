#include "gui_controls.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int close_to(float actual, float expected) {
  return fabsf(actual - expected) <= 1e-5f * fmaxf(1.0f, fabsf(expected));
}

static void test_table_complete(void) {
  for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
    const gui_control *control = &gui_controls[id];
    assert(control->label && control->format);
    assert(control->minimum < control->maximum);
    assert(control->display_factor > 0.0f);
    size_t size = control->scope == GUI_SCOPE_SURFACE ? sizeof(noise_surface) :
                                                        sizeof(noise_config);
    assert(control->offset + sizeof(float) <= size);
    if (control->scale == GUI_SCALE_LOG_OFF) {
      assert(control->minimum == 0.0f);
      assert(control->smallest > 0.0f && control->smallest < control->maximum);
    }
  }
}

static void test_startup_valid(void) {
  noise_config c;
  gui_startup_config(&c);
  assert(noise_config_valid(&c));
  for (unsigned surface = 0; surface < c.rain.surface_count; ++surface) {
    for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
      float value = gui_control_get(&c, surface, id);
      assert(value >= gui_controls[id].minimum && value <= gui_controls[id].maximum);
    }
  }
}

/* Any value on any control keeps the config valid, so the table ranges match the
   engine and the follow rules cover every cross-field constraint. */
static void test_every_edit_stays_valid(void) {
  static const float fractions[] = {0.0f, 1.0f, 0.5f, 0.0f, 0.0001f, 0.25f, 1.0f};
  for (int manual = 0; manual <= 1; ++manual) {
    noise_config c;
    gui_startup_config(&c);
    c.storm.manual = manual;
    assert(noise_config_valid(&c));
    for (size_t f = 0; f < sizeof(fractions) / sizeof(fractions[0]); ++f) {
      for (unsigned surface = 0; surface < c.rain.surface_count; ++surface) {
        for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
          const gui_control *control = &gui_controls[id];
          float value = control->minimum + fractions[f] * (control->maximum - control->minimum);
          gui_control_set(&c, surface, id, value);
          assert(noise_config_valid(&c));
          gui_control_set(&c, surface, id, -INFINITY);
          assert(noise_config_valid(&c));
          gui_control_set(&c, surface, id, INFINITY);
          assert(noise_config_valid(&c));
        }
      }
    }
  }
}

static void test_follow_rules(void) {
  noise_config c;
  gui_startup_config(&c);
  gui_control_set(&c, 0, CONTROL_STORM_MAX_SEVERITY, 0.5f);
  gui_control_set(&c, 0, CONTROL_STORM_MIN_SEVERITY, 0.9f);
  assert(c.storm.max_severity == 0.9f);
  gui_control_set(&c, 0, CONTROL_STORM_MAX_SEVERITY, 0.1f);
  assert(c.storm.min_severity == 0.1f);

  unsigned last = c.rain.surface_count - 1;
  for (unsigned surface = 0; surface < last; ++surface) {
    assert(gui_control_set(&c, surface, CONTROL_SURFACE_COVERAGE, 0.0f) == NULL);
  }
  assert(gui_control_set(&c, last, CONTROL_SURFACE_COVERAGE, 0.0f) != NULL);
  assert(gui_control_get(&c, last, CONTROL_SURFACE_COVERAGE) > 0.0f);

  gui_control_set(&c, 4, CONTROL_CLICK_FREQUENCY_MAX, 500.0f);
  assert(c.rain.surface[4].click_frequency_min_hz == 500.0f);
  assert(c.rain.surface[3].click_frequency_max_hz == 16000.0f);
  gui_control_set(&c, 4, CONTROL_LOWPASS, 5.0f);
  assert(c.rain.surface[4].lowpass_hz == 20.0f);
  gui_control_set(&c, 4, CONTROL_LOWPASS, 0.0f);
  assert(c.rain.surface[4].lowpass_hz == 0.0f);
}

static void test_slider_round_trip(void) {
  static const float fractions[] = {0.0f, 0.1f, 0.5f, 0.9f, 1.0f};
  for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
    const gui_control *control = &gui_controls[id];
    double low, high;
    gui_slider_range(id, &low, &high);
    assert(low < high);
    for (size_t f = 0; f < sizeof(fractions) / sizeof(fractions[0]); ++f) {
      float value = control->minimum + fractions[f] * (control->maximum - control->minimum);
      double position = gui_slider_position(id, value);
      assert(position >= low - 1e-9 && position <= high + 1e-9);
      assert(close_to(gui_slider_value(id, position), value));
    }
  }
  double low, high;
  gui_slider_range(CONTROL_SURFACE_COVERAGE, &low, &high);
  assert(gui_slider_value(CONTROL_SURFACE_COVERAGE, low) == 0.0f);
  assert(gui_slider_position(CONTROL_SURFACE_COVERAGE, 0.0f) == low);
  gui_slider_range(CONTROL_LOWPASS, &low, &high);
  assert(gui_slider_value(CONTROL_LOWPASS, low) == 0.0f);
  assert(gui_slider_position(CONTROL_LOWPASS, 0.0f) == low);
}

static void test_text(void) {
  char text[32];
  gui_control_format(CONTROL_BUBBLE_RADIUS_MIN, 0.00023f, text, sizeof(text));
  assert(strcmp(text, "0.23") == 0);
  float value = 0.0f;
  assert(gui_control_parse(CONTROL_BUBBLE_RADIUS_MIN, "0.5", &value));
  assert(close_to(value, 0.0005f));
  gui_control_format(CONTROL_SURFACE_COVERAGE, 6.31e-05f, text, sizeof(text));
  assert(strcmp(text, "6.31e-05") == 0);
  gui_control_format(CONTROL_FIXED_WIND_BEARING, -1.5707963f, text, sizeof(text));
  assert(strcmp(text, "-90") == 0);
  assert(gui_control_parse(CONTROL_FIXED_WIND_BEARING, "45", &value));
  assert(close_to(value, 0.78539816f));
  static const char *invalid[] = {"", "abc", "1x", "nan", "inf", "1e999", " "};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    value = 7.0f;
    assert(!gui_control_parse(CONTROL_MASTER_GAIN, invalid[i], &value));
    assert(value == 7.0f);
  }
}

static void test_surface_add(void) {
  noise_config c;
  gui_startup_config(&c);
  noise_config full = c;
  assert(gui_surface_add(&c, WATER) != NULL);
  assert(memcmp(&c, &full, sizeof(c)) == 0);

  assert(gui_surface_delete(&c, METAL) == NULL);
  assert(c.rain.surface_count == NOISE_MAX_SURFACES - 1);
  assert(strcmp(c.rain.surface[METAL].name, "Plastic") == 0);
  assert(gui_surface_add(&c, WATER) == NULL);
  noise_surface *added = &c.rain.surface[NOISE_MAX_SURFACES - 1];
  assert(strcmp(added->name, "Water copy") == 0);
  assert(added->coverage == 0.0f);
  noise_surface expected = c.rain.surface[WATER];
  expected.coverage = 0.0f;
  memcpy(expected.name, added->name, sizeof(expected.name));
  assert(memcmp(added, &expected, sizeof(expected)) == 0);
  assert(noise_config_valid(&c));

  gui_surface_delete(&c, 0);
  unsigned roof = ASPHALT_ROOF - 2;
  assert(strcmp(c.rain.surface[roof].name, "Asphalt roof") == 0);
  assert(gui_surface_add(&c, roof) == NULL);
  assert(strcmp(c.rain.surface[NOISE_MAX_SURFACES - 1].name, "Asphalt roof co") == 0);
  assert(noise_config_valid(&c));
}

static void test_surface_delete(void) {
  noise_config c;
  gui_startup_config(&c);
  while (c.rain.surface_count > 1) {
    assert(gui_surface_delete(&c, 0) == NULL || c.rain.surface[0].coverage > 0.0f);
    assert(noise_config_valid(&c));
  }
  assert(strcmp(c.rain.surface[0].name, "Asphalt roof") == 0);
  noise_config single = c;
  assert(gui_surface_delete(&c, 0) != NULL);
  assert(memcmp(&c, &single, sizeof(c)) == 0);

  gui_startup_config(&c);
  for (unsigned i = 0; i < c.rain.surface_count; ++i) c.rain.surface[i].coverage = 0.0f;
  c.rain.surface[DIRT].coverage = 1.0f;
  assert(gui_surface_delete(&c, DIRT) != NULL);
  assert(c.rain.surface[0].coverage > 0.0f);
  assert(noise_config_valid(&c));
}

static void test_surface_rename(void) {
  noise_config c;
  gui_startup_config(&c);
  assert(gui_surface_rename(&c, METAL, "") != NULL);
  assert(strcmp(c.rain.surface[METAL].name, "Metal") == 0);
  assert(gui_surface_rename(&c, METAL, "Tin roof") == NULL);
  assert(strcmp(c.rain.surface[METAL].name, "Tin roof") == 0);
  assert(gui_surface_rename(&c, METAL, "abcdefghijklmno") == NULL);
  assert(gui_surface_rename(&c, METAL, "abcdefghijklmnop") != NULL);
  assert(strcmp(c.rain.surface[METAL].name, "abcdefghijklmno") == 0);
  /* A two-byte character that would straddle the limit is dropped whole. */
  assert(gui_surface_rename(&c, METAL, "aaaaaaaaaaaaaa\xc3\xa9") != NULL);
  assert(strcmp(c.rain.surface[METAL].name, "aaaaaaaaaaaaaa") == 0);
  assert(noise_config_valid(&c));
}

static void test_track_shares(void) {
  noise_config c;
  gui_startup_config(&c);
  gui_control_set(&c, 0, CONTROL_SHAPE_BUILD, 0.8f);
  assert(c.storm.shape.decay_share <= 0.2f + 1e-6f);
  gui_control_set(&c, 0, CONTROL_SHAPE_DECAY, 0.9f);
  assert(c.storm.shape.build_share <= 0.1f + 1e-6f);
  assert(c.storm.shape.build_share + c.storm.shape.decay_share <= 1.0f);
  assert(noise_config_valid(&c));
}

static void test_status_text(void) {
  noise_config c;
  gui_startup_config(&c);
  noise_status s = {0};
  s.weather.temperature_c = 18.0f;
  s.weather.rain_mm_h = 12.0f;
  s.weather.wind_mean_m_s = 9.0f;
  s.weather.wind_m_s = 10.5f;
  s.weather.lightning_per_min = 2.0f;
  s.weather.cell.distance_m = 20000.0f;
  s.rain_played_per_s = 2000.0f;
  s.rain_arrivals_per_s = 310000.0f;
  s.bed_share = 0.95f;
  s.cricket_quiet = NOISE_QUIET_RAIN | NOISE_QUIET_WIND;
  s.cicada_quiet = NOISE_QUIET_COLD | NOISE_QUIET_RAIN;
  s.cicada_activity = 0.4f;
  char text[256];
  gui_layer_status(GUI_LAYER_RAIN, &s, &c, text, sizeof(text));
  assert(strcmp(text, "12.0 mm/h · 2k of 310k drops/s played") == 0);
  gui_layer_status(GUI_LAYER_BED, &s, &c, text, sizeof(text));
  assert(strcmp(text, "Plays 95% of rain power at ×1") == 0);
  gui_layer_status(GUI_LAYER_CRICKETS, &s, &c, text, sizeof(text));
  assert(strcmp(text, "Silent: rain 12.00 mm/h, above 0.50; wind 9.0 m/s, above 8.0") == 0);
  gui_layer_status(GUI_LAYER_CICADAS, &s, &c, text, sizeof(text));
  assert(strcmp(text, "Silent: 18.0 °C, below 22.0 °C; rain 12.00 mm/h, above 0.50 · "
                      "chorus fading, 40%") == 0);
  gui_layer_status(GUI_LAYER_THUNDER, &s, &c, text, sizeof(text));
  assert(strstr(text, "strikes past 15 km are silent"));
  gui_silenced_summary(&s, &c, text, sizeof(text));
  assert(strcmp(text, "Silenced: crickets (rain, wind) · cicadas (cold, rain)") == 0);
  c.cicadas.gain = 0.0f;
  gui_silenced_summary(&s, &c, text, sizeof(text));
  assert(strcmp(text, "Silenced: crickets (rain, wind)") == 0);
  gui_layer_status(GUI_LAYER_CICADAS, &s, &c, text, sizeof(text));
  assert(strcmp(text, "Off") == 0);
  /* Long text is cut, never overrun. */
  char small[8];
  gui_layer_status(GUI_LAYER_CRICKETS, &s, &c, small, sizeof(small));
  assert(strlen(small) == sizeof(small) - 1);
  gui_silenced_summary(&s, &c, small, sizeof(small));
  assert(strlen(small) < sizeof(small));
}

void run_gui_controls_tests(void) {
  test_table_complete();
  test_startup_valid();
  test_every_edit_stays_valid();
  test_follow_rules();
  test_slider_round_trip();
  test_text();
  test_surface_add();
  test_surface_delete();
  test_surface_rename();
  test_track_shares();
  test_status_text();
}
