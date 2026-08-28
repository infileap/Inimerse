'use strict';
class CrpWebSocketClient {
  constructor(url, options = {}) { this.url = String(url); this.retries = options.retries ?? 3; this.backoffMs = options.backoffMs ?? 100; this.attempt = 0; this.socket = null; this.closed = false; this.queue = []; this.onMessage = options.onMessage || (() => {}); }
  async connect() {
    this.closed = false; this.attempt = 0;
    while (!this.closed) {
      try { await new Promise((resolve, reject) => { const ws = new WebSocket(this.url); this.socket = ws; ws.addEventListener('open', () => { this.attempt = 0; while (this.queue.length) ws.send(this.queue.shift()); resolve(); }, { once: true }); ws.addEventListener('message', e => this.onMessage(e.data)); ws.addEventListener('error', () => reject(new Error('WebSocket connection failed')), { once: true }); }); return this; }
      catch (e) { if (this.closed || this.attempt++ >= this.retries) throw e; await new Promise(r => setTimeout(r, this.backoffMs * 2 ** (this.attempt - 1))); }
    }
    return this;
  }
  send(value) { const data = typeof value === 'string' ? value : JSON.stringify(value); if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(data); else this.queue.push(data); }
  close() { this.closed = true; this.queue.length = 0; if (this.socket) this.socket.close(); }
}
module.exports = { CrpWebSocketClient };
