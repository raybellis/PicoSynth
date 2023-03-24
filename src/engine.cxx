#include <cmath>

#include "audio.h"
#include "engine.h"
#include "oscillator.h"
#include "envelope.h"
#include "midi.h"

SynthEngine::SynthEngine()
{
	for (auto& v : voice) {
		v.osc = nullptr;
		v.dca = nullptr;
		v.chan = nullptr;
	}
}

void SynthEngine::deallocate(Voice& v)
{
	delete v.osc;
	delete v.dca;
	v.chan = nullptr;
}

static int16_t temp[SAMPLES_PER_BUFFER];

void SynthEngine::update(int32_t* samples, size_t n)
{
	for (auto& v : voice) {

		// voice not in use
		if (!v.chan) continue;

		// get the 15-bit DCA level, and release the voice if it's finished
		auto dca = v.dca->update();
		if (!v.dca->active()) {
			deallocate(v);
			continue;
		}

		// TODO: get the 14-bit pitch bend amount and scale the frequency
		// 8192 (0x2000) = 0 bend
		uint16_t bend = 8192;	// v.chan->bend;
		uint8_t range = 2;		// c.chan->bend range
		float bf = range * bend / 8192;
    	float freq = 440.0 * powf(2.0, (bf + v.note - 69) / 12.0);
		v.osc->set_frequency(freq);

		// generate a buffer full of (mono) samples
		v.osc->update(temp, n);

		// scale the DCA by the 7-bit note velocity
		dca *= v.vel;							// 22 bits

		// TODO: scale the DCA by the 7-bit channel volume
		dca *= 100;
		// dca *= v.chan->control[volume]		// 29 bits

		// accumulate the samples into the supplied output buffer
		for (auto i = 0, j = 0; i < n; ++i) {
#if SAMPLE_CHANS == 1
			samples[j++] += ((dca >> 13) * temp[i]) >> 16;
#else
			// TODO: pan position from v.chan->control[pan]
			samples[j++] += ((dca >> 13) * temp[i]) >> 16;
			samples[j++] += ((dca >> 13) * temp[i]) >> 16;
#endif
		}
	}
}

void SynthEngine::note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	if (vel == 0) {
		note_off(chan, note, 0);
	} else {
		for (auto& v : voice) {
			if (!v.chan) {
				v.chan = &channel[chan];
				v.dca = new ADSR(30, 20, 80, 20);
				v.osc = new SawtoothOscillator();

				v.note = note;
				v.vel = vel;
				v.dca->gate_on();
				v.osc->sync();

				return;
			}
		}
	}
}

void SynthEngine::note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
	for (auto& v: voice) {
		if (v.chan == &channel[chan] && v.note == note) {
			v.dca->gate_off();
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
		case 0x9:
			note_on(chan, d1, d2);
			break;
		case 0xb:
		case 0xc:
		case 0xd:
		case 0xe:
			channel[c].midi_in(cmd, d1, d2);
			break;
		case 0xf:
			break;
	}
}
