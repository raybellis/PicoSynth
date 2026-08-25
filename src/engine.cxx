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

// x is a (14-bit signed) offset into the power table which contains
/// 1:15 fixed-point log2 multipliers for x = 0.500 ..< 2.000
static inline void frequency_modulate(uint32_t& step, int16_t x)
{
	// power_table holds 2^(r/8192) as 1:15 for one octave, r in
	// [0, 8192).  the octave below is the same entries read as 1:16 -
	// that is, with this shift left omitted - because table[x + 8192]
	// is by definition 65536 * 2^(x/8192) when x is negative.  exact,
	// not an approximation, and it is what lets the table be half the
	// size the +/- range would otherwise need
	uint32_t mul = (x < 0)
		? power_table[(x + 8192) >> POWER_SHIFT]		// 1:16, [0.5, 1)
		: power_table[x >> POWER_SHIFT] << 1;			// 1:16, [1, 2)

	// the product needs 48 bits.  this used to be split into 16-bit
	// halves and reassembled, because ARMv6-M has no 32x32->64
	// multiply; the M33 does, so one umull covers it.  the result is
	// bit identical - the old form was exactly this sum
	step = ((uint64_t)step * mul) >> 16;
}

// combines a 7-bit patch parameter with its controller, which offsets
// it either side of centre, and holds the result in range
static inline uint8_t cc_offset(uint8_t base, uint8_t cc)
{
	int16_t v = (int16_t)base + cc - 64;

	if (v < 0) return 0;
	if (v > 127) return 127;

	return v;
}

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

// temporary buffer of mono samples
static int16_t mono[BUFFER_SIZE];

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

	// set up the interpolator
	interp_config cfg = interp_default_config();
	interp_config_set_shift(&cfg, 15);
	interp_config_set_mask(&cfg, 1, WAVE_SHIFT);
	interp_config_set_add_raw(&cfg, true);
	interp_set_config(interp0, 0, &cfg);

	for (auto& v : pool) {

		++voices;

		// get a reference to the channel parameters
		auto& chan = *v.channel;

		// and a reference to the current note's patch
		auto& p = *v.patch;

		// the chain is 15 + 7 + 7 + 7 + 7 + 7 = 50 bits.  the 32-bit
		// form had to shift twice partway through to stay in range,
		// losing 11 bits, and still finished within 1% of overflowing
		// uint32_t on the last multiply.  the M33 carries all 50 bits,
		// so the final shift is the only rounding left

		// get the 15-bit DCA current envelope level
		uint64_t dca = v.dca_env->level();		// 15 bits

		// scale the DCA by the patch's 7-bit DCA master level
		dca *= p.dca_env_level;					// 22 bits

		// scale the DCA by the 7-bit note velocity
		dca *= v.vel;							// 29 bits

		// scale the DCA by the 7-bit channel volume
		dca *= chan.control[volume];			// 36 bits

		// and by expression, which is the other half of MIDI volume -
		// CC 7 is the mix setting, CC 11 is what a part is played with,
		// and a score's dynamics live in this one
		dca *= chan.control[expression];		// 43 bits

		// apply 7-bit pan and scale back to 16 bits
		uint16_t level_l = (dca * chan.pan_l) >> 34;	// 50 - 34
		uint16_t level_r = (dca * chan.pan_r) >> 34;

		// scale the DCO step by the current pitchbend amount
		v.dco_step = v.dco_step_base;
		if (chan.bend) {
			frequency_modulate(v.dco_step, chan.bend_f);
		}
		// apply the DCO envelope.  frequency_modulate takes a 14-bit
		// signed value in which 8192 is one octave, so the shift is what
		// decides how far a full-depth pitch envelope can reach: the
		// envelope peaks at 0x7fff and the patch level at 127, and
		// 32767 * 127 >> 9 is 8127, which is that octave to within
		// 0.8%.  It was >> 10, which reached 4063 - half an octave -
		// while the comments alongside claimed 16, 24 and 14 bits for
		// values that are actually 15, 22 and 12
		if (v.dco_env && p.dco_env_level) {
			int32_t env = v.dco_env->level();	// 15 bits
			env = env * p.dco_env_level;		// 22 bits
			env >>= 9;							// 14 bits, 8192/octave
			frequency_modulate(v.dco_step, env);
		}

		// update and apply the LFO
		uint8_t wheel = chan.control[modwheel];
		v.lfo_step = note_table[p.lfo_freq];
		v.lfo_pos = (v.lfo_pos + v.lfo_step) & (WAVE_MAX - 1);
		if (wheel && p.lfo_depth) {
			int16_t* lfo_wave = waves[p.lfo_wave];
			int32_t lfo_amount = lfo_wave[v.lfo_pos >> 16];	// 16 bits
			lfo_amount *= p.lfo_depth;						// 23 bits
			lfo_amount *= wheel;							// 30 bits
			lfo_amount >>= 16;								// 14 bits
			frequency_modulate(v.dco_step, lfo_amount);
		}

		// generate a buffer full of (mono) samples
		v.update(mono, n);

		// set the filter from the patch, offset by the channel's two
		// sound controllers.  this is done per buffer rather than at
		// note-on so that moving a controller takes effect on notes
		// that are already sounding
		//
		// cutoff_table lands on one of 128 coarse steps; the envelope
		// then moves it in units of 1/SVF_STEPS of a semitone, which is
		// what svf_table's sub-semitone resolution exists for - nothing
		// else can reach between the coarse steps
		int32_t cutoff = cutoff_table[cc_offset(p.dcf_freq, chan.control[brightness])];

		if (v.dcf_env) {
			// depth is centred on 64, so it spans -64 .. +63 semitones
			int32_t depth = (int32_t)p.dcf_env_level - 64;
			int32_t env = v.dcf_env->level();		// 15 bits

			// 64 * 16 * 32767 is 33.5M, so this stays inside int32
			cutoff += (depth * SVF_STEPS * env) >> 15;

			// set_cutoff clamps the top but takes a uint16_t, so a
			// negative sweep would wrap to wide open instead of closing
			if (cutoff < 0) {
				cutoff = 0;
			} else if (cutoff > SVF_LEN - 1) {
				cutoff = SVF_LEN - 1;
			}
		}

		v.filter->set_cutoff(cutoff);
		v.filter->set_q(cc_offset(p.dcf_reso, chan.control[resonance]));

		// apply the filter.  this has to happen even while the voice
		// is inaudible, otherwise its state is stale by the time the
		// level comes back up, and it clicks
		v.filter->apply(mono, n);

		// don't bother accumulating silent channels
		if (!dca) continue;

		// accumulate the samples into the supplied output buffer
		for (size_t i = 0, j = 0; i < n; ++i) {
			samples[j++] += (level_l * mono[i]) >> 16;
			samples[j++] += (level_r * mono[i]) >> 16;
		}
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
