'use strict';
const assert = require('node:assert/strict');
const { createRelay } = require('./crp_relay');
const { CrpClient } = require('./crp_client');
const { server } = createRelay();
server.listen(0, async () => {
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    await fetch(base + '/register', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ id: 'demo', endpoint: 'local' }) });
    const c = new CrpClient(base, { retries: 2, backoffMs: 1 });
    assert.equal((await c.find('demo')).items.length, 1);
    assert.equal((await c.registerRoute('peer-route', '127.0.0.1:9000', 'demo')).ok, true);
    assert.equal((await c.resolveRoute('peer-route')).endpoint, '127.0.0.1:9000');
    assert.equal((await c.registerCandidate('peer-nat', '10.0.0.2:4000', 'demo')).ok, true);
    assert.equal((await c.listCandidates()).candidates.some(x => x.id === 'peer-nat'), true);
    assert.equal((await c.portal('demo', 'peer')).peer, 'peer');
    assert.equal((await c.signal('demo', 'join')).accepted, true);
    assert.equal((await c.signal('demo', 'ready')).seq, 2);
    assert.equal((await c.resumeSession('demo', 'peer', c.token, 0)).replay.length, 2);
    assert.equal((await c.stopSession('demo')).stopped, true);
    assert.equal((await c.startSession('demo')).resumed, true);
    assert.equal((await c.heartbeat('demo', 3)).accepted, true);
    const published = await c.publishPackage('demo', Buffer.from('pkg')); assert.equal((await c.downloadPackage('demo', { hash: published.hash })).toString(), 'pkg');
    assert.equal(c.hasCachedPackage('demo'), true); assert.equal(c.cachedPackage('demo').toString(), 'pkg'); assert.equal(c.cacheStats().entries, 1); c.clearPackageCache(); assert.equal(c.cacheStats().entries, 0);
    const limited = new CrpClient(base, { maxCacheBytes: 2 }); await limited.downloadPackage('demo'); assert.equal(limited.cacheStats().bytes, 0);
    assert.equal((await c.listPackages()).items.length, 1); await c.forkPackage('demo', 'forked'); assert.equal((await c.downloadPackage('forked')).toString(), 'pkg'); await c.deletePackage('forked'); assert.equal(c.hasCachedPackage('forked'), false);
    await assert.rejects(() => c.downloadPackage('demo', { hash: '0'.repeat(64) }), /hash mismatch/);
    const dataRes = await fetch(base + '/content', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ data: 'multi-source' }) }).then(r => r.json());
    const bytes = await c.fetchContent(dataRes.hash, [base + '-missing', base]);
    assert.equal(bytes.toString(), 'multi-source');
    await assert.rejects(() => new CrpClient(base + '-bad', { retries: 1, backoffMs: 1 }).find(), /fetch failed|ENOTFOUND|ECONNREFUSED|Invalid URL|parse URL/);
    console.log('CRP client tests: ok');
  } finally { server.close(); }
});
