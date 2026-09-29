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
  for (int vary = 0; vary <= 1; ++vary) {
    noise_config c;
    gui_startup_config(&c);
    gui_set_vary(&c, vary);
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
  gui_control_set(&c, 0, CONTROL_MAX_INTENSITY, 0.5f);
  gui_control_set(&c, 0, CONTROL_MIN_INTENSITY, 0.9f);
  assert(c.weather.max_intensity == 0.9f);
  assert(c.weather.intensity == 0.9f);
  gui_control_set(&c, 0, CONTROL_THUNDER_MIN_DISTANCE, 5000.0f);
  gui_control_set(&c, 0, CONTROL_THUNDER_MAX_DISTANCE, 1000.0f);
  assert(c.thunder.min_distance_m == 1000.0f);

  gui_set_vary(&c, 0);
  gui_control_set(&c, 0, CONTROL_RAIN_INTENSITY, 0.1f);
  assert(c.weather.intensity == 0.1f);
  gui_set_vary(&c, 1);
  assert(c.weather.intensity == c.weather.min_intensity);

  unsigned last = c.rain.surface_count - 1;
  for (unsigned surface = 0; surface < last; ++surface) {
    assert(gui_control_set(&c, surface, CONTROL_SURFACE_WEIGHT, 0.0f) == NULL);
  }
  assert(gui_control_set(&c, last, CONTROL_SURFACE_WEIGHT, 0.0f) != NULL);
  assert(gui_control_get(&c, last, CONTROL_SURFACE_WEIGHT) > 0.0f);

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
  gui_slider_range(CONTROL_SURFACE_WEIGHT, &low, &high);
  assert(gui_slider_value(CONTROL_SURFACE_WEIGHT, low) == 0.0f);
  assert(gui_slider_position(CONTROL_SURFACE_WEIGHT, 0.0f) == low);
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
  gui_control_format(CONTROL_SURFACE_WEIGHT, 6.05624e-05f, text, sizeof(text));
  assert(strcmp(text, "6.05624e-05") == 0);
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
  assert(added->weight == 0.0f && added->weight_mod == 0.0f);
  noise_surface expected = c.rain.surface[WATER];
  expected.weight = 0.0f;
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
    assert(gui_surface_delete(&c, 0) == NULL || c.rain.surface[0].weight > 0.0f);
    assert(noise_config_valid(&c));
  }
  assert(strcmp(c.rain.surface[0].name, "Asphalt roof") == 0);
  noise_config single = c;
  assert(gui_surface_delete(&c, 0) != NULL);
  assert(memcmp(&c, &single, sizeof(c)) == 0);

  gui_startup_config(&c);
  for (unsigned i = 0; i < c.rain.surface_count; ++i) c.rain.surface[i].weight = 0.0f;
  c.rain.surface[DIRT].weight = 1.0f;
  assert(gui_surface_delete(&c, DIRT) != NULL);
  assert(c.rain.surface[0].weight > 0.0f);
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
}
