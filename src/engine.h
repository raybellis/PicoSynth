#pragma once

#include <cstdint>
#include <cstddef>

#include "voice.h"
#include "channel.h"

class SynthEngine {

private:
	VoicePool				pool;
	Channel					channel[16];

private:
	void					note_on(uint8_t chan, uint8_t note, uint8_t vel);
	void					note_off(uint8_t chan, uint8_t note, uint8_t vel);
	void					sustain_off(uint8_t chan);

public:
	// must be called after tables_init(), and before any MIDI arrives
	void					init();

	void					midi_in(uint8_t c, uint8_t d1, uint8_t d2);

public:
	uint32_t				update(int32_t* samples, size_t n);

public:
							SynthEngine();
};
