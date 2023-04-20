#include <cmath>

#include "hardware/divider.h"
#include "channel.h"
#include "midi.h"

bool Channel::init = false;
uint8_t Channel::pan_table[128];

Channel::Channel() :
	control{0, }
{
	if (!init) {
		init = true;
		for (uint8_t i = 0; i < 128; ++i) {
			pan_table[i] = 127 * sqrtf(i / 127.0);
		}
	}

	update_cc(volume, 127);
	update_cc(pan, 64);
	update_bend(0, 64);
}

void Channel::update_cc(uint8_t cc, uint8_t v)
{
	control[cc] = v;

	if (cc == pan) {
		pan_l = pan_table[v];
		pan_r = pan_table[127 - v];
	}
}

void Channel::update_bend(uint8_t lsb, uint8_t msb)
{
	extern int16_t powers[];
	static int16_t bend_x = 0x8000;				// impossible value
	bend = (int16_t)((msb << 7) | lsb) - 8192;

	// calculate frequency scaling amount if changed
	if (bend != bend_x) {

		hw_divider_divmod_s32_start(bend * bend_range, 12);
		auto res = hw_divider_result_wait();
		uint16_t offset = 8192 + to_quotient_s32(res);

		bend_f = powers[offset];
		bend_x = bend;
	}
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
