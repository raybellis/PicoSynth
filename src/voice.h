#pragma once

#include <cstdint>
#include <cstddef>

#include "channel.h"
#include "patch.h"
#include "filter.h"

class Envelope;

class Voice {

	friend class			VoicePool;
	friend class			SynthEngine;

private:
	bool					free;
	bool					steal;

	// note-off arrived while the sustain pedal was down, so the note is
	// still sounding and owes a release when the pedal comes up
	bool					sustained;

	uint8_t					note;
	uint8_t					vel;

	// one set per oscillator.  the base already carries that
	// oscillator's coarse and fine tuning, both of which are fixed for
	// the life of the note, so the per-buffer modulation below only has
	// to deal with what actually moves
	uint32_t				dco_step_base[NDCO];
	uint32_t				dco_step[NDCO];
	uint32_t				dco_pos[NDCO];

	uint32_t				lfo_step;
	uint32_t				lfo_pos;

	Channel*				channel;
	Patch*					patch;
	Envelope*				dca_env;
	Envelope*				dco_env;
	Envelope*				dcf_env;
	Filter*					filter;

private:
	void					init();

	// renders one buffer of this voice and accumulates it into the
	// stereo output.  everything per-voice lives here - the DCA chain,
	// the pitch modulation, the oscillators, the filter - leaving
	// SynthEngine to manage voices and MIDI rather than to synthesise
	void					render(int32_t* samples, size_t n);

	// the three oscillators, mixed by their levels into one mono buffer
	void					oscillators(int16_t* out, size_t n);

	void					note_on(uint8_t chan, uint8_t note, uint8_t vel);
	void					note_off();

public:
							Voice();

};

//
// Owns the voice array and the lifetime of the per-note resources
// (envelopes, filter) that hang off each voice.  Voices are handed
// out by allocate() and given back by release(); iterating a pool
// visits only those voices that are currently in use.
//
// Note that nothing ever destroys the pool - the firmware doesn't
// exit - so there's deliberately no destructor here.
//
class VoicePool {

private:
	static const uint8_t	nv = 64;
	Voice					voice[nv];

private:
	Voice*					claim(Voice& v);

public:
	Voice*					allocate();
	void					release(Voice& v);

public:
	class iterator {

	private:
		Voice*				pos;
		Voice*				last;

	private:
		// skip over any voices that aren't in use
		void				next() { while (pos != last && pos->free) ++pos; }

	public:
							iterator(Voice* pos, Voice* last)
								: pos(pos), last(last) { next(); }

		Voice&				operator*() const { return *pos; }
		iterator&			operator++() { ++pos; next(); return *this; }
		bool				operator!=(const iterator& rhs) const { return pos != rhs.pos; }

	};

	iterator				begin() { return iterator(voice, voice + nv); }
	iterator				end()   { return iterator(voice + nv, voice + nv); }

};
