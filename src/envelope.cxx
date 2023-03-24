#include "envelope.h"

//--------------------------------------------------------------------+
// Envelope
//--------------------------------------------------------------------+

ADSR::ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r)
	: phase(off), v(0), a(a), d(d), s(s << 8), r(r)
{
	if (a < 1) a = 0;
	if (d < 1) d = 0;
	if (r < 1) r = 0;
}

uint16_t ADSR::update()
{
	int32_t l = v;	// large and signed to allow overflow
	switch (phase) {
		case attack: {
			l += (a << 7);
			if (l >= 0x7fff) {
				l = 0x7fff;
				phase = decay;
			}
			break;
		}
		case decay: {
			l -= (d << 5);
			if (l <= s) {
				l = s;
				phase = sustain;
			}
			break;
		}
		case release: {
			l -= (r << 4);
			if (l <= 0) {
				l = 0;
				phase = off;
			}
		}
	}
	v = (uint16_t)l;
	return v;
}

void ADSR::gate_on()
{
	phase = attack;
}

void ADSR::gate_off()
{
	phase = release;
}
