// The lookup tables, computed at startup.
//
// These used to be generated into C by utils/data.js at build time.
// Computing them here instead costs about 22 ms at boot and roughly
// 1.7 KB of code, and saves the whole table image in flash - plus the
// NodeJS build dependency, and the custom-command machinery that went
// with it.
//
// The formulas are the same ones the generator used.  Verified
// against its output: pan, svf, q and cutoff come out bit identical,
// power and scale differ by 1 LSB on a handful of entries, and
// note_table by 6 parts in 38 million - float against the generator's
// double, worth 0.05 cents at worst.

#include <math.h>
#include <stdint.h>

#include "data.h"

// the generator used to refuse to emit a q_table that left the range
// the filter is stable over.  the endpoints are now exact by
// construction - the curve pins them - so what is left to check is
// that the constants themselves are sane, which is a compile-time
// question rather than a generated-data one
_Static_assert(SVF_Q_MIN >= 1 && SVF_Q_MIN < SVF_Q_MAX,
	"q_table would leave the range the filter is stable over");
_Static_assert(SVF_Q_MID > 1.0f && SVF_Q_MID < (float)SVF_Q_MAX / SVF_Q_MIN,
	"the q curve midpoint must lie between the endpoints");
_Static_assert(SVF_NOTE_MID > 0.0f && SVF_NOTE_MID < SVF_NOTE_MAX,
	"the cutoff curve midpoint must lie between the endpoints");
_Static_assert(POWER_SHIFT >= 0, "power_table cannot be finer than 1/8192 octave");
_Static_assert(SVF_NOTE_MAX * SVF_STEPS < SVF_LEN,
	"cutoff_table would index past the end of svf_table");

uint32_t	note_table[128];
uint8_t		pan_table[128];
uint16_t	power_table[POWER_LEN];
int16_t		svf_table[SVF_LEN];
uint16_t	q_table[SVF_Q_LEN];
uint16_t	scale_table[SVF_Q_LEN];
uint16_t	cutoff_table[SVF_Q_LEN];

static inline float note_hz(float note)
{
	return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void tables_init(void)
{
	// where a preset of 64 with its controller centred lands, which is
	// 64/127 of the way along and not quite one half
	const float u_mid = 64.0f / (SVF_Q_LEN - 1);

	for (int i = 0; i < 128; ++i) {
		note_table[i] = (uint32_t)(65536.0f * WAVE_LEN * note_hz(i) / SAMPLE_RATE);
	}

	for (int i = 0; i < 128; ++i) {
		pan_table[i] = (uint8_t)(127.0f * sqrtf(i / 127.0f));
	}

	for (int i = 0; i < POWER_LEN; ++i) {
		power_table[i] = (uint16_t)lrintf(32768.0f * powf(2.0f, (float)i / POWER_LEN));
	}

	// the filter is only stable while Fc <= Fs/6, where the
	// coefficient reaches 1.0, so the top of the range clamps there
	// rather than blowing up
	const float fc_max = SAMPLE_RATE / 6.0f;

	for (int i = 0; i < SVF_LEN; ++i) {
		float fc = note_hz((float)i / SVF_STEPS);
		if (fc > fc_max) {
			fc = fc_max;
		}
		svf_table[i] = (int16_t)lrintf(32768.0f * sinf((float)M_PI * fc / SAMPLE_RATE));
	}

	// a plain geometric sweep would be even in dB, the right feel for
	// a control, but it cannot also put the midpoint as low as it
	// wants to be.  raising the curve to a power pins both endpoints
	// and moves the middle onto SVF_Q_MID
	const float q_span = (float)SVF_Q_MAX / SVF_Q_MIN;
	const float q_exp  = logf(logf(SVF_Q_MID) / logf(q_span)) / logf(u_mid);

	for (int i = 0; i < SVF_Q_LEN; ++i) {
		float u = (float)i / (SVF_Q_LEN - 1);
		q_table[i] = (uint16_t)lrintf(SVF_Q_MAX *
			powf((float)SVF_Q_MIN / SVF_Q_MAX, powf(u, q_exp)));
	}

	// the geometric mean of q and unity: the resonant peak sits a
	// factor of Q above the passband whatever happens, and this
	// chooses which of the two is held near unity.  splitting it
	// evenly halves the bass loss in exchange for the peak rising
	for (int i = 0; i < SVF_Q_LEN; ++i) {
		scale_table[i] = (uint16_t)lrintf(sqrtf((float)SVF_Q_MAX * q_table[i]));
	}

	// same shape again, for the cutoff
	const float n_exp = logf(SVF_NOTE_MID / SVF_NOTE_MAX) / logf(u_mid);

	for (int i = 0; i < SVF_Q_LEN; ++i) {
		float note = SVF_NOTE_MAX * powf((float)i / (SVF_Q_LEN - 1), n_exp);
		int32_t idx = lrintf(note * SVF_STEPS);
		cutoff_table[i] = (idx < SVF_LEN) ? idx : (SVF_LEN - 1);
	}
}
