#pragma once

#include <stdint.h>
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t* waves[];

const int wave_shift = WAVE_SHIFT;
const int wave_len = WAVE_LEN;
const int wave_max = WAVE_MAX;

#ifdef __cplusplus
};
#endif
