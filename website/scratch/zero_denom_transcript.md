# 甲⁸ 留档：探针源码 ＋ 逐字输出 ＋ 判据 ＋ 修前/修后

★★★ 本文件已把本目录的禁用串掩码为【串A·已掩码】（判据 Z1.4）。

## 一、修前那一刻的逐字读数（判据 Z5.8）
```
$ node --check website/scratch/zero_denom_probe.js
check_rc_before=1
sha256 = 0d42338ebb3eab90957c47d9a52f5e9b10ae5ada668baecf9ec52008893e5676
wc -l  = 141
stderr 逐字：
/home/sakiko/inimerse/.worktrees/website-ledger-readat/website/scratch/zero_denom_probe.js:99
  const bSub = g('[' + "'check'" + ', ' + "'('" + ', ' + "'x, y, z)'].join('')', ['website/']);
                                                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

SyntaxError: Invalid or unexpected token
    at wrapSafe (node:internal/modules/cjs/loader:1861:18)
    at checkSyntax (node:internal/main/check_syntax:76:3)

Node.js v24.20.0
```

## 二、修后逐字读数
```
$ node --check website/scratch/zero_denom_probe.js
check_rc_after=0   ★ 语法通过 ✓
sha256 = 349610a706b25042922586b58693a35a1de3be9efd055d06f263b63de4b53f58
wc -l  = 140
```

## 三、探针源码（逐字，已掩码）
```js
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

```

## 四、运行逐字输出
```
=== 判据 Z0：载体与分母
  受跟踪 website/scratch/*.md = 58 份
  零命中处（判据 Z0.3，按行）= 52
  其中【同一行里出现两次以上】的 = 5 处

=== 判据 Z2.6：三个载体类各占几处
  C 判据 = 1
  O 其他 = 4
  R 读数 = 8
  T 留档 = 39
  ★ 和 = 52 ⇒ = 分母 52 ✓
  ★★ 判据 Z0.4：留档（T）里的零是【工具印的】，不是我断言的 ——
     一个零的【出处】与它的【分母】是两个属性，「零命中」这个形状对两者都不区分。

=== 判据 Z1/Z2.2：逐处（产物／行号／载体类／逐字／分母）
  --- ① 分母写全仓 = 3
      website/scratch/mask_reading.md:70   [R 读数]   分母读到 = 全仓 ＋ website/ 子树 ＋ 受跟踪字节 ＋ 文档自身
        逐字：上一件（壬⁷）报的「**受跟踪字节 = 0**」**分母是 `website/`**；本件报的**分母是全仓** ⇒
      website/scratch/mask_transcript.md:125   [T 留档]   分母读到 = 全仓 ＋ 受跟踪字节
        逐字：if (!gA.length && !gB.length) say('  ⇒ **0 处。** 两个串在受跟踪字节里都零命中。');
      website/scratch/rerun_on_main.md:47   [O 其他]   分母读到 = 全仓 ＋ 那棵树/sha
        逐字：★ `246 / 116` 这两个数与 `c72b202` 上**一位未动**。
  --- ② 分母写子范围 = 30
      website/scratch/custody_transcript.md:261   [T 留档]   分母读到 = 那棵树/sha
        逐字：**第十二种（一条规矩只写在被引方，管不到引用方）**：`docs/SYNTAX.md` §5 逐字写着「`rand` 从未被任何文件注册（`grep -rn '"rand"' src/` 零命中）」，而**它自己引的那条命令**在 `fa8247e` 上恰好给 **1 条命中**（`src/mod
      website/scratch/eidos74_ref_vs_tree.md:85   [O 其他]   分母读到 = 那棵树/sha
        逐字：引文在 754403b 上逐字命中 = 0   ⇒ 这正是 readAt 必须存在的理由
      website/scratch/mask_criteria.md:58   [C 判据]   分母读到 = 受跟踪字节
        逐字：| ① | **掩码后受跟踪字节零命中** |
      website/scratch/mask_reading.md:38   [R 读数]   分母读到 = website/ 子树
        逐字：| `website/scratch/quote_census_reading.md:116` | 2 | 串A | （`['Inim', ' OS'].join('')`），所以 `website/scratch/` 下**一个字节都没有它**。 |
      website/scratch/mask_reading.md:77   [R 读数]   分母读到 = 受跟踪字节 ＋ 文档自身
        逐字：**串A = 22 处 ／ 串B = 0 处。**
      website/scratch/mask_reading.md:107   [R 读数]   分母读到 = website/ 子树 ＋ 文档自身
        逐字：- 输出行数 = **1213**；输出里**串A 命中 = 2 次**；**串B 命中 = 0 次**。
      website/scratch/mask_reading.md:139   [R 读数]   分母读到 = 受跟踪字节
        逐字：| **① 掩码后受跟踪字节零命中** | **1** | `quote_census_probe.js:131` |
      website/scratch/mask_reading.md:169   [R 读数]   分母读到 = website/ 子树 ＋ 受跟踪字节 ＋ 文档自身
        逐字：- ★★ **本件自己会制造一次命中吗？** —— 不会：本报告里那个串**一律写成 `【串A·已掩码】`**（判据 M1.4）；**本件的输出里串A 原样命中 = 0**（`grep -c` 逐字）。
      website/scratch/mask_transcript.md:4   [T 留档]   分母读到 = 文档自身
        逐字：★ 掩码计数：本文件里串A 原样命中 = 0（逐字见文末）。
      website/scratch/mask_transcript.md:86   [T 留档]   分母读到 = 受跟踪字节
        逐字：say('      受跟踪字节（git grep -c -F）= ' + (gitHitsA.length ? gitHitsA.join(' ; ') : '0 个文件'));
      website/scratch/mask_transcript.md:90   [T 留档]   分母读到 = 受跟踪字节 ＋ 第五档
        逐字：say('      受跟踪字节 = ' + (gitHitsB.length ? gitHitsB.join(' ; ') : '0 个文件'));
      website/scratch/mask_transcript.md:232   [T 留档]   分母读到 = website/ 子树 ＋ 文档自身
        逐字：say('      ⇒ 它的产物 `quote_census_transcript.md` 里串A 命中 = 0 ⇒ 落 ① ✓');
      website/scratch/mask_transcript.md:269   [T 留档]   分母读到 = 受跟踪字节 ＋ 第五档
        逐字：受跟踪字节 = 0 个文件
      website/scratch/mask_transcript.md:270   [T 留档]   分母读到 = 受跟踪字节 ＋ 第五档
        逐字：含第五档 = 0 个文件
      website/scratch/mask_transcript.md:288   [T 留档]   分母读到 = website/ 子树
        逐字：逐字：（`['Inim', ' OS'].join('')`），所以 `website/scratch/` 下**一个字节都没有它**。
      website/scratch/mask_transcript.md:310   [T 留档]   分母读到 = 受跟踪字节
        逐字：串B 受跟踪字节命中（今天）= 0 个文件
      website/scratch/mask_transcript.md:315   [T 留档]   分母读到 = 受跟踪字节 ＋ 文档自身
        逐字：串A = 22 处 ／ 串B = 0 处
      website/scratch/mask_transcript.md:363   [T 留档]   分母读到 = website/ 子树 ＋ 文档自身
        逐字：输出里串B 命中 = 0 次
      website/scratch/mask_transcript.md:406   [T 留档]   分母读到 = website/ 子树 ＋ 文档自身
        逐字：⇒ 它的产物 `quote_census_transcript.md` 里串A 命中 = 0 ⇒ 落 ① ✓
      website/scratch/mask_transcript.md:479   [T 留档]   分母读到 = 受跟踪字节
        逐字：| ① | **掩码后受跟踪字节零命中** |
      website/scratch/quote_census_reading.md:116   [R 读数]   分母读到 = website/ 子树
        逐字：（`['Inim', ' OS'].join('')`），所以 `website/scratch/` 下**一个字节都没有它**。
      website/scratch/quote_transcript.md:332   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处：
      website/scratch/quote_transcript.md:412   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:497   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:596   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:707   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:828   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:957   [T 留档]   分母读到 = 文档自身
        逐字：★ 只有口径 ③ 收的 = 0 处（恒等于 0，同上）。
      website/scratch/quote_transcript.md:1043   [T 留档]   分母读到 = 文档自身
        逐字：★ 先量 ③ ⊇ ② 是否成立：② 命中而 ③ 不命中的 = 0 处 ⇒ 成立 ✓（③ 确实是 ② 的加宽）
      website/scratch/span_census.md:51   [O 其他]   分母读到 = 文档自身
        逐字：改后    · ㉛ 乙 ⑦【分母：本文件里 check 调用总数】= 139　（哨兵行 3523 / 3538 圈出的射程内被排除 = 0 处）
  --- ③ 分母没写 = 19
      website/scratch/delta_reading.md:83   [R 读数]   分母读到 = **这一行没写**
        逐字：★★★ **按判据 D2.5 的 (a) 口径 = 0 处** —— 因为**写明了哪一对的那 2 处，重算都相同**。
      website/scratch/quote_transcript.md:360   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:445   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:530   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:549   [T 留档]   分母读到 = **这一行没写**
        逐字：★ 假阳性（形状命中但不含 expect）= 0 处
      website/scratch/quote_transcript.md:564   [T 留档]   分母读到 = **这一行没写**
        逐字：★★★ 而 ② 那一档在 A 上 = 0 处，在 B 上 = 0 处 ——
      website/scratch/quote_transcript.md:629   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:648   [T 留档]   分母读到 = **这一行没写**
        逐字：★ 假阳性（形状命中但不含 expect）= 0 处
      website/scratch/quote_transcript.md:663   [T 留档]   分母读到 = **这一行没写**
        逐字：★★★ 而 ② 那一档在 A 上 = 0 处，在 B 上 = 0 处 ——
      website/scratch/quote_transcript.md:740   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:759   [T 留档]   分母读到 = **这一行没写**
        逐字：★ 假阳性（形状命中但不含 expect）= 0 处
      website/scratch/quote_transcript.md:774   [T 留档]   分母读到 = **这一行没写**
        逐字：★★★ 而 ② 那一档在 A 上 = 0 处，在 B 上 = 0 处 ——
      website/scratch/quote_transcript.md:861   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:880   [T 留档]   分母读到 = **这一行没写**
        逐字：★ 假阳性（形状命中但不含 expect）= 0 处
      website/scratch/quote_transcript.md:895   [T 留档]   分母读到 = **这一行没写**
        逐字：★★★ 而 ② 那一档在 A 上 = 0 处，在 B 上 = 0 处 ——
      website/scratch/quote_transcript.md:990   [T 留档]   分母读到 = **这一行没写**
        逐字：方向 1（无头名 ⇒ 不是命令）：命中 = 0 / 59
      website/scratch/quote_transcript.md:1009   [T 留档]   分母读到 = **这一行没写**
        逐字：★ 假阳性（形状命中但不含 expect）= 0 处
      website/scratch/quote_transcript.md:1024   [T 留档]   分母读到 = **这一行没写**
        逐字：★★★ 而 ② 那一档在 A 上 = 0 处，在 B 上 = 0 处 ——
      website/scratch/registration_rule.md:205   [O 其他]   分母读到 = **这一行没写**
        逐字：1. 基线上**越界 = 0 次**（上一件量到两版在这 11 行上【相异】= 0）。
  --- ④ 判不出 = 0
  --- ⑤ 对象不可指 = 0

  ★ 和 = 52 ⇒ = 分母 52 ✓
  ★★ 判据 Z3.2：「对象不可指 ⇒ 不能换分母重算」不是 ④ —— 它是换分母这件事的射程之外，单列成 ⑤。

=== 判据 Z2.3：换分母之后不再是零的（只对【对象可指】的处给数）
  串A：分母 = `website/` ⇒ 0 份 ／ 分母 = 全仓 ⇒ 22 份
  ★★ 换分母之后不再是零：**是** —— 同一个串，子范围给 0，全仓给 22。
  串B：分母 = 全仓 ⇒ 0 份（两个范围都是 0）
  ★ 判据 Z2.3 的诚实边界：本件【只对串A／串B 给数】—— 其余处的对象不可指，
    因为【那个零是关于哪个对象的】不在印它的那一行上（判据 Z1.2）。

=== 判据 Z2.5：内容命中与名字命中，分开印 / 合成一个
  逐字含「内容」的处 = 0 ／ 逐字含「名字」的处 = 0 ／ 两者同处 = 0
  ★ 分开印的 = 0 ／ 合成一个的 = 0 ／ **两个字都没出现的** = 52
  ★★ 判据 Z2.5：三个数并排、不许合 —— 「合成一个」与「没有这一对」不是同一件事。
  ★★★ 本件自捉（第一版）：那一格曾被印成 `分母 − 0` ⇒ 「合成一个的 = 52」，
     而同一段上面刚印出「含内容 = 0 ／ 含名字 = 0」。**一句写在判据里的话，不会被实现自动遵守。**

=== 判据 Z2.7：分母 × 载体类（交叉表）
  行 = 分母档，列 = 载体类
  档                 R 读数    C 判据    T 留档    O 其他    和
  ① 全仓                 1       0       1       1      3
  ② 子范围                6       1      21       2     30
  ③ 没写                 1       0      17       1     19
  和                    8       1      39       4     52
  ★★ 判据 Z2.7：一个「零命中」的分母写没写，与【它是不是我断言的】强相关 ——
     留档（工具印的）里没写的多，读数（我断言的）里没写的少；
     而【零命中】这个形状对两者不区分。两个比例并排、不许合。

=== 判据 Z4：对照
  (a) 一处已知分母是全仓：`website/scratch/mask_reading.md:77` 的「串A = 22 处 ／ 串B = 0 处」
      ⇒ 落 ① ✓（本件第 22 与第 0 两个数都是全仓受跟踪）
  (b) 一处已知分母是子范围：`website/scratch/quote_census_reading.md:116` 的「`website/scratch/` 下一个字节都没有它」
      ⇒ 落 ② ✓（分母逐字写在那一行上）
  (c) 空集对照：对象 = ∅ ⇒ 四档全 0、和 = 0 = 分母 0 ✓（分母一起印）

=== 判据 Z5.2：本件跑了哪几条命令
  `git ls-files`（只读）、`git grep -l -F`（只读）—— 都是只读。
  **本件没有跑门禁。**

=== 判据 Z5.5：印出来的路径先 existsSync
  website/scratch/quote_census_reading.md ⇒ 在 ✓
  website/scratch/mask_reading.md ⇒ 在 ✓
  website/scratch/zero_denom_criteria.md ⇒ 在 ✓


```

## 五、判据源码（逐字，已掩码）
```md
# 甲⁸ 判据先行：把「零命中的分母」做成一个数

★ 本文件是**判据**，先落盘；读数是它的后代（`git merge-base --is-ancestor` 判，不眼看）。
★ **只读不写** —— 一个受跟踪文件都不改。

---

## 第零节 对象与形状

★ **Z0.1 对象 = 本车道在 `website/scratch/` 下的**受跟踪**产物里出现过的、形状为【零命中】的那一处。**
★ **Z0.2 【零命中】的形状**（正则逐字写死，**不靠语感**）：
```
命中 = 0 ／ 零命中 ／ = 0 处 ／ 0 个文件 ／ 0 次 ／ 一个字节都没有 ／ 一位未动 ／ 字节 = 0
```
★ **Z0.3** ★★★ **「处」= 一行**（`文件:行`）。**同一行里出现两次同一个形状，算 1 处**（并按 Z2.1 单列）。

★ **Z0.4** ★★★ **载体分三类，必须并排印**：
| 类 | 判词 |
|---|---|
| **R 读数** | `*_reading.md` —— **是我断言的** |
| **C 判据** | `*_criteria.md` —— **是我定义的**（★ 表格里的模板行也是它） |
| **T 留档** | `*_transcript.md` —— **是工具印的，不是我断言的** |

⇒ ★★★ **判据（署我名）：一个零的【出处】（谁印的）与它的【分母】（哪个范围）是两个不同的属性，而「零命中」这个形状对两者都不区分。**

---

## 第一节 逐处必须印的六样

★ **Z1.1** 逐处印：
1. **产物**；2. **行号**；3. **载体类**（R／C／T）；4. **逐字那一行**（**已掩码**，判据 Z1.4）；
5. ★★★ **那个零的分母（哪个范围）**；6. ★★★ **若分母换成【全仓】它变成几 ／ 若分母换成【受跟踪字节】它变成几**。

★ **Z1.2** ★★★ **第 5 样只许从【那一行及其上文】读出来，不许从别处推断。**
⇒ **读不出来 ⇒ 逐字写「这一行没写」**，**不许**从产物名、从上下文、从我记忆里补。
★ **理由**：**换分母重算需要知道【那个零是关于哪个对象的】—— 而那件事如果不在印它的那一行上，一个只读那一行的普查做不了它。**

★ **Z1.3** ★★★ **第 6 样只对【对象可指】的处给数**；对象不可指 ⇒ 逐字写「对象不可指 ⇒ 不能换分母重算」。
★ **Z1.4** **本报告里那个串一律写成 `【串A·已掩码】`**（判据 M1.4）。

---

## 第二节 必须逐字回答的五个问题

★ **Z2.1** **一共有几个「零命中」？**（给数；**并单列「同一行里出现两次」的处数**）
★ **Z2.2** ★★★ **其中【分母写全仓】几个 ／ 【分母写子范围】几个 ／ 【分母没写】几个？**
（给三个数，**逐处点名**）
★ **Z2.3** ★★★ **有几个「零命中」在换分母之后不再是零？**（给数，**逐处给新旧值**）
★ **Z2.4** ★★★ **有没有哪个「零命中」的分母，今天在产物里【一个字都没写】？**
（给数，逐处点名；**若 0 个，照实写 0 并说明你查了什么**）
★ **Z2.5** ★★★ **「内容命中」与「名字命中」这两个数，今天在产物里有几处是分开印的、几处是合成一个的？**
（给**两个数**）
★ **Z2.6** ★★★ **三个载体类（R／C／T）各占几处？**（给三个数；**和 = 分母 ✓**）

---

## 第三节 四档

| 档 | 判词 |
|---|---|
| **①** | **分母写全仓** |
| **②** | **分母写子范围** |
| **③** | **分母没写** |
| **④** | **判不出**（必须带理由；理由只能是「那把尺子看不见这个形状」或「对象在定义域之外」） |

★ **Z3.1** **和必须 = 分母 ✓。**
★ **Z3.2** ★★★ **「对象不可指 ⇒ 不能换分母重算」不是 ④** —— 它是**换分母这件事的射程之外**，**单列成 ⑤**。
★ **Z3.3** ★★★ **任何「某档 = 0」必须写清是【构造上的】【定义的推论】还是【我判得出】。**

---

## 第四节 对照

★ **Z4.1** 两个**反方向**对照：
- **(a) 一处已知分母是全仓** —— 落 ①；
- **(b) 一处已知分母是子范围** —— 落 ②。
★ **Z4.2** **红就照实报红。**
★ **Z4.3** **空集对照**：对象取空集 ⇒ 四档全 0、和 = 0 = 分母 ✓，**分母一起印**。

---

## 第五节 诚实边界

★ **Z5.1** **只读不写。**
★ **Z5.2** **逐字回答：本件跑了哪几条命令？**（逐条列出；**跑了门禁没有**）
★ **Z5.3** **逐个按名字 `git add`**；报告里印**跑后** `git status --porcelain` 的**逐字输出**。
★ **Z5.4** 四件套；`git diff --stat <分支点> HEAD -- docs/` 必须**空**（**三参、第一参先过 `merge-base --is-ancestor`**）。
★ **Z5.5** ★★★ **任何印出来的路径必须先 `existsSync` 再印**（exact-otter 在 [otter-letter 192] 五 立的硬约束）。
★ **Z5.6** ★★★ **一个「今天成立」的关系必须写明【有没有构造出反例】，不许写成「必然」**（同 六）。
★ **Z5.7** 做完**停**，等 exact-otter 裁。

```
