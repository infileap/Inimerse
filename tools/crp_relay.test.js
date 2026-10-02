'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { createRelay } = require('./crp_relay');
const ENROLL = 'enroll-secret-0123456789abcdef';
const proof = (verse, peer) => crypto.createHmac('sha256', ENROLL).update(`${verse}\0${peer}`).digest('base64url');
const { server, revoked, sessions } = createRelay({ ttlMs: 1000, tokenTtlMs: 1000, maxRevokedTokens: 1, enrollSecret: ENROLL });
server.listen(0, async () => {
  const base = `http://127.0.0.1:${server.address().port}`;
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
    // A proof is checked before the registry is inspected, so a caller who
    // cannot prove (verse, peer) never learns whether the verse exists.
    assert.equal((await post('/portal', { verse: 'missing', peer: 'p1', auth: proof('missing', 'p1') })).status, 404);
    assert.equal((await post('/portal', { verse: 'demo', peer: 'p1' })).status, 403);
    const pkg = await post('/package', { id: 'demo', data: Buffer.from('pkg').toString('base64') }); assert.equal(pkg.status, 201);
    assert.equal((await (await fetch(base + '/package/demo')).text()), 'pkg');
    assert.equal((await (await fetch(base + '/packages?q=demo')).json()).items.length, 1);
    assert.equal((await post('/package/fork', { source: 'demo', id: 'demo-fork' })).status, 201);
    assert.equal((await fetch(base + '/package/demo-fork', { method: 'DELETE' })).status, 200);
    assert.equal((await fetch(base + '/package/demo-fork')).status, 404);
    // FAIL-CLOSED default: a relay with no enrollment secret refuses /portal
    // outright. The default must not be "open until someone switches auth on".
    const bare = createRelay({ ttlMs: 1000, tokenTtlMs: 1000 });
    await new Promise(resolve => bare.server.listen(0, resolve));
    try {
      const res = await fetch(`http://127.0.0.1:${bare.server.address().port}/portal`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ verse: 'demo', peer: 'p1', auth: proof('demo', 'p1') }),
      });
      assert.equal(res.status, 403);
      assert.equal((await res.json()).error, 'portal enrollment is not configured');
    } finally { bare.server.close(); }
    console.log('CRP relay tests: ok');
  } finally { server.close(); }
});
