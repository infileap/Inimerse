'use strict';
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');
const { validate, writeSignature } = require('./vverse_validate');

function pack(root, output) {
  const base = path.resolve(root); writeSignature(base); const checked = validate(base, { strictStructure: true, requireSignature: true, requireCompleteSignature: true });
  const entries = {};
  for (const name of Object.keys(checked.files).sort()) entries[name] = fs.readFileSync(path.join(base, name)).toString('base64');
  entries['signatures/sha256.json'] = fs.readFileSync(path.join(base, 'signatures', 'sha256.json')).toString('base64');
  const edPath = path.join(base, 'signatures', 'ed25519.json');
  if (fs.existsSync(edPath)) entries['signatures/ed25519.json'] = fs.readFileSync(edPath).toString('base64');
  const raw = JSON.stringify({ format: 'vverse-1', files: entries });
  fs.writeFileSync(output, zlib.gzipSync(raw, { mtime: 0 }));
  return { output, bytes: fs.statSync(output).size, files: Object.keys(entries).length };
}

function unpack(input, destination) {
  const obj = JSON.parse(zlib.gunzipSync(fs.readFileSync(input), { to: 'string' }));
  if (obj.format !== 'vverse-1' || !obj.files || typeof obj.files !== 'object') throw new Error('invalid vverse package');
  const base = path.resolve(destination); fs.mkdirSync(base, { recursive: true });
  for (const dir of ['laws', 'assets', 'mods', 'signatures']) fs.mkdirSync(path.join(base, dir), { recursive: true });
  for (const [name, encoded] of Object.entries(obj.files)) {
    const target = path.resolve(base, name); if (target !== base && !target.startsWith(base + path.sep)) throw new Error(`path escapes package: ${name}`);
    fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, Buffer.from(encoded, 'base64'));
  }
  return validate(base, { strictStructure: true, requireSignature: true, requireCompleteSignature: true });
}

function preview(input) {
  const obj = JSON.parse(zlib.gunzipSync(fs.readFileSync(input), { to: 'string' }));
  if (obj.format !== 'vverse-1' || !obj.files || typeof obj.files !== 'object') throw new Error('invalid vverse package');
  if (!obj.files['manifest.json'] || !obj.files['blueprint.json'] || !obj.files['signatures/sha256.json']) throw new Error('package missing required metadata');
  const sig = JSON.parse(Buffer.from(obj.files['signatures/sha256.json'], 'base64').toString('utf8'));
  for (const [name, digest] of Object.entries(sig)) {
    if (name.startsWith('signatures/')) continue;
    if (!obj.files[name]) throw new Error(`signed file missing: ${name}`);
    const actual = crypto.createHash('sha256').update(Buffer.from(obj.files[name], 'base64')).digest('hex');
    if (actual !== digest) throw new Error(`digest mismatch: ${name}`);
  }
  for (const name of Object.keys(obj.files)) {
    if (name !== 'signatures/sha256.json' && name !== 'signatures/ed25519.json' && !(name in sig)) throw new Error(`unsigned file: ${name}`);
  }
  if (obj.files['signatures/ed25519.json']) {
    const ed = JSON.parse(Buffer.from(obj.files['signatures/ed25519.json'], 'base64').toString('utf8'));
    if (ed.algorithm !== 'ed25519' || typeof ed.publicKey !== 'string' || typeof ed.signature !== 'string') throw new Error('invalid ed25519 signature');
    const ordered = {}; for (const name of Object.keys(sig).filter(n => !n.startsWith('signatures/')).sort()) ordered[name] = sig[name];
    const key = crypto.createPublicKey({ key: Buffer.from(ed.publicKey, 'base64'), format: 'der', type: 'spki' });
    if (!crypto.verify(null, Buffer.from(JSON.stringify(ordered), 'utf8'), key, Buffer.from(ed.signature, 'base64'))) throw new Error('ed25519 signature verification failed');
  }
  return { format: obj.format, files: Object.keys(obj.files).sort(), bytes: fs.statSync(input).size, signed: true, readOnly: true };
}

if (require.main === module) {
  try { const [cmd, a, b] = process.argv.slice(2); if (cmd === 'pack') console.log(JSON.stringify(pack(a, b || `${a}.vvpkg`))); else if (cmd === 'unpack') console.log(JSON.stringify(unpack(a, b))); else if (cmd === 'preview') console.log(JSON.stringify(preview(a))); else throw new Error('usage: pack <dir> [file] | unpack <file> <dir> | preview <file>'); }
  catch (e) { console.error(`vverse pack: ${e.message}`); process.exit(1); }
}
module.exports = { pack, unpack, preview };
