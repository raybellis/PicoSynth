#include <cstring>

#include "pico.h"

#include "voice.h"
#include "data.h"
#include "dsp.h"
#include "envelope.h"
#include "midi.h"
#include "waves.h"

//--------------------------------------------------------------------+
// Utility functions
//--------------------------------------------------------------------+

// x is a (14-bit signed) offset into the power table which contains
// 1:15 fixed-point log2 multipliers for x = 0.500 ..< 2.000
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

// one buffer of mono samples, and the wider accumulator the oscillator
// mix needs before it is scaled back into it
static int16_t				mono[BUFFER_SIZE];

//--------------------------------------------------------------------+
// Per-voice state
//--------------------------------------------------------------------+

void Voice::init()
{
	free = true;
	steal = false;
	sustained = false;
	channel = nullptr;
	patch = nullptr;
	dca_env = nullptr;
	dco_env = nullptr;
	dcf_env = nullptr;
	aux_env = nullptr;
	filter = nullptr;
}

Voice::Voice()
{
	init();
}

// runs from RAM - this is the per-sample oscillator loop, called for
// every active voice on every buffer.
//
// Three voices from two accumulators.  dco[0] and the sub share one,
// because the sub *is* dco[0] an octave or more down: the accumulator
// runs at the sub's rate and dco[0] is read from it shifted up, since a
// slower phase cannot be derived from a faster one but the reverse is
// free.  That saves a phase, a step, a table pointer and a round of
// pitch modulation, and registers have decided every measurement here.
//
// No interpolator.  Measured against it: computed saw was 11.9% faster
// and direct table indexing level, once the pointers were deduplicated.
// Its one advantage - holding phase and step off the register file -
// is worth less than not needing a table pointer at all, and it came
// with a cap of two units that forced a second pass over the buffer.
void __not_in_flash_func(Voice::oscillators)(int16_t* out, size_t n)
{
	const Osc& o0 = patch->dco[0];
	const Osc& o1 = patch->dco[1];

	const int32_t l0 = o0.level;
	const int32_t ls = patch->sub_level;

	// dco[1]'s level, scaled by its own envelope when it has one.  This
	// is a block-rate value like every other level here - it stays a
	// constant across the sample loops below, so the whole feature costs
	// one multiply per voice per buffer and nothing in the inner loop
	int32_t l1 = o1.level;
	if (aux_env) {
		l1 = (l1 * aux_env->level()) >> 15;
	}

	if (!l0 && !l1 && !ls) {
		memset(out, 0, n * sizeof(int16_t));
		return;
	}

	// the shared accumulator runs at the sub's rate when there is one
	const int k = ls ? (patch->sub_octaves ? patch->sub_octaves : 1) : 0;

	uint32_t p0 = dco_pos[0], s0 = dco_step[0] >> k;
	uint32_t p1 = dco_pos[1], s1 = dco_step[1];

	const int16_t* w0 = waves[o0.wave];
	const int16_t* w1 = waves[o1.wave & 3];

	constexpr int SH = 32 - WAVE_SHIFT;

	// Saw is an exact function of the phase - the table is
	// 32767 - i*32 and nothing else - so a saw oscillator needs no table
	// read and, more to the point, no table pointer.  When dco[0] is a
	// saw its sub is one too, since the sub *is* dco[0], and the pair
	// then costs no pointer between them.
	//
	// The shapes are chosen once per buffer: dco[0] computed or read,
	// the sub present or not, and an auxiliary that is a table, a saw or
	// the noise generator.  Noise advances its phase by a different rule
	// so it cannot share a loop with the other two.
	const bool saw0 = (o0.wave == SAW);
	const bool noise = (o1.wave == NOISE);
	const bool saw1 = (o1.wave == SAW);

#define P0T		(w0[(p0 << k) >> SH] * l0)
#define P0S		(((int32_t)(p0 << k) >> 16) * l0)
#define SBT		+ (w0[p0 >> SH] * ls)
#define SBS		+ (((int32_t)p0 >> 16) * ls)
#define SB0
#define AXT		+ (w1[p1 >> SH] * l1)
#define AXS		+ (((int32_t)p1 >> 16) * l1)
#define STEP0	p0 += s0;
#define STEPT	p1 += s1;

	// an LCG, whose high bits are the usable ones - so extracting a
	// sample costs the same shift a table index would have, and the saw
	// extraction serves for both
#define STEPN	p1 = p1 * 1664525u + 1013904223u;

#define MIX(A, B, SA, C) \
	for (size_t i = 0; i < n; ++i) { \
		STEP0 SA out[i] = (int16_t)clamp16((A B C) >> 7); \
	}

	if (saw0 && ls) {
		if (noise)     { MIX(P0S, SBS, STEPN, AXS) }
		else if (saw1) { MIX(P0S, SBS, STEPT, AXS) }
		else           { MIX(P0S, SBS, STEPT, AXT) }
	} else if (saw0) {
		if (noise)     { MIX(P0S, SB0, STEPN, AXS) }
		else if (saw1) { MIX(P0S, SB0, STEPT, AXS) }
		else           { MIX(P0S, SB0, STEPT, AXT) }
	} else if (ls) {
		if (noise)     { MIX(P0T, SBT, STEPN, AXS) }
		else if (saw1) { MIX(P0T, SBT, STEPT, AXS) }
		else           { MIX(P0T, SBT, STEPT, AXT) }
	} else {
		if (noise)     { MIX(P0T, SB0, STEPN, AXS) }
		else if (saw1) { MIX(P0T, SB0, STEPT, AXS) }
		else           { MIX(P0T, SB0, STEPT, AXT) }
	}

#undef P0T
#undef P0S
#undef SBT
#undef SBS
#undef SB0
#undef AXT
#undef AXS
#undef STEP0
#undef STEPT
#undef STEPN
#undef MIX

	dco_pos[0] = p0;
	dco_pos[1] = p1;
}

// One voice, one buffer, accumulated into the stereo output.
//
// Runs from RAM, and everything per-voice happens here: the DCA chain,
// the pitch modulation, the oscillators, the filter.  The engine's job
// is to decide *which* voices exist, not how they sound.
void __not_in_flash_func(Voice::render)(int32_t* samples, size_t n)
{
	auto& chan = *channel;
	auto& p = *patch;

	// the chain is 15 + 7 + 7 + 7 + 7 + 7 = 50 bits.  the 32-bit form
	// had to shift twice partway through to stay in range, losing 11
	// bits, and still finished within 1% of overflowing uint32_t on the
	// last multiply.  the M33 carries all 50 bits, so the final shift
	// is the only rounding left

	// get the 15-bit DCA current envelope level
	uint64_t dca = dca_env->level();			// 15 bits

	// scale the DCA by the patch's 7-bit DCA master level
	dca *= p.dca_env_level;						// 22 bits

	// scale the DCA by the 7-bit note velocity
	dca *= vel;									// 29 bits

	// scale the DCA by the 7-bit channel volume
	dca *= chan.control[volume];				// 36 bits

	// and by expression, which is the other half of MIDI volume - CC 7
	// is the mix setting, CC 11 is what a part is played with, and a
	// score's dynamics live in this one
	dca *= chan.control[expression];			// 43 bits

	// apply 7-bit pan and scale back to 16 bits
	uint16_t level_l = (dca * chan.pan_l) >> 34;	// 50 - 34
	uint16_t level_r = (dca * chan.pan_r) >> 34;

	// the pitch modulation is common to all three oscillators - what
	// differs between them is their tuning, and that is already baked
	// into dco_step_base by note_on()
	int16_t bend = chan.bend ? chan.bend_f : 0;

	// apply the DCO envelope.  frequency_modulate takes a 14-bit signed
	// value in which 8192 is one octave, so the shift is what decides
	// how far a full-depth pitch envelope can reach: the envelope peaks
	// at 0x7fff and the patch level at 127, and 32767 * 127 >> 9 is
	// 8127, which is that octave to within 0.8%
	int16_t penv = 0;
	if (dco_env && p.dco_env_level) {
		int32_t env = dco_env->level();			// 15 bits
		env = env * p.dco_env_level;			// 22 bits
		penv = (int16_t)(env >> 9);				// 14 bits, 8192/octave
	}

	// --- the LFO ----------------------------------------------------
	//
	// The per-voice phase advances whether or not this patch uses it,
	// so that a voice which does is never in step with the last note
	// that used the same slot - that arbitrariness is the point, and is
	// what makes the per-voice mode sound like an ensemble rather than
	// one instrument.  lfo_global picks the channel's phase instead,
	// which all its notes share, and reads as vibrato.
	lfo_step = note_table[p.lfo_freq];
	lfo_pos = (lfo_pos + lfo_step) & (WAVE_MAX - 1);

	uint32_t phase = p.lfo_global ? chan.lfo_pos : lfo_pos;

	// the fade-in, applied to the LFO itself rather than to each of its
	// depths, so every destination arrives together
	if (lfo_ramp < 0x7fff) {
		int32_t r = p.lfo_delay
			? lfo_ramp + (128 - p.lfo_delay) * 4
			: 0x7fff;

		lfo_ramp = (uint16_t)((r > 0x7fff) ? 0x7fff : r);
	}

	int32_t lfo_raw = waves[p.lfo_wave][phase >> 16];		// 16 bits
	lfo_raw = (lfo_raw * lfo_ramp) >> 15;

	// How much LFO is running: the patch's own, plus what the two
	// controllers add.  Applied to the oscillator itself rather than to
	// each destination, so the mod wheel reaches the filter sweep and
	// the tremolo as well as the pitch - it used to reach only the
	// pitch, which left the other two stuck at whatever the patch said
	int32_t amount = p.lfo_amount
		+ ((p.lfo_wheel * chan.control[modwheel]) >> 7)
		+ ((p.lfo_press * chan.pressure) >> 7);

	if (amount > 127) {
		amount = 127;
	}

	lfo_raw = (lfo_raw * amount) >> 7;

	// and where it goes.  32767 * 127 >> 9 is 8127, an octave to within
	// 0.8% - the same reduction the DCO envelope makes, and for the
	// same reason
	int16_t lfo = (int16_t)((lfo_raw * p.lfo_pitch) >> 9);

	// Tremolo ducks from unity rather than modulating around it, so the
	// gain can never exceed 1 and nothing downstream has to keep room
	// for it.  It also has to be applied here, to the levels, rather
	// than folded into the DCA chain - that is already 50 bits, and one
	// more 15-bit stage would put it past what a uint64_t holds
	if (p.lfo_dca) {
		int32_t duck = (((32767 - lfo_raw) >> 1) * p.lfo_dca) >> 7;
		int32_t trem = 32767 - duck;

		level_l = (uint16_t)(((int32_t)level_l * trem) >> 15);
		level_r = (uint16_t)(((int32_t)level_r * trem) >> 15);
	}

	for (int d = 0; d < NDCO; ++d) {
		uint32_t step = dco_step_base[d];

		if (bend) frequency_modulate(step, bend);
		if (penv) frequency_modulate(step, penv);
		if (lfo)  frequency_modulate(step, lfo);

		dco_step[d] = step;
	}

	// generate a buffer full of (mono) samples
	oscillators(mono, n);

	// set the filter from the patch, offset by the channel's two sound
	// controllers.  this is done per buffer rather than at note-on so
	// that moving a controller takes effect on notes already sounding
	//
	// cutoff_table lands on one of 128 coarse steps; the envelope then
	// moves it in units of 1/SVF_STEPS of a semitone, which is what
	// svf_table's sub-semitone resolution exists for - nothing else can
	// reach between the coarse steps
	int32_t cutoff = cutoff_table[cc_offset(p.dcf_freq, chan.control[brightness])];

	// everything else that moves the cutoff, all in fine steps of
	// 1/SVF_STEPS of a semitone.  key tracking is measured from note 60
	// so that dcf_freq stays the cutoff at middle C whatever it is set
	// to, and 127 means the cutoff follows the keyboard one for one
	if (p.dcf_track) {
		cutoff += (((int32_t)note - 60) * SVF_STEPS * p.dcf_track) / 127;
	}

	if (p.dcf_vel) {
		cutoff += ((int32_t)p.dcf_vel * SVF_STEPS * vel) / 127;
	}

	if (p.dcf_press) {
		cutoff += ((int32_t)p.dcf_press * SVF_STEPS * chan.pressure) / 127;
	}

	if (p.lfo_dcf) {
		cutoff += (lfo_raw * p.lfo_dcf * SVF_STEPS) >> 15;
	}

	if (dcf_env) {
		int32_t env = dcf_env->level();			// 15 bits

		// 127 * 16 * 32767 is 66.6M, so this stays inside int32
		cutoff += ((int32_t)p.dcf_env_level * SVF_STEPS * env) >> 15;
	}

	// clamped once, after everything that moves it.  set_cutoff clamps
	// the top but takes a uint16_t, so anything that drove this negative
	// would wrap to wide open instead of closing
	if (cutoff < 0) {
		cutoff = 0;
	} else if (cutoff > SVF_LEN - 1) {
		cutoff = SVF_LEN - 1;
	}

	filter->set_cutoff(cutoff);
	filter->set_q(cc_offset(p.dcf_reso, chan.control[resonance]));

	// apply the filter.  this has to happen even while the voice is
	// inaudible, otherwise its state is stale by the time the level
	// comes back up, and it clicks
	filter->apply(mono, n);

	// don't bother accumulating silent voices
	if (!dca) return;

	// accumulate the samples into the supplied output buffer
	for (size_t i = 0, j = 0; i < n; ++i) {
		samples[j++] += (level_l * mono[i]) >> 16;
		samples[j++] += (level_r * mono[i]) >> 16;
	}
}

void Voice::note_on(uint8_t _chan, uint8_t _note, uint8_t _vel)
{

	// remember note parameters
	note = _note;
	vel = _vel;

	// the LFO fade-in restarts with the note.  its *phase* deliberately
	// does not - see render()
	lfo_ramp = 0;

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

	// set up the DCF envelope, which only exists at non-zero depth
	if (p.dcf_env_level) {
		dcf_env = new ADSR(p.dcf_env_a, p.dcf_env_d, p.dcf_env_s, p.dcf_env_r);
		dcf_env->gate_on();
	}

	// and dco[1]'s own, which exists only if the patch asked for one.
	// there is no depth field to test here as there is for the other
	// two, so all four parameters zero is the sentinel for "follow the
	// DCA" - safe because it otherwise describes the slowest possible
	// attack into silence, which no patch wants
	if (p.aux_env_a || p.aux_env_d || p.aux_env_s || p.aux_env_r) {
		aux_env = new ADSR(p.aux_env_a, p.aux_env_d, p.aux_env_s, p.aux_env_r);
		aux_env->gate_on();
	}

	// set up the oscillators.  coarse and fine are both fixed for the
	// life of the note, so they are folded into the base step here and
	// cost nothing per buffer afterwards
	for (int i = 0; i < NDCO; ++i) {
		const Osc& o = p.dco[i];

		// coarse shifts the note itself, which is exact - a semitone is
		// a table entry - and is not limited to the octave either side
		// that frequency_modulate can reach
		int32_t n = (int32_t)note + o.coarse;
		if (n < 0) {
			n = 0;
		} else if (n > 127) {
			n = 127;
		}

		uint32_t step = note_table[n];

		// fine is in 64ths of a semitone, and frequency_modulate wants
		// 8192 to the octave, so a 64th of a semitone is 8192/(12*64)
		if (o.fine) {
			frequency_modulate(step, (int16_t)((o.fine * 8192) / (12 * 64)));
		}

		// scaled to a full 32-bit accumulator, which wraps of its own
		// accord - the phase used to be 16.16 inside WAVE_MAX and needed
		// masking, which the interpolator did in hardware
		dco_step_base[i] = step << 5;
		dco_pos[i] = 0;
	}

	// set up the filter.  its cutoff and resonance aren't set here -
	// SynthEngine::update does that from the patch and the channel
	// controllers every buffer, before it ever calls apply()
	filter = new SVF();
}

void Voice::note_off()
{
	dca_env->gate_off();

	if (dco_env) {
		dco_env->gate_off();
	}

	if (dcf_env) {
		dcf_env->gate_off();
	}

	if (aux_env) {
		aux_env->gate_off();
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
	delete v.dcf_env;
	delete v.aux_env;
	delete v.filter;

	// resets the pointers above, and marks the voice free
	v.init();
}
