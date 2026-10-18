# 丁⁷ 逐字留档：把「谁保管那个数」做成可复核的数

四件套
```
$ pwd
/home/sakiko/inimerse/.worktrees/website-ledger-readat
$ git rev-parse HEAD
7d33d4a2ec40d3e5c85fd82c4df366f7ec05fb4a
$ git status --porcelain
?? website/scratch/custody_criteria.md
?? website/scratch/custody_probe.js
?? website/scratch/custody_reading.md
?? website/scratch/custody_transcript.md
$ git rev-parse origin/main
c120774eded3cf59554509c55ea05b79ed802213
$ git rev-list --count origin/main..HEAD
0
```

## 一、判据的落点
```
bf45f57f4ce7796608397f47d825b7c805872696bf6e3b3eb1258bd4a63ac42d  website/scratch/custody_criteria.md
d5909d8df9cb58676872ec371c4417bcf95bdd80106d62f03ed8b08604b5367d  website/scratch/custody_probe.js
```

## 二、尺子的源码（`website/scratch/custody_probe.js`）
```js
#!/usr/bin/env node
// 丁⁷ 探针：把「谁保管那个数」做成可复核的数。
//
// 用法：node website/scratch/custody_probe.js <repoRoot>
//
// ★ 载体与分母承 丙⁷ 的 `cmdnum_probe.js`（判据 E0：不许在下一件里悄悄换分母）。
//   枚举逻辑逐字相同；本件只加【候选一】那个谓词，并把【候选二】认成 档①∪档②。
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const SHELL_META = /[;&|<>$()`\n\\]/;
const GIT_READONLY = new Set(['ls-files', 'ls-tree', 'rev-list', 'rev-parse', 'show', 'grep', 'cat-file', 'merge-base', 'count-objects', 'log', 'diff', 'status']);
const SAFE_HEAD = new Set(['grep', 'wc', 'ctest', 'git', 'sed', 'python3', 'node', 'cmake', 'cargo', 'bash', 'ls-files', 'ls-tree']);

function whyNotRunnable(cmd) {
  const s = cmd.trim();
  if (!s) return '空';
  if (SHELL_META.test(s)) return '含 shell 元字符（本探针不许跑）';
  const head = s.split(/\s+/)[0];
  if (!SAFE_HEAD.has(head)) return '不在允许表里的可执行名：' + head;
  if (head === 'git') {
    const sub = s.split(/\s+/).slice(1).find((t) => !t.startsWith('-'));
    if (!sub) return 'git 后面没有子命令';
    if (!GIT_READONLY.has(sub)) return 'git 子命令不在只读表里：' + sub;
  }
  if (head === 'bash' || head === 'node' || head === 'python3' || head === 'cmake' || head === 'cargo') return '本探针不跑这一族（可能写盘）';
  return null;
}

function run(cmd) {
  const parts = cmd.trim().split(/\s+/);
  try {
    const out = execFileSync(parts[0], parts.slice(1), { cwd: ROOT, encoding: 'utf8', timeout: 20000, stdio: ['ignore', 'pipe', 'pipe'] });
    return { rc: 0, out: out.trim() };
  } catch (e) {
    return { rc: e.status == null ? -1 : e.status, out: (e.stdout || '').trim(), err: (e.stderr || '').trim() };
  }
}

// ---- 枚举（与 丙⁷ 逐字相同）----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);
const NUM_S1 = /\*\*(\d[\d,]*)\*\*/g;
const NUM_S2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const numS1 = (line) => [...line.matchAll(NUM_S1)].map((m) => m[1]);
const numS2 = (line) => [...line.matchAll(NUM_S2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean);
const numsOf = (line) => { const a = numS1(line); return a.length ? a : numS2(line); };

const rows = [];
for (const f of files) {
  let text;
  try { text = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  text.split('\n').forEach((line, i) => {
    const nums = numsOf(line);
    if (!nums.length) return;
    const spans = [...line.matchAll(/`([^`]*)`/g)].map((m) => m[1]);
    const cmds = spans.filter((s) => !whyNotRunnable(s) || SAFE_HEAD.has(s.trim().split(/\s+/)[0]));
    rows.push({ file: f, line: i + 1, nums, spans, cmds, text: line, shape: numS1(line).length ? 'S1' : 'S2' });
  });
}
const withCmd = rows.filter((r) => r.cmds.length > 0);
const cache = new Map();

// ---- 判据 E2：候选一 = 同一行里有一句话把那个数指到另一个时刻或另一棵树 ----
// ★★ 两张表都印，因为它们差 7 倍（判据 E2b）。
//   LOOSE = 丙⁷ 那一版（含「当时」「不再」「是在」这些【叙事里的时间词】）
//   TIGHT = 只有明确在说【那个数的来历】的话
const LOOSE_MARKS = ['上量的', '是在', '改动前', '写下这一段之后', '当时', '已过期', '不再', '旧值', '曾经'];
const TIGHT_MARKS = ['上量的', '改动前', '已过期', '写下这一段之后该数是', '登记时是'];
const authorMarkOf = (line) => TIGHT_MARKS.filter((m) => line.includes(m));
const looseMarkOf = (line) => LOOSE_MARKS.filter((m) => line.includes(m));
// ★ 判据 E5：④a 只许有两种合法理由，【我的允许表挡下】不在其中 —— 那是我这一侧的射程，落 ③。
const MINE_SIDE = /shell 元字符|不在允许表|git 子命令不在只读表|git 后面没有子命令|本探针不跑这一族/;

// ---- 分类 ----
const g = { '①': [], '②': [], '③': [], '④a': [], '④b': [] };
const cell = { '11': [], '10': [], '01': [], '00': [] }; // 候选一 × 候选二
for (const r of withCmd) {
  const cmd = r.cmds[0], num = r.nums[0];
  const marks = authorMarkOf(r.text);
  const loose = looseMarkOf(r.text);
  const hasOne = marks.length > 0;
  const why = whyNotRunnable(cmd);
  let canTwo = false, got = [], four = null;
  if (why) {
    // ★ 判据 E5：我的允许表挡下 ⇒ ③（我这一侧的射程）；其余 ⇒ ④a
    if (MINE_SIDE.test(why)) four = ['③', '我的允许表挡下：' + why];
    else four = ['④a', '命令片段不完整：' + why];
  } else {
    let res = cache.get(cmd);
    if (!res) { res = run(cmd); cache.set(cmd, res); }
    if (res.rc !== 0) {
      four = ['④a', 'rc=' + res.rc + ' ' + (res.err || '').split('\n')[0]];
    } else {
      got = (res.out.match(/-?\d[\d,]*/g) || []).map((x) => x.replace(/,/g, ''));
      // ★ 判据 E5：反引号片段只是【一条命令的一半】（wc -l / grep 不带参数）⇒ 判定没有对象 ⇒ ④a
      const HALF = /^(wc|grep|git)$|^wc\s+-l$|^grep\s|^ctest$/;
      if (!got.length && HALF.test(cmd.trim())) four = ['④a', '命令片段不完整（参数写在正文里）：`' + cmd + '`'];
      else if (!got.length) four = ['④b', '命令有输出但没有数：' + JSON.stringify(res.out.slice(0, 60))];
      else canTwo = true;   // 判据 E3：跑得动 ⇒ 候选二
    }
  }
  const rec = { ...r, cmd, num, marks, loose, got: got.slice(0, 4).join('/'), four };
  cell[(hasOne ? '1' : '0') + (canTwo ? '1' : '0')].push(rec);
  if (hasOne) g['①'].push(rec);
  else if (canTwo) g['②'].push(rec);
  else if (four && four[0] === '④a') g['④a'].push(rec);
  else if (four && four[0] === '④b') g['④b'].push(rec);
  else g['③'].push(rec);
}

const say = (s = '') => console.log(s);
say('=== 判据 E0：载体与分母');
say('  S1（加粗的【数】）                = ' + rows.filter((r) => r.shape === 'S1').length + ' 处');
say('  S1∪S2（加粗的【短语】里含数也算）  = ' + rows.length + ' 处');
say('  ★ 分母 B（带命令的）              = ' + withCmd.length + ' 处（' + new Set(withCmd.map((r) => r.file)).size + ' 个文件）');
say('  ⇒ 与 丙⁷ 的 565 ' + (withCmd.length === 565 ? '逐字相同 ✓' : '★ 不同：差 ' + (withCmd.length - 565)));
say();
say('=== ★★ 判据 E1：2×2 交集表（候选一 × 候选二）—— 先印这个，再印四档');
say('                  候选二 是          候选二 否');
say('  候选一 是        ' + String(cell['11'].length).padStart(5) + '            ' + String(cell['10'].length).padStart(5));
say('  候选一 否        ' + String(cell['01'].length).padStart(5) + '            ' + String(cell['00'].length).padStart(5));
say('  ★ 交集（同时为真）= ' + cell['11'].length + ' 处 —— 它在任何一格里都看不见');
say('  和 = ' + (cell['11'].length + cell['10'].length + cell['01'].length + cell['00'].length));
say();
say('=== 四档（按优先级：候选一 > 候选二 > 候选三 > ④），分母 = B');
say('  ① 候选一（作者自己留下了痕迹）    = ' + String(g['①'].length).padStart(4));
say('  ② 候选二（读者能一步跑到它的今天）= ' + String(g['②'].length).padStart(4));
say('  ③ 候选三（没有任何人）            = ' + String(g['③'].length).padStart(4));
say('  ④ 判不出                          = ' + String(g['④a'].length + g['④b'].length).padStart(4));
say('       ④a 尺子看不见那个形状        = ' + g['④a'].length);
say('       ④b 对象在定义域之外          = ' + g['④b'].length);
const sum = g['①'].length + g['②'].length + g['③'].length + g['④a'].length + g['④b'].length;
say('  和 = ' + sum + '  ⇒ ' + (sum === withCmd.length ? '= 分母 B ✓' : '★ ≠ 分母 B'));
say();
say('=== ★★★ 判据 E3：候选二那一档今天有几处（逐字回答派件）');
say('  宽（同行有可照抄的命令 且 那命令今天跑得动 = 档①∪档②） = ' + String(g['①'].length + g['②'].length).padStart(4));
say('  严（跑出来的东西与文档同形 = 输出恰好是一个数）        = ' + String(g['①'].filter((r) => r.got && !r.got.includes('/')).length + g['②'].filter((r) => r.got && !r.got.includes('/')).length).padStart(4));
say('  ★ 派件问的「上界」是宽的那个。');
say();
say('=== 判据 E6：两个反方向对照');
for (const [f, l, want] of [['docs/AUDIT.md', 3505, '①'], ['docs/RELEASE_0.5.1.md', 15, '②']]) {
  const hit = ['①', '②', '③', '④a', '④b'].find((x) => g[x].some((r) => r.file === f && r.line === l));
  say('  ' + f + ':' + l + '  该落 ' + want + '  实落 ' + (hit || '—') + '  ' + (hit === want ? '✓' : '★ 不符'));
  const r = ['①', '②', '③', '④a', '④b'].map((x) => g[x].find((y) => y.file === f && y.line === l)).find(Boolean);
  if (r) say('      逐字 marks=' + JSON.stringify(r.marks) + '  今天=' + r.got + '  cmd=`' + r.cmd + '`');
}
say();
say('=== ① 逐处（候选一：那一行里把那数指到别处的短语）');
g['①'].forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   marks=' + JSON.stringify(r.marks) + '   今天=' + (r.got || '—')));
say();
say('=== ④a 逐处');
g['④a'].forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   cmd=`' + r.cmd + '`   ' + r.four[1]));
say();
say('=== ④b 逐处（前 20）');
g['④b'].slice(0, 20).forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   cmd=`' + r.cmd + '`   ' + r.four[1]));
fs.writeFileSync(path.join(ROOT, 'website/scratch/.build/custody_grade.json'), JSON.stringify({ B: withCmd.length, cell: Object.fromEntries(Object.entries(cell).map(([k, v]) => [k, v.length])), g: Object.fromEntries(Object.entries(g).map(([k, v]) => [k, v.length])) }, null, 1));
```

## 三、判据 E2b 的换表实验（宽表 88 → 严表 12，逐处对照）
```
const { execFileSync } = require('child_process');
const fs=require('fs'), path=require('path');
const ROOT='.';
const SHELL_META=/[;&|<>$()`\n\\]/;
const GIT_RO=new Set(['ls-files','ls-tree','rev-list','rev-parse','show','grep','cat-file','merge-base','count-objects','log','diff','status']);
const SAFE=new Set(['grep','wc','ctest','git','sed','python3','node','cmake','cargo','bash','ls-files','ls-tree']);
function why(c){const s=c.trim();if(!s)return '空';if(SHELL_META.test(s))return 'shell元字符';const h=s.split(/\s+/)[0];if(!SAFE.has(h))return '不在允许表：'+h;if(h==='git'){const sub=s.split(/\s+/).slice(1).find(t=>!t.startsWith('-'));if(!sub)return 'git无子命令';if(!GIT_RO.has(sub))return 'git非只读：'+sub;}if(['bash','node','python3','cmake','cargo'].includes(h))return '本探针不跑这一族';return null;}
const files=execFileSync('git',['-c','core.quotepath=false','ls-files','-z','--','*.md'],{cwd:ROOT,encoding:'utf8'}).split('\0').filter(Boolean);
const N1=/\*\*(\d[\d,]*)\*\*/g, N2=/\*\*([^*]*?\d[^*]*?)\*\*/g;
const s1=l=>[...l.matchAll(N1)].map(m=>m[1]);
const s2=l=>[...l.matchAll(N2)].map(m=>(m[1].match(/\d[\d,]*/)||[''])[0]).filter(Boolean);
const nums=l=>{const a=s1(l);return a.length?a:s2(l);};
// ★ 严表：只有【明确在说那个数的来历】的话
const TIGHT=['上量的','改动前','已过期','写下这一段之后该数是','登记时是'];
// ★ 宽表（丙⁷ 用的那张）
const LOOSE=['上量的','是在','改动前','写下这一段之后','当时','已过期','不再','旧值','曾经'];
const rows=[];
for(const f of files){let t;try{t=fs.readFileSync(path.join(ROOT,f),'utf8');}catch{continue;}
 t.split('\n').forEach((line,i)=>{const ns=nums(line);if(!ns.length)return;
  const spans=[...line.matchAll(/`([^`]*)`/g)].map(m=>m[1]);
  const cmds=spans.filter(s=>!why(s)||SAFE.has(s.trim().split(/\s+/)[0]));
  rows.push({file:f,line:i+1,num:ns[0],cmds,text:line});});}
const withCmd=rows.filter(r=>r.cmds.length>0);
const tight=withCmd.filter(r=>TIGHT.some(m=>r.text.includes(m)));
const loose=withCmd.filter(r=>LOOSE.some(m=>r.text.includes(m)));
console.log('分母 B = '+withCmd.length);
console.log('宽表（当时/不再/是在…）= '+loose.length);
console.log('严表（上量的/改动前/已过期…）= '+tight.length);
console.log('--- 严表逐处');
tight.forEach(r=>console.log('  '+r.file+':'+r.line+'  数='+r.num+'  cmd=`'+r.cmds[0]+'`'));
console.log('--- 宽表减严表 = 只在宽表里的（前 12）');
loose.filter(r=>!TIGHT.some(m=>r.text.includes(m))).slice(0,12).forEach(r=>{
 const ms=LOOSE.filter(m=>r.text.includes(m));
 console.log('  '+r.file+':'+r.line+'  marks='+JSON.stringify(ms)+'  数='+r.num);
 console.log('     '+r.text.replace(/\s+/g,' ').slice(0,150));});
---
分母 B = 572
宽表（当时/不再/是在…）= 88
严表（上量的/改动前/已过期…）= 12
--- 严表逐处
  docs/AUDIT.md:2963  数=0  cmd=`grep`
  docs/AUDIT.md:3505  数=74  cmd=`git rev-list --count v0.5.1..v0.5.2`
  docs/BOARD.md:146  数=71  cmd=`git ls-files '*.im'`
  docs/BOARD.md:270  数=134  cmd=`git stash push -- src/runtime/runtime_posix.c`
  docs/BOARD.md:286  数=149  cmd=`git worktree add --detach .worktrees/t2old 58f1a44`
  docs/BOARD.md:287  数=150  cmd=`git worktree add --detach .worktrees/t2old 58f1a44`
  docs/DECFY_DESIGN.md:26  数=10  cmd=`grep -n "OP_AND\|OP_OR" src/compiler/compiler.c`
  docs/DECFY_DESIGN.md:538  数=37  cmd=`grep -c '独立' docs/DECFY_DESIGN.md`
  docs/DECFY_DESIGN.md:1065  数=1  cmd=`grep -c "expected 'expression'"`
  docs/DECFY_DESIGN.md:1122  数=49  cmd=`grep -n '一致'`
  docs/RELEASE_0.5.2.md:13  数=76  cmd=`git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'`
  docs/RELEASE_0.5.2.md:73  数=0  cmd=`git show 751b2a0:<path>`
--- 宽表减严表 = 只在宽表里的（前 12）
  docs/AUDIT.md:96  marks=["当时","不再"]  数=32
     **顺带发现的第二个真缺陷**：写这个测试的当天，它抓出 **wasm 的 `not` 是反的**（**当时**） —— `cg_cond` 在 `TOK_NOT` 分支里已经做了一次 `i32.eqz`，`cg_expr` 的 `TOK_NOT` 分支又做了一次（双重否定），于是 `not 0` 
  docs/AUDIT.md:412  marks=["当时"]  数=1
     **未进门禁（已修，见 §1.12）**：`contract_test.im` 当时是手动/dormant 套件，不在 CTest 内（`grep -in contract CMakeLists.txt` 只有两处无关注释）。它此后的第一个失败曾是 `list set`（`contract_test
  docs/AUDIT.md:578  marks=["当时"]  数=1
     **症状（不是运行错误，是流程缺陷）**：仓库根的 `contract_test.im` 是 API/SPI 契约套件，头注释写着「每次引擎 API 变更必须跑本套件（回归门槛）」—— 但它**当时根本不在 CTest 里**（`grep -in contract CMakeLists.txt` 当
  docs/AUDIT.md:648  marks=["当时"]  数=64
     2. **`push_int` 的参数是 `int`。** `src/vm/vm.c:325` / `src/vm/vm.h:379` 原文 `void push_int(VM *vm, int v)`，而 `L_PUSH_REG` 对 `VAL_INT` 直接调它 —— **函数传参路径上每一个 
  docs/AUDIT.md:704  marks=["是在"]  数=32
     §1.15 是在 `posix_core_sum` 上发现的。同一种「拿快速路径的门当答案」的写法，在 **WIN32 副本**里还有两处。这次不是撞上的，是照着 `grep -rn compCount src/` 把每个调用点看了一遍找出来的：
  docs/AUDIT.md:3387  marks=["当时","旧值"]  数=2
     11. **本文档指向 `CMakeLists.txt` 的行号没有任何东西在查，而且已经漂过。** 这次合并对 `CMakeLists.txt` 的改动是 `@@ -1031,8 +1031,21 @@`（净 +13），于是 §1.73 里那处 `:1370`（38 个可执行目标的第一个源文件那一
  docs/AUDIT.md:3636  marks=["当时"]  数=0
     我要求 agent4 修 `docs/DECFY_DESIGN.md` 里「`8d28c60` 在 `stream/sayout-ignore` 上、尚未合进 `main`」这半句，理由是我在 `main` 上写下了它。**它不在 `main` 上** —— 那是 `7daf145` 的版本，age
  docs/AUDIT.md:3648  marks=["是在","当时"]  数=3887
     **而它过期的方式比「号越尾」更坏**：`:3851` 现在**解析得到**（落到一个空行）⇒ **一条「这个行号存不存在」的检查会放它过去**。⇒ 与 A 段同族：**判据的射程（行存在）小于断言的内容（那一行说的是什么）**。 **★ 而这句更正自己也烂了 —— 第三代（`exact-otter
  docs/AUDIT.md:4028  marks=["是在","当时"]  数=5
     **第十二种（一条规矩只写在被引方，管不到引用方）**：`docs/SYNTAX.md` §5 逐字写着「`rand` 从未被任何文件注册（`grep -rn '"rand"' src/` 零命中）」，而**它自己引的那条命令**在 `fa8247e` 上恰好给 **1 条命中**（`src/mod
  docs/AUDIT.md:4325  marks=["是在","曾经"]  数=7
     - ★ **`bytecode_read_file_compat` / `bytecode_check_compatible` 属于「从来没被调用过」，不是回归。** `git log --all -S` 对两个名字各只返回一笔（`7be36c6`），在那之前 0 命中，在那一笔里两个都存在且**从
  docs/AUDIT.md:4343  marks=["当时"]  数=6
     1. **★ 「三条硬规则」是一个名字四个答案（本行已就地更正）。** 本节初稿写的是「一个名字两个答案」，只数了 `docs/BOARD.md` 与 `docs/README.md` 两处 —— **当时用的筛子是那两个我碰巧读到的文件。** `ivory-ember` 用 `git grep '
  docs/AUDIT.md:4370  marks=["当时"]  数=1
     **A1. `git add -A` 在一笔合并提交里加的不是「合并」。** 合 `stream/pin-shift` 时我用了 `git add -A`，**把另一个会话（`website`）当时未提交的 14 个文件（`website/**`）一并扫进了那笔合并提交**。它不具破坏性（文件本来就
```

## 四、★ 跑后 `status --porcelain`（判据 E7）
```
$ git status --porcelain
?? website/scratch/custody_criteria.md
?? website/scratch/custody_probe.js
?? website/scratch/custody_reading.md
?? website/scratch/custody_transcript.md
$ rm -rf Testing   # 探针跑 ctest 的副产物，不在 .gitignore 里
rm_rc=0
```

## 五、探针的完整输出
```
=== 判据 E0：载体与分母
  S1（加粗的【数】）                = 644 处
  S1∪S2（加粗的【短语】里含数也算）  = 4520 处
  ★ 分母 B（带命令的）              = 572 处（48 个文件）
  ⇒ 与 丙⁷ 的 565 ★ 不同：差 7

=== ★★ 判据 E1：2×2 交集表（候选一 × 候选二）—— 先印这个，再印四档
                  候选二 是          候选二 否
  候选一 是            2               10
  候选一 否           90              470
  ★ 交集（同时为真）= 2 处 —— 它在任何一格里都看不见
  和 = 572

=== 四档（按优先级：候选一 > 候选二 > 候选三 > ④），分母 = B
  ① 候选一（作者自己留下了痕迹）    =   12
  ② 候选二（读者能一步跑到它的今天）=   90
  ③ 候选三（没有任何人）            =  239
  ④ 判不出                          =  231
       ④a 尺子看不见那个形状        = 190
       ④b 对象在定义域之外          = 41
  和 = 572  ⇒ = 分母 B ✓

=== ★★★ 判据 E3：候选二那一档今天有几处（逐字回答派件）
  宽（同行有可照抄的命令 且 那命令今天跑得动 = 档①∪档②） =  102
  严（跑出来的东西与文档同形 = 输出恰好是一个数）        =   25
  ★ 派件问的「上界」是宽的那个。

=== 判据 E6：两个反方向对照
  docs/AUDIT.md:3505  该落 ①  实落 ①  ✓
      逐字 marks=["上量的"]  今天=80  cmd=`git rev-list --count v0.5.1..v0.5.2`
  docs/RELEASE_0.5.1.md:15  该落 ②  实落 ②  ✓
      逐字 marks=[]  今天=113  cmd=`git rev-list --count v0.5.0..v0.5.1`

=== ① 逐处（候选一：那一行里把那数指到别处的短语）
  docs/AUDIT.md:2963   数=0   marks=["上量的"]   今天=—
  docs/AUDIT.md:3505   数=74   marks=["上量的"]   今天=80
  docs/BOARD.md:146   数=71   marks=["改动前"]   今天=—
  docs/BOARD.md:270   数=134   marks=["已过期"]   今天=—
  docs/BOARD.md:286   数=149   marks=["改动前"]   今天=—
  docs/BOARD.md:287   数=150   marks=["改动前"]   今天=—
  docs/DECFY_DESIGN.md:26   数=10   marks=["已过期"]   今天=—
  docs/DECFY_DESIGN.md:538   数=37   marks=["改动前","写下这一段之后该数是"]   今天=2
  docs/DECFY_DESIGN.md:1065   数=1   marks=["已过期"]   今天=—
  docs/DECFY_DESIGN.md:1122   数=49   marks=["改动前"]   今天=—
  docs/RELEASE_0.5.2.md:13   数=76   marks=["上量的"]   今天=—
  docs/RELEASE_0.5.2.md:73   数=0   marks=["上量的"]   今天=—

=== ④a 逐处
  docs/API.md:479   数=85   cmd=`ctest --test-dir build --output-on-failure`   rc=-1 
  docs/AUDIT.md:75   数=3166   cmd=`grep -rn "OP_OR" src/compiler/`   rc=1 
  docs/AUDIT.md:457   数=0   cmd=`grep '"list"' src/compilation/*.c`   rc=2 grep: src/compilation/*.c: No such file or directory
  docs/AUDIT.md:757   数=114   cmd=`grep -E`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1044   数=118   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1178   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1179   数=119   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1258   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1289   数=0   cmd=`grep -c getline`   rc=1 
  docs/AUDIT.md:1682   数=22   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:2124   数=7   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:2946   数=0   cmd=`grep -rn 'Total Tests: 131' docs/ tools/`   rc=2 grep: Tests:: No such file or directory
  docs/AUDIT.md:2948   数=70   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:2967   数=3365   cmd=`grep -n 'out of 11[0-3]' docs/STATUS.md`   rc=2 grep: of: No such file or directory
  docs/AUDIT.md:2997   数=1   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:3070   数=968   cmd=`git show c71ea00:...`   rc=128 error: object 96fad2311ed46223dbe7aa460b1e7c10a216db53 is a tree, not a commit
  docs/AUDIT.md:3283   数=12   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:3285   数=0   cmd=`grep -c 'src/'`   rc=1 
  docs/AUDIT.md:3287   数=0   cmd=`grep -c 'src/'`   rc=1 
  docs/AUDIT.md:3303   数=43   cmd=`grep -c 'vm_register_builtin'`   rc=1 
  docs/AUDIT.md:3319   数=2734   cmd=`git grep -n 'child_proc_' -- src/ tools/ vtest/`   rc=1 
  docs/AUDIT.md:3339   数=588   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:3348   数=5   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:3358   数=148   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:3383   数=3   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:3384   数=168   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:3416   数=0   cmd=`grep -c 'src/' tools/check_orphan_fixtures.py`   rc=1 
  docs/AUDIT.md:3533   数=76   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   rc=128 fatal: Needed a single revision
  docs/AUDIT.md:3557   数=0   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   rc=128 fatal: Needed a single revision
  docs/AUDIT.md:3650   数=23   cmd=`sed -n '3849,3853p'`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:3654   数=3851   cmd=`sed -n '3851p' docs/AUDIT.md`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:3747   数=1   cmd=`git ls-tree -r --name-only`   rc=129 usage: git ls-tree [<options>] <tree-ish> [<path>...]
  docs/AUDIT.md:4024   数=2303496   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:4028   数=5   cmd=`grep -rn '"rand"' src/`   rc=1 
  docs/AUDIT.md:4073   数=4104   cmd=`grep 'SYNTAX.md:468'`   rc=1 
  docs/AUDIT.md:4079   数=344   cmd=`git grep -n '"gui_show"' 344a37b -- src/`   rc=1 
  docs/AUDIT.md:4103   数=7   cmd=`sed -n '528p'`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:4117   数=0   cmd=`grep -c meta_test CMakeLists.txt`   rc=1 
  docs/AUDIT.md:4121   数=0   cmd=`grep -rn svprobe docs/*.md CMakeLists.txt`   rc=2 grep: docs/*.md: No such file or directory
  docs/AUDIT.md:4128   数=1756   cmd=`git grep -n 'STMT_WINDOW' -- src/`   rc=1 
  docs/AUDIT.md:4152   数=344   cmd=`git grep -n 'OP_CALL_BUILTIN' 344a37b -- . ':!docs/' ':!future/'`   rc=1 
  docs/AUDIT.md:4162   数=0   cmd=`grep -rn 'self-check init' --include='*.py' --include='*.js' --include='*.sh' CMakeLists.txt`   rc=2 grep: init': No such file or directory
  docs/AUDIT.md:4163   数=0   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:4208   数=8   cmd=`git grep '无法读取文件' -- src/`   rc=1 
  docs/AUDIT.md:4210   数=000   cmd=`git grep -P`   rc=128 fatal: no pattern given
  docs/AUDIT.md:4251   数=919   cmd=`git ls-tree -r -z --name-only`   rc=129 usage: git ls-tree [<options>] <tree-ish> [<path>...]
  docs/AUDIT.md:4325   数=7   cmd=`git log --all -S`   rc=129 error: switch `S' requires a value
  docs/AUDIT.md:4556   数=06   cmd=`git cat-file -e d4b65c6:docs/PLAN_V06.md`   rc=128 fatal: path 'docs/PLAN_V06.md' exists on disk, but not in 'd4b65c6'
  docs/AUDIT.md:4570   数=34   cmd=`grep -n 'selfhost' tools/logic_semantics.test.py`   rc=1 
  docs/AUDIT.md:4759   数=95   cmd=`grep -rln '独佔全部锁'`   命令片段不完整（参数写在正文里）：`grep -rln '独佔全部锁'`
  docs/AUDIT.md:4942   数=10   cmd=`git grep`   rc=128 fatal: no pattern given
  docs/AUDIT.md:5029   数=35   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:5097   数=0   cmd=`git rev-parse --short 'v0.5.1^{commit}'`   rc=128 fatal: Needed a single revision
  docs/AUDIT.md:5316   数=1   cmd=`git log -S`   rc=129 error: switch `S' requires a value
  docs/AUDIT.md:5363   数=0   cmd=`git merge-base --is-ancestor 199e240 d23691e`   rc=1 
  docs/AUDIT.md:5376   数=40   cmd=`git grep`   rc=128 fatal: no pattern given
  docs/AUDIT.md:5387   数=4106   cmd=`git merge-base --is-ancestor`   rc=129 usage: git merge-base [-a | --all] <commit> <commit>...
  docs/AUDIT.md:5886   数=75   cmd=`grep -rn "OP_OR" src/compiler/`   rc=1 
  docs/AUDIT.md:5919   数=459   cmd=`git log`   rc=-1 
  docs/AUDIT.md:5969   数=2   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:5995   数=2   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6030   数=860   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6050   数=2   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:6058   数=0   cmd=`git grep -n 'gate.sh' -- .github`   rc=1 
  docs/AUDIT.md:6064   数=77   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:6159   数=0   cmd=`git grep -n 'gate.sh' -- .github`   rc=1 
  docs/AUDIT.md:6173   数=77   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/AUDIT.md:6296   数=96   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6461   数=77410   cmd=`git merge-base --is-ancestor d4b65c6 022d002`   rc=1 
  docs/AUDIT.md:6533   数=06   cmd=`grep -n 'params_precompiled' CMakeLists.txt`   rc=1 
  docs/AUDIT.md:6543   数=12   cmd=`git grep -n 'required-for' -- .github tools CMakeLists.txt`   rc=1 
  docs/AUDIT.md:6613   数=1   cmd=`git log -S`   rc=129 error: switch `S' requires a value
  docs/AUDIT.md:6929   数=18   cmd=`git grep`   rc=128 fatal: no pattern given
  docs/AUDIT.md:7771   数=19   cmd=`grep -l`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7773   数=19   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7833   数=9   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7952   数=4   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:54   数=142   cmd=`ctest --test-dir build --output-on-failure -j4`   rc=-1 
  docs/BOARD.md:114   数=41   cmd=`grep -rln -iE 'forge'`   命令片段不完整（参数写在正文里）：`grep -rln -iE 'forge'`
  docs/BOARD.md:130   数=0   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:141   数=3   cmd=`git log --all`   rc=-1 
  docs/BOARD.md:158   数=1   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:160   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:163   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:164   数=0   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:165   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:166   数=5   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:269   数=1   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:285   数=0   cmd=`grep -c _WIN32`   rc=1 
  docs/DECFY_DESIGN.md:27   数=10   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:28   数=3   cmd=`grep -rn "emit_from_bytecode" src/`   rc=1 
  docs/DECFY_DESIGN.md:43   数=0   cmd=`grep -rn "emit_from_bytecode" src/`   rc=1 
  docs/DECFY_DESIGN.md:50   数=2   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:189   数=3   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:223   数=69   cmd=`grep -cE '^ *L_[A-Z0-9_]+:' src/vm/vm.c`   rc=2 grep: *L_[A-Z0-9_]+:': No such file or directory
  docs/DECFY_DESIGN.md:292   数=141   cmd=`sed -n '54p' tools/gate.sh`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:313   数=07   cmd=`grep -n 'orphan_fixtures' tools/gate.sh`   rc=1 
  docs/DECFY_DESIGN.md:380   数=3   cmd=`grep -c 'set_union' src/vm/vm.c`   rc=1 
  docs/DECFY_DESIGN.md:481   数=2026   cmd=`grep -c vm_throw`   rc=1 
  docs/DECFY_DESIGN.md:493   数=3   cmd=`grep -qF`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:542   数=3   cmd=`grep -n 独立`   rc=1 
  docs/DECFY_DESIGN.md:547   数=7   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:648   数=4262   cmd=`grep -rl`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:722   数=0   cmd=`grep -c meta_test CMakeLists.txt`   rc=1 
  docs/DECFY_DESIGN.md:821   数=48   cmd=`grep -c 'case OP_'`   rc=2 grep: OP_': No such file or directory
  docs/DECFY_DESIGN.md:926   数=1   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:978   数=04   cmd=`grep -c "expected 'expression'"`   rc=2 grep: 'expression'": No such file or directory
  docs/DECFY_DESIGN.md:1056   数=48   cmd=`grep -c 'case OP_' src/vm/vm.c`   rc=2 grep: OP_': No such file or directory
  docs/DECFY_DESIGN.md:1064   数=4   cmd=`grep -rn 'im_typeset_union' src/`   rc=1 
  docs/DECFY_DESIGN.md:1067   数=138   cmd=`grep -n '十五个阶段' docs/BOARD.md`   rc=1 
  docs/DECFY_DESIGN.md:1071   数=1348   cmd=`git merge-base --is-ancestor a85cdf8 stream/builtin-contract-rulings`   rc=1 
  docs/DECFY_DESIGN.md:1077   数=0   cmd=`grep -rn 'Total Tests: 131' docs/ tools/`   rc=2 grep: Tests:: No such file or directory
  docs/DECFY_DESIGN.md:1078   数=3   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:1092   数=2026   cmd=`grep -rn "emit_from_bytecode" src/`   rc=1 
  docs/DECFY_DESIGN.md:1093   数=2026   cmd=`grep -n`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:1095   数=69   cmd=`grep -c Bytecode`   rc=1 
  docs/DECFY_DESIGN.md:1102   数=134   cmd=`grep -rn "is already registered" src/`   rc=2 grep: already: No such file or directory
  docs/DECFY_DESIGN.md:1107   数=2026   cmd=`grep -rnE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]' --include=*.im .`   rc=1 
  docs/DECFY_DESIGN.md:1110   数=2026   cmd=`sed -n '1218,1227p' src/parser/parser.c`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1111   数=2026   cmd=`sed -n '3192,3200p' src/vm/vm.c`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1112   数=2026   cmd=`sed -n '54p' tools/gate.sh`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1113   数=69   cmd=`grep -nE '^ *L_[A-Z0-9_]+:' src/vm/vm.c`   rc=2 grep: *L_[A-Z0-9_]+:': No such file or directory
  docs/DECFY_DESIGN.md:1114   数=139   cmd=`sed -n '54p' tools/gate.sh`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1115   数=2026   cmd=`sed -n '54p' tools/gate.sh`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1116   数=12   cmd=`sed -n '54p' tools/gate.sh`   rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/PLAN_V06.md:95   数=0   cmd=`grep -c 'OP_' src/compilation/aot_native.c`   rc=1 
  docs/PLAN_V06.md:96   数=1   cmd=`grep -c 'OP_' src/compilation/wasm_backend.c`   rc=1 
  docs/PLAN_V06.md:515   数=0   cmd=`git grep -n -- '--no-gui' HEAD -- src/`   rc=1 
  docs/PLAN_V06.md:518   数=52   cmd=`git grep -nw -i 'entity' HEAD -- CMakeLists.txt`   rc=1 
  docs/RELEASE_0.5.1.md:14   数=148   cmd=`git rev-parse --short 'v0.5.1^{commit}'`   rc=128 fatal: Needed a single revision
  docs/RELEASE_0.5.2.md:12   数=148   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   rc=128 fatal: Needed a single revision
  docs/REQUIREMENTS_ANALYSIS.md:23   数=0   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/REQUIREMENTS_ANALYSIS.md:205   数=100   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/STATUS.md:42   数=76   cmd=`git rev-parse 'v0.5.2^{commit}'`   rc=128 fatal: ambiguous argument ''v0.5.2^{commit}'': unknown revision or path not in the working tree.
  docs/STATUS.md:112   数=8256   cmd=`grep "128 + 8192"`   rc=2 grep: +: No such file or directory
  docs/STATUS.md:327   数=826   cmd=`grep -rn 'im_jit_mode' src/`   rc=1 
  docs/STATUS.md:656   数=3   cmd=`git diff f18fc81`   rc=128 fatal: ambiguous argument 'f18fc81': unknown revision or path not in the working tree.
  docs/STATUS.md:672   数=1   cmd=`grep -rn "commitment" src/ --include=*.c --include=*.h -i`   rc=1 
  docs/STATUS.md:774   数=89   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/STATUS.md:794   数=10   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:1011   数=0   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/STATUS.md:1101   数=1161   cmd=`git diff --stat 5305ebf..stream/xlang-bridge`   rc=128 fatal: ambiguous argument '5305ebf..stream/xlang-bridge': unknown revision or path not in the working tree.
  docs/STATUS.md:1132   数=5   cmd=`git diff --name-only main...stream/wasm-simd-gc`   rc=128 fatal: ambiguous argument 'main...stream/wasm-simd-gc': unknown revision or path not in the working tree.
  docs/STATUS.md:1234   数=69   cmd=`git log`   rc=-1 
  docs/STATUS.md:1371   数=86   cmd=`ctest --test-dir build`   rc=-1 
  docs/STATUS.md:1436   数=101   cmd=`ctest --test-dir build`   rc=-1 
  docs/STATUS.md:1767   数=41   cmd=`grep -rln -iE 'forge'`   命令片段不完整（参数写在正文里）：`grep -rln -iE 'forge'`
  docs/STATUS.md:2059   数=69   cmd=`grep -qE`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:2078   数=67   cmd=`grep -c "async fn"`   rc=2 grep: fn": No such file or directory
  docs/STATUS.md:2090   数=4   cmd=`grep -c "^async fn"`   rc=2 grep: fn": No such file or directory
  docs/STATUS.md:2192   数=75   cmd=`grep -qE`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:2500   数=1   cmd=`grep -rn t_sugar_desugared CMakeLists.txt tools/`   rc=1 
  docs/STATUS.md:2910   数=15   cmd=`git show HEAD:docs/AUDIT.md`   rc=-1 
  docs/STATUS.md:3081   数=10   cmd=`grep -rn "SetObj" src/`   rc=1 
  docs/STATUS.md:3110   数=114   cmd=`grep -E`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3205   数=117   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3261   数=118   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3372   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3374   数=0   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3450   数=1   cmd=`grep -Ec`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3702   数=11   cmd=`grep -n`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3888   数=38   cmd=`sed`   rc=1 Usage: sed [OPTION]... {script-only-if-no-other-script} [input-file]...
  docs/SYNTAX.md:528   数=2026   cmd=`grep -rn '"rand"' src/runtime/runtime_posix.c`   rc=1 
  docs/THREADS_V06.md:131   数=8   cmd=`grep -c 'VM_GSHARD'`   rc=1 
  docs/TYPESET_V06.md:164   数=101   cmd=`git grep -lw be main -- '*.im'`   rc=1 
  docs/TYPESET_V06.md:224   数=2026   cmd=`grep '"count"' src/runtime/runtime.c src/runtime/runtime_posix.c src/vm/vm.c`   rc=1 
  docs/TYPESET_V06.md:396   数=117   cmd=`grep -n '"size"' src/runtime/runtime.c src/runtime/runtime_posix.c`   rc=1 
  docs/TYPESET_V06.md:438   数=5   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:187   数=97   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:237   数=71   cmd=`grep 'Error at line'`   rc=2 grep: at: No such file or directory
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:239   数=67   cmd=`git log -S`   rc=129 error: switch `S' requires a value
  docs/streams/builtin-platform-census.md:42   数=2026   cmd=`grep -rn '"rand"' src/`   rc=1 
  docs/streams/crp-in-engine.md:137   数=89   cmd=`ctest --test-dir build -j4`   rc=-1 
  docs/streams/docs-audit.md:82   数=21503   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/eidos-state.md:75   数=04   cmd=`grep -nw`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/eidos-state.md:93   数=62   cmd=`git grep`   rc=128 fatal: no pattern given
  docs/streams/json-min-nul-escape.md:184   数=2026   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/surgery-verify-report.md:162   数=29   cmd=`grep -rn 'be_bound' src/`   rc=1 
  docs/streams/surgery-verify-report.md:246   数=0   cmd=`grep -rn 'be_bound' src/`   rc=1 
  docs/streams/win-source-attribution.md:20   数=73   cmd=`grep -c re_seq src/runtime/runtime_posix.c`   rc=1 
  docs/streams/xlang-bridge.md:91   数=95   cmd=`ctest`   命令片段不完整（参数写在正文里）：`ctest`
  future/multi-agent-coordination-bridge.md:434   数=7   cmd=`grep -n "status" tools/crp_relay.js`   rc=1 
  future/multi-agent-coordination-bridge.md:456   数=103   cmd=`grep -n "status" tools/crp_relay.js`   rc=1 
  future/multi-agent-coordination-bridge.md:647   数=506   cmd=`grep -niE "test" README.md`   rc=1 
  tools/README.md:200   数=41   cmd=`grep -c 'FAIL_REGULAR_EXPRESSION' CMakeLists.txt`   rc=1 
  tools/README.md:201   数=71   cmd=`grep -c 'gated_run.py' CMakeLists.txt`   rc=1 
  website/scratch/cmdnum_criteria.md:17   数=8256   cmd=`grep "128 + 8192"`   rc=2 grep: +: No such file or directory
  website/scratch/fifth_tier_visible.md:63   数=17   cmd=`git show undefined:…`   rc=128 fatal: invalid object name 'undefined'.
  website/scratch/registered_sites_audit.md:14   数=6   cmd=`grep -c`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  website/scratch/registered_sites_audit.md:57   数=1   cmd=`grep`   rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...

=== ④b 逐处（前 20）
  docs/AUDIT.md:94   数=1   cmd=`git ls-files '*.im'`   命令有输出但没有数：""
  docs/AUDIT.md:96   数=32   cmd=`git diff`   命令有输出但没有数：""
  docs/AUDIT.md:592   数=0   cmd=`ctest -R contract_suite_runtime`   命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/AUDIT.md:3281   数=0   cmd=`git ls-files 'src/**/*.c'`   命令有输出但没有数：""
  docs/AUDIT.md:3379   数=4   cmd=`git diff --name-only`   命令有输出但没有数：""
  docs/AUDIT.md:3553   数=751   cmd=`git rev-parse`   命令有输出但没有数：""
  docs/AUDIT.md:3565   数=30   cmd=`git merge-base --is-ancestor 30ee4d0 HEAD`   命令有输出但没有数：""
  docs/AUDIT.md:3753   数=68   cmd=`git diff --numstat`   命令有输出但没有数：""
  docs/AUDIT.md:4062   数=353   cmd=`git ls-files 'src/**/*.c'`   命令有输出但没有数：""
  docs/AUDIT.md:4164   数=15   cmd=`git status --porcelain`   命令有输出但没有数："?? Testing/\n?? website/scratch/custody_criteria.md\n?? websit"
  docs/AUDIT.md:4181   数=79   cmd=`git ls-files -- 'vtest/*.im'`   命令有输出但没有数：""
  docs/AUDIT.md:4214   数=8248   cmd=`git log --diff-filter=D -- '**/sync.im'`   命令有输出但没有数：""
  docs/AUDIT.md:4230   数=854   cmd=`git diff --numstat`   命令有输出但没有数：""
  docs/AUDIT.md:5227   数=1   cmd=`git diff --stat -- src/`   命令有输出但没有数：""
  docs/AUDIT.md:5353   数=17   cmd=`git diff --numstat`   命令有输出但没有数：""
  docs/AUDIT.md:6318   数=1   cmd=`git diff --stat`   命令有输出但没有数：""
  docs/AUDIT.md:6437   数=3   cmd=`ctest -R case_alias_runtime`   命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/AUDIT.md:6463   数=77410   cmd=`git diff --stat dc9a33f 77410cb -- docs/DECFY_DESIGN.md`   命令有输出但没有数：""
  docs/AUDIT.md:6467   数=0   cmd=`git diff --numstat`   命令有输出但没有数：""
  docs/BOARD.md:98   数=30   cmd=`ctest 143/143`   命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
```
