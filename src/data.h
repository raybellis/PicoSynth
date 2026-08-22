#pragma once

#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// These are computed at startup by tables_init(), not baked into the
// image - so they live in .bss and cost nothing in flash.  They are
// deliberately not const; a const table would go to .rodata, which is
// in flash and cannot be written.
//
// Declaring them here rather than in each consumer is what keeps the
// definitions in tables.c checked against every use: tables.c
// includes this header itself, so a mismatched type is a compile
// error rather than a silent link.

extern uint32_t	note_table[128];		// 16.16 phase increments
extern uint8_t	pan_table[128];			// equal-power pan, 7-bit
extern uint16_t	power_table[POWER_LEN];	// 2^(r/POWER_LEN), 1:15
extern int16_t	svf_table[SVF_LEN];		// 2*sin(pi*Fc/Fs), 2:14
extern uint16_t	q_table[SVF_Q_LEN];		// 1/Q, 1:15
extern uint16_t	scale_table[SVF_Q_LEN];	// filter input scaling, 1:15
extern uint16_t	cutoff_table[SVF_Q_LEN];// 7-bit parameter to svf_table index

// call once, before anything reads a table
void tables_init(void);

#ifdef __cplusplus
};
#endif
