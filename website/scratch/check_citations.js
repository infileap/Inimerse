#!/usr/bin/env node
/* S1 引用钉 —— 台账/规范页那些引文的守卫。
 *
 * 为什么需要它：`website/data/decisions.js` 是**手写**的，而手写的引文会漂。
 * 本仓库已经吃过两次这个亏（手抄的 `integer modulo by zero`、手拼的引文），
 * 而漂的方向永远是「看起来更具体」。
 *
 * 它钉四件事：
 *   1. **节号真的存在** —— 在 `ref` 那棵树的那份文件里，必须有这个标题。
 *      （一个不存在的节号与一个写错的节号，在页面上长得一样。）
 *   2. **引文逐字** —— `source.quote` 必须是那份文件里的一个子串，一个字不差。
 *   3. **id 唯一** —— 两个条目用同一个 id，页面上就会出现两条一样的锚点，
 *      而反馈会落到不确定的一条上。
 *   4. **被禁字符串不出现在站点里** —— 人类硬约束。
 *
 * 用法：`node website/scratch/check_citations.js`（从任何目录）。
 * 全绿 exit 0；有 FAIL exit 1。
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { execFileSync } = require('child_process');

const HERE = __dirname;                    // website/scratch
const SITE = path.resolve(HERE, '..');     // website
const ROOT = path.resolve(SITE, '..');     // 仓库根

const results = [];
let current = '(全局)';
function check(name, cond, detail) {
  results.push({ where: current, name, ok: !!cond, detail: detail === undefined ? '' : String(detail) });
}

/* ---------------- 读数据 ---------------- */
const sandbox = { window: {} };
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(SITE, 'data', 'decisions.js'), 'utf8'), sandbox,
  { filename: 'data/decisions.js' });
const D = sandbox.window.INFIVERSE_DECISIONS;

/* ---------------- 取 ref 那棵树里的文件 ---------------- */
/** 从 git 对象里取文件；取不到（比如 ref 是未提交的工作树）就退回读工作树，并说明。 */
function docAt(ref, file) {
  try {
    return { text: execFileSync('git', ['show', `${ref}:${file}`], { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 }), from: `${ref}:${file}` };
  } catch (e) {
    return { text: fs.readFileSync(path.join(ROOT, file), 'utf8'), from: `${file}（工作树，git 里取不到 ${ref}）` };
  }
}

/** 节号在标题里出现：`## 4. ★ super…` / `### 3.1 ★★★ …` / `## 7. 未定…` */
function sectionExists(text, section) {
  const esc = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．]|$)`, 'm');
  const m = text.match(re);
  return m ? m[0] : null;
}

/* ---------------- 检查 ---------------- */
current = '数据文件 data/decisions.js';
check('导出 INFIVERSE_DECISIONS', !!D);
check('有 ref', !!(D && D.ref), D && D.ref);
check('有 entries 且非空', !!(D && Array.isArray(D.entries) && D.entries.length), D && D.entries && D.entries.length);

const docCache = new Map();
function doc(file) {
  if (!docCache.has(file)) docCache.set(file, docAt(D.ref, file));
  return docCache.get(file);
}

const seenIds = new Map();
const SECTION_SOURCE = 'docs/ERROR_CODES_V06.md 开头与 docs/EIDOS_V06.md 文件头都逐字要求「引节号（§3.2、§5），不要引行号」';

for (const e of D.entries) {
  current = `${e.id} ${e.title}`;

  /* 必填 */
  check('有 status', !!e.status && !!D.statuses[e.status], e.status);
  check('有 title', typeof e.title === 'string' && e.title.length > 0);
  check('有 decided', typeof e.decided === 'string' && e.decided.length > 0);
  check('有 source.file / source.section / source.quote',
    !!(e.source && e.source.file && e.source.section && e.source.quote));

  /* id 唯一 —— 两个条目用同一个 id 也要红 */
  if (seenIds.has(e.id)) {
    check('id 唯一（不是第二个同名条目）', false, `与 ${seenIds.get(e.id)} 重复`);
  } else {
    seenIds.set(e.id, e.id);
  }

  if (!e.source || !e.source.file) continue;
  const d = doc(e.source.file);

  /* ① 节号存在 */
  const hit = sectionExists(d.text, e.source.section);
  check(`节号 §${e.source.section} 在 ${e.source.file} 里真实存在（来源规矩：${SECTION_SOURCE.slice(0, 24)}…）`,
    !!hit, hit ? `命中标题「${hit.trim()}」` : `找不到 §${e.source.section} —— 来源：${d.from}`);

  /* ② 引文逐字 */
  const at = d.text.indexOf(e.source.quote);
  check('引文在文件里逐字存在',
    at !== -1,
    at !== -1 ? `第 ${d.text.slice(0, at).split('\n').length} 行` : `找不到：「${e.source.quote.slice(0, 60)}…」`);

  /* ③ alternative 为 null 时必须能被归因 */
  if (e.alternative === null) {
    check('未记录备选 ⇒ 有可归因的 altSearch（文件 + 筛法 + 计数）',
      !!(D.altSearch && D.altSearch.files && D.altSearch.terms && D.altSearch.method && D.altSearch.counts),
      '缺 altSearch 的话，「我查过」与「我没找到」在页面上长得一样');
  }
}

/* ---------------- 被禁字符串守卫 ---------------- */
current = '全站文本';
const BANNED = ['Inim', ' OS'].join('');   // 人类硬约束：任何地方不许出现
const exts = ['.html', '.js', '.css', '.md'];
const scanned = [];
(function walk(dir) {
  for (const n of fs.readdirSync(dir)) {
    if (n === '.build' || n === 'node_modules') continue;
    const p = path.join(dir, n);
    const st = fs.statSync(p);
    if (st.isDirectory()) { walk(p); continue; }
    if (!exts.includes(path.extname(n))) continue;
    if (p === __filename) continue;                       // 守卫自己不参与（它用拼接构造那个串）
    scanned.push(p);
    const t = fs.readFileSync(p, 'utf8');
    if (t.includes(BANNED)) {
      check(`被禁字符串不出现在 ${path.relative(SITE, p)}`, false, '人类硬约束：任何地方不许出现那个名字');
    }
  }
})(SITE);
check(`被禁字符串扫描了 ${scanned.length} 个文本文件`, scanned.length > 0, `${scanned.length} 个`);

/* ---------------- 报告 ---------------- */
const bad = results.filter((r) => !r.ok);
results.filter((r) => !r.ok).forEach((r) => {
  console.log(`FAIL  [${r.where}] ${r.name}${r.detail ? '   ← ' + r.detail : ''}`);
});

const docsTouched = new Set(D.entries.map((e) => e.source && e.source.file).filter(Boolean));
console.log('');
console.log(`条目数：${D.entries.length}`);
console.log(`引文钉：${D.entries.length} 条，涉及 ${docsTouched.size} 个 docs 文件（读自 ref ${String(D.ref).slice(0, 7)}）`);
console.log(`节号钉：${D.entries.length} 个`);
console.log(`被禁字符串：扫描 ${scanned.length} 个文本文件`);
console.log(`检查项：${results.length - bad.length}/${results.length} 通过`);
if (bad.length) {
  console.log(`\n✗ 有 ${bad.length} 项失败`);
  process.exit(1);
}
console.log('\n✓ 全部通过');
