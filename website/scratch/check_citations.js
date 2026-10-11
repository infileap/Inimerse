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

/* ---------------- ④ 身份一致：台账写的 status 必须与 docs 自己标的一致 ----------------
 * 为什么需要它：`docs/EIDOS_V06.md` 自己给每一节标了身份（【已裁】/【提案】/【待裁】/【未做】）。
 * 而台账是**手写**的 —— 于是「docs 里已经裁了、而台账还写着提案」这种漂移
 * 没有任何东西会说话。★ 这一条不是假想的：写这一笔的时候 `§4 super` 就已经
 * 从【提案】变成了【已裁】，而台账当时还写着 proposal。
 *
 * 规则（只在**用这套括号词表的文档**上生效，避免把别的文档的「裁定」读成同一件事）：
 *   节正文里有【已裁】 ⇒ status 必须是 ruled
 *   节正文里有【提案】 ⇒ status 必须是 proposal
 *   两者都没有      ⇒ status 不许是 ruled/proposal（不许替 docs 编一个身份）
 * ★ 标记看的是**节正文**，不是标题 —— 因为 §3.1/§3.3/§6.1 的标题里都没有这四个字。
 */
current = '身份一致（台账 vs docs 原文）';
const MARKER_STATUS = { '【已裁】': 'ruled', '【提案】': 'proposal' };
const NOT_A_STANCE = ['undecided', 'notdone', 'notdoing', 'discipline'];

/** 取某节的正文：从标题行之后，到下一个标题为止 */
function sectionBody(text, section) {
  const esc = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．]|$).*$`, 'm');
  const m = re.exec(text);
  if (!m) return null;
  const rest = text.slice(m.index + m[0].length);
  const next = rest.search(/^#{2,4}\s/m);
  return next === -1 ? rest : rest.slice(0, next);
}

let stanceChecked = 0;
let stanceSkipped = 0;
for (const e of D.entries) {
  if (!e.source || !e.source.file) continue;
  const d = doc(e.source.file);
  const usesVocabulary = Object.keys(MARKER_STATUS).some((k) => d.text.includes(k));
  if (!usesVocabulary) { stanceSkipped++; continue; }
  const body = sectionBody(d.text, e.source.section);
  if (body === null) { stanceSkipped++; continue; }
  stanceChecked++;
  current = `${e.id} 身份一致`;
  const marks = Object.keys(MARKER_STATUS).filter((k) => body.includes(k));
  if (marks.length === 0) {
    check(`§${e.source.section} 在 docs 里没有标身份 ⇒ 台账也不许把它读成裁定或提案`,
      NOT_A_STANCE.includes(e.status),
      `docs 原文既没有【已裁】也没有【提案】，而台账写的是 ${e.status}`);
  } else {
    const want = marks.map((k) => MARKER_STATUS[k]);
    check(`§${e.source.section} 的身份与台账一致（docs 标了 ${marks.join(' / ')}）`,
      want.includes(e.status),
      `docs 标的是 ${marks.join(' / ')} ⇒ 台账应为 ${want.join(' 或 ')}，实际写的是 ${e.status}`);
  }
}

/* ⑤ 出处分级：humanRuled 那四条必须真的存在、真的都是 ruled */
current = '出处分级';
check('humanRuled 存在（四条人类在 ask_user_question 上选的）', !!(D.humanRuled && D.humanRuled.ids));
for (const id of (D.humanRuled && D.humanRuled.ids) || []) {
  const e = D.entries.find((x) => x.id === id);
  check(`${id} 在台账里存在且状态是 ruled`, !!e && e.status === 'ruled', e ? e.status : '条目不存在');
}
if (D.humanRuled && D.humanRuled.source) {
  const d = doc(D.humanRuled.source.file);
  check('humanRuled 的出处引文逐字存在',
    d.text.indexOf(D.humanRuled.source.quote) !== -1,
    `找不到：「${String(D.humanRuled.source.quote).slice(0, 50)}…」`);
}

/* ---------------- ⑥ 规范页的文档元信息：标题、行数、文件头引文都必须逐字 ----------------
 * `website/data/spec.js` 是手写的，而它里面混了两种东西：
 *   - 那份文件的**逐字**原文（标题、文件头几行、引文规矩）；
 *   - 本站对它的**概括**（`role`）。
 * 两者在页面上长得一样，所以概括必须被标明，而逐字的那部分必须被钉住 ——
 * 否则「我概括的」会被读成「它写的」，而漂的方向永远是「看起来更具体」。
 */
current = '规范页元信息 data/spec.js';
const specBox = { window: {} };
vm.createContext(specBox);
vm.runInContext(fs.readFileSync(path.join(SITE, 'data', 'spec.js'), 'utf8'), specBox,
  { filename: 'data/spec.js' });
const S = specBox.window.INFIVERSE_SPEC;

check('导出 INFIVERSE_SPEC', !!S);
check('spec.ref 与 decisions.ref 相同（两页读的是同一棵树）',
  !!(S && D && S.ref === D.ref), S && S.ref);
check('每份文档都声明了 role 是本站概括', !!(S && S.docs.every((x) => typeof x.role === 'string' && x.role.length)));

for (const sd of (S && S.docs) || []) {
  current = `规范页 ${sd.file}`;
  const d = docAt(sd.ref || S.ref, sd.file);

  /* 标题逐字（去掉我写在 title 里的 `# ` 前缀再比对） */
  const t = String(sd.title).replace(/^#+\s*/, '');
  check(`${sd.file} 的标题逐字存在`, d.text.indexOf(t) !== -1, `找不到标题「${t}」`);

  /* 行数：一个数就是一条断言 —— 它会随下一笔提交变假，而这一条会说话 */
  const actual = d.text.replace(/\n$/, '').split('\n').length;
  check(`${sd.file} 的行数与声明一致（${sd.lines}）`, actual === sd.lines,
    `声明 ${sd.lines}，实际 ${actual}（读自 ${d.from}）`);

  /* 文件头逐字 */
  for (const h of sd.headers || []) {
    const at = d.text.indexOf(h);
    check(`${sd.file} 的文件头逐字存在：「${h.slice(0, 34)}…」`,
      at !== -1, at === -1 ? `找不到：「${h.slice(0, 60)}」` : `第 ${d.text.slice(0, at).split('\n').length} 行`);
  }

  /* 引用规矩那条引文 */
  if (sd.citeQuote) {
    check(`${sd.file} 的引用规矩引文逐字存在`, d.text.indexOf(sd.citeQuote) !== -1,
      `找不到：「${sd.citeQuote.slice(0, 50)}…」`);
  }
}

/* 纪律 3 的载荷：引 PLAN_V07 的数字时必须同时显示的那一句，本身也要是逐字的 */
if (D.planV07Note) {
  const d = doc(D.planV07Note.file);
  check(`planV07Note（§${D.planV07Note.section}）引文逐字存在`,
    d.text.indexOf(D.planV07Note.quote) !== -1,
    `找不到：「${String(D.planV07Note.quote).slice(0, 50)}…」`);
}

/* ⑦ 覆盖：台账里每一个 source.file 都必须在规范页的文档清单里。
 * 为什么需要它：规范页按「文档」分组渲染裁定 —— 一条裁定如果挂在规范页没有的文档上，
 * 它在那一页上**一条都不显示**，而页面上看不出少了什么（没有空位、没有计数）。
 * ★ 这一条不是假想的：PLAN_V06 / PLAN_V07 原先不在 spec.js 的清单里，
 *   而台账里最多的裁定正是出自那两份。 */
current = '覆盖：台账的出处都在规范页的文档清单里';
const specFiles = new Set(((S && S.docs) || []).map((x) => x.file));
const missing = [...new Set(D.entries.map((e) => e.source && e.source.file).filter(Boolean))]
  .filter((f) => !specFiles.has(f));
check('台账引用的每个 docs 文件都在规范页的文档清单里',
  missing.length === 0,
  missing.length ? `规范页缺：${missing.join('、')}（那些裁定在规范页上一条都不会显示）` : `${specFiles.size} 份文档`);

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
console.log(`身份一致：检查了 ${stanceChecked} 条，跳过 ${stanceSkipped} 条（跳过的是那份文档不用【已裁】/【提案】这套括号词表的）`);
console.log(`出处分级：humanRuled ${((D.humanRuled && D.humanRuled.ids) || []).length} 条`);
console.log(`规范页元信息：${((S && S.docs) || []).length} 份文档（标题 / 行数 / 文件头引文）`);
console.log(`被禁字符串：扫描 ${scanned.length} 个文本文件`);
console.log(`检查项：${results.length - bad.length}/${results.length} 通过`);
if (bad.length) {
  console.log(`\n✗ 有 ${bad.length} 项失败`);
  process.exit(1);
}
console.log('\n✓ 全部通过');
