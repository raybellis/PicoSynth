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

// a, d and r are rates, not times, and each is used as the amount to
// move the level by on every buffer.  A rate of zero would move it by
// nothing, so the phase would never end: an attack of zero never
// reaches peak, which means the note never sounds at all rather than
// sounding instantly.  Hence the floor of 1.
//
// This used to be written as three `if (a < 1) a = 1;` in the
// constructor body, which did nothing - the members were already
// initialised from the parameters by the list above, and the body was
// assigning to the parameters.  No preset had a zero rate, so it stayed
// latent.
ADSR::ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r)
	: s(s << 8),
	  a(a < 1 ? 1 : a),
	  d(d < 1 ? 1 : d),
	  r(r < 1 ? 1 : r),
	  phase(off)
{
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
