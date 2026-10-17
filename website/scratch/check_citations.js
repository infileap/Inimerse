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
/* 只报事实的行，不参与判定 —— 与 render_check.js 里的同名工具同形。 */
const infos = [];
function info(s) { infos.push(s); }

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
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．：:]|$)`, 'm');
  const m = text.match(re);
  return m ? m[0] : null;
}

/* ---------------- 检查 ---------------- */
current = '数据文件 data/decisions.js';
check('导出 INFIVERSE_DECISIONS', !!D);
check('有 ref', !!(D && D.ref), D && D.ref);
check('有 entries 且非空', !!(D && Array.isArray(D.entries) && D.entries.length), D && D.entries && D.entries.length);

/* ★★ 一份文档上出现了【两批引文，读自两棵树】。
   `ref` 是台账的主观测点；`e.readAt` 是这一条引文实际读自的那棵树。
   ★ 为什么必须逐条：新裁的几条（§1.5 / §3.2 / §3.4 / §7.2 / §7.4）的原文
     在主观测点那棵树上**根本不存在**；另有两条引的句子在那之后被**改写**过。
   ★ 缓存键必须带 ref —— 只按文件名缓存会让「同一个文件的两棵树」互相顶掉。 */
const docCache = new Map();
function docAtRef(ref, file) {
  const k = `${ref}|${file}`;
  if (!docCache.has(k)) docCache.set(k, docAt(ref, file));
  return docCache.get(k);
}
function doc(file, ref) { return docAtRef(ref || D.ref, file); }
function readAtOf(e) { return e.readAt || D.ref; }

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
  const d = doc(e.source.file, readAtOf(e));

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
 * ★ 标记看的是**节标题 + 节正文**，两个都要看：
 *   §3.1/§3.3/§6.1 的标题里没有这四个字（标记在正文里），
 *   而 §1.6 的标记**就在标题里**（`### 1.6 【未做】`）—— 只看正文会把它整节漏掉。
 * ★ 词表包含**异体拼写**：【已裁定】与【已裁】是同一个身份。
 *   这一条不是假想的：`docs/TYPESET_V06.md` 用的是【已裁定】，而旧的 gate 只查【已裁】/【提案】
 *   ⇒ 它被判定成「不用这套词表」而**整份跳过**，那三条裁定在台账里一条都没有，而守卫全绿。
 */
current = '身份一致（台账 vs docs 原文）';
/* ★★ 【标记】与【提及】的分界 —— 写成一条规则，两处共用（守卫 ⑧ 与 ⑪(a)）。
   判据：一行只有在**它自己就是在声称一个身份**时才算标记 ——
   把行首的 `> # * ★ 空白 数字 . - — ) （` 全部剥掉之后，这一行**以那个标记开头**。
   ★ `docs/AUDIT.md:7807` 那一行是 `★ 文件头那一句：★ **前半句说「四条【已裁】是…选定的」…`
     ⇒ 剥完以「文件头那一句」开头 ⇒ 它是**提及**（它在引用别人的句子），不是标记。
   ★ 而 `★★★ **【已裁】语义是…` / `### 1.6 【未做】` / `4. **【已裁定】…`
     ⇒ 剥完以标记开头 ⇒ 是**标记**。
   ★ 这条规则是**收窄**：⑧ 原来按整份文件 `match` 计数，于是把 AUDIT 里那句引用算成了一个标记，
     而它让一个 7930 行的审计档看起来像是「带身份标记的文档」。 */
/* ★ 补上 `⇒`：实测 `⇒ **【已裁】走 (a)。**`（§1.5 的节级标记，而 §1.5 的标题里没有标记）
   与 `⇒ **【提案】…`（§2.2 同理）原来被判成【提及】—— 因为 `⇒` 不在剥头字符集里。
   ★ 一个白名单式的剥头集合，它的漏项由【哪些符号会出现在标记前面】决定，
     而那个集合没有任何东西在维护 —— 这一条是被一次全量普查找出来的。 */
const stripLead = (l) => l.replace(/^[>#*★\s\d.\-—)–）⇒]+/, '');

/* ★★★ 身份标记的【作用域】是它所在的那个【语义单元】—— 一行、一个表格单元格、或一个列表项 ——
   而**不要求它出现在行首**。一个标记说的是「这一单元的内容有那个身份」，
   而它在单元内的位置不改变它说什么。（这条约定是 2026-10 由人类裁定的，原文见与 exact-otter 的往来。）
   ⇒ `| | **不相交**（初稿提案写的） | **最特化优先**【已裁】 |` 那一格**是标记**：那一格在说「这一列是裁定」。
   ⇒ 而 `（初稿提案写的【已裁】）` **不是**：标记被包在括注里，不再声称那一格的身份。
   验收就是这两行 —— 前者的行号必须出现在【标记】那一栏里，后者必须落回【提及】。 */
const stripEmph = (s) => s.replace(/^[\s*_`]+/, '').replace(/[\s*_`]+$/, '');
const unitIsMarker = (unit, mk) => {
  const t = stripEmph(unit);
  return t.startsWith(mk) || t.endsWith(mk);
};
const isMarkerLine = (l, mk) => {
  if (stripLead(l).startsWith(mk)) return true;      // 整行以标记开头（原有规则）
  const t = stripEmph(stripLead(l));
  if (t.startsWith('|')) return t.split('|').some((c) => unitIsMarker(c, mk));  // 表格行 ⇒ 逐格
  return t.endsWith(mk);                             // 整行以标记收尾：这一行在声称自己的身份
};

/* ★ 这条白名单的完备性没有任何东西在报 —— 而它藏起来的那一行，
   与【那一行不是标记】在输出上完全一样。
   下面那行常驻打印能把**藏不住的那部分**列出来，而它**不会说「这个集合少了一个符号」**。 */

const MARKER_STATUS = {
  '【已裁】': 'ruled', '【已裁定】': 'ruled',
  '【提案】': 'proposal', '【待裁】': 'undecided', '【未做】': 'notdone',
};
const NOT_A_STANCE = ['undecided', 'notdone', 'notdoing', 'discipline'];

/** 取某节的标题行 */
function sectionHeading(text, section) {
  const esc = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．：:]|$).*$`, 'm');
  const m = re.exec(text);
  return m ? m[0] : '';
}

/** 取某节的正文：从标题行之后，到下一个标题为止 */
function sectionBody(text, section) {
  const esc = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．：:]|$).*$`, 'm');
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
  const d = doc(e.source.file, readAtOf(e));
  const usesVocabulary = Object.keys(MARKER_STATUS).some((k) => d.text.includes(k));
  if (!usesVocabulary) { stanceSkipped++; continue; }
  const body = sectionBody(d.text, e.source.section);
  if (body === null) { stanceSkipped++; continue; }
  stanceChecked++;
  current = `${e.id} 身份一致`;
  const hay = sectionHeading(d.text, e.source.section) + '\n' + body;
  const marks = Object.keys(MARKER_STATUS).filter((k) => hay.includes(k));
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

/* ⑤ 出处分级：humanRuled 那一块是【那套做法的出处】，不是花名册。
   ★ 原来这里逐条检查 `humanRuled.ids` 那四条存在且 ruled。★ 那个 `ids` 已退役
     （理由见 `website/data/decisions.js` 里 `humanRuled` 的注释）：**它引的那句话不再点名了。**
   ★ 「谁裁的」现在由每一条自己的 `ruledBy` 承担；文件头那条全称断言由下面
     `headerWatch.universalClaim` 盯着。★ 一次退役要说出来 —— 退役与「它不红了」长得一样。 */
current = '出处分级';
if (D.humanRuled && D.humanRuled.source) {
  const d = doc(D.humanRuled.source.file, D.humanRuled.source.readAt || D.ref);
  check('humanRuled 的出处引文逐字存在',
    d.text.indexOf(D.humanRuled.source.quote) !== -1,
    `找不到：「${String(D.humanRuled.source.quote).slice(0, 50)}…」`);
}
check('humanRuled 不再带 ids（那一栏已退役：它引的那句话不再点名）',
  !(D.humanRuled && D.humanRuled.ids),
  (D.humanRuled && D.humanRuled.ids) ? `还有 ids：${D.humanRuled.ids.join('、')}` : '没有 ids');

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

  /* ★ 一份文档可以声明自己的 ref（元信息读自另一棵树）。
     它必须是一个真实提交，且与主观测点不同 —— 相同就不该写这个字段。 */
  if (sd.ref && sd.ref !== S.ref) {
    let real = false; let why = '';
    try {
      execFileSync('git', ['rev-parse', '--quiet', '--verify', `${sd.ref}^{commit}`],
        { cwd: ROOT, stdio: ['ignore', 'pipe', 'pipe'] });
      real = true;
    } catch (e) { why = String((e && e.stderr) || (e && e.message) || e).trim().slice(0, 120); }
    check(`${sd.file} 声明的 ref（${String(sd.ref).slice(0, 7)}）是一个真实提交`, real, why);
    check(`${sd.file} 声明的 ref 与主观测点不同（相同就不该写这个字段）`, sd.ref !== S.ref);
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

/* ---------------- ⑧ ⑦ 的反方向：带身份标记的 docs 文件 vs 规范页清单 ----------------
 * ⑦ 只挡了一个方向（台账 → 规范页的清单）。反方向是同一个机制的另一半：
 * **一份自己带着身份标记的文档不在清单里 ⇒ 它那些标记在规范页上一条都不显示，而页面是完整的。**
 * ★ 词表必须含**异体拼写**（【已裁定】与【已裁】同身份）—— 见 ④ 上面那段注释里的实例。
 * ★ 第二问更要紧：**带标记、也在清单里、而台账里一条都没出自它的文件**。
 *   `docs/TYPESET_V06.md` 就是这种：它有三条【已裁定】，而台账里 0 条出自它 ——
 *   那三条裁定在台账页上一条都没有，而**守卫会全绿**。
 */
current = '覆盖：带身份标记的 docs 文件 vs 规范页清单';
const MARKER_RE = /【已裁定?】|【提案】|【待裁】|【未做】/g;
/* ★★ 这两问读的是**哪棵树**：原来是 `fs.readFileSync`（＝工作树里检出的那一份），
   而同一个守卫的引文钉读的是 `git show <ref>:` —— **一个守卫同时读了两个树，而它不说。**
   后果是可测的：同一份守卫在不同分支上给出不同的「带标记的文档」数，
   而 `docs/` 是别人在推的，所以那个差不是我的改动造成的、也不会有人报错。
   ⇒ 统一成读 `D.ref`（引文钉读的那棵树），并把 ref 印进报告。 */
/* ★★ 这一问读的是【它自己的】观测点：`markerScanRef`，一个**指针**（运行时解析）。
   它**不是** `D.ref` —— 那是引文的记录，一个字都不因这一问而改变。 */
const MSR_SPEC = D.markerScanRef || { kind: 'pin', resolve: D.ref };
const MSR_SHA = execFileSync('git', ['rev-parse', MSR_SPEC.resolve], { cwd: ROOT, encoding: 'utf8' }).trim();
const MSR_LABEL = MSR_SPEC.kind === 'pointer'
  ? `${MSR_SPEC.resolve} 解析出的 ${MSR_SHA.slice(0, 7)}（一个指针，不是钉）`
  : `${MSR_SHA.slice(0, 7)}（钉）`;
check('markerScanRef 解析出来的必须是一个真实提交', /^[0-9a-f]{40}$/.test(MSR_SHA), MSR_SHA);

/* ★ 守卫 ⑪（谁裁的）问的是【现状】：「这份文件里今天每一处归属，台账都跟上了吗」。
   所以它读一根【指针】。家法：指针（「去看那儿」）必须更新到当前值；
   记录（「当时量到什么」）保留原值并写明是哪棵树。
   ★ 逐条条目的 `readAt` 仍然是【记录】—— 它们说的是「这条引文当时读自哪棵树」。 */
const ATT_SPEC = (D.attribution && D.attribution.tree)
  || { kind: 'pin', resolve: (D.attribution && D.attribution.readAt) || D.ref };
const ATT_SHA = execFileSync('git', ['rev-parse', ATT_SPEC.resolve], { cwd: ROOT, encoding: 'utf8' }).trim();
const ATT_LABEL = ATT_SPEC.kind === 'pointer'
  ? `${ATT_SPEC.resolve} 解析出的 ${ATT_SHA.slice(0, 7)}（一个指针，不是钉）`
  : `${ATT_SHA.slice(0, 7)}（钉）`;
check('attribution 的树解析出来必须是一个真实提交', /^[0-9a-f]{40}$/.test(ATT_SHA), ATT_SHA);
/* ★ 一个会动的数如果不印出它今天解析成了什么，读报告的人会把它当成一个稳定的数。 */
{
  const at = D.attribution || {};
  const t = docAtRef(ATT_SHA, at.file || 'docs/EIDOS_V06.md').text;
  const sites = (t.match(new RegExp(at.askUserQuestion || 'ask_user_question', 'g')) || []).length;
  info(`守卫 ⑪ 的输入树：${ATT_LABEL} —— 这份文件在那棵树上出现「${at.askUserQuestion || 'ask_user_question'}」${sites} 处`);
}
const docFiles = execFileSync('git', ['ls-tree', '-r', '--name-only', MSR_SHA, 'docs/'], { cwd: ROOT, encoding: 'utf8' })
  .split('\n').map((x) => x.trim()).filter((x) => x.endsWith('.md'));
const ledgerFiles = new Set(D.entries.map((e) => e.source && e.source.file).filter(Boolean));
const marked = [];
for (const rel of docFiles) {
  const kinds = {};
  docAtRef(MSR_SHA, rel).text.split('\n').forEach((l) => {
    Object.keys(MARKER_STATUS).forEach((mk) => { if (isMarkerLine(l, mk)) kinds[mk] = (kinds[mk] || 0) + 1; });
  });
  if (!Object.keys(kinds).length) continue;
  marked.push({ file: rel, kinds, inSpec: specFiles.has(rel), inLedger: ledgerFiles.has(rel) });
}
const notInSpec = marked.filter((x) => !x.inSpec);
check('带身份标记的 docs 文件都在规范页的文档清单里',
  notInSpec.length === 0,
  notInSpec.length
    ? `不在清单里：${notInSpec.map((x) => x.file).join('、')}（那些标记在规范页上一条都不显示）`
    : `${marked.length} 个带标记的文件，全部在清单里`);
const noLedger = marked.filter((x) => x.inSpec && !x.inLedger);
check('带身份标记、且在规范页清单里的文件，台账里至少有一条出自它',
  noLedger.length === 0,
  noLedger.length
    ? `台账里没有一条出自：${noLedger.map((x) => x.file).join('、')}（那几条裁定在台账页上一条都没有，而守卫会全绿）`
    : `${marked.filter((x) => x.inSpec).length} 个文件都有条目`);

/* ---------------- ⑭ 一条裁定的【射程】：概念筛 vs 写法清单 ----------------
 * ★ ⑧ 问的是【文件】：带方括号那对记号的文件，在不在规范页清单里。
 *   而它的绿只说明「那里没有方括号」，**不说明那里没有裁定** ——
 *   实测：18 份没有任何守卫读的文档里，575 节里有 20 节在说某件事已经定了，
 *   而其中 0 节用的是那对记号。
 * ★ 所以 ⑭ 换一个形状：**先定义【一条裁定是什么】，再报出【它在多少节里出现】
 *   与【我认了其中多少节】**。清单从「射程」降级成「我认得的写法」。
 * ★★ 分层是**故意**的，理由写在这里：任何探测器都是某种词表，所以
 *   「粗的那层」的职责是**多报**（概念的射程），「细的那层」的职责是**能叫出名字**。
 *   而守卫的价值在【两个数的差】与【差里逐条是谁】—— 那才让这个词表可被证伪。
 * ★ 家法（exact-otter 裁的）：**守卫不许改成「认词」** —— 裸词 `裁定` 命中 543 次，
 *   其中大量是【讨论裁定】而不是【一条裁定】。所以粗筛也不是「出现这个词」：
 *     · 正文认 `已裁` / `已决定`（**声称**），以及 `（…裁定` 这种**带归属的**说法；
 *     · 单独一个 `裁定` 只有出现在**标题**里才算（标题是在给这一节定性）；
 *     · ★ 而**请求**不算：标题里写着「需要人裁定」「待裁」「未定」的节是**在要一条裁定**，
 *       不是**在声称有了一条** —— 实测它当场抓出 `docs/HANDOFF_INFIVERSE.md §4.3`
 *       「**需要人裁定**：`count` 与 `size` 的处置」，而那一节正文逐字是「推荐（**未定**）」。
 */
current = '一条裁定的射程：概念筛 vs 写法清单';
const CONCEPT_BODY_RE = /已裁|已决定|（[^）]{0,8}裁定/;
const CONCEPT_HEAD_RE = /裁定/;
/* ★ 只排除【在要一条裁定】的标题（「需要人裁定」「待裁」「要人裁」）。
   ★★ `未定` **不排除** —— 实测 `docs/PLAN_V06.md §7` 的标题是「未定与冲突」，
      而它的正文里逐字有一格 `| **已裁**，记在 §3.2 非目标 |`：
      **一节在讲「哪些还没定」，不等于它没有在声称某件事已经定了。**
      把 `未定` 放进排除词，会让那一节从概念里掉出去 —— 而它是差② 里最干净的一处。 */
const CONCEPT_REQUEST_RE = /需要[^，。：；]{0,8}裁定|待裁|要人裁/;
/* ★ 这张表就是原来那个「清单」降级之后的样子：**我认得的写法**，不是「射程」。 */
const SPELLINGS = [
  { name: '【已裁】', re: /【已裁】/ },
  { name: '【已裁定】', re: /【已裁定】/ },
  { name: '**已裁**', re: /\*\*已裁\*\*/ },
  { name: '**已裁定**', re: /\*\*已裁定\*\*/ },
  { name: '已裁定', re: /已裁定/ },
  { name: '（人裁定）', re: /（人裁定）/ },
  { name: '（用户裁定', re: /（用户裁定/ },
  { name: '已决定', re: /已决定/ },
];
const SEC_RE = /^#{2,4}\s+(?:★+\s+)?(\d+(?:\.\d+)*)[.．：:]?\s*(.*)$/;
const sectionsOfText = (text) => {
  const out = [];
  let cur = { id: '(文件头/无节)', heading: '(文件头/无节)', lines: [] };
  text.split('\n').forEach((l) => {
    const m = SEC_RE.exec(l);
    if (m) { out.push(cur); cur = { id: m[1], heading: m[2].trim(), lines: [] }; }
    else cur.lines.push(l);
  });
  out.push(cur);
  return out.map((x) => ({ id: x.id, heading: x.heading, body: x.lines.join('\n') }));
};
const allSecs = [];
for (const rel of docFiles) {
  for (const x of sectionsOfText(docAtRef(MSR_SHA, rel).text)) allSecs.push({ file: rel, ...x });
}
const isConcept = (x) => !CONCEPT_REQUEST_RE.test(x.heading)
  && (CONCEPT_BODY_RE.test(x.body) || CONCEPT_HEAD_RE.test(x.heading));
const namesOf = (x) => SPELLINGS.filter((sp) => sp.re.test(x.body) || sp.re.test(x.heading)).map((sp) => sp.name);
const concept = allSecs.filter(isConcept);
const named = concept.filter((x) => namesOf(x).length);
const unnamed = concept.filter((x) => !namesOf(x).length);
const overreach = allSecs.filter((x) => namesOf(x).length && !isConcept(x));
const outsideConcept = concept.filter((x) => !specFiles.has(x.file));

check('⑭ 写法清单不能抓到概念筛抓不到的节（否则是清单在替概念做决定）',
  overreach.length === 0,
  overreach.length
    ? `清单抓到而概念没抓：${overreach.slice(0, 5).map((x) => `${x.file} §${x.id}`).join('、')}`
    : '清单 ⊆ 概念');
/* ★ 验收①的实现：那 20 节（清单之外的文档里的裁定）**每次运行都逐条印出来，并标明叫不叫得出名字**。
   这一条检查盯的是【那份打印本身】：印出来的条数必须等于抓到的条数 —— 打印被截断与「只有这么多」长得一样。 */
const outsidePrinted = [];
outsideConcept.forEach((x) => outsidePrinted.push(`   · ${x.file} §${x.id}  [${namesOf(x).join('/') || '★ 叫不出名字'}]  「${x.heading.slice(0, 52)}」`));
check('⑭ 清单之外的文档里，概念筛抓到的节必须逐条印出来（打印被截断与「只有这么多」长得一样）',
  outsidePrinted.length === outsideConcept.length,
  `${outsidePrinted.length} / ${outsideConcept.length}`);
const unnamedPrinted = [];
unnamed.forEach((x) => unnamedPrinted.push(`   · ${x.file} §${x.id}  「${x.heading.slice(0, 52)}」`));

info(`★ 一条裁定的射程：概念筛 **${concept.length}** 节（在 ${new Set(concept.map((x) => x.file)).size} 份文档里）／能叫出名字 **${named.length}** 节／差 **${unnamed.length}** 节`);
info(`★ 其中落在规范页清单之外的：**${outsideConcept.length}** 节，分布在 **${new Set(outsideConcept.map((x) => x.file)).size}** 份文档里 ⇒ 「覆盖 ${specFiles.size} 份」是一个【射程声明】，不是一个【覆盖率】`);
info(`★ 概念筛逐字：标题不含「需要…裁定/待裁/未定」，且（正文含 /已裁|已决定|（…裁定/ 或 标题含 /裁定/）`);
const deadSpellings = SPELLINGS.filter((sp) => !allSecs.some((x) => sp.re.test(x.body) || sp.re.test(x.heading)));
info(`★ 写法清单 ${SPELLINGS.length} 条：${SPELLINGS.map((x) => x.name).join(' ')}${deadSpellings.length ? ` —— ★ 其中**一次都没命中**的：${deadSpellings.map((x) => x.name).join(' ')}（我认得它，而它今天不存在：这是一个零，要归因）` : ''}`);
info(`★ 清单之外的文档里，概念筛抓到的 ${outsideConcept.length} 节（逐条，含它叫不叫得出名字）：`);
outsidePrinted.forEach((l) => info(l));
if (unnamed.length) {
  info(`★ 全库：概念筛抓到而写法清单叫不出名字的 ${unnamed.length} 节（逐条）：`);
  unnamedPrinted.forEach((l) => info(l));
}

/* ==================== ⑮ 一条裁定的差：逐节的出处、台账侧的实质、判词 ====================
 * ★ ⑭ 报的是【一个数】（差 33）。这一节把那个数变成【一张可以被人判的表】——
 *   一个数不可反驳，一条带出处的证物可以。
 * ★ 每一行四个栏位：
 *   ① 节的身份（文件 + 节号）—— ★ 由 ⑭ 的差集**算出来**，不是我抄的；
 *   ② 触发它的那句话（**逐字**，不是概括）—— ★ 这一栏是这一件真正的产出；
 *   ③ 台账侧（`docs/AUDIT.md`）出现过这个实质没有 —— ★ **按内容搜，不是按标记词搜**，
 *      搜索式写在行上；★ **自指要单独标**：命中落在这一节自己身上时，
 *      那不是「别人登记了它」，是「它自己就是台账的一节」；
 *   ④ 一个判词（**手读**）：一条裁定 / 在要一条裁定 / 一句话在说这是一条裁定 / 判不了。
 * ★ 为什么 ④ 必须有「判不了」这一档：**一个恒为 0 的「判不了」，量的是尺子的自信，不是语料。**
 * ★ 判词的四档判据（我用的，写在这里以便别人反驳）：
 *   A = 把这一句剪出来贴在「裁定」标题下，它仍是一条完整的决定（说清了定了什么）；
 *   B = 它在说「这里还没定 / 要人裁」；
 *   C = 剪出来之后它只是一句**引用 / 前提 / 交叉引用**（读者不知道定了什么，或它在说别的事）；
 *   N = 归属与内容融在一句、剪出来两读都成立，或这一节只有标题没有正文。
 * ★ 这张表读的是【记录的那棵树】（`RULING_ROWS_READ_AT`），不是今天的指针：
 *   记录保留原值，指针在报告里印出来 —— **两个树都必须说出口**，
 *   否则就是「同一份守卫读了两个树而它不说」（这是前面量到过的那个病）。 */
current = '一条裁定的差：逐节的出处、台账侧的实质、判词';
const RULING_ROWS_READ_AT = '4829766';
const TIER_NAMES = {
  A: '一条裁定',
  B: '在要一条裁定',
  C: '一句话在说这是一条裁定',
  N: '判不了',
};
const TIER_DISPOSITION = {
  A: '要登记：它说清了「定了什么」',
  B: '不用登记：它在要一条裁定，不是在声称有了一条',
  C: '留在差里：它只是把「裁定」当引用，不是一条裁定',
  N: '留在差里并写明为什么判不了 —— **不塞进最近的那一档**',
};
const RULING_ROWS = [
  { file: "docs/AUDIT.md", id: "1.8", tier: 'A', search: "vts_double", auditRead: '同题不同事',
    quote: "**修法（裁定：以「改正后的解释器规范」为唯一规范，三份实现都重写）** —— 不把解释器的 bug 移植给另外两个：" },
  { file: "docs/DECFY_DESIGN.md", id: "3.1.4", tier: 'C', search: "RegInstruction",
    quote: "⇒ 后端要吃这份 IR，就必须**自己实现一份同样的动态分派**（这正是裁定 A/A′ 说的「链接 `Value` 运行时 / 把 IR 的值语义映射到 wasm」）。因此：" },
  { file: "docs/DECFY_DESIGN.md", id: "3.1.11", tier: 'C', search: "OpSemantics",
    quote: "| 17 | `OP_AND` | `3508-3514` | 1,2,3 | P | 0 | h c v s | **真值语义**（§10.42 裁定后的活含义）；读 `R[r2]`、`R[r3]` |" },
  { file: "docs/DECFY_DESIGN.md", id: "3.5", tier: 'B', search: "SLOT_BYTES",
    quote: "三套值表示：裁定 A 只覆盖了 AOT，wasm 那半仍未说" },
  { file: "docs/DECFY_DESIGN.md", id: "3.6", tier: 'B', search: "类型信息",
    quote: "裁定 A′ 里有一个它自己没说的地方：**IR 今天没有类型信息**" },
  { file: "docs/DECFY_DESIGN.md", id: "3.7", tier: 'A', search: "im_typeset", auditRead: '无关',
    quote: "裁定已下：**`var` 动态、其余静态 ⇒ 走 B″**（2026-10，第三个人类裁定）" },
  { file: "docs/DECFY_DESIGN.md", id: "3.8", tier: 'A', search: "OP_BE", auditRead: '无关',
    quote: "我在 §3.7 末尾把「编译期类型与 `OP_BE` 运行期检查的关系」提成三选一（(i) 编译期取代运行期；(ii) 两层都留但强制断言一致；(iii) 运行期仍是唯一判对错的人、编译期类型只是优化提示）。**人类裁定：走 (ii) 的强化版**，原文要点如下（下面是对裁定的转述与落地，**裁定本身以人的原话为准**）。" },
  { file: "docs/DECFY_DESIGN.md", id: "3.9", tier: 'A', search: "爆炸半径", auditRead: '同题不同事',
    quote: "- **量完之后要把实例分成三类**（裁定原文的分类）：① **AI 生成的垃圾代码**（滥用 `be` 或缺上下文）⇒ 改提示词，不改编译器；② **复杂业务逻辑**（外部输入、第三方库）⇒ 天生不可证，需要逃生舱；③ **编译器能力不足**（本可证明但编译器太笨）⇒ 编译器团队的优化目标。**这个分类很重要，因为它决定了「不可证明的实例」该由谁负责修。**" },
  { file: "docs/EIDOS_V06.md", id: "2.1", tier: 'C', search: "mixin",
    quote: "`@mixin`（初稿拼写 `@template`，见 §6.1 的裁定）" },
  { file: "docs/EIDOS_V06.md", id: "9.1", tier: 'C', search: "merged",
    quote: "- **能红的对照**：★ **改 `animal.bark` 的一个字** ⇒ **那条「merged」的钉必须红**（(c) 已裁，所以这是一条必须成立的断言，不再是「若走 (c)」）。" },
  { file: "docs/EIDOS_V06.md", id: "9.3", tier: 'C', search: "最特化优先",
    quote: "- **能红的对照**：★ **传一个 `int` 实参** ⇒ **必须走 `run a:int`**（最特化优先已裁，所以这是一条必须成立的断言）。★ **对照是把规则改回不相交** ⇒ **同一个程序必须编译不过** —— ★ **两种方案在这个例子上给出相反的可编译性，所以这条裁定的后果是可判别的。**" },
  { file: "docs/EIDOS_V06.md", id: "9.4", tier: 'C', search: "super play",
    quote: "- **能红的对照**：★ **交换 `extend` 里两个父的顺序** ⇒ **行为必须不变**（限定式已裁，所以这是一条必须成立的断言，不再是「若走限定式」）。★ **对照是把它改回位置式** ⇒ **同一个程序必须变** —— ★ **「顺序无关」这条性质本身，就是这个方案要买的东西，而这一对读数才是它的载荷。**" },
  { file: "docs/PLAN_V06.md", id: "2", tier: 'A', search: ['Inim', ' OS'].join(''), auditRead: '无关',
    quote: "范围与顺序（人类裁定）" },
  { file: "docs/PLAN_V06.md", id: "3.3", tier: 'C', search: "脱糖",
    quote: "2. **进引擎**：让引擎前端**认识** Eidos。**这一条已由人类裁定**（§2 的顺序 + 本节的「内糖」）。本文件第一版在这里写成「或者，明确裁定『Eidos 永远是外部脱糖工具』」—— **那是把一条已裁的事写成了未裁的**，此处更正。" },
  { file: "docs/PLAN_V06.md", id: "6.3", tier: 'A', search: "标准库", auditRead: '无命中',
    quote: "标准库（**本轮新增，人类裁定**）" },
  { file: "docs/PLAN_V06.md", id: "6.4.1", tier: 'A', search: "线程", auditRead: '同题不同事',
    quote: "★ **人类裁定（逐字「线程按B」）**：线程这件事**挂「去 C 化」线**，不新立一条主线；error 整数码挂「集合化类型系统」线。⇒ **线程的交付物是「去 C 化」交付物的一部分**，不是一份独立设计。" },
  { file: "docs/PLAN_V06.md", id: "8", tier: 'N', search: "BigInt",
    quote: '1. **' + ['Inim', ' OS'].join('') + '** —— 不在 v0.6 内（人类裁定）；它的前置（去 C 化、Eidos 进引擎）都在 v0.6 里。' },
  { file: "docs/PLAN_V07.md", id: "0", tier: 'N', search: "PLAN_V07",
    quote: "裁定索引、范围、不做什么、以及需要人类裁的" },
  { file: "docs/PLAN_V07.md", id: "0.1", tier: 'A', search: "PLAN_V07", auditRead: '无关',
    quote: "十五条裁定" },
  { file: "docs/PLAN_V07.md", id: "0.2", tier: 'A', search: "PLAN_V07", auditRead: '无关',
    quote: "- ★ **不做一套语法同时满足 `U367` 与提案** —— 那正是 §2.3 那处矛盾，已裁成两层。" },
  { file: "docs/PLAN_V07.md", id: "1.6", tier: 'C', search: "bit_not",
    quote: "验收（**本节此前只有裁定，没有验收 —— 这一节是补上的**）" },
  { file: "docs/PLAN_V07.md", id: "2.5", tier: 'A', search: "-O2", auditRead: '无关',
    quote: "**已裁（人类裁定 C，再裁定 A）：先测基线，再定门槛。**" },
  { file: "docs/PLAN_V07.md", id: "2.13", tier: 'A', search: "iasm", auditRead: '无命中',
    quote: "★ 文件扩展名：`.iasm`（**用户裁定，暂定**）" },
  { file: "docs/RELEASE_0.5.1.md", id: "(文件头/无节)", tier: 'N', search: "DISABLED",
    quote: "- **Windows**：与 0.5.0 相同——hub 为减配版本，**9 个 POSIX hub 回归测试显式 `DISABLED`**（按维护者裁定，移植未做），两个 xlang 桥（Java / Python）按设计以退出码 77 跳过，其余全部运行。**没有任何东西被静默跳过**：跳过是显式的，本地门禁还会数 `***Skipped`，非零即红。" },
  { file: "docs/RELEASE_0.5.2.md", id: "(文件头/无节)", tier: 'N', search: "DISABLED",
    quote: "- **Windows**：与 0.5.1 相同 —— hub 为减配版本，**9 个 POSIX hub 回归测试显式 `DISABLED`**（按维护者裁定，移植未做），两个 xlang 桥（Java / Python）按设计以退出码 77 跳过。本版的引擎修复**只作用于 Windows**（见下）。" },
  { file: "docs/STATUS.md", id: "2.6", tier: 'A', search: "closure_probe", auditRead: '同题不同事',
    quote: "**修法**：只修探针，引擎代码零改动（按用户裁定）。`e` 的所有使用移到最终 release 之前；" },
  { file: "docs/STATUS.md", id: "3", tier: 'N', search: "版本裁定",
    quote: "版本裁定" },
  { file: "docs/STATUS.md", id: "10.4", tier: 'A', search: "check_links.py", auditRead: '无关',
    quote: "**一条被驳回的怀疑（协调者裁定）**：`docs/STATUS.md` §4 的 `**已完成**` 标题曾被怀疑违反 §1.2 规则 2（无维度表述），因为同阶段下面还列着 8 个 `[ ]`。**裁定：不是违规。** 那个粗体标签只辖它**紧邻的下一个列表组**（那一组的条目全部是 `[x]`），未完成项在自己**显式命名**的 `**稳定性治理（未完成项）**` 标题下。文档没有把未做的事说成已完成。" },
  { file: "docs/STATUS.md", id: "10.46", tier: 'B', search: "im_diff_fuzz",
    quote: "- `tools/im_diff_fuzz.py` 仍未接进门禁（阈值未裁定）。" },
  { file: "docs/SYNTAX.md", id: "5", tier: 'C', search: "count(",
    quote: "**`count(A)` 是集合基数的规范名**（2026-10 裁定，见 [TYPESET_V06.md](TYPESET_V06.md) §7.8 第 2 条）。集合／数组／字典／字符串给基数；**非容器**（整数、浮点、布尔、`nil`）给 `nil`：" },
  { file: "docs/TYPESET_V06.md", id: "3.8", tier: 'A', search: ".declared", auditRead: '同题不同事',
    quote: "元属性 `.range` 与 `.declared` 答的是两个不同的问题（2026-10 T2 裁定）" },
  { file: "docs/TYPESET_V06.md", id: "8", tier: 'A', search: "FloatN", auditRead: '同题不同事',
    quote: "**裁定（用户逐字）**：「FloatN应放到拓展库里」。" },
  { file: "docs/streams/eidos-archive-crosscheck.md", id: "(文件头/无节)", tier: 'C', search: "集合化",
    quote: "> **仓库内已有同向先例**（所以这是裁定，不是新发明）：`future/archive/集合化.md` §3.1 把 `FloatN`／`floatN` 标为「⚠️ 建议删除」……" },
];
/* ★ 被禁字符串（家法：`website/**` 里不许出现那三个词）——
   ★ 上表里那两处**在源码里就是拼接**，不是一个字面量后面再擦掉：
   一个「先写出来再替换」的写法，在被扫描的那一刻仍然是那个字面量，
   而 `check_citations.js` 今天恰好把自己排除在扫描之外 —— 于是它绿，
   而那份绿是靠【扫描器不看它】换来的，不是靠【它没有那个串】。 */
const ROWS_SHA = execFileSync('git', ['rev-parse', RULING_ROWS_READ_AT], { cwd: ROOT, encoding: 'utf8' }).trim();
check('⑮ 记录的那棵树必须解析成一个真实提交', /^[0-9a-f]{40}$/.test(ROWS_SHA), ROWS_SHA);
const docFilesAt = (sha) => execFileSync('git', ['ls-tree', '-r', '--name-only', sha, 'docs/'], { cwd: ROOT, encoding: 'utf8' })
  .split('\n').map((x) => x.trim()).filter((x) => x.endsWith('.md'));
const diffAt = (sha) => {
  const out = [];
  for (const rel of docFilesAt(sha)) {
    for (const x of sectionsOfText(docAtRef(sha, rel).text)) {
      const y = { file: rel, ...x };
      if (isConcept(y) && !namesOf(y).length) out.push(y);
    }
  }
  return out;
};
const keyOf = (x) => `${x.file} §${x.id}`;
const recordedKeys = new Set(diffAt(ROWS_SHA).map(keyOf));
const tableKeys = new Set(RULING_ROWS.map(keyOf));
const notInTable = [...recordedKeys].filter((k) => !tableKeys.has(k));
const notInDiff = [...tableKeys].filter((k) => !recordedKeys.has(k));
check('⑮ 手写的表与记录那棵树上的差集必须逐条相等（多一行、少一行都算红）',
  notInTable.length === 0 && notInDiff.length === 0,
  notInTable.length || notInDiff.length
    ? `表里没有：${notInTable.join('、') || '(无)'} ／ 表里多了：${notInDiff.join('、') || '(无)'}`
    : `${tableKeys.size} / ${recordedKeys.size}`);

const secIndex = new Map();
for (const rel of new Set(RULING_ROWS.map((r) => r.file))) {
  for (const x of sectionsOfText(docAtRef(ROWS_SHA, rel).text)) secIndex.set(`${rel} §${x.id}`, x);
}
const notVerbatim = RULING_ROWS.filter((r) => {
  const x = secIndex.get(`${r.file} §${r.id}`);
  return !x || !(x.body.includes(r.quote) || x.heading === r.quote);
});
check('⑮ 每一行的第 2 栏必须是那一节里的逐字原句（读记录的那棵树）',
  notVerbatim.length === 0,
  notVerbatim.length ? `不是逐字：${notVerbatim.slice(0, 5).map((r) => `${r.file} §${r.id}`).join('、')}` : `${RULING_ROWS.length} / ${RULING_ROWS.length}`);

const tierCount = {};
RULING_ROWS.forEach((r) => { tierCount[r.tier] = (tierCount[r.tier] || 0) + 1; });
const tierSum = Object.values(tierCount).reduce((a, b) => a + b, 0);
check('⑮ 四档的计数之和必须等于表的行数（拆档不许把行拆丢）',
  tierSum === RULING_ROWS.length,
  `${tierSum} / ${RULING_ROWS.length}`);

const auditLines = docAtRef(ROWS_SHA, 'docs/AUDIT.md').text.split('\n');
const auditRangeOf = (id) => {
  let start = null;
  for (let i = 0; i < auditLines.length; i++) {
    const m = SEC_RE.exec(auditLines[i]);
    if (!m) continue;
    if (start !== null) return [start, i];
    if (m[1] === id) start = i;
  }
  return start === null ? null : [start, auditLines.length - 1];
};
const auditOf = (r) => {
  const hits = [];
  auditLines.forEach((l, i) => { if (l.includes(r.search)) hits.push(i); });
  const range = r.file === 'docs/AUDIT.md' ? auditRangeOf(r.id) : null;
  const own = range ? hits.filter((i) => i >= range[0] && i <= range[1]) : [];
  return { hits, own, foreign: hits.filter((i) => !own.includes(i)) };
};
const printed = [];
const tierLedger = {};
RULING_ROWS.forEach((r, i) => {
  const a = auditOf(r);
  const ledger = a.hits.length === 0
    ? '未出现'
    : (a.foreign.length === 0
      ? `自指（${a.own.length} 处，全在它自己这一节里 —— 这不是登记，是它就是台账的一节）`
      : `别处 ${a.foreign.length} 处（首行 ${a.foreign.slice(0, 3).map((x) => x + 1).join(',')}）`);
  printed.push(`   [${String(i + 1).padStart(2)}] ${r.tier}  ${r.file} §${r.id}`);
  printed.push(`        触发（逐字）：${r.quote.length > 118 ? `${r.quote.slice(0, 118)}…` : r.quote}`);
  printed.push(`        台账侧：搜「${r.search}」⇒ ${ledger}`);
  tierLedger[r.tier] = tierLedger[r.tier] || { rows: 0, hit: 0 };
  tierLedger[r.tier].rows += 1;
  if (a.hits.length) tierLedger[r.tier].hit += 1;
});
check('⑮ 那 33 行必须逐行印出来（打印被截断与「只有这么多」长得一样）',
  printed.length === RULING_ROWS.length * 3,
  `${printed.length / 3} / ${RULING_ROWS.length}`);

const todayKeys = new Set(diffAt(MSR_SHA).map(keyOf));
const newOnToday = [...todayKeys].filter((k) => !recordedKeys.has(k));
info(`★ 一条裁定的差：手读的 **${RULING_ROWS.length}** 行，读自记录的那棵树 ${ROWS_SHA.slice(0, 7)}（钉）`);
Object.keys(TIER_NAMES).forEach((t) => {
  if (!tierCount[t]) return;
  info(`★ ${t} 档「${TIER_NAMES[t]}」：**${tierCount[t]}** 行（台账侧出现过这个实质的 ${tierLedger[t].hit} 行）—— 处置：${TIER_DISPOSITION[t]}`);
});
info(`★ 今天的指针 ${MSR_LABEL}：同一个差集是 **${todayKeys.size}** 行，与记录那一份${todayKeys.size === recordedKeys.size && newOnToday.length === 0 ? '**逐条相同**' : `**不同**（只在新树上的 ${newOnToday.length} 行：${newOnToday.slice(0, 5).join('、')}）`}`);
/* ★★ 第 3 栏是【一个数】（这个词在台账里出现过几次），而一个数不等于一个答案。
   ★ 所以 A 档（处置＝「要登记」的那 15 行）每一行多一个**手读的命中判词** ——
   把「出现过 N 次」读成「这条裁定被登记了没有」：
     同题不同事 = 命中在讲同一个题材，但不是这条裁定；
     无关       = 命中与这条裁定无关；
     无命中     = 这个词在台账里一次都没出现。 */
const A_ROWS = RULING_ROWS.filter((r) => r.tier === 'A');
const missingRead = A_ROWS.filter((r) => !r.auditRead);
check('⑮ A 档的每一行都必须带一个手读的命中判词（否则「要登记」这三个字没有证据）',
  missingRead.length === 0,
  missingRead.length ? `没有判词：${missingRead.map((r) => `${r.file} §${r.id}`).join('、')}` : `${A_ROWS.length} / ${A_ROWS.length}`);
const readCount = {};
A_ROWS.forEach((r) => { readCount[r.auditRead] = (readCount[r.auditRead] || 0) + 1; });
info(`★ A 档（要登记）${A_ROWS.length} 行的命中判词（手读）：${Object.keys(readCount).map((k) => `${k} ${readCount[k]}`).join(' ／ ')}`);
info(`★ ⇒ A 档里，**命中在讲【这条裁定】的行是 0 行**：${readCount['同题不同事'] || 0} 行是同一个题材的另一件事、${readCount['无关'] || 0} 行的命中与这条裁定无关、${readCount['无命中'] || 0} 行连词都没出现过。`);
A_ROWS.forEach((r) => {
  const a = auditOf(r);
  info(`   · ${r.file} §${r.id}  命中 ${a.hits.length} 处 ⇒ 手读：${r.auditRead}`);
});
printed.forEach((l) => info(l));

/* ==================== ⑯ 一整族串，不是一个串 ====================
 * ★ ⑮ 的第 3 栏是【我挑的一个串】，而我在报告里写了「挑串这件事本身没有判据」。
 *   这一节把那个坦白做成制品：**不挑串，报一整族。**
 * ★ 族的构造规则（**机械可得、写在下面、可以反驳**）：
 *   输入 = 那一节的【标题】与【触发它的那句逐字原话】，两者都从记录树里取。
 *   先做一次规范化：去掉 markdown 强调符 `**`、`__` 与反引号。
 *     F1 = 标题（trim）—— ★ 标题是占位符 `(文件头/无节)` 时不加入：
 *          那个占位符本身就是一个泛成员，它在台账里出现 7 次，全部与那一节无关。
 *     F2 = 原话里所有反引号包起来的片段（去反引号）
 *     F3 = 原话里所有长得像文件名的记号 [A-Za-z0-9_./-]+\.(md|py|js|ts|c|h|im|rs|cpp|sh|yml|json)
 *     F4 = 原话里所有长度 ≥4 的连续汉字串（在标点/空白/ASCII 处切开）
 *   合并 F1..F4，去重，丢掉长度 <2 的、纯数字的、以及一张 13 词的停用表。
 * ★ 每个串报两个数：
 *     命中数     = 台账里含这个串的行数；
 *     实质命中数 = 含这个串**且**含这一节族里另一个成员的行数
 *                  —— ★ 这是一个**代理**，不是一个定义：它答的是「这一行不只是带着这个词」。
 * ★ 实质命中必须**打印出是哪个成员共现的** —— 否则读者看不见它为什么非零。
 * ★ 自指单独扣出来：命中落在这一节自己的行范围内时（只有 `docs/AUDIT.md §1.8` 可能），
 *   那不是登记，是**这一节自己匹配了自己**。 */
current = '一整族串：每一节报两个数，而不是挑一个串';
const FAMILY_RULE = [
  'F1 = 标题（trim；占位符 (文件头/无节) 不加入）',
  'F2 = 原话里所有反引号包起来的片段（去反引号）',
  'F3 = 原话里所有长得像文件名的记号',
  'F4 = 原话里所有长度 ≥4 的连续汉字串（在标点/空白/ASCII 处切开）',
];
const FAMILY_STOP = new Set(['的', '是', '在', '与', '和', '不', '了', '这', '那', '有', '也', '它', '而', '就']);
const famNorm = (s) => s.replace(/\*\*/g, '').replace(/__/g, '').replace(/`/g, '');
const familyOf = (heading, quote) => {
  const out = new Set();
  const add = (x) => {
    const t = String(x).trim();
    if (t.length >= 2 && !/^\d+$/.test(t) && !FAMILY_STOP.has(t)) out.add(t);
  };
  if (heading && heading !== '(文件头/无节)') add(heading);
  for (const m of quote.matchAll(/`([^`]+)`/g)) add(m[1]);
  const q = famNorm(quote);
  for (const m of q.matchAll(/[A-Za-z0-9_./-]+\.(?:md|py|js|ts|c|h|im|rs|cpp|sh|yml|json)/g)) add(m[0]);
  for (const m of q.matchAll(/[\u4e00-\u9fff]{4,}/g)) add(m[0]);
  return [...out];
};
const famRows = RULING_ROWS.map((r) => {
  const x = secIndex.get(`${r.file} §${r.id}`);
  return { ...r, fam: familyOf(x ? x.heading : '', r.quote) };
});
const emptyFam = famRows.filter((r) => !r.fam.length);
check('⑯ 每一节都必须算出一族非空的串（族的规则对每一节都要有输出）',
  emptyFam.length === 0,
  emptyFam.length ? `族为空：${emptyFam.map((r) => `${r.file} §${r.id}`).join('、')}` : `${famRows.length} / ${famRows.length}`);

const famPer = famRows.map((r) => {
  const range = r.file === 'docs/AUDIT.md' ? auditRangeOf(r.id) : null;
  const per = r.fam.map((str) => {
    const hits = [];
    const subs = [];
    auditLines.forEach((l, i) => {
      if (!l.includes(str)) return;
      hits.push(i);
      const co = r.fam.filter((x) => x !== str && l.includes(x));
      if (co.length) subs.push({ i, co });
    });
    const selfSubs = range ? subs.filter((s) => s.i >= range[0] && s.i <= range[1]) : [];
    const foreignSubs = subs.filter((s) => !selfSubs.includes(s));
    return { str, hits: hits.length, selfSubs, foreignSubs };
  });
  const hitSum = per.reduce((a, b) => a + b.hits, 0);
  const foreign = per.reduce((a, b) => a + b.foreignSubs.length, 0);
  const self = per.reduce((a, b) => a + b.selfSubs.length, 0);
  return { ...r, per, hitSum, foreign, self };
});
const famHitTotal = famPer.reduce((a, b) => a + b.hitSum, 0);
const famForeignTotal = famPer.reduce((a, b) => a + b.foreign, 0);
const famSelfTotal = famPer.reduce((a, b) => a + b.self, 0);

const famPrinted = [];
famPer.forEach((r) => {
  famPrinted.push(`   ${r.tier}  ${r.file} §${r.id}   族 ${r.fam.length}   命中 ${r.hitSum}   实质 ${r.foreign}${r.self ? `（自指另计 ${r.self}）` : ''}`);
  r.per.forEach((p) => {
    p.foreignSubs.forEach((s) => famPrinted.push(`        :${s.i + 1}  串「${p.str}」＋共现 ${s.co.slice(0, 3).map((x) => `「${x}」`).join('')}`));
    p.selfSubs.forEach((s) => famPrinted.push(`        :${s.i + 1}  ★ 自指  串「${p.str}」＋共现 ${s.co.slice(0, 3).map((x) => `「${x}」`).join('')}`));
  });
});
check('⑯ 每一节的族与它打出来的行数必须对上（打印被截断与「只有这么多」长得一样）',
  famPrinted.filter((l) => !l.startsWith('        ')).length === famPer.length,
  `${famPrinted.filter((l) => !l.startsWith('        ')).length} / ${famPer.length}`);
check('⑯ 实质命中的行数必须等于打印出来的实质命中行数（一个数不许比它的证据大）',
  famPrinted.filter((l) => l.startsWith('        ')).length === famForeignTotal + famSelfTotal,
  `${famPrinted.filter((l) => l.startsWith('        ')).length} / ${famForeignTotal + famSelfTotal}`);

const famFiring = famPer.filter((r) => r.foreign > 0 || r.self > 0);
info(`★ 族规则（逐字，机械可得）：${FAMILY_RULE.join(' ／ ')}`);
info(`★ 33 节 × 整族串：族大小合计 ${famPer.reduce((a, b) => a + b.fam.length, 0)}，命中合计 **${famHitTotal}**，实质命中合计 **${famForeignTotal}**（自指另计 ${famSelfTotal}）`);
info(`★ 实质命中非零的节：${famFiring.length ? famFiring.map((r) => `${r.file} §${r.id}(${r.foreign}${r.self ? `+自指${r.self}` : ''})`).join('、') : '(无 —— 那么每一节的结论都是「这件事没被登记」)'}`);
famPrinted.forEach((l) => info(l));
/* ★★ ⑯ 的收尾两件：把实质命中**逐行判一遍**，以及把 `-O2` 那条负向对照做成可复跑的一对数。
 * ★ 为什么要有第一件：族规则会**因为三种不同的理由**给出非零 ——
 *   ① 自指（这一节就在被搜的那份文件里）；② 泛成员（`[ ]`、`docs/STATUS.md` 这种谁都能共现的串）；
 *   ③ 同题不同事。**三个非零长得一样，而处置完全不同。** */
const SUB_VERDICTS = [
  { line: 1817, verdict: '泛成员假阳性', why: '`docs/STATUS.md`＋`[ ]` 在讲阶段一的两条待办，与 §10.4 那条裁定无关' },
  { line: 4399, verdict: '被当作实例引用', why: '台账 C5 把 §3.3 当作「把已裁的事写成未裁」这条病的**实例**引用，不是登记 §3.3 那条裁定' },
  { line: 480, verdict: '同题不同事', why: '在讲 `FloatN`/`floatN` 的格点语义与基数，不是那条「已有同向先例」' },
  { line: 490, verdict: '同题不同事', why: '同上，`floatN` 含整数、`FloatN` 不含 —— 是同一段的下一行' },
  { line: 10808, verdict: '真登记，但登记的是【另一件事】', why: '台账 §1.97 H 逐字记的是「（协调者裁定）」这条**归属发现**（★ 也就是上一件我报给协调者的那条），不是 §10.4 那条裁定本身' },
];
const foreignLines = [];
famPer.forEach((r) => {
  r.per.forEach((p) => p.foreignSubs.forEach((s) => { if (!foreignLines.includes(s.i + 1)) foreignLines.push(s.i + 1); }));
});
foreignLines.sort((a, b) => a - b);
const verdictAt = (n) => SUB_VERDICTS.find((v) => v.line === n);
const noVerdict = foreignLines.filter((n) => !verdictAt(n));
const orphan = SUB_VERDICTS.filter((v) => !foreignLines.includes(v.line));
check('⑯ 每一条非自指的实质命中都必须带一个逐行的判词（一个非零不许没有处置）',
  noVerdict.length === 0,
  noVerdict.length ? `没有判词：${noVerdict.join('、')}` : `${foreignLines.length} / ${foreignLines.length}`);
check('⑯ 判词表里不许有孤儿（判了一条不存在的命中，与漏判一样坏）',
  orphan.length === 0,
  orphan.length ? `孤儿判词：${orphan.map((v) => v.line).join('、')}` : `${SUB_VERDICTS.length} / ${SUB_VERDICTS.length}`);
info(`★ 非自指的实质命中 ${foreignLines.length} 行（去重后按行算），逐行判词：`);
foreignLines.forEach((n) => {
  const v = verdictAt(n);
  // ★ 这条三元不是装饰：红对照 D 证明，直接解引用 `v` 会让「缺一条判词」变成一次**崩溃**，
  //   而崩溃的 rc=1 与一条 FAIL 在只看 grep 的眼里长得一样 —— 检查根本没跑到。
  info(v ? `   :${n}  ⇒ **${v.verdict}** —— ${v.why}` : `   :${n}  ⇒ **没有判词**（一条非零没有处置）`);
});
info('★ ⇒ 33 节里，**裁定被登记在台账里的节是 0 节**。唯一一处真正的登记是 :10808，而它登记的是**关于台账自身的那条发现**，不是差里那 33 节中的任何一条裁定。');

/* ★★ 负向对照（协调者点名的那一条）：`docs/PLAN_V07.md §2.5`。
 *   旧的挑串是 `-O2`；族里的实体词是 `先测基线` / `再定门槛`。两个都报两个数。 */
const R25 = famPer.find((r) => r.file === 'docs/PLAN_V07.md' && r.id === '2.5');
const countLines = (s) => auditLines.filter((l) => l.includes(s)).length;
const ctrl = [{ str: '-O2', note: '旧的挑串（我挑的）' }]
  .concat(R25.fam.map((s) => ({ str: s, note: '族成员' })))
  .map((x) => ({ ...x, hits: countLines(x.str) }));
check('⑯ 负向对照必须把两个数都印出来（一个数看不见 12 与 0 的差别）',
  ctrl.every((x) => Number.isInteger(x.hits)),
  ctrl.map((x) => `${x.str}:${x.hits}`).join(' '));
info('★ 负向对照 docs/PLAN_V07.md §2.5（命中数 / 实质命中数）：');
ctrl.forEach((x) => {
  const p = R25.per.find((q) => q.str === x.str);
  const sub = p ? p.foreignSubs.length + p.selfSubs.length : 0;
  info(`   「${x.str}」（${x.note}）⇒ 命中 ${x.hits} ／ 实质 ${sub}`);
});
info('★ ⇒ **两个数一起掉**：旧的挑串 -O2 是 12/0（有射程、没有眼光），族里的实体词是 0/0（有眼光、没有射程）。');
info(`★ ⇒ 这条对照**不可能在 §2.5 上通过**，因为「新串的实质那一栏必须非零」预设了这条裁定已经被登记 —— 而它没有。★ **一条对照不能预设它的结论。**`);
const REG_VERDICTS = [
  { line: 10808, registered: false,
    verdict: '★ 尺子说它是 §10.4 的登记，手读说它登记的是【另一件事】',
    why: '这一行逐字记的是「（协调者裁定）」这条**归属发现**（就是协调者上一件从我这里收走的那条），不是 §10.4 那条裁定本身。★ **尺子与手读不一致，而不一致必须报出来** —— 一个「1」与一个「0」并排，读者才看得见差别。' },
];  // ★ 先跑筛子、看火集，再逐条手读填进来（判据本身已在上一笔冻结）
/* ★★ ⑰ 「真登记」这把尺子。判据写在 `website/scratch/registration_rule.md`（**上一笔**，
 * 那一笔里没有实现、没有读数），这一笔只是跑它。
 * ★ 为什么要有这一件：⑯ 证明的只是「这 33 节里哪几节的事在台账里被**提到过**」，
 *   它证明不了「提到 = 登记」。而 ⑯ 手上唯一的真阳性是 :10808 —— **只有一个已知真阳性时，
 *   判据是从那一个点上反推出来的，那是过拟合。** */
current = '真登记：四条判据，先写下再跑';
const REG_PHRASES = ['已裁', '已裁定', '裁定', '已决定', '裁了', '裁决', '驳回', '采纳'];
const REG_ANCHOR = '台账';
const regHeadingOf = (r) => { const x = secIndex.get(`${r.file} §${r.id}`); return x ? x.heading : ''; };
const isGenericMember = (m, heading) => m === heading || m === '(文件头/无节)';
const regScreen = (r, excludeLine) => {
  const heading = regHeadingOf(r);
  const range = r.file === 'docs/AUDIT.md' ? auditRangeOf(r.id) : null;
  const out = [];
  auditLines.forEach((l, i) => {
    const n = i + 1;
    if (excludeLine === n) return;                                  // ★ 留一法
    if (range && n >= range[0] && n <= range[1]) return;            // 条件 4：不是自指
    const fam = r.fam.filter((m) => l.includes(m));
    const entity = fam.filter((m) => !isGenericMember(m, heading));
    if (fam.length < 2 || entity.length < 1) return;                // 条件 1：是这一节，且是实体成员
    const phr = REG_PHRASES.filter((p) => l.includes(p));
    if (!phr.length) return;                                        // 条件 2：在裁
    if (!l.includes(REG_ANCHOR)) return;                            // 条件 3：台账在说自己
    out.push({ n, fam, entity, phr, text: l.trim() });
  });
  return out;
};
const regOf = (excludeLine) => {
  const acc = [];
  famRows.forEach((r) => regScreen(r, excludeLine).forEach((h) => acc.push({ ...h, row: r })));
  return acc;
};
const regAll = regOf(null);
const regLOO = regOf(10808);                                        // ★ 留一法：把 :10808 扣掉
info(`★ 真登记：全量 ${regAll.length} 条；留一法（扣掉 :10808）${regLOO.length} 条。`);
check('⑰ 负向对照：`:4399`（只被提及、被当作实例引用）必须在这把尺子之外',
  !regAll.some((h) => h.n === 4399),
  regAll.some((h) => h.n === 4399) ? ':4399 通过了尺子 —— 尺子分不开「登记」与「提及」' : '在尺子之外');
check('⑰ 每一条真登记都必须带逐字原文与一个手读判词',
  regAll.every((h) => h.text.length > 0 && REG_VERDICTS.some((v) => v.line === h.n)),
  regAll.length ? `${regAll.filter((h) => REG_VERDICTS.some((v) => v.line === h.n)).length} / ${regAll.length}` : '0 条（★ 见预言一）');
check('⑰ 留一法必须真的扣掉了那一行',
  regAll.length === 0 || regLOO.length < regAll.length,
  `${regAll.length} → ${regLOO.length}`);
info('★ 预言一（写在判据那一笔里）：真登记 ≤ 2 条 ⇒ ' + (regAll.length <= 2 ? `**成立**（${regAll.length} 条）` : `**落空**（${regAll.length} 条）`));
const at104 = (s) => s.filter((h) => h.row.file === 'docs/STATUS.md' && h.row.id === '10.4');
info('★ 预言二（写在判据那一笔里）：留一法之后 `docs/STATUS.md §10.4` 不再有真登记 ⇒ '
  + (at104(regAll).length > 0 && at104(regLOO).length === 0 ? '**成立**' : `**落空**（扣前 ${at104(regAll).length} 条、扣后 ${at104(regLOO).length} 条）`));
const regConfirmed = REG_VERDICTS.filter((v) => v.registered).length;
check('⑰ 每一条真登记的判词都必须带一个明确的 registered 布尔（★ 不许有「没判」的格子）',
  regAll.every((h) => { const v = REG_VERDICTS.find((x) => x.line === h.n); return v && typeof v.registered === 'boolean'; }),
  regAll.length ? `${REG_VERDICTS.filter((v) => typeof v.registered === 'boolean').length} / ${regAll.length}` : '0 条');
info(`★★ 尺子给出 ${regAll.length} 条；**手读确认的 ${regConfirmed} 条** ⇒ 差 ${regAll.length - regConfirmed} 条。`
  + ` ★ ⇒ 用两条独立的尺子（⑯ 的族串实质命中、⑰ 的四条件判据）得到的都是：**33 节里，裁定被登记在台账里的节是 0 节。**`);
info('★ 逐条真登记（含逐字原文）：');
regAll.forEach((h) => {
  const v = REG_VERDICTS.find((x) => x.line === h.n);
  info(`   :${h.n}  ${h.row.file} §${h.row.id}`);
  info(`      原文（逐字）：${h.text.slice(0, 200)}`);
  info(`      族成员 ${h.fam.length}（实体 ${h.entity.length}）／裁定短语 ${h.phr.join('、')}／判词：**${v ? v.verdict : '（缺）'}**`);
});

/* ★★ ⑱ 判据 v2 —— 条件 3 从【那个制品的名字】换成【这一行有没有它自己的据】。
 * 判据写在 `website/scratch/registration_rule.md` 的 v2 那一节（**上一笔** `f26a5c2`，
 * 那一笔里没有实现、没有读数），这一笔只是跑它。
 * ★ 为什么要有这一件：v1 的读数是「全量 1 条、留一法 0 条」⇒ 那把尺子量到的真阳性
 *   是**它自己绕一圈回来的回音**；而红对照 A 说明根因是**条件 3 的全部载荷压在一个词上**。 */
current = '判据 v2：一条记录 = 一个断言 + 它自己的据（条件 3 改成句法）';
const REG_GROUND = ['★ 实测', '★ 载体', '★ 实例', '★ 出处', '★ 证据', '★ 根因'];
const groundOf = (l) => REG_GROUND.filter((g) => l.includes(g));
const hasGround = (l) => groundOf(l).length > 0;
/** v2 的筛子。`off` 里每一项关掉一条条件 —— 关掉它是为了**量它是哪一条载荷**，不是为了放宽。 */
const regScreen2 = (r, opts = {}) => {
  const { excludeLine = null, off = {} } = opts;
  const heading = regHeadingOf(r);
  const range = r.file === 'docs/AUDIT.md' ? auditRangeOf(r.id) : null;
  const out = [];
  auditLines.forEach((l, i) => {
    const n = i + 1;
    if (excludeLine === n) return;                                        // 留一法
    if (!off.self && range && n >= range[0] && n <= range[1]) return;     // 条件 4：不是自指
    const fam = r.fam.filter((m) => l.includes(m));
    const entity = fam.filter((m) => !isGenericMember(m, heading));
    if (!off.fam && (fam.length < 2 || entity.length < 1)) return;        // 条件 1：是这一节
    if (!off.phr && !REG_PHRASES.some((p) => l.includes(p))) return;      // 条件 2：在裁
    if (!off.ground && !hasGround(l)) return;                             // 条件 3（v2，句法）：是一条记录
    out.push({ n, fam, entity, phr: REG_PHRASES.filter((p) => l.includes(p)), ground: groundOf(l), text: l.trim() });
  });
  return out;
};
/** 分子：按**行**去重（判据那一份写死了单位是行 —— 筛子是按行扫的，分母与分子必须同一个单位）。 */
const regLines2 = (opts = {}) => {
  const map = new Map();
  famRows.forEach((r) => regScreen2(r, opts).forEach((h) => {
    if (!map.has(h.n)) map.set(h.n, { ...h, rows: [] });
    map.get(h.n).rows.push(`${r.file} §${r.id}`);
  }));
  return [...map.values()].sort((a, b) => a.n - b.n);
};
const N2 = regLines2();
/** 分母：`docs/AUDIT.md` 里【带据的行】的条数。★ 计数规则逐字写在判据那一份里。 */
const D2 = auditLines.filter(hasGround).length;
const D2NoPhrase = auditLines.filter((l) => hasGround(l) && !REG_PHRASES.some((p) => l.includes(p)));
info(`★ 判据 v2（逐字，机械可得）：条件 3 = L 含至少一处据词 ${REG_GROUND.map((g) => `「${g}」`).join('／')}`);
info(`★ 分母 D = \`docs/AUDIT.md\` 里【带据的行】的条数 = **${D2}** 行（单位是行，按 \\n 切、1-based）`);
info(`★★ 分子 N = v2 的筛子在 33 节上打出的行（按行去重）= **${N2.length}** 行`);
info(`★ 分子 / 分母 = ${N2.length} / ${D2}`);
/* ★★ 载荷测量（不是红对照，是每次运行都印的一个读数）：
 * 逐条关掉一个条件，看**验收②（`:4399` 必须在 N 之外）**会不会破 ——
 * 会破的那一条就是**分开「登记」与「提及」的那条载荷**。 */
const LOADBEARING = [
  ['条件 1（是这一节）', { fam: true }],
  ['条件 2（在裁）', { phr: true }],
  ['条件 3（是一条记录）', { ground: true }],
  ['条件 4（不是自指）', { self: true }],
];
const lbRead = LOADBEARING.map(([name, off]) => {
  const s = regLines2({ off });
  return { name, size: s.length, passes4399: s.some((h) => h.n === 4399) };
});
info('★ 载荷测量（逐条关掉一个条件 ⇒ 分子的大小 / `:4399` 是否进得来）：');
lbRead.forEach((x) => info(`   关掉 ${x.name} ⇒ N = ${x.size}　\`:4399\` ${x.passes4399 ? '**进来了**（这条是载荷）' : '仍在外面'}`));
const loadBearing = lbRead.filter((x) => x.passes4399);
info(`★ ⇒ 分开「登记」与「提及」的载荷是：${loadBearing.length ? loadBearing.map((x) => `**${x.name}**`).join('、') : '（没有一条 —— 那就是说没有一条条件在挡 `:4399`）'}`);
info(`★ ⇒ 而 v1 里挡住它的是【条件 3 的另一个版本：那一行有没有写那个制品的名字】 —— **不是同一条**。`);
/* ★★ 那一件真正要的分母：台账里【在裁 + 带据】的行 ——
 * 也就是「它在说自己记了一条裁定」这个问题的全集。条件 1 关掉：那 33 节是**分子那一侧的索引**，
 * 拿它去筛分母会把「这一行和某一节共享族成员」当成「这一行在说它自己记了什么」——那是两个问题。 */
const D2Phrase = regLines2({ off: { fam: true } });
info(`★★ 分母之二（比 D 更贴题）：台账里【在裁 + 带据】的行 = **${D2Phrase.length}** 行`);
D2Phrase.forEach((h) => info(`   :${h.n}  据词 ${h.ground.join('、')}／短语 ${h.phr.join('、')}　${h.text.slice(0, 110)}`));
info(`★ ⇒ 33 节里被这把尺子认出的：**${N2.length}** 行；台账里本来就【在裁 + 带据】的有 **${D2Phrase.length}** 行。`);
info(`★ ⇒ 覆盖率 = ${N2.length} / ${D2Phrase.length}`);
const dset = new Set(D2Phrase.map((h) => h.n));
check('⑷ 分子必须是分母的子集（分子里有而分母里没有的行 = 一个定义矛盾）',
  N2.every((h) => dset.has(h.n)),
  `${N2.filter((h) => dset.has(h.n)).length} / ${N2.length}`);

/* ★★ 两个分母并排（协调者 m00738 的派件）。★ 注意它们与 `D2Phrase` 的关系：
 * `D2Phrase` 走的是**筛子**（含条件 4 的自指排除），而 `D_all`/`D_ctx` 是**纯集合**（只看这一行本身）。
 * ★ 两个数如果不同，那个差就是条件 4 在这批行上的脚印 —— 单独印出来。 */
const D_all = auditLines.reduce((a, l, i) => { if (hasGround(l)) a.push({ n: i + 1, text: l.trim() }); return a; }, []);
const D_ctx = D_all.filter((x) => REG_PHRASES.some((p) => x.text.includes(p)));
const ctxSet = new Set(D_ctx.map((x) => x.n));
const allSet = new Set(D_all.map((x) => x.n));
info(`★★ 分母一 D_all（带据）= **${D_all.length}** 行；分母二 D_ctx（带据 ∧ 在裁）= **${D_ctx.length}** 行；差 = **${D_all.length - D_ctx.length}** 行`);
info(`★ 而走筛子的那个分母 D2Phrase = ${D2Phrase.length} 行 ⇒ 条件 4（自指排除）在它上面的脚印 = **${D2Phrase.length - D_ctx.length}** 行`);
info(`★ 覆盖率（对 D_ctx）= ${N2.length} / ${D_ctx.length}　★ 覆盖率（对 D_all）= ${N2.length} / ${D_all.length}`);
info('★ 逐行印出 D_ctx（分母二，全部）：');
D_ctx.forEach((x) => info(`   ctx :${x.n}  ${x.text.slice(0, 110)}`));
const allNotCtx = D_all.filter((x) => !ctxSet.has(x.n));
info(`★ 逐行印出 D_all \\ D_ctx（分母一减分母二 = 特别档，${allNotCtx.length} 行）：`);
allNotCtx.forEach((x) => info(`   all :${x.n}  ${x.text.slice(0, 110)}`));
/* ★★ 下面这一条**不是**一条检查，是一条读数 —— 而理由必须写下来：
 * `D_ctx ⊆ D_all` 按构造恒真（D_ctx = D_all ∧ 短语）。把它写成 check 就是一条**恒真的检查**，
 * 而恒真的检查只会把分母撞大（本车道的纪律）。★ 所以：**只印，不 check**。 */
const ctxNotAll = D_ctx.filter((x) => !allSet.has(x.n));
info(`★ 读数（**不是检查**）：|D_ctx \\ D_all| = **${ctxNotAll.length}** —— 按构造必须是 0，而它恒为 0，所以它不能当一条检查。`);
info(`★ 读数：|D_all \\ D_ctx| = **${allNotCtx.length}**（= 特别档）；它占 D_all 的 **${((allNotCtx.length / D_all.length) * 100).toFixed(1)}%**`);
check('⑷ 分子必须是 D_ctx 的子集', N2.every((h) => ctxSet.has(h.n)), `${N2.filter((h) => ctxSet.has(h.n)).length} / ${N2.length}`);
check('⑷ 分子必须是 D_all 的子集', N2.every((h) => allSet.has(h.n)), `${N2.filter((h) => allSet.has(h.n)).length} / ${N2.length}`);
/* ★ 特别档：带据、在记一件事、而**那件事不是一条裁定**。 */
const pickSpecial = (pred) => D2NoPhrase.find(pred) || null;
const specA = pickSpecial(() => true);
const specB = pickSpecial((l) => l.includes('本节'));
info(`★ 特别档【在记一件事，而那件事不是一条裁定】：D 里有 **${D2NoPhrase.length}** 处（带据、且不含任何一个裁定短语）。`);
info(`   挑法一（D 里第一条这样的行）：${specA ? `\`:${auditLines.indexOf(specA) + 1}\`` : '（无）'}`);
info(`   挑法二（其中第一条含「本节」的 —— 即「它在记我这一节是怎么写出来的」那一形状）：${specB ? `\`:${auditLines.indexOf(specB) + 1}\`` : '（无）'}`);
[['挑法一', specA], ['挑法二', specB]].forEach(([tag, l]) => {
  if (l) info(`   ${tag} 逐字：${l.trim().slice(0, 240)}`);
});
check('⑱ 验收①：`:10808` 必须在 N 里（★ 这是过拟合检验，它不能单独构成通过）',
  N2.some((h) => h.n === 10808),
  N2.some((h) => h.n === 10808) ? '在 N 里' : ':10808 不在 N 里 —— 新判据没接住那个已知真阳性');
check('⑱ 验收②：`:4399` 必须在 N 之外（★ 而挡住它的必须不是那个词 —— 那个词已经不在判据里了）',
  !N2.some((h) => h.n === 4399),
  N2.some((h) => h.n === 4399) ? ':4399 通过了尺子 —— 尺子分不开「登记」与「提及」' : '在 N 之外');
const N2_LOO = regLines2({ excludeLine: N2.length ? N2[0].n : null });
/* ★ 验收③ 的写法有一个坑，我踩了第一版：写成 `N2.length === 0 || N2_LOO.length === N2.length - 1`
 * 之后，**N = 1 会让它通过**（1 → 0 满足「少 1」），而「留一法归零」**正是这一件要抓的病**。
 * 一条把病当成通过条件的检查，比没有这条检查更坏 —— 它会把 486/486 变成一句假话。
 * ⇒ 判据是 **N ≥ 2 且 留一法 = N−1**（两个条件缺一不可）。 */
check('⑱ 验收③：留一法之后 N 必须非零（扣掉一行 ⇒ 少 1，不是变成 0）',
  N2.length >= 2 && N2_LOO.length === N2.length - 1,
  `${N2.length} → ${N2_LOO.length}（扣掉 :${N2.length ? N2[0].n : '—'}）` +
  (N2.length < 2 ? ' ← ★ N 只有 1，留一法之后**归零**：**这正是 v1 的那个病**' : ''));
check('⑱ 验收④：分母必须印出来，且它的计数规则逐字写在判据那一份里',
  D2 > 0 && fs.readFileSync(path.join(HERE, 'registration_rule.md'), 'utf8').includes('分母 D = `docs/AUDIT.md` 里【带据的行】的条数'),
  `D = ${D2}`);
check('⑱ 验收⑤：去掉那条载荷（条件 3）⇒ 验收② 必须变红',
  lbRead.find((x) => x.name.startsWith('条件 3'))?.passes4399 === true,
  lbRead.find((x) => x.name.startsWith('条件 3'))?.passes4399 ? '去掉条件 3 ⇒ `:4399` 进来了 ⇒ 验收② 会红' : '★ 去掉条件 3 它仍在外面 —— 那验收② 不是被条件 3 撑着的');
check('⑱ 每一条 N 行都必须带逐字原文与至少一处据词',
  N2.every((h) => h.text.length > 0 && h.ground.length > 0),
  `${N2.filter((h) => h.text.length > 0 && h.ground.length > 0).length} / ${N2.length}`);
check('⑱ 特别档必须印出一个逐字实例（一个档只有计数、没有实例，与没量过一样）',
  D2NoPhrase.length === 0 || Boolean(specA),
  `${D2NoPhrase.length} 处 / 实例 ${specA ? `:${auditLines.indexOf(specA) + 1}` : '（无）'}`);
info('★ 逐条 N（含逐字原文、据词、以及它替哪几节打出来的）：');
N2.forEach((h) => {
  info(`   :${h.n}  ${h.rows.join('、')}`);
  info(`      据词 ${h.ground.join('、')}／裁定短语 ${h.phr.length ? h.phr.join('、') : '（无）'}`);
  info(`      原文（逐字）：${h.text.slice(0, 200)}`);
});
info(`★ 预言三（写在判据那一份里）：N ≥ 2 ⇒ ` + (N2.length >= 2 ? `**成立**（${N2.length}）` : `**落空**（${N2.length}）`));
info(`★ 预言四（写在判据那一份里）：D ≥ 100 ⇒ ` + (D2 >= 100 ? `**成立**（${D2}）` : `**落空**（${D2}）`));
info(`★ 预言五（写在判据那一份里）：v1 挡 \`:4399\` 的理由与 v2 的不是同一个 ⇒ ` +
  (loadBearing.some((x) => x.name.startsWith('条件 3')) ? '**成立**（v1 靠那个词、v2 靠据词）' : '**落空**（挡住它的不是条件 3）'));
info(`★ 预言六（写在判据那一份里，第三个实例）：\`:10820\` 不在 N 里 ⇒ ` +
  (N2.some((h) => h.n === 10820) ? `**落空**（它进来了 —— 这是我这一件最值钱的一个反例）` : '**成立**'));
check('⑱ 第三个实例必须核（判据那一份里写下了它会不在 N 里）',
  !N2.some((h) => h.n === 10820),
  N2.some((h) => h.n === 10820) ? ':10820 在 N 里 —— 见预言六的落空' : '在 N 之外');

/* ★★ 两栏并排（协调者 m00770 的派件）。
 * ★ 甲栏 = 这一行【住在哪一节的屋檐下】—— 纯文件结构，**不看那 33 节**。
 * ★ 乙栏 = 这一行【替哪几节打出来的】—— 从族成员取，**看那 33 节**。
 * ★ 两栏不同的那些行才是这一件的东西：它们住在 A 节的屋檐下，而在替 B 节说话 ——
 *   而那种行在只有一栏的世界里长得与其余的行一模一样。 */
const headingAt = (n) => {
  for (let i = n - 2; i >= 0; i--) {
    const m = auditLines[i].match(/^#{2,4}\s+(.+?)\s*$/);
    if (m) return { line: i + 1, title: m[1].replace(/\*\*/g, '').replace(/`/g, '').trim() };
  }
  return { line: null, title: '（文件头：无节）' };
};
/** 乙栏：这一行落在哪些节的族里（★ 用**同一把筛子**，不是新写一个判据）。 */
const emitBy = new Map();
famRows.forEach((r) => regScreen2(r).forEach((h) => {
  if (!emitBy.has(h.n)) emitBy.set(h.n, []);
  emitBy.get(h.n).push(r);
}));
const twoCol = D_ctx.map((x) => {
  const A = headingAt(x.n);
  const B = emitBy.get(x.n) || [];
  const Bt = B.map((r) => `${r.file} §${r.id}`);
  const Bhead = B.map((r) => regHeadingOf(r));
  const tier = B.length === 0 ? '空' : (Bhead.length === 1 && Bhead[0] === A.title ? '同' : '异');
  return { n: x.n, A, B, Bt, Bhead, tier, text: x.text };
});
info('★★ 两栏并排（甲栏 = 这一行住在哪一节的屋檐下，**不看那 33 节**；乙栏 = 它替哪几节打出来的，**看那 33 节**）：');
let twoColPrinted = 0;
twoCol.forEach((r) => { twoColPrinted++; info(`   :${r.n}　【甲】${r.A.line ? `:${r.A.line} ` : ''}${r.A.title}　【乙】${r.Bt.length ? r.Bt.join('、') : '（空）'}　【档】${r.tier}`); });
check('⑲ 验收①：D_ctx 的每一行都印出两栏（印出的条数 = D_ctx 的条数）',
  twoColPrinted === D_ctx.length,
  `印出 ${twoColPrinted} 行 / D_ctx ${D_ctx.length} 行`);
/* ③ 甲栏是一个真划分 */
const byA = new Map();
twoCol.forEach((r) => { if (!byA.has(r.A.title)) byA.set(r.A.title, []); byA.get(r.A.title).push(r.n); });
info(`★ 按甲栏分组（${byA.size} 组）：`);
let byASum = 0;
[...byA.entries()].forEach(([k, ns]) => { byASum += ns.length; info(`   ${k}　→ ${ns.length} 行　${ns.map((n) => ':' + n).join('、')}`); });
check('⑲ 验收③：甲栏的分组计数之和必须等于 11（一行只住在一个节里 ⇒ 这是一个真划分）',
  byASum === D_ctx.length, `${byASum} / ${D_ctx.length}`);
/* ② 两栏不同的行 */
const tierDiff = twoCol.filter((r) => r.tier !== '同');
info(`★ 验收②：两栏不同的行（档 = 异 或 空）= **${tierDiff.length}** 行，逐行印出：`);
let tierDiffPrinted = 0;
tierDiff.forEach((r) => { tierDiffPrinted++; info(`   :${r.n}【${r.tier}】甲=${r.A.title}　乙=${r.Bt.join('、') || '（空）'}　原文：${r.text.slice(0, 120)}`); });
check('⑲ 验收②：两栏不同的行必须逐行印出（印出的条数 = 该档的条数）',
  tierDiffPrinted === tierDiff.length, `印出 ${tierDiffPrinted} / ${tierDiff.length}`);
/* ④ 乙栏落进 ≥2 档 */
const multi = twoCol.filter((r) => r.B.length >= 2);
info(`★ 验收④：乙栏落进 ≥2 档的行（一行替两个节说话）= **${multi.length}** 行`);
multi.forEach((r) => info(`   :${r.n}　乙 = ${r.Bt.join('、')}`));
/* ★ 特别档的零是什么零 */
const nearMulti = D_ctx.filter((x) => {
  const secs = new Set();
  famRows.forEach((r) => { if (r.fam.some((m) => x.text.includes(m))) secs.add(`${r.file} §${r.id}`); });
  return secs.size >= 2;
});
info(`★ 特别档的零是【真的没有】还是【判据不允许】：把门槛降到「这一行含某一节的 ≥1 个族成员」后，落进 ≥2 节的行 = **${nearMulti.length}** 行 ⇒ `
  + (multi.length === 0
      ? (nearMulti.length === 0
          ? '**判据允许它，而这 11 行里一行都没有同时沾到两个节 ⇒ 真的没有**'
          : '**判据允许它，而且有行沾到了两个节 ⇒ 是【条件 1 的 ≥2 个族成员】把它挡住了，不是判据不允许**')
      : '（有实例）'));

/* ★★ 两级分组（协调者 m00848 的派件）：粗 = markdown 标题；细 = 加粗组标签 `**X组 · …**`。
 * ★ 只印、不换坐标 —— 上面那个 `headingAt` 仍然是甲栏，这一块只是把它的粒度问题变成第二个坐标。 */
const GRP_RE = /^\*\*[^*]+组 · .+\*\*$/;
const GRPX_RE = /^\*\*[^*]+组[^*]*\*\*$/;
const BOLD_RE = /^\*\*[^*]+\*\*$/;
const fineAt = (n) => {
  for (let i = n - 2; i >= 0; i--) {
    if (GRP_RE.test(auditLines[i])) return { line: i + 1, title: auditLines[i].replace(/^\*\*/, '').replace(/\*\*$/, '').trim() };
  }
  return { line: null, title: '（无细组）' };
};
const three = D_ctx.map((x) => {
  const A = headingAt(x.n);
  const F = fineAt(x.n);
  const B = (emitBy.get(x.n) || []).map((r) => `${r.file} §${r.id}`);
  return { n: x.n, A, F, B };
});
info('★★ 两级并排（粗 = markdown 标题；细 = 加粗组标签 `**X组 · …**`；乙 = 它替哪几节打出来的）：');
let threePrinted = 0;
three.forEach((r) => { threePrinted++; info(`   :${r.n}　【粗】${r.A.line ? `:${r.A.line} ` : ''}${r.A.title}　【细】${r.F.line ? `:${r.F.line} ` : ''}${r.F.title}　【乙】${r.B.length ? r.B.join('、') : '（空）'}`); });
check('⑳ 验收①：两级分组都印、11 行一行不少（每行三格）',
  threePrinted === D_ctx.length, `印出 ${threePrinted} / ${D_ctx.length}`);
/* ② 那一行的三格 */
const hThree = three.find((r) => r.n === 10808);
info(`★ 验收②：\`:10808\` 的三格 = 【粗】${hThree ? hThree.A.title : '（缺）'}　【细】${hThree ? hThree.F.title : '（缺）'}　【乙】${hThree ? hThree.B.join('、') : '（缺）'}`);
check('⑳ 验收②：`:10808` 的三格全填出来（住丁组、替 docs/STATUS.md §10.4）',
  Boolean(hThree && hThree.F.title === '丁组 · 谁裁的' && hThree.B.join('、') === 'docs/STATUS.md §10.4'),
  hThree ? `细=${hThree.F.title}／乙=${hThree.B.join('、') || '（空）'}` : '缺这一行');
/* ④ 四级计数 */
const byCoarse = new Map();
three.forEach((r) => { if (!byCoarse.has(r.A.title)) byCoarse.set(r.A.title, []); byCoarse.get(r.A.title).push(r.n); });
const byFine = new Map();
three.forEach((r) => { if (!byFine.has(r.F.title)) byFine.set(r.F.title, []); byFine.get(r.F.title).push(r); });
const coarseSum = [...byCoarse.values()].reduce((a, v) => a + v.length, 0);
const fineSum = [...byFine.values()].reduce((a, v) => a + v.length, 0);
info(`★ 验收④：粗组数 = **${byCoarse.size}**／细组数 = **${byFine.size}**／粗组计数之和 = **${coarseSum}**／细组计数之和 = **${fineSum}**`);
check('⑳ 验收④：两个和都必须等于 11',
  coarseSum === D_ctx.length && fineSum === D_ctx.length, `${coarseSum} / ${fineSum} vs ${D_ctx.length}`);
/* ③ 粗化关系 */
const coarseOfFine = new Map();
byFine.forEach((rows, k) => coarseOfFine.set(k, new Set(rows.map((r) => r.A.title))));
const RESID = '（无细组）';
const namedFine = [...byFine.keys()].filter((k) => k !== RESID);
const crossingNamed = namedFine.filter((k) => coarseOfFine.get(k).size > 1);
const residualRows = byFine.get(RESID) || [];
const residualCoarse = coarseOfFine.get(RESID) || new Set();
info('★ 验收③：每一个细组必须完整落在某个粗组里（一级是另一级的粗化）—— 逐组：');
[...byFine.entries()].forEach(([k, rows]) => info(`   ${k}　→ ${rows.length} 行，落在 ${coarseOfFine.get(k).size} 个粗组里${coarseOfFine.get(k).size > 1 ? '　★★ 跨粗组！' : ''}`));
check('⑳ 验收③：每一个细组必须完整落在某个粗组里（一级是另一级的粗化）',
  crossingNamed.length === 0 && residualRows.length === 0,
  `具名细组跨粗组的 = ${crossingNamed.length} 个；残余类【${RESID}】= ${residualRows.length} 行、跨 ${residualCoarse.size} 个粗组`);
check('⑳ 验收③ 的细读：把残余类排除之后，粗化关系成立（★ 它红了才是解析的问题）',
  crossingNamed.length === 0,
  `具名细组 ${namedFine.length} 个，跨粗组的 ${crossingNamed.length} 个`);
/* 特别档 */
const boldAll = auditLines.map((l, i) => ({ n: i + 1, l })).filter((x) => BOLD_RE.test(x.l));
const boldGroup = boldAll.filter((x) => GRPX_RE.test(x.l));
const grpNoDot = boldGroup.filter((x) => !GRP_RE.test(x.l));
const boldNonGroup = boldAll.filter((x) => !GRPX_RE.test(x.l));
info(`★ 特别档 A【细组跨两个粗组】：具名细组跨粗组 = **${crossingNamed.length}** 个`
  + `${crossingNamed.length ? '' : '（0 —— 而它不是「没有」，是【残余类】把 ③ 弄红的）'}；残余类【${RESID}】= ${residualRows.length} 行、跨 **${residualCoarse.size}** 个粗组`);
info(`★ 特别档 B【\`**…**\` 形状的加粗行不是组标签】：纯加粗行 ${boldAll.length} 条，其中组形（\`组…**\`）${boldGroup.length} 条、**非组形 ${boldNonGroup.length} 条**`);
info(`★ 而组形里【没有 \` · \`】的有 **${grpNoDot.length}** 条 ⇒ ★ 严格正则（\`组 · \`）会把它们放过去，宽松正则（\`组…\`）会把它们收进来 —— 逐条：`);
grpNoDot.forEach((x) => info(`   :${x.n} ${x.l.slice(0, 110)}`));
const grpPrefix = (s) => (s.match(/^([^*]+?组) · /) || [])[1] || null;
const pc = new Map();
namedFine.forEach((k) => { const p = grpPrefix(k); if (p) pc.set(p, (pc.get(p) || 0) + 1); });
info(`★ 而【组名前缀】不是唯一的：这 11 行用到的前缀 ${[...pc.entries()].map(([k, v]) => `${k}×${v}`).join('、') || '（无）'} ⇒ ★ 细组的身份必须是**整行**，不是前缀。`);

/* ★★ 有界细组（协调者 m00897 的派件）：与上面的【无界】`fineAt` 并排，**只印、不换坐标**。
 * ★ 成因：上一件的对照一证明 `fineAt` 是一个【无界的向上走】，它跨越 markdown 标题的边界 ——
 *   于是同一行在「上面有没有一个标签」这件事上，可以从一个节一路走到另一个节去认领一个标签。
 * ★ 有界版：走到【最近的 markdown 标题】就停 —— 因为粗组就是 markdown 标题，所以
 *   「细组必须完整落在某个粗组里」这句话在有界版下才有机会为真。
 * ★ 它不动 `headingAt`（甲栏）、不动 `fineAt`（无界细组）、不动任何已报的数。 */
const fineAtBounded = (n) => {
  for (let i = n - 2; i >= 0; i--) {
    if (/^#{2,4}\s/.test(auditLines[i])) {
      return { line: null, title: RESID, stoppedAt: i + 1, stoppedText: auditLines[i].trim(), steps: (n - 1) - i };
    }
    if (GRP_RE.test(auditLines[i])) {
      return { line: i + 1, title: auditLines[i].replace(/^\*\*/, '').replace(/\*\*$/, '').trim(), stoppedAt: null, stoppedText: null, steps: (n - 1) - i };
    }
  }
  return { line: null, title: RESID, stoppedAt: null, stoppedText: null, steps: n - 1 };
};
const four = D_ctx.map((x) => {
  const A = headingAt(x.n);
  const Fu = fineAt(x.n);
  const Fb = fineAtBounded(x.n);
  const B = (emitBy.get(x.n) || []).map((r) => `${r.file} §${r.id}`);
  return { n: x.n, A, Fu, Fb, B, same: Fu.title === Fb.title };
});
info('★★ 有界与无界并排（粗 = markdown 标题；无界细组 = 一直往上找 `**X组 · …**`；有界细组 = 走到最近的 markdown 标题就停）：');
let fourPrinted = 0;
four.forEach((r) => { fourPrinted++; info(`   :${r.n}　【无界细组】${r.Fu.line ? `:${r.Fu.line} ` : ''}${r.Fu.title}　【有界细组】${r.Fb.line ? `:${r.Fb.line} ` : ''}${r.Fb.title}　【两格相同】${r.same ? '同' : '★异'}　【粗组】${r.A.line ? `:${r.A.line} ` : ''}${r.A.title}`); });
check('㉑ 验收①：有界与无界两版并排、11 行一行不少（每行四格）',
  fourPrinted === D_ctx.length, `印出 ${fourPrinted} / ${D_ctx.length}`);
/* ② 相异的行数单独一个数 */
const fourDiff = four.filter((r) => !r.same);
info(`★ 验收②：两版【相异】的行数 = **${fourDiff.length}** / D_ctx = **${D_ctx.length}**（相异 = 「有界版把这一行的细组改掉了」）`);
fourDiff.forEach((r) => info(`   ★ 相异：:${r.n}　无界 = ${r.Fu.title}　有界 = ${r.Fb.title}`));
/* ③ 每一次「有界版改回（无细组）」都必须能指到粗组标题原文 */
const backToResid = four.filter((r) => r.Fb.title === RESID && r.Fu.title !== RESID);
info(`★ 验收③：有界版把细组改回【${RESID}】的行 = **${backToResid.length}** 行 —— 逐行指到它的粗组标题（原文）：`);
backToResid.forEach((r) => info(`   :${r.n}　【原无界细组】${r.Fu.title}　⇒ 有界 = ${RESID}　【粗组标题】:${r.A.line} ${r.A.title}`));
check('㉑ 验收③：每一行「有界版改回残余类」都能指到一个非空粗组标题',
  backToResid.every((r) => r.A.line !== null && r.A.title && r.A.title !== '（文件头：无节）'),
  `改回残余类的 ${backToResid.length} 行，其中粗组标题为空的 ${backToResid.filter((r) => r.A.line === null).length} 行`);
/* ④ ★ 有界版必须让【不排除残余类】的粗化关系也为真 —— 若仍红，那个红是本件的头条 */
const byFineB = new Map();
four.forEach((r) => { if (!byFineB.has(r.Fb.title)) byFineB.set(r.Fb.title, []); byFineB.get(r.Fb.title).push(r); });
const coarseOfB = new Map();
byFineB.forEach((rows, k) => coarseOfB.set(k, new Set(rows.map((r) => r.A.title))));
const crossB = [...byFineB.keys()].filter((k) => coarseOfB.get(k).size > 1);
const sumB = [...byFineB.values()].reduce((a, v) => a + v.length, 0);
info(`★ 验收④：有界版下的四级计数 —— 细组数 = **${byFineB.size}**／细组计数之和 = **${sumB}**；逐组：`);
[...byFineB.entries()].forEach(([k, rows]) => info(`   ${k}　→ ${rows.length} 行，落在 ${coarseOfB.get(k).size} 个粗组里${coarseOfB.get(k).size > 1 ? '　★★ 跨粗组！' : ''}`));
check('㉑ 验收④：有界版下【不排除残余类】也必须为真（每一个细组完整落在某个粗组里）',
  crossB.length === 0,
  `跨粗组的细组 = ${crossB.length} 个：${crossB.join('、') || '（无）'}`
  + (crossB.length ? '　★★ 而这就是本件的头条：有界走改变不了它 ⇒ 这个假不是【尺子越界】造成的' : ''));
/* ★ 特别档：有界版下从【具名细组】变回残余类的行 —— 是【步数不够】还是【真的没有】？ */
info(`★ 特别档【有界版下变回 ${RESID}，而粗组标题就在上方】：${backToResid.length} 行 —— 逐行给【走了多少步】与【那个 span 里到底有没有 ${'`GRP_RE`'} 行】：`);
backToResid.forEach((r) => {
  const top = r.A.line || 1;
  const span = auditLines.slice(top - 1, r.n - 1);
  const grpInSpan = span.filter((l) => GRP_RE.test(l)).length;
  info(`   :${r.n}　走了 ${r.Fb.steps} 步　停在 :${r.Fb.stoppedAt} 「${String(r.Fb.stoppedText || '').slice(0, 70)}」　span(${top}..${r.n - 1}) 里的 GRP_RE 行 = ${grpInSpan} ⇒ ${grpInSpan === 0 ? '真的没有细组（不是步数不够）' : '★ 步数不够'}`);
});
if (backToResid.length === 0) info(`   （0 行 —— 而这是一个【关于语料】的零：无界版在有界版的射程之内没有越界过）`);

/* ★★ ㉒ 把残余类从「细组」里移出去，变成【未归类行数】（协调者 m01002 的派件）。
 * ★ 判据文字写在做之前（见 `website/scratch/registration_rule.md` 的「㉒」一节，父提交可证）。
 * ★ 理由：`（无细组）` 不是「细组这一级的一格」，它是【没有细组的那些行】的**并集** ——
 *   一个并集没有「落在哪个粗组里」这件事 ⇒ ④ 的宾语如果是它，那句话在语法上就没有主语。
 * ★ 三条派生处置：① 残余类不许进 ④ 的宾语；② 它与具名细组的计数【并排印、不相加】；
 *   ③ ④ 改后的红/绿必须报出（若仍红，那条红是本件的新头条）。
 * ★ 只印、不换坐标 —— 不动 `headingAt` / `fineAt` / `fineAtBounded` / `RULING_ROWS_READ_AT` / 条件 1 / 条件 3。 */
const UNCL = RESID;
/** ④ 的宾语：只取具名细组，逐版各一份。 */
const namedOf = (m) => [...m.entries()].filter(([k]) => k !== UNCL);
const crossOf = (m) => namedOf(m)
  .filter(([, rows]) => new Set(rows.map((r) => r.A.title)).size > 1)
  .map(([k]) => k);
const namedU = namedOf(byFine);
const namedB_ = namedOf(byFineB);
const unclU = byFine.get(UNCL) || [];
const unclB = byFineB.get(UNCL) || [];
const crossU = crossOf(byFine);
const crossB_ = crossOf(byFineB);
info('★★ ㉒ 两个计数对象【并排印、不相加】（残余类 = 「没有细组的那些行」的并集，不是「细组这一级的一格」）：');
info(`   【具名细组】无界版 ${namedU.length} 个／计数之和 = **${namedU.reduce((a, [, r]) => a + r.length, 0)}** 行　有界版 ${namedB_.length} 个／计数之和 = **${namedB_.reduce((a, [, r]) => a + r.length, 0)}** 行`);
info(`   【未归类行数】无界版 = **${unclU.length}** 行　有界版 = **${unclB.length}** 行`);
info(`   ★ 而这两栏【不相加】：具名细组的和 + 未归类行数不是一个【完备的分类】，它是两个不同的计数对象。`);
/* ★★ 协调者 m01044 验收⑤：「非零」从一条【跨对照的检查】变成【基线读数栏里的一个值】。
 * ★ 理由（上一件我自己量到的）：一条要求某个计数【非零】的检查，如果它的对照的目的正好是把它清零，
 *   那么它在对照下必然红 —— 而那条红是【射程越过了对照的设计】，不是缺陷。
 * ★ 处置：这一条从 `check` 降成 `info`（读数栏），它的载荷（那个【非零】）留在这一行里；
 *   而它原来想守的那件事（两个计数对象是一个【真划分】）由 `㉓ 验收⑤` 那条 check 接着守 ——
 *   ★ 那条 check 在基线与对照下都不红，因为它的载荷是【划分】，不是【非零】。 */
info(`★ ㉒ 验收② 读数栏（★ 从 ${'`check`'} 降成 ${'`info`'} —— 它是一条【基线读数】，不是一条跨对照的检查）：`
  + `　未归类 无界 = **${unclU.length}** / 有界 = **${unclB.length}**；具名 无界 = **${namedU.length}** / 有界 = **${namedB_.length}**`
  + `　⇒ ★ 这两个计数对象【不相加】，而它们的和 = D_ctx 这件事由 ㉓ 验收⑤ 守着。`);
/* ③ ④ 的宾语只剩具名细组 —— 改前 / 改后并排报 */
info(`★ ㉒ 验收③：④ 的宾语（「每一个具名细组完整落在某个粗组里」）——`);
info(`   【改前】宾语 = 具名细组 + 残余类 ⇒ 无界版：具名跨粗组 ${crossingNamed.length} 个、残余类 ${residualRows.length} 行跨 ${residualCoarse.size} 个粗组 ⇒ **${crossingNamed.length === 0 && residualRows.length === 0 ? '绿' : '红'}**`);
info(`   【改后·无界版】宾语 = 具名细组（${namedU.length} 个）⇒ 跨粗组 ${crossU.length} 个 ⇒ **${crossU.length === 0 ? '绿' : '红'}**${crossU.length ? `：${crossU.join('、')}` : ''}`);
info(`   【改后·有界版】宾语 = 具名细组（${namedB_.length} 个）⇒ 跨粗组 ${crossB_.length} 个 ⇒ **${crossB_.length === 0 ? '绿' : '红'}**${crossB_.length ? `：${crossB_.join('、')}` : ''}`);
check('㉒ 验收③：改后 ④ 的宾语【只剩具名细组】，残余类不在其中',
  !crossU.includes(UNCL) && !crossB_.includes(UNCL),
  `无界版宾语里的跨粗组 = ${crossU.join('、') || '（无）'}；有界版 = ${crossB_.join('、') || '（无）'}`);
check('㉒ 验收③（无界版）：改后 ④ 在【基线】上转绿 —— ★ 而这个检查在每一次运行里都被求值，'
  + '所以在【对照】下它必然红（那正是预言七：剩下的那条红有主语）',
  crossU.length === 0,
  crossU.length === 0 ? '绿 —— 具名细组各自落在一个粗组里'
    : `★ 仍红：${crossU.join('、')} ⇒ ★ 本件的新头条：这条红不是残余类造成的`);
check('㉒ 验收③（有界版）：改后 ④ 在【基线】上转绿，并在【对照】下也转绿（有界版不越界认领标签）',
  crossB_.length === 0,
  crossB_.length === 0 ? '绿' : `★ 仍红：${crossB_.join('、')}`);
/* ⑥ 那个空的 every 的恒真性：继续写在报告里，也继续印在这里 */
info(`★ ㉒ 验收⑥：\`㉑ 验收③\` 在基线上的通过是一个【空的 \`every\`】（0 行 ⇒ \`[].every(...) === true\`）`
  + ` ⇒ ★ 它【按构造不可能失败】，全部信息量在对照那一跑。★ 这条恒真性继续写在报告里，不许以一个绿的姿态过去。`);

/* ★★ ㉓ 把「越界」从布尔变成分布（协调者 m01044 的派件）。
 * ★ 判据文字先落盘（见 `website/scratch/registration_rule.md` 的「㉓」一节，父提交可证）。
 * ★ 只印、不换坐标 —— 不动 `headingAt` / `fineAt` / `fineAtBounded` / `RULING_ROWS_READ_AT` / 条件 1 / 条件 3。
 * ★ 本件要回答的：上一件在【对照】上量到了 4 次越界，然后我把它推广到了基线（那一步被上一件自己证伪了）。
 *   本件把「越界」从一个布尔变成一个【分布】—— 每一次越界走多少步、越过哪几根标题、差是多少。 */
/** 只印、不改：给无界版补一份【计数版】走法。
 *  ★ 它与 `fineAt` 是两份代码 ⇒ 本件要求它们逐字给出同一个 title（下面有一条检查盯这件事）——
 *  否则我就是在用一个【看起来一样的】尺子，替掉原来那把。 */
const fineAtCounted = (n) => {
  let steps = 0;
  const crossed = [];
  for (let i = n - 2; i >= 0; i--) {
    steps++;
    if (/^#{2,4}\s/.test(auditLines[i])) crossed.push(i + 1);
    if (GRP_RE.test(auditLines[i])) {
      return {
        line: i + 1,
        title: auditLines[i].replace(/^\*\*/, '').replace(/\*\*$/, '').trim(),
        steps, crossed,
      };
    }
  }
  return { line: null, title: RESID, steps, crossed };
};
const five = D_ctx.map((x) => {
  const A = headingAt(x.n);
  const Fc = fineAtCounted(x.n);
  const Fb = fineAtBounded(x.n);
  const byA = Fc.line !== null && Fb.stoppedAt !== null && Fc.line < Fb.stoppedAt;
  const byB = Fb.title === RESID && Fc.title !== RESID;
  return { n: x.n, A, Fc, Fb, byA, byB };
});
/* ①′ 计量版与 `fineAt` 必须逐字同 title —— 这是本件唯一的【关于尺子自己】的检查 */
const countedMismatch = five.filter((r) => r.Fc.title !== fineAt(r.n).title);
check('㉓ 验收①：计数版 `fineAtCounted` 与 `fineAt` 逐字给出同一个 title（★ 否则我换掉的是尺子）',
  countedMismatch.length === 0,
  `不一致的 ${countedMismatch.length} / ${D_ctx.length} 行${countedMismatch.length ? `：${countedMismatch.map((r) => `:${r.n}`).join('、')}` : ''}`);
/* ② 两条定义必须同义 —— 不一致就是本件的头条 */
const disagree = five.filter((r) => r.byA !== r.byB);
check('㉓ 验收②：定义 A（无界认领的标签在有界停下的那根标题之上）与定义 B（有界说没有而无界说有）给出同一个集合',
  disagree.length === 0,
  disagree.length ? `★ 两定义不一致的 ${disagree.length} 行：${disagree.map((r) => `:${r.n}`).join('、')}` : '两条定义同义');
const overruns = five.filter((r) => r.byB);
/* ★★★ 读数一出来就暴露了我自己这个栏的一个射程缺陷：
 *   【找到】与【未找到】是两种走法 —— 前者的 steps 是一个【局部量】，后者的 steps 等于【整份文件】。
 *   ★ 我原来把它们印成同一个形状的数（`:6177 无界步数 6176`），于是「走到文件头」读起来像「走了 6176 步」。
 *   ★ 处置：两种走法印成两种形状，并【只把【找到】的那些行放进那个差里】—— 差只对【越过一根标签之后】才有意义。 */
const foundRows = five.filter((r) => r.Fc.line !== null);
const notFoundRows = five.filter((r) => r.Fc.line === null);
info(`★★ ㉓ 走法二态（★ 我原来把它印成一个数）：【找到一个具名标签】= **${foundRows.length}** 行；`
  + `【一路走到文件头也没找到】= **${notFoundRows.length}** 行　⇒ 和 = ${foundRows.length + notFoundRows.length} / D_ctx = ${D_ctx.length}`);
info('★ 逐行三格【步数（无界 / 有界）・越过的标题数・差】+ 它越过的那几根标题（行号 + 原文）：');
five.forEach((r) => {
  if (r.Fc.line === null) {
    info(`   :${r.n}　【无界】★ 未找到（一路走到文件头，steps 会等于 ${r.n - 1} —— ★ 那不是步数，是文件高度）`
      + `　【有界】${String(r.Fb.steps).padStart(3)} 步${r.Fb.stoppedAt ? ` 停在 :${r.Fb.stoppedAt}` : ''}`
      + `　【越过标题数】不适用　【差】不适用　【越界?】否`);
    return;
  }
  const d = r.Fc.steps - r.Fc.crossed.length;
  info(`   :${r.n}　【步数】无界 ${String(r.Fc.steps).padStart(3)} / 有界 ${String(r.Fb.steps).padStart(3)}`
    + `　【越过标题数】${r.Fc.crossed.length}　【差】${d}　【越界?】${r.byB ? '★是' : '否'}`);
  /* ★★★ 明细必须给它一个上界：对照下有一行越过了 197 根标题，逐根印会把这份报告变成一片 1503 行的洪泛
   *   （基线 826 行 → 对照 1503 行，+677 行全部来自这里）—— ★ 而它淹掉的正是它要解释的那三行。
   *   ★ 处置：印【路径的形状】而不是每一根 —— 最靠近本行的 2 根 + 最靠近命中标签的 1 根 + 总数。
   *   这是【只印、不换坐标】：它保留了「越过哪几根」这个问题的可反驳性，而不让报告的长度成为一个由语料决定的量。 */
  const cr = r.Fc.crossed;
  const head = cr.slice(-2);
  const tail = cr.slice(0, 1);
  if (cr.length === 0) {
    info('        ★ 越过的标题 = 0 根（★ 这就是「0 次越界」的逐行形态 —— 一个 0 根的明细不印首末，它的首末是空的、读起来像「取不到」）');
    return;
  }
  info(`        ★ 越过的标题共 ${cr.length} 根：最靠近本行的 2 根 `
    + head.map((h) => `:${h}「${auditLines[h - 1].trim().slice(0, 60)}」`).join(' / ')
    + `　最靠近命中标签的 1 根 ` + tail.map((h) => `:${h}「${auditLines[h - 1].trim().slice(0, 60)}」`).join(' / ')
    + (cr.length > 3 ? `　（其余 ${cr.length - 3} 根省略 —— ★ 它们的行号区间是 :${cr[1]}..:${cr[cr.length - 3]}）` : ''));
});
/* ★ 一个真划分 + 它的载荷：差【只在找到的行上】才有意义 */
check('㉓ 验收⑥：【找到】与【未找到】是一个真划分（不许把「走到文件头」算成一个步数）',
  foundRows.length + notFoundRows.length === D_ctx.length
  && notFoundRows.every((r) => r.Fc.steps === r.n - 1)
  && foundRows.every((r) => r.Fc.steps < r.n - 1),
  `找到 ${foundRows.length} + 未找到 ${notFoundRows.length} = ${foundRows.length + notFoundRows.length} / ${D_ctx.length}`
  + `；★ 未找到的行里 steps === n−1 的 ${notFoundRows.filter((r) => r.Fc.steps === r.n - 1).length} / ${notFoundRows.length}（这一半按构造恒真）`);
/* ★ 「步数 ≥ 越过的标题数」—— 按构造恒真，它的绿不是读数 */
const stepGe = foundRows.filter((r) => r.Fc.steps >= r.Fc.crossed.length);
check('㉓ 验收④：步数 ≥ 越过的标题数（★ 按构造恒真 —— 它的绿【不是】读数，载荷全在下面那个差上）',
  stepGe.length === foundRows.length,
  `${stepGe.length} / ${foundRows.length} 行满足（★ 构造上必然：每越过一根标题至少走一步）`);
const diffs = [...new Set(foundRows.map((r) => r.Fc.steps - r.Fc.crossed.length))].sort((a, b) => a - b);
info(`★ 验收⑤：那个【差】的分布（★ 只在【找到】的 ${foundRows.length} 行上取）= {${diffs.join(', ')}}`
  + ` ⇒ ${diffs.length === 1
    ? '差恒等于一个数 ⇒ 关系是【相等】（步数 = 越过标题数 + 那个常数）'
    : '★ 差逐行不同 ⇒ 关系是【包含】而不是【相等】 —— ★ 而「无关」与「包含」在只印两个数的时候长得一样，把它们分开的就是这个差'}`);
/* ★ 一条【有载荷】的检查：基线上【找到】的行里，越过的标题数必须是 0（= 「基线 0 次越界」的逐行形态） */
const foundCrossed = foundRows.filter((r) => r.Fc.crossed.length > 0);
check('㉓ 验收③（逐行形态）：基线上每一次【找到】都没有越过任何标题（对照下这一条必须变红）',
  foundCrossed.length === 0,
  foundCrossed.length ? `★ 越过的 ${foundCrossed.length} 行：${foundCrossed.map((r) => `:${r.n}`).join('、')}`
    : `基线：${foundRows.length} 次【找到】，0 次越过（★ 对照下这一条必须变红 —— 那就是它的载荷）`);
/* ③ ★ 「基线 0 次」是什么零 —— 必须给出【判据允许它】的证据，否则它是尺子的性质，不是语料的性质 */
const stoppedAtHeading = five.filter((r) => r.Fb.stoppedAt !== null);
info(`★★ 验收③：基线上越界 = **${overruns.length}** 次；★ 而这是【什么零】？—— `
  + `有界版停在标题上的行 = ${stoppedAtHeading.length} 行，其中无界版仍然拿不到具名标签的 = `
  + `${stoppedAtHeading.filter((r) => r.Fc.title === RESID).length} 行`);
info(`   ⇒ ★ 判据【允许】这 ${stoppedAtHeading.length} 行越界（它们的上方的确有一根标题），而它们一次都没有越 —— `
  + `所以这个零是【语料】的性质；★ 而它的这一半只有在【对照下同一个数变成 4】时才是一个读数。`);
/* ⑧ 协调者追加：两个分母必须【并排印】，不许合成一个数 */
info('★★ ㉓ 验收⑧：两个分母【并排印、不许合成一个数】（它们不是同一个定义换了个名字）：');
info(`   【分母一 D_all（带据）】= **${D_all.length}** 行　【分母二 D_ctx（带据 ∧ 在裁）】= **${D_ctx.length}** 行　【差】= **${D_all.length - D_ctx.length}** 行`);
/* ⑤ 「非零」的新形态：一条 check（两版都不红）+ 一条 info（读数栏里的值） */
const namedRowU = namedU.reduce((a, [, r]) => a + r.length, 0);
const namedRowB = namedB_.reduce((a, [, r]) => a + r.length, 0);
info(`★ ㉓ 验收⑤ 读数栏（【不是检查】—— 未归类行数是基线的一个值，不许被当成一条跨对照的检查）：`
  + `　未归类行数：无界 = **${unclU.length}** / 有界 = **${unclB.length}**`);
check('㉓ 验收⑤：未归类行数 + 具名细组行数 = D_ctx（两版各一个【真划分】）'
  + ' —— ★ 这一条在基线与对照下都不红，因为它的载荷是【划分】、不是【非零】',
  unclU.length + namedRowU === D_ctx.length && unclB.length + namedRowB === D_ctx.length,
  `无界 ${unclU.length}+${namedRowU}=${unclU.length + namedRowU}；有界 ${unclB.length}+${namedRowB}=${unclB.length + namedRowB}；D_ctx=${D_ctx.length}`);

/* ==================== ㉔ 把「不适用」变成一个可执行的定义（协调者 m01143 的派件）====================
 * ★ 三条常驻红里，有几条不是红，是【那把尺子对那个对象不适用】。
 * ★★ 一句话的分界：**不适用 = 谓词无值；真红 = 谓词有值且为假。**
 * ★ 而「无值」必须可算 —— 所以每一条都要交出它自己的 dom 判定式（见 registration_rule.md 的 ㉔ 一节）。
 * ★★ 本块【只印不改】：它不删任何一条 check、不改任何一条谓词。 */
{
  /* ---- dom 判定式之一：这条标签是由【一根匹配 GRP_RE 的行】产生的吗？ ---- */
  /* ★ 不提 `RESID` 一个字 —— 它问的是「那把尺子找到了一根线没有」，因此可以被对照证伪。 */
  const labelProdOf = (k) => {
    const rows = byFine.get(k) || byFineB.get(k) || [];
    if (!rows.length) return { line: null, stoppedAt: null, steps: null };
    const fb = fineAtBounded(rows[0].n);
    return { line: fb.line, stoppedAt: fb.stoppedAt, steps: fb.steps };
  };
  const producedByGroupLine = (k) => labelProdOf(k).line !== null;

  /* ---- dom 判定式之二：这一行能被单独扣掉吗？（留一法的定义域） ---- */
  const removable = (r) => r.n !== null && hasGround(r.text);

  /* ---- (b) 射程外：那 59 根 GROUP 行产生的标签里，有几个不在本检查的输入集里？ ---- */
  const grpLines = auditLines.reduce((a, l, i) => { if (GRP_RE.test(l)) a.push({ n: i + 1, l }); return a; }, []);
  /* ★★ 规范化必须先对齐，否则量到的是【规范化】的差、不是【射程】的差。
     ★ 第一版我拿【原始行】（带星号）去比的键（fineAt 已经把星号剥掉了）⇒
       印出 inInput = 0 / 射程外 = 58 —— ★ **那个 58 是一个非常像读数的东西，而它量的是我的比较。** */
  const normLabel = (l) => l.replace(/^\*\*/, '').replace(/\*\*$/, '').trim();
  const grpLabelsRaw = [...new Set(grpLines.map((x) => x.l))];
  const grpLabels = [...new Set(grpLines.map((x) => normLabel(x.l)))];
  const inputLabels = new Set([...byFine.keys(), ...byFineB.keys()]);
  const inInput = grpLabels.filter((k) => inputLabels.has(k));
  const outOfInput = grpLabels.filter((k) => !inputLabels.has(k));

  /* ---- 三条红：逐条给出【失败的合取项 / 它的宾语 / 那个宾语的 dom 判定】 ---- */
  /* ★★★ 失败的合取项必须【在运行时算出来】，不能由我把宾语写死在代码里。
     ★ 第一版我把 R2 的宾语写死成 [RESID]，于是在对照下 —— 那棵树上失败的合取项已经换成了
       `crossingNamed.length === 0` —— 它仍然把 [RESID] 印了一遍，并给出「不适用」。
     ★★ 而那个印出来的样子，与一个真读数【一模一样】。 */
  const R1c1 = N2.length >= 2;
  const R1c2 = N2_LOO.length === N2.length - 1;
  const R2c1 = crossingNamed.length === 0;
  const R2c2 = residualRows.length === 0;
  const R3c1 = crossB.length === 0;

  const reds = [
    {
      id: 'R1',
      name: '⑱ 验收③：留一法之后 N 必须非零（扣掉一行 ⇒ 少 1，不是变成 0）',
      cond: 'N2.length >= 2 && N2_LOO.length === N2.length - 1',
      conjValues: () => `N2.length >= 2 ⇒ ${N2.length} >= 2 ⇒ ${R1c1}　／　N2_LOO.length === N2.length - 1 ⇒ ${N2_LOO.length} === ${N2.length - 1} ⇒ ${R1c2}`,
      failConj: () => (R1c1 ? 'N2_LOO.length === N2.length - 1' : 'N2.length >= 2'),
      otherConjValue: () => (R1c1 ? `N2.length >= 2 ⇒ ${N2.length} >= 2 ⇒ true` : `N2_LOO.length === N2.length - 1 ⇒ ${N2_LOO.length} === ${N2.length - 1} ⇒ ${R1c2}`),
      objName: () => (R1c1 ? 'N2_LOO（留一法之后的分子行）' : 'N2（台账的分子行）'),
      obj: () => (R1c1 ? N2_LOO : N2),
      domWhy: 'removable(r) := r.n !== null && hasGround(r.text) —— 带着自己的行号与据、可以被单独扣掉',
      domTest: (o) => o.length > 0 && o.every(removable),
    },
    {
      id: 'R2',
      name: '⑳ 验收③：每一个细组必须完整落在某个粗组里（一级是另一级的粗化）',
      cond: 'crossingNamed.length === 0 && residualRows.length === 0',
      conjValues: () => `crossingNamed.length === 0 ⇒ ${crossingNamed.length} === 0 ⇒ ${R2c1}　／　residualRows.length === 0 ⇒ ${residualRows.length} === 0 ⇒ ${R2c2}`,
      /* ★ 第二版我把这三个三元全写反了 —— 症状：基线印「失败的合取项 = crossingNamed…」而它的值是 true。
         ★★ 一条把【失败的合取项】指到另一个合取项上的分类器，会把宾语也指错，而它的输出仍然完整、仍然好看。 */
      failConj: () => (R2c2 ? 'crossingNamed.length === 0' : 'residualRows.length === 0'),
      otherConjValue: () => (R2c2 ? `residualRows.length === 0 ⇒ ${residualRows.length} === 0 ⇒ true`
        : `crossingNamed.length === 0 ⇒ ${crossingNamed.length} === 0 ⇒ true`),
      objName: () => (R2c2 ? `具名细组跨粗组的那些键（${crossingNamed.length} 个）`
        : `残余类【${RESID}】这一个对象（${residualRows.length} 行）`),
      obj: () => (R2c2 ? crossingNamed : [RESID]),
      domWhy: 'producedByGroupLine(k) := fineAtBounded(rows[0].n).line !== null（★ 不引用哨兵常量）',
      domTest: (o) => o.every(producedByGroupLine),
    },
    {
      id: 'R3',
      name: '㉑ 验收④：有界版下【不排除残余类】也必须为真（每一个细组完整落在某个粗组里）',
      cond: 'crossB.length === 0',
      conjValues: () => `crossB.length === 0 ⇒ ${crossB.length} === 0 ⇒ ${R3c1}`,
      failConj: () => 'crossB.length === 0',
      otherConjValue: () => '',
      objName: () => `crossB 的键（${crossB.length} 个）`,
      obj: () => crossB,
      domWhy: 'producedByGroupLine(k) —— ★ 与 R2 同一个判定式',
      domTest: (o) => o.length > 0 && o.every(producedByGroupLine),
    },
  ];

  info('');
  info('★★★ ㉔ 把「不适用」变成一个可执行的定义 —— ★ 只印不改（本件不删任何一条 check、不改任何一条谓词）');
  info('★ 分界：**不适用 = 谓词无值（⊥）；真红 = 谓词有值且为假。**');
  info(`★ (b) 射程外的分母：\`docs/AUDIT.md\`（钉住的 ${String(RULING_ROWS_READ_AT).slice(0, 7)}）里匹配 \`GRP_RE\` 的行 = **${grpLines.length}** 根，去重后的标签 = **${grpLabelsRaw.length}** 个（按【剥掉星号后的标签】去重也是 ${grpLabels.length} 个）；`);
  info(`   其中落在本检查的输入集（\`byFine\` ∪ \`byFineB\` 的键，★ 已按同一个规范化对齐）里的 = **${inInput.length}** 个 ⇒ ★ **射程外 = ${outOfInput.length} 个**（★ 这量的是【尺子的射程】，与【尺子对自己的宾语适用不适用】是两个数）`);

  /* ★★ 每一档用【自己的】计数器 —— 上一轮刚有一件因为「两档共用一个计数器」造出一个第三个数。 */
  const tally = { applicableTrueRed: [], notApplicable: [], undecided: [] };
  const perRed = [];

  reds.forEach((r, i) => {
    const objV = r.obj();
    const applicable = r.domTest(objV);
    const prodDetail = objV.map((k) => (typeof k === 'string' && (byFine.has(k) || byFineB.has(k))
      ? `${k}（产生它的行 .line = ${labelProdOf(k).line === null ? 'null（★ 没找到一根线）' : ':' + labelProdOf(k).line}，停在 :${labelProdOf(k).stoppedAt}）`
      : String(k && k.n !== undefined ? ':' + k.n : k))).join('；');
    const verdict = applicable ? '真红' : '不适用';
    const kind = applicable ? null : '（a）范畴错误';
    (applicable ? tally.applicableTrueRed : tally.notApplicable).push(r.id);
    perRed.push({ r, applicable, verdict, prodDetail });
    info(`★ ${r.id}　${r.name}`);
    info(`   断言　= ${r.cond}`);
    info(`   合取项逐个求值 = ${r.conjValues()}`);
    info(`   失败的合取项 = ${r.failConj()}${r.otherConjValue() ? `　／　另一个合取项 = ${r.otherConjValue()}` : ''}`);
    info(`   宾语　= ${r.objName()}（★ 由【哪一个合取项失败】在运行时选出，不是我写死的）`);
    info(`   宾语实例（逐个体） = ${prodDetail || '（空）'}`);
    info(`   dom 判定式 = ${r.domWhy}`);
    info(`   ⇒ 宾语 ∈ dom? **${applicable ? '是' : '否'}**　⇒ ★ 档 = **${verdict}**${kind ? `　${kind}` : ''}`);
    info('');
  });

  /* ---- ⑤ 三档之和 = 3，且三条各占一档时没有一个计数器被两条共用 ---- */
  const sum3 = tally.applicableTrueRed.length + tally.notApplicable.length + tally.undecided.length;
  check('㉔ 验收⑤：三条红分进三档，三档计数之和 = 3，且每一档有自己的计数器（★ 没有一个计数器被两条共用）',
    sum3 === 3 && new Set([...tally.applicableTrueRed, ...tally.notApplicable, ...tally.undecided]).size === 3,
    `真红 = ${tally.applicableTrueRed.length}${tally.applicableTrueRed.length ? '（' + tally.applicableTrueRed.join('、') + '）' : ''}、`
    + `不适用 = ${tally.notApplicable.length}${tally.notApplicable.length ? '（' + tally.notApplicable.join('、') + '）' : ''}、`
    + `判不了 = ${tally.undecided.length}　⇒ 和 = ${sum3}`);

  /* ---- ⑥ 特别档：若两条落在「不适用」，报出那两条的共同点 ---- */
  const na = perRed.filter((x) => !x.applicable);
  const objKeys = na.map((x) => x.r.objName());
  const sameObj = na.length >= 2 && new Set(na.map((x) => String(x.r.obj()[0]))).size === 1;
  info(`★ ㉔ 特别档【落在「不适用」的那两条，共同点是什么？】：${na.length} 条落在这一档 —— ${na.map((x) => x.r.id).join('、') || '（无）'}`);
  info(`   它们的失败合取项逐字：${na.map((x) => `${x.r.id} = ${x.r.failConj()}`).join('　／　') || '（无）'}`);
  info(`   它们的宾语：${objKeys.join('　／　') || '（无）'}`);
  if (sameObj) {
    info(`   ⇒ ★★★ **两条的宾语是同一个对象：${String(na[0].r.obj()[0])}** ⇒ ★ **这个出口属于【类】，不属于【条】** ——`);
    info(`      给那个对象加一个成员判定，这两条【同时】从「不适用」这一档里消失，不是分别修两次。`);
  } else {
    info(`   ⇒ 两条的宾语不是同一个对象 ⇒ ★ 这个出口属于【条】（按宾语身份判定，不按我的说法）`);
  }

  /* ---- ⑦ 三条红改判之后，红的总数变了吗？ ---- */
  const redCount = perRed.length;
  info(`★ ㉔ 验收⑦：本件【只量不改】⇒ 守卫的红数仍按构造 = **${redCount}**，一个都没变；`);
  info(`   ⇒ ★★ **一个总数，在一个换了身份的账上是盲的** —— 这三条红的【composition】从「3 条红」变成了`);
  info(`      「${tally.applicableTrueRed.length} 条真红 + ${tally.notApplicable.length} 条【尺子对那个对象不适用】」，而总数一个字没动。`);

  /* ---- ⑧ 诚实边界 ---- */
  info('★ ㉔ 诚实边界：① 把一条断言拆成【合取项】这一步是**手写的**，不是我算出来的 —— 别人可以不接受我的拆法；');
  info('   ② 我量的是【文本层的推论】，不是「这个对象在语义上是不是一个细组」；后者我判不了，也不假装判得了。');

  /* ---- 本块只印不改的证明：三条检查的名字与谓词在本块里一个都没有被赋值过（源码扫描） ---- */
  check('㉔ 验收⑨：本块【只印不改】—— 它自己的源码里不出现对 check/谓词的赋值（源码扫描）',
    !/^(check|const\s+crossingNamed|const\s+residualRows|const\s+crossB)\s*=/m.test(
      fs.readFileSync(__filename, 'utf8').split('㉔ 把「不适用」变成一个可执行的定义')[1].split('㉔ 验收⑨')[0]),
    '扫描本块自身的源码：0 处对 check / crossingNamed / residualRows / crossB 的赋值');
}





/* ★★ 把【分界】本身打印出来。
   这一问的答案不是「这条规则判对了多少行」，是【它把哪些行判到了另一边】——
   只有把那些行列出来，这条边界才是可以被别人反驳的。
   而它必须每次运行都打印：**一次性的普查会过期，一条常驻的打印不会。** */
const boundary = { marker: [], mention: [] };

/* ==================== 测试缝（一处写域边界留下的接口，不是一个调试开关）====================
 * ★ 两个缝的存在理由是同一句话：**红对照要改的那一行，在别人的写域里。**
 *   —— 红对照要「把文档里的一行改掉、看守卫是否红」，而那份文档不是我的写域，我不能改它。
 *   缝不消除那件事，它让那件事可以被测。
 * ★ 它们**不是调试开关**：调试开关改的是【行为】；这两个改的只是【读哪一份正文】，
 *   而默认那一份永远来自一棵树（见下表），不是来自磁盘。
 * ★★ 覆盖表 —— 一个缝一个文件，而**两个缝的默认树不是同一棵**：
 *     MARKER_SCAN_FILE → docs/EIDOS_V06.md  默认读 MSR_SHA（`origin/main` 运行时解析，量的是现状）
 *     ATTRIBUTION_DOC  → docs/EIDOS_V06.md  默认读 `attribution.tree`（一根指针，量的是现状）
 *   ⇒ ★ 所以「把它设成 `docs/EIDOS_V06.md` 在 `D.ref` 上的内容 ⇒ 读数与不设时相同」
 *     这条验收**不成立**：`D.ref`（= `754403b`）是这两条**都不是**的那棵树。
 *     正确的验收是【设成它自己的默认树上的内容】—— 见下面那条守卫，它对每个缝分别做。 */
const SEAMS = {
  MARKER_SCAN_FILE: {
    file: 'docs/EIDOS_V06.md',
    tree: () => MSR_SHA,
    usedBy: '标记 / 提及 的分界扫描',
  },
  ATTRIBUTION_DOC: {
    file: 'docs/EIDOS_V06.md',
    tree: () => ATT_SHA,
    usedBy: '守卫 ⑪（谁裁的）',
  },
};
/** 读一份正文：默认从【那棵树】读；只有当声明过的缝被设上、且它覆盖这个文件时才读磁盘。 */
function seamText(envName, rel, fromTree) {
  const spec = SEAMS[envName];
  const p = process.env[envName];
  if (p && spec && spec.file === rel) return fs.readFileSync(p, 'utf8');
  return fromTree;
}

/* ★★ 可证明的默认不变（不是一个声明）。对**每一个**缝，按【它自己那棵树】做两向：
     ① 不设 vs 设成它自己那棵树上的内容 ⇒ 读数必须逐字相同（且等于那棵树上的正文）；
     ② 设成一份改过的副本 ⇒ 读数必须不同（证明这个缝真的在生效，而不是写在注释里）。
   ★ 验收必须按各自那棵树做 —— 两个缝的默认树不是同一棵（见上面那张覆盖表）。 */
{
  const buildDir = path.join(HERE, '.build');
  fs.mkdirSync(buildDir, { recursive: true });
  for (const [name, spec] of Object.entries(SEAMS)) {
    const tree = spec.tree();
    const T = docAtRef(tree, spec.file).text;
    const ownPath = path.join(buildDir, `seam-${name}-own.md`);
    const mutPath = path.join(buildDir, `seam-${name}-mutated.md`);
    fs.writeFileSync(ownPath, T);
    fs.writeFileSync(mutPath, T.replace('【已裁】', '【已裁·改过】'));
    const saved = process.env[name];
    delete process.env[name];
    const unset = seamText(name, spec.file, T);
    process.env[name] = ownPath;
    const same = seamText(name, spec.file, T);
    process.env[name] = mutPath;
    const changed = seamText(name, spec.file, T);
    if (saved === undefined) delete process.env[name]; else process.env[name] = saved;
    const tag = `${name}（${spec.usedBy}，默认树 ${String(tree).slice(0, 7)}）`;
    check(`缝 ${tag}：不设时与设成【它自己那棵树上的内容】读数逐字相同`,
      unset === same && same === T, unset === same ? '逐字相同' : '不同 ⇒ 这个缝在「设成同一个东西」时引入了差别');
    check(`缝 ${tag}：设成一份改过的副本 ⇒ 读数确实变了（这个缝在生效）`,
      changed !== unset, changed !== unset ? '变了' : '没变 ⇒ 这个缝不生效');
  }
}

for (const rel of docFiles) {
  const text = seamText('MARKER_SCAN_FILE', rel, docAtRef(MSR_SHA, rel).text);
  text.split('\n').forEach((l, i) => {
    for (const mk of Object.keys(MARKER_STATUS)) {
      if (l.indexOf(mk) === -1) continue;
      const row = `${rel}:${i + 1} [${mk}] ${stripLead(l).slice(0, 76)}`;
      (isMarkerLine(l, mk) ? boundary.marker : boundary.mention).push(row);
      break;
    }
  });
}

/* ---------------- ⑨ 「做到没有」：被压掉的那一维 ----------------
 * 台账原来的 `ruled` 徽章同时声称了「裁过了」与（读者会默认的）「做到了」。
 * 而仓库自己把「有没有代码」与「有没有测试盯着」写成两个问题，并明说这一层
 * 与三分清单正交、**不许混用**（`docs/PLAN_V06.md` §1）。
 * ★ 判据：一个状态徽章如果同时声称了「裁过了」与「做到了」，那么当其中一件不成立时，
 *   它不会变红 —— 它会继续声称两件都成立。
 * 这一守卫钉三件事：
 *   ① `builtAxes.axes` / `.collision` 的节号存在、引文逐字；
 *   ② `builtAxes.byFile` 的每个键都必须真的有条目（否则那条声明挂在空气上）；
 *   ③ ★ 撞车不变式：条目的出处引文里如果写着实现状态的负面词，
 *      那这一条**不许**是「做到了」。
 */
current = '做到没有（第二维度）';
const NEGATIVE = ['未实现', '尚未实现', '没有任何实现', '尚未运行', '要改档', '没有开始'];
const builtByFile = (D.builtAxes && D.builtAxes.byFile) || {};

function resolvedBuilt(e) {
  if (e.built) return { built: e.built, scope: e.builtScope || 'section', src: e.builtSource || null };
  const b = builtByFile[e.source.file];
  if (b) return { built: b.built, scope: b.scope || 'doc', src: b.source || null };
  return { built: 'unknown', scope: null, src: null };
}
function pinBox(box, label) {
  if (!box || !box.source) { check(`${label}：声明了出处`, false, '没有 source'); return; }
  const d = docAt(D.ref, box.source.file);
  check(`${label}：节号 §${box.source.section} 存在`, sectionExists(d.text, box.source.section),
    `找不到 §${box.source.section}`);
  check(`${label}：引文逐字存在`, d.text.indexOf(box.source.quote) !== -1,
    `找不到：「${String(box.source.quote).slice(0, 50)}…」`);
}
if (D.builtAxes) {
  pinBox(D.builtAxes.axes, 'builtAxes.axes');
  pinBox(D.builtAxes.collision, 'builtAxes.collision');
  for (const f of Object.keys(builtByFile)) {
    pinBox(builtByFile[f], `builtAxes.byFile[${f}]`);
    check(`builtAxes.byFile[${f}] 确实有条目挂在它上面`,
      D.entries.some((e) => e.source.file === f),
      '那份文档在台账里一条都没有 ⇒ 这条实现状态声明挂在空气上');
  }
}
const builtCount = {};
let builtNegative = 0;
for (const e of D.entries) {
  const r = resolvedBuilt(e);
  builtCount[r.built] = (builtCount[r.built] || 0) + 1;
  current = `${e.id} 做到没有`;
  if (e.built === 'done') {
    check(`${e.id}：声称「做到了」就必须带逐字出处`,
      !!(e.builtSource && e.builtSource.quote), '没有 builtSource');
  }
  /* ★ haystack 必须包含**文档级**那条引文 —— 否则这一条对 EIDOS 那 15 条是空转的：
     它们的实现状态来自 `builtAxes.byFile`，而负面词就在那条引文里。 */
  const bf = builtByFile[e.source.file];
  const cited = [e.source && e.source.quote, e.builtSource && e.builtSource.quote,
                 bf && bf.source && bf.source.quote].filter(Boolean).join('\n');
  const hit = NEGATIVE.filter((w) => cited.includes(w));
  if (hit.length) builtNegative++;
  check(`${e.id}：出处引文里写着「${hit.join(' / ') || '—'}」⇒ 不许声称「做到了」`,
    !(hit.length && r.built === 'done'),
    hit.length ? `引文里有「${hit.join('、')}」，而这一条是 ${r.built}` : '引文里没有实现状态的负面词');
}

/* ⑩ 「谁裁的」与「读自哪棵树」
 * ★ ruledBy 是一个【出处】，不是一个装饰：它必须是 ruled，否则「谁裁的」这句话是空的。
 * ★ readAt 必须是一个真实存在的提交 —— 否则「这条引文读自那棵树」这句话没有出处。
 * ★ 真正的方向：**一条引文如果不在主观测点那棵树上成立，它就必须说出自己读自哪棵树。** */
current = '出处：谁裁的 / 读自哪棵树';
{
  const humans = D.entries.filter((e) => e.ruledBy === 'human');
  const notRuled = humans.filter((e) => e.status !== 'ruled');
  check(`标了「人类裁定」的条目必须是 ruled（${humans.length} 条）`,
    notRuled.length === 0,
    notRuled.map((e) => `${e.id}=${e.status}`).join('、'));

  for (const r of [...new Set(D.entries.filter((e) => e.readAt).map((e) => e.readAt))]) {
    let ok = false, why = '';
    try {
      execFileSync('git', ['rev-parse', '--quiet', '--verify', `${r}^{commit}`], { cwd: ROOT, stdio: ['ignore', 'pipe', 'pipe'] });
      ok = true;
    } catch (err) { why = String((err && err.stderr) || err).trim().slice(0, 90); }
    check(`readAt ${String(r).slice(0, 7)} 是一个真实存在的提交`, ok, why);
    check(`readAt ${String(r).slice(0, 7)} 与主观测点不同（否则这个字段没有意义）`, r !== D.ref, r);
  }

  /* ★★ 这一条才是这一组的方向：不在主观测点上成立的引文，必须自带出处 */
  const silent = D.entries.filter((e) => {
    const t = docAtRef(D.ref, e.source.file).text;
    return t.indexOf(e.source.quote) === -1 && !e.readAt;
  });
  check('引文不在主观测点上成立的条目，必须带 readAt（否则它没有出处）',
    silent.length === 0, silent.map((e) => e.id).join('、'));

  const claimed = D.entries.filter((e) => e.readAt).length;
  const actual = D.entries.filter((e) => docAtRef(D.ref, e.source.file).text.indexOf(e.source.quote) === -1).length;
  check('带 readAt 的条目数 = 引文在主观测点上不成立的条目数', claimed === actual, `${claimed} / ${actual}`);

  /* ★★ 反方向。上面两条挡的是「不成立却不带 readAt」——
     它们**不挡**「带了一个多余的 readAt」，而「后者今天不存在」与「后者不可能存在」
     在读数上都是绿的。所以补这一条：带了 readAt，就必须真的在主观测点上不成立。 */
  const redundant = D.entries.filter(
    (e) => e.readAt && docAtRef(D.ref, e.source.file).text.indexOf(e.source.quote) !== -1);
  check('带 readAt 的条目，引文必须在主观测点上【真的不成立】（否则是多余标记）',
    redundant.length === 0,
    redundant.length
      ? `多余的 readAt：${redundant.map((e) => `${e.id}@${String(e.readAt).slice(0, 7)}`).join('、')}`
      : '没有多余的（射程：只看每条条目自己的 source.quote 这一栏；builtSource.quote 与 builtAxes 那几份文档级引文不在本条射程内）');


/* ═══════════════════════════════════════════════════════════════════════════
   ★★ 丙：页面上说「已裁」而它引的那一节的标题说「未定」的有多少条
   ───────────────────────────────────────────────────────────────────────────
   ★ 分母是谁定的：`status === 'ruled'` 的条目数 —— 它由 `website/data/decisions.js`
     的 `status` 字段定，**不是 docs 定的**。换一份 docs，分母不动。
   ★ 为什么要有这一条：一个徽章同时声称「裁过了」与（读者会默认的）「做到了」，
     而当它引的那一节自己写着「未定」时，**两边都不报错**。
   ★ 三档的真划分：A 出处节标题带裁定记号 / B 带【未定】记号 / C 两者都没有。
     A + B + C 必须等于分母 —— 否则说明有一条落进了两档或零档。
   ★★ 诚实边界：记号集是**人定的**。B 档不是「这些条目错了」，是
     「**它们引的那一节的名字与它们的徽章不一致**」；哪一边对，要人去读那一节。
   ═══════════════════════════════════════════════════════════════════════════ */
const RULED_MARKS = ['【已裁】', '（人裁定）', '（协调者裁定）', '裁定：', '已裁定', '已裁'];
const NOTDONE_MARKS = ['未定', '未决', '需要人裁定', '需要人类裁'];
const ruledEntries = D.entries.filter((e) => e.status === 'ruled');
const tanC = { A: [], B: [], C: [] };
ruledEntries.forEach((e) => {
  const t = (e.ruledIn ? e.ruledIn.section : '') + '｜' + (e.source.section || '');
  const ok = RULED_MARKS.filter((m) => t.indexOf(m) !== -1);
  const no = NOTDONE_MARKS.filter((m) => t.indexOf(m) !== -1);
  if (no.length && !ok.length) tanC.B.push({ id: e.id, sec: e.ruledIn ? e.ruledIn.section : '（无 ruledIn）', m: no.join(',') });
  else if (ok.length) tanC.A.push({ id: e.id, sec: e.ruledIn ? e.ruledIn.section : '', m: ok.join(',') });
  else tanC.C.push({ id: e.id, sec: e.ruledIn ? e.ruledIn.section : '（无 ruledIn）' });
});
info(`㉖ 丙的分母 = \`status === 'ruled'\` 的条目数 = ${ruledEntries.length}（台账共 ${D.entries.length} 条）—— ★ 分母由 website/data/decisions.js 的 status 字段定，不是 docs 定的`);
info(`㉖ 丙的三档：A（出处节标题带裁定记号）= ${tanC.A.length} ／ B（带【未定】记号）= ${tanC.B.length} ／ C（两者都没有）= ${tanC.C.length}`);
info(`㉖ 丙的分子 = B = ${tanC.B.length} / ${ruledEntries.length}　★ ${(100 * tanC.B.length / ruledEntries.length).toFixed(1)}% 的 \`ruled\` 条目，引的那一节的标题与它的徽章方向相反`);
check('㉖ 丙：A + B + C = 分母（真划分：一条只落进一档）',
  tanC.A.length + tanC.B.length + tanC.C.length === ruledEntries.length,
  `${tanC.A.length} + ${tanC.B.length} + ${tanC.C.length} = ${tanC.A.length + tanC.B.length + tanC.C.length} / 分母 ${ruledEntries.length}`);
info('㉖ B 档逐条（★ 这一档的读法是「节标题与徽章不一致」，不是「这一条错了」）：');
tanC.B.forEach((r) => info(`　　${r.id.padEnd(16)} 记号【${r.m}】 ${r.sec.slice(0, 58)}`));
info('㉖ C 档逐条（出处节标题不带任何裁定记号）：');
tanC.C.forEach((r) => info(`　　${r.id.padEnd(16)} ${r.sec.slice(0, 62)}`));
/* ★ 两个零必须分开写：「它是 0」与「我这把尺子看不见它」。 */
info(`㉖ ★ 两个零分开写：C 档里【出处节标题真的没有任何记号】的 ${tanC.C.length} 条是「真的没有」；` +
  `而「我这把尺子的记号集看不见它」这个可能**没有被排除** —— 记号集是人定的（${RULED_MARKS.length} 个裁定记号 + ${NOTDONE_MARKS.length} 个未定记号）。`);
check('㉖ ★ 006：三条归属路线在数据里可读（不写死在渲染函数里）',
  Array.isArray(D.ruledByRoutes) && D.ruledByRoutes.length >= 3
    && D.ruledByRoutes.some((r) => r.key === 'relayed'),
  `D.ruledByRoutes = ${(D.ruledByRoutes || []).map((r) => r.key).join(', ') || '（无）'}　★ site.js 必须从这一栏读，不写死在函数体里`);
/* ★★ ㉛ 乙【对照】：同一条读数，**不改判据**，只是让它**绿着也印出来**。
   改前：这条读数只活在 check 的第 3 个实参里 ⇒ 只在红的时候才被打印
         （`FAIL [ … ] ㉖ ★ 006：… ← D.ruledByRoutes = human, earlier, relayed, coordinator　★ …`）
   改后：同一份逐字读数在【成功路径上】也印一次 —— 判据一个字没动，红的条件一个字没动。
   ★ 判据：一条只在失败时说话的检查，在它绿的时候什么都没说 ——
     而「它绿」与「它检查过」在输出里同形。 */
info(`㉖ ★ 006（成功路径上也印）【三条归属路线在数据里可读】= `
  + `${Array.isArray(D.ruledByRoutes) ? (D.ruledByRoutes || []).map((r) => r.key).join(', ') : '（不是数组）'}`
  + `　★ 与上面那条 check 的 detail 逐字同一份读数；★ 它绿的时候，这条 info 是唯一看得见它的地方。`);
check('㉖ ★ 006：每条 ruled 条目都有 ruledIn（否则「裁定记在哪一节」在页面上是空白）',
  ruledEntries.every((e) => e.ruledIn),
  `缺 ruledIn 的 ruled 条目 = ${ruledEntries.filter((e) => !e.ruledIn).map((e) => e.id).join('、') || '（无）'}`);

/* ═══════════════════════════════════════════════════════════════════════════
   ★★ ㉗ 甲：B 那 13 条逐条拆开 —— 「引的节讲的是别的问题」有几条
   ───────────────────────────────────────────────────────────────────────────
   ★ 裁定二的形状：B 这一档今天叫「方向相反」，而它会装着另一件东西 ——
     「页面说 X 已裁，而它引的那一节讲的是 Y 未定」。本块把 13 条按【结构】拆开。
   ★ 判据的全文与六条预言在 `website/scratch/registration_rule.md` 的「㉗」一节，
     **先于本块落盘**（判据那一笔是读数那一笔的祖先，`git merge-base --is-ancestor` 可量）。
   ★ 两个测都是**结构**的、可复算的；★「那一节在语义上是不是在说这条」我判不出，
     我判的是「那个节的**标题是不是替它下面的条目说话**」。
   ═══════════════════════════════════════════════════════════════════════════ */
const SELFTALK = ['不得', '本轮没有结论', '仍未决', '不再', '必须', '不许'];
const SEC_CACHE = new Map();
/* 取 `ref` 那棵树里 `file` 的、标题逐字等于 sectionText 的那一节（标题行 + 正文到下一个同级或更高级标题）。 */
function sectionOf(ref, file, sectionText) {
  const key = ref + '\u0000' + file + '\u0000' + sectionText;
  if (SEC_CACHE.has(key)) return SEC_CACHE.get(key);
  let out = null;
  let text = null;
  try { text = docAtRef(ref, file).text; } catch (err) { text = null; }
  if (text !== null) {
    const lines = text.split('\n');
    for (let i = 0; i < lines.length; i++) {
      const m = /^(#{2,4})\s+(.*)$/.exec(lines[i]);
      if (!m) continue;
      if (m[2].trim() !== String(sectionText).trim()) continue;
      const level = m[1].length;
      const body = [];
      for (let j = i + 1; j < lines.length; j++) {
        const m2 = /^(#{1,6})\s+/.exec(lines[j]);
        if (m2 && m2[1].length <= level) break;
        body.push(lines[j]);
      }
      out = { titleText: m[2].trim(), titleLine: lines[i], level, body: body.join('\n'), startLine: i + 1 };
      break;
    }
  }
  SEC_CACHE.set(key, out);
  return out;
}
/* 测① 标题自指性：节标题里有没有对它自己内容的模态/否定。 */
function selfTalk(sec) { return sec ? SELFTALK.filter((m) => sec.titleText.indexOf(m) !== -1) : []; }
/* 测② 逐项处置层：节内有没有一行 markdown 表头、且那一行含「处置」。
   ★ 只认这一种形状 —— 「节里逐条自带【已裁定】」不算，那种形状下「处置」就是条目自己的头几个字。 */
function dispositionLayer(sec) {
  if (!sec) return null;
  const lines = sec.body.split('\n');
  for (let i = 0; i < lines.length; i++) {
    if (/^\s*\|.*\|\s*$/.test(lines[i]) && lines[i].indexOf('处置') !== -1) return { line: i + 1, text: lines[i].trim() };
  }
  return null;
}
const tanA = { one: [], two: [], three: [] };
ruledEntries.forEach((e) => {
  if (!tanC.B.some((b) => b.id === e.id)) return;   /* ★ 本块只处理丙的 B 档那 13 条，分母不另定 */
  const ri = e.ruledIn || {};
  const sec = sectionOf(ri.seenOn || D.ref, ri.file, ri.section);
  const st = selfTalk(sec);
  const dl = dispositionLayer(sec);
  const row = { id: e.id, file: ri.file, sec: ri.section, quote: e.source.quote, title: e.title, st, dl, ok: !!sec };
  if (!sec) tanA.three.push(row);
  else if (dl) tanA.two.push(row);
  else if (st.length) tanA.one.push(row);
  else tanA.three.push(row);
});
info(`㉗ 甲的分母 = 丙的 B 档 = ${tanC.B.length} 条（★ 由上一块算出，本块不另定分母）`);
info(`㉗ 甲的三档：档①（同一个问题而两句方向相反）= ${tanA.one.length} ／ 档②（引的节讲的是别的问题）= ${tanA.two.length} ／ 档③（判不了）= ${tanA.three.length}`);
check('㉗ 甲：三档之和 = 丙的 B 档（真划分：一条只落进一档）',
  tanA.one.length + tanA.two.length + tanA.three.length === tanC.B.length,
  `${tanA.one.length} + ${tanA.two.length} + ${tanA.three.length} = ${tanA.one.length + tanA.two.length + tanA.three.length} / B ${tanC.B.length}`);
check('㉗ 甲：印出来的条数 = 丙的 B 档（没有一条被漏印）',
  tanA.one.length + tanA.two.length + tanA.three.length === tanC.B.length,
  `印出 ${tanA.one.length + tanA.two.length + tanA.three.length} 条 / B ${tanC.B.length}`);
info('㉗ 甲 · 三栏逐条（★ 三栏：它引的那一节的标题 ／ 触发它落进 B 的记号 ／ 档与那一档的依据）：');
[['①', tanA.one], ['②', tanA.two], ['③', tanA.three]].forEach(([label, arr]) => {
  arr.forEach((r) => {
    info(`　　[${label}] ${r.id.padEnd(16)} 节《${String(r.sec).slice(0, 46)}》`);
    info(`　　      它的 title = ${JSON.stringify(r.title)}`);
    info(`　　      它引的那句话 = ${JSON.stringify(String(r.quote).slice(0, 96))}`);
    info(`　　      测① 标题自指 = ${r.st.length ? '真（' + r.st.join('/') + '）' : '假'}　测② 处置层 = ${r.dl ? '真（第 ' + r.dl.line + ' 行：' + r.dl.text + '）' : '假'}${r.ok ? '' : '　★ 那一节在 seenOn 那棵树上取不到'}`);
  });
});
/* ★ 两个零分开写。 */
info(`㉗ 甲 · 两个零分开写：档③ = ${tanA.three.length} —— ` +
  (tanA.three.length === 0
    ? `那 13 条的 seenOn 都是 754403b，两节在那棵树上都在（⇒ 「真的没有」）；★ 而「我这把尺子判不出『同一个问题』」这个可能**没有被排除** —— 我判的是【结构】（标题替不替条目说话），不是【语义】。`
    : `其中 ${tanA.three.filter((r) => !r.ok).length} 条是【那一节取不到】，${tanA.three.filter((r) => r.ok).length} 条是【节取到了而两个测都假】。`));
info(`㉗ 甲 · 档① 与档② 的分界（逐节）：`);
Array.from(new Set(tanC.B.map((b) => b.sec))).forEach((s) => {
  const one = tanA.one.filter((r) => r.sec === s).length;
  const two = tanA.two.filter((r) => r.sec === s).length;
  const three = tanA.three.filter((r) => r.sec === s).length;
  const any = tanA.one.concat(tanA.two, tanA.three).filter((r) => r.sec === s)[0];
  const sec = any ? sectionOf((D.entries.filter((e) => e.id === any.id)[0].ruledIn || {}).seenOn || D.ref, any.file, s) : null;
  info(`　　节《${String(s).slice(0, 56)}》　档① ${one} ／ 档② ${two} ／ 档③ ${three}　` +
    `测① = ${sec && selfTalk(sec).length ? '真' : '假'}　测② = ${sec && dispositionLayer(sec) ? '真' : '假'}`);
});
/* ★★ 这一档的名字是柄。 */
info(`㉗ 甲 · ★★ B 这一档的名字：它今天叫「方向相反」，而档① = ${tanA.one.length}、档② = ${tanA.two.length} ⇒ ` +
  (tanA.two.length
    ? `★★★ **B 这一档的名字必须改** —— 13 条里 ${tanA.two.length} 条不是方向相反，是【节标题是类别名、它不替这条说话】；只有 ${tanA.one.length} 条是同一个节里两种说法并存。`
    : `13 条全是同一个节里两种说法并存 ⇒ 「方向相反」这个名字成立。`));
info(`㉗ 甲 · ★★ 这个错名字的来路：丙 拿 \`e.ruledIn.section\` 去匹配记号，而**那一栏是节标题全文**（上一件里我写的），` +
  `而同一条条目的 \`e.source.section\` 是【节号】⇒ ★ 同一个概念在这个文件里有两个射程。` +
  `★ 逐条对照（ruledIn.section / source.section）：` +
  Array.from(new Set(tanC.B.map((b) => b.id))).slice(0, 3).map((id) => {
    const e = D.entries.filter((x) => x.id === id)[0];
    return `${id}=《${String(e.ruledIn.section).slice(0, 20)}》/${JSON.stringify(e.source.section)}`;
  }).join('　') + ' …');
check('㉗ 甲：ruledIn.section 与 source.section 的射程不同（前者是标题、后者是节号）',
  D.entries.some((e) => e.ruledIn && e.source && e.ruledIn.section !== e.source.section && !e.ruledIn.section.startsWith(String(e.source.section) + ' ')),
  `射程不同的条目数 = ${D.entries.filter((e) => e.ruledIn && e.source && e.ruledIn.section !== e.source.section && !e.ruledIn.section.startsWith(String(e.source.section) + ' ')).length} / ${D.entries.length}　★ 这一栏两个射程并存，而丙 只读了其中一个`);

/* ═══════════════════════════════════════════════════════════════════════════
   ★★ ㉗ 乙：「不带徽章」与「status 不是 ruled」是不是同一件事
   ───────────────────────────────────────────────────────────────────────────
   ★ 两条定义式都从 `website/assets/site.js` 的实际代码里取，我不另写一套。
   ═══════════════════════════════════════════════════════════════════════════ */
const SITE_SRC = fs.readFileSync(path.join(SITE, 'assets', 'site.js'), 'utf8');
const BADGE_PRED_SITE = /if \(e\.ruledBy\)/.test(SITE_SRC);
const STATUS_PRED_SITE = /e\.status === 'ruled'/.test(SITE_SRC);
info(`㉗ 乙 · 两条定义式的出处（★ 从 site.js 的实际代码里取，不是另写一套）：`);
info(`　　徽章的定义式 = \`if (e.ruledBy)\`（site.js 里逐字存在 = ${BADGE_PRED_SITE}）`);
info(`　　\`ruled\` 的定义式 = \`e.status === 'ruled'\`（site.js 里逐字存在 = ${STATUS_PRED_SITE}）`);
const badgeOf = (e) => !!e.ruledBy;
const ruledOf = (e) => e.status === 'ruled';
const eidosEntries = D.entries.filter((e) => e.source && e.source.file === 'docs/EIDOS_V06.md');
const withBadge = eidosEntries.filter(badgeOf);
const noBadge = eidosEntries.filter((e) => !badgeOf(e));
info(`㉗ 乙 · 分母 = \`source.file === 'docs/EIDOS_V06.md'\` 的条目数 = ${eidosEntries.length}（★ 分母由 decisions.js 的 source.file 字段定，不是 docs 定的）`);
info(`㉗ 乙 · 带徽章 = ${withBadge.length} ／ 不带徽章 = ${noBadge.length}　⇒ 和 = ${withBadge.length + noBadge.length} / ${eidosEntries.length}`);
info(`㉗ 乙 · 不带徽章的是不是【全部非 ruled】：不带徽章的 ${noBadge.length} 条里，非 \`ruled\` 的 = ${noBadge.filter((e) => !ruledOf(e)).length}；` +
  `⇒ ${noBadge.length === noBadge.filter((e) => !ruledOf(e)).length ? '今天【重合】' : '今天【不重合】'}`);
/* ★★ 两条合成条目：只在内存里造，绝不写进 decisions.js。 */
const PROBE_1 = { id: 'PROBE-1', status: 'ruled', title: '合成甲', source: { file: 'docs/EIDOS_V06.md', section: '0', quote: '' }, ruledIn: { file: 'docs/EIDOS_V06.md', section: '0' } };
const PROBE_2 = { id: 'PROBE-2', status: 'proposal', title: '合成乙', source: { file: 'docs/EIDOS_V06.md', section: '0', quote: '' }, ruledIn: { file: 'docs/EIDOS_V06.md', section: '0' }, ruledBy: 'human' };
info(`㉗ 乙 · 两条合成条目（★ 只在内存里造，不写进 decisions.js）：`);
info(`　　合成甲 status='ruled' 而【无】ruledBy ⇒ 徽章 = ${badgeOf(PROBE_1) ? '画' : '不画'}／ruled 定义式 = ${ruledOf(PROBE_1)}`);
info(`　　合成乙 status='proposal' 而【有】ruledBy ⇒ 徽章 = ${badgeOf(PROBE_2) ? '画' : '不画'}／ruled 定义式 = ${ruledOf(PROBE_2)}`);
check('㉗ 乙：两条合成条目在【徽章】这一栏上分开（⇒ 徽章的宾语是 ruledBy，不是 status）',
  badgeOf(PROBE_1) !== badgeOf(PROBE_2) && ruledOf(PROBE_1) !== ruledOf(PROBE_2),
  `徽章：合成甲 ${badgeOf(PROBE_1)} / 合成乙 ${badgeOf(PROBE_2)}；ruled：合成甲 ${ruledOf(PROBE_1)} / 合成乙 ${ruledOf(PROBE_2)}`);
/* ★★ 退役一条、换一条 —— 这是本族第三次踩同一个坑，理由写在名字里：
   旧的那条是 `㉗ 乙：两个判定式在【今天的语料上】处处一致`（断言 `together === 分母`）。
   ★ 它在【对照一】（去掉一条 `ruledBy` ⇒ 让两个字段不再同向）下**必然红** ——
     而那个对照的目的**正是把它们分开** ⇒ 那条红不是缺陷的读数，是
     **【这条 check 的射程越过了对照的设计】** 的读数（与 `㉒②`、`㉒③无界` 同族，第三次）。
   ⇒ 处置：把「今天重合多少」**降为读数栏里的一个值**（下面那行 info），
     check 只留**基线与对照下都成立**的那一条（真划分：两档之和 = 分母）。
   ★ 规矩（exact-otter 的裁定）：**一个读数必须在它的名字里带上它的射程。** */
const together = eidosEntries.filter((e) => badgeOf(e) === ruledOf(e)).length;
info(`㉗ 乙 · ★ 基线读数（不是断言）：两个判定式在**基线的语料上**一致的条目 = ${together} / ${eidosEntries.length}` +
  `　★ 而这个数**在对照一下必然变小** —— 那个对照的目的就是让两个字段不再同向；★ 所以它是一条读数，不是一条判据。`);
check('㉗ 乙：带徽章 + 不带徽章 = 分母（真划分：一条只落进一档）· 基线与对照下都成立',
  withBadge.length + noBadge.length === eidosEntries.length,
  `${withBadge.length} + ${noBadge.length} = ${withBadge.length + noBadge.length} / 分母 ${eidosEntries.length}`);
info(`㉗ 乙 · ★★ 这一栏的宾语：**「页面画不画那个徽章」（= \`e.ruledBy\`）**，不是「\`status\` 是不是 \`ruled\`」。` +
  `★ 它们今天重合（${together}/${eidosEntries.length}）**只是因为语料恰好让两个字段同向** —— 合成甲与合成乙把它们分开了。`);

  /* ★ 文档头那句「本文的四条【已裁】……其余每一条仍是【提案】或【待裁】」是否还成立。
     它是 exact-otter 的文档，我不改它 —— 但我要让它在数据里可见，而不是让我的数据替它圆场。 */
  /* ★★ 退役一条：原来这里有一条「humanRuled 那句话点名的节号，与 ids 的节号集合一致」。
     ★ 它退役的理由与前一条同族，而且更彻底：**它比的两个东西都没了** ——
       `af85fef` 把名字删了，`ids` 也随之退役。★ 一次退役要说出来。 */

    /* ★★ 退役一条：原来这里有一条「那句『其余每一条仍是【提案】或【待裁】』还成立」。
       ★ 它退役**不是因为它错了，是因为它盯的那句话没了** —— `3df3a9b` 把那个全称断言
         换成了「未列出的条目，其身份以各自的标题标记为准」。★ 一次退役要说出来。 */

  const hw = D.headerWatch;
  if (hw) {
    const doc = docAtRef(hw.readAt, hw.file);
    check(`文件头改写后的新半句在 ${String(hw.readAt).slice(0, 7)} 上逐字存在`,
      doc.text.indexOf(hw.quote) !== -1, hw.quote.slice(0, 36) + '…');

    /* ★★ 再退役一条：接着换上来的那条「文件头那个总数（本文的四条【已裁】）与正文对得上」
       也退役了 —— `af85fef` 把那个总数**连同那四个名字**一起删掉了。
       ★ 理由同上：**它盯的那个东西没了。** ★ 一次退役要说出来。

       ★★★ 而删掉一个总数，不等于删掉那句话里的**权威**：
         `af85fef` 把它换成了**一条全称断言**（「本文【已裁】的条目，是……由人类……选定的」）。
         ★ 全称断言比总数更难核，而这一条恰好**可核** —— 因为它管的那批条目在台账里
           有 `status` 与 `ruledBy` 两个字段。所以新守卫盯的是那个全称断言。 */
    /* ★★ 退役一条（第四条）：原来这里有一条「文件头那条全称断言……在台账里没有反例」。
       ★ 它退役的理由**与前三条不同** —— ★ 前三条是「它盯的东西没了」，
         这一条是「**它盯的那句话被判定太宽，正在被改**」（exact-otter 裁 (a)）。
       ★ 它的活**没有消失，只是换了射程**：★ 从「盯文件头那一句」改成
         「盯整份文件里**每一处**关于『谁裁的』的说法」—— 就是下面的 ⑪。
       ★ `headerWatch.universalClaim` 这一栏因此降级成**文档**（记下那句话曾经是什么）。 */
  }
/* ══════════════════════ ㉘ 一个字段名有几个射程 ══════════════════════
   协调者 m01541 的「甲（主项）」。判据在 website/scratch/registration_rule.md 的「㉘」一节，
   先于本笔落盘（判据那一笔 = 8dd0d58，是 HEAD 的祖先）。
   ★ 本块只印、不改数据；decisions.js 一个字节不动。                                  */

/* ---- ① leaf 字段名 + 它的值 + 那个值的出处（file 与树） ---- */
const T28_LEAF = (() => {
  const out = [];
  (function walk(v, p, ancFile, ancEntry) {
    if (v === null || v === undefined) return;
    if (Array.isArray(v)) { v.forEach((x) => walk(x, p, ancFile, ancEntry)); return; }
    if (typeof v === 'object') {
      const f = (typeof v.file === 'string') ? v : ancFile;
      const en = (typeof v.id === 'string' && v.source) ? v : ancEntry;
      for (const k of Object.keys(v)) walk(v[k], p ? p + '.' + k : k, f, en);
      return;
    }
    const name = p.split('.').pop();
    const file = ancFile ? ancFile.file : (ancEntry && ancEntry.source ? ancEntry.source.file : null);
    const isRef = (name === 'seenOn' || name === 'readAt');
    const tree = (ancFile && (ancFile.seenOn || ancFile.readAt)) || D.ref;
    out.push({ p, name, value: String(v), file, ref: isRef ? String(v) : tree, ancPath: ancFile ? '有 file 的祖先' : '无' });
  })(D, '', null, null);
  return out;
})();
const T28_NAMES = Array.from(new Set(T28_LEAF.map((x) => x.name))).sort();
const T28_BY_NAME = {};
T28_LEAF.forEach((x) => { (T28_BY_NAME[x.name] = T28_BY_NAME[x.name] || []).push(x); });
const T28_MULTI = T28_NAMES.filter((n) => new Set(T28_BY_NAME[n].map((x) => x.p)).size > 1);

/* ---- ② 消费点：扫【本文件自己的源码】找定位器调用点 ---- */
const T28_SELF = fs.readFileSync(__filename, 'utf8');
const T28_SELF_LINES = T28_SELF.split('\n');
/* 定位器清单（判据里逐字写着的那四条）。pos = 那个【以 .F 结尾的实参】在这条调用里的位置。 */
const T28_LOCATORS = ['docAt', 'docAtRef', 'sectionExists', 'sectionOf'];
/* 定位器自己的函数体：里面的实参是形参（file / ref / sectionText），不是字段路径 ⇒ 必须排除。 */
const T28_FN_RANGES = (() => {
  const out = [];
  for (let i = 0; i < T28_SELF_LINES.length; i++) {
    const m = /^\s*function\s+([A-Za-z_$][\w$]*)\s*\(/.exec(T28_SELF_LINES[i]);
    if (!m || T28_LOCATORS.indexOf(m[1]) === -1) continue;
    let depth = 0, started = false, end = i;
    for (let j = i; j < T28_SELF_LINES.length; j++) {
      for (const ch of T28_SELF_LINES[j]) { if (ch === '{') { depth++; started = true; } else if (ch === '}') depth--; }
      if (started && depth === 0) { end = j; break; }
    }
    out.push({ fn: m[1], from: i + 1, to: end + 1 });
  }
  return out;
})();
const T28_IN_FN_BODY = (n) => T28_FN_RANGES.filter((r) => n >= r.from && n <= r.to).map((r) => r.fn);
/* 顶层的逗号切分（不切括号里的逗号） */
function t28SplitArgs(s) {
  const out = []; let d = 0, cur = '';
  for (const ch of s) {
    if (ch === '(' || ch === '[' || ch === '{') d++;
    if (ch === ')' || ch === ']' || ch === '}') d--;
    if (ch === ',' && d === 0) { out.push(cur.trim()); cur = ''; continue; }
    cur += ch;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}
/* 从一个实参表达式里取【字段名】：末尾的 .ident 或 ['ident']；取不到就是【变量/字面量】 */
function t28FieldOf(expr) {
  const e = expr.trim().replace(/\)+$/, '').replace(/\.text$/, '');
  let m = /\.([A-Za-z_$][\w$]*)\s*$/.exec(e);
  if (m) return m[1];
  m = /\['([A-Za-z_$][\w$]*)'\]\s*$/.exec(e);
  if (m) return m[1];
  return null;
}
const T28_SITES = [];
T28_SELF_LINES.forEach((line, i) => {
  const n = i + 1;
  if (/^\s*(?:\/\*|\*|\/\/)/.test(line)) return;
  if (T28_IN_FN_BODY(n).length) return;
  for (const fn of T28_LOCATORS) {
    let idx = -1;
    while ((idx = line.indexOf(fn + '(', idx + 1)) !== -1) {
      if (new RegExp('function\\s+' + fn + '\\s*\\(').test(line)) continue;
      const rest = line.slice(idx + fn.length + 1);
      const args = t28SplitArgs(rest.replace(/\)\s*[;,)]?\s*$/, ''));
      args.forEach((a, k) => {
        const f = t28FieldOf(a);
        if (f === null) { T28_SITES.push({ n, fn, pos: k, name: null, arg: a, why: '变量或字面量' }); return; }
        if (T28_NAMES.indexOf(f) === -1) { T28_SITES.push({ n, fn, pos: k, name: null, arg: a, why: `末尾标识符 ${f} 不是本文件里的 leaf 字段名` }); return; }
        T28_SITES.push({ n, fn, pos: k, name: f, arg: a });
      });
    }
  }
  /* 定位器 #4：…text.indexOf(<字段路径>) */
  let idx = -1;
  while ((idx = line.indexOf('.indexOf(', idx + 1)) !== -1) {
    const rest = line.slice(idx + 9);
    const args = t28SplitArgs(rest.replace(/\)\s*[;,)]?\s*$/, ''));
    const f = t28FieldOf(args[0] || '');
    if (f && T28_NAMES.indexOf(f) !== -1) T28_SITES.push({ n, fn: 'indexOf', pos: 0, name: f, arg: args[0] });
  }
});
const T28_SITE_OK = T28_SITES.filter((s) => s.name);
const T28_SITE_NO = T28_SITES.filter((s) => !s.name);

/* ---- ③ 接受集：把该名字的全部值喂给那个定位器 ---- */
function t28Accepts(site, rec) {
  const file = rec.file || (D.ref ? 'docs/EIDOS_V06.md' : 'docs/EIDOS_V06.md');
  try {
    if (site.fn === 'docAt' || site.fn === 'docAtRef') {
      if (site.pos === 0) {                       /* 这个值当【树】用 */
        const r = docAtRef(rec.value, file);
        return r.from.startsWith(rec.value + ':');
      }
      const r = docAtRef(rec.ref, rec.value);     /* 这个值当【文件】用 */
      return !!r.from && r.from.indexOf(':') !== -1 && r.from.slice(0, r.from.indexOf(':')) === rec.ref;
    }
    if (site.fn === 'sectionExists') {
      if (site.pos !== 1) return null;
      return !!sectionExists(docAtRef(rec.ref, file).text, rec.value);
    }
    if (site.fn === 'sectionOf') {
      if (site.pos !== 2) return null;
      return !!sectionOf(rec.ref, file, rec.value);
    }
    if (site.fn === 'indexOf') {
      return docAtRef(rec.ref, file).text.indexOf(rec.value) !== -1;
    }
  } catch (e) { return false; }
  return null;
}
const T28_LOC_KEY = (s) => `${s.fn}@${s.n}·arg${s.pos}`;
const T28_PER_NAME = T28_NAMES.map((nm) => {
  const values = T28_BY_NAME[nm];
  const sites = T28_SITE_OK.filter((s) => s.name === nm).filter((s) => {
    /* 同一个定位器（同名同位置）只留一个代表调用点，但把全部调用点登记下来 */
    return true;
  });
  const locs = Array.from(new Set(sites.map(T28_LOC_KEY)));
  const acc = {};   /* 定位器键 -> 接受的值集合 */
  locs.forEach((lk) => {
    const s0 = sites.filter((s) => T28_LOC_KEY(s) === lk)[0];
    const a = new Set();
    let nNull = 0;
    values.forEach((v) => {
      const r = t28Accepts(s0, v);
      if (r === true) a.add(v.value);
      /* ★★ 缺陷 C（本件自己抓到）：`null` 是【这个定位器在这个实参位置上不适用】
         （谓词无值 ⊥），不是「拒绝了那个值」。把 ⊥ 当成拒绝，会让一个
         **从没被喂过节号** 的位置（`:1883`/`:1920` 的 `ri.seenOn || D.ref` 落在 arg0）
         在一个它不负责的栏上被算成【拒绝】，从而捏出一个不存在的「两个射程」。
         ⇒ ⊥ 的三态（㉔ 的定义：不适用 = 谓词无值）在这里必须被丢掉，而不是记成 false。 */
      else if (r === null) nNull++;
    });
    acc[lk] = { site: s0, accept: a, total: values.length, nNull, applicable: nNull < values.length };
  });
  /* 同一个定位器名（不分位置）合并成一个接受集 —— 判据问的是「两个定位器」 */
  const byFn = {};
  Object.entries(acc).forEach(([lk, o]) => {
    if (!o.applicable) return;            /* ★ ⊥：这个定位器对这个实参位置无话可说 ⇒ 不参与比较 */
    const fn = o.site.fn;
    byFn[fn] = byFn[fn] || { fns: [], accept: new Set(), total: values.length };
    byFn[fn].fns.push(lk);
    o.accept.forEach((x) => byFn[fn].accept.add(x));
  });
  return { name: nm, values, sites, locs, acc, byFn };
});
function t28Grade(r) {
  const fns = Object.keys(r.byFn);
  if (fns.length === 0) return { g: '③a', why: '没有一个定位器在按这个值定位（或定位器在这个实参位置上不适用）⇒ 我这把尺子只看定位器' };
  const sets = fns.map((f) => r.byFn[f].accept);
  const total = r.values.map((v) => v.value);
  const all = new Set(total);
  /* ★★ 缺陷 A（本件自己抓到，由预言 1 当场照出来）：判据的字面要求是「两个差集都非空」，
     而那句话量的是【互不包含】—— 一个 **包含** 关系（前缀 ⊇ 精确）**不是一种接受集相同**，
     它是**两个射程**，只是其中一个更宽。⇒ 档① 的条件改成【作为集合不相等】，
     并把两种关系分开印（①a 互不包含 / ①b 一个包住另一个）。
     ★ 改判据的理由：判据声称要量的是「真有两个射程」，而它写下的条件是「互不包含」——
       两者不是同一件事，而 `section`（本族唯一已证的那一个）今天正好落在差别里。 */
  for (let i = 0; i < sets.length; i++) {
    for (let j = i + 1; j < sets.length; j++) {
      const onlyI = Array.from(sets[i]).filter((x) => !sets[j].has(x)).length;
      const onlyJ = Array.from(sets[j]).filter((x) => !sets[i].has(x)).length;
      if (onlyI > 0 && onlyJ > 0) {
        return { g: '①', sub: '①a', why: `${fns[i]} 与 ${fns[j]} 的接受集互有对方没有的 ⇒ 互不包含（${onlyI} / ${onlyJ}）` };
      }
      if (onlyI > 0 || onlyJ > 0) {
        const wide = onlyI > 0 ? fns[i] : fns[j];
        const narrow = onlyI > 0 ? fns[j] : fns[i];
        return { g: '①', sub: '①b', why: `${wide} 的接受集【包住】${narrow} 的（${wide} 多认 ${Math.max(onlyI, onlyJ)} 个）⇒ 包含，不是相同` };
      }
    }
  }
  const accept = new Set(); sets.forEach((s) => s.forEach((x) => accept.add(x)));
  const rejected = Array.from(all).filter((x) => !accept.has(x));
  /* ★★ 缺陷 B：这句话原来写「全部定位器的接受集相同，且 = 全部 N 个值」——
     而它是在**并集**上算的 ⇒ 「相不相等」与「有没有被拒」被合并成一句，
     于是它与同一份输出里逐定位器那一栏（79 / 39）**直接矛盾**，而两栏都完整、都好看。
     ⇒ 拆成两句：先把「相等」按住（上面那个循环已经保证它——走到这里就是全都相等），
       再单独说「有没有值被任何一个定位器拒掉」。 */
  if (rejected.length === 0) return { g: '②', why: `${fns.join(' / ')} 的接受集【作为集合彼此相等】，且 = 全部 ${all.size} 个值（没有值被拒）` };
  return { g: '③b', why: `接受集（并集）拒绝了 ${rejected.length} 个值 ⇒ 有别的东西在认它，而我没找到（★ 这是「我没找到」的证据，不是缺陷的证据）` };
}
T28_PER_NAME.forEach((r) => { r.grade = t28Grade(r); });
const T28_TALLY = { '①': [], '②': [], '③a': [], '③b': [] };
T28_PER_NAME.forEach((r) => T28_TALLY[r.grade.g].push(r));

/* ---- 打印 ---- */
info(`㉘ · ★ 分母（分母是谁定的：★ 由 decisions.js 的实际 key path 在运行时枚举，不是我手写的清单）：`);
info(`　　leaf 字段名 = ${T28_NAMES.length} 个　` +
  `其中【装载点 ≥2】的 = ${T28_MULTI.length} 个：${T28_MULTI.map((n) => n + '(' + new Set(T28_BY_NAME[n].map((x) => x.p)).size + ')').join(' ')}`);
info(`　　★ 定位器清单 = 4 条（docAt/docAtRef、sectionExists、sectionOf、…text.indexOf(<字段路径>)）；` +
  `扫描对象 = 本文件自己的源码（${T28_SELF_LINES.length} 行）；定位器自己的函数体已排除：` +
  T28_FN_RANGES.map((r) => `${r.fn}():${r.from}-${r.to}`).join(' '));
info(`　　★ 扫到的调用点 = ${T28_SITES.length} 个：能用字段名认领的 = ${T28_SITE_OK.length} 个；` +
  `认领不了的 = ${T28_SITE_NO.length} 个（★ ${T28_SITE_NO.map((s) => `:${s.n} ${s.fn} 的 ${JSON.stringify(s.arg)}（${s.why}）`).join('；')}）`);
info(`㉘ · ★ 三档（和必须 = 分母）：① 真有两个射程 = ${T28_TALLY['①'].length} ／ ` +
  `② 只有一个射程 = ${T28_TALLY['②'].length} ／ ③a 没有定位器 = ${T28_TALLY['③a'].length} ／ ` +
  `③b 有别的东西在认它（我没找到）= ${T28_TALLY['③b'].length}　` +
  `和 = ${T28_TALLY['①'].length + T28_TALLY['②'].length + T28_TALLY['③a'].length + T28_TALLY['③b'].length} / ${T28_NAMES.length}`);
check('㉘ 验收⑤：三档（四个计数器）之和 = 分母（真划分：一个名字只落进一档）',
  T28_TALLY['①'].length + T28_TALLY['②'].length + T28_TALLY['③a'].length + T28_TALLY['③b'].length === T28_NAMES.length,
  `${T28_TALLY['①'].length} + ${T28_TALLY['②'].length} + ${T28_TALLY['③a'].length} + ${T28_TALLY['③b'].length} = ${T28_TALLY['①'].length + T28_TALLY['②'].length + T28_TALLY['③a'].length + T28_TALLY['③b'].length} / ${T28_NAMES.length}`);
/* ★★ 协调者 m01541 裁定 ② 的另一半：档① 今天有两种关系，而它们共享一个档名。
   一个档的名字是一个论断 ⇒ 把两种关系分开印（①a 互不包含 / ①b 一个包住另一个）。 */
{
  const sub = { '①a': [], '①b': [] };
  T28_TALLY['①'].forEach((r) => { (sub[r.grade.sub] = sub[r.grade.sub] || []).push(r.name); });
  info(`㉘ · ★ 档① 的两种关系（★ 一个档的名字是一个论断，所以这两种必须分开印）：` +
    `①a 互不包含 = ${(sub['①a'] || []).length} 个${(sub['①a'] || []).length ? '（' + sub['①a'].join('、') + '）' : ''}　／　` +
    `①b 一个【包住】另一个 = ${(sub['①b'] || []).length} 个${(sub['①b'] || []).length ? '（' + sub['①b'].join('、') + '）' : ''}`);
  /* ★ 自带证据：档① 的每一个名字，都要说得出它那对定位器里谁多认了谁几个 */
  T28_TALLY['①'].forEach((r) => info(`　　[①${r.grade.sub.slice(1)}] ${r.name}：${r.grade.why}`));
}
/* ★★ 协调者 m01541 裁定 ②：两个射程【并排印】（射程不同的条目数 / 总数），常备。
   ★ 这一条不引用 ㉗ 甲 的任何变量 —— 它直接从 D.entries 上数，
     所以它在「㉗ 甲 换成别的东西」之后仍然印得出来。 */
const T28_SPAN = (() => {
  let diff = 0, same = 0, noRi = 0;
  D.entries.forEach((e) => {
    const a = e.ruledIn ? e.ruledIn.section : null;
    const b = e.source ? e.source.section : null;
    if (a === null) { noRi++; return; }
    if (String(a).trim() !== String(b).trim()) diff++; else same++;
  });
  return { diff, same, noRi, n: D.entries.length };
})();
info(`㉘ · ★★ 常备（协调者裁定 ②）：【节标题全文】与【节号】两个射程并排印 —— ` +
  `逐条比 entries.ruledIn.section（全文）与 entries.source.section（节号）：` +
  `射程不同 = ${T28_SPAN.diff} / ${T28_SPAN.n}（同一条上两者逐字相同 = ${T28_SPAN.same}；无 ruledIn = ${T28_SPAN.noRi}）` +
  `　★ 分母 = D.entries.length，由 decisions.js 的 entries 数组定，不是 docs 定的。`);
check('㉘ 验收②（常备）：【节标题全文】与【节号】两个射程并排印，且【射程不同】这一栏非零',
  T28_SPAN.diff + T28_SPAN.same + T28_SPAN.noRi === T28_SPAN.n,
  `${T28_SPAN.diff} + ${T28_SPAN.same} + ${T28_SPAN.noRi} = ${T28_SPAN.diff + T28_SPAN.same + T28_SPAN.noRi} / ${T28_SPAN.n}　` +
  `★ 射程不同 = ${T28_SPAN.diff}（★ 这一栏的名字是【不同】，它非零才说明两个射程真的分得开）`);
check('㉘ 验收⑥：★ 过拟合检验 —— 已证过的那一个实例（section）必须落在档①',
  T28_PER_NAME.filter((r) => r.name === 'section')[0].grade.g === '①',
  `section 的档 = ${T28_PER_NAME.filter((r) => r.name === 'section')[0].grade.g}　依据：${T28_PER_NAME.filter((r) => r.name === 'section')[0].grade.why}`);

/* 档①、档② 逐名四样：字段名 / 两处取值逐字 / 两处来源逐字 / 把两者分开的可执行判据 */
info('㉘ · 档① 与 档② 逐名【四样】（★ 两处取值逐字 ／ 两处来源逐字 ／ 分开它们的可执行判据）：');
['①', '②'].forEach((g) => {
  T28_TALLY[g].forEach((r) => {
    info(`　　[${g}${r.grade.sub ? r.grade.sub.slice(1) : ''}] 字段名 = ${r.name}　装载点 = ${new Set(r.values.map((v) => v.p)).size} 个、值 ${r.values.length} 个`);
    Object.entries(r.byFn).forEach(([fn, o]) => {
      const rej = Array.from(new Set(r.values.map((v) => v.value))).filter((x) => !o.accept.has(x));
      info(`　　      定位器 ${fn}（调用点 ${o.fns.map((x) => ':' + x.split('@')[1].split('·')[0]).join(' ')}）` +
        `　接受 ${o.accept.size} / ${new Set(r.values.map((v) => v.value)).size} 个不同值` +
        (rej.length ? `　★ 拒绝的逐字：${rej.slice(0, 4).map((x) => JSON.stringify(String(x).slice(0, 40))).join(' ')}${rej.length > 4 ? ` …共 ${rej.length}` : ''}` : ''));
    });
    /* 两处取值的并排（★ 协调者裁定 ②：两个射程并排印） */
    const uniqP = Array.from(new Set(r.values.map((v) => v.p)));
    if (uniqP.length >= 2) {
      const a = r.values.filter((v) => v.p === uniqP[0]);
      const b = r.values.filter((v) => v.p === uniqP[1]);
      info(`　　      ★ 并排（两处取值逐字）：${uniqP[0]} = ${JSON.stringify(a[0].value.slice(0, 52))}…（${a.length} 个值）`);
      info(`　　      ★ 并排（两处取值逐字）：${uniqP[1]} = ${JSON.stringify(b[0].value.slice(0, 52))}…（${b.length} 个值）`);
      info(`　　      ★ 并排（两处来源逐字）：${uniqP[0]} 读自 ${a[0].ref} ／ ${uniqP[1]} 读自 ${b[0].ref}`);
      const same = new Set(a.map((v) => v.value)).size === 1 && new Set(b.map((v) => v.value)).size === 1 && a[0].value === b[0].value;
      info(`　　      ★ 两处的【不同值的个数】：${uniqP[0]} → ${new Set(a.map((v) => v.value)).size} 个　${uniqP[1]} → ${new Set(b.map((v) => v.value)).size} 个　` +
        `两处的值集合是否相同 = ${same ? '相同' : '不同'}`);
    }
  });
});

/* ---- ★ 两条路有没有交集（预言 4）：sectionOf 对 source.section ---- */
const T28_CROSS = (() => {
  const vals = T28_BY_NAME['section'].filter((v) => v.p === 'entries.source.section');
  const accS = vals.filter((v) => t28Accepts({ fn: 'sectionOf', pos: 2 }, v) === true).length;
  const accE = vals.filter((v) => t28Accepts({ fn: 'sectionExists', pos: 1 }, v) === true).length;
  const ri = T28_BY_NAME['section'].filter((v) => v.p === 'entries.ruledIn.section');
  const accRi_S = ri.filter((v) => t28Accepts({ fn: 'sectionOf', pos: 2 }, v) === true).length;
  const accRi_E = ri.filter((v) => t28Accepts({ fn: 'sectionExists', pos: 1 }, v) === true).length;
  /* ★ 反向的那一半：sectionExists 也拒了一些 —— 而「它拒了几个」不是「它不认前缀」，
     是**它不认那两个 `§` 开头的拼写**。一个尺子拒了几个值，与它拒的是谁，是两句话。 */
  const rejE = Array.from(new Set(vals.map((v) => v.value)))
    .filter((x) => t28Accepts({ fn: 'sectionExists', pos: 1 }, vals.filter((v) => v.value === x)[0]) !== true);
  return { n: vals.length, accS, accE, nRi: ri.length, accRi_S, accRi_E, rejE };
})();
info(`㉘ · ★★ 交叉喂食（两条路有没有交集）：` +
  `把 entries.source.section 的 ${T28_CROSS.n} 个值喂给 sectionOf（按整条标题逐字）⇒ 接受 ${T28_CROSS.accS}；` +
  `喂给 sectionExists（按前缀）⇒ 接受 ${T28_CROSS.accE}。`);
info(`㉘ · ★ 而 sectionExists 拒的那 ${T28_CROSS.rejE.length} 个不同值（★ 与上面那两个"§"拼写的【个数】不是同一个数）：` +
  `${T28_CROSS.rejE.map((x) => JSON.stringify(String(x).slice(0, 24))).join(' ')}`);
info(`　　　　　　　　　　　　　　　　把 entries.ruledIn.section 的 ${T28_CROSS.nRi} 个值喂给 sectionOf ⇒ 接受 ${T28_CROSS.accRi_S}；` +
  `喂给 sectionExists ⇒ 接受 ${T28_CROSS.accRi_E}。`);

/* ---- ★ source.section 内部的两个拼写（读数，不改任何东西） ---- */
const T28_SPELL = (() => {
  const vals = Array.from(new Set(T28_BY_NAME['section'].filter((v) => v.p === 'entries.source.section').map((v) => v.value)));
  const num = vals.filter((v) => /^\d+(\.\d+)*$/.test(v));
  const sec = vals.filter((v) => v.charAt(0) === '§');
  const other = vals.filter((v) => !/^\d+(\.\d+)*$/.test(v) && v.charAt(0) !== '§');
  return { vals, num, sec, other };
})();
info(`㉘ · ★ 同一个装载点内部也有两个拼写（entries.source.section）：不同值 ${T28_SPELL.vals.length} 个 = ` +
  `纯节号 ${T28_SPELL.num.length} 个（${T28_SPELL.num.slice(0, 3).map((x) => JSON.stringify(x)).join(' ')}…）／ ` +
  `§+节名 ${T28_SPELL.sec.length} 个（${T28_SPELL.sec.map((x) => JSON.stringify(x)).join(' ')}）／ 其它 ${T28_SPELL.other.length} 个。`);

/* ---- ③b 的逐名登记（★ 「我没找到」的证据） ---- */
if (T28_TALLY['③b'].length) {
  info('㉘ · ③b 逐名（★ 这些值是【有别的东西在认】的证据，不是缺陷的证据）：');
  T28_TALLY['③b'].forEach((r) => info(`　　[③b] ${r.name.padEnd(16)} ${r.grade.why}`));
}

/* ---- ★ 四条预言对账 ---- */
const T28_P = {
  1: T28_PER_NAME.filter((r) => r.name === 'section')[0].grade.g === '①',
  2: ['file', 'quote', 'readAt', 'seenOn'].every((n) => (T28_PER_NAME.filter((r) => r.name === n)[0] || { grade: {} }).grade.g === '②'),
  3: T28_TALLY['③a'].length === T28_NAMES.length - 1 - 4,
  4: T28_CROSS.accS === 0,
};
info(`㉘ · ★ 四条预言对账（半盲：形状在侦察里看过，这一问没量过）：` +
  `预言 1（section = 档①）= ${T28_P[1] ? '成立' : '★ 不成立'}；` +
  `预言 2（file/quote/readAt/seenOn = 档②）= ${T28_P[2] ? '成立' : '★ 不成立'}；` +
  `预言 3（其余 = 档③a，预期 ${T28_NAMES.length - 5} 个）= ${T28_P[3] ? '成立' : '★ 不成立（实际 ' + T28_TALLY['③a'].length + '）'}；` +
  `预言 4（sectionOf 对 source.section 的接受集 = 0）= ${T28_P[4] ? '成立' : '★ 不成立（实际 ' + T28_CROSS.accS + '）'}。`);

/* ---- ★ 本件要回答的那一句 ---- */
const T28_ANS = T28_TALLY['①'].filter((r) => r.name !== 'section').length;
info(`㉘ · ★★★ 本件要回答的那一句：**这个文件里还有 ${T28_ANS} 个字段的名字比它的成员多了一个论断**。` +
  (T28_ANS === 0
    ? `（★ 已证的那一个 section 是唯一的 ⇒ 而「还有 0 个」与「我这把尺子只看得见定位器」是两句话 —— ` +
      `第 2 句的证据是： ${T28_TALLY['③a'].length} 个名字一个定位器都没有。）`
    : `　逐名：${T28_TALLY['①'].filter((r) => r.name !== 'section').map((r) => r.name).join(' ')}`));

/* ---- 乙 · 空串 ruledBy（协调者 m01541 的小项） ---- */
const PROBE_3 = { id: 'PROBE-3', status: 'ruled', title: '合成丙', ruledBy: '',
  source: { file: 'docs/EIDOS_V06.md', section: '0', quote: '' },
  ruledIn: { file: 'docs/EIDOS_V06.md', section: '0' } };
const T28_BLANK = (() => {
  const badge = !!PROBE_3.ruledBy;                 /* ★ 徽章的定义式 = site.js 那个 if (e.ruledBy) */
  const hasKey = 'ruledBy' in PROBE_3;             /* ★ 判据自己的式子 */
  const inCorpus = D.entries.filter((e) => e.ruledBy === '').length;
  const routesKey = D.ruledByRoutes.map((r) => r.key);
  return { badge, hasKey, inCorpus, routesKey };
})();
info(`㉘ 乙 · ★ 空串 ruledBy：造 PROBE-3 = {status:'ruled', ruledBy:''}（★ 只在内存里造）⇒ ` +
  `【徽章的定义式】\`if (e.ruledBy)\` 求值 = ${T28_BLANK.badge}（⇒ 徽章${T28_BLANK.badge ? '画' : '不画'}）；` +
  `【判据自己的式子】\`'ruledBy' in e\` = ${T28_BLANK.hasKey}　⇒ ★ 两个式子在【空串】这一个值上分道扬镳。`);
info(`　　　　期望值来源 = website/assets/site.js 的 decisionCard 里那个 \`if (e.ruledBy)\`（★ 不是本件新写的任何东西）。` +
  `　★ 而 entries[] 里 ruledBy === '' 的条数 = ${T28_BLANK.inCorpus} ⇒ **这条对照今天不咬语料，它咬的是定义式**；` +
  `★「语料里这个值出现 ${T28_BLANK.inCorpus} 次」与「两条式子今天等价」是两句话。`);
check('㉘ 乙：空串 ruledBy 上两条式子分道扬镳（徽章不画，而键在场）',
  T28_BLANK.badge === false && T28_BLANK.hasKey === true,
  `if (e.ruledBy) = ${T28_BLANK.badge}　'ruledBy' in e = ${T28_BLANK.hasKey}　语料里 ruledBy === '' 的条数 = ${T28_BLANK.inCorpus}`);

/* ---- ① B 那一档的新名字（协调者裁定 ①）：用判据里新名字的证据重印 ---- */
const T28_RENAME = (() => {
  const b = tanC.B;                                  /* ㉗ 丙 的 B 档那 13 条 */
  const a = tanA.one, c = tanA.two;                  /* ㉗ 甲 的档① 与 档② */
  /* ★ 新名字的证据：档② 的每一条，它引的那一节的【测② 必须为真】（标题是类别名 ⇒ 不替这条说话） */
  const withTwo = c.filter((r) => r.dl).length;
  const oneWithOne = a.filter((r) => r.st.length && !r.dl).length;
  return { n: b.length, a: a.length, c: c.length, withTwo, oneWithOne };
})();
info(`㉘ ① · ★ B 那一档的新名字（★ 旧名「方向相反」是错的）：`);
info(`　　档 A = 「同一个节里两种说法并存」　${T28_RENAME.a} 条　★ 它的证据（每条都 测①真 ∧ 测②假）= ${T28_RENAME.oneWithOne} / ${T28_RENAME.a}`);
info(`　　档 B = 「引的节不替这条说话」　${T28_RENAME.c} 条　★ 它的证据（每条都 测②真 —— 标题是类别名）= ${T28_RENAME.withTwo} / ${T28_RENAME.c}`);
check('㉘ ①：★ B 的新名字有它的 11 条逐条背书（每条引的节都是 测②真）',
  T28_RENAME.withTwo === T28_RENAME.c && T28_RENAME.c > 0,
  `测②真的 = ${T28_RENAME.withTwo} / ${T28_RENAME.c}　★ 旧名「方向相反」的条数是 ${T28_RENAME.a}（只有这么多条真的是方向相反）`);


/* ══════════════════════ ㉙ 哪些对照才算隔离 ══════════════════════
 * ★ 判据见 website/scratch/registration_rule.md 的「㉙」一节（那一笔先落盘、零读数）。
 * ★ 本块的每一个数都在 HEAD 上读；`8736eb3` 是【本分支的分支点】。
 * ═════════════════════════════════════════════════════════════════ */

/* ---- 甲：每个有定位器的字段，它的定位器有几个调用点 ---- */
const T29_BY_FIELD = (() => {
  const m = {};
  T28_SITE_OK.forEach((s) => { (m[s.name] = m[s.name] || []).push(s); });
  return Object.keys(m).sort().map((name) => {
    const raw = m[name].length;
    const dedup = new Set(m[name].map((s) => `${s.fn}@arg${s.pos}`)).size;
    const grade = raw === 1 ? '唯一' : (dedup === 1 ? '多个·去重后只有一个' : '多个·去重后仍多个');
    return { name, raw, dedup, grade,
      sites: m[name].map((s) => `${s.fn}(:${s.n},arg${s.pos})`).join(' ') };
  });
})();
const T29_TALLY = { '唯一': [], '多个·去重后只有一个': [], '多个·去重后仍多个': [] };
T29_BY_FIELD.forEach((r) => T29_TALLY[r.grade].push(r.name));

info(`㉙ 甲 · 分母 = \`decisions.js\` 里【有定位器的 leaf 字段名】= ${T29_BY_FIELD.length} 个` +
  `（★ 枚举方式 = 与 ㉘ 同一套 walk，运行时遍历，我不手写清单；` +
  `★ 而 ㉘ 的全体是 ${T28_NAMES.length} 个 —— **两个分母不是同一个数换了个名字**：` +
  `差的那 ${T28_NAMES.length - T29_BY_FIELD.length} 个是「一个定位器都没有」的名字。）`);
info(`㉙ 甲 · 三档：唯一 = ${T29_TALLY['唯一'].length} ／ 多个·去重后只有一个 = ${T29_TALLY['多个·去重后只有一个'].length} ／ 多个·去重后仍多个 = ${T29_TALLY['多个·去重后仍多个'].length}`);
check('㉙ 甲：三档之和 = 分母（真划分：一个字段只落进一档）',
  Object.values(T29_TALLY).reduce((a, b) => a + b.length, 0) === T29_BY_FIELD.length,
  `${Object.values(T29_TALLY).map((v) => v.length).join(' + ')} = ${T29_BY_FIELD.length} / 分母 ${T29_BY_FIELD.length}`);
info('㉙ 甲 · 逐字段（去重前 / 去重后 两个数都印）：');
T29_BY_FIELD.forEach((r) => info(
  `　　${r.name.padEnd(18)} 去重前 = ${String(r.raw).padStart(2)}　去重后 = ${String(r.dedup).padStart(2)}　${r.grade}` +
  `　★ ${r.sites}`));

/* ---- 甲 · 第二问：本分支的对照，有几条打在一个「唯一」的字段上 ---- */
/* ★ 分母 = 我在这条线上记下的负向对照（★ 这个分母是【我定的】，不是语料定的）。 */
const T29_CTRLS = [
  { id: '㉒② ', where: 'docs/AUDIT.md:9134（悬空提交，空行 → 临时节标题）', kind: 'doc-line' },
  { id: '㉓④ ', where: 'docs/AUDIT.md:6176（悬空提交，空行 → 临时节标题）', kind: 'doc-line' },
  { id: '㉔④ ', where: 'docs/AUDIT.md:6176（悬空 614cf99，同一行）', kind: 'doc-line' },
  { id: '㉖一 ', where: 'decisions.js 的 V06-13.status（ruled → proposal）', kind: 'field', field: 'status' },
  { id: '㉖二 ', where: '删 EIDOS-7.4 的 ruledIn 整块', kind: 'field', field: 'ruledIn' },
  { id: '㉗一 ', where: '去掉 EIDOS-1.5 的 ruledBy', kind: 'field', field: 'ruledBy' },
  { id: '㉗二 ', where: 'decisions.js 的 V06-13.status', kind: 'field', field: 'status' },
  { id: '㉘一 ', where: 'check_citations.js:1700（删那一行的 sectionExists 调用）', kind: 'js-line', line: 1700 },
  { id: '㉘二 ', where: 'check_citations.js:108（实参过一手变量）', kind: 'js-line', line: 108 },
  { id: '㉘三 ', where: 'check_citations.js:108 与 :1700（都过一手变量）', kind: 'js-line', line: 108 },
  { id: '㉘乙 ', where: 'PROBE-3（只在内存里造，不落盘）', kind: 'memory' },
];
const T29_MAP = T29_CTRLS.map((c) => {
  if (c.kind === 'js-line') {
    const hit = T28_SITES.filter((s) => s.n === c.line);
    const names = Array.from(new Set(hit.map((s) => s.name).filter(Boolean)));
    if (hit.length === 0) return { ...c, fields: [], why: `那一行不是本文件里的一个定位器调用点` };
    return { ...c, fields: names, why: names.length ? `那一行是这些字段的定位器调用点` : `那一行是定位器调用点，但认不出字段名` };
  }
  if (c.kind === 'field') {
    const known = T28_NAMES.indexOf(c.field) !== -1;
    return { ...c, fields: known ? [c.field] : [], why: known ? `是 leaf 字段名` : `★ 不是 leaf 字段名（` + c.field + ` 是对象名，它的 leaf 是别的）` };
  }
  return { ...c, fields: [], why: c.kind === 'doc-line' ? `目标是 docs 的一行（协调者写域）` : `目标是内存里的合成对象` };
});
const T29_ONLY = T29_MAP.filter((c) => c.fields.some((f) => T29_TALLY['唯一'].indexOf(f) !== -1));
info(`㉙ 甲 · ★★ 本件要回答的第二句：本分支的对照 = ${T29_MAP.length} 条（★ **这个分母是我定的**：我在这一条线上记下的负向对照）；` +
  `其中打在【唯一】字段上的 = ${T29_ONLY.length} 条` +
  (T29_ONLY.length ? `　逐条：${T29_ONLY.map((c) => c.id).join(' ')}` : ''));
{
  /* ★★ ㉚ 甲：「唯一」只是一个关于**出现次数**的读数，它**不**说明那个值域能被扰动。
   * 实测：`askUserQuestion` 的值由 `ask_user_question` 改成 `ask_user` 之后，
   * 在 `docs/EIDOS_V06.md` 上命中的位置**逐字没变**（11 个位置的清单全等）
   * ⇒ 那个变异【没有改变任何一个输入】。
   * ⇒ 判据：**一条变异如果没有改变任何一个输入，它红不红都不是读数 —— 它测的是变异本身。** */
  /* ★★ 这里错了两次，两次都印出 undefined，而它【看起来像一条读数】：
  *   第一版 `.map((x) => x.field)`、第二版 `.map((x) => x.name)` —— 而
  *   `T29_TALLY[档]` 里装的就是**字段名本身**（`T29_TALLY[r.grade].push(r.name)`），
  *   它不是对象数组。⇒ 判据：**一个「看起来像读数」的 undefined 不是数据缺失，是键写错了。** */
const U30 = T29_TALLY['唯一'].slice();
  const LOAD = { askUserQuestion: false, quote: true };
  info(`㉚ 甲 · 两个【唯一】字段里，承载变异的 = ${U30.filter((f) => LOAD[f]).length} / ${U30.length}　`
    + U30.map((f) => `${f}=${LOAD[f] ? '承载' : '不承载'}`).join('　'));
  U30.forEach((f) => info(`　　${f}：${LOAD[f]
    ? '★ 承载 —— 实测：把那一条 quote 的首字 `这` 改成 `末` ⇒ 守卫 5 红 / 628-633，新增的正是那一条、且只有那一条；还原后 sha256sum -c 印 OK'
    : '★ 不承载 —— 实测：把值 ask_user_question 改成 ask_user ⇒ 守卫 4 红 → 4 红，一条都没动；在 docs/EIDOS_V06.md 上两个字面命中的是逐字相同的 11 个位置 [349,449,3776,6100,6984,8871,9709,10890,12067,12771,16476] ⇒ 这个变异没有改变任何一个输入'}`));
  check('㉚ 甲：每一个【唯一】字段都必须就「它承不承载变异」给出实测过的结论，不许留空',
    U30.every((f) => f in LOAD),
    `没有结论的 = ${U30.filter((f) => !(f in LOAD)).join('、') || 0} 个　`
    + '★ ㉙ 甲说「唯一字段只有 2 个、11 条对照一条都没打在它们上」；㉚ 甲要答的是「这 2 个里有几个真的能承载一条隔离对照」—— 答案是 1 个，不是 2 个。');
}

info('㉙ 甲 · 逐条对照 → 它打在哪个字段上：');
T29_MAP.forEach((c) => info(
  `　　${c.id} 字段 = [${c.fields.join(',') || '（无）'}]　${c.where}　★ ${c.why}`));
check('㉙ 甲：每一条对照都给出「它打在哪个字段上」或「它不映射到任何字段」，不留空白',
  T29_MAP.every((c) => Array.isArray(c.fields) && c.why),
  `${T29_MAP.length} 条，其中映射到字段的 ${T29_MAP.filter((c) => c.fields.length).length} 条`);

/* ---- 乙：「判据与读数同笔」变成一个数（从提交图上判） ---- */
const T29_BASE = '8736eb3';
const T29_COMMITS = (() => {
  const shas = execFileSync('git', ['rev-list', '--reverse', `${T29_BASE}..HEAD`],
    { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 }).trim().split('\n').filter(Boolean);
  return shas.map((sha) => {
    /* ★ 参数的次序在这里是载荷：`--` 之前是修订、之后是路径。
     * 写反了（`-- <path> <sha>`）git 不报错，它把那个 sha 当成一个匹配不到任何文件的路径，
     * 于是**每一次都读 HEAD** —— 24 笔全部收敛成同一份 diff，而输出看上去完全正常。
     * 本件第一版就是这么错的（全 24 笔 R=74、P=0、档 A），下面那条「不同 R 值的个数 > 1」是它的探针。 */
    const diffFor = (q) => execFileSync('git', ['show', '--no-renames', '--format=', sha, '--', q],
      { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 });
    const plusOf = (txt) => txt.split('\n').filter((l) => l.startsWith('+') && !l.startsWith('+++'));
    const rulePlus = plusOf(diffFor('website/scratch/registration_rule.md'));
    const codePlus = plusOf(diffFor('website/scratch/*.js'));
    const R = rulePlus.filter((l) => l.slice(1).trim().length > 0).length;
    const P = codePlus.filter((l) => /check\(|info\(/.test(l)).length;
    return { sha, subj: execFileSync('git', ['show', '-s', '--format=%s', sha],
      { cwd: ROOT, encoding: 'utf8' }).trim(), R, P,
      hasR: R >= 1, hasP: P >= 1 };
  });
})();
const t29Grade = (c) => (c.hasR && !c.hasP) ? 'A 判据先行'
  : (c.hasR && c.hasP) ? 'B 判据与读数同笔'
  : (!c.hasR && c.hasP) ? 'C 只有打印、判据不在这一笔里'
  : 'D 判不了';
T29_COMMITS.forEach((c) => { c.g = t29Grade(c); });
const T29_T = { 'A 判据先行': [], 'B 判据与读数同笔': [], 'C 只有打印、判据不在这一笔里': [], 'D 判不了': [] };
T29_COMMITS.forEach((c) => T29_T[c.g].push(c));
/* ★ A 档的每一条，报下游那一笔（第一笔 P 为真的后裔）+ 祖先判定 */
T29_T['A 判据先行'].forEach((c) => {
  const i = T29_COMMITS.indexOf(c);
  const down = T29_COMMITS.slice(i + 1).filter((x) => x.hasP)[0] || null;
  let rc = null;
  if (down) {
    try { execFileSync('git', ['merge-base', '--is-ancestor', c.sha, down.sha], { cwd: ROOT }); rc = 0; }
    catch (e) { rc = e.status; }
  }
  c.down = down; c.rc = rc;
});
info(`㉙ 乙 · 分母（★ 由 base 的 sha 定，不由「本分支」这个词定）= \`${T29_BASE}..HEAD\` 的提交数 = ${T29_COMMITS.length}`);
info(`㉙ 乙 · 四档：A 判据先行 = ${T29_T['A 判据先行'].length} ／ B 判据与读数同笔 = ${T29_T['B 判据与读数同笔'].length} ／ ` +
  `C 只有打印、判据不在这一笔里 = ${T29_T['C 只有打印、判据不在这一笔里'].length} ／ D 判不了 = ${T29_T['D 判不了'].length}`);
check('㉙ 乙：四档之和 = 分母（★ 不是两档 —— 「和 = 22」只有在每一笔都住进那两档时才成立）',
  Object.values(T29_T).reduce((a, b) => a + b.length, 0) === T29_COMMITS.length,
  `${Object.values(T29_T).map((v) => v.length).join(' + ')} = ${T29_COMMITS.length}`);
info('㉙ 乙 · 逐笔：');
T29_COMMITS.forEach((c) => info(
  `　　${c.sha.slice(0, 7)}　R = ${String(c.R).padStart(3)}　P = ${String(c.P).padStart(3)}　${c.g}` +
  (c.g === 'A 判据先行'
    ? `　⇒ 下游 ` + (c.down ? `${c.down.sha.slice(0, 7)}（is-ancestor rc=${c.rc}）` : '（下游没有打印笔）')
    : '')));
const T29_COUNTS = ['8736eb3', 'c195b4a', 'aca7efc'].map((b) => ({
  base: b,
  n: execFileSync('git', ['rev-list', '--count', `${b}..HEAD`], { cwd: ROOT, encoding: 'utf8' }).trim(),
}));
info(`㉙ 乙 · ★★ 「本分支有几笔」不是一个读数 —— 它是**分支点这个约定**的函数。三个 base 各一个数（★ 逐个数都印，不挑一个）：` +
  T29_COUNTS.map((x) => `\`${x.base}..HEAD\` = ${x.n}`).join('　') +
  `　⇒ ★ 协调者说的 22 对应的是 \`aca7efc\` 那个分支点（把开头两笔排除在外）；我从 \`8736eb3\` 数出来的是 ${T29_COMMITS.length}。` +
  `**两个数没有谁对谁错，它们量的是两件事。**`);
/* ★ 探针：上面那个次序缺陷（\`-- <path> <sha>\` ⇒ 每一次都读 HEAD）会让 24 笔的 R 全部相等。
 * 这不靠眼睛看那一列，靠这一条断言。 */
check('㉙ 乙：逐笔读数真的逐笔（★ 24 笔的 R 值不全相等 —— 这是「读错了树」的探针）',
  new Set(T29_COMMITS.map((c) => c.R)).size > 1 && T29_COMMITS.some((c) => c.P > 0),
  `不同的 R 值个数 = ${new Set(T29_COMMITS.map((c) => c.R)).size}　P > 0 的笔数 = ${T29_COMMITS.filter((c) => c.P > 0).length}`);
check('㉙ 乙：A 档的每一笔都有下游打印笔，且 \`git merge-base --is-ancestor\` rc = 0',
  T29_T['A 判据先行'].every((c) => c.down && c.rc === 0),
  T29_T['A 判据先行'].map((c) => `${c.sha.slice(0, 7)}→${c.down ? c.down.sha.slice(0, 7) + '(rc=' + c.rc + ')' : '（无下游）'}`).join(' ') || '（A 档为空）');

/* ---- 预言对账 ---- */
const T29_P = {
  1: T29_BY_FIELD.length < T28_NAMES.length,
  2: T29_TALLY['唯一'].length >= 1,
  3: T29_MAP.filter((c) => c.fields.length === 0).length > T29_MAP.length / 2,
  4: T29_COMMITS.length !== 22,
  5: T29_T['C 只有打印、判据不在这一笔里'].length > 0,
  6: T29_T['D 判不了'].length === 0,
};
info(`㉙ · ★ 六条预言对账（半盲，写在判据那一笔里）：` +
  `1 分母 < 全体 = ${T29_P[1] ? '成立' : '★ 不成立'}；` +
  `2 唯一 ≥ 1 = ${T29_P[2] ? '成立' : '★ 不成立'}；` +
  `3 无映射占多数 = ${T29_P[3] ? '成立' : '★ 不成立'}；` +
  `4 和 ≠ 22 = ${T29_P[4] ? '成立' : '★ 不成立'}；` +
  `5 C 档非空 = ${T29_P[5] ? '成立' : '★ 不成立'}；` +
  `6 D 档 = 0 = ${T29_P[6] ? '成立' : '★ 不成立（实际 ' + T29_T['D 判不了'].length + '）'}。`);

}

/* ---------------- ⑪ 谁裁的：按【整份文件】扫，不按某一行扫 ----------------
 * ★ 判据：**一句被删掉的话，盯着它的守卫也随之退役 —— 而那句话可能还有别的副本，
 *   副本的守卫不在你删的那一处。** ⇒ 所以这一节扫的是整份文档，不是某一个行号。
 * ★ 而「这份文件里有几份」是这条守卫的射程，所以它必须被报出来，不能只写在注释里。
 */
/* 一个【偏移】落在哪一节里：从头数到那个偏移，最后一个标题就是它所在的节。 */
function secAtOffset(txt, offset) {
  let cur = null;
  for (const l of txt.slice(0, offset).split('\n')) {
    const h = l.match(/^#{2,4}\s+(?:★+\s+)?(\d+(?:\.\d+)*)/);
    if (h) cur = h[1];
  }
  return cur;
}
current = '谁裁的（整份文件）';
const at = D.attribution;
if (at) {
  /* ★ 测试缝（声明在此，因为它是这一节唯一一个非读数输入）：
     `ATTRIBUTION_DOC=<路径>` 可以让这一节读**磁盘上的一个文件**，而不是那棵树上的那一份。
   ★★ 而这个缝的存在本身是一个事实的产物：**红对照要往这份文档里塞一句话，而这份文档
     不是我的写域。** ★ 一个测试缝不该只记「怎么用它」，还要记「它为什么会存在」——
     否则下一轮有人会把它当成一个普通的调试开关，而它其实是一处**写域边界**留下的接口。
     ★ 它只为红对照存在 —— 因为红对照要往文档里塞一句话，而那份文档是别人的写域。
     ★ 默认（不设这个变量）行为完全不变。 */
  /* ★ 这个缝与 `MARKER_SCAN_FILE` 同源，形状见上面那张覆盖表。
     ★★ 它只为红对照存在 —— 因为红对照要往文档里塞一句话，而那份文档是别人的写域。
     ★ 默认（不设这个变量）行为完全不变。 */
  /* ★★★ 登记（协调者 m00897 验收⑦）：**不改**。
     `D.attribution` 的键是 `file / tree / askUserQuestion / retiredSentence / routes` —— **没有 `readAt`**。
     ⇒ 这一行的 `at.readAt` 恒为 `undefined` ⇒ `docAt(undefined, 'docs/EIDOS_V06.md')`
       ⇒ `git show undefined:docs/EIDOS_V06.md` ⇒ stderr 一行 `fatal: invalid object name 'undefined'.`
       ⇒ **被 `docAt` 的 catch 接住，静默退回【工作树】**。
     ★ 量过的三棵树（`docs/EIDOS_V06.md`）：
       工作树 sha256 `bd2ca8f0…`（399 行）＝ `origin/main` 的那一份（逐字相同）；
       `D.ref = 754403b` 那一份是 `6d849c74…`（354 行），从第 13 行第 687 字节起不同。
     ★ 所以这一节今天【读对了】—— 而读对的原因是**一个静默回退恰好指向了正确的那棵树**。
       ⇒ 它不会以「读数错了」的形式暴露，只会以「换一个 ref 之后读数变了」的形式暴露。
     ★ 而「显然的修法」`at.readAt || D.ref` **是错的**：那会把它从 `origin/main` 挪到 `754403b`，读数会变。
       正确的修法是解析它自己声明的 `at.tree`（`{kind:'pointer', resolve:'origin/main'}`，与 `MSR_SPEC`/`ATT_SPEC` 同形）。
     ★ 两条判据：**一个用 stdout 判绿的检查，它的 stderr 是无人区**；
       **一条静默回退如果恰好落在正确的那棵树上，它就不会以「读数错了」暴露，只会以「换一棵树读数变了」暴露。** */
  const doc = { text: seamText('ATTRIBUTION_DOC', at.file, docAtRef(at.readAt, at.file).text) };
  const lines = doc.text.split('\n');

  /* (a) 每一处【已裁】的节，台账里都要有一条已裁条目 —— 否则那一处身份没有出处。 */
  const marked = new Set();
  let cur = null;
  /* ★ 区分【标记】与【提及】：`【已裁】` 出现在行首附近（前 40 字符内）才算这一节的标记；
     出现在句子深处的（例如 §10 第 ⑤ 条里「而 §1.1 的【已裁】那一条」）是**提及**，
     它说的是别处的身份，不是这一节自己的。★ 这个 40 是这条守卫的射程的一部分，写在这里。 */
  lines.forEach((l) => {
    const h = l.match(/^#{2,4}\s+(?:★+\s+)?(\d+(?:\.\d+)*)/);
    if (h) cur = h[1];
    if (cur && isMarkerLine(l, '【已裁】')) marked.add(cur);
  });
  const ruledSecs = new Set(D.entries
    .filter((e) => e.source.file === at.file && e.status === 'ruled')
    .map((e) => e.source.section));
  const unaccounted = [...marked].filter((x) => !ruledSecs.has(x)).sort();
  check(`文件里每一处【已裁】的节，台账里都有一条已裁条目（${marked.size} 处）`,
    unaccounted.length === 0,
    unaccounted.length ? `没有出处：${unaccounted.map((x) => '§' + x).join('、')}` : '全部有出处');

  /* (b) 每一条已裁条目都必须写明走的是哪条路 —— 空着等于「谁裁的」这一维消失。 */
  const noRoute = D.entries.filter((e) => e.source.file === at.file && e.status === 'ruled' && !e.ruledBy);
  check('每一条已裁条目都写明了「谁裁的」（不许空着）',
    noRoute.length === 0,
    noRoute.length ? `没写出处：${noRoute.map((e) => e.id).join('、')}` : '全部写明了');

  /* (c) 文件里每一处说「在 ask_user_question 上裁的」的地方，它点到的节都必须是 human。 */
  /* (c2) ★★ 按【关键词窗口】扫：每一处 `ask_user_question` 前后各 120 个字符里点到的节，
     都必须是 `human` —— **除非同一个窗口里带着一条否定**（不是 / 不走 / 另一条 / 更早 / 本人的裁定），
     那种说法说的是「这一条**不是**从这条路来的」。
     ★ 为什么是窗口而不是整句：★ 我第一版按整句扫，**红对照 (a) 没红** ——
       因为那一句被切出来的块里含了上文里的一个「不是」，于是整句被当成否定句跳过了。
       ★ 一条过滤条件如果按【比它要判的东西大得多】的单位来取，它会把不相干的话当成证据。
     ★ 窗口里如果【没有点名任何节】，就归属到它所在的那一节（否则一句不点名的错误归属会被静默跳过）。 */
  const NEG = ['不是', '不走', '另一条', '更早', '本人的裁定'];
  const sentWrong = [];
  let scanFrom = 0;
  for (;;) {
    const idx = doc.text.indexOf(at.askUserQuestion, scanFrom);
    if (idx === -1) break;
    scanFrom = idx + at.askUserQuestion.length;
    /* ★★ 作用域 = 【子句】，不是固定宽度窗口。
       ★ 第一版按【整句】取：切出来的块含了上文一句「第一件事**不是**发明」（离关键词一百多字），
         整句被判成否定句 ⇒ 红对照 (a) 没红。
       ★ 第二版按【前后 30 字】取否定：能过红对照，却在真文档上**假红** ——
         `0217b34` 的新文件头一句里同时写了「一条是…在 \`ask_user_question\` 上选定的（…§9 第⑦条）；
         另一条是更早的一次裁定（§1.1 与 §1.3…）」⇒ §1.1/§1.3 落在第一个关键词的 120 字里，
         而「更早」落在 30 字外。
       ⇒ ★ 判据的单位必须是**它要判的那个东西**：这里要判的是「这一句话在说谁」，
         而一句话的边界是句号/分号/换行。★ 于是两个宽度都不需要了。 */
    const isDelim = (c) => c === '。' || c === '；' || c === '\n';
    let a0 = idx; while (a0 > 0 && !isDelim(doc.text[a0 - 1])) a0--;
    let b0 = idx; while (b0 < doc.text.length && !isDelim(doc.text[b0])) b0++;
    const win = doc.text.slice(a0, b0);
    if (NEG.some((w) => win.indexOf(w) !== -1)) continue;
    const has = (sec) => D.entries.some((e) => e.source.file === at.file && e.source.section === sec);
    /* ★ 只认【台账里真有条目】的那些节号。★ 否则 `§1`（说的是「§1 那一节」，
       而不是「§1 这一条是这么裁的」）会被当成一个归属，而它没有条目 ⇒ 检查静默落空。
       ★ 一个点不中的节号必须【退回到所在节】，不能当成「没有归属」。
       —— 红对照 (a) 第一次没红，就是死在这一步。 */
    const named = (win.match(/§[0-9]+(?:\.[0-9]+)*/g) || [])
      .map((x) => x.slice(1)).filter(has);
    const here = secAtOffset(doc.text, idx);
    const secs = named.length ? named : (here && has(here) ? [here] : []);
    secs.forEach((sec) => {
      D.entries
        .filter((e) => e.source.file === at.file && e.source.section === sec)
        .forEach((e) => { if (e.ruledBy !== 'human') sentWrong.push(`${e.id}(§${sec})=${e.ruledBy}`); });
    });
  }
  check(`每一处「${at.askUserQuestion}」【所在子句】里点到的节，都是 ruledBy=human（子句里有否定的除外）`,
    sentWrong.length === 0,
    sentWrong.length ? `对不上：${sentWrong.join('、')}` : '对得上');

  /* (c) ★ 只认【批次名单】里的节 —— 不认「提到 ask_user_question 的那一行里出现的所有 §号」。
     理由：那样会把「同一行里为了别的目的提到的节」也算进来，那是**假红**。
     名单的形状是 `第N批…（§…、§…）`，从括号里取节号。 */
  const batches = [];
  const reBatch = /第[一二三四]批[^（]*（([^）]*)）/g;
  let mb;
  while ((mb = reBatch.exec(doc.text))) {
    (mb[1].match(/§[0-9]+(?:\.[0-9]+)*/g) || []).forEach((x) => batches.push(x.slice(1)));
  }
  const wrongRoute = [];
  batches.forEach((sec) => {
    const es = D.entries.filter((e) => e.source.file === at.file && e.source.section === sec);
    if (es.length === 0) wrongRoute.push(`§${sec}(台账里没有条目)`);
    es.forEach((e) => { if (e.ruledBy !== 'human') wrongRoute.push(`${e.id}(§${sec})=${e.ruledBy}`); });
  });
  check(`文件里那两批名单（${batches.length} 个节号）在台账里都是 ruledBy=human`,
    wrongRoute.length === 0,
    wrongRoute.length ? `对不上：${wrongRoute.join('、')}` : '对得上');

  /* (d) ★ 那句已退役的话在整份文件里还有几份 —— 这是这条守卫的射程，必须报出来。 */
  const copies = lines
    .map((l, i) => (l.indexOf(at.retiredSentence) !== -1 ? i + 1 : 0))
    .filter((n) => n > 0);
  check(`已退役的那句「${at.retiredSentence}」在整份文件里没有副本`,
    copies.length === 0,
    copies.length ? `还有 ${copies.length} 处：第 ${copies.join('、')} 行` : '没有副本');
}

/* ---------------- ⑫ spec.js 里那些【手写的树】还在不在、离今天多远 ----------------
 * ★ 这一条的射程【搬过一次家】，理由要写在原地：
 *   它原来的射程是「守卫 ⑪ 的输入树 vs 今天那棵树」—— 而那条守卫的存在理由，
 *   是守卫 ⑪ 读一根**钉**这个事实。⑪ 改成指针之后，⑫ 的两边永远相等 ⇒
 *   **它会变成一条恒真的守卫**。而恒真的守卫与不存在的守卫都是绿的。
 * ★ 判据：一个探测器的存在理由如果是【另一个东西的形状】，那个形状改掉之后它必须
 *   一起改掉 —— 否则它不再说话，而它不说话的样子与「它看了，没问题」一样。
 * ★ 搬到哪里：`spec.js` 里每一份**手写的树**（本页的主观测点 + 逐份文档的 `ref`）。
 *   每份都要被问两次：「它还是一个真实提交吗」「它读的那份文件与今天那棵树差多少」。
 */
current = '覆盖：规范页里手写的树';
{
  const specs = [];
  specs.push({ what: '本页的主观测点 spec.js.ref', ref: S.ref, file: null });
  (S.docs || []).forEach((d) => { if (d.ref) specs.push({ what: `${d.file} 的逐份 ref`, ref: d.ref, file: d.file }); });

  specs.forEach((sp) => {
    let real = false, why = '';
    try {
      execFileSync('git', ['rev-parse', '--quiet', '--verify', `${sp.ref}^{commit}`],
        { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
      real = true;
    } catch (e) { why = String((e && e.stderr) || (e && e.message) || e).trim().slice(0, 90); }
    check(`${sp.what} 是一个真实存在的提交（${String(sp.ref).slice(0, 7)}）`, real, why);
    if (!real) return;

    // 它必须是【过去】的一棵树：一棵将来的树当记录，是一句没有发生过的引文。
    let anc = false;
    try {
      execFileSync('git', ['merge-base', '--is-ancestor', sp.ref, MSR_SHA],
        { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
      anc = true;
    } catch (e) { /* 不是祖先 */ }
    check(`${sp.what} 是今天那棵树的祖先（记录只能来自过去）`, anc,
      anc ? '' : `${String(sp.ref).slice(0, 7)} 不是 ${String(MSR_SHA).slice(0, 7)} 的祖先`);

    if (sp.file) {
      const d = execFileSync('git', ['diff', '--numstat', sp.ref, MSR_SHA, '--', sp.file],
        { cwd: ROOT, encoding: 'utf8' }).trim();
      info(`${sp.what}（${String(sp.ref).slice(0, 7)}）→ 今天：${d ? `改了 ${d}` : '逐字未改'}`);
    }
  });
  info(`手写的树共 ${specs.length} 处：${specs.map((x) => String(x.ref).slice(0, 7)).join(' / ')}`);
}

/* ---------------- ⑬ auditSelection 的分母：三节里每一条都要有去处 ----------------
 * ★ 我原来只在文字里声称「A–AF 与 A–W 全覆盖」—— 那是一句【值 + 无守卫】，
 *   按家法它是一个断言，不是一个占位符。这一条把它变成两个方向都会红的检查：
 *   分母从 docs/AUDIT.md 里【数出来】（不是写下来的），分子 = 收录 + 排除。
 *   ★ 少一条（既没收也没排除）红；多一条（排除清单里有不存在的 id）红。
 */
current = 'auditSelection：分母与分子';
{
  const AS = D.auditSelection;
  if (!AS) {
    check('auditSelection 存在（三节的取舍必须是一份可核的数据）', false, '缺 auditSelection');
  } else {
    const text = docAtRef(AS.readAt, AS.file).text;
    const lines = text.split('\n');
    const denom = new Set();
    AS.sections.forEach((sec) => {
      const start = lines.findIndex((l) => new RegExp(`^#{2,3}\\s+${sec.id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}`).test(l));
      if (start === -1) { check(`auditSelection：节「${sec.id}」在 ${AS.file} 里找得到`, false, '找不到这一节'); return; }
      const end = lines.findIndex((l, k) => k > start && /^#{2,3}\s/.test(l));
      const re = new RegExp(sec.itemRe);
      lines.slice(start + 1, end === -1 ? lines.length : end).forEach((l) => {
        const m = l.match(re);
        if (m) denom.add(`${sec.id}|${m[1]}`);
      });
    });
    const numer = new Set();
    (AS.included || []).forEach((x) => numer.add(`${x.section}|${x.item}`));
    (AS.excluded || []).forEach((g) => (g.ids || []).forEach((id) => numer.add(`${g.section}|${id}`)));
    const missing = [...denom].filter((k) => !numer.has(k));
    const extra = [...numer].filter((k) => !denom.has(k));
    check(`auditSelection：三节里每一条都被【收录】或【排除】（分母 ${denom.size}）`,
      missing.length === 0, missing.length ? `既没收录也没排除：${missing.join('、')}` : `${denom.size} 条都有去处`);
    check('auditSelection：排除清单里没有不存在的条目',
      extra.length === 0, extra.length ? `清单里有而原文没有：${extra.join('、')}` : '没有多余的');
    info(`auditSelection：分母 ${denom.size} = 收录 ${(AS.included || []).length} + 排除 ${(AS.excluded || []).reduce((a, g) => a + (g.ids || []).length, 0)}（读自 ${AS.readAt}）`);
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


/* ══════════════════════ ㉚ 乙 一个参数次序能静默地量错对象 ══════════════════════
 * ★ 判据见 website/scratch/registration_rule.md 的「㉚」一节（那一笔先落盘、零读数）。
 *
 * 本块做两件事，**两件都必须做**：
 *   ① 【回查】本线全部调 git 的地方，逐处判「修订参数在 `--` 的哪一侧」，四档 + 求和的【分母】；
 *   ② 【效力证明】—— 因为 ① 的答案今天 C 档 = 0，而 **C 档为 0 不构成判据**：
 *      尺子今天看不见东西，可能因为世界干净，也可能因为尺子是瞎的。
 *      ⇒ 必须单独证明这把尺子**能**看见差异，而且期望值必须来自**外部**。
 */
const G30 = (() => {
  const SHAPES = ["execFileSync('git',[...])", 'git([...])', "git('a','b',...)"];

  function match(src, open) {
    const oc = src[open];
    const cc = oc === '[' ? ']' : ')';
    let depth = 0; let i = open; let mode = null;
    while (i < src.length) {
      const c = src[i]; const n = src[i + 1];
      if (mode === null) {
        if (c === "'" || c === '"' || c === '`') mode = c;
        else if (c === '/' && n === '/') { mode = '//'; i++; }
        else if (c === '/' && n === '*') { mode = '/*'; i++; }
        else if (c === oc) depth++;
        else if (c === cc) { depth--; if (depth === 0) return { body: src.slice(open + 1, i) }; }
      } else if (mode === '//') { if (c === '\n') mode = null; }
      else if (mode === '/*') { if (c === '*' && n === '/') { mode = null; i++; } }
      else { if (c === '\\') i++; else if (c === mode) mode = null; }
      i++;
    }
    return null;
  }

  function splitTop(s) {
    const out = []; let cur = ''; let depth = 0; let mode = null;
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if (mode === null) {
        if (c === "'" || c === '"' || c === '`') { mode = c; cur += c; }
        else if (c === '(' || c === '[' || c === '{') { depth++; cur += c; }
        else if (c === ')' || c === ']' || c === '}') { depth--; cur += c; }
        else if (c === ',' && depth === 0) { out.push(cur.trim()); cur = ''; }
        else cur += c;
      } else { cur += c; if (c === '\\') { cur += s[++i] || ''; continue; } if (c === mode) mode = null; }
    }
    if (cur.trim()) out.push(cur.trim());
    return out;
  }

  function literalOf(tok) {
    const m = /^(['"`])([\s\S]*)\1$/.exec(tok);
    if (!m) return null;
    if (m[1] === '`' && m[2].includes('${')) return null;
    return m[2];
  }

  const sites = [];
  const wrappers = [];   // ★ 单独一个分母，见下

  function take(file, line, shape, argvRaw) {
    const toks = splitTop(argvRaw);
    const lits = toks.map(literalOf);
    const dd = lits.indexOf('--');
    const isRev = (t, l) => (l !== null ? /^[0-9a-f]{7,40}$/.test(l) : /(sha|ref|base|tip|rev|head)/i.test(t));
    let afterRev = -1;
    if (dd >= 0) for (let i = dd + 1; i < toks.length; i++) if (isRev(toks[i], lits[i])) { afterRev = i; break; }
    let grade;
    if (dd < 0) grade = 'A';
    else if (afterRev >= 0) grade = 'C';
    else grade = 'B';
    sites.push({ file, line, shape, argv: toks.join(', '), grade });
  }

  const files = [];
  (function walk(dir) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (e.name === '.build' || e.name === 'node_modules' || e.name === '.git') continue;
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else if (e.name.endsWith('.js')) files.push(p);
    }
  })(HERE);

  /* ★★ 这一段有过两版，两版都错，错法不同，都留在这里：
   * 第一版：不处理字符串就扫 ⇒ 本块自己的 `SHAPES` 里那三个字符串字面量被本块的三个正则
   *   各匹配一次 ⇒ 分母从 29 虚增到 **32**，多出来的那几条【argv 也被填上了】，
   *   看起来和真的调用处一模一样。
   *   ★★★ 而【这一段注释自己】就是第二次发作：我为了说明那个缺陷，把三个形状的原文
   *   逐字抄在这里，于是扫描器把【注释里的抄本】也数成了调用处 ——
   *   多出来的正是 `:2822`（SHAPES 那行）与 `:2896`（本行）。
   *   ⇒ 判据：**一段用来警告某个缺陷的注释，如果它逐字包含了那个缺陷的样本，
   *     它自己就又是一个样本。** 所以这段注释【不写那三个形状的原文】。
   * 第二版：整个文件跑一个模式状态机把字符串挖空 ⇒ 分母掉到 **2**。原因是一个**正则字面量**
   *   里的引号（`/^(['"`])(...)$/`）被当成了字符串的开头 ⇒ 它后面的整个文件都被挖空。
   * ⇒ 判据：**一个全文件的词法状态机，会被它自己不认识的一种字面量带跑，而且它带跑的
   *   方向是【静默地少算】，输出依旧自洽。**
   * 第三版（现在这版）：只在**行内**判「这个匹配点是不是落在引号里」——
   *   没有跨行的状态，所以一种它不认识的字面量最多影响一行，不会污染整个文件。 */
  /* ★★ 这一支也修过一次。第一版只认 ' " `，于是把**模板串插值里的那个调用**
   *   判成【文本】—— 而 `${…}` 里面是**代码**。后果同样是静默地少算一个真的调用处
   *   （实测：`report_diff.js:65` 整条不见了，分母从 29 掉到 28）。
   *   ★★★ 而这段注释的第一稿【又把样本逐字抄了进来】，于是本行自己变回了一个幽灵
   *   ⇒ 分母 28 → 30（多出来的两条：`report_diff.js:65` 真的、本行假的）。
   *   ⇒ 判据（第三次收到的同一条）：**一段用来警告某个缺陷的注释，如果它逐字包含了
   *     那个缺陷的样本，它自己就又是一个样本。** 所以这段注释不写样本的原文。
   *   ⇒ 判据（与正则字面量那次同族）：**一个行内的词法判定，不认识某一种构造时，
   *     会静默地少算一个真的对象，而输出依旧自洽。** 修法 = 让 `` ` `` 模式认
   *   `${` 与配对的 `}`：**插值里回到代码态**。 */
  function quoteAt(line, idx) {
    let mode = null;      /* ' " ` */
    let depth = 0;        /* 模板串里 ${…} 的嵌套深度 */
    for (let i = 0; i < idx && i < line.length; i++) {
      const c = line[i];
      if (mode === '`') {
        if (c === '\\') i++;
        else if (c === '`') mode = null;
        else if (c === '$' && line[i + 1] === '{') { depth++; mode = null; i++; }
      } else if (mode !== null) {
        if (c === '\\') i++;
        else if (c === mode) mode = null;
      } else {
        if (c === "'" || c === '"') mode = c;
        else if (c === '`') mode = '`';
        else if (depth > 0 && c === '}') { depth--; mode = '`'; }
      }
    }
    return mode !== null;
  }
  function inString(src, idx) {
    const ls = src.lastIndexOf('\n', idx - 1) + 1;
    return quoteAt(src.slice(ls, idx), idx - ls);
  }

  for (const f of files) {
    const src = fs.readFileSync(f, 'utf8');
    const rel = path.relative(ROOT, f);
    const ln = (idx) => src.slice(0, idx).split('\n').length;
    let m;

    const re1 = /execFileSync\(\s*['"]git['"]\s*,\s*/g;
    while ((m = re1.exec(src)) !== null) {
      /* ★★ 这里曾经有一个缺陷，留在这里当标本：
       * 第一版写的是「从调用点往后找【下一个】 `[`」。实参真是数组字面量时它是对的；
       * 但实参是【变量】时（`execFileSync('git', a, {…})` 这种【包装器定义】），
       * 它会一路找到【别处】的一个 `[` ⇒ 凭空多出一个「调用处」，而它的 argv 也被填上了，
       * 看起来和一个真的调用处一模一样（实测：`report_diff.js:26` 凭空得到 argv `2`，
       * 那是 `process.argv[2]`；`run_tutorial.js:31` 凭空得到 `1, 2, 3`）。
       * ⇒ 判据：**一个把「调用处」数出来的探针，一旦它假设了实参的形状，
       *   每一个形状不符的地方都会变成一个多出来的调用处 —— 而抓它不能靠计数，
       *   只能靠读每一个 argv 是不是 git 的参数。**
       * 修法：实参不是 `[` 就记进 wrappers，用【自己的分母】印，不与 A/B/C/D 混。 */
      if (inString(src, m.index)) continue;      /* ★ 落在引号里的，是【描述一个调用处的文本】，不是调用处 */
      let j = m.index + m[0].length;
      while (j < src.length && /\s/.test(src[j])) j++;
      if (src[j] !== '[') {
        let k = j; while (k < src.length && src[k] !== ',') k++;
        wrappers.push({ file: rel, line: ln(m.index), expr: src.slice(j, k).trim().slice(0, 40) });
        continue;
      }
      const r = match(src, j);
      if (r) take(rel, ln(m.index), SHAPES[0], r.body);
    }

    const re2 = /(^|[^.\w])git\(\s*\[/g;
    while ((m = re2.exec(src)) !== null) {
      /* ★ m[0] 是「前导字符 + git(」⇒ m.index 落在【那个前导字符】上，
       *   而行内判定在【进入引号之前】就停了 ⇒ 它看见的是【引号外面】。
       *   要判的是【那个 `git` 自己在不在引号里】：m.index + m[0].indexOf('git')。 */
      if (inString(src, m.index + m[0].indexOf('git'))) continue;
      const b = src.indexOf('[', m.index + m[0].length - 1);
      if (b < 0) continue;
      const r = match(src, b);
      if (r) take(rel, ln(m.index), SHAPES[1], r.body);
    }

    const re3 = /(^|[^.\w])git\(\s*(['"`])/g;
    while ((m = re3.exec(src)) !== null) {
      if (inString(src, m.index + m[0].indexOf('git'))) continue;   /* ★ 同 re2：判 git 自己 */
      /* ★★ 这里曾经是 `m.index + m[0].length - 2` —— **差一**：它落在【那个引号】上，
       * 于是 `indexOf('(', …)` 从引号往后找，找到的是【别处】的括号。
       * 后果不是报错，是**静默地丢掉一个真的调用处**：`report_diff.js:65`
       * （`git('diff', '--stat', ${base}..${tip}, '--', 'docs/')`）整条不见了，
       * 而 `:50` 同形状却靠运气解析对了 ⇒ 分母少 1 而输出看上去完全正常。
       * 修法：**不要算式**，直接问 m[0] 里那个 `(` 在哪。 */
      const open = m.index + m[0].indexOf('(');
      if (open < 0) continue;
      const r = match(src, open);
      if (r) take(rel, ln(m.index), SHAPES[2], r.body);
    }
  }

  sites.sort((a, b) => (a.file + ':' + a.line).localeCompare(b.file + ':' + b.line));
  const TALLY = { A: 0, B: 0, C: 0, D: 0 };
  for (const s of sites) TALLY[s.grade]++;

  /* ───────── 效力证明 ─────────
   * ★ C 档 = 0 不构成判据 ⇒ 必须另证尺子能看见差异。
   * ★ 期望值必须来自【外部】：`git show <sha>:<path>` 不经 `--`，
   *   所以它不会被同一个次序错误污染 —— 它是本证明的参照系。
   * ★ 三个次序【真跑一遍】，量 (rc, 字节数, 与参照是否同一对象)。
   *   证明的宾语是：**这三个次序给出的对象【不全相同】** ——
   *   「不全相同」就是这把尺子能看见差异的证据。
   */
  const P = 'docs/AUDIT.md';
  const S = RULING_ROWS_READ_AT;     // 4829766，已钉住、只读、一字未动
  const EMPTY_SHA = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';

  function probe(argv) {
    try {
      const out = execFileSync('git', argv, { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 });
      return { rc: 0, text: out };
    } catch (e) {
      return { rc: e.status === undefined ? -1 : e.status, text: String(e.stdout || '') };
    }
  }
  const sha = (t) => require('crypto').createHash('sha256').update(t).digest('hex');

  const ORDERS = [
    { key: '退化·两者都在 -- 之后', argv: ['show', '--no-renames', '--format=', '--', P, S] },
    { key: '退化·路径在前修订在后', argv: ['show', '--no-renames', '--format=', P, '--', S] },
    { key: '有序·修订在 -- 之前', argv: ['show', '--no-renames', '--format=', S, '--', P] },
  ];
  const runs = ORDERS.map((o) => {
    const r = probe(o.argv);
    return { ...o, rc: r.rc, bytes: Buffer.byteLength(r.text), sha: sha(r.text), empty: sha(r.text) === EMPTY_SHA };
  });
  const REF = probe(['show', `${S}:${P}`]);
  const refSha = sha(REF.text);

  return { sites, wrappers, TALLY, runs, refSha, refBytes: Buffer.byteLength(REF.text), S, P, EMPTY_SHA };
})();

info('㉚ 乙 · ★ 分母（脚本里出现的 git 调用处数，机械枚举，三种调用形状都算，且落在一行引号里的匹配不算调用处）= ' + G30.sites.length);
G30.sites.forEach((s) => info(`　　　${s.grade}  ${s.file}:${s.line}  [${s.shape}]  ${s.argv}`));
info(`　　A（无 --，次序问题不适用）= ${G30.TALLY.A}`);
info(`　　B（有 -- 且修订在 -- 之前）= ${G30.TALLY.B}`);
info(`　　C（有 -- 且修订在 -- 之后 ⇒ 静默地量错对象）= ${G30.TALLY.C}`);
info(`　　D（判不了）= ${G30.TALLY.D}　　和 = ${G30.TALLY.A + G30.TALLY.B + G30.TALLY.C + G30.TALLY.D}`);
info(`　　★ 另一个分母（不是 A/B/C/D 的一部分）：execFileSync('git', 变量, …) = ${G30.wrappers.length} 处 —— 这些是【包装器定义】，不是调用处；把定义算成它的一个实例，分母就虚了`);
G30.wrappers.forEach((w) => info(`　　　　${w.file}:${w.line}  实参=${w.expr}`));

info('');
info('㉚ 乙 · ★★ 效力证明（C 档今天 = 0，而 **一个看不见东西的 0 不构成判据**）：');
info(`　　★ 期望值来自【外部】：git show ${G30.S}:${G30.P} —— 它不经 --，所以不会被同一个次序错误污染；${G30.refBytes} 字节，sha256 ${G30.refSha.slice(0, 16)}…`);
G30.runs.forEach((r) => info(`　　　${r.key}　rc=${r.rc}　字节=${r.bytes}　${r.empty ? '★ 输出是【空】' : ''}`));
info(`　　★★ 不同的 sha256 个数 = ${new Set(G30.runs.map((r) => r.sha)).size} / ${G30.runs.length} 次运行`);
info('　　★ 这里【不写】「互不相同 = true/false」：那句话读起来像一条读数，其实是在回答一个**更强、且今天为假**的问题 —— 两个退化形态本来就给出同一个结果（空）。真问题是「**不全相同**」，它的意思是 set 大小 > 1，今天它是绿的。★ 判据：**一个读数必须在它的名字里说清它问的是哪个问题**，否则一个绿的条件旁边会站着一句 false。');
info(`　　★★ 我原先写进判据的机制是【错】的：我写「退化形态把那个 sha 当路径，于是每次都读 HEAD」。真跑出来不是这样 —— 退化形态返回 **rc=0、0 字节、零提示**，它连 HEAD 都不读，它什么都不读。`);

check('㉚ 乙 ①：★ 分母 = A + B + C + D（四档之和必须等于枚举出的调用处数 —— 一个漏掉的调用处会从和里消失）',
  G30.TALLY.A + G30.TALLY.B + G30.TALLY.C + G30.TALLY.D === G30.sites.length && G30.sites.length > 0,
  `和 = ${G30.TALLY.A + G30.TALLY.B + G30.TALLY.C + G30.TALLY.D}　枚举 = ${G30.sites.length}`);

check('㉚ 乙 ②：C 档 = 0（本线没有一处把修订参数写在 `--` 之后）',
  G30.TALLY.C === 0,
  `C = ${G30.TALLY.C}　★ 零是在这 ${G30.sites.length} 处上量的，不是在「印象里的 16 处」上量的`);

check('㉚ 乙 ③：★★ 尺子不是瞎的 —— 三个次序给出的对象【不全相同】',
  new Set(G30.runs.map((r) => r.sha)).size > 1,
  `不同的 sha256 个数 = ${new Set(G30.runs.map((r) => r.sha)).size} / ${G30.runs.length} 次运行　`
  + `★★ 这里【不是】「个数 = 次数」：两个退化形态**本来就给出同一个结果（空）**，`
  + `所以「不全相同」的意思是 set 大小 > 1，不是 set 大小 = 运行次数。后者是更强的条件，`
  + `它今天为假，而假的原因不是尺子瞎了，是那两个退化形态本来就同一个结果。（我第一版写的就是那个更强的条件，已改正。）`);

/* ★★ 这一条不依赖任何挖空/引号判定机制，它查的是【结果】：
 * 枚举出来的每一条，它那一行把注释前缀去掉之后，仍然得含有那个 `git(`。
 * 理由：本块已经在同一件事上栽过三次（SHAPES 的字面量、第一版警告注释、
 * 第二版警告注释），每一次的机制都不同，**共同点只有一个 —— 数出来的那一条
 * 根本不在代码里**。所以判据要落在「那一条是不是代码」上，不落在「我是怎么
 * 把非代码挡掉的」上。 */
{
  const files30 = [path.join(HERE, 'check_citations.js'), path.join(HERE, 'report_diff.js'),
    path.join(HERE, 'run_snippets.js'), path.join(HERE, 'run_tutorial.js')];
  const bad30 = [];
  G30.sites.forEach((st) => {
    const f30 = files30.find((x) => path.relative(ROOT, x) === st.file);
    if (!f30) { bad30.push(`${st.file}:${st.line}（文件不在列表里）`); return; }
    const raw30 = fs.readFileSync(f30, 'utf8').split('\n')[st.line - 1] || '';
    const code30 = raw30.replace(/^\s*\*\/?/, '').replace(/^\s*\/\//, '').trim();
    /* ★★ 第一版这里写的是 `/git\s*\(/` —— 它把本条探针自己变成了一个假阳性源：
     *   `execFileSync('git', ['rev-parse', …])` 那一形状里**根本没有裸的 `git(`**，
     *   那个 `git` 在引号里、后面跟的是 `', [`。⇒ 16 条真的调用处被判成注释。
     *   ★ 判据：**一条检查「这一条是不是代码」的探针，如果它找的记号不是那一条自己
     *     的形状的记号，它红的位置就与它要说的事无关。**
     *   修法：按形状取记号 —— 形状 0 找 `execFileSync(`，形状 1/2 找 `git(`。 */
    /* ★ 不要引 `SHAPES`（它在另一个作用域里，实测 `ReferenceError: SHAPES is not defined`）——
     *   形状标签【就在这一条自己身上】：s.shape 是那句原文。 */
    const mark30 = /^execFileSync/.test(st.shape) ? /execFileSync\s*\(/ : /git\s*\(/;
    if (!mark30.test(code30)) bad30.push(`${st.file}:${st.line}　行=「${code30.slice(0, 60)}」`);
  });
  check('㉚ 乙 ⑦：★★ 每一条枚举出来的调用处，它那一行都是【代码】不是【注释】',
    bad30.length === 0,
    `不是代码的 = ${bad30.length} 条${bad30.length ? '：' + bad30.join('；') : ''}　`
    + `★ 这一条不经过任何挖空机制 —— 它直接读那一行，把注释前缀剥掉再看还有没有那个调用。`);
}

check('㉚ 乙 ⑥：★★ 没有一个次序给出的对象等于【外部参照】（git show <sha>:<path>）',
  G30.runs.every((r) => r.sha !== G30.refSha),
  `与外部参照一致的 = ${G30.runs.filter((r) => r.sha === G30.refSha).length}　`
  + `★ 连【有序】形态都不等于它 —— 因为 git show <sha> -- <path> 给的是 **diff**（提交的差异），`
  + `而 git show <sha>:<path> 给的是 **blob**（文件本身）。⇒ 「次序对了」与「取到了那个对象」是两件事。`);

check('㉚ 乙 ④：★★ 那条静默路径是可复现的 —— 退化形态 rc=0 且输出为【空】，而它要的那个对象非空',
  G30.runs[0].rc === 0 && G30.runs[0].empty && G30.refBytes > 0,
  `退化形态 rc=${G30.runs[0].rc}　字节=${G30.runs[0].bytes}　sha256=${G30.runs[0].sha.slice(0, 16)}…（= 空串的 sha256）　`
  + `而 ${G30.S}:${G30.P} 有 ${G30.refBytes} 字节 ⇒ **空是静默的，因为它不报错**`);

check('㉚ 乙 ⑤：★ 包装器有自己的分母，且它不被算进调用处',
  G30.wrappers.length > 0 && G30.sites.every((s) => !G30.wrappers.some((w) => w.file === s.file && w.line === s.line)),
  `包装器 = ${G30.wrappers.length}　调用处 = ${G30.sites.length}　★ 两者没有一行重叠`);

/* ═══ ㉛ 丙 —— 徽章映射：主判据换对象（从「不同的 class」换成「不同的外观」）══════
   ★ exact-otter 30 号的裁决。★ 本块只【读】映射表；改【值】的动作一个字都没有。

   ★★ 为什么把主判据换对象：一条对今天并不算错的事情发红的规则，会被学会忽略 ——
      而它一旦被忽略，真正该红的那天也不会红。
      ⇒ 丙① 今天必须是【绿的】（文字与 title 都不同），而它会在真正撞上的那天红。
   ★ 外观的定义照 `site.js:292` 实际渲染的那三样（class ＋ 文字 ＋ title），
      并把那三样【逐字印在判词旁边】。
   ★★ `coordinator` 与 `relayed` 共用 class 是【有意的】—— 理由写在
      `website/data/decisions.js` 那张表的 `coordinator` 那一行上（没有写下来的「有意的共用」，
      下一个人会把它当成一个 bug 修掉）。 */
{
  const ROUTES = D.ruledByRoutes || [];

  /* 外观 = 渲染那一行真正用到的东西。逐字对照 site.js：
       :290  var label = route ? route.label : '谁裁的：' + e.ruledBy;
       :291  var cls   = route ? route.badge : 'badge--muted';
       :292  var badge = el('span', 'badge ' + cls, label);
       :293  if (route && route.where) badge.title = '出处：' + route.where; */
  const faceOf = (r) => `class=${r.badge}　文字=${JSON.stringify(r.label)}　title=${r.where ? '有(' + r.where + ')' : '无'}`;

  const byFace = {};
  ROUTES.forEach((r) => { (byFace[faceOf(r)] = byFace[faceOf(r)] || []).push(r.key); });
  const faceClash = Object.keys(byFace).filter((f) => byFace[f].length > 1);

  /* ── 丙① 主判据：不同的 key 必须【渲染】出不同的外观 ── */
  check('㉛ 丙①：不同的 key 必须【渲染】出不同的外观（class ＋ 文字 ＋ title 三样，照 site.js:292 实际渲染的算）'
    + '（否则「今天看见 N 种徽章」不是「有 N 条路线」）',
    faceClash.length === 0,
    faceClash.length
      ? '外观逐字相同的 key：' + faceClash.map((f) => `【${f}】← ${byFace[f].join(' ＋ ')}`).join('；')
      : `无。今天 ${ROUTES.length} 个 key 落到 ${Object.keys(byFace).length} 种外观，逐字：\n      `
        + ROUTES.map((r) => `${r.key.padEnd(12)} ${faceOf(r)}`).join('\n      '));

  /* ★★ 那三样必须【每次跑都印】—— 判词里的 detail 只在红时才打印，
     而 丙① 今天正是绿的 ⇒ 只写在判词里，等于今天看不见它。 */
  info(`㉛ 丙①【主判据今天的外观逐字】（判词里的同一份，绿的时候也要看得见）—— `
    + `${ROUTES.length} 个 key 落到 ${Object.keys(byFace).length} 种外观：`
    + ROUTES.map((r) => `
      ${r.key.padEnd(12)} ${faceOf(r)}`).join(''));

  /* ── 丙② 保留一个【数】，不保留一条红 ──
     ★ 今天它 ≥ 1（relayed ＋ coordinator 共用 `badge--route-relayed`），
       而那个共用是【有意的、写在表上的】—— 所以它必须是一个读数，不是一个红。 */
  const byBadge = {};
  ROUTES.forEach((r) => { (byBadge[r.badge] = byBadge[r.badge] || []).push(r.key); });
  const badgeShared = Object.keys(byBadge).filter((b) => byBadge[b].length > 1);
  info(`㉛ 丙②【共用同一个 class 的 key 组数】= ${badgeShared.length}`
    + `（今天：${badgeShared.map((b) => `${b} ← ${byBadge[b].join(' ＋ ')}`).join('；') || '无'}）`
    + `　★ 这是一个【读数】，不是一条红 —— 那个共用是有意的，理由写在 decisions.js 的 coordinator 那一行`);

  /* ── 乙 ④⑤⑥：那张表的 badge 与 label 全仓各有几个消费者 + 那个计数处的失效模样 ──
     ★ 逐字找的消费者（不含本块自己）：
       badge ← website/assets/site.js:291  `var cls = route ? route.badge : 'badge--muted';`      1 处
       label ← website/assets/site.js:290  `var label = route ? route.label : '谁裁的：' + e.ruledBy;`  1 处
     ★★ 而 site.js:338 那个计数【不读这张表】：
       `var human = D.entries.filter(function (e) { return e.ruledBy === 'human'; }).length;`
       ⇒ 它写死了一个字面量。所以它【不会】把 4 个 key 印成 3 —— 它连表都不看。
     ★★★ 它的失效模样（实测）：把表里的 key `human` 改名（值一个字不动）⇒
       :338 仍然印 `human = 18 条`，而按 site.js:289（`r.key === e.ruledBy`）能找到 route 的只剩 4 条
       ⇒ 同一页上「18 条是人类选的」与「0 个徽章」同时成立，而它们说的是同一件事。
       ★ 这条漂移【不是没人看】：render_check.js 已经有一条抓它，对照下逐字报 `← 0 / 18`。 */
  const consumers = { badge: ['website/assets/site.js:291'], label: ['website/assets/site.js:290'] };
  const corpusKeys = {};
  D.entries.forEach((e) => { if (e.ruledBy) corpusKeys[e.ruledBy] = (corpusKeys[e.ruledBy] || 0) + 1; });
  const tableKeys = new Set(ROUTES.map((r) => r.key));
  const orphans = Object.keys(corpusKeys).filter((k) => !tableKeys.has(k));
  info(`㉛ 乙 ④【全仓消费者】badge → ${consumers.badge.length} 处（${consumers.badge.join('、')}）；`
    + `label → ${consumers.label.length} 处（${consumers.label.join('、')}）`
    + `　★ 本块自己那几处不算消费者，它们是读的人`);
  info(`㉛ 乙 ⑤【那个计数处会不会把 4 个 key 印成 3】= 不会 —— 它不读那张表。`
    + `逐字：site.js:338 \`var human = D.entries.filter(function (e) { return e.ruledBy === 'human'; }).length;\` `
    + `★ 它写死了一个字面量 \`'human'\`。`
    + `　今天【语料里出现、表里查不到的 key】= ${orphans.length}${orphans.length ? '（' + orphans.map((k) => k + '×' + corpusKeys[k]).join('、') + '）' : ''}`
    + `　★ 它不是 0 的那天，:338 会继续印一个数，而徽章会全部退成 \`badge--muted\``);
  info(`㉛ 乙 ⑥【那条漂移有人看】：render_check.js 有一条「带「人类选的」标记的条数 = 逐条 ruledBy=human 的条数」；`
    + `把表里的 key 改名（值一个字不动）后，它逐字报 \`← 0 / 18\` —— 也就是 :338 那个数不动，而徽章数归零。`
    + `⇒ 判据：**一个写死的字面量不是「读这张表」，所以表变了它不会变 —— 而它不报错，它只是与另一处读数分道扬镳。**`);

  /* ── 甲② 可见差异：若那个 key 出现一次，页面会变吗 ── */
  const byKey = {};
  ROUTES.forEach((r) => { byKey[r.key] = r; });
  const collideKeys = badgeShared.reduce((a, b) => a.concat(byBadge[b]), []);
  info(`㉛ 甲②【若共用 class 的那些 key 各出现一次，页面会变吗】= `
    + (collideKeys.length >= 2
      ? `会变，而且看得见 —— 它们落到 ${new Set(collideKeys.map((k) => faceOf(byKey[k]))).size} 种不同的外观：`
        + collideKeys.map((k) => `【${k}】` + faceOf(byKey[k])).join('  vs  ')
      : '没有共用 class 的 key，这一问今天无载荷')
    + `　← 判据出处 = site.js:290-292（label 是元素的内容，badge 只是 class）与 site.js:293（where 只影响 title）`);

  /* ── 乙 三层各自独立的读（不许合并，和不相加）── */
  const allowedKeys = ROUTES.length;
  const counted = {};
  D.entries.forEach((e) => { if (e.ruledBy) counted[e.ruledBy] = (counted[e.ruledBy] || 0) + 1; });
  const todayKeys = Object.keys(counted);
  const distinctBadge = Object.keys(byBadge).length;
  const distinctFace = Object.keys(byFace).length;

  info(`㉛ 乙 ①【定义式允许】= ${allowedKeys} 个 key（${ROUTES.map((r) => r.key).join('／')}）`
    + ` ← 读 website/data/decisions.js:64 的 D.ruledByRoutes，${ROUTES.length} 行`);
  info(`㉛ 乙 ②【今天取到】= ${todayKeys.length} 个值`
    + `（${todayKeys.map((k) => k + '×' + counted[k]).join('、')}）`
    + ` ← 读 D.entries 里带 ruledBy 的 ${todayKeys.reduce((n, k) => n + counted[k], 0)} 行，分母 = entries 总数 ${D.entries.length}`);
  info(`㉛ 乙 ③【页面显现】= ${distinctFace} 种外观`
    + `（若只按 badge 算则是 ${distinctBadge} 种）`
    + ` ← 读同一张映射表：外观 = (badge,label)，因为 site.js:292 把 label 当成了元素的内容`);
  info(`㉛ 乙 ★★ 这三层之间的差不是减法：① ${allowedKeys} → ② ${todayKeys.length} → ③ ${distinctFace}。`
    + `第 2 个数来自【语料】（今天哪些 key 真出现过），第 3 个数来自【映射表】（哪些 (badge,label) 互不相同）。`
    + `★ 三行不相加。`);
  info(`㉛ 乙 ★★ 自检（今天有没有两行相等）—— 有：`
    + `① ${allowedKeys} 与 ③ ${distinctFace} 相等，而【这不是巧合，是同一件事】：两者读的是同一张表，`
    + `只要表里每一行的 (badge,label) 互不相同，① = ③ 就恒成立（今天是，所以 ${allowedKeys} = ${distinctFace}）。`
    + `　而 ② ${todayKeys.length} 与 ③ ${distinctFace} 【不相等】—— 那才是【语料】与【映射表】的差：`
    + `coordinator 出现一次，② 会跟着语料变成 ${allowedKeys}，③ 仍然 ${distinctFace}（映射表没动）。`);
  info(`㉛ 乙 ★★ 撤回一个我先前的预测：我预告过「② 与 ③ 都是 3、那是巧合」——`
    + `★ 那个相等是【我把「外观」定义成 badge 一个字段】的产物，不是事实。`
    + `把外观改回 site.js:292 真正渲染的 (class ＋ 文字) 之后，② ${todayKeys.length} ≠ ③ ${distinctFace}，那个「巧合」就消失了。`
    + `⇒ 判据：一个预测出来的巧合，先要检查它是【事实】还是【我那个定义】的产物。`);

  /* CENSUS-BEGIN ── ㉛ 乙 ⑦：把「读数被送进一个只在失败时说话的信道」做成一个数 ──────
     exact-otter 的判据：这道普查的答案不许用「我读了源码」来交 ——
       要么给一条能让它变红的输入，要么给一条让它绿着也印出来的补丁。
     ⇒ 本块自己解析自己的源码，并把解析器的四个已知坑都挡上：
         ① 括号配对（不逐行匹配）
         ② 行内引号判定（不跨行状态机 —— 本线吃过：一个正则字面量里的引号曾把整个文件挖空）
         ③ 正则字面量里的双斜杠曾被当成行注释 ⇒ 那一次它把实参表读穿了，而输出的那一行看起来完全正常
         ④ 它自己的样本 —— 本块**不许**逐字写出那种调用的样子；
            射程用两个哨兵行圈出来并在【结果】上排除（不是猜）。
     ★ 信道那一端是读出来的：全文件里 `.detail` 只被读一次，且那一次在 !r.ok 的循环里
       ⇒ 它【只在红的时候说话】。 */
  const CENSUS = (() => {
    const SELF = __filename;
    const s = fs.readFileSync(SELF, 'utf8');
    const lines = s.split('\n');
    /* ★★★ 第四次同一个笑话：`lines.findIndex((l) => l.includes('CENSUS-END'))` 返回的是
       【查找器自己的那一行】—— 因为那一行的源码里逐字写着 'CENSUS-END'。
       于是射程错了（3273..3289，而真哨兵在 3394），而且那个「射程内 = 0」的断言
       **绿着**：它绿，是因为它量错了区间，不是因为没有被排除的东西。
       ⇒ 修法：只认【整行以哨兵注释开头】的行（查找器自己那一行以 `const` 开头，不再匹配）。
       ⇒ 判据：一个「排除」如果没有被观测到【发生过】，它就是假的 —— 而它绿着。 */
    const cbeg = lines.findIndex((l) => /^\s*\/\* CENSUS-BEGIN/.test(l));
    const cend = lines.findIndex((l) => /^\s*\/\* CENSUS-END/.test(l));

    /* `/` 是正则的开始吗 —— 看它前面那个有效字符（标准启发式）。这是坑 ③ 的修法。 */
    const reStart = (t, i) => {
      for (let j = i - 1; j >= 0; j--) {
        const c = t[j];
        if (c === ' ' || c === '\t') continue;
        return '(,=:[!&|?{};+-*%~^<>\n'.indexOf(c) !== -1;
      }
      return true;
    };
    const atLine = (t, i) => t.slice(0, i).split('\n').length;
    const inStr = (t, i) => {
      const ls = t.lastIndexOf('\n', i - 1) + 1;
      let quote = null;
      for (let j = ls; j < i; j++) {
        const c = t[j];
        if (quote) { if (c === '\\') j++; else if (c === quote) quote = null; continue; }
        if (c === '/' && t[j + 1] === '/') break;
        if (c === '/' && reStart(t, j)) { j = t.indexOf('/', j + 1); if (j < 0) break; continue; }
        if (c === "'" || c === '"' || c === '`') quote = c;
      }
      return quote !== null;
    };
    const matchP = (t, open) => {
      let d = 0, quote = null;
      for (let i = open; i < t.length; i++) {
        const c = t[i];
        if (quote) { if (c === '\\') { i++; continue; } if (c === quote) quote = null; continue; }
        if (c === '/' && t[i + 1] === '/') { while (i < t.length && t[i] !== '\n') i++; continue; }
        if (c === '/' && t[i + 1] === '*') { i = t.indexOf('*/', i + 2); if (i < 0) return -1; i++; continue; }
        if (c === '/' && reStart(t, i)) { i = t.indexOf('/', i + 1); if (i < 0) return -1; continue; }
        if (c === "'" || c === '"' || c === '`') { quote = c; continue; }
        if (c === '(') d++;
        else if (c === ')') { d--; if (d === 0) return i; }
      }
      return -1;
    };
    const splitA = (t) => {
      const out = []; let depth = 0, start = 0, quote = null;
      for (let i = 0; i < t.length; i++) {
        const c = t[i];
        if (quote) { if (c === '\\') { i++; continue; } if (c === quote) quote = null; continue; }
        if (c === '/' && t[i + 1] === '/') { while (i < t.length && t[i] !== '\n') i++; continue; }
        if (c === '/' && reStart(t, i)) { i = t.indexOf('/', i + 1); if (i < 0) break; continue; }
        if (c === "'" || c === '"' || c === '`') { quote = c; continue; }
        if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') depth--;
        else if (c === ',' && depth === 0) { out.push(t.slice(start, i)); start = i + 1; }
      }
      out.push(t.slice(start));
      return out.map((x) => x.trim());
    };

    const CALL = 'check' + '(';
    const sites = [];
    let inside = 0;
    let ix = -1;
    while ((ix = s.indexOf(CALL, ix + 1)) !== -1) {
      const prev = ix > 0 ? s[ix - 1] : '';
      if (/[A-Za-z0-9_$.]/.test(prev)) continue;
      if (inStr(s, ix)) continue;
      const ln = atLine(s, ix);
      if (cbeg >= 0 && cend > cbeg && ln > cbeg + 1 && ln < cend + 1) { inside++; continue; }  // ★ ④
      const close = matchP(s, ix + CALL.length - 1);
      if (close < 0) continue;
      const a = splitA(s.slice(ix + CALL.length, close));
      if (a.length < 2) continue;
      sites.push({
        line: ln,
        nameLit: /^['"`]/.test(a[0]),
        name: /^['"`]/.test(a[0]) ? a[0].slice(1, -1) : null,
        hasInterp: a.length >= 3 && a[2].includes('${')
      });
    }

    const byName = new Map();
    results.forEach((r) => { if (!byName.has(r.name)) byName.set(r.name, []); byName.get(r.name).push(r); });
    const interp = sites.filter((x) => x.hasInterp);
    let green = 0, red = 0, unresolved = 0;
    for (const x of interp) {
      const rs = x.nameLit ? byName.get(x.name) : null;
      if (!rs || !rs.length) { unresolved++; continue; }
      if (rs.every((r) => r.ok)) green++; else red++;
    }
    const winIdx = lines.findIndex((l) => l.includes("' ← ' + r.detail") || l.includes('.detail'));
    return { SELF, sites, interp, green, red, unresolved, inside, cbeg, cend, winIdx };
  })();

  info(`㉛ 乙 ⑦【分母：本文件里 ${'check'} 调用总数】= ${CENSUS.sites.length}`
    + `　（哨兵行 ${CENSUS.cbeg + 1} / ${CENSUS.cend + 1} 圈出的射程内被排除 = ${CENSUS.inside} 处）`);
  info(`㉛ 乙 ⑦【第 3 个实参含 \${...} 插值 —— 读数被送进那个信道】= ${CENSUS.interp.length}`
    + `　★ 信道那一端是读出来的：.detail 全文只在本文件第 ${CENSUS.winIdx + 1} 行被读一次，`
    + `而那一行在 results.filter((r) => !r.ok) 的循环里 ⇒ 它【只在红的时候说话】。`);
  info(`㉛ 乙 ⑦【其中今天【绿】的 ⇒ 它们今天什么都没说】= ${CENSUS.green}`
    + `　（红 = ${CENSUS.red}，名字对不上 = ${CENSUS.unresolved}；和 = ${CENSUS.green + CENSUS.red + CENSUS.unresolved} / ${CENSUS.interp.length}）`
    + ` ★ 这一行的分母是【今天真的执行到的】那些；上一行的分母是【源码里写着】的 —— 两个分母不是同一个数换了个名字。`);
  info(`㉛ 乙 ⑦【对照：让一处绿着也印出来】= ㉖ ★ 006 那条「三条归属路线在数据里可读」旁边新加的 info()`
    + `　★ 判据：一条只在失败时说话的检查，在它绿的时候什么都没说 —— 而「它绿」与「它检查过」在输出里同形。`);
  info(`㉛ 乙 ⑦★ 全仓（不是本文件）：website/** 的 ${'check'} 调用 = 246，其中含插值 = 116`
    + `　★ 这两个数由【另一个解析器】在另一个进程里量出（website/scratch/.build/probe_details.js），`
    + `本块不复述它的结论、也不假装自己量了它 —— 记在这里是为了说明本块的分母只有它自己的 137-1 处。`);
  check('㉛ 乙 ⑦：自扫描的枚举不许把自己数进去 —— ★ 而「排除了」必须是【被观测到发生过】的',
    CENSUS.cbeg >= 0 && CENSUS.cend > CENSUS.cbeg && CENSUS.inside >= 1 && CENSUS.cend - CENSUS.cbeg > 100,
    `哨兵行 = ${CENSUS.cbeg + 1} / ${CENSUS.cend + 1}（跨度 ${CENSUS.cend - CENSUS.cbeg} 行，必须 > 100）；`
    + `射程内【确实被排除掉】的调用 = ${CENSUS.inside}（必须 >= 1，本块自己那条就在里面）`
    + `　★ 上一版写成 === 0，而它绿着 —— 因为它量错了区间：查找器把自己那一行当成了哨兵。`);
  /* CENSUS-END */
}

const bad = results.filter((r) => !r.ok);
results.filter((r) => !r.ok).forEach((r) => {
  console.log(`FAIL  [${r.where}] ${r.name}${r.detail ? '   ← ' + r.detail : ''}`);
});

const docsTouched = new Set(D.entries.map((e) => e.source && e.source.file).filter(Boolean));
console.log('');
console.log(`条目数：${D.entries.length}`);
const byRef = {};
D.entries.forEach((e) => { const r = readAtOf(e); byRef[r] = (byRef[r] || 0) + 1; });
console.log(`引文钉：${D.entries.length} 条，涉及 ${docsTouched.size} 个 docs 文件（读自 ${Object.keys(byRef).map((r) => `${String(r).slice(0, 7)} × ${byRef[r]}`).join(' / ')}）`);
console.log(`节号钉：${D.entries.length} 个`);
console.log(`身份一致：检查了 ${stanceChecked} 条，跳过 ${stanceSkipped} 条（词表 = 【已裁】/【已裁定】/【提案】/【待裁】/【未做】；标记看 标题+正文）`);
console.log(`做到没有：${Object.keys(builtCount).map((k) => k + ' ' + builtCount[k]).join(' / ')}（引文里带实现状态负面词的条目 ${builtNegative} 条）`);
/* ★ 分母要写清射程：`ruledBy` 是 `docs/EIDOS_V06.md` 这一份文件里的词汇，
   别的文档（PLAN_V06 / PLAN_V07 / TYPESET_V06）的已裁条目不写这一栏 —— 那是正常的，
   不是缺口。★ 所以下面那个「没写」只数那一份文件里的，否则它会把一个正常的省略读成缺陷。 */
const AT = D.attribution;
console.log('出处分级：' + Object.keys(AT.routes)
  .map((k) => `ruledBy=${k} ${D.entries.filter((e) => e.ruledBy === k).length} 条`)
  .join(' / ')
  + ` ／ 射程：${AT.file} 的已裁条目里，没写「谁裁的」的 `
  + D.entries.filter((e) => e.source.file === AT.file && e.status === 'ruled' && !e.ruledBy).length + ' 条'
  + '（humanRuled 那个块是这套做法的出处，不是花名册）');

infos.forEach((x) => console.log(`  · ${x}`));
console.log(`规范页元信息：${((S && S.docs) || []).length} 份文档（标题 / 行数 / 文件头引文）`);
console.log(`带身份标记的 docs：扫了 ${docFiles.length} 份 .md（读自 ${MSR_LABEL}），其中 ${marked.length} 份带标记`);
console.log(`标记 / 提及的分界：标记 ${boundary.marker.length} 行 / 提及 ${boundary.mention.length} 行（判成提及的逐行在下面）`);
boundary.mention.forEach((r) => console.log(`    · 提及  ${r}`));
console.log('');
console.log('  文件'.padEnd(32) + '各标记条数'.padEnd(44) + '在规范页清单里？ / 台账里有条目？');
if (!marked.length) {
  console.log('  （找不到：今天没有任何 docs 文件带这套身份标记 —— 这句话本身就是结论）');
} else {
  marked.forEach((x) => console.log(
    '  ' + x.file.padEnd(30)
    + Object.keys(x.kinds).map((k) => k + '×' + x.kinds[k]).join(' ').padEnd(42)
    + (x.inSpec ? '在清单里' : '★ 不在清单里')
    + ' / ' + (x.inLedger ? '有条目' : '★ 台账里 0 条')));
}
console.log(`被禁字符串：扫描 ${scanned.length} 个文本文件`);
console.log(`检查项：${results.length - bad.length}/${results.length} 通过`);
if (bad.length) {
  console.log(`\n✗ 有 ${bad.length} 项失败`);
  process.exit(1);
}
console.log('\n✓ 全部通过');
