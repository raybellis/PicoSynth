#pragma once

#include <cstdint>

class Patch;

class Channel {

	int16_t			bend = 0;
	uint8_t			control[128] = { 0, };
	uint8_t			pressure = 0;
	uint8_t			prognum = 0;

public:
					Channel();

public:
	void			midi_in(uint8_t cmd, uint8_t d1, uint8_t d2);

};
