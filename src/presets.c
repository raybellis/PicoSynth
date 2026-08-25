#include "patch.h"

Patch presets[] = {
	{	// 0
		.dco_wave		= 2,

		.dca_env_level	= 127,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,
		.dcf_env_level	= 64,			// no filter envelope

		.lfo_freq		= 64,
		.lfo_depth		= 20,
	},
	{	// 1
		.dco_wave		= 3,

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

		// the one preset that uses the filter envelope, so there is
		// something to hear it on.  opens 32 semitones above the patch
		// cutoff and settles about 10 above it.
		//
		// the decay rate is per buffer, so 6 ms a step: this one falls
		// 128 a step over the 22527 between peak and sustain, which is
		// 176 steps, a little over a second
		.dcf_env_level	= 96,
		.dcf_env_a		= 40,
		.dcf_env_d		= 4,
		.dcf_env_s		= 40,
		.dcf_env_r		= 20,

		.lfo_freq		= 92,
		.lfo_depth		= 20,
	},
	{	// 2
		.dco_wave		= 2,

		.dca_env_level	= 100,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,
		.dcf_env_level	= 64,			// no filter envelope

		.lfo_freq		= 64,
		.lfo_depth		= 127,
	},
	{	// 3
		.dco_wave		= 3,

		.dca_env_level	= 127,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.dcf_freq		= 64,
		.dcf_reso		= 64,
		.dcf_env_level	= 64,			// no filter envelope

		.lfo_wave		= 1,
		.lfo_freq		= 96,
		.lfo_depth		= 31,
	},
};
