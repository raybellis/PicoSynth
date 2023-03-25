#include <cmath>
#include <cstdio>

#include "audio.h"
#include "engine.h"
#include "oscillator.h"
#include "envelope.h"
#include "midi.h"

SynthEngine::SynthEngine()
{
	// all voices start out unused
	for (auto& v : voice) {
		v.free = true;
		v.steal = false;
		v.chan = nullptr;
		v.osc = nullptr;
		v.dca = nullptr;
	}
}

void SynthEngine::deallocate(Voice& v)
{
	delete v.dca;
	delete v.osc;
	v.chan = nullptr;
	v.osc = nullptr;
	v.dca = nullptr;
	v.free = true;
	v.steal = false;
}

SynthEngine::Voice* SynthEngine::allocate()
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

void SynthEngine::update(int32_t* samples, size_t n)
{
	for (auto& v : voice) {

		// voice not in use
		if (v.free) continue;

		// get the 16-bit DCA level, and release the voice if it's finished
		uint32_t dca = v.dca->update();
		if (!v.dca->active()) {
			deallocate(v);
			continue;
		}

		// scale the DCA by the 7-bit note velocity
		dca *= v.vel;							// 23 bits

		// scale the DCA by the 7-bit channel volume
		dca *= v.chan->control[volume];			// 30 bits
		dca >>= 7;								// 23 bits

		// apply pan
		auto level_l = dca * v.chan->pan_l;		// 30 bits
		auto level_r = dca * v.chan->pan_r;		// 30 bits

		// scale the levels back into a usable (16-bit) range
		level_l >>= 14;
		level_r >>= 14;

		// get the 14-bit pitch bend amount and scale the frequency
		float bend = v.chan->bend_f;			// range ±1
		bend *= 2.0;							// c.chan->bend range

		// calculate the resulting frequency
		float freq = 440.0 * powf(2.0, (bend + v.note - 69) / 12.0);
		v.osc->set_frequency(freq);

		// generate a buffer full of (mono) samples
		v.osc->update(mono, n);

		// accumulate the samples into the supplied output buffer
		for (size_t i = 0, j = 0; i < n; ++i) {
#if SAMPLE_CHANS == 1
			samples[j++] += ((uint16_t)level * mono[i]) >> 16;
#else
			// TODO: pan position from v.chan->control[pan]
			samples[j++] += ((uint16_t)level_l * mono[i]) >> 16;
			samples[j++] += ((uint16_t)level_r * mono[i]) >> 16;
#endif
		}
	}
}

void SynthEngine::note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	if (vel == 0) {
		note_off(chan, note, 0);
	} else {
		auto* vp = allocate();
		if (vp) {
			auto& v = *vp;
			v.chan = &channel[chan];
			v.dca = new ADSR(30, 20, 80, 20);
			v.osc = new SawtoothOscillator();

			v.note = note;
			v.vel = vel;
			v.dca->gate_on();
			v.osc->sync();
		}
	}
}

void SynthEngine::note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
	for (auto& v: voice) {
		if (v.free) continue;
		if (v.chan == &channel[chan] && v.note == note) {
			v.dca->gate_off();
			v.steal = true;		// voice may now be stolen
		}
	}
}

void SynthEngine::midi_in(uint8_t c, uint8_t d1, uint8_t d2)
{
	uint8_t cmd = (c & 0xf0);
	uint8_t chan = (c & 0x0f);

	switch (cmd >> 4) {
		case 0x8:
			note_off(chan, d1, d2);
			break;
		case 0x9:
			note_on(chan, d1, d2);
			break;
		case 0xb:
		case 0xc:
		case 0xd:
		case 0xe:
			channel[chan].midi_in(cmd, d1, d2);
			break;
		case 0xf:
			break;
	}
}
