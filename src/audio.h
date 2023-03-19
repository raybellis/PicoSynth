#pragma once

#include "pico/audio_i2s.h"

#define SAMPLE_RATE 44100
#define SAMPLES_PER_BUFFER 256

#ifdef __cplusplus
extern "C" {
#endif

struct audio_buffer_pool *audio_init();

#ifdef __cplusplus
};
#endif
