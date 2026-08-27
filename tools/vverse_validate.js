'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function validate(root, options = {}) {
  const base = path.resolve(root); const required = ['manifest.json', 'blueprint.json'];
  for (const f of required) if (!fs.existsSync(path.join(base, f))) throw new Error(`missing ${f}`);
  const manifest = JSON.parse(fs.readFileSync(path.join(base, 'manifest.json'), 'utf8'));
  for (const k of ['id', 'version', 'entry']) if (typeof manifest[k] !== 'string' || !manifest[k]) throw new Error(`manifest.${k} is required`);
  const entry = path.resolve(base, manifest.entry);
  if (entry !== base && !entry.startsWith(base + path.sep)) throw new Error('manifest.entry escapes package');
  if (!fs.existsSync(entry) || !fs.statSync(entry).isFile()) throw new Error(`manifest.entry not found: ${manifest.entry}`);
  const files = {};
  const walk = d => { for (const e of fs.readdirSync(d, { withFileTypes: true }).sort((a,b)=>a.name.localeCompare(b.name))) { if (e.name === 'signatures') continue; const f=path.join(d,e.name); if(e.isDirectory()) walk(f); else files[path.relative(base,f).split(path.sep).join('/')] = crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex'); } };
  walk(base);
  let signature = null;
  const signaturePath = path.join(base, 'signatures', 'sha256.json');
  if (fs.existsSync(signaturePath)) {
    signature = JSON.parse(fs.readFileSync(signaturePath, 'utf8'));
    for (const [name, digest] of Object.entries(signature)) {
      if (name.startsWith('signatures/')) continue;
      if (files[name] !== digest) throw new Error(`digest mismatch: ${name}`);
    }
    if (options.requireCompleteSignature) {
      for (const name of Object.keys(files)) if (!(name in signature)) throw new Error(`unsigned file: ${name}`);
    }
  } else if (options.requireSignature) {
    throw new Error('missing signatures/sha256.json');
  }
  return { manifest, files, signature, valid: true };
}

function writeSignature(root) {
  const base = path.resolve(root);
  const result = validate(base);
  const dir = path.join(base, 'signatures');
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, 'sha256.json'), JSON.stringify(result.files, null, 2) + '\n');
  return result.files;
}

if (require.main === module) {
  try {
    const args = process.argv.slice(2);
    const root = args.find(a => !a.startsWith('--')) || '.';
    if (args.includes('--write-signature')) {
      writeSignature(root);
    }
    console.log(JSON.stringify(validate(root, {
      requireSignature: args.includes('--require-signature'),
      requireCompleteSignature: args.includes('--require-complete-signature')
    }), null, 2));
  } catch (e) { console.error(`vverse: ${e.message}`); process.exit(1); }
}
module.exports = { validate, writeSignature };
