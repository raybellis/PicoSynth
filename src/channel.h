#pragma once

#include <cstdint>

class Channel {

protected:
	void			update_cc(uint8_t cc, uint8_t value);
	void			update_bend(uint8_t lsb, uint8_t msb);

public:
	int16_t			bend = 0;
	uint8_t			control[128] = { 0, };
	uint8_t			pressure = 0;
	uint8_t			prognum = 0;

	float			bend_f = 0.0;
	uint8_t			pan_l = 0;
	uint8_t			pan_r = 0;

public:
					Channel();

public:
	void			midi_in(uint8_t cmd, uint8_t d1, uint8_t d2);

};
