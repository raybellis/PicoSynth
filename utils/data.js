#!/usr/bin/env node

const fs = require('fs');
const args = process.argv.slice(2);

if (args.length != 3) {
  process.exit(1);
}

const sample_rate = +args[0];
const wave_shift  = +args[1];
const buffer_size = +args[2];

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

let fh = fs.openSync('src/data.c', 'w');

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

fs.closeSync(fh);

// declare the tables in a header that data.c itself includes, so that
// the definitions above and every user of them are checked against
// one another instead of against hand-written externs
fh = fs.openSync('src/data.h', 'w');
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

fh = fs.openSync('src/settings.h', 'w');
out(`#pragma once

#define SAMPLE_RATE ${sample_rate}
#define BUFFER_SIZE ${buffer_size}

#define WAVE_SHIFT  ${wave_shift}
#define WAVE_LEN    ${wave_len}
#define WAVE_MAX    0x${wave_max.toString(16)}

#define SVF_LEN     ${svf_len}
`);
fs.closeSync(fh);
