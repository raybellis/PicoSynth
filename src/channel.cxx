#include <cmath>

#include "hardware/divider.h"
#include "channel.h"
#include "midi.h"

Channel::Channel() :
	control{0, }
{
	set_cc(volume, 127);
	set_cc(pan, 64);
	set_bend(0, 64);
}

void Channel::set_program(uint8_t n)
{
	program = n;
}

void Channel::set_cc(uint8_t cc, uint8_t v)
{
	control[cc] = v;

	if (cc == pan) {
		extern uint8_t pan_table[];
		pan_l = pan_table[v];
		pan_r = pan_table[127 - v];
	}
}

void Channel::set_bend(uint8_t lsb, uint8_t msb)
{
	bend = (int16_t)((msb << 7) | lsb) - 8192;

	// adjust by bend range amount if changed
	if (bend != bend_x) {
		hw_divider_divmod_s32_start(bend * bend_range, 12);
		auto res = hw_divider_result_wait();
		bend_f = to_quotient_s32(res);
		bend_x = bend;
	}
}

void Channel::midi_in(uint8_t c, uint8_t d1, uint8_t d2)
{
	uint8_t cmd = c >> 4;
	switch (cmd) {
		case 0xb: // continuous controller
			set_cc(d1, d2);
			break;
		case 0xc: // program change
			set_program(d1);
			break;
		case 0xd: // channel pressure
			pressure = d1;
			break;
		case 0xe: // pitch bend
			set_bend(d1, d2);
			break;
	}
}
