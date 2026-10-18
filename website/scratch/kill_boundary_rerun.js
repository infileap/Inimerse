#!/usr/bin/env node
/* 甲⁷ 复跑 —— 把癸⁶ 那条取反模式的**左边界**补上，问三个数。
 *
 * 它**不改 ruler_gui6.js 一个字节**：读它的源码，在内存里做**一处**文本替换，
 * 断言替换恰好命中一次，写进 .build/ 再跑。替换本身逐字打印出来。
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const root = process.argv[2] || '.';
const SRC = path.join(root, 'website/scratch/ruler_gui6.js');
const OUT = path.join(root, 'website/scratch/.build/ruler_gui6_lb.js');

const OLD = "    new RegExp('!' + n + '\\\\b'), new RegExp('\\\\b' + n + '\\\\s*!=\\\\s*null'), new RegExp('\\\\b' + n + '\\\\s*===\\\\s*undefined'),";
const NEW = "    new RegExp('(?:^|[^!\\\\w])!' + n + '\\\\b'), new RegExp('\\\\b' + n + '\\\\s*!=\\\\s*null'), new RegExp('\\\\b' + n + '\\\\s*===\\\\s*undefined'),";

let s = fs.readFileSync(SRC, 'utf8');
const hits = s.split(OLD).length - 1;
console.log('=== 替换');
console.log('  旧: ' + OLD.trim());
console.log('  新: ' + NEW.trim());
console.log('  命中次数 = ' + hits + (hits === 1 ? '  ✓ 恰好一处' : '  ★ 不是一处 ⇒ 断言失败'));
if (hits !== 1) process.exit(2);
s = s.replace(OLD, NEW);
fs.mkdirSync(path.dirname(OUT), { recursive: true });
fs.writeFileSync(OUT, s);
console.log('  已写到 ' + path.relative(root, OUT) + '（.build/ 是 gitignored，不进树）');
console.log();

const run = (env, script) => {
  const out = execFileSync('node', [script, '.'], { cwd: root, env: { ...process.env, ...env }, encoding: 'utf8' });
  return out.split('\n').filter((l) => /\.js:\d+\t?$/.test(l)).map((l) => l.replace(/^website\/scratch\//, '').replace(/\t$/, ''));
};
const base = run({}, 'website/scratch/ruler_gui6.js');
const fixed = run({}, OUT);
const sb = new Set(base), sf = new Set(fixed);
const dNew = fixed.filter((k) => !sb.has(k));
const dGone = base.filter((k) => !sf.has(k));
console.log('=== 三个数');
console.log('  ① check_citations.js:87  改前收=' + sb.has('check_citations.js:87') + '  改后收=' + sf.has('check_citations.js:87') +
  '  ⇒ 档 ' + (sb.has('check_citations.js:87') ? '（不收）' : '') + (sf.has('check_citations.js:87') ? '①/③（收）' : ''));
console.log('  ② 癸⁶ 的分母  改前 = ' + base.length + '   改后 = ' + fixed.length);
console.log('  ③ 动的量 = ' + (fixed.length - base.length) + '   （新进 ' + dNew.length + ' ／ 掉出 ' + dGone.length + '）');
console.log();
console.log('=== 新进来的（最多 20）'); for (const k of dNew.slice(0, 20)) console.log('  + ' + k);
console.log('=== 掉出去的（最多 20）'); for (const k of dGone.slice(0, 20)) console.log('  - ' + k);
console.log();
console.log('=== 反方向对照');
const watch = ['render_check.js:661', 'check_citations.js:82', 'check_citations.js:950'];
for (const k of watch) console.log('  ' + k.padEnd(28) + ' 改前=' + sb.has(k) + '  改后=' + sf.has(k) + '  ⇒ ' + (sb.has(k) === sf.has(k) ? '不动 ✓' : '★ 动了'));
console.log();
console.log('=== 空集对照（种子表清空）');
console.log('  改前脚本 RULER_NO_SEED=1 ⇒ DENOM=' + run({ RULER_NO_SEED: '1' }, 'website/scratch/ruler_gui6.js').length);
console.log('  改后脚本 RULER_NO_SEED=1 ⇒ DENOM=' + run({ RULER_NO_SEED: '1' }, OUT).length);
