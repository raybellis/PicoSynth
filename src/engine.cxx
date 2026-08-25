#include <cstdio>

#include "hardware/interp.h"
#include "hardware/divider.h"

#include "engine.h"
#include "audio.h"
#include "data.h"
#include "envelope.h"
#include "midi.h"
#include "waves.h"

//--------------------------------------------------------------------+
// Utility functions
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// Core synth engine
//--------------------------------------------------------------------+

SynthEngine::SynthEngine()
{
	// nothing here on purpose - this is a global, so it is constructed
	// before main() has had a chance to call tables_init().  See init()
}

// Call once the lookup tables exist.  Channel::init() reads pan_table,
// which is computed at boot rather than baked into flash, so it cannot
// run any earlier
void SynthEngine::init()
{
	for (auto& c : channel) {
		c.init();
	}

	// set all channels to a default preset
	for (uint8_t c = 0; c < 16; ++c) {
		midi_in(0xc0 + c, c, 0);
	}
}

// returns the number of voices rendered, which is what the benchmark
// figures need reading against - the pool iterator only visits voices
// in use, and every one of them costs a full render and filter pass
// whether or not its DCA has anything left to say
uint32_t __not_in_flash_func(SynthEngine::update)(int32_t* samples, size_t n)
{
	uint32_t voices = 0;

	// update all envelopes and release any voice
	// that now has an inactive DCA
	for (auto& v: pool) {
		v.dca_env->update();
		if (!v.dca_env->active()) {
			pool.release(v);
			continue;
		}

		// get a reference to the current note's patch
		auto& p = *v.patch;

		// update DCO envelope
		if (p.dco_env_level && v.dco_env) {
			v.dco_env->update();
		}

		// and the DCF envelope, which only exists at non-zero depth
		if (v.dcf_env) {
			v.dcf_env->update();
		}
	}

	// set up both interpolators, identically.  a voice runs three
	// oscillators and each needs its own phase accumulator, so having
	// two of these is what lets a pair of them share one pass over the
	// buffer instead of taking one each - see Voice::oscillators()
	interp_config cfg = interp_default_config();
	interp_config_set_shift(&cfg, 15);
	interp_config_set_mask(&cfg, 1, WAVE_SHIFT);
	interp_config_set_add_raw(&cfg, true);
	interp_set_config(interp0, 0, &cfg);
	interp_set_config(interp1, 0, &cfg);

	for (auto& v : pool) {
		++voices;
		v.render(samples, n);
	}

	return voices;
}

void SynthEngine::note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	auto* vp = pool.allocate();
	if (vp) {
		auto& v = *vp;
		v.channel = &channel[chan];
		v.patch = &presets[v.channel->program % 4];
		v.note_on(chan, note, vel);
	}
}

// With the sustain pedal down the note carries on sounding and the
// release is owed until the pedal comes up, which is what a piano does
// and what any pedalled part is written expecting.  The note is marked
// rather than released; sustain_off() below settles the debt.
void SynthEngine::note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
	Channel* c = &channel[chan];
	bool pedal = c->control[sustain] >= 64;

	for (auto& v: pool) {
		if (v.channel == c && v.note == note) {
			if (pedal) {
				v.sustained = true;
			} else {
				v.note_off();
			}
		}
	}
}

// the pedal has come up: release everything it was holding
void SynthEngine::sustain_off(uint8_t chan)
{
	Channel* c = &channel[chan];

	for (auto& v: pool) {
		if (v.channel == c && v.sustained) {
			v.sustained = false;
			v.note_off();
		}
	}
}

void SynthEngine::midi_in(uint8_t c, uint8_t d1, uint8_t d2)
{
	uint8_t cmd = (c & 0xf0) >> 4;
	uint8_t chan = (c & 0x0f);

	switch (cmd) {
		case 0x8:
			note_off(chan, d1, d2);
			break;
		case 0x9:
			if (d2) {
				note_on(chan, d1, d2);
			} else {
				note_off(chan, d1, d2);
			}
			break;
		case 0xb:
			channel[chan].midi_in(c, d1, d2);

			// the channel stores the pedal, but only the engine can see
			// the voices it was holding
			if (d1 == sustain && d2 < 64) {
				sustain_off(chan);
			}
			break;
		case 0xc:
		case 0xd:
		case 0xe:
			channel[chan].midi_in(c, d1, d2);
			break;
		case 0xf:
			break;
	}
}
