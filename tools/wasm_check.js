'use strict';
const { spawnSync } = require('node:child_process');
function probe(command, args) { const r = spawnSync(command, args, { encoding: 'utf8' }); return r.status === 0 ? (r.stdout || r.stderr).split(/\r?\n/)[0] : null; }
const emcc = probe('emcc', ['--version']); const clang = probe('clang', ['--target=wasm32-wasi', '--version']);
if (!emcc && !clang) { console.error('WASM toolchain not found. Install emscripten or wasi-sdk/clang.'); process.exit(2); }
console.log(JSON.stringify({ emscripten: emcc, wasiClang: clang, target: 'wasm32-wasi', status: 'toolchain-ready' }));
