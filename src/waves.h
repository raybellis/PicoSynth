#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t* waves[];
const int wave_shift = 11;
const int wave_len = (1 << wave_shift);
const int wave_max = wave_len * 0x10000;

#ifdef __cplusplus
};
#endif
