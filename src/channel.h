#pragma once

#include <cstdint>

class Channel {

	// SynthEngine sets this state from MIDI; Voice reads it every buffer
	// while rendering, which is why the synthesis moved to voice.cxx
	friend class			SynthEngine;
	friend class			Voice;

private:
	// applies the power-on defaults.  Separate from the constructor
	// because it reads tables that do not exist until tables_init()
	void					init();

	void					set_program(uint8_t program);

	// advances lfo_pos by one buffer at the current patch's LFO rate
	void					lfo_tick();
	void					set_cc(uint8_t cc, uint8_t value);
	void					set_bend(uint8_t lsb, uint8_t msb);

private:					// state mirroring MIDI values
	const uint8_t			bend_range = 2;
	int16_t					bend = 0;
	uint8_t					control[128];
	uint8_t					pressure;
	uint8_t					program;

private:					// calculated state
	int16_t					bend_f;
	uint8_t					pan_l;
	uint8_t					pan_r;

	// the channel's own LFO phase, advanced once per buffer for every
	// channel rather than once per voice.  A patch that asks for it
	// gets one phase shared by all its notes, which is what makes
	// vibrato coherent instead of a chorus of independent wobbles
	uint32_t				lfo_pos;

private:					// cached state
	int16_t					bend_x = 0xffff;

public:
							Channel();

public:
	void					midi_in(uint8_t cmd, uint8_t d1, uint8_t d2);

};
