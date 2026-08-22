#pragma once

#include <cstddef>
#include <cstdint>

#include "settings.h"

class Filter {

protected:					// wide open, and no resonance, until set
	uint16_t				cutoff = SVF_LEN - 1;
	uint16_t				q = SVF_Q_MAX;
	uint16_t				scale = SVF_Q_MAX;

public:
	void					set_cutoff(uint16_t cutoff);
	void					set_q(uint16_t n);		// indexes q_table

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
