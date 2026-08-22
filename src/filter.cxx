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
// integrators below accumulates into a DC offset, so all of these
// round to nearest instead

// multiply by the 1:15 fixed point damping factor.  only used on the
// input, which is an int16_t, so a 32-bit product cannot overflow
static inline int32_t fmul_su(int32_t a, int32_t s)
{
	return ((a * s) + (1 << 14)) >> 15;
}

// the same, for the damping term, whose operand is the band state and
// so runs to STATE_MAX rather than to int16_t
static inline int32_t fmul_su_wide(int32_t a, int32_t s)
{
	return (int32_t)((((int64_t)a * s) + (1 << 14)) >> 15);
}

// multiply by the 2:14 fixed point cutoff coefficient, which has to
// carry a factor of two and therefore can't use the same scale.  both
// of its operands - the band state and the high term - are wider than
// 16 bits, so this one is always the 64-bit form
static inline int32_t fmul_f_wide(int32_t a, int32_t f)
{
	return (int32_t)((((int64_t)a * f) + (1 << 13)) >> 14);
}

// saturate to full scale instead of wrapping, so that an overdriven
// filter distorts at the rails rather than inverting the signal
static inline int32_t clamp16(int32_t x)
{
	if (x > INT16_MAX) return INT16_MAX;
	if (x < INT16_MIN) return INT16_MIN;
	return x;
}

// the integrators are deliberately not clamped at all.  three things
// bound them between them: the input is an int16_t, the filter is
// stable for every entry the tables can produce (f <= 1.0 from the
// Fs/6 clamp on svf_table, q1 <= 1.0 from SVF_Q_MAX, against a limit
// of q1 < 2/f - f/2), and a stable filter's state cannot exceed its
// input times its peak gain, which is sqrt(Q) here.
//
// measured across 9 cutoffs x all 128 resonances x square, saw, sine,
// impulse, DC and noise at full scale, the state peaks at 353512 -
// 2^18.4, or 6000x inside int32_t.  a clamp would only ever fire if
// one of those three premises broke, and it is worth about a fifth of
// this loop, so there isn't one.  re-run utils/../unclamped.js if the
// tables or the input scaling ever change

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
	//
	// note that only the output is clamped to int16_t.  clamping the
	// integrators themselves saturates inside the feedback loop, which
	// is a nonlinearity rather than a clip, and it wrecks the response
	// at any useful resonance: measured against the ideal filter with
	// the same output clip, a full scale input at Q=8 came out at 9 dB
	// SNR that way against 86 dB this way
	for (size_t i = 0; i < n; ++i) {
		int32_t input = buf[i];

		low += fmul_f_wide(band, f);
		high = fmul_su(input, scale) - fmul_su_wide(band, q) - low;
		band += fmul_f_wide(high, f);
		// notch = high + low;

		buf[i] = clamp16(low);
	}
}
