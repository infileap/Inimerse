'use strict';
const fs = require('node:fs');
const file = process.argv[2];
if (!file) throw new Error('wasm file required');
const b = fs.readFileSync(file);
if (b.length < 8 || b.readUInt32LE(0) !== 0x6d736100 || b.readUInt32LE(4) !== 1) throw new Error('invalid WebAssembly module');
for (const name of ['inimerse_probe', 'inimerse_abi_version', 'inimerse_capabilities']) if (!b.includes(Buffer.from(name))) throw new Error(`missing export: ${name}`);
console.log(`wasm probe valid (${b.length} bytes)`);
