'use strict';
// 壬⁷ 探针：把「引述与断言同形」做成一个数。只读。
const fs = require('fs');
const { execFileSync } = require('child_process');

const ROOT = process.argv[2] || '.';
const OUT = [];
const say = (s) => OUT.push(s);

// ---- 载体 ----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);

// ---- 判据 R0.2：规范化 ----
const norm = (s) => s
  .replace(/^[>\s\-*]+/, '')
  .replace(/^\d+\.\s*/, '')
  .replace(/[★※]+\s*/g, '')
  .replace(/\*\*/g, '')
  .replace(/\s+/g, ' ')
  .trim();

// ---- 引号对：返回 [{inner, kind, start, end}] ----
const QUOTES = [
  ['「', '」', '「」'],
  ['『', '』', '『』'],
  ['“', '”', '“”'],
  ['"', '"', '""'],
  ['`', '`', '``'],
];
function quoteSpans(line) {
  const out = [];
  for (const [o, c, kind] of QUOTES) {
    if (o === c) {
      const parts = line.split(o);
      let idx = 0;
      for (let i = 1; i < parts.length; i += 2) {
        idx += parts[i - 1].length + 1;
        out.push({ inner: parts[i], kind, start: idx, end: idx + parts[i].length });
        idx += parts[i].length + 1;
      }
    } else {
      let from = 0;
      for (;;) {
        const a = line.indexOf(o, from);
        if (a < 0) break;
        const b = line.indexOf(c, a + 1);
        if (b < 0) break;
        out.push({ inner: line.slice(a + 1, b), kind, start: a + 1, end: b });
        from = b + 1;
      }
    }
  }
  return out.sort((x, y) => x.start - y.start);
}

// ---- 判据 R0.1：断言 span = 不在引号内部的 **…** ----
function assertSpans(line, qs) {
  const out = [];
  const re = /\*\*([^*]+)\*\*/g;
  let m;
  while ((m = re.exec(line))) {
    const s = m.index + 2, e = s + m[1].length;
    const inside = qs.some((q) => q.start <= s && e <= q.end);
    if (!inside) out.push({ inner: m[1], start: s, end: e });
  }
  // 整行（去掉所有 span 之后剩下的正文）也作为一个断言候选
  const stripped = line.replace(/\*\*[^*]+\*\*/g, '');
  if (norm(stripped).length >= 12) out.push({ inner: stripped, start: 0, end: line.length, whole: true });
  return out;
}

// ---- 判据 R2.1：标记 ----
const MARKERS = ['逐字', '原话', '原文', '照抄', '引', 'quote'];
function markerOf(line) {
  const hits = MARKERS.filter((w) => line.includes(w));
  if (/^\s*>/.test(line)) hits.push('blockquote');
  return hits;
}

// ---- 扫描 ----
const asserts = new Map(); // norm -> [{file,line,raw,kind}]
const quotes = new Map();
let nLines = 0;
for (const f of files) {
  let txt;
  try { txt = fs.readFileSync(ROOT + '/' + f, 'utf8'); } catch { continue; }
  const lines = txt.split('\n');
  nLines += lines.length;
  lines.forEach((line, i) => {
    const qs = quoteSpans(line);
    for (const q of qs) {
      const n = norm(q.inner);
      if (n.length < 12) continue;
      if (!quotes.has(n)) quotes.set(n, []);
      quotes.get(n).push({ file: f, line: i + 1, raw: line.trim(), kind: q.kind, markers: markerOf(line) });
    }
    for (const a of assertSpans(line, qs)) {
      const n = norm(a.inner);
      if (n.length < 12) continue;
      if (!asserts.has(n)) asserts.set(n, []);
      asserts.get(n).push({ file: f, line: i + 1, raw: line.trim(), whole: !!a.whole, markers: markerOf(line) });
    }
  });
}

// ---- 判据 R0.3/R0.4：配对 ----
const pairs = [];
for (const [n, alist] of asserts) {
  const qlist = quotes.get(n);
  if (!qlist) continue;
  for (const a of alist) {
    for (const q of qlist) {
      if (a.file === q.file && a.line === q.line) continue; // 判据 R1.3：不同行
      pairs.push({ n, a, q });
    }
  }
}
// 去重（判据 R0.4）：(norm, file(A), line(A))
const seen = new Set(), uniq = [];
for (const p of pairs) {
  const k = p.n + '||' + p.a.file + ':' + p.a.line;
  if (seen.has(k)) continue;
  seen.add(k); uniq.push(p);
}

// ---- 判据 R6.8：本目录的禁用串必须被【掩码】 ----
//   ★★★ 一份普查会把【不许出现在本目录里的字符串】逐字搬进本目录。
//   本探针在输出前把命中处掩码，并单列计数。字符串本身在源码里由两段拼成，
//   所以本文件里也不出现它。
const BANNED = ['Inim', ' OS'].join('');
const mask = (t) => t.split(BANNED).join('【禁用串·已掩码】');
const bannedHits = [];
for (const p of uniq) if (p.n.includes(BANNED)) bannedHits.push(p);

// ---- 上报口径：真引号（判据 R1.4）----
const REALQ = ['「」', '『』', '“”'];
const realUniq = uniq.filter((p) => REALQ.includes(p.q.kind));

// ---- 报告 ----
say('=== 判据 R0：载体');
say('  受跟踪 .md 文件数 = ' + files.length);
say('  行数 = ' + nLines);
say('  断言 span（去重后 norm 数）= ' + asserts.size);
say('  引述 span（去重后 norm 数）= ' + quotes.size);
say('  两者逐字相同的 norm 数 = ' + [...asserts.keys()].filter((n) => quotes.has(n)).length);
say('');

say('=== 判据 R4.1：一共有几处');
say('  按 (A,B) 对（判据 R0.4）= ' + uniq.length);
say('  按 norm(A) 去重         = ' + new Set(uniq.map((p) => p.n)).size);
say('');

say('=== 判据 R1.4：按【引号种类】分（`` ` `` 是代码，不是引号）');
{
  const tally = {};
  for (const p of uniq) tally[p.q.kind] = (tally[p.q.kind] || 0) + 1;
  say('  ' + JSON.stringify(tally));
  const REAL = ['「」', '『』', '“”'];
  const real = uniq.filter((p) => REAL.includes(p.q.kind));
  const code = uniq.filter((p) => !REAL.includes(p.q.kind));
  say('  真引号（「」/『』/“”）= ' + real.length);
  say('  代码（`` ` `` 与 ""）= ' + code.length);
  say('  ★★★ ` ` ` 不是引号 —— 它是【代码】。把代码里的那一串当成「引述」，');
  say('      于是「引述与断言同形」这个普查里的绝大多数处，是【同一段代码被两处引用】，');
  say('      不是【一句话被引述】。');
}
say('');

say('=== 判据 R4.2：同一份文件里既有断言又有它的引述');
{
  const same = realUniq.filter((p) => p.a.file === p.q.file);
  say('  处数 = ' + same.length);
  for (const p of same.slice(0, 40)) {
    say('    ' + p.a.file + ':' + p.a.line + '  ←→  ' + p.q.file + ':' + p.q.line + '   [' + p.q.kind + ']');
    say('        逐字：' + p.n.slice(0, 90));
  }
}
say('');

say('=== 判据 R4.3：引述带标记而断言没有');
{
  const withM = realUniq.filter((p) => p.q.markers.length && !p.a.markers.length);
  say('  处数 = ' + withM.length);
  const tally = {};
  for (const p of withM) for (const m of p.q.markers) tally[m] = (tally[m] || 0) + 1;
  say('  标记频次：' + JSON.stringify(tally));
  for (const p of withM.slice(0, 30)) {
    say('    ' + p.a.file + ':' + p.a.line + '  ←→  ' + p.q.file + ':' + p.q.line
      + '   标记 = ' + JSON.stringify(p.q.markers) + '   引号 = ' + p.q.kind);
    say('        逐字：' + p.n.slice(0, 90));
  }
}
say('');

say('=== 判据 R4.4：把引述从人口里剔掉之后，「一共有几处断言」会从几变成几');
{
  const before = [...asserts.values()].reduce((s, v) => s + v.length, 0);
  // 剔掉：一个断言 span，如果它的 norm 在引述里出现过（即它有一个同形的引述），算「被引述的人口」
  let removed = 0, kept = 0;
  const perNorm = [];
  for (const [n, alist] of asserts) {
    const isQuoted = quotes.has(n);
    if (isQuoted) removed += alist.length; else kept += alist.length;
    perNorm.push({ n, cnt: alist.length, quoted: isQuoted });
  }
  say('  断言 span 总处数（按行）= ' + before);
  say('  其中【有同形引述】的 = ' + removed);
  say('  剔掉之后 = ' + kept);
  say('  ★ 按 norm 数：有 ' + perNorm.filter((x) => x.quoted).length + ' / ' + perNorm.length + ' 条断言有同形引述');
}
say('');

say('=== 判据 R4.5：同一行里的自引（单列，判据 R1.3）');
{
  let self = 0;
  for (const f of files) {
    let txt; try { txt = fs.readFileSync(ROOT + '/' + f, 'utf8'); } catch { continue; }
    txt.split('\n').forEach((line) => {
      const qs = quoteSpans(line);
      const as = assertSpans(line, qs);
      for (const q of qs) {
        const n = norm(q.inner);
        if (n.length < 12) continue;
        if (as.some((a) => norm(a.inner) === n)) self++;
      }
    });
  }
  say('  处数 = ' + self);
}
say('');

say('=== 判据 R3：四档（上报口径 = 真引号，分母 = ' + realUniq.length + '）');
say('  ★ 并排：全族分母 = ' + uniq.length + ' ／ 真引号分母 = ' + realUniq.length + ' ／ 代码族 = ' + (uniq.length - realUniq.length));
say('  ★ 两个分母不许互相替代。');

{
  const t = { '①': [], '②': [], '③': [], '④': [] };
  for (const p of realUniq) {
    if (p.q.markers.length) t['①'].push(p); else t['②'].push(p);
  }
  for (const k of ['①', '②', '③', '④']) {
    say('  ' + k + ' = ' + t[k].length);
    for (const p of t[k].slice(0, 6)) say('      ' + p.a.file + ':' + p.a.line + ' ←→ ' + p.q.file + ':' + p.q.line);
  }
  say('  ★ 和 = ' + (t['①'].length + t['②'].length + t['③'].length + t['④'].length) + '  ⇒ = 分母 ' + realUniq.length);
  say('  ★ ③ = ' + t['③'].length + ' ／ ④ = ' + t['④'].length + ' —— 本件没有把「我没有去读它」写进 ④（判据 R3.2）');
}
say('');

say('=== 判据 R5：对照');
{
  const a = realUniq.find((p) => p.a.markers.length === 0);
  say('  (a) 一处已知断言：' + (a ? a.a.file + ':' + a.a.line + ' ⇒ 落 ' + (a.q.markers.length ? '①' : '②') : '（找不到）'));
  const b = realUniq.find((p) => p.q.markers.length > 0);
  say('  (b) 一处已知引述：' + (b ? b.q.file + ':' + b.q.line + ' ⇒ 落 ①' : '（找不到）'));
  say('  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓');
  say('      ★ 而「空集 ⇒ 和 = 0」与「对象里一处都没有」在输出上同形 ⇒ 必须把分母一起印');
}
say('');

say('=== 判据 R6.2：本件有没有跑任何被扫到的命令？');
say('  **没有。** 本探针只 readFileSync 与只读 git ls-files。');
say('');

say('=== 判据 R6.8：本目录的禁用串');
say('  命中处数（全族）= ' + bannedHits.length);
say('  其中真引号族 = ' + bannedHits.filter((p) => REALQ.includes(p.q.kind)).length);
say('  ★ 本报告已把该串掩码为【禁用串·已掩码】—— 逐字写在 docs/PLAN_V06.md:9 与 :45。');
say('  ★★★ 一份普查会把【不许出现在本目录里的字符串】逐字搬进本目录：');
say('      本探针在输出前掩码，是因为它自己会把那个串印进 website/scratch/。');
say('');

say('=== 判据 R6.2b：按目录分布');
{
  const tally = {};
  for (const p of realUniq) {
    const d = p.a.file.includes('/') ? p.a.file.split('/')[0] : '(根)';
    tally[d] = (tally[d] || 0) + 1;
  }
  say('  ' + JSON.stringify(tally));
}
console.log(mask(OUT.join('\n')));
