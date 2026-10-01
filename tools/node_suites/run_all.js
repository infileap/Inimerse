'use strict';
/*
 * Run every JS protocol suite under tools/ that CTest does not cover.
 *
 * These suites existed and passed, but nothing invoked them: `ctest` registers
 * only the Python suites, and tools/gate.sh had no JS stage.  A suite nobody
 * runs cannot fail, so a regression in UPP/CRP/.vverse reference code would
 * have gone unnoticed until someone ran it by hand.
 *
 * Each suite is a standalone script that prints "... : ok" and exits non-zero
 * on the first failed assertion, so this runner spawns them one by one and
 * reports a tally.  Adding a suite means adding a file to the list below --
 * deliberately explicit, so a new suite cannot be silently omitted.
 */
const { spawnSync } = require('node:child_process');
const path = require('node:path');

const SUITES = [
  'upp_reference.test.js',
  'upp_session.test.js',
  'crp_reference.test.js',
  'crp_relay.test.js',
  'crp_client.test.js',
  'crp_ws_client.test.js',
  'vverse_pack.test.js',
  'vverse_validate.test.js',
  // The eight entries above are the UPP/CRP/.vverse protocol suites.  These
  // three were missed by the first version of this runner, which listed only
  // suites named after a protocol acronym -- but they are just as runnable
  // standalone, and just as unrun by CTest.  wasm_host self-skips when the
  // wasm module has not been built yet, so it is safe to list unconditionally.
  'say_reference.test.js',
  'hub_client.test.js',
  'wasm_host.test.js',
  // NOT listed on purpose -- crp_session_flow.test.js needs a live hub URL as
  // argv[2]; tools/crp_session.test.py starts the engine and drives it, and
  // that pair already runs in CTest as crp_session_flow_regression.
];

const toolsDir = path.resolve(__dirname, '..');
let passed = 0;
const failed = [];

for (const suite of SUITES) {
  const file = path.join(toolsDir, suite);
  const res = spawnSync(process.execPath, [file], {
    cwd: path.resolve(toolsDir, '..'),
    encoding: 'utf8',
    timeout: 120000,
  });
  if (res.status === 0) {
    passed += 1;
    console.log(`  ok   ${suite}`);
  } else {
    failed.push(suite);
    console.log(`  FAIL ${suite}`);
    if (res.stdout) console.log(res.stdout.trimEnd());
    if (res.stderr) console.log(res.stderr.trimEnd());
  }
}

console.log(`\nnode protocol suites: ${passed}/${SUITES.length} passed`);
if (failed.length) {
  console.log(`failed: ${failed.join(', ')}`);
  process.exit(1);
}
