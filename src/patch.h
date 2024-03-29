#pragma once

#include <stdint.h>

typedef struct {

	uint8_t				dco_wave;

	uint8_t				dca_env_level;
	uint8_t				dca_env_a;
	uint8_t				dca_env_d;
	uint8_t				dca_env_s;
	uint8_t				dca_env_r;

	uint8_t				dco_env_level;
	uint8_t				dco_env_a;
	uint8_t				dco_env_d;
	uint8_t				dco_env_s;
	uint8_t				dco_env_r;

	uint8_t				lfo_wave;
	uint8_t				lfo_depth;
	uint8_t				lfo_freq;

} Patch;

extern Patch presets[];
