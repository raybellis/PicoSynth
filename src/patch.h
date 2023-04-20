#pragma once

#include <cstdint>

class Patch {

public:
	uint8_t				wavenum;
	uint8_t				dca_env_a;
	uint8_t				dca_env_d;
	uint8_t				dca_env_s;
	uint8_t				dca_env_r;

public:
	static Patch		presets[4];

public:
						Patch();
						~Patch();

};
