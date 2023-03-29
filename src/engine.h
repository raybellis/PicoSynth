#pragma once

#include <cstdint>
#include <cstddef>

#include "channel.h"

class Oscillator;
class Envelope;

class Voice {

	friend class			SynthEngine;

private:
	uint8_t					chan;
	bool					free:1;
	bool					steal:1;
	uint8_t					note;
	uint8_t					vel;
	Oscillator*				osc;
	Envelope*				dca;

private:
	void					init();

public:
							Voice();

};

class SynthEngine {

private:
	Voice					voice[64];
	Channel					channel[16];

private:
	Voice*					allocate();
	void					deallocate(Voice& v);

	void					note_on(uint8_t chan, uint8_t note, uint8_t vel);
	void					note_off(uint8_t chan, uint8_t note, uint8_t vel);

public:
	void					midi_in(uint8_t c, uint8_t d1, uint8_t d2);

public:
	void					update(int32_t* samples, size_t n);

public:
							SynthEngine();
};
