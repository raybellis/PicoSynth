#include "hardware/interp.h"

#include "voice.h"
#include "envelope.h"
#include "waves.h"

extern const uint32_t note_table[];

//--------------------------------------------------------------------+
// Per-voice state
//--------------------------------------------------------------------+

void Voice::init()
{
	free = true;
	steal = false;
	channel = nullptr;
	patch = nullptr;
	dca_env = nullptr;
	dco_env = nullptr;
	filter = nullptr;
}

Voice::Voice()
{
	init();
}

void Voice::update(int16_t* samples, size_t n)
{
	// copy voice state to the interpolator
	interp0->base[0] = dco_step;
	interp0->base[2] = (uint32_t)waves[patch->dco_wave];
	interp0->accum[0] = dco_pos;

	// generate the samples
	for (uint i = 0; i < n; ++i) {
		samples[i] = *(int16_t*)interp0->pop[2];
	}

	// update voice state
	dco_pos = interp0->accum[0] & (wave_max - 1);
}

void Voice::note_on(uint8_t _chan, uint8_t _note, uint8_t _vel)
{

	// remember note parameters
	note = _note;
	vel = _vel;

	// load the current patch parameters
	auto& p = *patch;

	// set up the DCA envelope
	dca_env = new ADSR(p.dca_env_a, p.dca_env_d, p.dca_env_s, p.dca_env_r);
	dca_env->gate_on();

	// set up the DCO envelope
	if (p.dco_env_level) {
		dco_env = new ADSR(p.dco_env_a, p.dco_env_d, p.dco_env_s, p.dco_env_r);
		dco_env->gate_on();
	}

	// setup DCO
	dco_step_base = note_table[note];
	dco_pos = 0;

	// set up the filter
	filter = new SVF();
	filter->set_cutoff(8192);
	filter->set_q(16384);
}

void Voice::note_off()
{
	dca_env->gate_off();

	if (dco_env) {
		dco_env->gate_off();
	}

	steal = true;		// voice may now be stolen
}

//--------------------------------------------------------------------+
// Voice allocation
//--------------------------------------------------------------------+

// marks a voice as in use, and hands it to the caller.  Both of the
// paths through allocate() funnel through here, so that a voice can
// never be returned still flagged as free.
Voice* VoicePool::claim(Voice& v)
{
	v.free = false;
	v.steal = false;

	return &v;
}

Voice* VoicePool::allocate()
{
	// look for a spare voice
	for (auto& v: voice) {
		if (v.free) {
			return claim(v);
		}
	}

	// none found, so steal one that's already been released
	for (auto& v: voice) {
		if (v.steal) {
			release(v);
			return claim(v);
		}
	}

	return nullptr;
}

void VoicePool::release(Voice& v)
{
	delete v.dca_env;
	delete v.dco_env;
	delete v.filter;

	// resets the pointers above, and marks the voice free
	v.init();
}
