#include <stdio.h>

#include "test_support.h"

int main(void) {
  run_engine_tests();
  run_ambient_tests();
  run_wind_tests();
  run_crickets_tests();
  run_cicadas_tests();
  run_thunder_tests();
  run_rain_tests();
  run_spatial_tests();
  run_weather_tests();
  run_reverb_tests();
  printf("core checks passed; engine size: %zu bytes\n", sizeof(noise_gen));
  return 0;
}
