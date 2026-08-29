'use strict';
const { negotiate } = require('./upp_reference');

const STATES = Object.freeze({ IDLE: 'idle', RUNNING: 'running', STOPPED: 'stopped', CRASHED: 'crashed', INCOMPATIBLE: 'incompatible' });

class UppSession {
  constructor(role, manifest) { if (!['host','verse','client'].includes(role)) throw new Error('invalid role'); this.role = role; this.manifest = manifest; this.state = STATES.IDLE; this.lastHeartbeat = 0; this.lastHeartbeatAt = 0; this.abi = null; this.error = null; }
  acceptHello(remoteHello) {
    try { const welcome = negotiate({ payload: { role: this.role, manifest: this.manifest, capabilities: [] } }, remoteHello); this.abi = welcome.payload.abi; return welcome; }
    catch (e) { this.state = STATES.INCOMPATIBLE; this.error = e.message; throw e; }
  }
  apply(message) {
    if (!message || typeof message.type !== 'string') throw new TypeError('UPP message required');
    const p = message.payload || {};
    if (message.type === 'heartbeat') {
      if (!Number.isSafeInteger(p.seq) || p.seq < this.lastHeartbeat) throw new Error('heartbeat sequence out of order');
      if (Number.isFinite(p.timestamp) && this.lastHeartbeatAt > 0 && p.timestamp < this.lastHeartbeatAt) throw new Error('heartbeat timestamp out of order');
      this.lastHeartbeat = p.seq; this.lastHeartbeatAt = Number.isFinite(p.timestamp) ? p.timestamp : Date.now(); return this.state;
    }
    if (message.type === 'start') { if (this.state === STATES.RUNNING) return this.state; if (this.state === STATES.CRASHED || this.state === STATES.INCOMPATIBLE) throw new Error(`cannot start from ${this.state}`); this.state = STATES.RUNNING; return this.state; }
    if (message.type === 'stop') { this.state = STATES.STOPPED; return this.state; }
    if (message.type === 'crash') { this.state = STATES.CRASHED; this.error = p.error || 'unknown crash'; return this.state; }
    return this.state;
  }
  reset() { this.state = STATES.IDLE; this.error = null; this.lastHeartbeat = 0; this.lastHeartbeatAt = 0; return this.state; }
  isHeartbeatStale(now = Date.now(), timeoutMs = 15000) { return this.lastHeartbeatAt > 0 && now - this.lastHeartbeatAt > timeoutMs; }
  checkHeartbeat(now = Date.now(), timeoutMs = 15000) { if (this.state === STATES.RUNNING && this.isHeartbeatStale(now, timeoutMs)) { this.state = STATES.CRASHED; this.error = 'heartbeat timeout'; return false; } return true; }
  recover() { if (this.state !== STATES.CRASHED && this.state !== STATES.STOPPED) throw new Error(`cannot recover from ${this.state}`); this.state = STATES.IDLE; this.error = null; this.lastHeartbeat = 0; this.lastHeartbeatAt = 0; return this.state; }
  snapshot() { return Object.freeze({ role: this.role, state: this.state, abi: this.abi, lastHeartbeat: this.lastHeartbeat, lastHeartbeatAt: this.lastHeartbeatAt, error: this.error }); }
}

module.exports = { UppSession, STATES };
