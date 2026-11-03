'use strict';
const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const wsClients = new Set();

const parseAdmissions = text => {
const table = new Map();
for (let raw of String(text).split('\n')) {
  if (raw.endsWith('\r')) raw = raw.slice(0, -1);
  const line = raw.replace(/ +$/, '');
  if (line.trim() === '' || line.trimStart().startsWith('#')) continue;
  const fields = line.split('\t');
  if (fields.length !== 4) return { table: null, cause: 'admissions-malformed' };
  const verse = fields[0].trim(), peer = fields[1].trim(), secret = fields[2], capsRaw = fields[3].trim();
  if (!verse || !peer || !secret) return { table: null, cause: 'admissions-malformed' };
  const capabilities = capsRaw === '' ? [] : capsRaw.split(',').map(c => c.trim()).filter(Boolean);
  for (const c of capabilities) if (!ADMISSION_CAPABILITIES.has(c)) return { table: null, cause: 'admissions-unknown-capability' };
  const key = `${verse}\0${peer}`;
  if (table.has(key)) return { table: null, cause: 'admissions-duplicate' };
  table.set(key, { verse, peer, secret, capabilities });
}
return { table, cause: null };
};

/* The capability values this build knows.  An admission line naming anything
 * else voids the whole table (see parseAdmissions). */
const ADMISSION_CAPABILITIES = new Set(['signal']);

/* Canonical description, byte-identical to crp_admissions_describe() in
 * src/verse/crp.c: cause, line count, then each accepted line.  The crosscheck
 * corpus compares this string, so the two parsers are pinned against each other
 * on every edge case rather than merely "both parse the happy path". */
function describeAdmissions(result) {
  const parts = [`cause=${result.cause || '-'}`, `n=${result.table ? result.table.size : 0}`];
  if (result.table) for (const line of result.table.values()) parts.push([line.verse, line.peer, line.secret].join('\t'));
  return parts.join('|');
}

function createRelay(options = {}) {
  const verses = new Map();
  const friends = new Map();
  const sessions = new Map();
  const sessionEvents = new Map();
  const content = new Map();
  const packages = new Map();
  const revoked = new Set();
  const maxRevoked = options.maxRevokedTokens || 10000;
  const maxContent = options.maxContentBytes || 8 * 1024 * 1024;
  const requireWsAuth = options.requireWsAuth === true;
  const secret = options.secret || crypto.randomBytes(32).toString('hex');
  const makeToken = (verse, peer, capabilities = ['signal']) => { const exp = Date.now() + (options.tokenTtlMs || 5 * 60 * 1000); const body = Buffer.from(JSON.stringify({ verse, peer, capabilities, exp })).toString('base64url'); const sig = crypto.createHmac('sha256', secret).update(body).digest('base64url'); return `${body}.${sig}`; };
  const checkToken = (token, verse, peer, capability) => { try { const [body, sig] = String(token).split('.'); const expected = crypto.createHmac('sha256', secret).update(body).digest('base64url'); if (!body || !sig || !crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(expected))) return false; const p = JSON.parse(Buffer.from(body, 'base64url')); return p.verse === verse && p.peer === peer && p.exp > Date.now() && p.capabilities.includes(capability); } catch { return false; } };
  /* /portal is the only authorization entry point: every other capability
     endpoint trusts the token it mints, so the token *is* the authorization.
     The caller proves it may open a portal for (verse, peer) with an HMAC over
     the pair, keyed by the secret of THAT pair's registration entry -- the hub
     holds the table, the caller holds one line's secret.  Knowing some other
     pair's secret (or the old hub-wide CRP_ENROLL_SECRET, which now decides
     nothing) buys nothing.

     FAIL-CLOSED and SINGLE-SOURCED.  The table comes from exactly one place,
     chosen once at startup: options.admissions, else CRP_ADMISSIONS_FILE, else
     <hub>/portal_admissions.tsv where <hub> is the startup root or
     INIMERSE_HUB_DIR.  If the chosen source cannot be read the table is empty
     and every request is refused -- there is NO fallback to another source,
     because "file misconfigured -> quietly use the default path -> old table
     lets people through" is the worst kind of silence.

     A malformed file is an empty table, not a partial one: a pair written
     twice (rotation half-done, or a typo -- first-wins and last-wins are both
     wrong, and R2 is that disease already on the books), a line without four
     fields, a field of blanks, and an unknown capability value all void the
     whole file.  Group/other-readable is only a warning: plaintext secrets on
     disk are an honest boundary, and the operator should be able to see it.

     UNREGISTERED PAIR AND BAD PROOF ARE INDISTINGUISHABLE: same status, same
     headers, same body, and constant-time comparison so the timing agrees too.
     The cause goes to the log, never to the response -- a distinguishable
     refusal turns 404 into an enumeration oracle.  For the same reason the
     admissions test runs BEFORE the registry test: an unregistered pair must
     not learn whether a verse exists. */
  const ENROLL_REFUSED = Object.freeze({ error: 'invalid enrollment proof' });
  const hubDir = options.hubDir || process.env.INIMERSE_HUB_DIR || path.join(process.cwd(), 'universe');
  const admissionsSource =
    options.admissions !== undefined
      ? 'options.admissions'
      : process.env.CRP_ADMISSIONS_FILE
        ? process.env.CRP_ADMISSIONS_FILE
        : path.join(hubDir, 'portal_admissions.tsv');
  const logRefusal = cause => console.error(`crp_relay: portal refused cause=${cause}`);
  let admissionsCache = null;
  const loadAdmissions = () => {
    if (admissionsSource === 'options.admissions') {
      const list = Array.isArray(options.admissions) ? options.admissions : [];
      const text = list
        .map(e => [e.verse, e.peer, e.secret, Array.isArray(e.capabilities) ? e.capabilities.join(',') : String(e.capabilities || '')].join('\t'))
        .join('\n');
      return { ...parseAdmissions(text), basis: admissionsSource };
    }
    let st;
    try { st = fs.statSync(admissionsSource); }
    catch (e) {
      const cause = e && e.code === 'ENOENT' ? 'admissions-missing' : 'admissions-unreadable';
      return { table: new Map(), cause, basis: admissionsSource };
    }
    const stamp = `${st.mtimeMs}:${st.size}`;
    if (admissionsCache && admissionsCache.stamp === stamp) return admissionsCache.value;
    if ((st.mode & 0o044) !== 0) console.error('crp_relay: portal admissions cause=admissions-perms-loose');
    let value;
    try { value = { ...parseAdmissions(fs.readFileSync(admissionsSource, 'utf8')), basis: admissionsSource }; }
    catch { value = { table: new Map(), cause: 'admissions-unreadable', basis: admissionsSource }; }
    admissionsCache = { stamp, value };
    return value;
  };
  const enrollProof = (verse, peer, pairSecret) =>
    crypto.createHmac('sha256', pairSecret).update(`${verse}\0${peer}`).digest('base64url');
  const enrollOk = (auth, verse, peer, pairSecret) => {
    if (typeof auth !== 'string' || typeof pairSecret !== 'string' || !pairSecret) return false;
    const got = Buffer.from(auth); const want = Buffer.from(enrollProof(verse, peer, pairSecret));
    return got.length === want.length && crypto.timingSafeEqual(got, want);
  };
  const tokenTtlMs = options.tokenTtlMs || 5 * 60 * 1000;
  const ttl = options.ttlMs || 10 * 60 * 1000;
  const registryTtlMs = options.registryTtlMs || 30 * 60 * 1000;
  const pruneRegistry = () => {
    const cutoff = Date.now() - registryTtlMs;
    for (const [id, v] of verses) if ((v.updated || 0) < cutoff) verses.delete(id);
    for (const [id, v] of friends) if ((v.updated || 0) < cutoff) friends.delete(id);
  };
  const json = (res, code, value) => { res.writeHead(code, { 'content-type': 'application/json; charset=utf-8' }); res.end(JSON.stringify(value)); };
  const read = req => new Promise((resolve, reject) => { let b = ''; req.on('data', c => { b += c; if (b.length > 1024 * 1024) reject(new Error('payload too large')); }); req.on('end', () => { try { resolve(b ? JSON.parse(b) : {}); } catch { reject(new Error('invalid JSON')); } }); req.on('error', reject); });
  const server = http.createServer(async (req, res) => {
    try {
      if (req.method === 'POST' && req.url === '/register') {
        const p = await read(req); if (!p.id || !p.endpoint) return json(res, 400, { error: 'id and endpoint are required' });
        verses.set(String(p.id), { ...p, updated: Date.now() }); return json(res, 200, { ok: true });
      }
      if (req.method === 'GET' && req.url.startsWith('/find')) {
        pruneRegistry();
        const q = new URL(req.url, 'http://localhost').searchParams.get('q') || '';
        const result = [...verses.values()].filter(v => !q || String(v.id).includes(q) || String(v.name || '').toLowerCase().includes(q.toLowerCase())).map(({ updated, ...v }) => v);
        return json(res, 200, { items: result });
      }
      if (req.method === 'POST' && req.url === '/friends') {
        const p = await read(req); if (!p.id || !p.verse) return json(res, 400, { error: 'id and verse are required' });
        const id = String(p.id); friends.set(id, { id, verse: String(p.verse), name: String(p.name || id), endpoint: String(p.endpoint || ''), updated: Date.now() });
        return json(res, 200, { ok: true, id });
      }
      if (req.method === 'GET' && req.url.startsWith('/friends')) {
        pruneRegistry();
        const q = new URL(req.url, 'http://localhost').searchParams.get('verse') || '';
        const items = [...friends.values()].filter(f => !q || f.verse === q).map(({ updated, ...f }) => f);
        return json(res, 200, { items });
      }
      if (req.method === 'POST' && (req.url === '/route' || req.url === '/nat/candidate')) {
        const p = await read(req); if (!p.id || !p.endpoint) return json(res, 400, { error: 'id and endpoint are required' });
        const id = String(p.id); friends.set(id, { id, verse: String(p.verse || ''), name: String(p.name || id), endpoint: String(p.endpoint), updated: Date.now() }); return json(res, 200, { ok: true, id, endpoint: String(p.endpoint) });
      }
      if (req.method === 'GET' && req.url.startsWith('/route/')) {
        const id = decodeURIComponent(req.url.slice('/route/'.length)); const f = friends.get(id); if (!f) return json(res, 404, { error: 'route not found' }); return json(res, 200, { id, endpoint: f.endpoint });
      }
      if (req.method === 'GET' && req.url.startsWith('/nat/candidates')) {
        pruneRegistry(); return json(res, 200, { candidates: [...friends.values()].filter(f => f.endpoint).map(f => ({ id: f.id, endpoint: f.endpoint })) });
      }
      if (req.method === 'POST' && req.url === '/portal') {
        const p = await read(req);
        const admissions = loadAdmissions();
        if (!admissions.table) { logRefusal(admissions.cause); return json(res, 403, ENROLL_REFUSED); }
        const entry = typeof p.verse === 'string' && typeof p.peer === 'string'
          ? admissions.table.get(`${p.verse}\0${p.peer}`)
          : undefined;
        if (!entry) { logRefusal('unregistered-pair'); return json(res, 403, ENROLL_REFUSED); }
        if (!enrollOk(p.auth, p.verse, p.peer, entry.secret)) { logRefusal('bad-proof'); return json(res, 403, ENROLL_REFUSED); }
        /* The portal's scope is a pair of non-empty strings, and the verse must
           be one this hub has registered: `verses.has(p.verse)` is the
           reference's registry test.  A member that is not a string names no
           scope at all -- minting it would issue a token whose claim (a number,
           an array, "") no request carrying a string could ever match -- so it
           gets the same 404 as an unregistered verse, before anything is
           minted.  The engine asks the same question with the same answer
           (src/verse/crp.c registry_has_verse + a VJ_STR peer test), which is
           what the relay_portal corpus records pin.  This runs AFTER the
           admissions test on purpose: see the note above. */
        if (typeof p.verse !== 'string' || !verses.has(p.verse) || typeof p.peer !== 'string' || !p.peer)
          return json(res, 404, { error: 'verse not found' });
        const token = makeToken(p.verse, p.peer); return json(res, 200, { token, verse: p.verse, peer: p.peer, expires: Date.now() + tokenTtlMs });
      }
      if (req.method === 'POST' && req.url === '/signal') {
        const p = await read(req); if (!p.verse || !p.event) return json(res, 400, { error: 'verse and event are required' });
        if (!verses.has(p.verse)) return json(res, 404, { error: 'verse not found' });
        if (p.token && (revoked.has(String(p.token)) || !checkToken(p.token, p.verse, p.peer || '', 'signal'))) return json(res, 403, { error: 'invalid capability token' });
        const key = `${p.verse}\0${p.peer || ''}`; const seq = Number.isSafeInteger(p.seq) && p.seq >= 0 ? p.seq : ((sessions.get(key) || 0) + 1); sessions.set(key, seq);
        const events = sessionEvents.get(key) || []; events.push({ seq, event: p.event, data: p.data || {} }); while (events.length > 64) events.shift(); sessionEvents.set(key, events);
        return json(res, 202, { accepted: true, verse: p.verse, event: p.event, seq });
      }
      if (req.method === 'POST' && req.url === '/session/resume') {
        const p = await read(req); if (!p.verse || !p.peer || !p.token) return json(res, 400, { error: 'verse, peer and token are required' });
        if (!verses.has(String(p.verse)) || !checkToken(p.token, String(p.verse), String(p.peer), 'signal') || revoked.has(String(p.token))) return json(res, 403, { error: 'invalid capability token' });
        const key = `${p.verse}\0${p.peer}`; const seq = Number.isSafeInteger(p.seq) && p.seq >= 0 ? p.seq : 0; const prev = sessions.get(key) || 0;
        if (seq < prev && !p.replay) return json(res, 409, { error: 'session sequence out of order', lastSeq: prev });
        if (seq > prev) sessions.set(key, seq); const replay = (sessionEvents.get(key) || []).filter(e => e.seq > seq); return json(res, 200, { resumed: true, verse: p.verse, peer: p.peer, lastSeq: Math.max(prev, seq), replay });
      }
      if (req.method === 'POST' && req.url === '/session/stop') {
        const p = await read(req); if (!p.verse || !p.peer || !p.token || !checkToken(p.token, String(p.verse), String(p.peer), 'signal')) return json(res, 403, { error: 'invalid capability token' });
        const key = `${p.verse}\0${p.peer}`; sessions.set(key, sessions.get(key) || 0); return json(res, 200, { stopped: true, verse: p.verse, peer: p.peer });
      }
      if (req.method === 'POST' && req.url === '/revoke') {
        const p = await read(req); if (!p.token) return json(res, 400, { error: 'token is required' });
        const token = String(p.token); revoked.add(token);
        while (revoked.size > maxRevoked) revoked.delete(revoked.values().next().value);
        return json(res, 200, { revoked: true });
      }
      if (req.method === 'POST' && req.url === '/content') {
        const p = await read(req); if (typeof p.data !== 'string') return json(res, 400, { error: 'data must be a string' });
        const bytes = Buffer.from(p.data, 'utf8'); if (bytes.length > maxContent) return json(res, 413, { error: 'content too large' });
        const hash = crypto.createHash('sha256').update(bytes).digest('hex');
        if (p.hash && p.hash !== hash) return json(res, 400, { error: 'content hash mismatch' });
        content.set(hash, bytes); return json(res, 201, { hash, size: bytes.length, uri: `ref://sha256:${hash}` });
      }
      if (req.method === 'GET' && req.url.startsWith('/content/')) {
        const hash = req.url.slice('/content/'.length); if (!/^[a-f0-9]{64}$/.test(hash)) return json(res, 400, { error: 'invalid hash' });
        const bytes = content.get(hash); if (!bytes) return json(res, 404, { error: 'content not found' });
        res.writeHead(200, { 'content-type': 'application/octet-stream', 'content-length': bytes.length }); return res.end(bytes);
      }
      if (req.method === 'POST' && req.url === '/package') {
        const p = await read(req); if (!p.id || typeof p.data !== 'string') return json(res, 400, { error: 'id and data are required' });
        const bytes = Buffer.from(p.data, 'base64'); if (bytes.length > maxContent) return json(res, 413, { error: 'package too large' });
        const id = String(p.id); const hash = crypto.createHash('sha256').update(bytes).digest('hex'); content.set(`pkg:${id}`, bytes);
        packages.set(id, { id, hash, size: bytes.length, updated: Date.now(), forkOf: p.forkOf || null });
        return json(res, 201, { id, hash, size: bytes.length });
      }
      if (req.method === 'GET' && req.url.startsWith('/packages')) { const q = new URL(req.url, 'http://localhost').searchParams.get('q') || ''; const items = [...packages.values()].filter(x => !q || x.id.includes(q)).sort((a, b) => b.updated - a.updated); return json(res, 200, { items }); }
      if (req.method === 'POST' && req.url === '/package/fork') {
        const p = await read(req); if (!p.source || !p.id) return json(res, 400, { error: 'source and id are required' });
        const source = packages.get(String(p.source)); const bytes = content.get(`pkg:${p.source}`); if (!source || !bytes) return json(res, 404, { error: 'source package not found' });
        const id = String(p.id); content.set(`pkg:${id}`, bytes); packages.set(id, { id, hash: source.hash, size: source.size, updated: Date.now(), forkOf: source.id });
        return json(res, 201, { id, hash: source.hash, forkOf: source.id });
      }
      if (req.method === 'DELETE' && req.url.startsWith('/package/')) {
        const id = decodeURIComponent(req.url.slice('/package/'.length)); if (!packages.has(id)) return json(res, 404, { error: 'package not found' });
        packages.delete(id); content.delete(`pkg:${id}`); return json(res, 200, { deleted: true, id });
      }
      if (req.method === 'GET' && req.url.startsWith('/package/')) {
        const id = decodeURIComponent(req.url.slice('/package/'.length)); const bytes = content.get(`pkg:${id}`);
        if (!bytes) return json(res, 404, { error: 'package not found' });
        res.writeHead(200, { 'content-type': 'application/octet-stream', 'content-length': bytes.length }); return res.end(bytes);
      }
      json(res, 404, { error: 'not found' });
    } catch (e) { json(res, 400, { error: e.message }); }
  });
  server.on('upgrade', (req, socket) => {
    let parsed; try { parsed = new URL(req.url || '', 'http://localhost'); } catch { return socket.destroy(); }
    if (parsed.pathname !== '/ws') return socket.destroy();
    if (requireWsAuth) {
      const token = parsed.searchParams.get('token') || '';
      const verse = parsed.searchParams.get('verse') || '';
      const peer = parsed.searchParams.get('peer') || '';
      if (!verses.has(verse) || revoked.has(token) || !checkToken(token, verse, peer, 'signal')) return socket.destroy();
    }
    const key = req.headers['sec-websocket-key']; if (!key) return socket.destroy();
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`); wsClients.add(socket);
    let buf = Buffer.alloc(0);
    socket.on('data', chunk => {
      buf = Buffer.concat([buf, chunk]);
      while (buf.length >= 2) {
        const first = buf[0], second = buf[1], masked = !!(second & 0x80); let n = second & 127; let head = 2;
        if (n === 126) { if (buf.length < 4) break; n = buf.readUInt16BE(2); head = 4; }
        else if (n === 127) { if (buf.length < 10) break; n = Number(buf.readBigUInt64BE(2)); head = 10; }
        if (masked) head += 4; const total = head + n; if (buf.length < total) break;
        const key = masked ? buf.subarray(head - 4, head) : null; const payload = Buffer.from(buf.subarray(head, total)); buf = buf.subarray(total);
        if (key) for (let i = 0; i < payload.length; i++) payload[i] ^= key[i % 4];
        const opcode = first & 15; if (opcode === 8) { socket.end(); break; }
        if (opcode === 9) { socket.write(Buffer.from([0x8a, 0])); continue; }
        if (opcode !== 1) continue;
        try { const msg = JSON.parse(payload.toString()); if (!msg.verse || !msg.event) continue; const out = Buffer.from(JSON.stringify(msg)); const frame = out.length < 126 ? Buffer.concat([Buffer.from([0x81, out.length]), out]) : Buffer.concat([Buffer.from([0x81, 126, out.length >> 8, out.length & 255]), out]); for (const peer of wsClients) if (peer !== socket && !peer.destroyed) peer.write(frame); } catch {}
      }
    });
    socket.on('close', () => wsClients.delete(socket)); socket.on('error', () => wsClients.delete(socket));
  });
  return { server, verses, friends, sessions, packages, checkToken, revoked, wsClients, loadAdmissions, enrollProof };
}

if (require.main === module) createRelay().server.listen(Number(process.env.CRP_PORT || 8787), () => console.log('CRP relay listening'));
module.exports = { createRelay, parseAdmissions, describeAdmissions };
