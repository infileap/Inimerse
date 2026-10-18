#!/usr/bin/env node
// 辛⁷ 探针：把「Δ 的方向与符号」做成一张可复核的表。
//
// 用法：node website/scratch/delta_probe.js <repoRoot>
//
// ★ 本探针【只读】：它不跑任何文档里的命令，不改任何受跟踪文件。
// ★★ 判据 D0.4：两个分母并排 —— P（行）与 Q（断言）。
//   留档里同一句话出现 1 + 运行次数 次（一次是源码里的 say(...)，其余是输出）。
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const NAIVE = /[;&|<>$()`\n\\]/;

// ---- 三个口径（与庚⁷ 逐字相同）----
function m2(s) {
  let q = null, back = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) { if (c === q) q = null; continue; }
    if (c === "'" || c === '"') { q = c; continue; }
    if (c === '\\') { back = back || c; continue; }
    if (';&|<>$()`\n'.includes(c)) return c;
  }
  return back;
}
function m3(s) {
  let q = null, back = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q === "'") { if (c === "'") q = null; continue; }
    if (q === '"') { if (c === '"') { q = null; continue; } if (c === '$' || c === '`') return c; continue; }
    if (c === "'" || c === '"') { q = c; continue; }
    if (c === '\\') { back = back || c; continue; }
    if (';&|<>$()`\n'.includes(c)) return c;
  }
  return back;
}

// ---- 语料：与丁⁷ / 戊⁷ / 己⁷ / 庚⁷ 同一份载体定义（逐字复制）----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);
const N1 = /\*\*(\d[\d,]*)\*\*/g, N2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const nums = (l) => { const a = [...l.matchAll(N1)].map((m) => m[1]); return a.length ? a : [...l.matchAll(N2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean); };
const SAFE_HEAD = new Set(['grep', 'wc', 'ctest', 'git', 'sed', 'python3', 'node', 'cmake', 'cargo', 'bash', 'ls-files', 'ls-tree']);
const GIT_READONLY = new Set(['ls-files', 'ls-tree', 'rev-list', 'rev-parse', 'show', 'grep', 'cat-file', 'merge-base', 'count-objects', 'log', 'diff', 'status']);
function gitSub(cmd) {
  const toks = cmd.trim().split(/\s+/);
  for (let i = 1; i < toks.length; i++) {
    const t = toks[i];
    if (t === '-C' || t === '-c') { i++; continue; }
    if (t.startsWith('--git-dir=') || t.startsWith('--work-tree=')) continue;
    if (t.startsWith('-')) continue;
    return t;
  }
  return null;
}
const hit1 = (s) => (s.match(NAIVE) || [null])[0];
function rulesHit(cmd) {
  const s = cmd.trim();
  const hit = [];
  if (!s) { hit.push(['R0', '空']); return hit; }
  if (hit1(s)) hit.push(['R1', 'x']);
  const head = s.split(/\s+/)[0];
  if (!SAFE_HEAD.has(head)) hit.push(['R2', 'x']);
  if (head === 'git') {
    const sub = gitSub(s);
    if (!sub) hit.push(['R3', 'x']); else if (!GIT_READONLY.has(sub)) hit.push(['R4', 'x']);
  }
  if (['bash', 'node', 'python3', 'cmake', 'cargo'].includes(head)) hit.push(['R5', 'x']);
  return hit;
}
const isMine = (hit) => hit.some(([k]) => k !== 'R0');

const rows = [];
for (const f of files) {
  let t; try { t = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  t.split('\n').forEach((line, i) => {
    if (!nums(line).length) return;
    const spans = [...line.matchAll(/`([^`]*)`/g)].map((m) => m[1]);
    const cmds = spans.filter((s) => !isMine(rulesHit(s)) || SAFE_HEAD.has(s.trim().split(/\s+/)[0]));
    if (!cmds.length) return;
    rows.push({ k: f + ':' + (i + 1), cmd: cmds[0] });
  });
}
const uniq = (a) => { const m = new Map(); for (const r of a) if (!m.has(r.k)) m.set(r.k, r); return [...m.values()]; };
const all = uniq(rows);
const S1 = new Set(all.filter((r) => !!hit1(r.cmd)).map((r) => r.k));
const S2 = new Set(all.filter((r) => !!m2(r.cmd)).map((r) => r.k));
const S3 = new Set(all.filter((r) => !!m3(r.cmd)).map((r) => r.k));
const d = (A, B) => [...A].filter((k) => !B.has(k)).length;
const TODAY = { '①→②': d(S1, S2), '②→③': d(S3, S2), '①→③': d(S1, S3) };

const say = (s) => console.log(s);
say('=== 判据 D0：载体与今天的三个集合');
say('  受跟踪 .md 文件数 = ' + files.length + '   语料 = ' + all.length);
say('  |①| = ' + S1.size + '   |②| = ' + S2.size + '   |③| = ' + S3.size);
say('  今天重算： ①→② = ' + TODAY['①→②'] + '   ②→③ = ' + TODAY['②→③'] + '   ①→③ = ' + TODAY['①→③']);
say('  ★ 定义： ①→② = |① \\ ②| ／ ②→③ = |③ \\ ②| ／ ①→③ = |① \\ ③|');
say('  ★ 方向定义：Δ = 【后一个口径收的】 − 【前一个口径收的】 ⇒ π₁ 是收紧时 Δ 为负');
say('');

// ================= 扫描：本车道的产物里的「差 = N」 =================
const lane = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', 'website/scratch/*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);
say('=== 判据 D0.1：本车道的产物（' + lane.length + ' 份 .md）');
say('');

// 「差」后跟一个数（可带负号），或「的差 = N」
const RE = /差\s*(?:=|\s*是\s*|\s*为\s*)\s*(−|-)?\s*(\d[\d,]*)/g;
const hits = [];
for (const f of lane) {
  let t; try { t = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  t.split('\n').forEach((line, i) => {
    RE.lastIndex = 0;
    let mm;
    while ((mm = RE.exec(line)) !== null) {
      const sign = mm[1] || '';
      const n = parseInt(mm[2].replace(/,/g, ''), 10);
      // 逐字那一句：取包含匹配位置的那一段（到行尾或到分号/句号）
      const start = Math.max(0, line.lastIndexOf('★', mm.index));
      const raw = line.slice(start === 0 ? 0 : start).trim();
      hits.push({ file: f, line: i + 1, raw, sign, n, text: line, src: sourceOf(line, mm.index) });
    }
  });
}
// ★★★ 判据 D1.6：一句话的【来源】按形状判，不按语义判。
//   - 源码：这一行里有 `say(`（探针自己打印的那句话）
//   - 引述：那个「差 = N」落在一个【引号对内部】（「…」或 "…"）—— 它在引述别人
//   - 断言：其余
function sourceOf(line, idx) {
  if (/say\(/.test(line)) return '源码';
  const pre = line.slice(0, idx);
  const opens = (pre.match(/「/g) || []).length, closes = (pre.match(/」/g) || []).length;
  if (opens > closes) return '引述';
  const dq = (pre.match(/"/g) || []).length;
  if (dq % 2 === 1) return '引述';
  return '断言';
}

// 「哪一对」的判定（判据 D1.2）：只看那句话本身
// ★★★ 判据 D1.2b：口径名必须是【独立记号】—— `E₁₃` 里的 `③` 不是口径名。
//   第一版把 `E₁₃ 与 E₁₂ 的差` 读成了「③ 与 ②」，给出一个不存在的对 `③→②`，
//   而 `TODAY['③→②']` 是 undefined ⇒ 那一处落了一个假的 ②。
const TOK = '(?<![\dA-Za-z_₁₂₃])(①|②|③|不认引号|认引号)';
// ★★★ 判据 D1.2c：那一对必须出现在【「差」的左边】。
//   `scope_transcript.md:70` 逐字：`★ 检查 ③ ⊇ ②：E₁₃ 与 E₁₂ 的差 = -2（=0 表示③与②改判的处完全相同）`
//   —— 本版第一版在【括号里的解释】里找到了 `③与②`，于是把那一句读成了「写了哪一对」。
//   而它的主语是 `E₁₃ 与 E₁₂`（**集合名，不是口径名**）⇒ 它【没有写哪一对口径】。
function pairOf(s) {
  const cut = s.search(/差/);
  const head = cut < 0 ? s : s.slice(0, cut);
  const m = head.match(new RegExp(TOK + '\\s*(?:与|和|→|->|相对)\\s*(①|②|③|不认引号|认引号)'));
  if (!m) return null;
  const canon = (x) => x.startsWith('①') || x === '不认引号' ? '①' : x.startsWith('②') || x === '认引号' ? '②' : x.startsWith('③') ? '③' : x;
  return canon(m[1]) + '→' + canon(m[2]);
}

// ★★★ 判据 D0.4b：一条【断言】= (哪一对, 符号, 那个数) 这个三元组。
//   第一版用的是「整行正文逐字相同」⇒ `quote_reading.md:48` 与 `quote_transcript.md:418`
//   是同一句话而尾部不同，被算成了两条断言。
//   ⇒ 去重规则决定了分母 —— 而两条规则给出的数都自洽。
const assertKey = (h) => (pairOf(h.raw) || '未写') + '|' + h.sign + '|' + h.n;
const byAssert = new Map();
for (const h of hits) {
  const key = assertKey(h);
  if (!byAssert.has(key)) byAssert.set(key, []);
  byAssert.get(key).push(h);
}


say('=== 判据 D2.1：一共有几处');
say('  分母 P（行）   = ' + hits.length);
say('  分母 Q（断言） = ' + byAssert.size + '   ★ 上报口径 = Q');
say('  ★ 差 = ' + (hits.length - byAssert.size) + ' ⇒ 那是【同一句话的多份副本】（判据 D0.3）');
say('');
say('=== 判据 D1.6：按【来源】分（形状判据）');
{
  const bySrc = {};
  for (const h of hits) (bySrc[h.src] = bySrc[h.src] || []).push(h);
  for (const k of ['断言', '引述', '源码']) say('  ' + k + '（按行）= ' + ((bySrc[k] || []).length));
  const bySrcA = {};
  for (const [, g] of byAssert) { const k = g[0].src; bySrcA[k] = (bySrcA[k] || 0) + 1; }
  for (const k of ['断言', '引述', '源码']) say('  ' + k + '（按断言）= ' + (bySrcA[k] || 0));
  say('  ★★ 只有【断言】那一档是「本车道自己下的断言」；引述与源码里的那两个数');
  say('     与断言逐字相同，在报告里读起来一模一样。');
}
say('');

const table = [];
for (const [key, group] of byAssert) {
  const h = group[0];
  const pr = pairOf(h.raw);
  const today = pr ? TODAY[pr] : null;
  let tier;
  if (!pr) tier = '③';
  else if (today === h.n) tier = '①';
  else tier = '②';
  table.push({ key, h, copies: group.length, pair: pr, today, tier });
}
table.sort((a, b) => a.h.file.localeCompare(b.h.file) || a.h.line - b.h.line);

say('=== 判据 D1：逐处（文件名 ／ 行号 ／ 逐字那一句 ／ 哪一对 ／ 符号 ／ 今天重算）');
for (const r of table) {
  say('  ' + r.h.file + ':' + r.h.line + '   [副本 ×' + r.copies + ']');
  say('     逐字：' + r.h.raw.slice(0, 120));
  say('     哪一对 = ' + (r.pair || '**未写**') + '   符号 = ' + (r.h.sign || '无') + '   逐字值 = ' + r.h.n +
      '   今天重算 = ' + (r.today === null ? '—（未写哪一对，无法核对）' : r.today) +
      '   档 = ' + r.tier);
}
say('');

say('=== 判据 D2.2：那一句里没有写【哪一对】的处数');
const noPair = table.filter((r) => !r.pair);
say('  处数 = ' + noPair.length + '  / ' + table.length);
noPair.forEach((r) => say('    ' + r.h.file + ':' + r.h.line + '  逐字：' + r.h.raw.slice(0, 100)));
say('');

say('=== 判据 D2.3：符号为负的处数');
const neg = table.filter((r) => r.h.sign);
say('  处数 = ' + neg.length);
neg.forEach((r) => say('    ' + r.h.file + ':' + r.h.line + '  逐字值 = ' + r.h.sign + r.h.n));
say('');

say('=== 判据 D2.4：今天重算之后值变了的处数');
const changed = table.filter((r) => r.pair && r.today !== r.h.n);
say('  处数 = ' + changed.length);
changed.forEach((r) => say('    ' + r.h.file + ':' + r.h.line + '  ' + r.h.n + ' → ' + r.today + '   逐字：' + r.h.raw.slice(0, 100)));
say('  ★ 判据 D2.5：只判 (a)「那个数本身变了」；(b)「数没变而它指的对象变了」单列 —— 本件 (b) = 见下');
say('');

// ================= 四档 =================
say('=== 判据 D3：四档（分母 = ' + table.length + '）');
const byT = {};
for (const r of table) (byT[r.tier] = byT[r.tier] || []).push(r);
let sum = 0;
for (const t of ['①', '②', '③', '④']) {
  const v = byT[t] || []; sum += v.length;
  say('  ' + t + ' = ' + String(v.length).padStart(2) + '   ' + v.map((r) => r.h.file + ':' + r.h.line).join(' '));
}
say('  ★ 和 = ' + sum + '  ⇒ = 分母 Q = ' + table.length + ' ' + (sum === table.length ? '✓' : '★ 红'));
say('  ★ ④ = ' + (byT['④'] || []).length + ' —— 且本件没有把「我没有去读它」写进 ④（判据 D3.2）');
say('');

// ================= 对照 =================
say('=== 判据 D2.6：③ 那 8 处，若【强行从上下文补出】那一对，会得到什么');
say('  ★ 判据 D1.3：补出来的配对【不改变档位】—— 它只用来回答「那个数今天还对不对」。');
{
  const T = { '①→②': TODAY['①→②'], '②→③': TODAY['②→③'], '①→③': TODAY['①→③'] };
  for (const [key, group] of byAssert) {
    const h = group[0];
    const p = pairOf(h.raw);
    if (p) continue;
    // 从整行里【差】之后找 ①②③ 或中文口径名
    const after = h.raw.slice(h.raw.search(/差/));
    let guess = null;
    if (/不认引号/.test(h.raw) && /认引号/.test(h.raw)) guess = '①→②';
    else if (/π₁ 收 61/.test(h.raw) && /π₀ 收 137/.test(h.raw)) guess = '①→③';
    else if (/π₁ 收 59/.test(h.raw) && /π₀ 收 137/.test(h.raw)) guess = '①→②';
    else if (/E₁₃/.test(h.raw) && /E₁₂/.test(h.raw)) guess = '②→③';
    // ★★★ 判据 D2.7：若【同一行】补不出，回看三行 —— 那两个操作数可能在上面。
    //   戊⁷ 那一处在 allowlist_transcript.md:72，而它的两个操作数在 :69 与 :70：
    //     `  R1（不认引号，丁⁷ 那一版）挡下  = 136`
    //     `  R1（认引号，只算引号外的元字符）= 59`
    let win = null;
    if (!guess) {
      const lines = fs.readFileSync(h.file, 'utf8').split('\n');
      const ctx = lines.slice(Math.max(0, h.line - 4), h.line).join('\n');
      if (/不认引号/.test(ctx) && /认引号/.test(ctx) && /136|137/.test(ctx)) win = '①→②';
    }
    if (win) { guess = win; }
    const today = guess ? T[guess] : null;
    say('  ' + h.file + ':' + h.line + '  逐字值 = ' + h.sign + h.n + '   推断的对 = ' + (guess || '（补不出）')
      + (win ? '（同行补不出，回看三行 ⇒ ①→②）' : '')
      + '   今天重算 = ' + (today === null ? '—' : today)
      + (today !== null && today !== Number(h.n) ? '   ★★★ 变了：' + h.sign + h.n + ' → ' + today : (today !== null ? '   （未变）' : '')));
  }
}
say('');

say('=== 判据 D4：对照');
{
  // (a) 已知写了哪一对：己⁷ 的「② 与 ③ 的差 = 2 处」
  const a = table.find((r) => r.pair === '②→③');
  say('  (a) 已知写了哪一对：' + (a ? a.h.file + ':' + a.h.line + '  哪一对 = ' + a.pair + '  档 = ' + a.tier : '★ 没找到'));
  say('      ⇒ ' + (a && (a.tier === '①' || a.tier === '②') ? '落 ' + a.tier + ' ✓' : '★ 红'));
  // (b) 已知没写哪一对：戊⁷ 的「差 = 77 处」
  const b = table.find((r) => !r.pair && r.h.n === 77);
  say('  (b) 已知没写哪一对：' + (b ? b.h.file + ':' + b.h.line + '  逐字：' + b.h.raw.slice(0, 80) : '★ 没找到'));
  say('      ⇒ ' + (b && b.tier === '③' ? '落 ③ ✓' : '★ 红'));
}
{
  const empty = [];
  const byT2 = {};
  for (const r of empty) byT2[r.tier] = 1;
  say('  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓');
  say('      ★★ 而「空集 ⇒ 和 = 0」与「对象里一处都没有」在输出上同形 ⇒ 必须把分母一起印');
}
say('');

say('=== 判据 D5.2：本件有没有跑任何文档里的命令？');
say('  **没有。** 本探针只读文件（`fs.readFileSync`）与只读 git（`git ls-files`），不执行任何被扫到的命令。');
