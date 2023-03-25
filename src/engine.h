#pragma once

#include <cstdint>
#include <cstddef>

#include "channel.h"

class Oscillator;
class Envelope;

class SynthEngine {

private:
	static const int		nv = 32;

							struct Voice {
								Channel*		chan;
								Oscillator*		osc;
								Envelope*		dca;
								uint8_t			note;
								uint8_t			vel;
								bool			free:1;
								bool			steal:1;
							};

	Voice					voice[nv];
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
