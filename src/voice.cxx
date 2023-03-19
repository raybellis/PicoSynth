#include <cmath>
#include "voice.h"

static inline float note_to_freq(uint8_t note)
{
	return 440.0 * powf(2.0, (note - 69) / 12.0);
}

inline void NCOscillator::set_frequency(float f)
{
	step = (uint32_t)(0x10000 * table_len * f / SAMPLE_RATE);
}

void SquarewaveOscillator::update(uint16_t vol, int32_t* samples)
{
	static const int mid = (table_len << 16) >> 1;
	static const int mask = pos_max - 1;

	for (int i = 0; i < SAMPLES_PER_BUFFER; ++i) {
		int16_t amplitude = (pos < mid ? 32767 : -32767);
		samples[i] += (vol * amplitude) >> 16;
		pos = (pos + step) & mask;
	}
}

void SawtoothOscillator::update(uint16_t vol, int32_t* samples)
{
	static const int mid = (table_len << 16) >> 1;
	static const int mask = pos_max - 1;

	// n:16 bit table entry, 16 bit sample amplitude 
	// int16_t amplitude = ((pos - mid) >> 16) << (16 - n);

	for (int i = 0; i < SAMPLES_PER_BUFFER; ++i) {
		int16_t amplitude = (pos - mid) >> table_shift;
		samples[i] += (vol * amplitude) >> 17;
		pos = (pos + step) & mask;
	}
}

static bool wave_init = false;
int16_t WavetableOscillator::table[NCOscillator::table_len];

WavetableOscillator::WavetableOscillator()
{
	if (!wave_init) {
		for (int i = 0; i < table_len; ++i) {
			table[i] = 32767 * cosf(i * 2 * (float) (M_PI / table_len));
		}
		wave_init = true;
	}
}

void WavetableOscillator::update(uint16_t vol, int32_t* samples)
{
	static const int mask = pos_max - 1;
	for (uint i = 0; i < SAMPLES_PER_BUFFER; ++i) {
		int16_t amplitude = table[pos >> 16];
		samples[i] += (vol * amplitude) >> 16;
		pos = (pos + step) & mask;
	}
}

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

Voice::Voice()
{
	osc = new SawtoothOscillator();
	// osc = new WavetableOscillator();
	// osc = new SquarewaveOscillator();
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

void Voice::update(int32_t* samples)
{
	uint16_t e = env->update();	
	if (true || e) {
		osc->update((vel * e) >> 8, samples); // 15 + 7 bits back into 16
	}
}
