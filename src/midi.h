#pragma once

#include <cstdint>

enum CC : uint8_t {
	modwheel	= 1,
	volume		= 7,
	pan			= 10,
	expression	= 11,
	sustain		= 64,
	portamento	= 65,

	// sound controllers, which offset the patch either side of 64
	// rather than setting it outright
	resonance	= 71,
	brightness	= 74,

	// channel mode messages.  a file that sends a pedal down and then
	// stops leaves its notes ringing without these
	all_sound_off	= 120,
	all_notes_off	= 123
};
