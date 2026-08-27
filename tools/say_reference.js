'use strict';
const fs = require('node:fs');
const TARGETS = new Set(['console','log','chat','ui','world','character','dialogue','system','file','json','ai']);
class OutputStream {
  constructor(handler, options = {}) { this.handler = handler; this.maxQueue = options.maxQueue || 1024; this.queue = []; this.closed = false; }
  write(event) { if (this.closed) throw new Error('stream closed'); if (this.queue.length >= this.maxQueue) throw new Error('stream backpressure'); this.queue.push(event); this.handler(event); return true; }
  close() { this.closed = true; this.queue.length = 0; }
}
function createRouter(streams = {}) { return { say(target, text, meta = {}) { if (!TARGETS.has(target)) throw new Error(`unknown say target: ${target}`); if (typeof text !== 'string') throw new TypeError('say text must be a string'); const event = { target, text, meta, timestamp: Date.now() }; const stream = streams[target]; if (!stream) throw new Error(`no stream for target: ${target}`); stream.write(event); return event; } }; }
function desugarSay(source) { return String(source).replace(/say@(console|log|chat|ui|world|character|dialogue|system|file|json|ai)\s+([^\n]+)/g, 'say_target("$1", $2)'); }
module.exports = { OutputStream, createRouter, desugarSay, TARGETS };
