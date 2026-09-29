#include "noise_core.h"

#define NOISE_RNG_SEED 1u
#define NOISE_BATCH_FRAMES 256u

static noise_gen generator;
static int16_t noise_batch[NOISE_BATCH_FRAMES * NOISE_CHANNELS];
static bool noise_ready;

void setup() {
  Serial.begin(115200);
  noise_config config;
  noise_config_default(&config);
  config.ambient_gain[NOISE_KIND_PINK] = 0.0f;
  config.rain_intensity = 0.5f;
  config.vary_rain = 1;
  noise_ready = noise_init(&generator, &config, NOISE_RNG_SEED) == NOISE_OK;
  if (!noise_ready) Serial.println("Invalid noise configuration");
}

void loop() {
  if (!noise_ready) return;
  noise_fill(&generator, noise_batch, NOISE_BATCH_FRAMES);
  // A blocking I2S write must pace these interleaved stereo frames at 44100 Hz.
}
