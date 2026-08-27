'use strict';
const http = require('node:http');
const crypto = require('node:crypto');

function createRelay(options = {}) {
  const verses = new Map();
  const content = new Map();
  const revoked = new Set();
  const maxRevoked = options.maxRevokedTokens || 10000;
  const maxContent = options.maxContentBytes || 8 * 1024 * 1024;
  const secret = options.secret || crypto.randomBytes(32).toString('hex');
  const makeToken = (verse, peer, capabilities = ['signal']) => { const exp = Date.now() + (options.tokenTtlMs || 5 * 60 * 1000); const body = Buffer.from(JSON.stringify({ verse, peer, capabilities, exp })).toString('base64url'); const sig = crypto.createHmac('sha256', secret).update(body).digest('base64url'); return `${body}.${sig}`; };
  const checkToken = (token, verse, peer, capability) => { try { const [body, sig] = String(token).split('.'); const expected = crypto.createHmac('sha256', secret).update(body).digest('base64url'); if (!body || !sig || !crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(expected))) return false; const p = JSON.parse(Buffer.from(body, 'base64url')); return p.verse === verse && p.peer === peer && p.exp > Date.now() && p.capabilities.includes(capability); } catch { return false; } };
  const ttl = options.ttlMs || 10 * 60 * 1000;
  const json = (res, code, value) => { res.writeHead(code, { 'content-type': 'application/json; charset=utf-8' }); res.end(JSON.stringify(value)); };
  const read = req => new Promise((resolve, reject) => { let b = ''; req.on('data', c => { b += c; if (b.length > 1024 * 1024) reject(new Error('payload too large')); }); req.on('end', () => { try { resolve(b ? JSON.parse(b) : {}); } catch { reject(new Error('invalid JSON')); } }); req.on('error', reject); });
  const server = http.createServer(async (req, res) => {
    try {
      if (req.method === 'POST' && req.url === '/register') {
        const p = await read(req); if (!p.id || !p.endpoint) return json(res, 400, { error: 'id and endpoint are required' });
        verses.set(String(p.id), { ...p, updated: Date.now() }); return json(res, 200, { ok: true });
      }
      if (req.method === 'GET' && req.url.startsWith('/find')) {
        const q = new URL(req.url, 'http://localhost').searchParams.get('q') || '';
        const result = [...verses.values()].filter(v => !q || String(v.id).includes(q) || String(v.name || '').toLowerCase().includes(q.toLowerCase())).map(({ updated, ...v }) => v);
        return json(res, 200, { items: result });
      }
      if (req.method === 'POST' && req.url === '/portal') {
        const p = await read(req); if (!verses.has(p.verse) || !p.peer) return json(res, 404, { error: 'verse not found' });
        const token = makeToken(p.verse, p.peer); return json(res, 200, { token, verse: p.verse, peer: p.peer, expires: Date.now() + ttl });
      }
      if (req.method === 'POST' && req.url === '/signal') {
        const p = await read(req); if (!p.verse || !p.event) return json(res, 400, { error: 'verse and event are required' });
        if (!verses.has(p.verse)) return json(res, 404, { error: 'verse not found' });
        if (p.token && (revoked.has(String(p.token)) || !checkToken(p.token, p.verse, p.peer || '', 'signal'))) return json(res, 403, { error: 'invalid capability token' });
        return json(res, 202, { accepted: true, verse: p.verse, event: p.event });
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
        const hash = crypto.createHash('sha256').update(bytes).digest('hex'); content.set(`pkg:${p.id}`, bytes);
        return json(res, 201, { id: String(p.id), hash, size: bytes.length });
      }
      if (req.method === 'GET' && req.url.startsWith('/package/')) {
        const id = decodeURIComponent(req.url.slice('/package/'.length)); const bytes = content.get(`pkg:${id}`);
        if (!bytes) return json(res, 404, { error: 'package not found' });
        res.writeHead(200, { 'content-type': 'application/octet-stream', 'content-length': bytes.length }); return res.end(bytes);
      }
      json(res, 404, { error: 'not found' });
    } catch (e) { json(res, 400, { error: e.message }); }
  });
  return { server, verses, checkToken, revoked };
}

if (require.main === module) createRelay().server.listen(Number(process.env.CRP_PORT || 8787), () => console.log('CRP relay listening'));
module.exports = { createRelay };
