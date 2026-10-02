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
const fs = require('node:fs');
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
  // Infiverse desktop: the eight existing modules, asserted through a real DOM
  // (BOARD row forge-panels).  Self-skips when jsdom is absent -- see the
  // note at the top of that file; a skip prints a line and is never silent.
  'infiverse_panels.test.js',
  // NOT listed on purpose -- crp_session_flow.test.js needs a live hub URL as
  // argv[2]; tools/crp_session.test.py starts the engine and drives it, and
  // that pair already runs in CTest as crp_session_flow_regression.
];

const toolsDir = path.resolve(__dirname, '..');
const repoRoot = path.resolve(toolsDir, '..');

// wasm_host.test.js defaults to tools/wasm_probe.wasm, but CMake builds the
// probe at <build>/wasm_probe.wasm -- so with the default argument the suite
// skipped on every machine that HAD built it, and the old runner counted that
// skip as a pass.  Handing it the built artifact turns a permanent skip into a
// real test.  The path stays optional: with no build tree the suite skips, and
// the gate now reports that skip as a failure rather than hiding it.
const WASM_PROBE = process.env.INIMERSE_WASM_PROBE
  || ['build', 'build-release', 'build-asan-closure']
    .map((d) => path.join(repoRoot, d, 'wasm_probe.wasm'))
    .find((p) => fs.existsSync(p));
const SUITE_ARGS = { 'wasm_host.test.js': WASM_PROBE ? [WASM_PROBE] : [] };

let passed = 0;
const failed = [];
const skipped = [];

for (const suite of SUITES) {
  const file = path.join(toolsDir, suite);
  const res = spawnSync(process.execPath, [file, ...(SUITE_ARGS[suite] || [])], {
    cwd: repoRoot,
    encoding: 'utf8',
    timeout: 120000,
  });
  // A suite may decline to run (missing jsdom, unbuilt wasm module).  It exits 0
  // and prints "skipped" so a missing optional toolchain does not read as a
  // regression.  Counting that as a pass, though, is how a green gate ends up
  // having verified nothing -- the same hole stage_ctest closes by counting
  // ***Skipped separately.  This runner did not do it until the panels suite
  // made the problem visible.  Skips are now counted and named.
  const out = `${res.stdout || ''}${res.stderr || ''}`;
  if (res.status === 0 && /\bskipped\b/.test(out)) {
    skipped.push(suite);
    console.log(`  SKIP ${suite}`);
    console.log(`       ${out.trim().split('\n')[0]}`);
  } else if (res.status === 0) {
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
if (skipped.length) {
  console.log(`skipped (NOT passes): ${skipped.join(', ')}`);
}
if (failed.length) {
  console.log(`failed: ${failed.join(', ')}`);
  process.exit(1);
}
