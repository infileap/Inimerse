'use strict';
const fs = require('node:fs'); const path = require('node:path'); const crypto = require('node:crypto');
class HubClient {
  constructor(baseUrl, options = {}) { this.baseUrl = String(baseUrl).replace(/\/$/, ''); this.cacheDir = options.cacheDir || null; }
  async request(route, options = {}) { const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), options.timeoutMs || 10000); try { const r = await fetch(this.baseUrl + route, { ...options, signal: controller.signal }); if (!r.ok) throw new Error(`hub HTTP ${r.status}`); return r; } finally { clearTimeout(timer); } }
  async list(query = '') { const r = await this.request('/packages' + (query ? `?q=${encodeURIComponent(query)}` : '')); return r.json(); }
  async download(id, expectedHash = null) { const r = await this.request('/package/' + encodeURIComponent(id)); const data = Buffer.from(await r.arrayBuffer()); const hash = crypto.createHash('sha256').update(data).digest('hex'); if (expectedHash && hash.toLowerCase() !== String(expectedHash).toLowerCase()) throw new Error('hub package digest mismatch'); if (this.cacheDir) { fs.mkdirSync(this.cacheDir, { recursive: true }); fs.writeFileSync(path.join(this.cacheDir, hash + '.vverse'), data); } return data; }
  async fork(id, newId) { const r = await this.request('/package/fork', { method: 'POST', headers: {'content-type':'application/json'}, body: JSON.stringify({ source: id, id: newId }) }); return r.json(); }
}
module.exports = { HubClient };
