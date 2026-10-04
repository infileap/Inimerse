#!/usr/bin/env node
/*
 * Cross-checks the engine-side UPP implementation against the JS reference.
 *
 * The corpus below is the single source of truth: it is replayed by
 * tools/upp_session.js in this process and, as JSONL, by
 * src/verse/upp_probe.c (`--transcript`).  Both sides emit one canonical
 * record per op and the two transcripts are diffed byte for byte, so a
 * disagreement names the exact op, the exact field and both texts.
 *
 * usage: node tools/upp_engine_crosscheck.js [path-to-verse_upp_probe]
 *
 * Deliberate limits (kept out of the corpus on purpose):
 *   - `log()` / `crash()` build in Date.now(), so their frames are not
 *     reproducible; the corpus applies equivalent literal frames instead.
 *   - a heartbeat with no `timestamp` also falls back to Date.now() and would
 *     differ only in lastHeartbeatAt, so it is covered by the C probe instead.
 *   - json_min.c accepts integers only, so a float anywhere in the corpus would
 *     make the engine reject the whole line; no float appears here.  See
 *     docs/streams/upp-in-engine.md §6.
 */
'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const upp = require('./upp_reference');
const { UppSession } = require('./upp_session');

const M = Object.freeze({
  id: 'demo', name: 'Demo', version: '1.0.0', engine: 'inimerse', entry: 'main.im', abi: 1,
});

const CLIENT_HELLO = upp.hello('client', M, ['shutdown', 'heartbeat', 'heartbeat']);
const HOST_HELLO = upp.hello('host', M, ['heartbeat']);
const NO_PAYLOAD_HELLO = upp.frame('hello');
const RANGE_HELLO = upp.hello('host', { ...M, abiRange: '3..9' }, []);

/* Frames built through the reference so the corpus is, by construction, a
 * sequence the reference itself produces. */
const startFrame = (entry, args) => (args === undefined ? upp.start(entry) : upp.start(entry, args));
const beat = (seq, timestamp) => upp.heartbeat(seq, timestamp);
const crashFrame = (payload) => upp.frame('crash', payload);

const ops = [
  /* ---------------------------------------------------- full lifecycle ---- */
  { op: 'init', role: 'host', manifest: M },
  { op: 'snapshot' },
  { op: 'accept', frame: CLIENT_HELLO },
  { op: 'snapshot' },
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'snapshot' },
  { op: 'apply', frame: beat(1, 1000) },
  { op: 'check', now: 16000, timeoutMs: 15000 },
  { op: 'snapshot' },
  { op: 'check', now: 16001, timeoutMs: 15000 },
  { op: 'snapshot' },
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'recover' },
  { op: 'snapshot' },
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'apply', frame: upp.stop('requested') },
  { op: 'recover' },
  { op: 'reset' },
  { op: 'snapshot' },

  /* ------------------------------------------------ crash and defaults ---- */
  { op: 'apply', frame: crashFrame({ error: null, exitCode: null, timestamp: 2000 }) },
  { op: 'snapshot' },
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'apply', frame: crashFrame({ error: '', timestamp: 2000 }) },
  { op: 'apply', frame: crashFrame({ error: 'real', exitCode: 0, timestamp: 2000 }) },
  { op: 'snapshot' },
  { op: 'recover' },
  { op: 'apply', frame: upp.frame('mystery', { any: 'thing' }) },
  { op: 'snapshot' },

  /* --------------------------------------------- heartbeat ordering ------- */
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'apply', frame: beat(1, 2000) },
  { op: 'apply', frame: beat(1, 2000) },
  { op: 'apply', frame: beat(0, 2000) },
  { op: 'apply', frame: beat(2, 1500) },
  { op: 'apply', frame: beat(2, 2000) },
  { op: 'apply', frame: upp.frame('heartbeat', { timestamp: 2000 }) },
  { op: 'apply', frame: upp.frame('heartbeat', { seq: '3', timestamp: 2000 }) },
  { op: 'apply', frame: upp.frame('heartbeat', { seq: 9007199254740992, timestamp: 2000 }) },
  { op: 'apply', frame: upp.frame('heartbeat', { seq: null, timestamp: null }) },
  { op: 'snapshot' },

  /* -------------------------------------------- malformed apply input ---- */
  { op: 'apply', frame: null },
  { op: 'apply', frame: 'not a frame' },
  { op: 'apply', frame: { upp: 1 } },
  { op: 'apply', frame: { upp: 1, type: 5 } },
  { op: 'apply', frame: { upp: 1, type: 'heartbeat' } },
  { op: 'snapshot' },

  /* --------------------------------------------- stop / recover rules ---- */
  { op: 'apply', frame: upp.stop('requested') },
  { op: 'apply', frame: upp.stop('again') },
  { op: 'snapshot' },
  { op: 'recover' },
  { op: 'recover' },
  { op: 'snapshot' },
  { op: 'apply', frame: upp.frame('start', {}) },
  { op: 'apply', frame: crashFrame({ error: 'x', timestamp: 1 }) },
  { op: 'recover' },
  { op: 'apply', frame: upp.stop() },
  { op: 'apply', frame: startFrame('main.im', ['a', 'b']) },
  { op: 'reset' },
  { op: 'snapshot' },

  /* ------------------------------------------- ABI negotiation (verse) -- */
  { op: 'init', role: 'verse', manifest: { ...M, abiRange: '1..4' } },
  { op: 'accept', frame: RANGE_HELLO },
  { op: 'snapshot' },
  { op: 'accept', frame: upp.hello('verse', M) },
  { op: 'snapshot' },
  { op: 'apply', frame: startFrame('main.im') },
  { op: 'recover' },
  { op: 'reset' },
  { op: 'accept', frame: NO_PAYLOAD_HELLO },
  { op: 'snapshot' },
  { op: 'init', role: 'host', manifest: M },
  { op: 'accept', frame: upp.hello('client', { ...M, abiRange: '5' }) },
  { op: 'snapshot' },
  { op: 'reset' },

  /* ------------------------------------------------- hello() builders ---- */
  { op: 'hello', role: 'client', manifest: M, capabilities: ['z', 'a', 'z', 7, null, 'a'] },
  { op: 'hello', role: 'host', manifest: M },
  { op: 'hello', role: 'verse', manifest: { ...M, abi: 2, abiRange: '2..4', capabilities: ['b', 'a'] }, capabilities: [] },
  { op: 'hello', role: 'nobody', manifest: M, capabilities: [] },
  { op: 'hello', manifest: M, capabilities: [] },
  { op: 'hello', role: 'client', capabilities: [] },
  { op: 'hello', role: 'client', manifest: {}, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, id: '-bad' }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, version: '1.0' }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, name: '  ' }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, files: [] }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, capabilities: [1] }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, abi: 0 }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: { ...M, abiRange: '1..' }, capabilities: [] },
  { op: 'hello', role: 'client', manifest: [], capabilities: [] },

  /* ------------------------------------------- start/stop/heartbeat ------ */
  { op: 'start', entry: 'main.im' },
  { op: 'start', entry: 'main.im', args: ['a', 'b'] },
  { op: 'start', entry: 'main.im', args: [] },
  { op: 'start', entry: 'main.im', args: null },
  { op: 'start', entry: 'main.im', args: 'nope' },
  { op: 'start', entry: 'main.im', args: ['a', 1] },
  { op: 'start', entry: '  ' },
  { op: 'start', entry: null },
  { op: 'start' },
  { op: 'stop' },
  { op: 'stop', reason: null },
  { op: 'stop', reason: 'requested' },
  { op: 'stop', reason: '   ' },
  { op: 'stop', reason: '' },
  { op: 'heartbeat', seq: 0, timestamp: 0 },
  { op: 'heartbeat', seq: 3, timestamp: 1000 },
  { op: 'heartbeat', seq: -1, timestamp: 1000 },
  { op: 'heartbeat', seq: 9007199254740992, timestamp: 1000 },
  { op: 'incompatible', required: 2, actual: 1 },
  { op: 'incompatible', required: 2, actual: 2 },
  { op: 'incompatible', required: '2', actual: 1 },
  { op: 'incompatible', required: 2, actual: '1' },
  { op: 'incompatible', required: 2 },
  { op: 'incompatible', required: 2, actual: null },
];

/* ------------------------------------------------------------ JS side ---- */

const PROBE_DEFAULT = path.join(__dirname, '..', 'build', 'verse_upp_probe');

/* The exact record emitted by emit_record() in src/verse/upp_probe.c.  Key
 * order is load-bearing: the two transcripts are diffed as text. */
function record(n, op, outcome, session) {
  const snap = session ? session.snapshot() : null;
  return JSON.stringify({
    n,
    op: op.op,
    ok: outcome.ok,
    error: outcome.ok ? null : outcome.error,
    frame: outcome.frame === undefined ? null : outcome.frame,
    result: outcome.result === undefined ? null : outcome.result,
    state: snap ? snap.state : null,
    lastHeartbeat: snap ? snap.lastHeartbeat : null,
    lastHeartbeatAt: snap ? snap.lastHeartbeatAt : null,
    abi: snap ? snap.abi : null,
    sessionError: snap ? snap.error : null,
  });
}

function runJs(corpus) {
  const lines = [];
  let session = null;
  corpus.forEach((op, i) => {
    const outcome = { ok: true, error: '' };
    try {
      switch (op.op) {
        case 'init':
          /* a throwing constructor leaves the previous session bound */
          session = new UppSession(op.role, op.manifest);
          break;
        case 'hello':
          outcome.frame = JSON.stringify(upp.hello(
            op.role, op.manifest, 'capabilities' in op ? op.capabilities : []));
          break;
        case 'start':
          outcome.frame = JSON.stringify(startFrame(op.entry, 'args' in op ? op.args : undefined));
          break;
        case 'stop':
          outcome.frame = JSON.stringify(upp.stop(op.reason));
          break;
        case 'heartbeat':
          outcome.frame = JSON.stringify(upp.heartbeat(op.seq, op.timestamp));
          break;
        case 'incompatible':
          outcome.frame = JSON.stringify('actual' in op
            ? upp.incompatible(op.required, op.actual)
            : upp.incompatible(op.required));
          break;
        case 'accept':
          outcome.frame = JSON.stringify(session.acceptHello(op.frame));
          break;
        case 'apply':
          session.apply(op.frame);
          break;
        case 'recover':
          session.recover();
          break;
        case 'reset':
          session.reset();
          break;
        case 'check':
          outcome.result = session.checkHeartbeat(op.now, op.timeoutMs);
          break;
        case 'snapshot':
          break;
        default:
          throw new Error(`crosscheck driver does not know op ${op.op}`);
      }
    } catch (err) {
      outcome.ok = false;
      outcome.error = err.message;
    }
    lines.push(record(i + 1, op, outcome, session));
  });
  return lines;
}

/* ---------------------------------------------------------- engine side -- */

function runEngine(probe, corpusPath) {
  const res = spawnSync(probe, ['--transcript', corpusPath], {
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
  });
  if (res.error) throw new Error(`could not run ${probe}: ${res.error.message}`);
  if (res.status !== 0) {
    throw new Error(`engine probe exited ${res.status}\n${res.stderr || ''}`.trim());
  }
  if (res.stderr && res.stderr.trim()) {
    process.stderr.write(`engine probe stderr:\n${res.stderr}`);
  }
  return res.stdout.split('\n').map(l => l.replace(/\r$/, '')).filter(l => l.length > 0);
}

/* --------------------------------------------------------------- main ---- */

function main() {
  const probe = process.argv[2] || PROBE_DEFAULT;
  if (!fs.existsSync(probe)) {
    console.error(`upp crosscheck: engine probe not found at ${probe}`);
    console.error('build it first (cmake --build build) or pass its path as the first argument');
    return 1;
  }

  const corpusText = `${ops.map(op => JSON.stringify(op)).join('\n')}\n`;
  const corpusPath = path.join(os.tmpdir(), `upp_corpus_${process.pid}.jsonl`);
  fs.writeFileSync(corpusPath, corpusText);

  let engineLines;
  try {
    engineLines = runEngine(probe, corpusPath);
  } finally {
    /* keep the corpus on failure so the diff can be reproduced by hand */
    if (engineLines && engineLines.length === ops.length) fs.unlinkSync(corpusPath);
  }

  const jsLines = runJs(ops);
  let mismatches = 0;
  for (let i = 0; i < Math.max(jsLines.length, engineLines.length); i++) {
    const js = jsLines[i];
    const engine = engineLines[i];
    if (js === engine) continue;
    mismatches++;
    if (mismatches > 5) continue;
    console.error(`\nMISMATCH at op ${i + 1} (${ops[i] ? ops[i].op : '?'})`);
    console.error(`  corpus: ${corpusText.split('\n')[i]}`);
    console.error(`  engine: ${engine === undefined ? '<missing>' : engine}`);
    console.error(`  js    : ${js === undefined ? '<missing>' : js}`);
  }

  if (mismatches) {
    console.error(`\nupp crosscheck: ${mismatches}/${ops.length} records differ`);
    console.error(`corpus kept at ${corpusPath}`);
    return 1;
  }
  console.log(`upp crosscheck: ok (${ops.length} ops, engine and reference agree)`);
  return 0;
}

if (require.main === module) process.exit(main());
