'use strict';
const assert = require('node:assert/strict');
const { createRelay } = require('./crp_relay');
const { server, revoked } = createRelay({ ttlMs: 1000, maxRevokedTokens: 1 });
server.listen(0, async () => {
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (u, body) => fetch(base + u, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) }).then(r => r.json().then(j => ({ status: r.status, ...j })));
  try {
    assert.equal((await post('/register', { id: 'demo', name: 'Demo', endpoint: 'local' })).ok, true);
    assert.equal((await (await fetch(base + '/find?q=dem')).json()).items.length, 1);
    const portal = await post('/portal', { verse: 'demo', peer: 'p1' });
    assert.equal(portal.peer, 'p1');
    assert.equal((await post('/signal', { verse: 'demo', peer: 'p1', event: 'join', token: portal.token, data: {} })).status, 202);
    assert.equal((await post('/signal', { verse: 'demo', peer: 'bad', event: 'join', token: portal.token, data: {} })).status, 403);
    assert.equal((await post('/revoke', { token: portal.token })).revoked, true);
    assert.equal((await post('/signal', { verse: 'demo', peer: 'p1', event: 'join', token: portal.token, data: {} })).status, 403);
    const portal2 = await post('/portal', { verse: 'demo', peer: 'p2' });
    await post('/revoke', { token: portal2.token });
    assert.equal(revoked.size, 1);
    const stored = await post('/content', { data: 'hello' });
    assert.equal(stored.status, 201);
    const got = await (await fetch(base + '/content/' + stored.hash)).text();
    assert.equal(got, 'hello');
    assert.equal((await post('/content', { data: 'hello', hash: '0'.repeat(64) })).status, 400);
    assert.equal((await post('/portal', { verse: 'missing', peer: 'p1' })).status, 404);
    const pkg = await post('/package', { id: 'demo', data: Buffer.from('pkg').toString('base64') }); assert.equal(pkg.status, 201);
    assert.equal((await (await fetch(base + '/package/demo')).text()), 'pkg');
    assert.equal((await (await fetch(base + '/packages?q=demo')).json()).items.length, 1);
    assert.equal((await post('/package/fork', { source: 'demo', id: 'demo-fork' })).status, 201);
    assert.equal((await fetch(base + '/package/demo-fork', { method: 'DELETE' })).status, 200);
    assert.equal((await fetch(base + '/package/demo-fork')).status, 404);
    console.log('CRP relay tests: ok');
  } finally { server.close(); }
});
