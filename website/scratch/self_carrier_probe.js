#!/usr/bin/env node
// 乙⁷ 探针：把「仪器把自己算进载体」做成可判。
//
// 用法：node website/scratch/self_carrier_probe.js <repoRoot>
//
// 纪律（对应 self_carrier_criteria.md）：
//   C1 先把尺子真正读的载体逐份印出来（不问「有几份」，让尺子说）
//   C2 逐把解析器 + 用尺子自印的 DENOM 对账；对不上账就停下
//   C3 排除 ruler_*.js，逐把给出含/排除
//   C4 逐处列出「排除后消失的那些」
//   C5 两个分母并排，不许合
//   C6 三个对照真的跑（含空集）
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const RULERS = {
  '己⁶′': { s: 'website/scratch/ruler_jig6_recon.js', e: { RULER_SKIP_DECL: '1' } },
  '庚⁶': { s: 'website/scratch/ruler_geng6.js', e: {} },
  '壬⁶': { s: 'website/scratch/ruler_ren6.js', e: {} },
  '癸⁶': { s: 'website/scratch/ruler_gui6.js', e: {} },
};
const isRuler = (k) => /(^|\/)ruler_/.test(k);
const norm = (k) => (k.startsWith('website/scratch/') ? k.slice('website/scratch/'.length) : k);

// C1：让癸⁶ 把它读的载体逐份印出来（内存里插桩，不改尺子源码一个字节）
function carriersFromRuler() {
  const src = fs.readFileSync(path.join(ROOT, 'website/scratch/ruler_gui6.js'), 'utf8');
  const probe = src.replace(
    '  for (const f of files) {',
    "  if (process.env.RULER_LIST) { console.log('CARRIERS=' + files.length); for (const x of files) console.log('  ' + x); }\n  for (const f of files) {",
  );
  if (probe === src) throw new Error('插桩没命中：for (const f of files) 那行不在源码里');
  const tmp = path.join(ROOT, 'website/scratch/.build/self_carrier_list.js');
  fs.mkdirSync(path.dirname(tmp), { recursive: true });
  fs.writeFileSync(tmp, probe, 'utf8');
  const out = execFileSync('node', [tmp, ROOT], { env: { ...process.env, RULER_LIST: '1' }, encoding: 'utf8' });
  const m = out.match(/CARRIERS=(\d+)/);
  const list = out.split('\n').filter((l) => /^ {2}\S/.test(l)).map((l) => l.trim());
  return { declared: m ? +m[1] : null, list };
}

// C2：逐把解析器，并用尺子自印的 DENOM 对账
function runRuler(name) {
  const { s, e } = RULERS[name];
  const out = execFileSync('node', [path.join(ROOT, s), ROOT], { env: { ...process.env, ...e }, encoding: 'utf8' });
  const m = out.match(/DENOM=(\d+)/);
  const declared = m ? +m[1] : null;
  const rows = out
    .split('\n')
    .filter((l) => /\.js:\d+(\t|$)/.test(l))
    .map((l) => norm(l.split('\t')[0]));
  return { declared, rows };
}

const lines = [];
const say = (s = '') => { lines.push(s); console.log(s); };

// ---------- C1 ----------
say('=== C1：尺子真正读的载体（逐份，由癸⁶ 自己印）');
const c = carriersFromRuler();
say('  癸⁶ 自印 CARRIERS=' + c.declared + '，逐份：');
c.list.forEach((f, i) => say('    ' + String(i + 1).padStart(2) + '  ' + f));
say('  其中 ruler_*.js 一族：' + c.list.filter(isRuler).length + ' 份 —— ' + c.list.filter(isRuler).map((f) => f.replace('website/scratch/', '')).join('、'));
say();

// ---------- C2 ----------
say('=== C2：逐把解析器 + 用尺子自印的 DENOM 对账');
const all = {}, decl = {}, okAll = [];
for (const n of Object.keys(RULERS)) {
  const { declared, rows } = runRuler(n);
  const ok = declared === rows.length;
  decl[n] = declared; all[n] = rows; okAll.push(ok);
  say('  ' + n.padEnd(6) + ' 尺子自印 DENOM=' + String(declared).padStart(4) + '  解析器读到=' + String(rows.length).padStart(4) + '  ' + (ok ? '✓ 一致' : '★ 不一致 ⇒ 不许用'));
}
if (!okAll.every(Boolean)) { say('★ 有尺子对不上账，停止。'); process.exit(1); }
say();

// ---------- C3 ----------
const excl = {}; for (const n of Object.keys(RULERS)) excl[n] = all[n].filter((k) => !isRuler(k));
const uniq = (o) => [...new Set(Object.values(o).flat())];
const A = uniq(all), E = uniq(excl);
say('=== C3：排除 ruler_*.js 之后，逐把（含 / 排除）');
for (const n of Object.keys(RULERS)) {
  const d = all[n].length - excl[n].length;
  say('  ' + n.padEnd(6) + ' ' + String(all[n].length).padStart(4) + ' / ' + String(excl[n].length).padStart(4) + (d ? '   （−' + d + '）' : '   （一格没动）'));
}
say();

// ---------- C4 ----------
const only = A.filter((k) => !new Set(E).has(k));
say('=== C4：排除后消失的 ' + only.length + ' 处（逐处）');
only.forEach((k) => say('    ' + k));
say();

// ---------- C5 ----------
say('=== C5：两个分母并排（不许合、不许互相替代）');
say('  癸⁶ 的分母：含 ruler_*.js = ' + all['癸⁶'].length + '   排除 ruler_*.js = ' + excl['癸⁶'].length);
say('  四把并集  ：含 ruler_*.js = ' + A.length + '   排除 ruler_*.js = ' + E.length);
say();

// ---------- C6 ----------
say('=== C6：三个对照（真的跑）');
const d0 = all['癸⁶'].length, d1 = excl['癸⁶'].length;
say('  该动   癸⁶ 含=' + d0 + ' 排除=' + d1 + ' ⇒ ' + (d1 < d0 ? '降了 ' + (d0 - d1) + ' ✓' : '★ 没降 ⇒ 红'));
const nonA = Object.keys(RULERS).map((n) => all[n].filter((k) => !isRuler(k)).length);
const nonE = Object.keys(RULERS).map((n) => excl[n].filter((k) => !isRuler(k)).length);
say('  该不动 非 ruler 载体的命中数：含=[' + nonA + '] 排除=[' + nonE + '] ⇒ ' + (JSON.stringify(nonA) === JSON.stringify(nonE) ? '逐把相同 ✓' : '★ 不同 ⇒ 红'));
// 空集：把全部载体排除 —— 真的跑，不是推
// ★ 谓词方向：数的是【移除载体之后剩下的】，不是【落在载体名单里的】。
//   上一版写成了后者，于是印出的数恰好 = 并集总数 459，读起来像「仪器不归零」。
const allCarriers = c.list.map(norm);
const head = (k) => k.split(':')[0];
const inCarrier = E.filter((k) => allCarriers.includes(head(k))).length;   // 落在载体名单里的（= 总数，用于自证名单匹配上了）
const emptyCount = E.filter((k) => !allCarriers.includes(head(k))).length; // 移除载体之后剩下的 ⇒ 必须 0
say('  空集   把【全部载体】从读数里排除 ⇒ 剩余 = ' + emptyCount + ' ⇒ ' + (emptyCount === 0 ? '0 ✓' : '★ 不为 0 ⇒ 本对照红'));
say('         （自证名单匹配上了：并集 ' + E.length + ' 处里，落在载体名单上的 = ' + inCarrier + ' ⇒ ' + (inCarrier === E.length ? '全部命中 ✓ 所以剩余 0 是「移除」的 0，不是「没匹配上」的 0' : '★ 有没匹配上的 ⇒ 那个 0 不可信'));
say();

// ---------- 键 ----------
say('=== 两处目标的键（己⁶′/庚⁶/壬⁶/癸⁶ 顺序；含 ⇒ 排除）');
const key = (o, k) => Object.keys(RULERS).map((n) => (o[n].includes(k) ? '1' : '0')).join('');
for (const k of ['check_citations.js:87', 'render_check.js:661']) {
  const a = key(all, k), b = key(excl, k);
  say('  ' + k.padEnd(26) + ' 含=' + a + '  排除=' + b + '  ' + (a === b ? '⇒ 键不变 ✓' : '⇒ ★ 键变了'));
}
fs.writeFileSync(path.join(ROOT, 'website/scratch/.build/self_carrier.json'),
  JSON.stringify({ carriers: c, all, excl, decl }, null, 1));
