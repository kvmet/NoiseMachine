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
    assert(control->offset + sizeof(float) <= sizeof(noise_config));
  }
}

static void test_startup_valid(void) {
  noise_config c;
  gui_startup_config(&c);
  assert(noise_config_valid(&c));
  for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
    float value = gui_control_get(&c, id);
    assert(value >= gui_controls[id].minimum && value <= gui_controls[id].maximum);
  }
}

/* Any value on any control keeps the config valid, so the table ranges match the
   engine and the follow rules cover every cross-field constraint. */
static void test_every_edit_stays_valid(void) {
  static const float fractions[] = {0.0f, 1.0f, 0.5f, 0.0f, 0.25f, 1.0f};
  for (int vary = 0; vary <= 1; ++vary) {
    noise_config c;
    gui_startup_config(&c);
    gui_set_vary(&c, vary);
    assert(noise_config_valid(&c));
    for (size_t f = 0; f < sizeof(fractions) / sizeof(fractions[0]); ++f) {
      for (gui_control_id id = 0; id < CONTROL_COUNT; ++id) {
        const gui_control *control = &gui_controls[id];
        float value = control->minimum + fractions[f] * (control->maximum - control->minimum);
        gui_control_set(&c, id, value);
        assert(noise_config_valid(&c));
        gui_control_set(&c, id, -INFINITY);
        assert(noise_config_valid(&c));
        gui_control_set(&c, id, INFINITY);
        assert(noise_config_valid(&c));
      }
    }
  }
}

static void test_follow_rules(void) {
  noise_config c;
  gui_startup_config(&c);
  gui_control_set(&c, CONTROL_MAX_INTENSITY, 0.5f);
  gui_control_set(&c, CONTROL_MIN_INTENSITY, 0.9f);
  assert(c.weather.max_intensity == 0.9f);
  assert(c.weather.intensity == 0.9f);
  gui_control_set(&c, CONTROL_THUNDER_MIN_DISTANCE, 5000.0f);
  gui_control_set(&c, CONTROL_THUNDER_MAX_DISTANCE, 1000.0f);
  assert(c.thunder.min_distance_m == 1000.0f);

  gui_set_vary(&c, 0);
  gui_control_set(&c, CONTROL_RAIN_INTENSITY, 0.1f);
  assert(c.weather.intensity == 0.1f);
  gui_set_vary(&c, 1);
  assert(c.weather.intensity == c.weather.min_intensity);

  for (unsigned surface = 0; surface < NOISE_SURFACE_SLOTS - 1; ++surface) {
    assert(gui_control_set(&c, CONTROL_SURFACE_WEIGHT + surface, 0.0f) == NULL);
  }
  gui_control_id last = CONTROL_SURFACE_WEIGHT + NOISE_SURFACE_SLOTS - 1;
  assert(gui_control_set(&c, last, 0.0f) != NULL);
  assert(gui_control_get(&c, last) > 0.0f);
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
  gui_control_id weight = CONTROL_SURFACE_WEIGHT + WATER;
  double low, high;
  gui_slider_range(weight, &low, &high);
  assert(gui_slider_value(weight, low) == 0.0f);
  assert(gui_slider_position(weight, 0.0f) == low);
}

static void test_text(void) {
  char text[32];
  gui_control_format(CONTROL_WATER_BUBBLE_RADIUS_MIN, 0.00023f, text, sizeof(text));
  assert(strcmp(text, "0.23") == 0);
  float value = 0.0f;
  assert(gui_control_parse(CONTROL_WATER_BUBBLE_RADIUS_MIN, "0.5", &value));
  assert(close_to(value, 0.0005f));
  gui_control_format(CONTROL_SURFACE_WEIGHT + WATER, 6.05624e-05f, text, sizeof(text));
  assert(strcmp(text, "6.05624e-05") == 0);
  static const char *invalid[] = {"", "abc", "1x", "nan", "inf", "1e999", " "};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    value = 7.0f;
    assert(!gui_control_parse(CONTROL_MASTER_GAIN, invalid[i], &value));
    assert(value == 7.0f);
  }
}

void run_gui_controls_tests(void) {
  test_table_complete();
  test_startup_valid();
  test_every_edit_stays_valid();
  test_follow_rules();
  test_slider_round_trip();
  test_text();
}
