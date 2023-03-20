#include <cmath>
#include "oscillator.h"

//--------------------------------------------------------------------+
// Utility Functions
//--------------------------------------------------------------------+

static inline float note_to_freq(uint8_t note)
{
	return 440.0 * powf(2.0, (note - 69) / 12.0);
}

inline void NCOscillator::set_frequency(float f)
{
	step = (uint32_t)(0x10000 * table_len * f / SAMPLE_RATE);
}

//--------------------------------------------------------------------+
// SquareWave
//--------------------------------------------------------------------+

void SquareOscillator::update(uint16_t vol, int32_t* samples)
{
	static const int mid = (table_len << 16) >> 1;
	static const int mask = pos_max - 1;

	for (int i = 0; i < SAMPLES_PER_BUFFER; ++i) {
		int16_t amplitude = (pos < mid ? 32767 : -32767);
		samples[i] += (vol * amplitude) >> 16;
		pos = (pos + step) & mask;
	}
}

//--------------------------------------------------------------------+
// Sawtooth
//--------------------------------------------------------------------+

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

//--------------------------------------------------------------------+
// Sine Oscillator
//--------------------------------------------------------------------+

bool SineOscillator::init = false;
int16_t SineOscillator::sine[NCOscillator::table_len];

SineOscillator::SineOscillator()
	: WavetableOscillator(&sine[0])
{
	if (!init) {
		for (int i = 0; i < table_len; ++i) {
			sine[i] = 32767 * cosf(i * 2 * (float) (M_PI / table_len));
		}
		init = true;
	}
}

//--------------------------------------------------------------------+
// Wavetable
//--------------------------------------------------------------------+

WavetableOscillator::WavetableOscillator(int16_t *wavetable)
	: wavetable(wavetable)
{
}

void WavetableOscillator::update(uint16_t vol, int32_t* samples)
{
	static const int mask = pos_max - 1;
	for (uint i = 0; i < SAMPLES_PER_BUFFER; ++i) {
		int16_t amplitude = wavetable[pos >> 16];
		samples[i] += (vol * amplitude) >> 16;
		pos = (pos + step) & mask;
	}
}
