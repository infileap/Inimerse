#!/usr/bin/env node
/*
 * UPP v1 local reference implementation.
 *
 * The wire format is newline-delimited JSON (JSONL).  It deliberately has no
 * transport dependency so the same handshake can run over stdio, TCP or a
 * test loopback.  Every frame is self-contained and bounded by MAX_FRAME_BYTES.
 */
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const VERSION = 1;
const MAX_FRAME_BYTES = 1024 * 1024;
const ROLES = new Set(['host', 'verse', 'client']);
const CONTROL_TYPES = new Set(['heartbeat', 'start', 'stop', 'log', 'crash', 'incompatible']);

function asObject(value, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new TypeError(`${label} must be an object`);
  }
  return value;
}

function validateManifest(manifest) {
  const m = asObject(manifest, 'manifest');
  for (const key of ['id', 'name', 'version', 'engine', 'entry']) {
    if (typeof m[key] !== 'string' || !m[key].trim()) throw new Error(`manifest.${key} is required`);
  }
  if (!/^[a-z0-9][a-z0-9._-]{0,63}$/i.test(m.id)) throw new Error('manifest.id has invalid characters');
  if (!/^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/.test(m.version)) throw new Error('manifest.version must be semver');
  if (m.files !== undefined) asObject(m.files, 'manifest.files');
  if (m.capabilities !== undefined && (!Array.isArray(m.capabilities) || m.capabilities.some(x => typeof x !== 'string'))) {
    throw new Error('manifest.capabilities must be an array of strings');
  }
  if (m.abi !== undefined && (!Number.isInteger(m.abi) || m.abi < 1)) throw new Error('manifest.abi must be a positive integer');
  if (m.abiRange !== undefined && (typeof m.abiRange !== 'string' || !/^\d+(?:\.\.\d+)?$/.test(m.abiRange))) throw new Error('manifest.abiRange must be N or N..M');
  return Object.freeze({ ...m });
}

function frame(type, payload = {}, id = '') {
  if (typeof type !== 'string' || !type) throw new TypeError('frame type is required');
  const out = { upp: VERSION, type, id: id || undefined, payload };
  if (!out.id) delete out.id;
  return out;
}

function encode(message) {
  const value = asObject(message, 'message');
  if (value.upp !== VERSION) throw new Error(`unsupported UPP version: ${value.upp}`);
  const line = JSON.stringify(value);
  const size = Buffer.byteLength(line, 'utf8');
  if (size > MAX_FRAME_BYTES) throw new Error('UPP frame exceeds 1 MiB');
  return `${line}\n`;
}

function decodeLine(line) {
  if (Buffer.byteLength(line, 'utf8') > MAX_FRAME_BYTES) throw new Error('UPP frame exceeds 1 MiB');
  const value = asObject(JSON.parse(line), 'message');
  if (value.upp !== VERSION || typeof value.type !== 'string') throw new Error('invalid UPP frame header');
  return value;
}

function createDecoder(onMessage) {
  let pending = '';
  return {
    push(chunk) {
      pending += Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
      if (Buffer.byteLength(pending, 'utf8') > MAX_FRAME_BYTES * 2) throw new Error('UPP input buffer is too large');
      const lines = pending.split('\n');
      pending = lines.pop();
      for (const line of lines) if (line.trim()) onMessage(decodeLine(line.replace(/\r$/, '')));
    },
    end() { if (pending.trim()) onMessage(decodeLine(pending)); pending = ''; },
  };
}

function hello(role, manifest, capabilities = []) {
  if (!ROLES.has(role)) throw new Error(`invalid role: ${role}`);
  const m = validateManifest(manifest);
  const caps = [...new Set(capabilities.filter(x => typeof x === 'string'))].sort();
  return frame('hello', { role, manifest: m, capabilities: caps });
}

function negotiate(local, remote) {
  const a = asObject(local, 'local hello').payload || local;
  const b = asObject(remote, 'remote hello').payload || remote;
  if (a.role === b.role) throw new Error('UPP peers must use different roles');
  const av = new Set(a.capabilities || []);
  const capabilities = (b.capabilities || []).filter(x => av.has(x)).sort();
  const range = m => { const v=m?.abi || 1, r=m?.abiRange || String(v), [lo,hi]=(r.includes('..')?r:`${r}..${r}`).split('..').map(Number); return {lo,hi}; };
  const ra=range(a.manifest), rb=range(b.manifest), abi=Math.max(ra.lo,rb.lo);
  if (abi > Math.min(ra.hi,rb.hi)) throw new Error(`incompatible ABI ranges: ${a.manifest?.abiRange || ra.lo} vs ${b.manifest?.abiRange || rb.lo}`);
  return frame('welcome', { protocol: VERSION, peerRole: b.role, capabilities, abi });
}

function control(type, payload = {}, id = '') {
  if (!CONTROL_TYPES.has(type)) throw new Error(`unsupported UPP control type: ${type}`);
  return frame(type, asObject(payload, `${type} payload`), id);
}

function heartbeat(seq = 0, timestamp = Date.now()) {
  if (!Number.isSafeInteger(seq) || seq < 0) throw new TypeError('heartbeat sequence must be a non-negative integer');
  if (!Number.isFinite(timestamp)) throw new TypeError('heartbeat timestamp must be finite');
  return control('heartbeat', { seq, timestamp });
}

function start(entry, args = []) {
  if (typeof entry !== 'string' || !entry.trim()) throw new TypeError('start entry is required');
  if (!Array.isArray(args) || args.some(x => typeof x !== 'string')) throw new TypeError('start args must be strings');
  return control('start', { entry, args });
}

function stop(reason = 'requested') {
  if (typeof reason !== 'string' || !reason.trim()) throw new TypeError('stop reason is required');
  return control('stop', { reason });
}

function log(level, message) {
  if (!['debug', 'info', 'warn', 'error'].includes(level)) throw new TypeError('invalid UPP log level');
  if (typeof message !== 'string') throw new TypeError('log message must be a string');
  return control('log', { level, message, timestamp: Date.now() });
}

function crash(error, exitCode = null) {
  if (typeof error !== 'string' || !error.trim()) throw new TypeError('crash error is required');
  if (exitCode !== null && (!Number.isInteger(exitCode) || exitCode < 0)) throw new TypeError('invalid crash exit code');
  return control('crash', { error, exitCode, timestamp: Date.now() });
}

function incompatible(required, actual = VERSION) {
  if (typeof required !== 'number' || !Number.isInteger(required)) throw new TypeError('required protocol must be an integer');
  if (typeof actual !== 'number' || !Number.isInteger(actual)) throw new TypeError('actual protocol must be an integer');
  return control('incompatible', { required, actual });
}

function readManifest(path) {
  const raw = asObject(JSON.parse(fs.readFileSync(path, 'utf8')), 'manifest');
  // VDP manifests predating UPP used `main` and omitted descriptive fields.
  // Normalize those files at the boundary while keeping validateManifest strict.
  return validateManifest({
    name: raw.name || raw.id,
    version: raw.version || '0.0.0',
    engine: raw.engine || 'inimerse',
    entry: raw.entry || raw.main,
    ...raw,
  });
}

function sha256File(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function generateManifest(root, options = {}) {
  if (typeof root !== 'string' || !root.trim()) throw new TypeError('project root is required');
  const base = path.resolve(root);
  if (!fs.statSync(base).isDirectory()) throw new Error('project root must be a directory');
  const files = {};
  const walk = dir => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      if (entry.name === '.git' || entry.name === 'node_modules') continue;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.isFile()) files[path.relative(base, full).split(path.sep).join('/')] = sha256File(full);
    }
  };
  walk(base);
  const raw = fs.existsSync(path.join(base, 'manifest.json'))
    ? JSON.parse(fs.readFileSync(path.join(base, 'manifest.json'), 'utf8')) : {};
  const manifest = validateManifest({
    id: options.id || raw.id || path.basename(base).toLowerCase().replace(/[^a-z0-9._-]+/g, '-'),
    name: options.name || raw.name || path.basename(base),
    version: options.version || raw.version || '0.1.0',
    engine: options.engine || raw.engine || 'inimerse',
    entry: options.entry || raw.entry || raw.main || 'main.im',
    abi: options.abi || raw.abi || 1,
    capabilities: options.capabilities || raw.capabilities || [],
    interfaces: options.interfaces || raw.interfaces || [],
    files,
  });
  return manifest;
}

if (require.main === module) {
  const path = process.argv[2];
  if (!path) { console.error('usage: node tools/upp_reference.js <manifest.json> | --generate <project-dir>'); process.exit(2); }
  try {
    if (path === '--generate') {
      const generated = generateManifest(process.argv[3]);
      process.stdout.write(`${JSON.stringify(generated, null, 2)}\n`);
    } else process.stdout.write(encode(hello('host', readManifest(path), ['heartbeat', 'shutdown'])));
  }
  catch (err) { console.error(`UPP manifest error: ${err.message}`); process.exit(1); }
}

module.exports = { VERSION, MAX_FRAME_BYTES, frame, encode, decodeLine, createDecoder, validateManifest, hello, negotiate, control, heartbeat, start, stop, log, crash, incompatible, readManifest, generateManifest };
