'use strict';
const crypto = require('node:crypto');

class CrpClient {
  constructor(baseUrl, options = {}) {
    this.baseUrl = String(baseUrl).replace(/\/$/, '');
    this.retries = options.retries ?? 3;
    this.backoffMs = options.backoffMs ?? 100;
    this.packageCache = new Map(); this.maxCacheBytes = options.maxCacheBytes ?? 128 * 1024 * 1024; this.cacheBytes = 0;
  }
  async request(path, init = {}, signal) {
    let last;
    const method = String(init.method || 'GET').toUpperCase();
    const attempts = method === 'GET' || method === 'HEAD' ? this.retries : 0;
    for (let attempt = 0; attempt <= attempts; attempt++) {
      try {
        const res = await fetch(this.baseUrl + path, { ...init, signal });
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || `CRP HTTP ${res.status}`);
        return body;
      } catch (e) {
        last = e;
        if (signal?.aborted || attempt === attempts) throw last;
        await new Promise((resolve, reject) => { const t = setTimeout(resolve, this.backoffMs * 2 ** attempt); signal?.addEventListener('abort', () => { clearTimeout(t); reject(signal.reason || e); }, { once: true }); });
      }
    }
    throw last;
  }
  find(query = '', signal) { return this.request(`/find?q=${encodeURIComponent(query)}`, {}, signal); }
  registerFriend(id, verse, endpoint = '', name = '', signal) { return this.request('/friends', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ id, verse, endpoint, name }) }, signal); }
  listFriends(verse = '', signal) { return this.request('/friends' + (verse ? `?verse=${encodeURIComponent(verse)}` : ''), {}, signal); }
  portal(verse, peer, signal) { return this.request('/portal', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ verse, peer }) }, signal); }
  signal(verse, event, data = {}, signal) { return this.request('/signal', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ verse, event, data }) }, signal); }
  resumeSession(verse, peer, token, seq = 0, signal) { return this.request('/session/resume', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ verse, peer, token, seq }) }, signal); }
  revoke(token, signal) { return this.request('/revoke', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token }) }, signal); }
  publishPackage(id, bytes, signal) { const data = Buffer.from(bytes).toString('base64'); return this.request('/package', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ id, data }) }, signal); }
  listPackages(signal) { return this.request('/packages', {}, signal); }
  cachedPackage(id) { return this.packageCache.get(String(id)) || null; }
  hasCachedPackage(id) { return this.packageCache.has(String(id)); }
  cacheStats() { return { entries: this.packageCache.size, bytes: this.cacheBytes, maxBytes: this.maxCacheBytes }; }
  clearPackageCache() { this.packageCache.clear(); this.cacheBytes = 0; }
  forkPackage(source, id, signal) { return this.request('/package/fork', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ source, id }) }, signal); }
  deletePackage(id, signal) { this.packageCache.delete(String(id)); return this.request('/package/' + encodeURIComponent(id), { method: 'DELETE' }, signal); }
  async downloadPackage(id, options = {}, signal) {
    if (typeof options === 'string') options = { hash: options };
    const maxBytes = options.maxBytes ?? 64 * 1024 * 1024;
    if (options.cache !== false && this.packageCache.has(String(id))) { const cached = this.packageCache.get(String(id)); if (options.hash && crypto.createHash('sha256').update(cached).digest('hex') !== options.hash) throw new Error('package hash mismatch'); return cached; }
    const res = await fetch(this.baseUrl + '/package/' + encodeURIComponent(id), { signal }); if (!res.ok) throw new Error(`package HTTP ${res.status}`);
    const length = Number(res.headers.get('content-length') || 0); if (length > maxBytes) throw new Error('package too large');
    const data = Buffer.from(await res.arrayBuffer()); if (data.length > maxBytes) throw new Error('package too large');
    if (options.hash) { const got = crypto.createHash('sha256').update(data).digest('hex'); if (got !== options.hash) throw new Error('package hash mismatch'); }
    if (options.cache !== false && data.length <= this.maxCacheBytes) { while (this.cacheBytes + data.length > this.maxCacheBytes && this.packageCache.size) { const first = this.packageCache.keys().next().value; this.cacheBytes -= this.packageCache.get(first).length; this.packageCache.delete(first); } this.packageCache.set(String(id), data); this.cacheBytes += data.length; }
    return data;
  }
  async fetchContent(hash, sources = [this.baseUrl], signal) {
    if (!/^[a-f0-9]{64}$/.test(hash)) throw new TypeError('invalid SHA-256 hash');
    let last;
    for (const source of sources) {
      try {
        const res = await fetch(String(source).replace(/\/$/, '') + '/content/' + hash, { signal });
        if (!res.ok) throw new Error(`content source HTTP ${res.status}`);
        const data = Buffer.from(await res.arrayBuffer());
        const got = crypto.createHash('sha256').update(data).digest('hex');
        if (got !== hash) throw new Error('content hash mismatch');
        return data;
      } catch (e) { last = e; if (signal?.aborted) throw e; }
    }
    throw new Error(`all content sources failed: ${last?.message || 'unknown error'}`);
  }
}

module.exports = { CrpClient };
