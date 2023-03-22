#include "audio.h"
#include "oscillator.h"

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
	void			update(int32_t* buf, size_t n);

};
