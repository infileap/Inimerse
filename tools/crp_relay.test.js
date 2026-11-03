'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { createRelay } = require('./crp_relay');
/* The legacy hub-wide secret.  It is what a migrating caller still computes
   with, but it decides nothing by itself: only the admissions table decides who
   may open a portal, and one line of that table is the only key that works for
   one pair. */
const ENROLL = 'enroll-secret-0123456789abcdef';
const proof = (verse, peer, secret = ENROLL) => crypto.createHmac('sha256', secret).update(`${verse}\0${peer}`).digest('base64url');
const ADMISSIONS = [
  { verse: 'demo', peer: 'p1', secret: ENROLL, capabilities: ['signal'] },
  { verse: 'demo', peer: 'p2', secret: ENROLL, capabilities: ['signal'] },
  { verse: 'missing', peer: 'p1', secret: ENROLL, capabilities: ['signal'] },
];
const { server, revoked, sessions } = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, maxRevokedTokens: 1, admissions: ADMISSIONS });
/* Raw HTTP, because one of the judgements below is "these two refusals are the
   same bytes", and neither fetch nor JSON can show that: fetch hides the status
   line and normalises the headers.  Date is dropped from the comparison on
   purpose -- it has one-second resolution, so two requests that straddle a tick
   would differ every time and the criterion would be relaxed the first time it
   flaked.  Date is asserted separately to be a well-formed HTTP date, which is
   the part that matters. */
const rawPost = (port, urlPath, body) => new Promise((resolve, reject) => {
  const payload = Buffer.from(JSON.stringify(body));
  const req = http.request({ host: '127.0.0.1', port, path: urlPath, method: 'POST', headers: { 'content-type': 'application/json', 'content-length': payload.length } }, res => {
    const chunks = [];
    res.on('data', c => chunks.push(c));
    res.on('end', () => {
      const headers = [];
      for (let i = 0; i < res.rawHeaders.length; i += 2) if (res.rawHeaders[i].toLowerCase() !== 'date') headers.push([res.rawHeaders[i].toLowerCase(), res.rawHeaders[i + 1]]);
      const at = res.rawHeaders.findIndex(h => String(h).toLowerCase() === 'date');
      resolve({ statusLine: `HTTP/${res.httpVersion} ${res.statusCode} ${res.statusMessage}`, headers, body: Buffer.concat(chunks).toString('utf8'), date: at < 0 ? '' : res.rawHeaders[at + 1] });
    });
  });
  req.on('error', reject); req.end(payload);
});
const listen = async relay => { await new Promise(r => relay.server.listen(0, r)); return relay.server.address().port; };
const tmpAdmissions = text => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'crp-admissions-'));
  fs.writeFileSync(path.join(dir, 'portal_admissions.tsv'), text);
  return dir;
};
server.listen(0, async () => {
  const port = server.address().port;
  const base = `http://127.0.0.1:${port}`;
  const post = (u, body) => fetch(base + u, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) }).then(r => r.json().then(j => ({ status: r.status, ...j })));
  try {
    assert.equal((await post('/register', { id: 'demo', name: 'Demo', endpoint: 'local' })).ok, true);
    assert.equal((await (await fetch(base + '/find?q=dem')).json()).items.length, 1);
    assert.equal((await post('/friends', { id: 'p1', verse: 'demo', endpoint: 'ws://local', name: 'Peer' })).ok, true);
    assert.equal((await (await fetch(base + '/friends?verse=demo')).json()).items[0].id, 'p1');
    // /portal is the only authorization entry point, so minting must require
    // the caller to prove (verse, peer). Absent, wrong and mismatched-pair
    // proofs are all refused, and refused BEFORE anything is minted or bound:
    // no response token, and no session created as a side effect.
    const absent = await post('/portal', { verse: 'demo', peer: 'p1' });
    assert.equal(absent.status, 403);
    assert.equal(absent.error, 'invalid enrollment proof');
    assert.equal(absent.token, undefined);
    assert.equal((await post('/portal', { verse: 'demo', peer: 'p1', auth: 'not-a-proof' })).status, 403);
    assert.equal((await post('/portal', { verse: 'demo', peer: 'p2', auth: proof('demo', 'p1') })).status, 403);
    assert.equal((await post('/portal', { verse: 'demo', peer: 'p1', auth: proof('other', 'p1') })).status, 403);
    assert.equal(sessions.size, 0);
    const portal = await post('/portal', { verse: 'demo', peer: 'p1', auth: proof('demo', 'p1') });
    assert.equal(portal.peer, 'p1');
    assert.ok(portal.expires > Date.now() && portal.expires <= Date.now() + 1100);
    assert.equal((await post('/session/resume', { verse: 'demo', peer: 'p1', token: portal.token, seq: 4 })).lastSeq, 4);
    assert.equal((await post('/session/resume', { verse: 'demo', peer: 'p1', token: portal.token, seq: 3 })).status, 409);
    assert.equal((await post('/signal', { verse: 'demo', peer: 'p1', event: 'join', token: portal.token, data: {} })).status, 202);
    assert.equal((await post('/signal', { verse: 'demo', peer: 'bad', event: 'join', token: portal.token, data: {} })).status, 403);
    assert.equal((await post('/revoke', { token: portal.token })).revoked, true);
    assert.equal((await post('/signal', { verse: 'demo', peer: 'p1', event: 'join', token: portal.token, data: {} })).status, 403);
    const portal2 = await post('/portal', { verse: 'demo', peer: 'p2', auth: proof('demo', 'p2') });
    await post('/revoke', { token: portal2.token });
    assert.equal(revoked.size, 1);
    const stored = await post('/content', { data: 'hello' });
    assert.equal(stored.status, 201);
    const got = await (await fetch(base + '/content/' + stored.hash)).text();
    assert.equal(got, 'hello');
    assert.equal((await post('/content', { data: 'hello', hash: '0'.repeat(64) })).status, 400);
    // ADMISSIONS BEFORE REGISTRY.  A pair that is not in the table and a proof
    // that does not verify are the same refusal, byte for byte; only a pair the
    // table admits can reach the registry test and learn that its verse is
    // unknown.  ('missing', 'p1') is admitted, so it gets the 404; ('nobody',
    // 'p1') is not, and must not be able to tell that 'nobody' does not exist.
    const badProof = await rawPost(port, '/portal', { verse: 'demo', peer: 'p1', auth: proof('demo', 'p1', 'not-the-secret') });
    assert.equal(badProof.statusLine, 'HTTP/1.1 403 Forbidden');
    assert.equal(badProof.body, '{"error":"invalid enrollment proof"}');
    assert.match(badProof.date, /^[A-Z][a-z]{2}, \d{2} [A-Z][a-z]{2} \d{4} \d{2}:\d{2}:\d{2} GMT$/);
    assert.deepEqual(await rawPost(port, '/portal', { verse: 'demo', peer: 'p3', auth: proof('demo', 'p3') }), badProof);
    assert.deepEqual(await rawPost(port, '/portal', { verse: 'nobody', peer: 'p1', auth: proof('nobody', 'p1') }), badProof);
    assert.deepEqual(await rawPost(port, '/portal', { verse: 'demo', peer: 'p1' }), badProof);
    assert.equal((await post('/portal', { verse: 'missing', peer: 'p1', auth: proof('missing', 'p1') })).status, 404);
    // The portal's scope is a pair of non-empty strings, so a member that is
    // not a string names no scope at all: it is refused with the same 404 as an
    // unregistered verse, and mints nothing.  The engine asks the very same
    // question (registry_has_verse + a VJ_STR peer test in src/verse/crp.c),
    // which is what the relay_portal corpus records pin.
    // A member that is not a string names no pair at all, so it is refused like
    // an unregistered one -- not with a 404 it could fingerprint.
    const sessionsBefore = sessions.size;
    for (const body of [
      { verse: 5, peer: 'p1', auth: proof(5, 'p1') },
      { verse: 'demo', peer: 5, auth: proof('demo', 5) },
      { verse: 'demo', peer: ['a'], auth: proof('demo', 'a') },
      { verse: 'demo', peer: '', auth: proof('demo', '') },
    ]) {
      assert.equal((await post('/portal', body)).status, 403);
      assert.deepEqual(await rawPost(port, '/portal', body), badProof);
    }
    assert.equal(sessions.size, sessionsBefore);
    assert.equal((await post('/portal', { verse: 'demo', peer: 'p1' })).status, 403);
    const pkg = await post('/package', { id: 'demo', data: Buffer.from('pkg').toString('base64') }); assert.equal(pkg.status, 201);
    assert.equal((await (await fetch(base + '/package/demo')).text()), 'pkg');
    assert.equal((await (await fetch(base + '/packages?q=demo')).json()).items.length, 1);
    assert.equal((await post('/package/fork', { source: 'demo', id: 'demo-fork' })).status, 201);
    assert.equal((await fetch(base + '/package/demo-fork', { method: 'DELETE' })).status, 200);
    assert.equal((await fetch(base + '/package/demo-fork')).status, 404);
    // FAIL-CLOSED default: a relay with no enrollment secret refuses /portal
    // outright. The default must not be "open until someone switches auth on".
    const bare = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, admissions: [] });
    await new Promise(resolve => bare.server.listen(0, resolve));
    try {
      const res = await fetch(`http://127.0.0.1:${bare.server.address().port}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1') }),
      });
      assert.equal(res.status, 403);
      // Same refusal as a bad proof: an empty table must not have a shape of
      // its own, or "is enrollment even configured" becomes a probe.
      assert.equal((await res.json()).error, 'invalid enrollment proof');
    } finally { bare.server.close(); }

    // --- A FILE IS A SINGLE SOURCE.  CRLF, comments and blank lines are
    // formatting; trailing spaces are not part of a field; the secret may
    // contain a space (it may not contain a tab -- that is the separator).
    const cleanDir = tmpAdmissions('# portal admissions\r\n\r\ndemo\tp1\tlegacy secret with space\tsignal\r\n\r\n');
    const clean = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, hubDir: cleanDir });
    const cleanPort = await listen(clean);
    try {
      const reg = await fetch(`http://127.0.0.1:${cleanPort}/register`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ id: 'demo', name: 'Demo', endpoint: 'local' }),
      });
      assert.equal(reg.status, 200);
      const ok = await fetch(`http://127.0.0.1:${cleanPort}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1', 'legacy secret with space') }),
      });
      assert.equal(ok.status, 200);
      const wrong = await fetch(`http://127.0.0.1:${cleanPort}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1', ENROLL) }),
      });
      assert.equal(wrong.status, 403);
    } finally { clean.server.close(); }

    // --- A TABLE THAT CANNOT BE TRUSTED IS AN EMPTY TABLE, NOT A PARTIAL ONE.
    // A pair written twice is a half-finished rotation or a typo, and both must
    // stop the world: first-wins and last-wins are the same disease as R2.  An
    // unknown capability, a short line and a blank field likewise void the file,
    // and none of them may name itself in the response.
    for (const [name, text] of Object.entries({
      duplicate: 'demo\tp1\tone\tsignal\ndemo\tp1\ttwo\tsignal\n',
      'unknown-capability': 'demo\tp1\tone\tsignal,teleport\n',
      malformed: 'demo\tp1\tone\n',
      'blank-field': 'demo\t\tone\tsignal\n',
    })) {
      const relay = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, hubDir: tmpAdmissions(text) });
      const p = await listen(relay);
      try {
        const res = await fetch(`http://127.0.0.1:${p}/portal`, {
          method: 'POST', headers: { 'content-type': 'application/json' },
          body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1', 'one') }),
        });
        assert.equal(res.status, 403, `${name} must void the table`);
        assert.equal((await res.json()).error, 'invalid enrollment proof', `${name} must not name itself in the response`);
      } finally { relay.server.close(); }
    }

    // --- NO FALLBACK ACROSS SOURCES.  With CRP_ADMISSIONS_FILE pointing at a
    // file that does not exist, the hub default (which DOES exist here) must not
    // take over: a misconfigured pointer is an empty table, not a licence to
    // read whatever else is lying around.
    const fallbackDir = tmpAdmissions('demo\tp1\t' + ENROLL + '\tsignal\n');
    const savedPointer = process.env.CRP_ADMISSIONS_FILE;
    process.env.CRP_ADMISSIONS_FILE = path.join(fallbackDir, 'does-not-exist.tsv');
    const noFallback = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, hubDir: fallbackDir });
    const nfPort = await listen(noFallback);
    try {
      const res = await fetch(`http://127.0.0.1:${nfPort}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1') }),
      });
      assert.equal(res.status, 403, 'a missing CRP_ADMISSIONS_FILE must not fall back to the hub default');
    } finally {
      noFallback.server.close();
      if (savedPointer === undefined) delete process.env.CRP_ADMISSIONS_FILE; else process.env.CRP_ADMISSIONS_FILE = savedPointer;
    }

    // --- CRP_ENROLL_SECRET IS DEAD FACT.  With the old hub-wide secret in the
    // environment and a table in play, an unregistered pair is still refused:
    // knowing the old secret must not buy somebody else's portal.
    const savedLegacy = process.env.CRP_ENROLL_SECRET;
    process.env.CRP_ENROLL_SECRET = ENROLL;
    const envRelay = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, admissions: ADMISSIONS });
    const envPort = await listen(envRelay);
    try {
      const res = await fetch(`http://127.0.0.1:${envPort}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p4', auth: proof('demo', 'p4') }),
      });
      assert.equal(res.status, 403, 'CRP_ENROLL_SECRET in the environment must not admit an unregistered pair');
    } finally {
      envRelay.server.close();
      if (savedLegacy === undefined) delete process.env.CRP_ENROLL_SECRET; else process.env.CRP_ENROLL_SECRET = savedLegacy;
    }

    console.log('CRP relay tests: ok');
  } finally { server.close(); }
});
