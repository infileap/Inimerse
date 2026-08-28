'use strict';
class CrpWebSocketClient {
  constructor(url, options = {}) { this.url = String(url); this.retries = Math.max(0, options.retries ?? 3); this.backoffMs = Math.max(1, options.backoffMs ?? 100); this.socket = null; this.closed = false; this.queue = []; this.onMessage = options.onMessage || (() => {}); this._connectPromise = null; this._generation = 0; }
  async connect() { if (this._connectPromise) return this._connectPromise; this.closed = false; const generation = ++this._generation; this._connectPromise = this._connectLoop(generation).finally(() => { this._connectPromise = null; }); return this._connectPromise; }
  async _connectLoop(generation) {
    let attempt = 0;
    while (!this.closed && generation === this._generation) {
      try {
        await new Promise((resolve, reject) => {
          const ws = new WebSocket(this.url); this.socket = ws; let settled = false;
          const fail = () => { if (!settled) { settled = true; reject(new Error('WebSocket connection failed')); } };
          ws.addEventListener('open', () => { if (settled) return; settled = true; attempt = 0; while (this.queue.length && ws.readyState === WebSocket.OPEN) ws.send(this.queue.shift()); resolve(); }, { once: true });
          ws.addEventListener('message', e => this.onMessage(e.data)); ws.addEventListener('error', fail, { once: true });
          ws.addEventListener('close', () => { if (this.socket === ws) this.socket = null; if (!this.closed && generation === this._generation) this.connect().catch(() => {}); }, { once: true });
        }); return this;
      } catch (e) { if (this.closed || generation !== this._generation || attempt++ >= this.retries) throw e; await new Promise(r => setTimeout(r, this.backoffMs * 2 ** (attempt - 1))); }
    }
    return this;
  }
  send(value) { const data = typeof value === 'string' ? value : JSON.stringify(value); if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(data); else this.queue.push(data); }
  close() { this.closed = true; ++this._generation; this.queue.length = 0; if (this.socket) this.socket.close(); this.socket = null; }
}
module.exports = { CrpWebSocketClient };
