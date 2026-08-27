'use strict';
const { negotiate } = require('./upp_reference');

const STATES = Object.freeze({ IDLE: 'idle', RUNNING: 'running', STOPPED: 'stopped', CRASHED: 'crashed', INCOMPATIBLE: 'incompatible' });

class UppSession {
  constructor(role, manifest) { this.role = role; this.manifest = manifest; this.state = STATES.IDLE; this.lastHeartbeat = 0; this.abi = null; this.error = null; }
  acceptHello(remoteHello) {
    try { const welcome = negotiate({ payload: { role: this.role, manifest: this.manifest, capabilities: [] } }, remoteHello); this.abi = welcome.payload.abi; return welcome; }
    catch (e) { this.state = STATES.INCOMPATIBLE; this.error = e.message; throw e; }
  }
  apply(message) {
    if (!message || typeof message.type !== 'string') throw new TypeError('UPP message required');
    const p = message.payload || {};
    if (message.type === 'heartbeat') { if (!Number.isSafeInteger(p.seq) || p.seq < this.lastHeartbeat) throw new Error('heartbeat sequence out of order'); this.lastHeartbeat = p.seq; return this.state; }
    if (message.type === 'start') { if (this.state === STATES.CRASHED || this.state === STATES.INCOMPATIBLE) throw new Error(`cannot start from ${this.state}`); this.state = STATES.RUNNING; return this.state; }
    if (message.type === 'stop') { this.state = STATES.STOPPED; return this.state; }
    if (message.type === 'crash') { this.state = STATES.CRASHED; this.error = p.error || 'unknown crash'; return this.state; }
    return this.state;
  }
  snapshot() { return { role: this.role, state: this.state, abi: this.abi, lastHeartbeat: this.lastHeartbeat, error: this.error }; }
}

module.exports = { UppSession, STATES };
