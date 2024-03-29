#pragma once

#include <cstdint>

class Channel {

	friend class			SynthEngine;

private:
	void					set_program(uint8_t program);
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

private:					// cached state
	int16_t					bend_x = 0xffff;

public:
							Channel();

public:
	void					midi_in(uint8_t cmd, uint8_t d1, uint8_t d2);

};
