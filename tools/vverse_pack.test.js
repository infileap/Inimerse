'use strict';
const assert = require('node:assert/strict'); const fs = require('node:fs'); const os = require('node:os'); const path = require('node:path');
const { pack, unpack, preview } = require('./vverse_pack');
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vpack-')); for (const d of ['laws','assets','mods','signatures']) fs.mkdirSync(path.join(root,d));
fs.writeFileSync(path.join(root,'manifest.json'), JSON.stringify({id:'demo',version:'1.0.0',entry:'main.im'})); fs.writeFileSync(path.join(root,'blueprint.json'),'{}'); fs.writeFileSync(path.join(root,'main.im'),'say "ok"');
const archive = path.join(root, '..', 'demo.vvpkg'); const result = pack(root, archive); assert.equal(result.files >= 3, true);
const info = preview(archive); assert.equal(info.readOnly, true); assert.equal(info.signed, true); assert.ok(info.files.includes('manifest.json'));
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'vunpack-')); const checked = unpack(archive, out); assert.equal(checked.valid, true); assert.equal(fs.readFileSync(path.join(out,'main.im'),'utf8'),'say "ok"');
console.log('vverse pack tests: ok');
