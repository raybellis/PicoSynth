#!/usr/bin/env node

const fs = require('fs');
const fh = fs.openSync('src/powers.c', 'w');
const waves = [];

const out = (...args) => fs.writeSync(fh, ...args);

function generate(name, fn)
{
	let data = Array(16384).fill(0).map((e, i) => i).map(fn);
	out(`const int16_t __in_flash() ${name}[] = {\n`);
	let n = data.length;
	for (let i = 0; i < n; i += 8) {
		out("\t");
		for (let j = 0; (j < 8) && (i + j < n); ++j) {
			let hex = (data[i + j] & 0xffff).toString(16).padStart(4, '0');
			out(`0x${hex},`);
		}
		out("\n");
	}
	out(`};\n\n`);
	waves.push(name);
}

out(`#include <stdint.h>\n#include <pico/platform.h>\n\n`);

generate("powers", i => Math.round(32768 * Math.pow(2.0, (i - 8192) / 8192)));

fs.closeSync(fh);
