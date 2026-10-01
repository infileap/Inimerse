'use strict';
// CRP session flow over a real websocket connection (M2, white paper
// §55.3 handshake/negotiation, §55.6 sequencing + resume).
// Usage: node crp_session_flow.test.js ws://127.0.0.1:PORT
const assert = require('node:assert/strict');
const { MiniWs } = require('./mini_ws');

const base = process.argv[2];
assert.ok(base, 'usage: node crp_session_flow.test.js ws://host:port');
const m = base.match(/^ws:\/\/([^:/]+):(\d+)/);
assert.ok(m, `bad base url: ${base}`);
const HOST = m[1], PORT = Number(m[2]);

function connect(query, timeoutMs = 4000) {
  const ws = new MiniWs(HOST, PORT);
  return ws.connect('/ws' + query, timeoutMs).then(() => ws);
}

function nextMessage(ws, timeoutMs = 3000) {
  return ws.nextMessage(timeoutMs);
}

async function main() {
  // §55.3: version mismatch must be rejected explicitly (§24.6)
  {
    const ws = await connect('?ver=2&caps=3');
    const msg = await nextMessage(ws);
    assert.ok(msg.includes('protocol_version_mismatch'), `expected mismatch, got: ${msg}`);
    assert.ok(msg.includes('peer 2'), `expected peer version in error, got: ${msg}`);
    ws.close();
  }

  // §55.6: sequencing on a negotiated connection
  const ws = await connect('?ver=1&caps=3');
  ws.sendText(JSON.stringify({ type: 'update', seq: 1 }));

  ws.sendText(JSON.stringify({ type: 'update', seq: 5 }));   // gap
  let msg = await nextMessage(ws);
  assert.ok(msg.includes('resume_required') && msg.includes('"last_applied":1'),
            `expected gap response last_applied=1, got: ${msg}`);

  ws.sendText(JSON.stringify({ type: 'update', seq: 1 }));   // duplicate: dropped
  ws.sendText(JSON.stringify({ type: 'update', seq: 3 }));   // still a gap after duplicate
  msg = await nextMessage(ws);
  assert.ok(msg.includes('resume_required') && msg.includes('"last_applied":1'),
            `duplicate must not advance last_applied, got: ${msg}`);

  ws.sendText(JSON.stringify({ type: 'update', seq: 2 }));   // contiguous
  ws.sendText(JSON.stringify({ type: 'update', seq: 3 }));
  ws.sendText(JSON.stringify({ type: 'update', seq: 9 }));   // next gap reports progress
  msg = await nextMessage(ws);
  assert.ok(msg.includes('resume_required') && msg.includes('"last_applied":3'),
            `expected last_applied=3, got: ${msg}`);

  ws.close();
  console.log('crp session flow: ok');
}

main().catch((e) => { console.error(String(e && e.message || e)); process.exit(1); });
