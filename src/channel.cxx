#include "channel.h"
#include "midi.h"

Channel::Channel() :
	control{0, },
	bend{0}
{
	control[volume] = 100;
	control[pan] = 64;
}

void Channel::midi_in(uint8_t cmd, uint8_t d1, uint8_t d2)
{
	switch (cmd >> 4) {
		case 0xb0:
			control[d1] = d2;
			break;
		case 0xc0:
			// program change
			break;
		case 0xd0:
			pressure = d1;
			break;
		case 0xe0:
			bend = ((d1 << 7) | d2) - 0x2000;
			break;
	}
}
