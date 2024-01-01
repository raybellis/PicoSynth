#pragma once

#include <cstddef>
#include <cstdint>

class Filter {

protected:
	uint16_t				cutoff;

public:
	void					set_cutoff(uint16_t cutoff);
	virtual void			apply(int16_t* buf, size_t n) = 0;

public:
							Filter() = default;
	virtual					~Filter() = default;

};

class SVF : virtual public Filter {

private:
	int16_t					d1, d2;

public:
	virtual void			apply(int16_t* buf, size_t n);

public:
							SVF();

};
