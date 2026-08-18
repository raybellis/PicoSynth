#include "pico.h"

#include "data.h"
#include "filter.h"

// https://www.musicdsp.org/en/latest/Filters/23-state-variable.html

// cutoff is in units of 1/128th of a semitone, and indexes directly
// into the generated tables, so it must stay within their bounds
void Filter::set_cutoff(uint16_t n)
{
	cutoff = (n < SVF_LEN) ? n : (SVF_LEN - 1);
}

// n is a 7-bit resonance parameter indexing q_table, which maps it
// onto Q = 1 .. 64 as the 1:15 damping factor 1/Q, and scale_table
// alongside it, which is what the input gets scaled by.  taking the
// pair from generated tables is what keeps the damping inside the
// range where the filter is stable and fmul_f(high, f) cannot
// overflow - there is no value of n that escapes it
void Filter::set_q(uint16_t n)
{
	if (n >= SVF_Q_LEN) {
		n = SVF_Q_LEN - 1;
	}

	q = q_table[n];
	scale = scale_table[n];
}

SVF::SVF() :
	low(0), band(0)
{
}

SVF::~SVF()
{
}

// an arithmetic shift rounds towards -infinity, which inside the
// integrators below accumulates into a DC offset, so both of these
// round to nearest instead

// multiply by the 1:15 fixed point damping factor
static inline int32_t fmul_su(int32_t a, int32_t s)
{
	return ((a * s) + (1 << 14)) >> 15;
}

// multiply by the 2:14 fixed point cutoff coefficient, which has to
// carry a factor of two and therefore can't use the same scale
static inline int32_t fmul_f(int32_t a, int32_t f)
{
	return ((a * f) + (1 << 13)) >> 14;
}

// saturate to full scale instead of wrapping, so that a high-Q
// setting distorts at the rails rather than inverting the signal
static inline int32_t clamp16(int32_t x)
{
	if (x > INT16_MAX) return INT16_MAX;
	if (x < INT16_MIN) return INT16_MIN;
	return x;
}

// runs from RAM - this is the per-sample inner loop of the audio
// path, same as SynthEngine::update that calls it
void __not_in_flash_func(SVF::apply)(int16_t* buf, size_t n)
{
	int32_t f = svf_table[cutoff];
	int32_t high;

	// q and scale are deliberately left as member reads.  buf is an
	// int16_t* and they are uint16_t, so those two may alias and the
	// compiler reloads both every iteration - but hoisting them into
	// locals measured two instructions a sample *worse*, because on
	// M0+ the extra live values land in high registers and every muls
	// then needs a mov down to a low one
	for (size_t i = 0; i < n; ++i) {
		int32_t input = buf[i];

		low = clamp16(low + fmul_f(band, f));
		high = fmul_su(input, scale) - fmul_su(band, q) - low;
		band = clamp16(band + fmul_f(high, f));
		// notch = high + low;

		buf[i] = low;
	}
}
