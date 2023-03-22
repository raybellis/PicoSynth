#pragma once

#include <cstdint>

class Patch;

class Channel {

	int16_t			bend = 0;
	uint8_t			controls[128] = { 0, };
	uint8_t			prognum = 0;
	Patch*			patch = nullptr;

public:
					Channel();

};
