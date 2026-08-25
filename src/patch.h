#pragma once

#include <stdint.h>

// One of the three oscillators a voice runs.
//
// coarse and fine are *signed*, and zero means no offset.  The rest of
// Patch follows the MIDI convention of a 7-bit value centred on 64, but
// these are preset-only fields that never arrive over the wire, and the
// centred form has a nasty property in a POD filled with designated
// initialisers: the neutral value is not zero, so a preset that omits
// the field gets a large detune rather than none.  That trap has cost
// real time here more than once already.
//
// They are also applied by different means, because they have to be.
// coarse shifts the note before the table lookup, which is exact and
// costs nothing; fine goes through frequency_modulate, which can only
// reach an octave either way - the whole of power_table - and so could
// not carry coarse even if it were convenient.
typedef struct {

	uint8_t				wave;			// index into waves[]
	uint8_t				level;			// 7-bit, mixed with the others

	int8_t				coarse;			// semitones
	int8_t				fine;			// 64ths of a semitone

} Osc;

#define NDCO			3

typedef struct {

	Osc					dco[NDCO];

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

	// what moves the cutoff besides the envelope below.  all three are
	// signed or zero-neutral, so a preset that omits them gets none
	int8_t				dcf_vel;		// semitones at full velocity
	uint8_t				dcf_track;		// key follow, 127 = one for one
	int8_t				dcf_press;		// semitones at full aftertouch

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
	uint8_t				lfo_freq;

	// phase source.  per voice, the phase is whatever the voice was
	// last doing, so notes wobble independently and the result reads as
	// ensemble or chorus; per channel they all share one phase and it
	// reads as vibrato.  Both are wanted, hence the choice
	uint8_t				lfo_global;		// 0 = per voice, 1 = per channel

	// how long the LFO takes to reach full depth.  0 is instant; higher
	// is slower, so vibrato arrives after the note rather than with it
	uint8_t				lfo_delay;

	// depth to pitch, summed from three sources and capped at 127.
	// lfo_depth applies always - a patch can have vibrato of its own
	// without anyone touching a controller, which the wheel-only form
	// made impossible
	uint8_t				lfo_depth;		// intrinsic
	uint8_t				lfo_wheel;		// added by the mod wheel
	uint8_t				lfo_press;		// added by aftertouch

	// the LFO's other destinations, each with its own depth so that a
	// patch can have wah without vibrato or the reverse
	uint8_t				lfo_dcf;		// to cutoff, in semitones
	uint8_t				lfo_dca;		// to amplitude, as tremolo

} Patch;

extern Patch presets[];
