#!/usr/bin/env node
'use strict';
const { spawnSync } = require('node:child_process');
const tests = ['upp_reference.test.js', 'crp_reference.test.js', 'crp_relay.test.js', 'crp_client.test.js', 'vverse_validate.test.js'];
for (const test of tests) {
  const r = spawnSync(process.execPath, [require('node:path').join(__dirname, test)], { encoding: 'utf8' });
  process.stdout.write(r.stdout || ''); process.stderr.write(r.stderr || '');
  if (r.status !== 0) { console.error(`FAILED: ${test}`); process.exit(r.status || 1); }
}
console.log(`Regression suite passed (${tests.length} tests)`);
