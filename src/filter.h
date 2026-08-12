#pragma once

#include <cstddef>
#include <cstdint>

class Filter {

protected:
	uint16_t				cutoff;
	uint16_t				q;

public:
	void					set_cutoff(uint16_t cutoff);
	void					set_q(uint16_t q);

	virtual void			apply(int16_t* buf, size_t n) = 0;

public:
							Filter() = default;
	virtual					~Filter() = default;

};

class SVF : virtual public Filter {

private:
	int32_t					low, band;

public:
	virtual void			apply(int16_t* buf, size_t n);

public:
							SVF();
	virtual					~SVF();

};
