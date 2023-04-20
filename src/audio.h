#pragma once

#include "pico/audio_i2s.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_buffer_pool *audio_init();

#ifdef __cplusplus
};
#endif
