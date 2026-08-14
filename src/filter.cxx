#include "filter.h"

// https://www.musicdsp.org/en/latest/Filters/23-state-variable.html

// cutoff is in units of 1/128th of a semitone, and indexes directly
// into the generated tables, so it must stay within their bounds
void Filter::set_cutoff(uint16_t n)
{
	cutoff = (n < SVF_LEN) ? n : (SVF_LEN - 1);
}

void Filter::set_q(uint16_t _q)
{
	q = _q;
}

SVF::SVF() :
	low(0), band(0)
{
}

SVF::~SVF()
{
}

static inline int32_t fmul_su(int32_t a, int32_t s)
{
	return ((a * s) >> 15);
}

void SVF::apply(int16_t* buf, size_t n)
{
	extern const int16_t svf_table[];
	int32_t f = svf_table[cutoff];
	uint16_t scale = q;
	int32_t high;

	for (size_t i = 0; i < n; ++i) {
		int32_t input = buf[i];

		low += fmul_su(band, f);
		high = fmul_su(input, scale) - fmul_su(band, q) - low;
		band += fmul_su(high, f);
		// notch = high + low;

		buf[i] = low;
	}
}
