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

const out = (...args) => fs.writeSync(fh, ...args);

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
}

let fh = fs.openSync('src/data.c', 'w');

out(`#include <stdint.h>
#include <pico.h>

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

fs.closeSync(fh);

fh = fs.openSync('src/settings.h', 'w');
out(`#pragma once

#define SAMPLE_RATE ${sample_rate}
#define BUFFER_SIZE ${buffer_size}

#define WAVE_SHIFT  ${wave_shift}
#define WAVE_LEN    ${wave_len}
#define WAVE_MAX    0x${wave_max.toString(16)}
`);
fs.closeSync(fh);
