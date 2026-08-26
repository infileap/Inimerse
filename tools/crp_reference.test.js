'use strict';
const assert = require('node:assert/strict');
const { find, portal, signal, encode, decode } = require('./crp_reference');
const f = find('demo', { limit: 10 });
assert.equal(f.type, 'FIND');
assert.equal(decode(encode(f)).payload.limit, 10);
assert.equal(portal('demo', 'peer-1').payload.verse, 'demo');
assert.equal(signal('demo', 'join', { user: 'a' }).payload.data.user, 'a');
assert.throws(() => find('', { limit: 0 }), /limit/);
assert.throws(() => portal('', 'peer'), /verse/);
assert.throws(() => signal('demo', '', {}), /event/);
console.log('CRP reference tests: ok');

