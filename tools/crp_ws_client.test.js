'use strict';
/*
 * Offline tests for tools/crp_ws_client.js (the CRP WebSocket client).
 *
 * The module talks to the *global* `WebSocket` (`crp_ws_client.js:10` installs
 * `new WebSocket(this.url)`, `:12`/`:20` compare `readyState` against
 * `WebSocket.OPEN`).  This repository ships no WebSocket server and no `ws`
 * dependency, so this suite installs its own fake into `globalThis.WebSocket`
 * BEFORE requiring the module, fires `open`/`message`/`error`/`close` by hand,
 * and restores the global afterwards.  Nothing below opens a port or touches
 * the network, so the three mechanisms that make the module worth having --
 * connect, reconnect and queueing -- are pinned deterministically.
 *
 * Why a fake rather than a real socket: an assertion that depends on a live
 * peer also depends on that peer's timing, and a red test would then be
 * ambiguous between "the client regressed" and "the peer was slow".  Firing the
 * four events by hand makes every check a pure function of the client's code.
 */
const assert = require('node:assert/strict');

// ── fake global WebSocket ────────────────────────────────────────────────────
const CONNECTING = 0;
const OPEN = 1;
const CLOSING = 2;
const CLOSED = 3;
const realWebSocket = globalThis.WebSocket;

class FakeWebSocket {
  constructor(url) {
    this.url = url;
    this.readyState = CONNECTING;
    this.sent = [];
    this.createdAt = Date.now();
    this.listeners = new Map();
    FakeWebSocket.instances.push(this);
    if (FakeWebSocket.autoFail) {
      // Deferred, like a real socket: the module registers its listeners right
      // after `new WebSocket(...)` returns, so a synchronous fire would land
      // before anything was listening.
      setTimeout(() => { if (this.readyState === CONNECTING) this.fire('error', {}); }, 0);
    }
  }
  addEventListener(type, handler, options) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push({ handler, once: !!(options && options.once) });
  }
  send(data) { this.sent.push(data); }
  close() { this.readyState = CLOSED; }
  // fire() advances readyState first, so a handler sees the same state a real
  // browser would expose to it, and honours { once: true }.
  fire(type, event = {}) {
    if (type === 'open') this.readyState = OPEN;
    if (type === 'close') this.readyState = CLOSED;
    const registered = this.listeners.get(type) || [];
    this.listeners.set(type, registered.filter(entry => !entry.once));
    for (const entry of registered) entry.handler(event);
  }
}
FakeWebSocket.CONNECTING = CONNECTING;
FakeWebSocket.OPEN = OPEN;
FakeWebSocket.CLOSING = CLOSING;
FakeWebSocket.CLOSED = CLOSED;
FakeWebSocket.instances = [];
FakeWebSocket.autoFail = false;

globalThis.WebSocket = FakeWebSocket;
const { CrpWebSocketClient } = require('./crp_ws_client');

// ── assertion helpers ────────────────────────────────────────────────────────
let checks = 0;
function ok(condition, what) {
  checks += 1;
  assert.ok(condition, what);
  console.log(`  ok  ${what}`);
}
function eq(actual, expected, what) {
  checks += 1;
  assert.deepEqual(actual, expected, what);
  console.log(`  ok  ${what}`);
}
function reset() { FakeWebSocket.instances = []; FakeWebSocket.autoFail = false; }
function latest() { return FakeWebSocket.instances[FakeWebSocket.instances.length - 1]; }
function tick(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }

// ── connect ──────────────────────────────────────────────────────────────────
async function connectResolvesOnOpen() {
  console.log('connect:');
  reset();
  const c = new CrpWebSocketClient('ws://fake/connect', { retries: 0, backoffMs: 1 });
  const pending = c.connect();
  eq(FakeWebSocket.instances.length, 1, 'connect() dials exactly one socket');
  const ws = latest();
  eq(ws.url, 'ws://fake/connect', 'connect() dials the url the constructor was given');
  ok(c.socket === ws, 'the dialing socket is the client\'s current socket before it opens');
  ws.fire('open');
  ok((await pending) === c, 'open resolves connect()');
}

async function concurrentConnectSharesOneSocket() {
  reset();
  const c = new CrpWebSocketClient('ws://fake/in-flight', { retries: 0, backoffMs: 1 });
  const first = c.connect();
  const second = c.connect();
  eq(FakeWebSocket.instances.length, 1, 'a second connect() while the first is in flight dials no second socket');
  const ws = latest();
  ws.fire('open');
  ok((await first) === c && (await second) === c, 'both in-flight connect() calls resolve once the socket opens');
}

async function messagesReachOnMessage() {
  reset();
  const seen = [];
  const c = new CrpWebSocketClient('ws://fake/messages', {
    retries: 0, backoffMs: 1, onMessage: data => seen.push(data),
  });
  const pending = c.connect();
  const ws = latest();
  ws.fire('open');
  await pending;
  ws.fire('message', { data: 'hello' });
  ws.fire('message', { data: 'world' });
  eq(seen, ['hello', 'world'], 'message events reach onMessage with their payload, in arrival order');
}

// ── retry ────────────────────────────────────────────────────────────────────
async function retriesAreBoundedAndBackOff() {
  console.log('retry:');
  reset();
  FakeWebSocket.autoFail = true;
  const backoffMs = 25;
  const c = new CrpWebSocketClient('ws://fake/retry', { retries: 2, backoffMs });
  let error = null;
  try { await c.connect(); } catch (e) { error = e; }
  ok(error instanceof Error && /WebSocket connection failed/.test(error.message),
    'error rejects connect() once the retry budget is exhausted');
  eq(FakeWebSocket.instances.length, 3, 'retries: 2 means one initial attempt plus exactly two retries');
  const starts = FakeWebSocket.instances.map(ws => ws.createdAt);
  ok(starts[1] - starts[0] >= backoffMs * 0.8,
    `the first retry waits backoffMs before redialing (observed ${starts[1] - starts[0]}ms)`);
  ok(starts[2] - starts[1] >= backoffMs * 1.8,
    `the second retry waits longer than the first (observed ${starts[2] - starts[1]}ms)`);

  reset();
  FakeWebSocket.autoFail = true;
  const noRetry = new CrpWebSocketClient('ws://fake/no-retry', { retries: 0, backoffMs: 1 });
  let noRetryError = null;
  try { await noRetry.connect(); } catch (e) { noRetryError = e; }
  ok(noRetryError instanceof Error, 'retries: 0 rejects instead of redialing');
  eq(FakeWebSocket.instances.length, 1, 'retries: 0 makes exactly one attempt');
}

async function closeAbortsAnInFlightConnect() {
  reset();
  const c = new CrpWebSocketClient('ws://fake/abort', { retries: 5, backoffMs: 100 });
  const pending = c.connect();
  const ws = latest();
  ws.fire('error', {});          // first attempt fails, deterministically
  await tick(0);                 // let the catch arm the backoff timer (0ms < 100ms)
  c.close();                     // now close during that wait
  ok((await pending) === c, 'close() during the backoff wait ends connect() instead of leaving it pending');
  eq(FakeWebSocket.instances.length, 1, 'close() stops the retry loop -- no attempt is made after it');
}

// ── reconnect ────────────────────────────────────────────────────────────────
async function unexpectedCloseReconnects() {
  console.log('reconnect:');
  reset();
  const c = new CrpWebSocketClient('ws://fake/reconnect', { retries: 0, backoffMs: 1 });
  const pending = c.connect();
  const first = latest();
  first.fire('open');
  await pending;
  first.fire('close', {});       // the peer went away, not us
  eq(FakeWebSocket.instances.length, 2, 'an unexpected close dials a replacement connection');
  const second = latest();
  ok(second !== first && c.socket === second, 'the replacement is a new socket and becomes the current one');
  second.fire('open');
  await tick(0);
}

async function closeStopsReconnecting() {
  reset();
  const c = new CrpWebSocketClient('ws://fake/closed', { retries: 0, backoffMs: 1 });
  const pending = c.connect();
  const ws = latest();
  ws.fire('open');
  await pending;
  c.close();
  ok(c.socket === null, 'close() forgets the current socket');
  ws.fire('close', {});          // the peer's close event arrives after we asked to stop
  eq(FakeWebSocket.instances.length, 1, 'after close(), a close event dials no replacement connection');
}

async function staleCloseDoesNotClobberNewerConnection() {
  reset();
  const c = new CrpWebSocketClient('ws://fake/generation', { retries: 0, backoffMs: 1 });
  const firstPending = c.connect();
  const old = latest();
  old.fire('open');
  await firstPending;

  c.close();                     // bumps the generation
  const secondPending = c.connect();
  const live = latest();
  ok(live !== old, 'reconnecting after close() dials a fresh socket');
  live.fire('open');
  await secondPending;

  old.fire('close', {});         // the OLD socket's close event, from a stale generation
  eq(FakeWebSocket.instances.length, 2, 'a stale generation\'s close event dials no third connection');
  ok(c.socket === live, 'a stale generation\'s close event does not drop the live socket');
  c.send('still-live');
  eq(live.sent, ['still-live'], 'the live socket still carries traffic after a stale close event');
}

// ── queue ────────────────────────────────────────────────────────────────────
async function queuesUntilOpenThenFlushesInOrder() {
  console.log('queue:');
  reset();
  const c = new CrpWebSocketClient('ws://fake/queue', { retries: 0, backoffMs: 1 });
  const pending = c.connect();
  const ws = latest();           // created, but still CONNECTING
  c.send('first');
  c.send({ n: 2 });
  eq(ws.sent, [], 'send() on a socket that is not OPEN does not touch the wire');
  eq(c.queue, ['first', '{"n":2}'], 'send() queues the string verbatim and the object JSON-serialized');
  ws.fire('open');
  await pending;
  eq(ws.sent, ['first', '{"n":2}'], 'open flushes the queue to the wire in enqueue order');
  eq(c.queue, [], 'the queue is empty once it has been flushed');
  c.send('live');
  eq(ws.sent, ['first', '{"n":2}', 'live'], 'send() on an OPEN socket writes straight through');
  eq(c.queue, [], 'send() on an OPEN socket does not also queue');
}

async function closeClearsTheQueue() {
  reset();
  const c = new CrpWebSocketClient('ws://fake/queue-clear', { retries: 0, backoffMs: 1 });
  c.send('a');
  c.send('b');
  eq(c.queue.length, 2, 'payloads sent while disconnected are queued');
  c.close();
  eq(c.queue, [], 'close() clears the pending queue');
}

// ── run ──────────────────────────────────────────────────────────────────────
async function main() {
  await connectResolvesOnOpen();
  await concurrentConnectSharesOneSocket();
  await messagesReachOnMessage();
  await retriesAreBoundedAndBackOff();
  await closeAbortsAnInFlightConnect();
  await unexpectedCloseReconnects();
  await closeStopsReconnecting();
  await staleCloseDoesNotClobberNewerConnection();
  await queuesUntilOpenThenFlushesInOrder();
  await closeClearsTheQueue();
}

main()
  .then(() => {
    globalThis.WebSocket = realWebSocket;
    console.log(`CRP WebSocket client tests: ok (${checks} assertions)`);
  })
  .catch(error => {
    globalThis.WebSocket = realWebSocket;
    console.error(error);
    process.exit(1);
  });
