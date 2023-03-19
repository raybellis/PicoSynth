#include "audio.h"
#include <atomic>

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
	static const int	table_shift = 11;
	static const size_t table_len = (1 << table_shift);
	static const size_t	pos_max = (table_len << 16);	// 11:16 format

protected:
	uint32_t			step;
	uint32_t			pos = 0;

public:
	virtual void		set_frequency(float f);
	void				sync() { pos = 0; }
};

class SquarewaveOscillator : virtual public NCOscillator {

public:
	virtual void		update(uint16_t vol, int32_t*);
};

class SawtoothOscillator : virtual public NCOscillator {

public:
	virtual void		update(uint16_t vol, int32_t*);
};

class WavetableOscillator : virtual public NCOscillator {

private:
	static int16_t		table[table_len];

public:
						WavetableOscillator();

public:
	virtual void		update(uint16_t vol, int32_t*);

};

class Envelope {

public:
	virtual void		gate_on() = 0;
	virtual void		gate_off() = 0;
	virtual uint16_t	update() = 0;

};

class ADSR : virtual public Envelope {

private:
	uint16_t		level;
	uint16_t		s;
	uint8_t			a, d, r;

					enum Phase {
						off,
						attack,
						decay,
						sustain,
						release
					} phase = off;

public:
	void			gate_on();
	void			gate_off();
	uint16_t		update();

public:
					ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r);

};

class Voice {

private:
	uint8_t			note = 0;
	uint8_t			vel = 0;
	Oscillator		*osc = nullptr;
	Envelope		*env = nullptr;

public:
	enum Phase {
		ready,
		attack,
		decay,
		sustain,
		release
	};

public:
	void			note_on(uint8_t note, uint8_t vel);
	void			note_off();

public:
					Voice();

public:
	void			update(int32_t* buf);

};
