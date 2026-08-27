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

if (require.main === module) {
  try { const [cmd, a, b] = process.argv.slice(2); if (cmd === 'pack') console.log(JSON.stringify(pack(a, b || `${a}.vvpkg`))); else if (cmd === 'unpack') console.log(JSON.stringify(unpack(a, b))); else throw new Error('usage: pack <dir> [file] | unpack <file> <dir>'); }
  catch (e) { console.error(`vverse pack: ${e.message}`); process.exit(1); }
}
module.exports = { pack, unpack };
