#!/usr/bin/env node

const fs = require('fs');
const fh = fs.openSync('src/data.c', 'w');

const out = (...args) => fs.writeSync(fh, ...args);

function generate(name, n, type, len, fn)
{
	let mask = Math.pow(2, len * 4) - 1;
	let data = Array(n).fill(0).map((e, i) => i).map(fn);
	out(`const ${type} __in_flash() ${name}[] = {\n`);
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

out(`#include <stdint.h>\n#include <pico/platform.h>\n\n`);

generate("note_table", 128, 'uint32_t', 8, i => 65536 * 440.0 * Math.pow(2.0, (i - 69) / 12));
generate("pan_table", 128, 'uint8_t', 2, i => 127 * Math.sqrt(i / 127.0));
generate("power_table", 16384, 'uint16_t', 4, i => Math.round(32768 * Math.pow(2.0, (i - 8192) / 8192)));

fs.closeSync(fh);
