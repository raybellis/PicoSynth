#pragma once

#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// The audio output backend, behind three calls so that the transport
// can be swapped at compile time.  src/audio_i2s.c is the only
// implementation today; a USB one would present the same interface.
//
// Deliberately no pico_audio types here.  Leaking them was what tied
// audio_task() to one transport, and it is why this header used to
// include pico/audio_i2s.h.

// brings the transport up.  called once, from core 0
void		audio_init(void);

// a buffer to fill with 2 * BUFFER_SIZE interleaved int16_t samples,
// or NULL if none is free.
//
// this never blocks, and must not: it is called from core 1, where
// blocking once cost a whole boot hang - see the note in CLAUDE.md
// about take_audio_buffer and __wfe().  poll it instead
int16_t*	audio_take(void);

// hands back the buffer from the most recent audio_take().  only one
// may be outstanding at a time, which is why this takes no argument
void		audio_give(void);

#ifdef __cplusplus
};
#endif
