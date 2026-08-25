#include <cmath>

#include "hardware/divider.h"
#include "channel.h"
#include "data.h"
#include "midi.h"

// Deliberately does not set the derived values - see init().
//
// Channels are reached through a global SynthEngine, so this runs during
// static initialisation, before main() calls tables_init().  pan_table is
// still all zeros at that point, so setting the defaults here left
// pan_l = pan_r = 0 on every channel, permanently: the only thing that
// recomputes them is an incoming CC 10.
//
// The effect was that any MIDI file which never sends a pan controller
// played in complete silence, while one that does sounded fine - which
// made it look like a property of the file rather than a bug.  It is a
// regression from computing the tables at boot: when they were generated
// into .rodata they were valid this early.
Channel::Channel() :
	control{0, }
{
}

void Channel::init()
{
	set_cc(volume, 127);
	set_cc(pan, 64);

	// expression multiplies volume in the DCA chain, so left at its
	// zero-initialised value it would silence the channel outright -
	// 127 is both the MIDI default and the only safe one
	set_cc(expression, 127);

	// these two offset the patch either side of centre, so they have
	// to start centred - left at zero they would close every filter
	set_cc(brightness, 64);
	set_cc(resonance, 64);

	set_bend(0, 64);
}

void Channel::set_program(uint8_t n)
{
	program = n;
}

void Channel::set_cc(uint8_t cc, uint8_t v)
{
	control[cc] = v;

	if (cc == pan) {	// zero = hard left
		pan_l = pan_table[127 - v];
		pan_r = pan_table[v];
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
