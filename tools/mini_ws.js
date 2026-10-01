'use strict';
// Minimal deterministic websocket client for the CRP flow test (no browser
// WebSocket event-order surprises): raw net socket, manual handshake and
// framing.  Server->client frames are unmasked (RFC 6455).
const net = require('node:net');
const crypto = require('node:crypto');

class MiniWs {
  constructor(host, port) {
    this.host = host;
    this.port = port;
    this.sock = null;
    this.buf = Buffer.alloc(0);
    this.pending = [];
    this.waiters = [];
    this.closed = false;
  }

  connect(pathWithQuery, timeoutMs = 4000) {
    return new Promise((resolve, reject) => {
      const key = crypto.randomBytes(16).toString('base64');
      const sock = net.createConnection({ host: this.host, port: this.port });
      this.sock = sock;
      const timer = setTimeout(() => { sock.destroy(); reject(new Error('connect timeout')); }, timeoutMs);
      let handshaken = false;
      sock.on('error', (e) => { if (!handshaken) { clearTimeout(timer); reject(e); } });
      sock.on('data', (chunk) => {
        this.buf = Buffer.concat([this.buf, chunk]);
        if (!handshaken) {
          const idx = this.buf.indexOf('\r\n\r\n');
          if (idx < 0) return;
          const head = this.buf.slice(0, idx).toString();
          if (!head.includes('101 Switching Protocols')) {
            clearTimeout(timer);
            sock.destroy();
            reject(new Error(`handshake failed: ${head.split('\r\n')[0]}`));
            return;
          }
          handshaken = true;
          clearTimeout(timer);
          this.buf = this.buf.slice(idx + 4);
          resolve(this);
        }
        this._drain();
      });
      sock.on('close', () => { this.closed = true; });
      sock.write(
        `GET ${pathWithQuery} HTTP/1.1\r\nHost: ${this.host}\r\nUpgrade: websocket\r\n` +
        `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
    });
  }

  _drain() {
    for (;;) {
      if (this.buf.length < 2) return;
      const opcode = this.buf[0] & 0x0f;
      let len = this.buf[1] & 0x7f;
      let off = 2;
      if (len === 126) { if (this.buf.length < 4) return; len = this.buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (this.buf.length < 10) return; len = Number(this.buf.readBigUInt64BE(2)); off = 10; }
      if (this.buf.length < off + len) return;
      const payload = this.buf.slice(off, off + len);
      this.buf = this.buf.slice(off + len);
      if (opcode === 1 || opcode === 2) {
        const text = payload.toString();
        const w = this.waiters.shift();
        if (w) w(text); else this.pending.push(text);
      }
    }
  }

  sendText(text) {
    const payload = Buffer.from(text);
    const mask = crypto.randomBytes(4);
    let header;
    if (payload.length < 126) header = Buffer.from([0x81, 0x80 | payload.length]);
    else header = Buffer.concat([Buffer.from([0x81, 0x80 | 126, payload.length >> 8, payload.length & 0xff])]);
    const masked = Buffer.alloc(payload.length);
    for (let i = 0; i < payload.length; i++) masked[i] = payload[i] ^ mask[i & 3];
    this.sock.write(Buffer.concat([header, mask, masked]));
  }

  nextMessage(timeoutMs = 3000) {
    if (this.pending.length) return Promise.resolve(this.pending.shift());
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('timeout waiting for message')), timeoutMs);
      this.waiters.push((text) => { clearTimeout(timer); resolve(text); });
    });
  }

  close() { if (this.sock) this.sock.destroy(); }
}

module.exports = { MiniWs };
