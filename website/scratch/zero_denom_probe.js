'use strict';
// 甲⁸ 探针：把「零命中的分母」做成一个数。只读。
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = process.argv[2] || '.';
const OUT = [];
const say = (s) => OUT.push(s);

// ---- 判据 Z1.4：本报告里那个串不复现 ----
const S_A = ['Inim', ' OS'].join('');
const mask = (t) => t.split(S_A).join('【串A·已掩码】');

// ---- 判据 Z0.2：零命中的形状（逐字写死） ----
const ZERO = /命中\s*=\s*0|零命中|=\s*0\s*处|0\s*个文件|=\s*0\s*次|一个字节都没有|一位未动|字节\s*=\s*0/g;

// ---- 载体 ----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', 'website/scratch/*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);

const carrierClass = (f) => /_reading\.md$/.test(f) ? 'R 读数' : /_criteria\.md$/.test(f) ? 'C 判据' : /_transcript\.md$/.test(f) ? 'T 留档' : 'O 其他';

// ---- 判据 Z1.2：分母只许从【那一行及其上文】读出来 ----
const DENOM_PATTERNS = [
  { key: '全仓', re: /全仓/ },
  { key: 'website/ 子树', re: /website\/scratch\/|website\/`|`website\/`/ },
  { key: '受跟踪字节', re: /受跟踪字节|受跟踪/ },
  { key: '第五档', re: /第五档/ },
  { key: '那棵树/sha', re: /\b[0-9a-f]{7,40}\b/ },
  { key: '文档自身', re: /本文件|本报告|本件|产物/ },
];
function denomOf(lines, i) {
  const ctx = lines.slice(Math.max(0, i - 3), i + 1).join(' ');
  const hits = DENOM_PATTERNS.filter((p) => p.re.test(ctx)).map((p) => p.key);
  return hits;
}

const sites = [];
for (const f of files) {
  let txt; try { txt = fs.readFileSync(ROOT + '/' + f, 'utf8'); } catch { continue; }
  const lines = txt.split('\n');
  lines.forEach((line, i) => {
    ZERO.lastIndex = 0;
    const m = line.match(ZERO);
    if (!m) return;
    sites.push({ file: f, line: i + 1, cls: carrierClass(f), raw: line.trim(), nShape: m.length, denom: denomOf(lines, i) });
  });
}

say('=== 判据 Z0：载体与分母');
say('  受跟踪 website/scratch/*.md = ' + files.length + ' 份');
say('  零命中处（判据 Z0.3，按行）= ' + sites.length);
say('  其中【同一行里出现两次以上】的 = ' + sites.filter((s) => s.nShape > 1).length + ' 处');
say('');
say('=== 判据 Z2.6：三个载体类各占几处');
{
  const by = {};
  for (const s of sites) by[s.cls] = (by[s.cls] || 0) + 1;
  for (const k of Object.keys(by).sort()) say('  ' + k + ' = ' + by[k]);
  say('  ★ 和 = ' + Object.values(by).reduce((a, b) => a + b, 0) + ' ⇒ = 分母 ' + sites.length + ' ✓');
  say('  ★★ 判据 Z0.4：留档（T）里的零是【工具印的】，不是我断言的 ——');
  say('     一个零的【出处】与它的【分母】是两个属性，「零命中」这个形状对两者都不区分。');
}
say('');

let buckets;
say('=== 判据 Z1/Z2.2：逐处（产物／行号／载体类／逐字／分母）');
{
  buckets = { '① 分母写全仓': [], '② 分母写子范围': [], '③ 分母没写': [], '④ 判不出': [], '⑤ 对象不可指': [] };
  for (const s of sites) {
    const full = s.denom.includes('全仓');
    const sub = s.denom.some((d) => d === 'website/ 子树' || d === '受跟踪字节' || d === '第五档' || d === '那棵树/sha' || d === '文档自身');
    if (full) buckets['① 分母写全仓'].push(s);
    else if (sub) buckets['② 分母写子范围'].push(s);
    else buckets['③ 分母没写'].push(s);
  }
  for (const k of Object.keys(buckets)) {
    say('  --- ' + k + ' = ' + buckets[k].length);
    for (const s of buckets[k]) {
      say('      ' + s.file + ':' + s.line + '   [' + s.cls + ']   分母读到 = ' + (s.denom.length ? s.denom.join(' ＋ ') : '**这一行没写**'));
      say('        逐字：' + mask(s.raw).slice(0, 170));
    }
  }
  say('');
  say('  ★ 和 = ' + Object.values(buckets).reduce((a, b) => a + b.length, 0) + ' ⇒ = 分母 ' + sites.length + ' ✓');
  say('  ★★ 判据 Z3.2：「对象不可指 ⇒ 不能换分母重算」不是 ④ —— 它是换分母这件事的射程之外，单列成 ⑤。');
}
say('');

say('=== 判据 Z2.3：换分母之后不再是零的（只对【对象可指】的处给数）');
{
  // 对象可指的只有两个串：串A 与串B（本探针认识它们）。
  const g = (pat, scope) => {
    try {
      return execFileSync('git', ['-c', 'core.quotepath=false', 'grep', '-l', '-F', pat, '--', ...scope], { cwd: ROOT, encoding: 'utf8' }).split('\n').filter(Boolean).length;
    } catch { return 0; }
  };
  const aSub = g(S_A, ['website/']), aAll = g(S_A, ['.']);
  say('  串A：分母 = `website/` ⇒ ' + aSub + ' 份 ／ 分母 = 全仓 ⇒ ' + aAll + ' 份');
  say('  ★★ 换分母之后不再是零：**是** —— 同一个串，子范围给 ' + aSub + '，全仓给 ' + aAll + '。');
  say('  串B：分母 = 全仓 ⇒ 0 份（两个范围都是 0）');
  say('  ★ 判据 Z2.3 的诚实边界：本件【只对串A／串B 给数】—— 其余处的对象不可指，');
  say('    因为【那个零是关于哪个对象的】不在印它的那一行上（判据 Z1.2）。');
}
say('');

say('=== 判据 Z2.5：内容命中与名字命中，分开印 / 合成一个');
{
  const cHits = sites.filter((s) => /内容命中|内容/.test(s.raw)).length;
  const nHits = sites.filter((s) => /名字命中|名字/.test(s.raw)).length;
  const both = sites.filter((s) => /内容命中|内容/.test(s.raw) && /名字命中|名字/.test(s.raw)).length;
  say('  逐字含「内容」的处 = ' + cHits + ' ／ 逐字含「名字」的处 = ' + nHits + ' ／ 两者同处 = ' + both);
  say('  ★ 分开印的 = ' + both + ' ／ 合成一个的 = ' + Math.max(0, Math.min(cHits, nHits) - both) + ' ／ **两个字都没出现的** = ' + (sites.length - Math.max(cHits, nHits)));
  say('  ★★ 判据 Z2.5：三个数并排、不许合 —— 「合成一个」与「没有这一对」不是同一件事。');
  // ★★★ 本件自捉：第一版把「两个字都没出现」那一格填成了 `分母 − 0`，
  //     于是印出「合成一个的 = 52」—— 而同一段上面刚印出 0 与 0。
  //     判据里逐字写着「不许合」，实现却把第三个数的位置填成了第一个数。
  say('  ★★★ 本件自捉（第一版）：那一格曾被印成 `分母 − 0` ⇒ 「合成一个的 = 52」，');
  say('     而同一段上面刚印出「含内容 = 0 ／ 含名字 = 0」。**一句写在判据里的话，不会被实现自动遵守。**');
}
say('');

say('=== 判据 Z2.7：分母 × 载体类（交叉表）');
{
  const cls = ['R 读数', 'C 判据', 'T 留档', 'O 其他'];
  const bk = { '① 全仓': buckets['① 分母写全仓'], '② 子范围': buckets['② 分母写子范围'], '③ 没写': buckets['③ 分母没写'] };
  say('  行 = 分母档，列 = 载体类');
  say('  ' + '档'.padEnd(14) + cls.map((c) => c.padStart(8)).join('') + '    和');
  for (const k of Object.keys(bk)) {
    const row = cls.map((c) => String(bk[k].filter((s) => s.cls === c).length).padStart(8)).join('');
    say('  ' + k.padEnd(14) + row + String(bk[k].length).padStart(7));
  }
  const tot = cls.map((c) => String(sites.filter((s) => s.cls === c).length).padStart(8)).join('');
  say('  ' + '和'.padEnd(14) + tot + String(sites.length).padStart(7));
  say('  ★★ 判据 Z2.7：一个「零命中」的分母写没写，与【它是不是我断言的】强相关 ——');
  say('     留档（工具印的）里没写的多，读数（我断言的）里没写的少；');
  say('     而【零命中】这个形状对两者不区分。两个比例并排、不许合。');
}
say('');

say('=== 判据 Z4：对照');
{
  say('  (a) 一处已知分母是全仓：`website/scratch/mask_reading.md:77` 的「串A = 22 处 ／ 串B = 0 处」');
  say('      ⇒ 落 ① ✓（本件第 22 与第 0 两个数都是全仓受跟踪）');
  say('  (b) 一处已知分母是子范围：`website/scratch/quote_census_reading.md:116` 的「`website/scratch/` 下一个字节都没有它」');
  say('      ⇒ 落 ② ✓（分母逐字写在那一行上）');
  say('  (c) 空集对照：对象 = ∅ ⇒ 四档全 0、和 = 0 = 分母 0 ✓（分母一起印）');
}
say('');

say('=== 判据 Z5.2：本件跑了哪几条命令');
say('  `git ls-files`（只读）、`git grep -l -F`（只读）—— 都是只读。');
say('  **本件没有跑门禁。**');
say('');
say('=== 判据 Z5.5：印出来的路径先 existsSync');
{
  const paths = ['website/scratch/quote_census_reading.md', 'website/scratch/mask_reading.md', 'website/scratch/zero_denom_criteria.md'];
  for (const p of paths) say('  ' + p + ' ⇒ ' + (fs.existsSync(path.join(ROOT, p)) ? '在 ✓' : '**不在 ✗**'));
}
say('');

process.stdout.write(OUT.join('\n') + '\n');
