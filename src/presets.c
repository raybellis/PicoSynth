#include "patch.h"

Patch presets[] = {
	{	// 0
		.dco_wave		= 0,

		.dca_env_level	= 127,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

		.lfo_freq		= 64,
		.lfo_depth		= 20,
	},
	{	// 1
		.dco_wave		= 1,

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

		.lfo_freq		= 64,
		.lfo_depth		= 127,
	},
	{	// 2
		.dco_wave		= 2,

		.dca_env_level	= 100,
		.dca_env_a		= 30,
		.dca_env_d		= 20,
		.dca_env_s		= 80,
		.dca_env_r		= 20,

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

		.lfo_wave		= 1,
		.lfo_freq		= 96,
		.lfo_depth		= 31,
	},
};
