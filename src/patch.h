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

	uint8_t				dcf_freq;
	uint8_t				dcf_reso;

	// Filter envelope depth, centred on 64: 64 is no modulation, 127
	// sweeps up by 63 semitones and 0 down by 64.
	//
	// This is the one field where zero is *not* neutral.  Patch is a POD
	// filled with designated initialisers, so anything a preset omits
	// comes out as zero - and zero here is a full downward sweep, which
	// shuts the filter.  Every preset must set this explicitly.
	uint8_t				dcf_env_level;
	uint8_t				dcf_env_a;
	uint8_t				dcf_env_d;
	uint8_t				dcf_env_s;
	uint8_t				dcf_env_r;

	uint8_t				lfo_wave;
	uint8_t				lfo_depth;
	uint8_t				lfo_freq;

} Patch;

extern Patch presets[];
