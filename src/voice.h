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
	uint8_t					note;
	uint8_t					vel;

	uint32_t				dco_step_base;
	uint32_t				dco_step;
	uint32_t				dco_pos;

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
	void					update(int16_t* samples, size_t n);
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
