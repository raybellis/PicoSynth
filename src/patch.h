#pragma once

#include <cstdint>

class Oscillator;
class Envelope;

class Patch {

public:
	uint8_t				wavenum;
	uint8_t				dca_a;
	uint8_t				dca_d;
	uint8_t				dca_s;
	uint8_t				dca_r;

public:
	static Patch		presets[4];

public:
	Oscillator*			get_oscillator();
	Envelope*			get_dca();

public:
						Patch();
						~Patch();

};
