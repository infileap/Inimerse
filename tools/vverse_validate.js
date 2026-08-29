'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function validate(root, options = {}) {
  const base = path.resolve(root); const required = ['manifest.json', 'blueprint.json'];
  for (const f of required) if (!fs.existsSync(path.join(base, f))) throw new Error(`missing ${f}`);
  if (options.strictStructure) for (const dir of ['laws', 'assets', 'mods', 'signatures']) if (!fs.existsSync(path.join(base, dir)) || !fs.statSync(path.join(base, dir)).isDirectory()) throw new Error(`missing directory: ${dir}`);
  const manifest = JSON.parse(fs.readFileSync(path.join(base, 'manifest.json'), 'utf8'));
  for (const k of ['id', 'version', 'entry']) if (typeof manifest[k] !== 'string' || !manifest[k]) throw new Error(`manifest.${k} is required`);
  if (manifest.dependencies !== undefined && (!manifest.dependencies || typeof manifest.dependencies !== 'object' || Array.isArray(manifest.dependencies))) throw new Error('manifest.dependencies must be an object');
  if (manifest.dependencies) for (const [id, range] of Object.entries(manifest.dependencies)) {
    if (!/^[A-Za-z0-9._-]+$/.test(id) || typeof range !== 'string' || !range.trim()) throw new Error(`invalid dependency: ${id}`);
  }
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
  const edPath = path.join(base, 'signatures', 'ed25519.json');
  if (fs.existsSync(edPath)) {
    const ed = JSON.parse(fs.readFileSync(edPath, 'utf8'));
    if (ed.algorithm !== 'ed25519' || typeof ed.publicKey !== 'string' || typeof ed.signature !== 'string') throw new Error('invalid ed25519 signature');
    const key = options.trustedPublicKey
      ? crypto.createPublicKey(fs.readFileSync(options.trustedPublicKey))
      : crypto.createPublicKey({ key: Buffer.from(ed.publicKey, 'base64'), format: 'der', type: 'spki' });
    if (key.asymmetricKeyType !== 'ed25519' || !crypto.verify(null, signaturePayload(files), key, Buffer.from(ed.signature, 'base64'))) throw new Error('ed25519 signature verification failed');
  } else if (options.requirePublicSignature) {
    throw new Error('missing signatures/ed25519.json');
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

function signaturePayload(files) {
  const ordered = {};
  for (const name of Object.keys(files).sort()) ordered[name] = files[name];
  return Buffer.from(JSON.stringify(ordered), 'utf8');
}

function writeEd25519Signature(root, privateKeyPath) {
  const base = path.resolve(root);
  const files = writeSignature(base);
  const privateKey = crypto.createPrivateKey(fs.readFileSync(privateKeyPath));
  if (privateKey.asymmetricKeyType !== 'ed25519') throw new Error('signing key must be Ed25519');
  const publicKey = crypto.createPublicKey(privateKey);
  const signature = crypto.sign(null, signaturePayload(files), privateKey);
  const out = { algorithm: 'ed25519', publicKey: publicKey.export({ type: 'spki', format: 'der' }).toString('base64'), signature: signature.toString('base64') };
  const dir = path.join(base, 'signatures'); fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, 'ed25519.json'), JSON.stringify(out, null, 2) + '\n');
  return out;
}

if (require.main === module) {
  try {
    const args = process.argv.slice(2);
    const root = args.find(a => !a.startsWith('--')) || '.';
    if (args.includes('--write-signature')) {
      writeSignature(root);
    }
    if (args.includes('--sign-key')) writeEd25519Signature(root, args[args.indexOf('--sign-key') + 1]);
    console.log(JSON.stringify(validate(root, {
      requireSignature: args.includes('--require-signature'),
      requireCompleteSignature: args.includes('--require-complete-signature'),
      requirePublicSignature: args.includes('--require-public-signature'),
      trustedPublicKey: args.includes('--trust-key') ? args[args.indexOf('--trust-key') + 1] : undefined,
      strictStructure: args.includes('--strict')
    }), null, 2));
  } catch (e) { console.error(`vverse: ${e.message}`); process.exit(1); }
}
module.exports = { validate, writeSignature, writeEd25519Signature };
