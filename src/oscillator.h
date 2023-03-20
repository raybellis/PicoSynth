#pragma once
#include "audio.h"

class Oscillator {

private:
	float				freq;

public:
	virtual void		set_frequency(float f) = 0;
	virtual void		sync() = 0;
	virtual void		update(uint16_t vol, int32_t*) = 0;

};

class NCOscillator : virtual public Oscillator {

protected:
	static const int	table_shift = 10;
	static const size_t table_len = (1 << table_shift);
	static const size_t	pos_max = (table_len << 16);	// 11:16 format

protected:
	uint32_t			step;
	uint32_t			pos = 0;

public:
	virtual void		set_frequency(float f);
	void				sync() { pos = 0; }
};

class SquareOscillator : virtual public NCOscillator {

public:
	virtual void		update(uint16_t vol, int32_t*);
};

class SawtoothOscillator : virtual public NCOscillator {

public:
	virtual void		update(uint16_t vol, int32_t*);
};

class WavetableOscillator : virtual public NCOscillator {

protected:
	int16_t				*wavetable;

public:
						WavetableOscillator(int16_t *wavetable);

public:
	virtual void		update(uint16_t vol, int32_t*);

};

class SineOscillator : virtual public WavetableOscillator {

private:
	static bool			init;
	static int16_t		sine[table_len];

public:
						SineOscillator();
};
