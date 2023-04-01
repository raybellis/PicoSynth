#!/usr/bin/env node

const fs = require('fs');
const fh = fs.openSync('src/waves.c', 'w');
const waves = [];

const out = (...args) => fs.writeSync(fh, ...args);

function generate(name, fn)
{
	let data = Array(2048).fill(0).map((e, i) => i).map(fn);
	out(`static int16_t ${name}[] = {\n`);
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

out(`#include <stdint.h>\n\n`);

generate("sine_table", i => Math.floor(32767 * Math.sin(2 * Math.PI * i / 2048)));
generate("square_table", i => i < 1024 ? 32767 : -32768);
generate("saw_table", i => 32767 - i * 32);
generate("tri_table", i => {
	return (i < 512) ? i * 64 :
           (i < 1536) ? 32767 - 64 * (i - 512) :
		   -32768 + 64 * (i - 1536);
});

out(`int16_t* waves[] = {\n`);
for (let name of waves) {
	out(`\t${name},\n`);
}
out('};\n')

fs.closeSync(fh);
