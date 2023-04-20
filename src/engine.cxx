#include "hardware/interp.h"
#include "hardware/divider.h"

#include "engine.h"
#include "audio.h"
#include "envelope.h"
#include "midi.h"
#include "waves.h"

//--------------------------------------------------------------------+
// Per-voice state
//--------------------------------------------------------------------+

void Voice::init()
{
	free = true;
	steal = false;
	chan = 0xff;
	dca = nullptr;
}

Voice::Voice()
{
	init();
}

void Voice::update(int16_t* samples, size_t n)
{
	// copy voice state to the interpolator
	interp0->base[0] = step;
	interp0->base[2] = (uint32_t)wavetable;
	interp0->accum[0] = pos;

	// generate the samples
	for (uint i = 0; i < n; ++i) {
		samples[i] = *(int16_t*)interp0->pop[2];
	}

	// update voice state
	pos = interp0->accum[0] & (wave_max - 1);
}

// calculate this using a power table:
//   float f = 440.0 * powf(2.0, (note - 69) * (1.0 / 12.0));
//   step = 0x10000 * wave_len * f * (1.0 / SAMPLE_RATE);
static uint32_t __attribute__((noinline)) step_for_note(uint8_t note)
{
	extern uint16_t powers[];

	// normalize base frequency around MIDI note
	// range 57 - 80, i.e. A3 -> A4 (440Hz) -> B5
	uint32_t f = 440;
	while (note > 80) {
		f <<= 1;
		note -= 12;
	}
	while (note < 57) {
		f >>= 1;
		note += 12;
	}

	// calculate offset into the 16k entry powers table
	hw_divider_divmod_s32_start((note - 69) * 8192, 12);

	auto res = hw_divider_result_wait();
	uint16_t offset = 8192 + to_quotient_s32(res);

	// adjust the frequency - NB: goes out of Hz scale
	f *= powers[offset];		// Hz * (1 << 15)

	// we'll needed f * 0x10000 anyway, so one more shift does that
	f <<= 1;					// Hz * (1 << 16)

	return f * ((float)wave_len / SAMPLE_RATE);
}

void Voice::note_on(uint8_t _chan, uint8_t _note, uint8_t _vel)
{
	// remember note parameters
	chan = _chan;
	note = _note;
	vel = _vel;

	// determine patch waveform
	wavetable = waves[chan % 4];

	// set up the envelope
	dca = new ADSR(30, 20, 80, 20);
	dca->gate_on();

	// setup NCO
	step_base = step_for_note(note);
	pos = 0;
}

void Voice::note_off()
{
	dca->gate_off();
	steal = true;		// voice may now be stolen
}

//--------------------------------------------------------------------+
// Core synth engine
//--------------------------------------------------------------------+

SynthEngine::SynthEngine()
{
	// all voices start out unused
	for (auto& v : voice) {
		v.init();
	}
}

void SynthEngine::deallocate(Voice& v)
{
	delete v.dca;
	v.init();
}

Voice* SynthEngine::allocate()
{
	// look for a spare voice
	for (auto& v: voice) {
		if (v.free) {
			v.free = false;
			v.steal = false;
			return &v;
		}
	}

	// none-found, we need to steal
	for (auto& v: voice) {
		if (v.steal) {
			deallocate(v);
			return &v;
		}
	}

	return nullptr;
}

// temporary buffer of mono samples
static int16_t mono[SAMPLES_PER_BUFFER];

void __not_in_flash_func(SynthEngine::update)(int32_t* samples, size_t n)
{
	// update all DCAs, and release any voice
	// that now has an inactive DCA
	for (auto& v: voice) {
		if (v.free) continue;

		v.dca->update();
		if (!v.dca->active()) {
			deallocate(v);
		}
	}

	// set up the interpolator
	interp_config cfg = interp_default_config();
	interp_config_set_shift(&cfg, 15);
	interp_config_set_mask(&cfg, 1, wave_shift);
	interp_config_set_add_raw(&cfg, true);
	interp_set_config(interp0, 0, &cfg);

	for (auto& v : voice) {

		// voice not in use
		if (v.free) continue;

		// get a reference to the channel parameters
		assert(v.chan < 0x10);

		auto& chan = channel[v.chan];

		// get the 15-bit DCA level
		uint32_t dca = v.dca->level();

		// scale the DCA by the 7-bit note velocity
		dca *= v.vel;							// 22 bits

		// scale the DCA by the 7-bit channel volume
		dca *= chan.control[volume];			// 29 bits
		dca >>= 4;								// 25 bits

		// apply pan and scale back to 16 bits
		uint16_t level_l = (dca * chan.pan_l) >> 16;
		uint16_t level_r = (dca * chan.pan_r) >> 16;

		// scale the NCO step by the current pitchbend amount
		v.step = v.step_base;
		if (chan.bend) {
			v.step = ((uint64_t)v.step * chan.bend_f) >> 15;
		}

		// generate a buffer full of (mono) samples
		v.update(mono, n);

		// accumulate the samples into the supplied output buffer
		for (size_t i = 0, j = 0; i < n; ++i) {
			samples[j++] += (level_l * mono[i]) >> 16;
			samples[j++] += (level_r * mono[i]) >> 16;
		}
	}
}

void SynthEngine::note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	auto* vp = allocate();
	if (vp) {
		auto& v = *vp;
		v.note_on(chan, note, vel);
	}
}

void SynthEngine::note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
	for (auto& v: voice) {
		if (v.free) continue;
		if (v.chan == chan && v.note == note) {
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
		case 0xc:
		case 0xd:
		case 0xe:
			channel[chan].midi_in(c, d1, d2);
			break;
		case 0xf:
			break;
	}
}
