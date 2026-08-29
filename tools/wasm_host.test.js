'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { InimerseWasmHost } = require('./wasm_host');

(async () => {
  const file = process.argv[2] || path.join(__dirname, 'wasm_probe.wasm');
  if (!fs.existsSync(file)) { console.log('wasm host test skipped (build wasm first)'); return; }
  const host = await new InimerseWasmHost().load(fs.readFileSync(file));
  assert.equal(host.probe(), 0x0300);
  assert.equal(host.capabilities(), 0);
  await assert.rejects(() => new InimerseWasmHost().load(new Uint8Array([0, 1, 2])), /wasm|magic|compile/i);
  console.log('wasm host tests: ok');
})();
