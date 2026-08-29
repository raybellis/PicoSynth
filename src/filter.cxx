#include "pico.h"

#include "data.h"
#include "dsp.h"
#include "filter.h"

// https://www.musicdsp.org/en/latest/Filters/23-state-variable.html

// cutoff is in units of 1/SVF_STEPS of a semitone, and indexes
// directly into svf_table, so it must stay within its bounds.  the
// 7-bit patch parameter reaches it through cutoff_table rather than
// arriving here directly - the fine units exist so that a future
// filter envelope can sweep between them
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
	low(0.0f), band(0.0f)
{
}

SVF::~SVF()
{
}

// clamp16() and to_int() are in dsp.h - the output stage needs them too

// the integrators are deliberately not clamped.  the input is an
// int16_t, the filter is stable for every entry the tables can produce
// (f <= 1.0 from the Fs/6 clamp on svf_table, q1 <= 1.0 from
// SVF_Q_MAX, against a limit of q1 < 2/f - f/2), and a stable filter's
// state cannot exceed its input times its peak gain, which is sqrt(Q).
// measured, the state peaks around 2^17.4 - nowhere near the range of
// a float, let alone its precision, which is 24 bits of mantissa
// against the 18 the state occupies
//
// only the output is clamped.  clamping the integrators saturates
// inside the feedback loop, which is a nonlinearity rather than a
// clip, and it wrecks the response at any useful resonance: against
// the ideal filter with the same output clip, a full scale input at
// Q=8 measured 9 dB SNR that way and 86 dB this way

// runs from RAM - this is the per-sample inner loop of the audio
// path, same as SynthEngine::update that calls it
void __not_in_flash_func(SVF::apply)(int16_t* buf, size_t n)
{
	// the tables stay integer and are converted once here, not per
	// sample: the compiler folds each into a single vcvt in the
	// preamble and keeps all three in FP registers across the loop.
	// float tables would save those three instructions per buffer and
	// cost twice the flash
	const float f  = svf_table[cutoff] * (1.0f / 16384.0f);	// 2:14
	const float dq = q                 * (1.0f / 32768.0f);	// 1:15
	const float sc = scale             * (1.0f / 32768.0f);	// 1:15

	// held in registers across the loop rather than written back each
	// sample - the members only exist to carry state between buffers
	float lo = low, bd = band;

	for (size_t i = 0; i < n; ++i) {
		float input = buf[i];

		lo += f * bd;
		float hi = sc * input - dq * bd - lo;
		bd += f * hi;
		// notch = hi + lo;

		buf[i] = clamp16(to_int(lo));
	}

	low = lo;
	band = bd;
}
