#ifndef EXPORT_AUDIO_H
#define EXPORT_AUDIO_H

#import <Foundation/Foundation.h>

#include <AudioToolbox/AudioToolbox.h>
#include <stdint.h>

#include "noise_core.h"

/* Renders frames from a fresh engine into an AAC .m4a file. Calls progress with
   the frames written about every ten seconds of audio and at the end. Safe to
   run on any thread. */
OSStatus export_m4a(NSURL *url, const noise_config *config, uint32_t seed, uint32_t frames,
                    void (^progress)(uint32_t done));

#endif
