#include "noise_core.h"

#define NOISE_RNG_SEED 1u
#define NOISE_BATCH_SIZE 256

static noise_gen noise_state;
static int16_t noise_batch[NOISE_BATCH_SIZE];

void setup() {
  noise_init(&noise_state, NOISE_KIND_PINK, NOISE_RNG_SEED);
}

void loop() {
  noise_fill(&noise_state, noise_batch, NOISE_BATCH_SIZE);
  // Send noise_batch to the audio output here.
}
