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
		printf("%d %d\n", cc, v);
	}
}

void Channel::update_bend(uint8_t lsb, uint8_t msb)
{
	bend = (int16_t)((msb << 7) | lsb) - 8192;
	bend_f = bend / 8192.0;
}

void Channel::midi_in(uint8_t cmd, uint8_t d1, uint8_t d2)
{
	switch (cmd >> 4) {
		case 0xb0:
			update_cc(d1, d2);
			break;
		case 0xc0:
			// program change
			break;
		case 0xd0:
			pressure = d1;
			break;
		case 0xe0:
			update_bend(d1, d2);
			break;
	}
}
