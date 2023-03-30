#include <cstdio>
#include <cmath>
#include "channel.h"
#include "midi.h"

Channel::Channel() :
	control{0, }
{
	update_cc(volume, 127);
	update_cc(pan, 64);
	update_bend(0, 64);
}

void Channel::update_cc(uint8_t cc, uint8_t v)
{
	control[cc] = v;

	if (cc == pan) {
		pan_l = 127 * sqrtf(v / 127.0);
		pan_r = 127 * sqrtf((127 - v) / 127.0);
	}
}

void Channel::update_bend(uint8_t lsb, uint8_t msb)
{
	const float divisor = 1.0 / (12 * 8192);	// fractions of an octave
	bend = (int16_t)((msb << 7) | lsb) - 8192;
	bend_f = bend ? powf(2.0, divisor * bend_range * bend) : 1.0;
}

void Channel::midi_in(uint8_t c, uint8_t d1, uint8_t d2)
{
	uint8_t cmd = c >> 4;
	switch (cmd) {
		case 0xb:
			update_cc(d1, d2);
			break;
		case 0xc:
			// program change
			break;
		case 0xd:
			pressure = d1;
			break;
		case 0xe:
			update_bend(d1, d2);
			break;
	}
}
