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

// 128 MIDI notes at 128 steps per semitone
const svf_len     = 128 * 128;

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

generate("power_table", 16384, 'uint16_t', 4,
	i => Math.round(32768 * Math.pow(2.0, (i - 8192) / 8192))
);

// the state variable filter's cutoff coefficient is 2*sin(pi*Fc/Fs),
// which spans 0 ..< 2 and is therefore stored as 2:14 fixed point.
//
// the filter is only stable while Fc <= Fs/6 (where the coefficient
// reaches 1.0), so the top of the range is clamped there rather than
// left to blow up
const svf_max = sample_rate / 6;

generate("svf_table", svf_len, 'int16_t', 4, i => {
	i /= 128;
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
// the floor is where the resonance stops improving - by Q=64 rounding
// in the integrators has eaten a fifth of the peak, and past a few
// hundred there is no peak left to speak of
const svf_q_max = 32768;			// Q = 1
const svf_q_min = 512;				// Q = 64
const svf_q_len = 128;				// one entry per 7-bit patch value

// geometric, so that each step is a constant increment in the dB of
// resonant emphasis (36 dB across the range, about 0.28 dB a step)
const q_values = Array(svf_q_len).fill(0).map((e, i) =>
	Math.round(svf_q_max * Math.pow(svf_q_min / svf_q_max, i / (svf_q_len - 1)))
);

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
#define SVF_Q_LEN   ${svf_q_len}
#define SVF_Q_MAX   ${svf_q_max}
`);
fs.closeSync(fh);
