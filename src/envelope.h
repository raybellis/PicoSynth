#pragma once

#include <cstdint>

class Envelope {

protected:
	int32_t				_level;

public:
	virtual void		gate_on() = 0;
	virtual void		gate_off() = 0;

public:
	virtual bool		active() = 0;
	virtual int16_t		update() = 0;
	virtual int16_t		level() { return _level; }

public:
						Envelope();
	virtual				~Envelope() = default;

};

class ADSR : virtual public Envelope {

private:
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

public:
	bool			active() { return phase > off; };
	int16_t			update();

public:
					ADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r);

};
