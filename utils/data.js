#!/usr/bin/env node

const fs = require('fs');
const args = process.argv.slice(2);

if (args.length != 4) {
  process.exit(1);
}

const dir         = args[0];
const sample_rate = +args[1];
const wave_shift  = +args[2];
const buffer_size = +args[3];

fs.mkdirSync(dir, { recursive: true });

const wave_len    = (1 << wave_shift);
const wave_max    = 0x10000 * wave_len;

// 128 MIDI notes, at svf_steps steps per semitone.
//
// only 128 entries of this are reachable today, because the only
// thing that indexes it is cutoff_table, which has one entry per
// value of a 7-bit parameter.  the sub-semitone resolution is there
// for a filter envelope to sweep through, which does not exist yet.
//
// it used to be 128 steps per semitone, which cost 32 KB to hold a
// quarter of a cent of precision.  16 steps is 4 KB and lands within
// 3.1 cents worst case - far below anything audible as a cutoff
// error, and finer than the motion of a sweep between buffers, since
// the coefficients only change at 172 Hz
const svf_steps   = 16;
const svf_len     = 128 * svf_steps;

const out = (...args) => fs.writeSync(fh, ...args);

// every table generated below, so that data.h can declare them
const tables = [];

function generate(name, n, type, len, fn)
{
	let mask = Math.pow(2, len * 4) - 1;
	let data = Array(n).fill(0).map((e, i) => i).map(fn);
	out(`const ${type} ${name}[] = {\n`);
	for (let i = 0; i < n; i += 8) {
		out("\t");
		for (let j = 0; (j < 8) && (i + j < n); ++j) {
			let hex = (data[i + j] & mask).toString(16).padStart(len, '0');
			out(`0x${hex},`);
		}
		out("\n");
	}
	out(`};\n\n`);
	tables.push({ name, n, type });
}

let fh = fs.openSync(`${dir}/data.c`, 'w');

out(`#include "data.h"

`);

generate("note_table", 128, 'uint32_t', 8, i=> {
	const f = 440.0 * Math.pow(2.0, (i - 69) / 12);
	return 0x10000 * wave_len * f / sample_rate;
});

generate("pan_table", 128, 'uint8_t', 2,
	i => 127 * Math.sqrt(i / 127.0)
);

// 2^(x/8192) as a 1:15 multiplier, for pitch modulation.  x is a
// 14-bit signed offset, so the range wanted is one octave either side
// of unity - but only the upper octave is stored.
//
// the lower one is the very same entries read as 1:16 instead of
// 1:15, which is to say with the shift at the call site omitted: for
// x < 0, table[x + 8192] already holds 65536 * 2^(x/8192).  that is
// exact rather than an approximation, so folding costs one branch and
// halves the table.
//
// resolution then costs 2 bytes an entry.  8192 entries an octave -
// what this had - is 0.15 cents a step, roughly seven times finer
// than anyone can hear on a sustained tone, and far finer than the
// modulation itself moves between buffers at 172 Hz.  4096 is 0.29
// cents and 8 KB
const power_bits  = 12;					// entries per octave, log2
const power_len   = 1 << power_bits;
const power_shift = 13 - power_bits;	// x units per entry, log2

generate("power_table", power_len, 'uint16_t', 4,
	i => Math.round(32768 * Math.pow(2.0, i / power_len))
);

// the state variable filter's cutoff coefficient is 2*sin(pi*Fc/Fs),
// which spans 0 ..< 2 and is therefore stored as 2:14 fixed point.
//
// the filter is only stable while Fc <= Fs/6 (where the coefficient
// reaches 1.0), so the top of the range is clamped there rather than
// left to blow up
const svf_max = sample_rate / 6;

generate("svf_table", svf_len, 'int16_t', 4, i => {
	i /= svf_steps;
	const cutoff = Math.min(440.0 * Math.pow(2.0, (i - 69) / 12), svf_max);
	return Math.round(16384 * 2 * Math.sin(Math.PI * cutoff / sample_rate));
});

// the damping factor is 1:15 fixed point holding 1/Q, and it scales
// the filter's input as well as its feedback, so the passband sits at
// 1/Q while the resonant peak stays at unity.
//
// that makes 1.0 the ceiling: above it the filter has gain at DC and
// clips a full scale voice.  it is also well inside the stability
// bound for this topology, q < 2/f - f/2, which bottoms out at 1.5
// where svf_table clamps at Fs/6.
//
// the floor used to be Q = 64, on the grounds that that is where
// rounding in the integrators starts eating the peak.  it is now 16:
// reaching the very top was never needed, and giving it up buys a far
// more even control, for reasons the midpoint note below explains
const svf_q_max = 32768;			// Q = 1
const svf_q_min = 2048;				// Q = 16
const svf_q_len = 128;				// one entry per 7-bit patch value

// the sound controllers offset the patch around centre, so the middle
// of this table is what every patch sounds like before anyone touches
// a controller.  that makes the midpoint the number to tune, and it
// was tuned by ear: Q = 1.28, about 2 dB of emphasis over the
// passband.
//
// a plain geometric sweep between the endpoints would be even in dB,
// which is the right feel for a control, but it cannot also put the
// midpoint that low - the two constraints fight, and the midpoint
// wins.  so the curve is raised to a power that pins both ends and
// moves the middle onto svf_q_mid.
//
// the bottom half is necessarily compressed, because the chosen
// default sits close to the Q = 1 floor and there is simply nothing
// between them.  going up from the default is what matters, and that
// runs 1.28 - 2.8 - 5.9 - 16 across the upper half.
const svf_q_mid = 1.28;					// Q at the middle of the table

// index 64 is where a preset of 64 with the controller centred lands,
// which is 64/127 of the way along and not quite one half
const svf_q_u_mid = 64 / (svf_q_len - 1);

const svf_q_exp =
	Math.log(Math.log(svf_q_mid) / Math.log(svf_q_max / svf_q_min)) /
	Math.log(svf_q_u_mid);

const q_values = Array(svf_q_len).fill(0).map((e, i) => {
	const u = i / (svf_q_len - 1);
	return Math.round(svf_q_max *
		Math.pow(svf_q_min / svf_q_max, Math.pow(u, svf_q_exp)));
});

// nothing in the table may exceed 1.0, both because the filter would
// have gain at DC above that and because set_q no longer range checks
// the value itself, only the index
if (q_values.some(q => q > svf_q_max || q < 1)) {
	throw new Error("q_table out of range");
}

generate("q_table", svf_q_len, 'uint16_t', 4, i => q_values[i]);

// the filter scales its input by this alongside the damping, which is
// what places the response in absolute terms: the resonant peak sits
// a factor of Q above the passband whatever happens, and all this
// chooses is which of the two is held at unity.
//
// scaling by q itself would pin the peak at unity and let the
// passband fall away as 1/Q - 36 dB by the top of the range, which is
// far more bass loss than is wanted.  the geometric mean of q and 1.0
// splits the difference evenly in dB, halving the droop to 18 dB in
// exchange for the peak rising by the same amount.
//
// note that this stays at or below 1.0 for every entry, since q does
// too, so it costs nothing in the headroom that keeps fmul_f(high, f)
// from overflowing.  a table that boosted above unity would not be
// safe without fixing that first
generate("scale_table", svf_q_len, 'uint16_t', 4, i =>
	Math.round(Math.sqrt(svf_q_max * q_values[i]))
);

// the cutoff parameter used to scale straight to a MIDI note, one
// semitone per step, which put the midpoint - and so every preset's
// default - at note 64, around 330 Hz.  by ear it wants to be note 90,
// about 1480 Hz, so it gets a curve of its own, indexed by the same
// 7-bit parameter as the two tables above.
//
// the top is note 118 rather than 127 because svf_table saturates at
// Fs/6, which is note 117.8.  anything above that is the same filter
// setting, so mapping to it would only waste control travel.
//
// the bottom stays at note 0, so the filter can still be closed right
// down.  that costs resolution at the low end - the first few steps
// move in three semitone jumps - but only across territory that is
// muffled to begin with, and it buys sub-semitone steps up where the
// sweeping actually happens
const svf_note_max = 118;				// where svf_table stops changing
const svf_note_mid = 90;				// note at the middle of the table

const svf_note_exp =
	Math.log(svf_note_mid / svf_note_max) / Math.log(svf_q_u_mid);

generate("cutoff_table", svf_q_len, 'uint16_t', 4, i => {
	const note = svf_note_max * Math.pow(i / (svf_q_len - 1), svf_note_exp);
	return Math.min(svf_len - 1, Math.round(note * svf_steps));
});

fs.closeSync(fh);

// declare the tables in a header that data.c itself includes, so that
// the definitions above and every user of them are checked against
// one another instead of against hand-written externs
fh = fs.openSync(`${dir}/data.h`, 'w');
out(`#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

`);

for (const t of tables) {
	out(`extern const ${t.type} ${t.name}[${t.n}];\n`);
}

out(`
#ifdef __cplusplus
};
#endif
`);
fs.closeSync(fh);

fh = fs.openSync(`${dir}/settings.h`, 'w');
out(`#pragma once

#define SAMPLE_RATE ${sample_rate}
#define BUFFER_SIZE ${buffer_size}

#define WAVE_SHIFT  ${wave_shift}
#define WAVE_LEN    ${wave_len}
#define WAVE_MAX    0x${wave_max.toString(16)}

#define SVF_LEN     ${svf_len}
#define SVF_STEPS   ${svf_steps}

#define POWER_LEN   ${power_len}
#define POWER_SHIFT ${power_shift}
#define SVF_Q_LEN   ${svf_q_len}
#define SVF_Q_MAX   ${svf_q_max}
`);
fs.closeSync(fh);
