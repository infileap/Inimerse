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
const isMarkerLine = (l, mk) => stripLead(l).startsWith(mk);

const MARKER_STATUS = {
  '【已裁】': 'ruled', '【已裁定】': 'ruled',
  '【提案】': 'proposal', '【待裁】': 'undecided', '【未做】': 'notdone',
};
const NOT_A_STANCE = ['undecided', 'notdone', 'notdoing', 'discipline'];

/** 取某节的标题行 */
function sectionHeading(text, section) {
  const esc = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`^#{2,4}\\s+(?:★+\\s+)?${esc}\\.?(?![\\d])(?=[\\s．]|$).*$`, 'm');
  const m = re.exec(text);
  return m ? m[0] : '';
}

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
const docFiles = execFileSync('git', ['ls-tree', '-r', '--name-only', D.ref, 'docs/'], { cwd: ROOT, encoding: 'utf8' })
  .split('\n').map((x) => x.trim()).filter((x) => x.endsWith('.md'));
const ledgerFiles = new Set(D.entries.map((e) => e.source && e.source.file).filter(Boolean));
const marked = [];
for (const rel of docFiles) {
  const kinds = {};
  docAtRef(D.ref, rel).text.split('\n').forEach((l) => {
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

/* ★★ 把【分界】本身打印出来。
   这一问的答案不是「这条规则判对了多少行」，是【它把哪些行判到了另一边】——
   只有把那些行列出来，这条边界才是可以被别人反驳的。
   而它必须每次运行都打印：**一次性的普查会过期，一条常驻的打印不会。** */
const boundary = { marker: [], mention: [] };
for (const rel of docFiles) {
  docAtRef(D.ref, rel).text.split('\n').forEach((l, i) => {
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
  const seam = process.env.ATTRIBUTION_DOC;
  const doc = seam ? { text: fs.readFileSync(seam, 'utf8') } : docAtRef(at.readAt, at.file);
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
console.log(`规范页元信息：${((S && S.docs) || []).length} 份文档（标题 / 行数 / 文件头引文）`);
console.log(`带身份标记的 docs：扫了 ${docFiles.length} 份 .md（读自 ${String(D.ref).slice(0, 7)}），其中 ${marked.length} 份带标记`);
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
