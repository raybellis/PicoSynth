#include "filter.h"

void Filter::set_cutoff(uint16_t n)
{
	cutoff = n;
}

SVF::SVF() :
	d1(0), d2(0)
{
}

void SVF::apply(int16_t* buf, size_t n)
{
	extern uint16_t* svf_table;
	uint16_t f1 = svf_table[cutoff];
	uint16_t q1 = 1;

	for (size_t i = 0; i < n; ++i) {
		int16_t l = d2 + f1 * d1;
		int16_t h = buf[i] - l - q1 * d1;
		int16_t b = f1 * h + d1;
		// int16_t n = h + l;

		d1 = b;
		d2 = l;
		buf[i] = l;
	}
}
