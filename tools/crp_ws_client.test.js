'use strict';
const assert = require('node:assert/strict');
const { CrpWebSocketClient } = require('./crp_ws_client');
const c = new CrpWebSocketClient('ws://127.0.0.1:1', { retries: 0 });
c.send({ queued: true }); assert.equal(c.queue.length, 1); c.close(); assert.equal(c.queue.length, 0);
console.log('CRP WebSocket client tests: ok');
