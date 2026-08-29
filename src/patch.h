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
// indices into waves[], named so a preset says what it sounds like.
// NOISE is not a table - it is generated - and only dco[1] may use it
enum {
	SINE	= 0,
	SQUARE	= 1,
	SAW		= 2,
	TRI		= 3,
	NOISE	= 4,
};

typedef struct {

	uint8_t				wave;			// index into waves[]
	uint8_t				level;			// 7-bit, mixed with the others

	int8_t				coarse;			// semitones
	int8_t				fine;			// 64ths of a semitone

} Osc;

// the two freely-tunable oscillators.  The sub is a third voice but not
// a third Osc - it has no tuning of its own
#define NDCO			2

typedef struct {

	// dco[0] is the primary and dco[1] the auxiliary.  Only dco[1] may
	// be NOISE, which keeps the generator off the primary pitch path
	// and turns "which oscillator is noise" into a single test
	Osc					dco[NDCO];

	// The sub-oscillator: dco[0] again, a whole number of octaves down.
	// It has no wave, no coarse and no fine of its own - it *is* dco[0],
	// read at a lower rate, so it shares the waveform and inherits any
	// detune, which is what stops a sub beating against its own parent.
	//
	// That sharing is the point rather than a simplification: one phase
	// accumulator, one step, one table pointer and one round of pitch
	// modulation serve both, and registers are the resource that has
	// decided every oscillator measurement here.
	uint8_t				sub_level;		// 0 for no sub
	uint8_t				sub_octaves;	// below dco[0]; 0 reads as 1

	// An amplitude envelope for dco[1] alone, so the auxiliary can have
	// a contour of its own rather than following the DCA.  All four zero
	// means it does follow the DCA, which is the common case and costs
	// nothing - the envelope is only created when one of them is set.
	//
	// This is the one modulation the filter cannot stand in for.  A
	// downward filter envelope will darken a partial as a note decays,
	// but noise is broadband, so a lowpass ducks it and the tone in the
	// same proportion; and nothing a lowpass does can fade an oscillator
	// tuned *below* the primary.  The breath in a flute is a chiff at
	// the onset settling to a quiet hiss, and that shape needs this
	uint8_t				aux_env_a;
	uint8_t				aux_env_d;
	uint8_t				aux_env_s;
	uint8_t				aux_env_r;

	uint8_t				dca_env_level;
	uint8_t				dca_env_a;
	uint8_t				dca_env_d;
	uint8_t				dca_env_s;
	uint8_t				dca_env_r;

	// signed, so the envelope can sweep the pitch either way: positive
	// starts the note sharp and falls to pitch, negative starts it flat
	// and rises.  frequency_modulate handles either without help - the
	// folded power_table is what makes a negative offset exact
	int8_t				dco_env_level;
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

	// Filter envelope depth, in semitones: positive sweeps the cutoff
	// up, negative down, and zero is no modulation at all.
	//
	// This was a 7-bit value centred on 64, matching the controllers -
	// but no controller touches it, and the centred form meant a preset
	// that omitted the field got a full downward sweep and a shut filter
	// rather than nothing.  Signed is both safer and easier to read: the
	// preset that wants 32 semitones now says 32 instead of 96
	int8_t				dcf_env_level;
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

	// How much LFO is running at all, summed from these three and
	// capped at 127.  This scales the oscillator itself, before any of
	// the routing below, so a controller reaches every destination
	// rather than only the pitch - which is what the mod wheel used to
	// do, leaving a tremolo or a filter sweep stuck at full depth with
	// no way to bring it in.
	//
	// lfo_amount applies always, so a patch can have movement of its
	// own without anyone touching a controller.
	uint8_t				lfo_amount;		// intrinsic
	uint8_t				lfo_wheel;		// added by the mod wheel
	uint8_t				lfo_press;		// added by aftertouch

	// and where that goes.  Pure routing depths: each says how far this
	// destination moves at full amount, and nothing here knows what the
	// controllers are doing
	uint8_t				lfo_pitch;		// vibrato, in 64ths of an octave
	uint8_t				lfo_dcf;		// to cutoff, in semitones
	uint8_t				lfo_dca;		// to amplitude, as tremolo

} Patch;

// const on purpose: 128 patches at 41 bytes is 5 KB, and a non-const
// array goes to .data, which costs that much RAM *and* the same again in
// flash for the initialiser.  Nothing writes them
extern const Patch presets[];

#define NPRESETS		128
