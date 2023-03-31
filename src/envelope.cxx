#include "envelope.h"

//--------------------------------------------------------------------+
// Generic Envelope with 16 bits of resolution
//--------------------------------------------------------------------+
Envelope::Envelope()
	: _level(0)
{
}

//--------------------------------------------------------------------+
// Standard four phase ADSR Envelope
//--------------------------------------------------------------------+

ADSR::ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r)
	: s(s << 8), a(a), d(d), r(r), phase(off)
{
	if (a < 1) a = 1;
	if (d < 1) d = 1;
	if (r < 1) r = 1;
}

int16_t ADSR::update()
{
	auto& v = _level;

	switch (phase) {
		case attack: {
			v += (a << 7);
			if (v >= 0x7fff) {
				v = 0x7fff;
				phase = decay;
			}
			break;
		}
		case decay: {
			v -= (d << 5);
			if (v <= s) {
				v = s;
				phase = v ? sustain : off;	// ADSR with no sustain
			}
			break;
		}
		case release: {
			v -= (r << 4);
			if (v <= 0) {
				v = 0;
				phase = off;
			}
			break;
		}
		default:
			break;
	}

	return (int16_t)v;
}

void ADSR::gate_on()
{
	phase = attack;
}

void ADSR::gate_off()
{
	phase = release;
}
