#pragma once

#include <stdint.h>
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t* waves[];

// call once, before anything reads a wavetable
void waves_init(void);

#ifdef __cplusplus
};
#endif
