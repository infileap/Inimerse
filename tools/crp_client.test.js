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
    assert.equal((await c.portal('demo', 'peer')).peer, 'peer');
    assert.equal((await c.signal('demo', 'join')).accepted, true);
    const published = await c.publishPackage('demo', Buffer.from('pkg')); assert.equal((await c.downloadPackage('demo', { hash: published.hash })).toString(), 'pkg');
    await assert.rejects(() => c.downloadPackage('demo', { hash: '0'.repeat(64) }), /hash mismatch/);
    const dataRes = await fetch(base + '/content', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ data: 'multi-source' }) }).then(r => r.json());
    const bytes = await c.fetchContent(dataRes.hash, [base + '-missing', base]);
    assert.equal(bytes.toString(), 'multi-source');
    await assert.rejects(() => new CrpClient(base + '-bad', { retries: 1, backoffMs: 1 }).find(), /fetch failed|ENOTFOUND|ECONNREFUSED|Invalid URL|parse URL/);
    console.log('CRP client tests: ok');
  } finally { server.close(); }
});
