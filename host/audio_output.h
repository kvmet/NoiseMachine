#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <AudioToolbox/AudioToolbox.h>
#include <stdint.h>

#include "noise_core.h"

/* Plays one engine through the default output device. Only the render thread
   touches the engine after create; every call below runs on the control thread
   and takes effect at the next render. */
typedef struct audio_output audio_output;

/* Returns kAudio_ParamError when config fails noise_config_valid. */
OSStatus audio_output_create(audio_output **output, const noise_config *config, uint32_t seed);
void audio_output_destroy(audio_output *output);
OSStatus audio_output_start(audio_output *output);
OSStatus audio_output_stop(audio_output *output);
/* Leaves the playing config unchanged on NOISE_INVALID_CONFIG. */
noise_result audio_output_set_config(audio_output *output, const noise_config *config);
/* Restarts the engine from seed with the current config. */
void audio_output_reset(audio_output *output, uint32_t seed);
/* position must be inside the range noise_trigger_thunder accepts. */
void audio_output_strike(audio_output *output, position_polar position);
/* Copies the status after the last render into status. Returns 0 and leaves status
   unchanged when nothing rendered since the last call. */
int audio_output_status(audio_output *output, noise_status *status);

#endif
