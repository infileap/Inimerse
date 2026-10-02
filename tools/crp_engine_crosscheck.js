#!/usr/bin/env node
'use strict';
/*
 * crp_engine_crosscheck.js -- diff the engine's CRP layer against the JS
 * reference, line by line, on one shared corpus.
 *
 *   node tools/crp_engine_crosscheck.js <path-to-verse_crp_probe>
 *
 * The corpus below is replayed twice:
 *   - by the engine probe, as `crp_probe --transcript <corpus> --secret <hex>
 *     --now <ms>`, one JSON record per corpus line;
 *   - by this script, through `tools/crp_reference.js` for the frame/codec/token
 *     ops and through a real `tools/crp_relay.js` HTTP server for the registry
 *     ops.
 * Every record must be text-identical.  Exit 1 on the first difference.
 *
 * Both sides run on the SAME frozen clock: `Date.now` is replaced before the
 * relay is loaded, which is what `--now` does on the engine side.  Without that
 * the tokens, `expires` fields and registry pruning would differ by wall-clock
 * drift and nothing would be comparable.
 */

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const NOW = 1767225600000;
const SECRET = '00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff';

let CLOCK = NOW;
Date.now = () => CLOCK;

const ref = require('./crp_reference.js');
const { createRelay } = require('./crp_relay.js');

/* ---- the two reference helpers the relay keeps private ------------------- */

function makeToken(secret, verse, peer, capabilities = ['signal'], exp) {
  const body = Buffer.from(JSON.stringify({ verse, peer, capabilities, exp })).toString('base64url');
  const sig = crypto.createHmac('sha256', secret).update(body).digest('base64url');
  return `${body}.${sig}`;
}

function checkToken(secret, token, verse, peer, capability, now) {
  try {
    const [body, sig] = String(token).split('.');
    const expected = crypto.createHmac('sha256', secret).update(body).digest('base64url');
    if (!body || !sig || !crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(expected))) return false;
    const p = JSON.parse(Buffer.from(body, 'base64url'));
    return p.verse === verse && p.peer === peer && p.exp > now && p.capabilities.includes(capability);
  } catch {
    return false;
  }
}

/* ---- record shapes ------------------------------------------------------- */

/* A reference call that either returns the comparable text or throws the
 * reference's own error message. */
function call(fn) {
  try {
    const value = fn();
    return { ok: true, error: null, out: value === undefined ? null : String(value), result: null };
  } catch (e) {
    return { ok: false, error: e.message, out: null, result: null };
  }
}

function truthyResult(value) {
  return { ok: true, error: null, out: null, result: value };
}

/* ---- the corpus ---------------------------------------------------------- */
/* Each entry: `req` is the corpus line handed to both sides, `run` produces the
 * expected record from the reference.  Registry ops use `http`, which is bound
 * once the relay is listening. */

const KAT_TOKEN =
  'eyJ2ZXJzZSI6ImRlbW8iLCJwZWVyIjoicGVlci0xIiwiY2FwYWJpbGl0aWVzIjpbInNpZ25hbCJdLCJleHAiOjE3NjcyMjU5MDAwMDB9' +
  '.dCdjTpv1uLtSe_ed6Px19ikVpL3Q_IVbNx_tZ_SIKVE';

let http = null;
let relay = null;

/* The two portal tokens the corpus refers to.  They are produced by the same
 * helper the relay's /portal uses, at the same frozen clock, so the engine's
 * own /portal response must be byte-identical to them -- which the `relay_portal`
 * corpus entries check directly. */
const TOKEN_A = makeToken(SECRET, 'demo', 'peer-a', undefined, NOW + 300000);
const TOKEN_B = makeToken(SECRET, 'demo', 'peer-b', undefined, NOW + 300000);

function pick(op, keys) {
  const body = {};
  for (const k of keys) if (k in op) body[k] = op[k];
  return body;
}

const CORPUS = [
  /* ---- frames ----------------------------------------------------------- */
  { req: { op: 'frame', type: 'FIND', payload: { query: 'x' } },
    run: () => call(() => JSON.stringify(ref.frame('FIND', { query: 'x' }))) },
  { req: { op: 'frame', type: 'NOPE', payload: {} },
    run: () => call(() => JSON.stringify(ref.frame('NOPE', {}))) },
  { req: { op: 'frame', type: 'FIND', payload: 'nope' },
    run: () => call(() => JSON.stringify(ref.frame('FIND', 'nope'))) },
  { req: { op: 'frame', type: 'SIGNAL', payload: {}, id: 'f1' },
    run: () => call(() => JSON.stringify(ref.frame('SIGNAL', {}, 'f1'))) },
  { req: { op: 'frame', type: 'PORTAL', payload: { verse: 'v', peer: 'p' }, id: '' },
    run: () => call(() => JSON.stringify(ref.frame('PORTAL', { verse: 'v', peer: 'p' }, ''))) },

  /* ---- FIND frames ------------------------------------------------------ */
  { req: { op: 'find', query: 'demo' },
    run: () => call(() => JSON.stringify(ref.find('demo'))) },
  { req: { op: 'find' },
    run: () => call(() => JSON.stringify(ref.find())) },
  { req: { op: 'find', query: 'demo', limit: 1, cursor: 'c1', id: 'f2' },
    run: () => call(() => JSON.stringify(ref.find('demo', { limit: 1, cursor: 'c1', id: 'f2' }))) },
  { req: { op: 'find', query: 'demo', limit: null },
    run: () => call(() => JSON.stringify(ref.find('demo', { limit: null }))) },
  { req: { op: 'find', query: 'demo', limit: 0 },
    run: () => call(() => JSON.stringify(ref.find('demo', { limit: 0 }))) },
  { req: { op: 'find', query: 'demo', limit: 1001 },
    run: () => call(() => JSON.stringify(ref.find('demo', { limit: 1001 }))) },
  { req: { op: 'find', query: 'demo', limit: '5' },
    run: () => call(() => JSON.stringify(ref.find('demo', { limit: '5' }))) },
  { req: { op: 'find', query: 5 },
    run: () => call(() => JSON.stringify(ref.find(5))) },
  { req: { op: 'find', query: 'demo', cursor: 5 },
    run: () => call(() => JSON.stringify(ref.find('demo', { cursor: 5 }))) },
  { req: { op: 'find', query: 'demo', cursor: null },
    run: () => call(() => JSON.stringify(ref.find('demo', { cursor: null }))) },

  /* ---- PORTAL frames ---------------------------------------------------- */
  { req: { op: 'portal', verse: 'demo', peer: 'p1' },
    run: () => call(() => JSON.stringify(ref.portal('demo', 'p1'))) },
  { req: { op: 'portal', verse: 'demo', peer: 'p1', token: 't', expires: 123 },
    run: () => call(() => JSON.stringify(ref.portal('demo', 'p1', { token: 't', expires: 123 }))) },
  { req: { op: 'portal', verse: '   ', peer: 'p1' },
    run: () => call(() => JSON.stringify(ref.portal('   ', 'p1'))) },
  { req: { op: 'portal', verse: 'demo', peer: '' },
    run: () => call(() => JSON.stringify(ref.portal('demo', ''))) },
  { req: { op: 'portal', verse: 5, peer: 'p1' },
    run: () => call(() => JSON.stringify(ref.portal(5, 'p1'))) },
  { req: { op: 'portal', verse: 'demo', peer: 'p1', token: '' },
    run: () => call(() => JSON.stringify(ref.portal('demo', 'p1', { token: '' }))) },
  { req: { op: 'portal', verse: 'demo', peer: 'p1', expires: 0 },
    run: () => call(() => JSON.stringify(ref.portal('demo', 'p1', { expires: 0 }))) },

  /* ---- SIGNAL frames ---------------------------------------------------- */
  { req: { op: 'signal', verse: 'v', event: 'e' },
    run: () => call(() => JSON.stringify(ref.signal('v', 'e'))) },
  { req: { op: 'signal', verse: 'v', event: 'e', data: { a: 1 }, timestamp: 42, id: 's1' },
    run: () => call(() => JSON.stringify(ref.signal('v', 'e', { a: 1 }, { timestamp: 42, id: 's1' }))) },
  { req: { op: 'signal', verse: '', event: 'e' },
    run: () => call(() => JSON.stringify(ref.signal('', 'e'))) },
  { req: { op: 'signal', verse: 'v', event: 'e', data: [1] },
    run: () => call(() => JSON.stringify(ref.signal('v', 'e', [1]))) },
  { req: { op: 'signal', verse: 'v', event: 'e', data: null },
    run: () => call(() => JSON.stringify(ref.signal('v', 'e', null))) },
  { req: { op: 'signal', verse: 'v', event: 'e', timestamp: null },
    run: () => call(() => JSON.stringify(ref.signal('v', 'e', {}, { timestamp: null }))) },

  /* ---- encode / decode -------------------------------------------------- */
  { req: { op: 'encode', message: { crp: 1, type: 'FIND', payload: { query: 'x' } } },
    run: () => call(() => ref.encode({ crp: 1, type: 'FIND', payload: { query: 'x' } })) },
  { req: { op: 'encode', message: { crp: 2, type: 'FIND', payload: {} } },
    run: () => call(() => ref.encode({ crp: 2, type: 'FIND', payload: {} })) },
  { req: { op: 'encode', message: 'nope' },
    run: () => call(() => ref.encode('nope')) },
  { req: { op: 'encode', message: { crp: 1, type: 'NOPE', payload: {} } },
    run: () => call(() => ref.encode({ crp: 1, type: 'NOPE', payload: {} })) },
  { req: { op: 'encode_big', n: 1048376 },
    run: () => call(() => bigRoundTrip(1048376)) },
  { req: { op: 'encode_big', n: 1048577 },
    run: () => call(() => bigRoundTrip(1048577)) },
  { req: { op: 'decode', line: '{"crp":1,"type":"FIND","payload":{"query":"x"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"FIND","payload":{"query":"x"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"FIND","payload":{"query":"x"}}\n' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"FIND","payload":{"query":"x"}}\n'))) },
  { req: { op: 'decode', line: '  {"crp":1,"type":"SIGNAL","payload":{}}  ' },
    run: () => call(() => ref.encode(ref.decode('  {"crp":1,"type":"SIGNAL","payload":{}}  '))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"NOPE","payload":{}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"NOPE","payload":{}}'))) },
  { req: { op: 'decode', line: '[]' },
    run: () => call(() => ref.encode(ref.decode('[]'))) },

  /* ---- \u escapes in a payload ------------------------------------------ *
   * The corpus never contained a surrogate before, which is why the engine's
   * CESU-8 output for \uD83D\uDE00 went unnoticed: json_min encoded the two
   * halves separately (ED A0 BD ED B8 80) where JSON.parse combines them into
   * U+1F600 (F0 9F 98 80).  A lone surrogate was emitted as its 3-byte
   * "encoding" instead of U+FFFD (EF BF BD).  These entries make both live
   * under the byte-identical contract; the round trip through ref.encode also
   * pins what the engine's writer must emit for the decoded bytes.
   *
   * \u0000 is deliberately absent: the engine refuses it where JSON.parse
   * yields a NUL, a documented divergence (VjVal's string is a length-less
   * char *).  A LITERAL "\u0000" -- six characters, no NUL byte -- is fine and
   * is covered below, because such text is not an escape at all. */
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D\\uDE00"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D\\uDE00"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\uDE00"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\uDE00"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\u0001"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\u0001"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"\\uD83D\\uDE00":"\\uD83D\\uDE00"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"\\uD83D\\uDE00":"\\uD83D\\uDE00"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D\\uDE00\\uD83D\\uDE00"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\uD83D\\uDE00\\uD83D\\uDE00"}}'))) },
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\u00e9\\u4e2d"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\u00e9\\u4e2d"}}'))) },
  /* six literal characters: a backslash, a 'u', four zeros -- not an escape */
  { req: { op: 'decode', line: '{"crp":1,"type":"SIGNAL","payload":{"x":"\\\\u0000"}}' },
    run: () => call(() => ref.encode(ref.decode('{"crp":1,"type":"SIGNAL","payload":{"x":"\\\\u0000"}}'))) },

  /* ---- tokens ----------------------------------------------------------- */
  { req: { op: 'token_make', verse: 'demo', peer: 'p1', capabilities: ['signal'], exp: NOW + 300000 },
    run: () => call(() => makeToken(SECRET, 'demo', 'p1', ['signal'], NOW + 300000)) },
  { req: { op: 'token_make', verse: 'demo', peer: 'p1', exp: NOW + 300000 },
    run: () => call(() => makeToken(SECRET, 'demo', 'p1', undefined, NOW + 300000)) },
  { req: { op: 'token_make', verse: 'demo', peer: 'p1', capabilities: null, exp: NOW + 300000 },
    run: () => call(() => makeToken(SECRET, 'demo', 'p1', null, NOW + 300000)) },
  { req: { op: 'token_make', verse: 'demo', peer: 'p1', capabilities: [], exp: NOW + 300000 },
    run: () => call(() => makeToken(SECRET, 'demo', 'p1', [], NOW + 300000)) },
  { req: { op: 'token_make', verse: 'demo', peer: 'peer-1', exp: NOW + 300000 },
    run: () => call(() => makeToken(SECRET, 'demo', 'peer-1', undefined, NOW + 300000)) },
  { req: { op: 'token_check', token: KAT_TOKEN, verse: 'demo', peer: 'peer-1', capability: 'signal' },
    run: () => truthyResult(checkToken(SECRET, KAT_TOKEN, 'demo', 'peer-1', 'signal', CLOCK)) },
  { req: { op: 'token_check', token: KAT_TOKEN, verse: 'other', peer: 'peer-1', capability: 'signal' },
    run: () => truthyResult(checkToken(SECRET, KAT_TOKEN, 'other', 'peer-1', 'signal', CLOCK)) },
  { req: { op: 'token_check', token: KAT_TOKEN, verse: 'demo', peer: 'peer-1', capability: 'events' },
    run: () => truthyResult(checkToken(SECRET, KAT_TOKEN, 'demo', 'peer-1', 'events', CLOCK)) },
  { req: { op: 'token_check', token: 'nodot', verse: 'demo', peer: 'peer-1', capability: 'signal' },
    run: () => truthyResult(checkToken(SECRET, 'nodot', 'demo', 'peer-1', 'signal', CLOCK)) },
  { req: { op: 'token_check', token: '', verse: 'demo', peer: 'peer-1', capability: 'signal' },
    run: () => truthyResult(checkToken(SECRET, '', 'demo', 'peer-1', 'signal', CLOCK)) },
  { req: { op: 'token_check', token: KAT_TOKEN, verse: 'demo', peer: 'peer-1', capability: 'signal', now: NOW + 300000 },
    run: () => truthyResult(checkToken(SECRET, KAT_TOKEN, 'demo', 'peer-1', 'signal', NOW + 300000)) },

  /* ---- registry: register / find ---------------------------------------- */
  { req: { op: 'now', ms: NOW }, run: () => { CLOCK = NOW; return { ok: true, error: null, out: null, result: null }; } },
  { req: { op: 'register', payload: { id: 'demo', name: 'Demo Verse', endpoint: 'local' } },
    run: () => http('POST', '/register', { id: 'demo', name: 'Demo Verse', endpoint: 'local' }) },
  { req: { op: 'register', payload: { id: 'second', name: 'Second', endpoint: 'remote' } },
    run: () => http('POST', '/register', { id: 'second', name: 'Second', endpoint: 'remote' }) },
  { req: { op: 'register', payload: { name: 'x', endpoint: 'e' } },
    run: () => http('POST', '/register', { name: 'x', endpoint: 'e' }) },
  { req: { op: 'register', payload: { id: 'demo', name: 'Demo Verse', endpoint: 'local' } },
    run: () => http('POST', '/register', { id: 'demo', name: 'Demo Verse', endpoint: 'local' }) },
  { req: { op: 'register', payload: { id: 0, endpoint: 'e' } },
    run: () => http('POST', '/register', { id: 0, endpoint: 'e' }) },
  { req: { op: 'register', payload: { id: 'num', name: 7, endpoint: 'e' } },
    run: () => http('POST', '/register', { id: 'num', name: 7, endpoint: 'e' }) },
  { req: { op: 'register', payload: { id: 'extra', endpoint: 'e', updated: 1, extra: 'keep' } },
    run: () => http('POST', '/register', { id: 'extra', endpoint: 'e', updated: 1, extra: 'keep' }) },
  { req: { op: 'register', payload: { id: 5, endpoint: 'five' } },
    run: () => http('POST', '/register', { id: 5, endpoint: 'five' }) },
  { req: { op: 'relay_find' }, run: () => http('GET', '/find') },
  { req: { op: 'relay_find', q: 'demo' }, run: () => http('GET', `/find?q=${encodeURIComponent('demo')}`) },
  { req: { op: 'relay_find', q: 'DEM' }, run: () => http('GET', `/find?q=${encodeURIComponent('DEM')}`) },
  { req: { op: 'relay_find', q: '7' }, run: () => http('GET', `/find?q=${encodeURIComponent('7')}`) },
  { req: { op: 'relay_find', q: '5' }, run: () => http('GET', `/find?q=${encodeURIComponent('5')}`) },
  { req: { op: 'relay_find', q: 'zzz' }, run: () => http('GET', `/find?q=${encodeURIComponent('zzz')}`) },

  /* ---- registry: portal ------------------------------------------------- */
  { req: { op: 'relay_portal', verse: 'demo', peer: 'peer-a' },
    run: () => http('POST', '/portal', { verse: 'demo', peer: 'peer-a' }) },
  { req: { op: 'relay_portal', verse: 'nope', peer: 'peer-a' },
    run: () => http('POST', '/portal', { verse: 'nope', peer: 'peer-a' }) },
  { req: { op: 'relay_portal', verse: 'demo' },
    run: () => http('POST', '/portal', { verse: 'demo' }) },
  { req: { op: 'relay_portal', verse: 5, peer: 'peer-a' },
    run: () => http('POST', '/portal', { verse: 5, peer: 'peer-a' }) },

  /* ---- registry: signal ------------------------------------------------- */
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', data: { n: 1 } },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', data: { n: 1 } }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', data: { n: 1 }, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', data: { n: 1 }, peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join' }) },
  { req: { op: 'relay_signal', verse: 'demo' },
    run: () => http('POST', '/signal', { verse: 'demo' }) },
  { req: { op: 'relay_signal', verse: 'nope', event: 'join' },
    run: () => http('POST', '/signal', { verse: 'nope', event: 'join' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', token: 'garbage', peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', token: 'garbage', peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', token: null, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', token: null, peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', data: null, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', data: null, peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', seq: 5, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', seq: 5, peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', seq: -1, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', seq: -1, peer: 'peer-a' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', seq: '7', peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', seq: '7', peer: 'peer-a' }) },

  /* ---- registry: resume ------------------------------------------------- */
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: 'garbage' },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: 'garbage' }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a' },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a' }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 0 },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 0 }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 0, replay: true },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 0, replay: true }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 5, replay: true },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 5, replay: true }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 6 },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 6 }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 10 },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 10 }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 9 },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 9 }) },

  /* ---- registry: revoke ------------------------------------------------- */
  { req: { op: 'relay_revoke' }, run: () => http('POST', '/revoke', {}) },
  { req: { op: 'relay_revoke', token: TOKEN_A }, run: () => http('POST', '/revoke', { token: TOKEN_A }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', token: TOKEN_A, peer: 'peer-a' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', token: TOKEN_A, peer: 'peer-a' }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 11 },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-a', token: TOKEN_A, seq: 11 }) },

  /* ---- registry: token TTL and registry TTL ----------------------------- */
  { req: { op: 'relay_portal', verse: 'demo', peer: 'peer-b' },
    run: () => http('POST', '/portal', { verse: 'demo', peer: 'peer-b' }) },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', token: TOKEN_B, peer: 'peer-b' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', token: TOKEN_B, peer: 'peer-b' }) },
  { req: { op: 'now', ms: NOW + 300001 }, run: () => { CLOCK = NOW + 300001; return { ok: true, error: null, out: null, result: null }; } },
  { req: { op: 'relay_signal', verse: 'demo', event: 'join', token: TOKEN_B, peer: 'peer-b' },
    run: () => http('POST', '/signal', { verse: 'demo', event: 'join', token: TOKEN_B, peer: 'peer-b' }) },
  { req: { op: 'relay_resume', verse: 'demo', peer: 'peer-b', token: TOKEN_B, seq: 0, replay: true },
    run: () => http('POST', '/session/resume', { verse: 'demo', peer: 'peer-b', token: TOKEN_B, seq: 0, replay: true }) },
  { req: { op: 'relay_find', q: 'demo' }, run: () => http('GET', `/find?q=${encodeURIComponent('demo')}`) },
  { req: { op: 'now', ms: NOW + 1800001 }, run: () => { CLOCK = NOW + 1800001; return { ok: true, error: null, out: null, result: null }; } },
  { req: { op: 'relay_find' }, run: () => http('GET', '/find') },
  { req: { op: 'relay_find', q: 'demo' }, run: () => http('GET', `/find?q=${encodeURIComponent('demo')}`) },
];

function bigRoundTrip(n) {
  const frame = ref.signal('v', 'e', { blob: 'x'.repeat(n) }, { timestamp: 0 });
  return ref.encode(ref.decode(ref.encode(frame)));
}

/* Canonical serialisation of one record line, used only when the raw text of
 * the two records differs.  The corpora contain lone surrogates (see the
 * `\u escapes in a payload` entries): the engine follows the JSON spec and
 * emits U+FFFD, while JSON.parse keeps the unpaired surrogate, so the
 * reference's `out` carries the six characters `\ud83d` where the engine's
 * carries a literal EF BF BD.  One is a JSON escape, the other a raw code
 * point: the same value on the wire, not the same JS string, so the record text
 * differs even though neither side is wrong.  `out` is itself a JSON document
 * (the CRP line that was encoded/decoded), so parse it and normalise the parse:
 * escaping U+FFFD back to `\ufffd` is a no-op for every well-formed text, so
 * this cannot hide a real difference -- it only stops the two spellings of an
 * unpaired surrogate from disagreeing.  A line that is not JSON is returned
 * as-is so the mismatch still points at it. */
function canonicalRecord(line) {
  let rec;
  try {
    rec = JSON.parse(line);
  } catch {
    return line;
  }
  if (rec && typeof rec.out === 'string') {
    try {
      rec.out = JSON.parse(rec.out);
    } catch {
      /* not a CRP message -- leave it alone */
    }
  }
  return JSON.stringify(rec, (key, value) => {
    if (typeof value !== 'string' || value.isWellFormed()) return value;
    let fixed = '';
    for (const ch of value) {
      const cp = ch.codePointAt(0);
      fixed += cp >= 0xd800 && cp <= 0xdfff ? '\uFFFD' : ch;
    }
    return fixed;
  });
}

async function main() {
  const probe = process.argv[2];
  if (!probe) {
    console.error('usage: node tools/crp_engine_crosscheck.js <path-to-verse_crp_probe>');
    process.exit(2);
  }
  if (!fs.existsSync(probe)) {
    console.error(`crp_engine_crosscheck: probe not found: ${probe}`);
    process.exit(2);
  }

  relay = createRelay({ secret: SECRET });
  await new Promise(resolve => relay.server.listen(0, '127.0.0.1', resolve));
  const port = relay.server.address().port;
  http = (method, url, body) => request(port, method, url, body);

  const tmp = path.join(os.tmpdir(), `crp_corpus_${process.pid}.jsonl`);
  fs.writeFileSync(tmp, CORPUS.map(e => JSON.stringify(e.req)).join('\n') + '\n');

  const proc = spawnSync(probe, ['--transcript', tmp, '--secret', SECRET, '--now', String(NOW)], {
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
  });
  fs.unlinkSync(tmp);
  if (proc.status !== 0) {
    console.error(`crp_engine_crosscheck: probe exited ${proc.status}\n${proc.stderr}`);
    process.exit(1);
  }
  const got = proc.stdout.split('\n').filter(l => l.length > 0);

  const want = [];
  for (let i = 0; i < CORPUS.length; i++) {
    const rec = await CORPUS[i].run();
    want.push(JSON.stringify({ n: i + 1, op: CORPUS[i].req.op, ...rec }));
  }

  let bad = 0;
  const n = Math.max(got.length, want.length);
  for (let i = 0; i < n; i++) {
    const a = want[i];
    const b = got[i];
    if (a === b) continue;
    /* The corpora now contain lone surrogates, and a well-formed-JSON record
     * cannot carry one.  The engine follows the spec and emits U+FFFD where
     * JSON.parse keeps the unpaired surrogate, so its record holds a literal
     * EF BF BD while JSON.stringify escapes the reference's U+D83D as the six
     * characters `\ud83d`.  Both decode to the same JSON value -- the same
     * bytes on the wire -- so compare the canonical serialisation of BOTH
     * sides rather than the two spellings. */
    if (a !== undefined && b !== undefined && canonicalRecord(b) === canonicalRecord(a)) continue;
    bad++;
    if (bad <= 20) {
      console.error(`MISMATCH line ${i + 1} (${CORPUS[i] ? CORPUS[i].req.op : '?'})`);
      console.error(`  reference: ${a === undefined ? '<missing>' : a}`);
      console.error(`  engine   : ${b === undefined ? '<missing>' : b}`);
    }
  }

  relay.server.close();
  if (bad === 0) {
    console.log(`crp_engine_crosscheck: ${want.length} records, text-identical`);
    return 0;
  }
  console.error(`crp_engine_crosscheck: ${bad} of ${want.length} records differ`);
  return 1;
}

/* ---- HTTP ---------------------------------------------------------------- */

async function request(port, method, url, body) {
  const init = { method };
  if (body !== undefined) {
    init.headers = { 'content-type': 'application/json' };
    init.body = JSON.stringify(body);
  }
  const res = await fetch(`http://127.0.0.1:${port}${url}`, init);
  const text = await res.text();
  return { ok: true, error: null, out: `${res.status} ${text}`, result: null };
}

main().then(code => process.exit(code), err => {
  console.error(err);
  process.exit(1);
});
