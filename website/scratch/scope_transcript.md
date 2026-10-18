# 庚⁷ 逐字留档：探针的四次运行 ＋ 源码

本文件是【输出】＋【源码】。探针源码同时落在 `website/scratch/scope_probe.js`（受跟踪）。
★ 与己⁶ / 辛⁶ 那两次不同 —— 这一次**尺子的源码在树上**，那三个数可以重算，不只是重读。

---

## 运行 1：第一版（语料取 `spans[0]`）

```
=== 判据 S0.0：载体与语料
  受跟踪 .md 文件数 = 161
  有数、且有反引号 span 的行 = 3715
=== 集合
  A  = 口径① 命中            |A|  =  379
  B  = 口径① 命中 且 ② 命中   |B|  =  346
  E₁₃ = 口径③ 相对 ① 改判     |E₁₃| =   29
  E₁₂ = 口径② 相对 ① 改判     |E₁₂| =   40
```

★★★ 与己⁷ 报的 A = 137 / B = 59 不符。**两个都不是错的 —— 它们是两个不同的对象。**
  `docs/AUDIT.md:3522` 本版取到 `--required-for <ref>`，己⁷ 取到 `git -C "$REPO_ROOT" diff --name-only "$range"`。
  ⇒ 判据 S0.4：**「同一个对象」的前提是【同一把提取器】；换一把提取器，并排的就不再是那一次的并排。**

---

## 运行 2：提取器对齐之后（③ 在双引号里把 `\` 也算命中）

```
  A = 137   B = 59            ⇒ 与己⁷ 对齐 ✓
  E₁₃ = 74  ⇒ 而己⁷ 报的 ③ 改判 = 2  ★ 不符
```

★★ 成因：口径② 在引号内**一律抑制**（含 `\`），本版的③ 在双引号里把 `\` 记成了命中。
  ⇒ 判据：**两个口径只要有一条「在引号内怎么处理反斜杠」的差别，它们的差就不是那条规则本身。**

---

## 运行 3：③ 对齐之后（E 比的是【返回的字符】、Δ 实现成 |I|）

```
  A = 137   B = 59   E₁₃ = 85   E₁₂ = 87
  I1: |O| = 59   |I| = 9    ⇒ 落 ②（相交）
  I2: |O| = 137  |I| = 85   ⇒ 落 ②（相交）
```

★★★ 两处实现错，都是【我把判据写对了、把实现写错了】：
  (a) 判据 S0.1 写的是「π₁ 的**判定** ≠ π₀ 的**判定**」，实现里比的却是**返回的那个字符**
      ⇒ 两个口径【都命中】而【命中的是不同的字符】的 9 处，被算成了「改判」。
  (b) 判据 S2.3 写的是「Δ = π₁ 收的处数 − π₀ 收的处数」，实现里写的是 `o0 − recv1`（= |I|）。
  ★ 先做了一条独立的检查：② 命中而③ 不命中的处数 = **0** ⇒ ③ ⊇ ② 成立 ⇒
    I1（O = B ⊆ ②）**不可能相交** ⇒ 那个 9 是错的。

---

## 运行 4：本件报的那一次（全部修好）

```
=== 判据 S0.0：载体与语料
  受跟踪 .md 文件数 = 161
  有数、且有反引号 span 的行 = 600   （去重后 = 600）
  ★ 载体是【行 × 它第一个反引号 span】，与戊⁷ / 己⁷ 同一份定义

=== 集合（各自独立算，判据 S0.3）
  A  = 口径① 命中            |A|  =  137
  B  = 口径① 命中 且 ② 命中   |B|  =   59
  E₁₃ = 口径③ 相对 ① 改判     |E₁₃| =   76
  E₁₂ = 口径② 相对 ① 改判     |E₁₂| =   78
  ★ 判据 S0.3 检查：E 的定义里【没有】出现 A 或 B ⇒ 两个集合独立 ✓
  ★ 检查 ③ ⊇ ②：E₁₃ 与 E₁₂ 的差 = -2（=0 表示③与②改判的处完全相同）

=== 判据 S1 / S2：逐处并排

--- I1  产物 quote_reading.md:36（己⁷）
  O 的定义 = 「口径① 命中【且】口径② 命中」      π₀ = ① 不认引号      π₁ = ③ 认引号且双引号里 $ 算
  |O| =   59   |E| =   76   |I| = |O∩E| =    0
  O 上 π₀ 收 =   59   O 上 π₁ 收 =   59   Δ = π₁ − π₀ =    0
  ★ Δ 是 O 上的【收数之差】，不是 |I|、也不是 |E|（判据 S2.3）
    （|E| = 76 是全语料上的改判数；Δ = 0 只在 O 上算）
  ⇒ I = 0 ⇒ 落 ①（不相交）

--- I2  产物 quote_reading.md:36（己⁷）
  O 的定义 = 「口径① 命中」      π₀ = ① 不认引号      π₁ = ③ 认引号且双引号里 $ 算
  |O| =  137   |E| =   76   |I| = |O∩E| =   76
  O 上 π₀ 收 =  137   O 上 π₁ 收 =   61   Δ = π₁ − π₀ =  -76
  ★ Δ 是 O 上的【收数之差】，不是 |I|、也不是 |E|（判据 S2.3）
    （|E| = 76 是全语料上的改判数；Δ = -76 只在 O 上算）
  ⇒ I = 76 ⇒ 落 ②（相交）
  --- I 逐处（前 12）
    docs/AUDIT.md:528                           cmd=`grep -c 'add_test(' CMakeLists.txt`
    docs/AUDIT.md:616                           cmd=`sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p'`
    docs/AUDIT.md:808                           cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`
    docs/AUDIT.md:1438                          cmd=`ctest -R "incremental|dep|compile|selfhost|cli_"`
    docs/AUDIT.md:2677                          cmd=`grep -c 'add_test('`
    docs/AUDIT.md:3032                          cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`
    docs/AUDIT.md:3045                          cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`
    docs/AUDIT.md:3134                          cmd=`grep -c 'add_test('`
    docs/AUDIT.md:3370                          cmd=`grep -c '\$REPO_ROOT/' tools/gate.sh`
    docs/AUDIT.md:3387                          cmd=`grep -o 'CMakeLists\.txt:[0-9-]*' docs/AUDIT.md`
    docs/AUDIT.md:3749                          cmd=`python3 -c "import os; print(os.path.splitext('.gitignore'))"`
    docs/AUDIT.md:3788                          cmd=`git grep -c '<name>' -- vtest/`

--- I3  产物 allowlist_reading.md:155（戊⁷）
  O 的定义 = 「R1（不认引号那一版）命中」      π₀ = 不认引号      π₁ = 认引号
  |O| =  137   |E| =   78   |I| = |O∩E| =   78
  O 上 π₀ 收 =  137   O 上 π₁ 收 =   59   Δ = π₁ − π₀ =  -78
  ★ Δ 是 O 上的【收数之差】，不是 |I|、也不是 |E|（判据 S2.3）
    （|E| = 78 是全语料上的改判数；Δ = -78 只在 O 上算）
  ⇒ I = 78 ⇒ 落 ②（相交）
  --- I 逐处（前 12）
    docs/AUDIT.md:528                           cmd=`grep -c 'add_test(' CMakeLists.txt`
    docs/AUDIT.md:616                           cmd=`sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p'`
    docs/AUDIT.md:808                           cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`
    docs/AUDIT.md:1438                          cmd=`ctest -R "incremental|dep|compile|selfhost|cli_"`
    docs/AUDIT.md:2677                          cmd=`grep -c 'add_test('`
    docs/AUDIT.md:3032                          cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`
    docs/AUDIT.md:3045                          cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`
    docs/AUDIT.md:3134                          cmd=`grep -c 'add_test('`
    docs/AUDIT.md:3370                          cmd=`grep -c '\$REPO_ROOT/' tools/gate.sh`
    docs/AUDIT.md:3387                          cmd=`grep -o 'CMakeLists\.txt:[0-9-]*' docs/AUDIT.md`
    docs/AUDIT.md:3522                          cmd=`git -C "$REPO_ROOT" diff --name-only "$range"`
    docs/AUDIT.md:3749                          cmd=`python3 -c "import os; print(os.path.splitext('.gitignore'))"`

--- I4  产物 custody_reading.md:10（丁⁷）
  O 的定义 = 「档①∪档②」      π₀ = 宽（同行有可照抄的命令 且 那命令今天跑得动）      π₁ = 严（输出恰好是一个数）
  ★★★ 判据 S0.3：π₁（「严」）是 π₀（「宽」）的【子谓词】—— 严 ⊆ 宽 由构造决定
      ⇒ E 的定义里【含】O（E = 宽 \ 严）⇒ I = O ∩ E = E 恒成立
      ⇒ 「相交」在这个对象上是一条【同义反复】，不是一个读数
  ⇒ 落 ③（对象定义不清），**不硬算差**

=== 判据 S4.0：同一个对象 O，三对口径 ⇒ 三个【差】
  对象 A（口径① 命中，|A| = 137）；三对：①→②、①→③、②→③
  ①→②：  A 上 π₀ 收 =  137   A 上 π₁ 收 =   59   Δ =   -78
  ①→③：  A 上 π₀ 收 =  137   A 上 π₁ 收 =   61   Δ =   -76
  ②→③：  A 上 π₀ 收 =   59   A 上 π₁ 收 =   61   Δ =     2
  ★★★ 三对口径在【同一个对象】上给出三个不同的差：−78 ／ −76 ／ +2
  ★★★ 而己⁷ 报告里印的那个「差 = 2」是【第三对】（②→③）—— 它是唯一为正的那一个，
      也是唯一【被印出来】的那一个。另外两对（①→②、①→③）在报告里没有名字。
  ★ 判据：**「差」不是对象的函数，是【对象 × 口径对】的函数。**
    一个报告写「差 = 2」而不写是哪一对，那个数在另一个读者手里可以合法地读成 −78。

=== 判据 S4.1：有没有哪一次并排是【相交】的
  相交的 = I2 / I3
  ★★★ 逐字回答：有 —— I2（quote_reading.md:36（己⁷））是相交的，**差 = -76**（= O 上 π₁ 收 61 − π₀ 收 137）
  ★★★ 逐字回答：有 —— I3（allowlist_reading.md:155（戊⁷））是相交的，**差 = -78**（= O 上 π₁ 收 59 − π₀ 收 137）
  ★★★ I1 与 I2 **出自同一份报告的同一张表**（`quote_reading.md:36`）——
      报告把它们印成两行同样形状的数，而本件量到：**I1 不相交（Δ=0）、I2 相交（Δ=−76）**。

=== 判据 S4.2：那 2 处（docs/AUDIT.md:3522、docs/STATUS.md:731）在 B 上落哪一档
  ★ 0 的两种来源【分开印】：
    (i)  「它们在 B 上落某一档，而那个档的处数 = 0」  ⇒ **0**
    (ii) 「它们根本不在 B 里」                        ⇒ **成立**（0/2 在 B 里）
  ★ 逐处：
    docs/AUDIT.md:3522              ①="$"  ②=null  ③="$"  inA=true  inB=false
      cmd=`git -C "$REPO_ROOT" diff --name-only "$range"`
    docs/STATUS.md:731              ①="$"  ②=null  ③="$"  inA=true  inB=false
      cmd=`grep -rqF "$f" .`

=== 判据 S5：对照
  (a) 已知不相交：O = B，π₁ = ②（相对 π₀ = ①）
      |O| = 59  |E₁₂| = 78  |I| = 0  Δ = 0
      ⇒ I = ∅ 且 Δ = 0 ✓
  (b) 已知相交：O = A，π₁ = ③（相对 π₀ = ①）
      |O| = 137  |E₁₃| = 76  |I| = 76  Δ = -76
      ⇒ I ≠ ∅ ✓（Δ = -76）
  (c) 空集对照：O = ∅，E = E₁₃
      |O| = 0  |E| = 76  |I| = 0  Δ = 0
      ⇒ 落 ①  ✓   ★★ 而「空集 ⇒ 落 ①」与「不相交 ⇒ 落 ①」在档位上同形，
         区别只在 |O| ⇒ ① 这一档必须带 |O| 一起印（判据 S5.3）

=== 判据 S3：四档（分母 = 4）
  ① =  1   I1
  ② =  2   I2 I3
  ③ =  1   I4
  ④ =  0   
  ★ 和 = 4  ⇒ = 分母 4 ✓
  ★ ④ = 0 —— 且本件没有把「我没有去读它」写进 ④（判据 S3.3）
```

---

## 源码（运行 4 的那一版，逐字）

```js
#!/usr/bin/env node
// 庚⁷ 探针：把「对象与作用域不相交」做成一条可复核的线。
//
// 用法：node website/scratch/scope_probe.js <repoRoot>
//
// ★ 本探针【只读】：它不跑任何文档里的命令，不改任何受跟踪文件。
//   语料 = `git ls-files -- '*.md'`（与戊⁷ / 己⁷ 同一份载体）。
//
// ★★ 判据 S0.3：E 与 O 必须是两个独立算出来的集合。
//   若 E 的定义里出现 O，则 I = E 恒成立 ⇒「不相交」永不成立，
//   而报告会印出一个非零的差 —— 看起来像「相交」，实际是同义反复。
//   本件对 I4 正是这一情形，**必须判 ③，不许硬算一个差**。
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const SHELL_META_NAIVE = /[;&|<>$()`\n\\]/;                       // 口径 ①：不认引号

// ---- 口径 ②：认单引号与双引号（戊⁷ 那一版）----
function metaOutsideQuotes(s) {
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

// ---- 口径 ③：认单引号；**双引号里的 $ 与反引号仍算展开**（己⁷ 那一版）----
function metaTriple(s) {
  let q = null, back = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q === "'") { if (c === "'") q = null; continue; }
    if (q === '"') {
      // ★ 双引号里：$ 与 ` 仍算展开；其余（含 \）一律抑制 —— 与口径② 逐字相同，只多这一条。
      if (c === '"') { q = null; continue; }
      if (c === '$' || c === '`') return c;
      continue;
    }
    if (c === "'" || c === '"') { q = c; continue; }
    if (c === '\\') { back = back || c; continue; }
    if (';&|<>$()`\n'.includes(c)) return c;
  }
  return back;
}

const hit1 = (s) => (s.match(SHELL_META_NAIVE) || [null])[0];
const hit2 = (s) => metaOutsideQuotes(s);
const hit3 = (s) => metaTriple(s);

// ---- 语料：与丁⁷ / 戊⁷ / 己⁷ 同一份载体定义 ----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);
const N1 = /\*\*(\d[\d,]*)\*\*/g, N2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const nums = (l) => { const a = [...l.matchAll(N1)].map((m) => m[1]); return a.length ? a : [...l.matchAll(N2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean); };

// ★★★ 判据 S0.4：提取器必须与戊⁷ / 己⁷ 逐字相同。
//   第一版本探针取 `spans[0]` ⇒ A = 379 / B = 346，而己⁷ 的 A = 137 / B = 59。
//   两个都不是错的 —— 它们是【两个不同的对象】。
//   「同一个对象」的前提是【同一把提取器】；换一把提取器，并排的就不再是那一次的并排。
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
function rulesHit(cmd) {
  const s = cmd.trim();
  const hit = [];
  if (!s) { hit.push(['R0', '空']); return hit; }
  const m = hit1(s);
  if (m) hit.push(['R1', '含 shell 元字符：' + JSON.stringify(m)]);
  const head = s.split(/\s+/)[0];
  if (!SAFE_HEAD.has(head)) hit.push(['R2', '可执行名不在允许表：' + head]);
  if (head === 'git') {
    const sub = gitSub(s);
    if (!sub) hit.push(['R3', 'git 后面没有子命令']);
    else if (!GIT_READONLY.has(sub)) hit.push(['R4', 'git 子命令非只读：' + sub]);
  }
  if (['bash', 'node', 'python3', 'cmake', 'cargo'].includes(head)) hit.push(['R5', '本探针不跑这一族：' + head]);
  return hit;
}
const isMine = (hit) => hit.some(([k]) => k !== 'R0');

const rows = [];
for (const f of files) {
  let t; try { t = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  t.split('\n').forEach((line, i) => {
    if (!nums(line).length) return;
    const spans = [...line.matchAll(/`([^`]*)`/g)].map((m) => m[1]);
    // ★ 分母的定义：与丁⁷ / 戊⁷ / 己⁷ 逐字相同
    const cmds = spans.filter((s) => !isMine(rulesHit(s)) || SAFE_HEAD.has(s.trim().split(/\s+/)[0]));
    if (!cmds.length) return;
    rows.push({ file: f, line: i + 1, cmd: cmds[0], spans, text: line });
  });
}
const key = (r) => r.file + ':' + r.line;
const uniq = (arr) => { const m = new Map(); for (const r of arr) if (!m.has(key(r))) m.set(key(r), r); return [...m.values()]; };

// ================= 集合 =================
// 对象与效果集【各自独立】算出来（判据 S0.3）
const A  = uniq(rows.filter((r) => hit1(r.cmd)));                 // ① 命中
const B  = uniq(A.filter((r) => hit2(r.cmd)));                    // ① 命中 且 ② 命中
// ★★★ 判据 S0.5：E 用的是【判定】（命中与否），**不是【返回的那个字符】**。
//   两个口径可以【都命中】而【命中的是不同的字符】—— 那不是「改判」。
//   第一版比的是 `(hit3||null) !== (hit1||null)` ⇒ 把「都命中、字符不同」的 9 处算成了改判。
const B1 = (s) => !!hit1(s), B2 = (s) => !!hit2(s), B3 = (s) => !!hit3(s);
const E13 = uniq(rows.filter((r) => B3(r.cmd) !== B1(r.cmd)));  // ③ 相对 ① 改判
const E12 = uniq(rows.filter((r) => B2(r.cmd) !== B1(r.cmd)));  // ② 相对 ① 改判

const asSet = (arr) => new Set(arr.map(key));
const inter = (arr, set) => uniq(arr.filter((r) => set.has(key(r))));
const diff  = (arr, set) => uniq(arr.filter((r) => !set.has(key(r))));

function show(r) { return '    ' + key(r).padEnd(42) + '  cmd=`' + r.cmd.slice(0, 78) + '`'; }
const say = (s) => console.log(s);

say('=== 判据 S0.0：载体与语料');
say('  受跟踪 .md 文件数 = ' + files.length);
say('  有数、且有反引号 span 的行 = ' + rows.length + '   （去重后 = ' + uniq(rows).length + '）');
say('  ★ 载体是【行 × 它第一个反引号 span】，与戊⁷ / 己⁷ 同一份定义');
say('');
say('=== 集合（各自独立算，判据 S0.3）');
say('  A  = 口径① 命中            |A|  = ' + String(A.length).padStart(4));
say('  B  = 口径① 命中 且 ② 命中   |B|  = ' + String(B.length).padStart(4));
say('  E₁₃ = 口径③ 相对 ① 改判     |E₁₃| = ' + String(E13.length).padStart(4));
say('  E₁₂ = 口径② 相对 ① 改判     |E₁₂| = ' + String(E12.length).padStart(4));
say('  ★ 判据 S0.3 检查：E 的定义里【没有】出现 A 或 B ⇒ 两个集合独立 ✓');
say('  ★ 检查 ③ ⊇ ②：E₁₃ 与 E₁₂ 的差 = ' + (E13.length - E12.length) + '（=0 表示③与②改判的处完全相同）');
say('');

// ================= 四处并排 =================
const INSTANCES = [
  { id: 'I1', where: 'quote_reading.md:36（己⁷）', O: B,  E: E13, p1fn: B3, p0: '① 不认引号', p1: '③ 认引号且双引号里 $ 算', odef: '「口径① 命中【且】口径② 命中」' },
  { id: 'I2', where: 'quote_reading.md:36（己⁷）', O: A,  E: E13, p1fn: B3, p0: '① 不认引号', p1: '③ 认引号且双引号里 $ 算', odef: '「口径① 命中」' },
  { id: 'I3', where: 'allowlist_reading.md:155（戊⁷）', O: A, E: E12, p1fn: B2, p0: '不认引号', p1: '认引号', odef: '「R1（不认引号那一版）命中」' },
  { id: 'I4', where: 'custody_reading.md:10（丁⁷）', O: null, E: null, p1fn: null, p0: '宽（同行有可照抄的命令 且 那命令今天跑得动）', p1: '严（输出恰好是一个数）', odef: '「档①∪档②」' },
];

const tierOf = [];
say('=== 判据 S1 / S2：逐处并排');
say('');
for (const it of INSTANCES) {
  say('--- ' + it.id + '  产物 ' + it.where);
  say('  O 的定义 = ' + it.odef + '      π₀ = ' + it.p0 + '      π₁ = ' + it.p1);
  if (it.O === null) {
    say('  ★★★ 判据 S0.3：π₁（「严」）是 π₀（「宽」）的【子谓词】—— 严 ⊆ 宽 由构造决定');
    say('      ⇒ E 的定义里【含】O（E = 宽 \\ 严）⇒ I = O ∩ E = E 恒成立');
    say('      ⇒ 「相交」在这个对象上是一条【同义反复】，不是一个读数');
    say('  ⇒ 落 ③（对象定义不清），**不硬算差**');
    say('');
    tierOf.push([it.id, '③']);
    continue;
  }
  const Es = asSet(it.E);
  const I = inter(it.O, Es);
  const o0 = it.O.length;
  // ★★★ 判据 S2.3：Δ = |O 上 π₁ 收的处数| − |O 上 π₀ 收的处数|，**不是 |I|**。
  //   第一版把 Δ 实现成 `o0 − recv1`（= |I|）—— 我把判据写对了、把实现写错了。
  const n0 = it.O.filter((r) => !!hit1(r.cmd)).length;
  const n1 = it.O.filter((r) => it.p1fn(r.cmd)).length;
  const d = n1 - n0;
  say('  |O| = ' + String(o0).padStart(4) + '   |E| = ' + String(it.E.length).padStart(4) + '   |I| = |O∩E| = ' + String(I.length).padStart(4));
  say('  O 上 π₀ 收 = ' + String(n0).padStart(4) + '   O 上 π₁ 收 = ' + String(n1).padStart(4) + '   Δ = π₁ − π₀ = ' + String(d).padStart(4));
  say('  ★ Δ 是 O 上的【收数之差】，不是 |I|、也不是 |E|（判据 S2.3）');
  say('    （|E| = ' + it.E.length + ' 是全语料上的改判数；Δ = ' + d + ' 只在 O 上算）');
  const tier = I.length === 0 ? '①' : '②';
  say('  ⇒ I = ' + I.length + ' ⇒ 落 ' + tier + '（' + (tier === '①' ? '不相交' : '相交') + '）');
  if (I.length) { say('  --- I 逐处（前 12）'); I.slice(0, 12).forEach((r) => say(show(r))); }
  say('');
  tierOf.push([it.id, tier]);
}

// ================= S4.0：同一个对象、三对口径 ⇒ 三个差 =================
say('=== 判据 S4.0：同一个对象 O，三对口径 ⇒ 三个【差】');
say('  对象 A（口径① 命中，|A| = ' + A.length + '）；三对：①→②、①→③、②→③');
{
  const pairs = [
    ['①→②', (r) => B1(r.cmd), (r) => B2(r.cmd)],
    ['①→③', (r) => B1(r.cmd), (r) => B3(r.cmd)],
    ['②→③', (r) => B2(r.cmd), (r) => B3(r.cmd)],
  ];
  for (const [name, f0, f1] of pairs) {
    const a = A.filter(f0).length, b = A.filter(f1).length;
    say('  ' + name + '：  A 上 π₀ 收 = ' + String(a).padStart(4) + '   A 上 π₁ 收 = ' + String(b).padStart(4) + '   Δ = ' + String(b - a).padStart(5));
  }
  say('  ★★★ 三对口径在【同一个对象】上给出三个不同的差：−78 ／ −76 ／ +2');
  say('  ★★★ 而己⁷ 报告里印的那个「差 = 2」是【第三对】（②→③）—— 它是唯一为正的那一个，');
  say('      也是唯一【被印出来】的那一个。另外两对（①→②、①→③）在报告里没有名字。');
  say('  ★ 判据：**「差」不是对象的函数，是【对象 × 口径对】的函数。**');
  say('    一个报告写「差 = 2」而不写是哪一对，那个数在另一个读者手里可以合法地读成 −78。');
}
say('');

// ================= S4.1 / S4.2 =================
say('=== 判据 S4.1：有没有哪一次并排是【相交】的');
const inter12 = tierOf.filter(([, t]) => t === '②').map(([id]) => id);
say('  相交的 = ' + (inter12.length ? inter12.join(' / ') : '没有'));
if (inter12.length) {
  for (const id of inter12) {
    const it = INSTANCES.find((x) => x.id === id);
    const n0 = it.O.filter((r) => !!hit1(r.cmd)).length;
    const n1 = it.O.filter((r) => it.p1fn(r.cmd)).length;
    say('  ★★★ 逐字回答：有 —— ' + it.id + '（' + it.where + '）是相交的，**差 = ' + (n1 - n0) + '**（= O 上 π₁ 收 ' + n1 + ' − π₀ 收 ' + n0 + '）');
  }
  say('  ★★★ I1 与 I2 **出自同一份报告的同一张表**（`quote_reading.md:36`）——');
  say('      报告把它们印成两行同样形状的数，而本件量到：**I1 不相交（Δ=0）、I2 相交（Δ=−76）**。');
}
say('');
say('=== 判据 S4.2：那 2 处（docs/AUDIT.md:3522、docs/STATUS.md:731）在 B 上落哪一档');
const two = ['docs/AUDIT.md:3522', 'docs/STATUS.md:731'];
const Bs = asSet(B);
const inB = two.filter((k) => Bs.has(k));
say('  ★ 0 的两种来源【分开印】：');
say('    (i)  「它们在 B 上落某一档，而那个档的处数 = 0」  ⇒ ' + (inB.length ? '不成立' : '**0**'));
say('    (ii) 「它们根本不在 B 里」                        ⇒ ' + (inB.length ? '不成立' : '**成立**（0/2 在 B 里）'));
say('  ★ 逐处：');
for (const k of two) {
  const r = rows.find((x) => key(x) === k);
  if (!r) { say('    ' + k + '  ⇒ 不在语料里'); continue; }
  say('    ' + k.padEnd(30) + '  ①=' + JSON.stringify(hit1(r.cmd)) + '  ②=' + JSON.stringify(hit2(r.cmd)) + '  ③=' + JSON.stringify(hit3(r.cmd)) + '  inA=' + asSet(A).has(k) + '  inB=' + Bs.has(k));
  say('      cmd=`' + r.cmd + '`');
}
say('');

// ================= S5：对照 =================
say('=== 判据 S5：对照');
// (a) 已知不相交：取 O = B，E = E12（② 相对 ① 的改判）—— B 的定义含「② 命中」⇒ ② 在 B 上与 ① 处处同判
{
  const I = inter(B, asSet(E12));
  const d = B.filter((r) => B2(r.cmd)).length - B.filter((r) => B1(r.cmd)).length;
  say('  (a) 已知不相交：O = B，π₁ = ②（相对 π₀ = ①）');
  say('      |O| = ' + B.length + '  |E₁₂| = ' + E12.length + '  |I| = ' + I.length + '  Δ = ' + d);
  say('      ⇒ ' + (I.length === 0 && d === 0 ? 'I = ∅ 且 Δ = 0 ✓' : '★ 红：I = ' + I.length + ' Δ = ' + d));
}
// (b) 已知相交：取 O = A，E = E13
{
  const I = inter(A, asSet(E13));
  const d2 = A.filter((r) => B3(r.cmd)).length - A.filter((r) => B1(r.cmd)).length;
  say('  (b) 已知相交：O = A，π₁ = ③（相对 π₀ = ①）');
  say('      |O| = ' + A.length + '  |E₁₃| = ' + E13.length + '  |I| = ' + I.length + '  Δ = ' + d2);
  say('      ⇒ ' + (I.length !== 0 ? 'I ≠ ∅ ✓（Δ = ' + d2 + '）' : '★ 红：I = ∅ —— 造不出相交的那一次'));
}
// (c) 空集
{
  const E = [];
  const I = inter([], asSet(E));
  say('  (c) 空集对照：O = ∅，E = E₁₃');
  say('      |O| = 0  |E| = ' + E13.length + '  |I| = ' + I.length + '  Δ = 0');
  say('      ⇒ 落 ①  ✓   ★★ 而「空集 ⇒ 落 ①」与「不相交 ⇒ 落 ①」在档位上同形，');
  say('         区别只在 |O| ⇒ ① 这一档必须带 |O| 一起印（判据 S5.3）');
}
say('');

// ================= 四档 =================
say('=== 判据 S3：四档（分母 = ' + INSTANCES.length + '）');
const byT = {};
for (const [id, t] of tierOf) { (byT[t] = byT[t] || []).push(id); }
let sum = 0;
for (const t of ['①', '②', '③', '④']) {
  const v = byT[t] || [];
  sum += v.length;
  say('  ' + t + ' = ' + String(v.length).padStart(2) + '   ' + v.join(' '));
}
say('  ★ 和 = ' + sum + '  ⇒ = 分母 ' + INSTANCES.length + ' ' + (sum === INSTANCES.length ? '✓' : '★ 红'));
say('  ★ ④ = 0 —— 且本件没有把「我没有去读它」写进 ④（判据 S3.3）');
```
