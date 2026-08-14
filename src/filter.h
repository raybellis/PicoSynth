#pragma once

#include <cstddef>
#include <cstdint>

#include "settings.h"

class Filter {

protected:					// wide open, and no resonance, until set
	uint16_t				cutoff = SVF_LEN - 1;
	uint16_t				q = 32768;

public:
	void					set_cutoff(uint16_t cutoff);
	void					set_q(uint16_t q);

	virtual void			apply(int16_t* buf, size_t n) = 0;

public:
							Filter() = default;
	virtual					~Filter() = default;

};

class SVF : public Filter {

private:
	int32_t					low, band;

public:
	virtual void			apply(int16_t* buf, size_t n);

public:
							SVF();
	virtual					~SVF();

};
