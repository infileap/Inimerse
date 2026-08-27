'use strict';
const crypto = require('node:crypto');

class CrpClient {
  constructor(baseUrl, options = {}) {
    this.baseUrl = String(baseUrl).replace(/\/$/, '');
    this.retries = options.retries ?? 3;
    this.backoffMs = options.backoffMs ?? 100;
  }
  async request(path, init = {}, signal) {
    let last;
    for (let attempt = 0; attempt <= this.retries; attempt++) {
      try {
        const res = await fetch(this.baseUrl + path, { ...init, signal });
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || `CRP HTTP ${res.status}`);
        return body;
      } catch (e) {
        last = e;
        if (signal?.aborted || attempt === this.retries) throw last;
        await new Promise((resolve, reject) => { const t = setTimeout(resolve, this.backoffMs * 2 ** attempt); signal?.addEventListener('abort', () => { clearTimeout(t); reject(signal.reason || e); }, { once: true }); });
      }
    }
    throw last;
  }
  find(query = '', signal) { return this.request(`/find?q=${encodeURIComponent(query)}`, {}, signal); }
  portal(verse, peer, signal) { return this.request('/portal', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ verse, peer }) }, signal); }
  signal(verse, event, data = {}, signal) { return this.request('/signal', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ verse, event, data }) }, signal); }
  revoke(token, signal) { return this.request('/revoke', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token }) }, signal); }
  publishPackage(id, bytes, signal) { const data = Buffer.from(bytes).toString('base64'); return this.request('/package', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ id, data }) }, signal); }
  async downloadPackage(id, signal) { const res = await fetch(this.baseUrl + '/package/' + encodeURIComponent(id), { signal }); if (!res.ok) throw new Error(`package HTTP ${res.status}`); return Buffer.from(await res.arrayBuffer()); }
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
