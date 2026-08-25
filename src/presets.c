#include "patch.h"

Patch presets[] = {
	{	// 0
		// all three oscillators: a detuned pair over a sub an
		// octave down.  the levels total 127, so the mix reaches
		// full scale without saturating
		.dco = {
			{ .wave = 2, .level = 45 },
			{ .wave = 2, .level = 45, .fine = 4 },
			{ .wave = 2, .level = 37, .coarse = -12 },
		},

		.dca_env_level	= 127,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,

		.lfo_freq		= 64,
		.lfo_wheel		= 20,
	},
	{	// 1
		// all three oscillators: a detuned pair over a sub an
		// octave down.  the levels total 127, so the mix reaches
		// full scale without saturating
		.dco = {
			{ .wave = 3, .level = 45 },
			{ .wave = 3, .level = 45, .fine = 4 },
			{ .wave = 3, .level = 37, .coarse = -12 },
		},

		.dca_env_level	= 100,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dco_env_level	= 0,
		.dco_env_a		= 127,
		.dco_env_d		= 40,
		.dco_env_s		= 0,
		.dco_env_r		= 0,

		// starts closed, at note 66 or 369 Hz, so that the envelope
		// below has somewhere to open *from* - a filter envelope on top
		// of an already-open filter only moves the upper harmonics and
		// is most of the way to inaudible.  the sweep runs 369 Hz ->
		// 2.3 kHz -> 658 Hz, a little under three octaves
		.dcf_freq		= 30,

		// Q 2.81, so about 9 dB of emphasis at the cutoff.  The peak is
		// what makes a sweep audible as a sweep - at the 64 the other
		// presets use, Q is 1.28 and worth only 2 dB, which reads as a
		// gentle tone change rather than a filter moving
		.dcf_reso		= 96,

		// how the filter answers the keyboard.  velocity is the one to
		// feel: play softly and the note is dark, lean on it and it
		// opens two octaves.  key tracking is measured from middle C,
		// at half a semitone of cutoff per semitone played, so the top
		// of the keyboard stays bright without the bottom going thin
		.dcf_vel		= 24,
		.dcf_track		= 64,

		// the one preset that uses the filter envelope, so there is
		// something to hear it on.  opens 32 semitones above the patch
		// cutoff and settles about 10 above it.
		//
		// the decay rate is per buffer, so 6 ms a step: this one falls
		// 128 a step over the 22527 between peak and sustain, which is
		// 176 steps, a little over a second
		.dcf_env_level	= 32,			// +32 semitones
		.dcf_env_a		= 40,
		.dcf_env_d		= 4,
		.dcf_env_s		= 40,
		.dcf_env_r		= 20,

		.lfo_freq		= 92,
		.lfo_wheel		= 20,
	},
	{	// 2
		// all three oscillators: a detuned pair over a sub an
		// octave down.  the levels total 127, so the mix reaches
		// full scale without saturating
		.dco = {
			{ .wave = 2, .level = 45 },
			{ .wave = 2, .level = 45, .fine = 4 },
			{ .wave = 2, .level = 37, .coarse = -12 },
		},

		.dca_env_level	= 100,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,

		// vibrato, and the demonstration of what the channel LFO is
		// for: one phase shared by every note, arriving 1.8 s after the
		// note rather than with it.  hold a chord and it swells in
		// together, which is what the per-voice phase cannot do - there
		// each note wobbles on its own and the result is a chorus
		.lfo_freq		= 92,			// 5.8 Hz
		.lfo_global		= 1,
		.lfo_delay		= 100,			// 1.8 s to full depth
		.lfo_depth		= 5,			// just under half a semitone
		.lfo_wheel		= 127,			// and the wheel adds an octave
		.lfo_press		= 90,			// as does leaning on the key
	},
	{	// 3
		// the one preset that runs all three oscillators: a pair of saws
		// detuned by 4/64 of a semitone - about six cents, which beats
		// slowly rather than sounding out of tune - over a sub an octave
		// down.  the levels total 127, so the mix reaches full scale
		// without saturating
		.dco = {
			{ .wave = 2, .level = 45 },
			{ .wave = 2, .level = 45, .fine = 4 },
			{ .wave = 2, .level = 37, .coarse = -12 },
		},

		.dca_env_level	= 127,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,

		// the LFO's other two destinations, on a preset of their own so
		// they are not competing with vibrato to be heard.  a sine
		// rather than the square this used to run, since both of these
		// want to sweep rather than chop
		.lfo_wave		= 0,
		.lfo_dca		= 90,			// tremolo
		.lfo_dcf		= 12,			// and a wah either side of it
		.lfo_freq		= 96,
		.lfo_wheel		= 31,
	},
};
