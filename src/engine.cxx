#include <cmath>
#include <cstdio>

#include "audio.h"
#include "engine.h"
#include "oscillator.h"
#include "envelope.h"
#include "midi.h"

void Voice::init()
{
	free = true;
	steal = false;
	chan = 0xff;
	osc = nullptr;
	dca = nullptr;
}

Voice::Voice()
{
	init();
}

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
	delete v.osc;
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

	putchar('X');
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

	for (const auto& v : voice) {

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

#if SAMPLE_CHANS == 1
		uint16_t level = dca >> 9;				// 16 bits
#else
		// apply pan and scale back to 16 bits
		uint16_t level_l = (dca * chan.pan_l) >> 16;
		uint16_t level_r = (dca * chan.pan_r) >> 16;
#endif

		// get the pitch bend amount (in floating-point semitones)
		float bend = chan.bend_f;

		// calculate the resulting frequency based on MIDI note 69 = A440
		float freq = 440.0 * powf(2.0, (bend + v.note - 69) / 12.0);
		v.osc->set_frequency(freq);

		// generate a buffer full of (mono) samples
		v.osc->update(mono, n);

		// accumulate the samples into the supplied output buffer
		for (size_t i = 0, j = 0; i < n; ++i) {
#if SAMPLE_CHANS == 1
			samples[j++] += (level * mono[i]) >> 16;
#else
			samples[j++] += (level_l * mono[i]) >> 16;
			samples[j++] += (level_r * mono[i]) >> 16;
#endif
		}
	}
}

void SynthEngine::note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	auto* vp = allocate();
	if (vp) {
		auto& v = *vp;
		v.chan = chan;
		v.dca = new ADSR(30, 20, 80, 20);
		switch (chan % 4) {
		case 0:
			v.osc = new SineOscillator();
			break;
		case 1:
			v.osc = new SawtoothOscillator();
			break;
		case 2:
			v.osc = new SineOscillator();
			break;
		case 3:
			v.osc = new SquareOscillator();
			break;
		}

		v.note = note;
		v.vel = vel;
		v.dca->gate_on();
		v.osc->sync();	// TODO: defer until note start
	}
}

void SynthEngine::note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
	for (auto& v: voice) {
		if (v.free) continue;
		if (v.chan == chan && v.note == note) {
			v.dca->gate_off();
			v.steal = true;		// voice may now be stolen
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
