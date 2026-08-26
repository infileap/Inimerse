#!/usr/bin/env node
'use strict';

const MAX_FRAME_BYTES = 1024 * 1024;
const TYPES = new Set(['FIND', 'PORTAL', 'SIGNAL']);

function obj(v, label) {
  if (!v || typeof v !== 'object' || Array.isArray(v)) throw new TypeError(`${label} must be an object`);
  return v;
}

function frame(type, payload = {}, id = '') {
  if (!TYPES.has(type)) throw new Error(`unsupported CRP type: ${type}`);
  const out = { crp: 1, type, payload: obj(payload, 'payload') };
  if (id) out.id = String(id);
  return out;
}

function find(query = '', options = {}) {
  if (typeof query !== 'string') throw new TypeError('FIND query must be a string');
  const p = { query, limit: options.limit ?? 50, cursor: options.cursor ?? null };
  if (!Number.isInteger(p.limit) || p.limit < 1 || p.limit > 1000) throw new RangeError('FIND limit must be 1..1000');
  if (p.cursor !== null && typeof p.cursor !== 'string') throw new TypeError('FIND cursor must be a string or null');
  return frame('FIND', p, options.id);
}

function portal(verse, peer, options = {}) {
  for (const [name, value] of Object.entries({ verse, peer })) if (typeof value !== 'string' || !value.trim()) throw new TypeError(`PORTAL ${name} is required`);
  return frame('PORTAL', { verse, peer, token: options.token || null, expires: options.expires ?? null }, options.id);
}

function signal(verse, event, data = {}, options = {}) {
  if (typeof verse !== 'string' || !verse.trim()) throw new TypeError('SIGNAL verse is required');
  if (typeof event !== 'string' || !event.trim()) throw new TypeError('SIGNAL event is required');
  return frame('SIGNAL', { verse, event, data: obj(data, 'SIGNAL data'), timestamp: options.timestamp ?? Date.now() }, options.id);
}

function encode(message) {
  const m = obj(message, 'message');
  if (m.crp !== 1 || !TYPES.has(m.type)) throw new Error('invalid CRP frame');
  const line = JSON.stringify(m);
  if (Buffer.byteLength(line, 'utf8') > MAX_FRAME_BYTES) throw new Error('CRP frame exceeds 1 MiB');
  return `${line}\n`;
}

function decode(line) {
  if (Buffer.byteLength(line, 'utf8') > MAX_FRAME_BYTES) throw new Error('CRP frame exceeds 1 MiB');
  const m = obj(JSON.parse(line), 'message');
  if (m.crp !== 1 || !TYPES.has(m.type)) throw new Error('invalid CRP frame');
  return m;
}

module.exports = { MAX_FRAME_BYTES, frame, find, portal, signal, encode, decode };

