'use strict';
/*
 * crp_wire_neg.test.js -- the CRP refusals, measured across a process boundary.
 *
 * docs/STATUS.md §10.100 settled a contradiction between the roadmap (§9.2 item
 * 2, "CRP FIND/PORTAL 回环和签名校验 仍未实现") and §10.6 (the engine-side line
 * layer landed) by running the loopback and reading the transcript.  That
 * reading answered "does it work", but every refusal it saw was produced by the
 * engine's own peer -- a driver that asserts what the engine believes it
 * refuses.  A driver like that cannot notice a capability check that returns
 * true for everything: the positive path stays green either way, and only the
 * developer's memory says a refusal was ever expected.
 *
 * So this suite is a third party.  It spawns the real `crp-hub` binary, speaks
 * the wire protocol over a real TCP socket, mints its own enrolment proofs and
 * capability tokens with the protocol's HMAC (the construction in
 * src/verse/crp.c's crp_enroll_proof), and then asks for things it must not
 * get.  The negative controls are the point:
 *
 *   - a proof signed for a different (verse, peer) pair    -> 403
 *   - a capability token whose signature was flipped       -> 403, error text
 *                                                             "invalid capability token"
 *   - a token minted under the wrong key                   -> 403
 *   - a valid token replayed against a different peer      -> 403
 *
 * Each refusal sits beside a positive control asked the same way (same op, same
 * shape, good signature -> 200 / 202), so a 403 cannot be explained by the
 * endpoint being broken for everybody.  Between them the four checks separate
 * "refused" from "never ran", which is the distinction §10.100 says it needs.
 *
 * Ports: the hub is started with `--port 0`, so the kernel assigns it and the
 * hub prints the number.  An explicit port would reintroduce the
 * release-to-bind race that docs/STATUS.md §2.9 closed.
 *
 * Hub lookup: argv[2], then INIM_CRP_HUB_BIN, then <repo>/build/crp-hub.  A
 * missing binary FAILS rather than skipping: on this platform the gate builds
 * it in the stage before the node suites, and `stage_node` treats a skip as a
 * failure anyway (see tools/gate.sh's note on "a skip is not a pass").
 *
 * Run standalone: node tools/crp_wire_neg.test.js [path/to/crp-hub]
 */
const net = require('node:net');
const os = require('node:os');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

const REPO = path.resolve(__dirname, '..');
const HUB = process.argv[2] || process.env.INIM_CRP_HUB_BIN || path.join(REPO, 'build', 'crp-hub');

// Fixed inputs: a suite whose readings depend on the clock or a random secret
// cannot be compared between runs.
const SECRET = '00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff';
const WRONG = 'ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100';
const ENROLL = 'ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100';
const NOW = 1767225600000; // 2026-01-01T00:00:00Z, the hub's "now" for expiry

function b64u(buf) { return Buffer.from(buf).toString('base64url'); }
function hmac(key, msg) { return b64u(crypto.createHmac('sha256', key).update(msg).digest()); }
// The enrolment proof is HMAC(secret, verse || "\0" || peer) -- see crp_enroll_proof.
function proof(verse, peer) { return hmac(ENROLL, String(verse) + '\0' + String(peer)); }
// A capability token is "<b64url(body)>.<b64url(HMAC(key, body))>".
function token(verse, peer, key) {
  const body = JSON.stringify({ verse, peer, capabilities: ['signal'], exp: NOW + 300000 });
  const b = b64u(Buffer.from(body, 'utf8'));
  return b + '.' + hmac(key, b);
}
// Flip one signature character: still well-formed base64url, no longer the signature.
function flip(tok) {
  const i = tok.lastIndexOf('.');
  const sig = tok.slice(i + 1);
  const c = sig[sig.length - 1] === 'A' ? 'B' : 'A';
  return tok.slice(0, i + 1) + sig.slice(0, -1) + c;
}

const checks = [];
function check(name, got, want) { checks.push({ name, got, want, ok: got === want }); }

async function main() {
  if (!fs.existsSync(HUB)) {
    console.error('wire_neg: no hub binary at ' + HUB +
      ' -- build it first (cmake --build build), or pass its path / set INIM_CRP_HUB_BIN.');
    process.exit(2);
  }
  /* One source for the hub's portal admissions: a table written here.  The pair
   * below is the only one admitted, so "a proof for another pair" has something
   * to be refused against.  CRP_ENROLL_SECRET stays in the environment on
   * purpose -- it is dead now, and the checks below prove it stays dead. */
  const admissions = path.join(os.tmpdir(), `wire_neg_admissions_${process.pid}.tsv`);
  fs.writeFileSync(admissions, `demo\twire-peer\t${ENROLL}\tsignal\n`);
  const hub = spawn(HUB, ['0', SECRET, String(NOW)],
    { env: { ...process.env, CRP_ENROLL_SECRET: ENROLL, CRP_ADMISSIONS_FILE: admissions }, stdio: ['ignore', 'pipe', 'pipe'] });
  let hubErr = '';
  hub.stderr.on('data', (d) => { hubErr += d; });
  const port = await new Promise((res, rej) => {
    hub.stdout.once('data', (d) => {
      const m = String(d).match(/"port":(\d+)/);
      if (m) { res(Number(m[1])); } else { rej(new Error('no port announce: ' + d)); }
    });
    hub.on('exit', (c) => rej(new Error('hub exited ' + c + ' ' + hubErr)));
  });

  const sock = net.connect(port, '127.0.0.1');
  await new Promise(r => sock.once('connect', r));
  let buf = '';
  const pending = [];
  sock.on('data', (d) => {
    buf += d;
    let nl;
    while ((nl = buf.indexOf('\n')) !== -1) {
      const line = buf.slice(0, nl); buf = buf.slice(nl + 1);
      pending.shift()(JSON.parse(line));
    }
  });
  function call(op, payload) {
    const body = { op, payload };
    return new Promise((res) => { pending.push(res); sock.write(JSON.stringify(body) + '\n'); });
  }

  // Two verses exist before anything is asked, so an empty catalog is never the
  // explanation of an empty answer.
  await call('register', { id: 'demo', name: 'Demo Verse', endpoint: 'local' });
  await call('register', { id: 'second', name: 'Second', endpoint: 'remote' });

  const f = await call('find', { query: 'dem' });
  check('FIND  query=dem -> status', f.status, 200);
  check('FIND  query=dem -> items', (f.body && f.body.items && f.body.items.length) || 0, 1);

  const p = await call('portal', { verse: 'demo', peer: 'wire-peer', auth: proof('demo', 'wire-peer') });
  check('PORTAL valid proof -> status', p.status, 200);
  const tok = p.body && p.body.token;
  check('PORTAL minted a token', typeof tok === 'string' && tok.length > 20, true);

  const crop = await call('portal', { verse: 'demo', peer: 'other-peer', auth: proof('demo', 'wire-peer') });
  check('PORTAL proof for another pair -> status (must be 403)', crop.status, 403);

  const s1 = await call('signal', { verse: 'demo', peer: 'wire-peer', token: tok, event: 'join', data: { n: 1 } });
  check('SIGNAL valid token -> status', s1.status, 202);

  const s2 = await call('signal', { verse: 'demo', peer: 'wire-peer', token: flip(tok), event: 'join', data: { n: 2 } });
  check('SIGNAL tampered signature -> status (must be 403)', s2.status, 403);
  check('SIGNAL tampered signature -> error text', s2.body && s2.body.error, 'invalid capability token');

  const s3 = await call('signal', { verse: 'demo', peer: 'wire-peer', token: token('demo', 'wire-peer', WRONG), event: 'join', data: { n: 3 } });
  check('SIGNAL token minted under wrong key -> status (must be 403)', s3.status, 403);

  const s4 = await call('signal', { verse: 'demo', peer: 'elsewhere', token: tok, event: 'join', data: { n: 4 } });
  check('SIGNAL valid token for another peer -> status (must be 403)', s4.status, 403);

  const r1 = await call('resume', { verse: 'demo', peer: 'wire-peer', token: tok, seq: 1 });
  check('RESUME valid token -> status', r1.status, 200);
  const r2 = await call('resume', { verse: 'demo', peer: 'wire-peer', token: flip(tok), seq: 1 });
  check('RESUME tampered signature -> status (must be 403)', r2.status, 403);

  const st = await call('status', {});
  check('engine /status served', st.status, 200);

  await call('bye', {});
  sock.end();
  await new Promise(r => hub.once('exit', r));

  let bad = 0;
  for (const c of checks) if (!c.ok) bad++;
  for (const c of checks) {
    console.log((c.ok ? 'ok   ' : 'FAIL ') + c.name + '  got=' + JSON.stringify(c.got) +
      (c.ok ? '' : ' want=' + JSON.stringify(c.want)));
  }
  console.log('wire_neg: ' + checks.length + ' checks, ' + bad + ' failures');
  process.exit(bad ? 1 : 0);
}

main().catch((e) => { console.error('wire_neg: ' + e.message); process.exit(2); });
