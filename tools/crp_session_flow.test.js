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

function step(name) { process.stderr.write(`[crp-flow] ${name}\n`); }

async function main() {
  step('version-mismatch');
  // §55.3: version mismatch must be rejected explicitly (§24.6)
  {
    const ws = await connect('?ver=2&caps=3');
    const msg = await nextMessage(ws);
    assert.ok(msg.includes('protocol_version_mismatch'), `expected mismatch, got: ${msg}`);
    assert.ok(msg.includes('peer 2'), `expected peer version in error, got: ${msg}`);
    ws.close();
  }

  step('sequencing');
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

  step('replay-refill');
  /* §51.9 M1xM2 bridge: events recorded during the session can be replayed
     back over HTTP so a peer that fell behind can refill the gap. */
  const verse = 'verse-replay', peer = 'peer-replay';
  const ws2 = await connect('?ver=1&caps=3');
  step('ws2-connected');
  for (let seq = 1; seq <= 3; seq++) {
    ws2.sendText(JSON.stringify({ type: 'update', seq, verse, peer, event: `e${seq}` }));
  }
  // wait until the server has persisted the window (retry the HTTP read)
  const httpBase = `http://${HOST}:${PORT}`;
  // /session/resume requires a capability token scoped to (verse, peer)
  const tokRes = await fetch(`${httpBase}/portal`, {
    method: 'POST', body: JSON.stringify({ verse, peer }),
  });
  const tokText = await tokRes.text();
  const tokMatch = tokText.match(/"token":"([^"]+)"/);
  assert.ok(tokMatch, `expected portal token, got: ${tokText}`);
  const token = tokMatch[1];
  let replay = null;
  for (let attempt = 0; attempt < 50; attempt++) {
    const res = await fetch(`${httpBase}/session/resume`, {
      method: 'POST',
      body: JSON.stringify({ verse, peer, seq: 0, token, replay: true }),
    });
    const text = await res.text();
    if (res.status === 200 && text.includes('"seq":3')) { replay = text; break; }
    await new Promise(r => setTimeout(r, 50));
  }
  assert.ok(replay, 'expected /session/resume to return the recorded events');
  assert.ok(replay.includes('"complete":true'), `expected complete replay, got: ${replay}`);
  assert.ok(replay.includes('"event":"e1"') && replay.includes('"event":"e3"'),
            `expected e1..e3 in replay, got: ${replay}`);
  // resuming from an already-covered point must not duplicate old events
  {
    const res = await fetch(`${httpBase}/session/resume`, {
      method: 'POST', body: JSON.stringify({ verse, peer, seq: 3, token, replay: true }),
    });
    const text = await res.text();
    assert.ok(!text.includes('"event":"e1"'), `stale events leaked: ${text}`);
  }
  // exceed the retained window -> explicit snapshot_required (§24.6)
  for (let seq = 4; seq <= 22; seq++) {
    ws2.sendText(JSON.stringify({ type: 'update', seq, verse, peer, event: `e${seq}` }));
  }
  let snap = null;
  for (let attempt = 0; attempt < 50; attempt++) {
    const res = await fetch(`${httpBase}/session/resume`, {
      method: 'POST', body: JSON.stringify({ verse, peer, seq: 1, token, replay: true }),
    });
    const text = await res.text();
    if (res.status === 200 && text.includes('snapshot_required')) { snap = text; break; }
    await new Promise(r => setTimeout(r, 50));
  }
  assert.ok(snap, 'expected snapshot_required once the request predates the window');
  assert.ok(snap.includes('"latest_seq":22'), `expected latest_seq progress, got: ${snap}`);
  ws2.close();

  ws.close();
  console.log('crp session flow: ok');
}

main().catch((e) => { console.error(String(e && e.message || e)); process.exit(1); });
