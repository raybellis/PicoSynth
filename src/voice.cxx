#include <cmath>
#include "voice.h"

//--------------------------------------------------------------------+
// Utility Functions
//--------------------------------------------------------------------+

static inline float note_to_freq(uint8_t note)
{
        return 440.0 * powf(2.0, (note - 69) / 12.0);
}

//--------------------------------------------------------------------+
// Envelope
//--------------------------------------------------------------------+

ADSR::ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r)
	: phase(off), level(0), a(a), d(d), s(s << 8), r(r)
{
	if (a < 1) a = 0;
	if (d < 1) d = 0;
	if (r < 1) r = 0;
}

uint16_t ADSR::update()
{
	int32_t l = level;	// large and signed to allow overflow
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
	level = (uint16_t)l;
	return level;
}

void ADSR::gate_on()
{
	phase = attack;
}

void ADSR::gate_off()
{
	phase = release;
}

//--------------------------------------------------------------------+
// Voice control
//--------------------------------------------------------------------+

Voice::Voice()
{
	// osc = new SineOscillator();
	osc = new SawtoothOscillator();
	// osc = new WavetableOscillator();
	// osc = new SquareOscillator();
	env = new ADSR(30, 20, 80, 20);
}

void Voice::note_on(uint8_t note, uint8_t vel)
{
	this->note = note;
	this->vel = vel;

	env->gate_on();
	osc->sync();
	osc->set_frequency(note_to_freq(note));
}

void Voice::note_off()
{
	env->gate_off();
}

static int16_t temp[SAMPLES_PER_BUFFER];

void Voice::update(int32_t* samples, size_t n)
{
	auto dca = env->update();	

	if (dca) {
		osc->update(temp, n);
#if SAMPLE_CHANS == 1
		uint16_t vol = (dca * vel) >> 6;	// 22 bits (15 env + 7 velocity)
		for (int i = 0; i < n; ++i) {
			samples[i] += (vol * temp[i]) >> 16;
		}
#else
		uint16_t vol = (dca * vel) >> 6;	// 22 bits (15 env + 7 velocity)
		for (int i = 0, j = 0; i < n; ++i) {
			samples[j++] += (vol * temp[i]) >> 16;
			samples[j++] += (vol * temp[i]) >> 16;
		}
#endif
	}
}

//--------------------------------------------------------------------+
// Voice allocator
//--------------------------------------------------------------------+
