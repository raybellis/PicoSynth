#include <cmath>
#include <cstdio>
#include "oscillator.h"
#include "hardware/interp.h"

//--------------------------------------------------------------------+
// Generic Numerically Controlled Oscillator
//--------------------------------------------------------------------+

inline void NCOscillator::set_frequency(float f)
{
	step = (uint32_t)(0x10000 * table_len * f / SAMPLE_RATE);
}

//--------------------------------------------------------------------+
// Square Wave
//--------------------------------------------------------------------+

void SquareOscillator::update(int16_t* samples, size_t n)
{
	static const int mid = (table_len << 16) >> 1;
	static const int mask = pos_max - 1;

	for (uint i = 0; i < n; ++i) {
		samples[i] = (pos < mid ? 32767 : -32767);
		pos = (pos + step) & mask;
	}
}

//--------------------------------------------------------------------+
// Sawtooth
//--------------------------------------------------------------------+

void SawtoothOscillator::update(int16_t* samples, size_t n)
{
	static const int mid = (table_len << 16) >> 1;
	static const int mask = pos_max - 1;

	// x:16 bit table entry, 16 bit sample amplitude 
	// int16_t amplitude = ((pos - mid) >> 16) << (16 - x);

	for (uint i = 0; i < n; ++i) {
		samples[i] = (pos - mid) >> table_shift;
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
		for (uint i = 0; i < table_len; ++i) {
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

void __not_in_flash_func(WavetableOscillator::update)(int16_t* samples, size_t n)
{
#if 1
	interp_config cfg = interp_default_config();
	interp_config_set_shift(&cfg, 15);
	interp_config_set_mask(&cfg, 1, table_shift);
	interp_config_set_add_raw(&cfg, true);
	interp_set_config(interp0, 0, &cfg);

	interp0->base[0] = step;
	interp0->base[2] = (uint32_t)wavetable;
	interp0->accum[0] = pos;

	for (uint i = 0; i < n; ++i) {
		samples[i] = *(int16_t*)interp0->pop[2];
	}
	pos = interp0->accum[0] & (pos_max - 1);

#else
	for (uint i = 0; i < n; ++i) {
		samples[i] = wavetable[pos >> 16];
		pos = (pos + step) & (pos_max - 1);
	}
#endif
}
