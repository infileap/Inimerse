# 丙⁷ 逐字留档：把「把命令与值并排写下」的句子做成可判

四件套
```
$ pwd
/home/sakiko/inimerse/.worktrees/website-ledger-readat
$ git rev-parse HEAD
f9111301fa1957a5d752f14116fac77c954ef4de
$ git status --porcelain
?? website/scratch/cmdnum_criteria.md
?? website/scratch/cmdnum_probe.js
?? website/scratch/cmdnum_reading.md
?? website/scratch/cmdnum_transcript.md
$ git rev-parse origin/main
b94090d86c461ca2c42dbb575f05d6d09001959e
$ git rev-list --count origin/main..HEAD
2
$ git diff --stat origin/main HEAD -- website/
 website/scratch/self_carrier_transcript.md |  63 ++++++++++++
 4 files changed, 438 insertions(+)
```

## 一、判据 D1–D10 的落点

`website/scratch/cmdnum_criteria.md` —— sha256:
```
2060d2f19c952793ed7596eb06dc37651f9638a8b54d87dd04bc2f10da23b413  website/scratch/cmdnum_criteria.md
```

## 二、尺子的源码（`website/scratch/cmdnum_probe.js`）

```js
#!/usr/bin/env node
// 丙⁷ 探针：把「把命令与值并排写下」的句子做成可判。
//
// 用法：node website/scratch/cmdnum_probe.js <repoRoot>
//
// ★ 安全边界（本探针会真的去跑文档里的命令，所以必须先划死允许表）：
//   只跑【匹配下列形状】的命令；任何含 ; & | > < $ ( ) ` 换行 的命令一律不跑（落档 ③）。
//   git 一族只跑只读子命令（ls-files/ls-tree/rev-list/rev-parse/show/grep/cat-file/merge-base/count-objects）。
//   其余 git 子命令（rm/checkout/reset/commit/push/clean/...）一律不跑。
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

// ---- 枚举：受跟踪的 .md ----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);

// ★ 两种「数」的形状（判据 D8）：
//   S1 = 加粗的【数】        `**12**`
//   S2 = 加粗的【短语】里含一个数  `**12 份受跟踪 `.js`**`
//   两条口径给出的分母不同，且 S1 看不见派件点名的那三处。
const NUM_S1 = /\*\*(\d[\d,]*)\*\*/g;
const NUM_S2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const numS1 = (line) => [...line.matchAll(NUM_S1)].map((m) => m[1]);
const numS2 = (line) => [...line.matchAll(NUM_S2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean);
// ★ S1 与 S2 的【并】—— 这是本件实际使用的口径，它的射程写在名字里
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
    rows.push({ file: f, line: i + 1, nums, spans, cmds, text: line,
      shape: numS1(line).length ? 'S1' : 'S2' });
  });
}
const withCmd = rows.filter((r) => r.cmds.length > 0);
// 三种分母并排（判据 D6/D8）：S1 ／ S1∪S2 ／ 其中带命令的
const s1Rows = rows.filter((r) => r.shape === 'S1');
const s1WithCmd = s1Rows.filter((r) => r.cmds.length > 0);
const cache = new Map();

// ---- 分类 ----
const grade = { '①': [], '②': [], '③': [], '④': [] };
for (const r of withCmd) {
  // 一处 = 一行里的一条命令 与 这一行的第一个数
  const cmd = r.cmds[0], num = r.nums[0];
  const why = whyNotRunnable(cmd);
  if (why) { grade['③'].push({ ...r, cmd, num, why, whyKind: '③a 不许跑' }); continue; }
  let res = cache.get(cmd);
  if (!res) { res = run(cmd); cache.set(cmd, res); }
  if (res.rc !== 0) { grade['③'].push({ ...r, cmd, num, why: 'rc=' + res.rc + ' ' + (res.err || '').split('\n')[0], whyKind: '③b 跑了但失败' }); continue; }
  const got = (res.out.match(/-?\d[\d,]*/g) || []).map((x) => x.replace(/,/g, ''));
  if (!got.length) { grade['④'].push({ ...r, cmd, num, why: '命令有输出但没有数：' + JSON.stringify(res.out.slice(0, 60)) }); continue; }
  const hit = got.includes(num.replace(/,/g, ''));
  grade[hit ? '②' : '①'].push({ ...r, cmd, num, got: got.slice(0, 4).join('/') });
}

const say = (s = '') => console.log(s);
say('=== 分母（三个，并排，不许合 —— 判据 D6/D8）');
say('  S1（加粗的【数】`**N**`）              = ' + s1Rows.length + ' 处（' + new Set(s1Rows.map((r) => r.file)).size + ' 个文件）');
say('  S1∪S2（加粗的【短语】里含数也算）      = ' + rows.length + ' 处（' + new Set(rows.map((r) => r.file)).size + ' 个文件）');
say('  其中带命令的（四档建在它上面，= 分母 B）= ' + withCmd.length + ' 处（' + new Set(withCmd.map((r) => r.file)).size + ' 个文件）');
say('  ★ 光用 S1 口径时，带命令的那一层 = ' + s1WithCmd.length + ' 处');
say();
say('=== 四档（分母 = B）');
for (const g of ['①', '②', '③', '④']) say('  ' + g + ' = ' + String(grade[g].length).padStart(4));
const sum = ['①', '②', '③', '④'].reduce((a, g) => a + grade[g].length, 0);
say('  和 = ' + sum + '  ⇒ ' + (sum === withCmd.length ? '= 分母 B ✓' : '★ ≠ 分母 B'));
say();
say('=== ★ 档 ③ 内部再分两格（判据 D4）');
say('  ③a 不许跑（允许表挡下）    = ' + grade['③'].filter((r) => r.whyKind === '③a 不许跑').length);
say('  ③b 跑了但失败              = ' + grade['③'].filter((r) => r.whyKind === '③b 跑了但失败').length);
say();
say('=== ★ 派件说「那三处必须在内」—— 逐处核对（三种口径）');
for (const [f, l] of [['website/scratch/two_rulers_side_by_side.md', 9], ['website/scratch/third_ruler.md', 8], ['website/scratch/fourth_ruler.md', 12]]) {
  const line = fs.readFileSync(path.join(ROOT, f), 'utf8').split('\n')[l - 1];
  const g = ['①', '②', '③', '④'].find((x) => grade[x].some((r) => r.file === f && r.line === l)) || '—';
  say('  ' + f + ':' + l);
  say('      S1=' + (numS1(line).length ? '看见' : '★ 看不见') + '  S2=' + (numS2(line).length ? '看见' : '看不见') + '  带命令=' + (withCmd.some((r) => r.file === f && r.line === l) ? '是' : '★ 否') + '  档=' + g);
}
say();
say('=== ① 逐处（位置 ＋ 那个数 ＋ 今天跑出来是几）');
grade['①'].forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   今天=' + r.got + '   cmd=`' + r.cmd + '`'));
say();
say('=== ③ 逐处（跑不动的理由）');
grade['③'].forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   cmd=`' + r.cmd + '`   理由=' + r.why));
say();
say('=== ④ 逐处');
grade['④'].forEach((r) => say('  ' + r.file + ':' + r.line + '   数=' + r.num + '   cmd=`' + r.cmd + '`   理由=' + r.why));
fs.writeFileSync(path.join(ROOT, 'website/scratch/.build/cmdnum_grade.json'), JSON.stringify({ A: rows.length, B: withCmd.length, grade }, null, 1));
```

## 三、四次运行的逐字输出

### 运行 1：S1 口径（加粗的**数**）
```
=== 分母（三个，并排，不许合 —— 判据 D6/D8）
  S1（加粗的【数】`**N**`）              = 621 处（49 个文件）
  S1∪S2（加粗的【短语】里含数也算）      = 4478 处（108 个文件）
  其中带命令的（四档建在它上面，= 分母 B）= 565 处（46 个文件）
  ★ 光用 S1 口径时，带命令的那一层 = 164 处

=== 四档（分母 = B）
  ① =   59
  ② =   28
  ③ =  416
  ④ =   62
  和 = 565  ⇒ = 分母 B ✓

=== ★ 档 ③ 内部再分两格（判据 D4）
  ③a 不许跑（允许表挡下）    = 243
  ③b 跑了但失败              = 173

=== ★ 派件说「那三处必须在内」—— 逐处核对（三种口径）
  website/scratch/two_rulers_side_by_side.md:9
      S1=★ 看不见  S2=看见  带命令=★ 否  档=—
  website/scratch/third_ruler.md:8
      S1=★ 看不见  S2=看见  带命令=★ 否  档=—
  website/scratch/fourth_ruler.md:12
      S1=★ 看不见  S2=看见  带命令=是  档=④

=== ① 逐处（位置 ＋ 那个数 ＋ 今天跑出来是几）
  docs/API.md:481   数=124   今天=0   cmd=`ctest -N`
  docs/AUDIT.md:648   数=64   今天=46/26/579/5047305   cmd=`git rev-list 8248e08..33b6dd2^ -- src/vm/vm.c`
  docs/AUDIT.md:704   数=32   今天=68/1283/0/1827   cmd=`grep -rn compCount src/`
  docs/AUDIT.md:2259   数=858   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:2627   数=12   今天=0   cmd=`ctest -N`
  docs/AUDIT.md:3330   数=55   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:3382   数=55   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:3505   数=74   今天=80   cmd=`git rev-list --count v0.5.1..v0.5.2`
  docs/AUDIT.md:3758   数=3709   今天=128/128/128/128   cmd=`git ls-files -z`
  docs/AUDIT.md:3883   数=15   今天=4/6/13/6   cmd=`grep -rn im_jit_mode src/`
  docs/AUDIT.md:4040   数=4017   今天=0   cmd=`wc -l`
  docs/AUDIT.md:4098   数=917   今天=9111301/1957/5/752   cmd=`git show`
  docs/AUDIT.md:4102   数=500   今天=10/10   cmd=`git diff --numstat 58f1a44 -- docs/SYNTAX.md`
  docs/AUDIT.md:4178   数=27   今天=128/128/128/128   cmd=`git ls-files -z`
```

### 运行 2：完整输出（`node website/scratch/cmdnum_probe.js .`，567 行）
```
=== 分母（三个，并排，不许合 —— 判据 D6/D8）
  S1（加粗的【数】`**N**`）              = 621 处（49 个文件）
  S1∪S2（加粗的【短语】里含数也算）      = 4478 处（108 个文件）
  其中带命令的（四档建在它上面，= 分母 B）= 565 处（46 个文件）
  ★ 光用 S1 口径时，带命令的那一层 = 164 处

=== 四档（分母 = B）
  ① =   59
  ② =   28
  ③ =  416
  ④ =   62
  和 = 565  ⇒ = 分母 B ✓

=== ★ 档 ③ 内部再分两格（判据 D4）
  ③a 不许跑（允许表挡下）    = 243
  ③b 跑了但失败              = 173

=== ★ 派件说「那三处必须在内」—— 逐处核对（三种口径）
  website/scratch/two_rulers_side_by_side.md:9
      S1=★ 看不见  S2=看见  带命令=★ 否  档=—
  website/scratch/third_ruler.md:8
      S1=★ 看不见  S2=看见  带命令=★ 否  档=—
  website/scratch/fourth_ruler.md:12
      S1=★ 看不见  S2=看见  带命令=是  档=④

=== ① 逐处（位置 ＋ 那个数 ＋ 今天跑出来是几）
  docs/API.md:481   数=124   今天=0   cmd=`ctest -N`
  docs/AUDIT.md:648   数=64   今天=46/26/579/5047305   cmd=`git rev-list 8248e08..33b6dd2^ -- src/vm/vm.c`
  docs/AUDIT.md:704   数=32   今天=68/1283/0/1827   cmd=`grep -rn compCount src/`
  docs/AUDIT.md:2259   数=858   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:2627   数=12   今天=0   cmd=`ctest -N`
  docs/AUDIT.md:3330   数=55   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:3382   数=55   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:3505   数=74   今天=80   cmd=`git rev-list --count v0.5.1..v0.5.2`
  docs/AUDIT.md:3758   数=3709   今天=128/128/128/128   cmd=`git ls-files -z`
  docs/AUDIT.md:3883   数=15   今天=4/6/13/6   cmd=`grep -rn im_jit_mode src/`
  docs/AUDIT.md:4040   数=4017   今天=0   cmd=`wc -l`
  docs/AUDIT.md:4098   数=917   今天=9111301/1957/5/752   cmd=`git show`
  docs/AUDIT.md:4102   数=500   今天=10/10   cmd=`git diff --numstat 58f1a44 -- docs/SYNTAX.md`
  docs/AUDIT.md:4178   数=27   今天=128/128/128/128   cmd=`git ls-files -z`
  docs/AUDIT.md:4194   数=919   今天=128/128/128/128   cmd=`git ls-files -z`
  docs/AUDIT.md:4568   数=06   今天=9111301/1957/5/752   cmd=`git show`
  docs/AUDIT.md:5339   数=57   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:6817   数=2483   今天=128/128/128/128   cmd=`git ls-files`
  docs/AUDIT.md:6849   数=196   今天=128/128/128/128   cmd=`git ls-files --error-unmatch`
  docs/AUDIT.md:6879   数=1   今天=911130/18/12   cmd=`git log --oneline -1`
  docs/AUDIT.md:7599   数=16   今天=128/128/128/128   cmd=`git ls-files`
  docs/BOARD.md:151   数=63   今天=39/1/1/802   cmd=`grep -in contract CMakeLists.txt`
  docs/BOARD.md:274   数=12   今天=0   cmd=`ctest -N`
  docs/BOARD.md:275   数=38   今天=0   cmd=`ctest -N`
  docs/BOARD.md:280   数=12   今天=0   cmd=`ctest -N`
  docs/BOARD.md:288   数=9   今天=9111301/1957/5/752   cmd=`git rev-parse HEAD`
  docs/DECFY_DESIGN.md:538   数=37   今天=2   cmd=`grep -c '独立' docs/DECFY_DESIGN.md`
  docs/DECFY_DESIGN.md:1072   数=056   今天=12/1/9/3   cmd=`git diff --stat 5cf4aa1 056e67b`
  docs/DECFY_DESIGN.md:1075   数=3   今天=0   cmd=`wc -l`
  docs/DECFY_DESIGN.md:1117   数=2026   今天=7/57   cmd=`git log -1 --format=%p 3743b99`
  docs/DECFY_DESIGN.md:1118   数=2026   今天=0   cmd=`wc -l`
  docs/DECFY_DESIGN.md:1121   数=1359   今天=12/1/9/3   cmd=`git diff --stat 5cf4aa1 056e67b`
  docs/DECFY_DESIGN.md:1124   数=2026   今天=9111301/1957/5/752   cmd=`git rev-parse HEAD`
  docs/HYGIENE.md:528   数=181   今天=128/128/128/128   cmd=`git ls-files`
  docs/PLAN_V06.md:76   数=4   今天=0/0/1/2   cmd=`git show d4b65c6^:selfhost/eval.im`
  docs/RELEASE_0.5.1.md:15   数=67   今天=113   cmd=`git rev-list --count v0.5.0..v0.5.1`
  docs/RELEASE_0.5.1.md:21   数=24   今天=0   cmd=`ctest -N`
  docs/STATUS.md:44   数=131   今天=0   cmd=`ctest -N`
  docs/STATUS.md:1152   数=923   今天=00/934/761/09   cmd=`git diff d923ec3..df82cf6 -- src/vm/vm.c`
  docs/STATUS.md:1442   数=58   今天=1/0   cmd=`ctest --show-only=json-v1`
  docs/STATUS.md:1507   数=0   今天=32/1/27/5   cmd=`git diff --stat 8248e08 HEAD -- selfhost/`
  docs/STATUS.md:2587   数=9   今天=39/1/1/802   cmd=`grep -in contract CMakeLists.txt`
  docs/STATUS.md:2904   数=78   今天=28/29/5   cmd=`git ls-files mods`
  docs/STATUS.md:2957   数=64   今天=46/26/579/5047305   cmd=`git rev-list 8248e08..33b6dd2^ -- src/vm/vm.c`
  docs/STATUS.md:3802   数=2   今天=0   cmd=`ctest -N`
  docs/STATUS.md:3872   数=64   今天=0   cmd=`ctest -N`
  docs/SYNTAX.md:913   数=134   今天=0   cmd=`ctest -N`
  docs/TYPESET_V06.md:437   数=2026   今天=94090/87/68   cmd=`git log --oneline -1 main`
  docs/TYPESET_V06.md:453   数=2026   今天=94090/87/68   cmd=`git log --oneline -1 origin/main`
  docs/streams/eidos-archive-crosscheck.md:166   数=8   今天=346/200/273/347   cmd=`git ls-tree --name-only HEAD future/archive/`
  docs/streams/eidos-diagnostic-fields.md:9   数=17571   今天=9111301/1957/5/752   cmd=`git rev-parse HEAD`
  docs/streams/v06-decision-points-delegation.md:9   数=9   今天=9111301/1957/5/752   cmd=`git rev-parse HEAD`
  docs/streams/win-source-attribution.md:12   数=27413   今天=9111301/1957/5/752   cmd=`git rev-parse HEAD`
  docs/streams/win-source-attribution.md:21   数=0   今天=429   cmd=`grep -n child_proc CMakeLists.txt`
  docs/streams/win-source-attribution.md:41   数=137   今天=0   cmd=`ctest -N`
  future/multi-agent-coordination-bridge.md:624   数=12   今天=128/128/128/128   cmd=`git ls-files`
  website/scratch/oldname_census.md:17   数=8   今天=-01/74/6/6   cmd=`git ls-files website/`
  website/scratch/red_direction_death_census.md:15   数=3   今天=0   cmd=`wc -l`
  website/scratch/two_rulers_side_by_side.md:34   数=12   今天=128/128/128/128   cmd=`git ls-files`

=== ③ 逐处（跑不动的理由）
  docs/API.md:378   数=1   cmd=`node tools/wasm_host.test.js tools/wasm_probe.wasm`   理由=本探针不跑这一族（可能写盘）
  docs/API.md:479   数=85   cmd=`ctest --test-dir build --output-on-failure`   理由=rc=-1 
  docs/API.md:497   数=2   cmd=`python3 tools/selfhost_bench.py --runs 5 --write-docs`   理由=本探针不跑这一族（可能写盘）
  docs/AUDIT.md:75   数=3166   cmd=`grep -rn "OP_OR" src/compiler/`   理由=rc=1 
  docs/AUDIT.md:457   数=0   cmd=`grep '"list"' src/compilation/*.c`   理由=rc=2 grep: src/compilation/*.c: No such file or directory
  docs/AUDIT.md:512   数=0   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：checkout
  docs/AUDIT.md:528   数=110   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:572   数=05   cmd=`git checkout HEAD -- src/vm/vm.c`   理由=git 子命令不在只读表里：checkout
  docs/AUDIT.md:616   数=45   cmd=`sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:757   数=114   cmd=`grep -E`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:808   数=0   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:882   数=82   cmd=`git show 2a14f68:<path> | sed -n '<n>p'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:1044   数=118   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1178   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1179   数=119   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1258   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:1287   数=5   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/AUDIT.md:1289   数=0   cmd=`grep -c getline`   理由=rc=1 
  docs/AUDIT.md:1438   数=5   cmd=`ctest -R "incremental|dep|compile|selfhost|cli_"`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:1984   数=130   cmd=`git stash push -- src/main.c`   理由=git 子命令不在只读表里：stash
  docs/AUDIT.md:2063   数=06   cmd=`git stash push -- src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：stash
  docs/AUDIT.md:2099   数=64   cmd=`git checkout`   理由=git 子命令不在只读表里：checkout
  docs/AUDIT.md:2124   数=7   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:2180   数=3   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/AUDIT.md:2677   数=134   cmd=`grep -c 'add_test('`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:2946   数=0   cmd=`grep -rn 'Total Tests: 131' docs/ tools/`   理由=rc=2 grep: Tests:: No such file or directory
  docs/AUDIT.md:2951   数=142   cmd=`git archive`   理由=git 子命令不在只读表里：archive
  docs/AUDIT.md:2963   数=0   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:2967   数=3365   cmd=`grep -n 'out of 11[0-3]' docs/STATUS.md`   理由=rc=2 grep: of: No such file or directory
  docs/AUDIT.md:2969   数=122   cmd=`node`   理由=本探针不跑这一族（可能写盘）
  docs/AUDIT.md:2997   数=1   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:3032   数=1   cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3045   数=1   cmd=`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3051   数=28   cmd=`git show <ref>:src/runtime/runtime.c`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3066   数=24   cmd=`git show c71ea00:src/runtime/runtime.c | sed -n '966p;971p;977p;981p;999p'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3070   数=968   cmd=`git show c71ea00:...`   理由=rc=128 error: object 96fad2311ed46223dbe7aa460b1e7c10a216db53 is a tree, not a commit
  docs/AUDIT.md:3099   数=44   cmd=`git rev-list --count <refA>..<refB>`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3134   数=148   cmd=`grep -c 'add_test('`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3285   数=0   cmd=`grep -c 'src/'`   理由=rc=1 
  docs/AUDIT.md:3287   数=0   cmd=`grep -c 'src/'`   理由=rc=1 
  docs/AUDIT.md:3303   数=43   cmd=`grep -c 'vm_register_builtin'`   理由=rc=1 
  docs/AUDIT.md:3319   数=2734   cmd=`git grep -n 'child_proc_' -- src/ tools/ vtest/`   理由=rc=1 
  docs/AUDIT.md:3335   数=0   cmd=`git show a64c4fd:docs/AUDIT.md | grep -c 'LICENSE'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3337   数=64   cmd=`git show 4b7b37c:docs/AUDIT.md | grep -c 'LICENSE'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3339   数=588   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:3370   数=15   cmd=`grep -c '\$REPO_ROOT/' tools/gate.sh`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3373   数=13   cmd=`git show 4b7b37c:tools/gate.sh | grep -c '\$REPO_ROOT/'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3387   数=2   cmd=`grep -o 'CMakeLists\.txt:[0-9-]*' docs/AUDIT.md`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3416   数=0   cmd=`grep -c 'src/' tools/check_orphan_fixtures.py`   理由=rc=1 
  docs/AUDIT.md:3522   数=675   cmd=`git -C "$REPO_ROOT" diff --name-only "$range"`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3533   数=76   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   理由=rc=128 fatal: Needed a single revision
  docs/AUDIT.md:3557   数=0   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   理由=rc=128 fatal: Needed a single revision
  docs/AUDIT.md:3561   数=0   cmd=`git reflog show v0.5.2`   理由=git 子命令不在只读表里：reflog
  docs/AUDIT.md:3563   数=2   cmd=`git merge --ff-only main`   理由=git 子命令不在只读表里：merge
  docs/AUDIT.md:3572   数=4   cmd=`git diff --name-only 4ca013d <head> -- tools/gate.sh`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3636   数=0   cmd=`git show origin/main:docs/DECFY_DESIGN.md | grep -c '尚未合进'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3640   数=4   cmd=`git show <你读的那棵树>:.gitignore | grep -cxF '/vtest/sayout.txt'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3648   数=3887   cmd=`git show <你读的那棵树>:docs/AUDIT.md | sed -n '3851p'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3650   数=23   cmd=`sed -n '3849,3853p'`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:3654   数=3851   cmd=`sed -n '3851p' docs/AUDIT.md`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:3685   数=3   cmd=`git archive`   理由=git 子命令不在只读表里：archive
  docs/AUDIT.md:3694   数=694   cmd=`git merge main`   理由=git 子命令不在只读表里：merge
  docs/AUDIT.md:3747   数=1   cmd=`git ls-tree -r --name-only`   理由=rc=129 usage: git ls-tree [<options>] <tree-ish> [<path>...]
  docs/AUDIT.md:3749   数=3   cmd=`python3 -c "import os; print(os.path.splitext('.gitignore'))"`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3759   数=2   cmd=`git ls-files | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:3788   数=0   cmd=`git grep -c '<name>' -- vtest/`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4024   数=2303496   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:4028   数=5   cmd=`grep -rn '"rand"' src/`   理由=rc=1 
  docs/AUDIT.md:4073   数=4104   cmd=`grep 'SYNTAX.md:468'`   理由=rc=1 
  docs/AUDIT.md:4079   数=344   cmd=`git grep -n '"gui_show"' 344a37b -- src/`   理由=rc=1 
  docs/AUDIT.md:4103   数=7   cmd=`sed -n '528p'`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/AUDIT.md:4117   数=0   cmd=`grep -c meta_test CMakeLists.txt`   理由=rc=1 
  docs/AUDIT.md:4121   数=0   cmd=`grep -rn svprobe docs/*.md CMakeLists.txt`   理由=rc=2 grep: docs/*.md: No such file or directory
  docs/AUDIT.md:4128   数=1756   cmd=`git grep -n 'STMT_WINDOW' -- src/`   理由=rc=1 
  docs/AUDIT.md:4152   数=344   cmd=`git grep -n 'OP_CALL_BUILTIN' 344a37b -- . ':!docs/' ':!future/'`   理由=rc=1 
  docs/AUDIT.md:4160   数=70   cmd=`git diff --stat c68894b d94622b -- <四目录> CMakeLists.txt tools/`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4162   数=0   cmd=`grep -rn 'self-check init' --include='*.py' --include='*.js' --include='*.sh' CMakeLists.txt`   理由=rc=2 grep: init': No such file or directory
  docs/AUDIT.md:4170   数=917   cmd=`git ls-tree -r --name-only <ref>`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4183   数=104   cmd=`cmake`   理由=本探针不跑这一族（可能写盘）
  docs/AUDIT.md:4208   数=8   cmd=`git grep '无法读取文件' -- src/`   理由=rc=1 
  docs/AUDIT.md:4210   数=000   cmd=`git grep -P`   理由=rc=128 fatal: no pattern given
  docs/AUDIT.md:4236   数=82   cmd=`git`   理由=git 后面没有子命令
  docs/AUDIT.md:4251   数=919   cmd=`git ls-tree -r -z --name-only`   理由=rc=129 usage: git ls-tree [<options>] <tree-ish> [<path>...]
  docs/AUDIT.md:4286   数=0   cmd=`grep -ciE 'gbk|cp936|iconv' tools/gate.sh`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4313   数=273   cmd=`git show 3f612b8^:src/index.js | grep -n`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4325   数=7   cmd=`git log --all -S`   理由=rc=129 error: switch `S' requires a value
  docs/AUDIT.md:4370   数=1   cmd=`git add -A`   理由=git 子命令不在只读表里：add
  docs/AUDIT.md:4388   数=62   cmd=`git grep -n '开放\|封闭' -- docs/ future/ | cut -c1-170 | head -30`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4390   数=300   cmd=`grep -v 'for \|case \|_ in '`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4556   数=06   cmd=`git cat-file -e d4b65c6:docs/PLAN_V06.md`   理由=rc=128 fatal: path 'docs/PLAN_V06.md' exists on disk, but not in 'd4b65c6'
  docs/AUDIT.md:4570   数=34   cmd=`grep -n 'selfhost' tools/logic_semantics.test.py`   理由=rc=1 
  docs/AUDIT.md:4659   数=8248   cmd=`git log -S 'capsv->type == VAL_BOOL' -- src/runtime/runtime.c`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4909   数=71   cmd=`grep -rnE '(\.|->)type *== *VAL_' src/ --include=*.c`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4927   数=0   cmd=`grep -nE 'STMT_TRY|EXPR_THROW|OP_THROW|OP_TRY|try_entries|catch'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:4942   数=10   cmd=`git grep`   理由=rc=128 fatal: no pattern given
  docs/AUDIT.md:5029   数=35   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:5097   数=0   cmd=`git rev-parse --short 'v0.5.1^{commit}'`   理由=rc=128 fatal: Needed a single revision
  docs/AUDIT.md:5134   数=25   cmd=`git commit`   理由=git 子命令不在只读表里：commit
  docs/AUDIT.md:5316   数=1   cmd=`git log -S`   理由=rc=129 error: switch `S' requires a value
  docs/AUDIT.md:5330   数=199   cmd=`git rm tools/aot_native_build.sh`   理由=git 子命令不在只读表里：rm
  docs/AUDIT.md:5363   数=0   cmd=`git merge-base --is-ancestor 199e240 d23691e`   理由=rc=1 
  docs/AUDIT.md:5376   数=40   cmd=`git grep`   理由=rc=128 fatal: no pattern given
  docs/AUDIT.md:5387   数=4106   cmd=`git merge-base --is-ancestor`   理由=rc=129 usage: git merge-base [-a | --all] <commit> <commit>...
  docs/AUDIT.md:5886   数=75   cmd=`grep -rn "OP_OR" src/compiler/`   理由=rc=1 
  docs/AUDIT.md:5919   数=459   cmd=`git log`   理由=rc=-1 
  docs/AUDIT.md:5975   数=4   cmd=`git check-ignore -v`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:5995   数=2   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6001   数=18   cmd=`bash -e`   理由=本探针不跑这一族（可能写盘）
  docs/AUDIT.md:6030   数=860   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6058   数=0   cmd=`git grep -n 'gate.sh' -- .github`   理由=rc=1 
  docs/AUDIT.md:6159   数=0   cmd=`git grep -n 'gate.sh' -- .github`   理由=rc=1 
  docs/AUDIT.md:6250   数=3   cmd=`python3 .probe-ie/fifth/four_words.py <ref>`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:6296   数=96   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:6306   数=0   cmd=`git cat-file -p c71ea00:tools/gate.sh | sed -n '54p'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:6411   数=17   cmd=`git worktree add --detach .worktrees/sv-ref-prev  c89e077~1`   理由=git 子命令不在只读表里：worktree
  docs/AUDIT.md:6413   数=21   cmd=`git worktree prune`   理由=git 子命令不在只读表里：worktree
  docs/AUDIT.md:6419   数=7   cmd=`git grep -n 'wt-f401f82\|wt-3f3e5b9' -- docs/`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:6461   数=77410   cmd=`git merge-base --is-ancestor d4b65c6 022d002`   理由=rc=1 
  docs/AUDIT.md:6521   数=06   cmd=`git check-ignore -v`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:6533   数=06   cmd=`grep -n 'params_precompiled' CMakeLists.txt`   理由=rc=1 
  docs/AUDIT.md:6543   数=12   cmd=`git grep -n 'required-for' -- .github tools CMakeLists.txt`   理由=rc=1 
  docs/AUDIT.md:6613   数=1   cmd=`git log -S`   理由=rc=129 error: switch `S' requires a value
  docs/AUDIT.md:6743   数=1   cmd=`git check-ignore -q`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:6763   数=73   cmd=`git check-ignore -q`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:6809   数=139   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:6831   数=1   cmd=`git check-ignore`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:6833   数=1   cmd=`git check-ignore`   理由=git 子命令不在只读表里：check-ignore
  docs/AUDIT.md:6929   数=18   cmd=`git grep`   理由=rc=128 fatal: no pattern given
  docs/AUDIT.md:7047   数=1   cmd=`git worktree prune`   理由=git 子命令不在只读表里：worktree
  docs/AUDIT.md:7183   数=5   cmd=`git show <ref>:<path>`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:7199   数=12   cmd=`git grep -l '硬规则' -- docs/ | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:7371   数=09   cmd=`git show b09a58a^:src/lexer/lexer.c | grep '"type"'`   理由=含 shell 元字符（本探针不许跑）
  docs/AUDIT.md:7771   数=19   cmd=`grep -l`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7773   数=19   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7833   数=9   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:7952   数=4   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/AUDIT.md:10792   数=12   cmd=`node protocol suites`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:54   数=142   cmd=`ctest --test-dir build --output-on-failure -j4`   理由=rc=-1 
  docs/BOARD.md:55   数=0   cmd=`python3 tools/im_diff_fuzz.py --count 120 --seed 1`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:56   数=39   cmd=`python3 tools/economy_migration.test.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:57   数=55   cmd=`node tools/dsh-inimerse/verify.mjs --live`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:58   数=12   cmd=`node tools/node_suites/run_all.js`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:61   数=0   cmd=`python3 tools/check_links.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:62   数=114   cmd=`python3 tools/check_orphan_fixtures.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:63   数=157   cmd=`python3 tools/check_orphan_targets.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:64   数=43   cmd=`python3 tools/check_line_refs.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:65   数=0   cmd=`python3 tools/check_release_tags.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:66   数=0   cmd=`python3 tools/check_text_integrity.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:67   数=0   cmd=`python3 tools/check_doc_paths.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:106   数=2693   cmd=`node tools/vverse_pack.js preview <pkg>`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:109   数=78   cmd=`python3 tools/check_links.py`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:112   数=16   cmd=`cargo test`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:113   数=36   cmd=`cargo tree`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:117   数=0   cmd=`git add -A`   理由=git 子命令不在只读表里：add
  docs/BOARD.md:118   数=1   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/BOARD.md:124   数=0   cmd=`git checkout main -- src/compilation/wasm_backend.{c,h}`   理由=git 子命令不在只读表里：checkout
  docs/BOARD.md:126   数=0   cmd=`git rm`   理由=git 子命令不在只读表里：rm
  docs/BOARD.md:127   数=1   cmd=`grep -n "exit(1)" src/parser/parser.c`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:128   数=1274   cmd=`grep -rniE '\bagent' src/`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:129   数=0   cmd=`grep -rn "compiler\.im" tools/ CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:130   数=0   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:131   数=2026   cmd=`git add`   理由=git 子命令不在只读表里：add
  docs/BOARD.md:132   数=11   cmd=`grep -an 'bytecode_add_string([^)]*\.\(start\|text\)' src/compiler/*.c src/vm/*.c`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:133   数=101   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/BOARD.md:137   数=0   cmd=`ctest -R '^type_registry_probe$'`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:138   数=0   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/BOARD.md:139   数=1   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:140   数=0   cmd=`git add`   理由=git 子命令不在只读表里：add
  docs/BOARD.md:141   数=3   cmd=`git log --all`   理由=rc=-1 
  docs/BOARD.md:147   数=107   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:149   数=3   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：checkout
  docs/BOARD.md:150   数=111   cmd=`git checkout HEAD -- src/vm/vm.c`   理由=git 子命令不在只读表里：checkout
  docs/BOARD.md:158   数=1   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:160   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:162   数=0   cmd=`ctest -R 'platform_probe|truthiness|union_member|…'`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:163   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:164   数=0   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:165   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:166   数=5   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:167   数=1   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/BOARD.md:170   数=1   cmd=`ctest -R "incremental|dep|compile|selfhost"`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:228   数=30   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/BOARD.md:262   数=1   cmd=`git stash push -- src/main.c`   理由=git 子命令不在只读表里：stash
  docs/BOARD.md:263   数=1   cmd=`git stash push`   理由=git 子命令不在只读表里：stash
  docs/BOARD.md:264   数=4609434218613702656   cmd=`git stash push -- src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：stash
  docs/BOARD.md:267   数=1   cmd=`git stash push -- src/vm/vm.c`   理由=git 子命令不在只读表里：stash
  docs/BOARD.md:269   数=1   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/BOARD.md:270   数=134   cmd=`git stash push -- src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：stash
  docs/BOARD.md:281   数=346   cmd=`grep -r "size("`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:283   数=148   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/BOARD.md:284   数=8   cmd=`git add -A`   理由=git 子命令不在只读表里：add
  docs/BOARD.md:285   数=0   cmd=`grep -c _WIN32`   理由=rc=1 
  docs/BOARD.md:286   数=149   cmd=`git worktree add --detach .worktrees/t2old 58f1a44`   理由=git 子命令不在只读表里：worktree
  docs/BOARD.md:287   数=150   cmd=`git worktree add --detach .worktrees/t2old 58f1a44`   理由=git 子命令不在只读表里：worktree
  docs/DECFY_DESIGN.md:26   数=10   cmd=`grep -n "OP_AND\|OP_OR" src/compiler/compiler.c`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:27   数=10   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:28   数=3   cmd=`grep -rn "emit_from_bytecode" src/`   理由=rc=1 
  docs/DECFY_DESIGN.md:32   数=26   cmd=`sed -n '<n>p' | wc -c`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:43   数=0   cmd=`grep -rn "emit_from_bytecode" src/`   理由=rc=1 
  docs/DECFY_DESIGN.md:44   数=10   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:50   数=2   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:189   数=3   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:201   数=0   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c | sort -u | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:223   数=69   cmd=`grep -cE '^ *L_[A-Z0-9_]+:' src/vm/vm.c`   理由=rc=2 grep: *L_[A-Z0-9_]+:': No such file or directory
  docs/DECFY_DESIGN.md:236   数=69   cmd=`sed -n '<区间>p' src/vm/vm.c`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:292   数=141   cmd=`sed -n '54p' tools/gate.sh`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:313   数=07   cmd=`grep -n 'orphan_fixtures' tools/gate.sh`   理由=rc=1 
  docs/DECFY_DESIGN.md:320   数=76   cmd=`python3 tools/check_orphan_fixtures.py`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:380   数=3   cmd=`grep -c 'set_union' src/vm/vm.c`   理由=rc=1 
  docs/DECFY_DESIGN.md:481   数=2026   cmd=`grep -c vm_throw`   理由=rc=1 
  docs/DECFY_DESIGN.md:493   数=3   cmd=`grep -qF`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:542   数=3   cmd=`grep -n 独立`   理由=rc=1 
  docs/DECFY_DESIGN.md:544   数=6   cmd=`node tools/wasm_run.js`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:547   数=7   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:648   数=4262   cmd=`grep -rl`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:722   数=0   cmd=`grep -c meta_test CMakeLists.txt`   理由=rc=1 
  docs/DECFY_DESIGN.md:821   数=48   cmd=`grep -c 'case OP_'`   理由=rc=2 grep: OP_': No such file or directory
  docs/DECFY_DESIGN.md:822   数=2   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c \| sort -u \| wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:823   数=3   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/wasm_backend.c \| sort -u \| wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:833   数=0   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:866   数=2026   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:868   数=0   cmd=`grep -rn 'EXPR_ARRAY' src/ | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:894   数=7   cmd=`node tools/wasm_run.js`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:926   数=1   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:978   数=04   cmd=`grep -c "expected 'expression'"`   理由=rc=2 grep: 'expression'": No such file or directory
  docs/DECFY_DESIGN.md:1009   数=0   cmd=`grep -rn 'EXPR_ARRAY' src/ | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1033   数=2   cmd=`git show 022d002:docs/AUDIT.md | grep -n 'DIVERGENCE'`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1056   数=48   cmd=`grep -c 'case OP_' src/vm/vm.c`   理由=rc=2 grep: OP_': No such file or directory
  docs/DECFY_DESIGN.md:1062   数=10   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1064   数=4   cmd=`grep -rn 'im_typeset_union' src/`   理由=rc=1 
  docs/DECFY_DESIGN.md:1065   数=1   cmd=`grep -c "expected 'expression'"`   理由=rc=2 grep: 'expression'": No such file or directory
  docs/DECFY_DESIGN.md:1066   数=3   cmd=`bash tools/gate.sh --only doc-paths`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:1067   数=138   cmd=`grep -n '十五个阶段' docs/BOARD.md`   理由=rc=1 
  docs/DECFY_DESIGN.md:1068   数=1   cmd=`grep -rln 'def find_engine' tools/ | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1071   数=1348   cmd=`git merge-base --is-ancestor a85cdf8 stream/builtin-contract-rulings`   理由=rc=1 
  docs/DECFY_DESIGN.md:1077   数=0   cmd=`grep -rn 'Total Tests: 131' docs/ tools/`   理由=rc=2 grep: Tests:: No such file or directory
  docs/DECFY_DESIGN.md:1078   数=3   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:1079   数=26   cmd=`git ls-files -z \| xargs -0 grep -n 'DECFY_DESIGN.md:[0-9]'`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1081   数=9   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:1083   数=2026   cmd=`grep -n '^### 3\.'`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1092   数=2026   cmd=`grep -rn "emit_from_bytecode" src/`   理由=rc=1 
  docs/DECFY_DESIGN.md:1093   数=2026   cmd=`grep -n`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/DECFY_DESIGN.md:1094   数=2026   cmd=`grep -n "strings are not supported\|expression type %d not supported\|strings/collections need the interpreter" src/compilation/wasm_backend.c`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1095   数=69   cmd=`grep -c Bytecode`   理由=rc=1 
  docs/DECFY_DESIGN.md:1096   数=2026   cmd=`sed 's,/\*.*\*/,,'`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1097   数=2026   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/DECFY_DESIGN.md:1100   数=0   cmd=`grep -rl "im_typeset" --include=*.c --include=*.h src/ \| grep -v "^src/types/"`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1101   数=330   cmd=`grep -c 'emit(comp->curBC, OP_' src/compiler/compiler.c`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1102   数=134   cmd=`grep -rn "is already registered" src/`   理由=rc=2 grep: already: No such file or directory
  docs/DECFY_DESIGN.md:1103   数=0   cmd=`grep -c 'EXPR_ARRAY\|EXPR_SET\|EXPR_DICT'`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1107   数=2026   cmd=`grep -rnE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]' --include=*.im .`   理由=rc=1 
  docs/DECFY_DESIGN.md:1108   数=2026   cmd=`grep -rnE '\btype[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=' --include=*.im .`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1109   数=15   cmd=`grep -c '^stage_[a-z_]*()' tools/gate.sh`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1110   数=2026   cmd=`sed -n '1218,1227p' src/parser/parser.c`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1111   数=2026   cmd=`sed -n '3192,3200p' src/vm/vm.c`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1112   数=2026   cmd=`sed -n '54p' tools/gate.sh`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1113   数=69   cmd=`grep -nE '^ *L_[A-Z0-9_]+:' src/vm/vm.c`   理由=rc=2 grep: *L_[A-Z0-9_]+:': No such file or directory
  docs/DECFY_DESIGN.md:1114   数=139   cmd=`sed -n '54p' tools/gate.sh`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1115   数=2026   cmd=`sed -n '54p' tools/gate.sh`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1116   数=12   cmd=`sed -n '54p' tools/gate.sh`   理由=rc=1 sed: -e expression #1, char 1: unknown command: `''
  docs/DECFY_DESIGN.md:1120   数=2026   cmd=`git show <ref>:docs/STATUS.md`   理由=含 shell 元字符（本探针不许跑）
  docs/DECFY_DESIGN.md:1122   数=49   cmd=`grep -n '一致'`   理由=rc=1 
  docs/EIDOS_V06.md:245   数=15   cmd=`python3 3.14.4`   理由=本探针不跑这一族（可能写盘）
  docs/EIDOS_V06.md:328   数=3   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/ERROR_CODES_V06.md:347   数=8248   cmd=`git blame`   理由=git 子命令不在只读表里：blame
  docs/HANDOFF_INFIVERSE.md:68   数=9   cmd=`grep -rn '"count"' src/ --include=*.c | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/HANDOFF_INFIVERSE.md:95   数=0   cmd=`grep -rn '@bits\|@bytes' src/ --include=*.c --include=*.h | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/HANDOFF_INFIVERSE.md:100   数=0   cmd=`grep -rn 'TOK_CARET\|OP_POW' src/ --include=*.c --include=*.h | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/PLAN_V06.md:95   数=0   cmd=`grep -c 'OP_' src/compilation/aot_native.c`   理由=rc=1 
  docs/PLAN_V06.md:96   数=1   cmd=`grep -c 'OP_' src/compilation/wasm_backend.c`   理由=rc=1 
  docs/PLAN_V06.md:489   数=0900   cmd=`git show HEAD:<path>`   理由=含 shell 元字符（本探针不许跑）
  docs/PLAN_V06.md:515   数=0   cmd=`git grep -n -- '--no-gui' HEAD -- src/`   理由=rc=1 
  docs/PLAN_V06.md:518   数=52   cmd=`git grep -nw -i 'entity' HEAD -- CMakeLists.txt`   理由=rc=1 
  docs/PLAN_V06.md:526   数=13   cmd=`git grep -l -i 'verse forge' HEAD | wc -l`   理由=含 shell 元字符（本探针不许跑）
  docs/RELEASE_0.5.1.md:14   数=148   cmd=`git rev-parse --short 'v0.5.1^{commit}'`   理由=rc=128 fatal: Needed a single revision
  docs/RELEASE_0.5.1.md:29   数=1383   cmd=`git ls-tree --name-only <那棵树> -- vtest/ | grep -c '\.im$'`   理由=含 shell 元字符（本探针不许跑）
  docs/RELEASE_0.5.2.md:12   数=148   cmd=`git rev-parse --short 'v0.5.2^{commit}'`   理由=rc=128 fatal: Needed a single revision
  docs/RELEASE_0.5.2.md:13   数=76   cmd=`git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'`   理由=rc=128 fatal: ambiguous argument ''v0.5.1^{commit}'..'v0.5.2^{commit}'': unknown revision or path not in the working tree.
  docs/RELEASE_0.5.2.md:72   数=8   cmd=`grep -rn 'installer\.iss' tools/ CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/RELEASE_0.5.2.md:73   数=0   cmd=`git show 751b2a0:<path>`   理由=含 shell 元字符（本探针不许跑）
  docs/RELEASE_0.5.2.md:75   数=0   cmd=`git grep 'RELEASE_0\.5\.2'`   理由=含 shell 元字符（本探针不许跑）
  docs/REQUIREMENTS_ANALYSIS.md:23   数=0   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/REQUIREMENTS_ANALYSIS.md:192   数=0   cmd=`grep tilemap\|scene::Node\|collision::AABB\|camera::Camera`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:42   数=76   cmd=`git rev-parse 'v0.5.2^{commit}'`   理由=rc=128 fatal: ambiguous argument ''v0.5.2^{commit}'': unknown revision or path not in the working tree.
  docs/STATUS.md:43   数=35   cmd=`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:45   数=6   cmd=`python3 tools/ports_race_probe.py`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:51   数=131   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:112   数=8256   cmd=`grep "128 + 8192"`   理由=rc=2 grep: +: No such file or directory
  docs/STATUS.md:162   数=3   cmd=`ctest -j$(nproc)`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:317   数=0   cmd=`grep -rl 'tilemap\|scene::Node\|collision::AABB\|camera::Camera' src/ examples/`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:327   数=826   cmd=`grep -rn 'im_jit_mode' src/`   理由=rc=1 
  docs/STATUS.md:640   数=3   cmd=`python3 tools/economy_migration.test.py`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:656   数=3   cmd=`git diff f18fc81`   理由=rc=128 fatal: ambiguous argument 'f18fc81': unknown revision or path not in the working tree.
  docs/STATUS.md:672   数=1   cmd=`grep -rn "commitment" src/ --include=*.c --include=*.h -i`   理由=rc=1 
  docs/STATUS.md:731   数=146   cmd=`grep -rqF "$f" .`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:794   数=10   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:987   数=1109   cmd=`node tools/vverse_pack.js preview <pkg>`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:995   数=1   cmd=`node tools/vverse_pack.js unpack`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1039   数=0   cmd=`bash tools/gate.sh`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1041   数=1   cmd=`git checkout 1fe93be -- src/`   理由=git 子命令不在只读表里：checkout
  docs/STATUS.md:1076   数=0   cmd=`bash tools/gate.sh`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1096   数=95   cmd=`ctest (expect 97/97, 0 skipped)`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1098   数=0   cmd=`git ls-files | grep -c xlang-toolchain`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1101   数=1161   cmd=`git diff --stat 5305ebf..stream/xlang-bridge`   理由=rc=128 fatal: ambiguous argument '5305ebf..stream/xlang-bridge': unknown revision or path not in the working tree.
  docs/STATUS.md:1112   数=1   cmd=`grep -n "exit(1)" src/parser/parser.c`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1115   数=32   cmd=`python3 -m build`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1125   数=128   cmd=`node tools/wasm_run.js --bench <mod> <n>`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1132   数=5   cmd=`git diff --name-only main...stream/wasm-simd-gc`   理由=rc=128 fatal: ambiguous argument 'main...stream/wasm-simd-gc': unknown revision or path not in the working tree.
  docs/STATUS.md:1146   数=19   cmd=`git checkout HEAD -- src/parser/parser.c`   理由=git 子命令不在只读表里：checkout
  docs/STATUS.md:1234   数=69   cmd=`git log`   理由=rc=-1 
  docs/STATUS.md:1371   数=86   cmd=`ctest --test-dir build`   理由=rc=-1 
  docs/STATUS.md:1436   数=101   cmd=`ctest --test-dir build`   理由=rc=-1 
  docs/STATUS.md:1554   数=10   cmd=`python3 tools/selfhost_compare.py --check`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1583   数=102   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1625   数=0   cmd=`git show f923f4d:CMakeLists.txt | grep -c 'aot-native'`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1756   数=100   cmd=`cargo test`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:1763   数=285   cmd=`grep -rniE 'access_token|oauth2/token|grant_type|refresh_token|exchange'`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:1857   数=4   cmd=`cargo tree`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:2059   数=69   cmd=`grep -qE`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:2078   数=67   cmd=`grep -c "async fn"`   理由=rc=2 grep: fn": No such file or directory
  docs/STATUS.md:2090   数=4   cmd=`grep -c "^async fn"`   理由=rc=2 grep: fn": No such file or directory
  docs/STATUS.md:2166   数=10   cmd=`ctest -R "node_discovery|lease_handoff|reconnect_generation|economy_migration|protocol_regression" -j4`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2192   数=75   cmd=`grep -qE`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:2500   数=1   cmd=`grep -rn t_sugar_desugared CMakeLists.txt tools/`   理由=rc=1 
  docs/STATUS.md:2508   数=1   cmd=`grep nv_error|nv_throw|nv_panic|abort()`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2545   数=107   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2574   数=108   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2576   数=8   cmd=`ctest (expect 108/108, 0 skipped)`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2626   数=10   cmd=`git rm`   理由=git 子命令不在只读表里：rm
  docs/STATUS.md:2654   数=10   cmd=`ctest (expect 108/108, 0 skipped)`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2656   数=10   cmd=`cargo --version`   理由=本探针不跑这一族（可能写盘）
  docs/STATUS.md:2731   数=0   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：checkout
  docs/STATUS.md:2737   数=110   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2810   数=0   cmd=`git checkout HEAD -- src/vm/vm.c`   理由=git 子命令不在只读表里：checkout
  docs/STATUS.md:2814   数=111   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2864   数=112   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2910   数=15   cmd=`git show HEAD:docs/AUDIT.md`   理由=rc=-1 
  docs/STATUS.md:2920   数=113   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:2928   数=2   cmd=`git show <旧提交>:src/mod/gui_mod.c.bak2_20260808_221050`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:3081   数=10   cmd=`grep -rn "SetObj" src/`   理由=rc=1 
  docs/STATUS.md:3110   数=114   cmd=`grep -E`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3192   数=0   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:3205   数=117   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3230   数=252   cmd=`git show d6bc6a2^:src/main.c | sed -n '247p'`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:3234   数=0   cmd=`ctest -R 'platform_probe|truthiness|union_member|...'`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:3261   数=118   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3372   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3374   数=0   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3450   数=1   cmd=`grep -Ec`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3467   数=5   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/STATUS.md:3636   数=1346   cmd=`git stash push -- src/main.c`   理由=git 子命令不在只读表里：stash
  docs/STATUS.md:3663   数=122   cmd=`git stash push -- src/runtime/runtime_posix.c`   理由=git 子命令不在只读表里：stash
  docs/STATUS.md:3692   数=134   cmd=`git stash push -- src/vm/vm.c`   理由=git 子命令不在只读表里：stash
  docs/STATUS.md:3702   数=11   cmd=`grep -n`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/STATUS.md:3848   数=126   cmd=`grep -c 'add_test('`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:3888   数=38   cmd=`sed`   理由=rc=1 Usage: sed [OPTION]... {script-only-if-no-other-script} [input-file]...
  docs/STATUS.md:3924   数=134   cmd=`grep -c 'add_test('`   理由=含 shell 元字符（本探针不许跑）
  docs/STATUS.md:4087   数=195   cmd=`grep -r "size("`   理由=含 shell 元字符（本探针不许跑）
  docs/SYNTAX.md:528   数=2026   cmd=`grep -rn '"rand"' src/runtime/runtime_posix.c`   理由=rc=1 
  docs/SYNTAX.md:927   数=3   cmd=`git mv`   理由=git 子命令不在只读表里：mv
  docs/SYNTAX.md:1003   数=2   cmd=`git rm src/mod/gui_mod.c.bak2_20260808_221050`   理由=git 子命令不在只读表里：rm
  docs/SYNTAX.md:1007   数=478   cmd=`python3`   理由=本探针不跑这一族（可能写盘）
  docs/THREADS_V06.md:131   数=8   cmd=`grep -c 'VM_GSHARD'`   理由=rc=1 
  docs/TYPESET_V06.md:164   数=101   cmd=`git grep -lw be main -- '*.im'`   理由=rc=1 
  docs/TYPESET_V06.md:224   数=2026   cmd=`grep '"count"' src/runtime/runtime.c src/runtime/runtime_posix.c src/vm/vm.c`   理由=rc=1 
  docs/TYPESET_V06.md:335   数=149   cmd=`grep -c 'add_test(' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/TYPESET_V06.md:337   数=200   cmd=`git grep 'builtin_pow\|"pow"\|TOK_CARET' main -- src`   理由=含 shell 元字符（本探针不许跑）
  docs/TYPESET_V06.md:395   数=346   cmd=`git ls-files '*.im' | xargs grep -n 'size('`   理由=含 shell 元字符（本探针不许跑）
  docs/TYPESET_V06.md:396   数=117   cmd=`grep -n '"size"' src/runtime/runtime.c src/runtime/runtime_posix.c`   理由=rc=1 
  docs/TYPESET_V06.md:423   数=0   cmd=`grep -c '\bbe\b'`   理由=含 shell 元字符（本探针不许跑）
  docs/TYPESET_V06.md:438   数=5   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:233   数=19   cmd=`grep -rn "array_test\.im\|block_edit\.im\|projects/demo/main\.im" CMakeLists.txt tools/`   理由=含 shell 元字符（本探针不许跑）
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:237   数=71   cmd=`grep 'Error at line'`   理由=rc=2 grep: at: No such file or directory
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:239   数=67   cmd=`git log -S`   理由=rc=129 error: switch `S' requires a value
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:254   数=19   cmd=`git checkout HEAD -- src/parser/parser.c`   理由=git 子命令不在只读表里：checkout
  docs/streams/agent-bridge.md:78   数=16   cmd=`ctest -R "crp\|upp\|json_min\|verse_"`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/agent-bridge.md:80   数=115   cmd=`node tools/crp_engine_crosscheck.js`   理由=本探针不跑这一族（可能写盘）
  docs/streams/agent-bridge.md:82   数=95   cmd=`grep -c "add_test(" CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/builtin-platform-census.md:3   数=18   cmd=`git show <sha>:<path>`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/builtin-platform-census.md:13   数=32   cmd=`git grep -n '"<name>"' <ref> -- src/`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/builtin-platform-census.md:42   数=2026   cmd=`grep -rn '"rand"' src/`   理由=rc=1 
  docs/streams/count-size.md:44   数=195   cmd=`grep -r "size("`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/crp-in-engine.md:137   数=89   cmd=`ctest --test-dir build -j4`   理由=rc=-1 
  docs/streams/crp-in-engine.md:138   数=11   cmd=`node tools/node_suites/run_all.js`   理由=本探针不跑这一族（可能写盘）
  docs/streams/docs-audit.md:82   数=21503   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/eidos-state.md:75   数=04   cmd=`grep -nw`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/eidos-state.md:93   数=62   cmd=`git grep`   理由=rc=128 fatal: no pattern given
  docs/streams/json-min-nul-escape.md:178   数=3   cmd=`ctest (expect 92/92)`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/json-min-nul-escape.md:184   数=2026   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  docs/streams/surgery-verify-report.md:162   数=29   cmd=`grep -rn 'be_bound' src/`   理由=rc=1 
  docs/streams/surgery-verify-report.md:208   数=6   cmd=`git show f6b3d87:<file>`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/surgery-verify-report.md:241   数=505   cmd=`grep -nE 'label_test|thread_test|GLOB' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/surgery-verify-report.md:246   数=0   cmd=`grep -rn 'be_bound' src/`   理由=rc=1 
  docs/streams/v06-decision-points.md:7   数=6   cmd=`git grep -n <symbol>`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/v06-decision-points.md:25   数=2   cmd=`grep -v 'for \|case \|_ in '`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/v06-decision-points.md:150   数=41   cmd=`git grep -ln 'OP_AND\|vm_truthy\|L_MOD\|set_contains' -- src/platform/`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/vverse-cli.md:74   数=0   cmd=`ctest (expect N/N)`   理由=含 shell 元字符（本探针不许跑）
  docs/streams/win-source-attribution.md:20   数=73   cmd=`grep -c re_seq src/runtime/runtime_posix.c`   理由=rc=1 
  examples/BUILDING_BRIDGES.md:153   数=3   cmd=`python3 -m venv`   理由=本探针不跑这一族（可能写盘）
  examples/BUILDING_BRIDGES.md:159   数=3   cmd=`python3 -m build`   理由=本探针不跑这一族（可能写盘）
  future/multi-agent-coordination-bridge.md:434   数=7   cmd=`grep -n "status" tools/crp_relay.js`   理由=rc=1 
  future/multi-agent-coordination-bridge.md:456   数=103   cmd=`grep -n "status" tools/crp_relay.js`   理由=rc=1 
  future/multi-agent-coordination-bridge.md:647   数=506   cmd=`grep -niE "test" README.md`   理由=rc=1 
  tools/README.md:150   数=12   cmd=`node tools/node_suites/run_all.js`   理由=本探针不跑这一族（可能写盘）
  tools/README.md:151   数=55   cmd=`node tools/dsh-inimerse/verify.mjs --live`   理由=本探针不跑这一族（可能写盘）
  tools/README.md:184   数=0   cmd=`grep -c 'PASS_REGULAR_EXPRESSION "[^"]*;' CMakeLists.txt`   理由=含 shell 元字符（本探针不许跑）
  tools/README.md:200   数=41   cmd=`grep -c 'FAIL_REGULAR_EXPRESSION' CMakeLists.txt`   理由=rc=1 
  tools/README.md:201   数=71   cmd=`grep -c 'gated_run.py' CMakeLists.txt`   理由=rc=1 
  website/scratch/fifth_tier_visible.md:63   数=17   cmd=`git show undefined:…`   理由=rc=128 fatal: invalid object name 'undefined'.
  website/scratch/fourth_ruler.md:103   数=0   cmd=`grep -c "^render_check.js:661<TAB>"`   理由=含 shell 元字符（本探针不许跑）
  website/scratch/fourth_ruler.md:106   数=0   cmd=`grep -c "^<短名>:<行>"`   理由=含 shell 元字符（本探针不许跑）
  website/scratch/registered_sites_audit.md:14   数=6   cmd=`grep -c`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  website/scratch/registered_sites_audit.md:57   数=1   cmd=`grep`   理由=rc=2 Usage: grep [OPTION]... PATTERNS [FILE]...
  website/scratch/rename_visibility.md:28   数=1   cmd=`node website/scratch/check_citations.js`   理由=本探针不跑这一族（可能写盘）
  website/scratch/rename_visibility.md:29   数=0   cmd=`node website/scratch/render_check.js`   理由=本探针不跑这一族（可能写盘）
  website/scratch/rerun_on_main.md:3   数=720   cmd=`git worktree add --detach ./wt-main dcff720`   理由=git 子命令不在只读表里：worktree

=== ④ 逐处
  docs/AUDIT.md:94   数=1   cmd=`git ls-files '*.im'`   理由=命令有输出但没有数：""
  docs/AUDIT.md:96   数=32   cmd=`git diff`   理由=命令有输出但没有数：""
  docs/AUDIT.md:592   数=0   cmd=`ctest -R contract_suite_runtime`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/AUDIT.md:1682   数=22   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:2948   数=70   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3281   数=0   cmd=`git ls-files 'src/**/*.c'`   理由=命令有输出但没有数：""
  docs/AUDIT.md:3283   数=12   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3348   数=5   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3358   数=148   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3379   数=4   cmd=`git diff --name-only`   理由=命令有输出但没有数：""
  docs/AUDIT.md:3383   数=3   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3384   数=168   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:3553   数=751   cmd=`git rev-parse`   理由=命令有输出但没有数：""
  docs/AUDIT.md:3565   数=30   cmd=`git merge-base --is-ancestor 30ee4d0 HEAD`   理由=命令有输出但没有数：""
  docs/AUDIT.md:3753   数=68   cmd=`git diff --numstat`   理由=命令有输出但没有数：""
  docs/AUDIT.md:4062   数=353   cmd=`git ls-files 'src/**/*.c'`   理由=命令有输出但没有数：""
  docs/AUDIT.md:4163   数=0   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:4164   数=15   cmd=`git status --porcelain`   理由=命令有输出但没有数："?? Testing/\n?? website/scratch/cmdnum_criteria.md\n?? website"
  docs/AUDIT.md:4181   数=79   cmd=`git ls-files -- 'vtest/*.im'`   理由=命令有输出但没有数：""
  docs/AUDIT.md:4214   数=8248   cmd=`git log --diff-filter=D -- '**/sync.im'`   理由=命令有输出但没有数：""
  docs/AUDIT.md:4230   数=854   cmd=`git diff --numstat`   理由=命令有输出但没有数：""
  docs/AUDIT.md:4759   数=95   cmd=`grep -rln '独佔全部锁'`   理由=命令有输出但没有数："docs/AUDIT.md\nwebsite/scratch/.build/audit_ctl.md\nwebsite/sc"
  docs/AUDIT.md:5227   数=1   cmd=`git diff --stat -- src/`   理由=命令有输出但没有数：""
  docs/AUDIT.md:5353   数=17   cmd=`git diff --numstat`   理由=命令有输出但没有数：""
  docs/AUDIT.md:5969   数=2   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:6050   数=2   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:6064   数=77   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:6173   数=77   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/AUDIT.md:6318   数=1   cmd=`git diff --stat`   理由=命令有输出但没有数：""
  docs/AUDIT.md:6437   数=3   cmd=`ctest -R case_alias_runtime`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/AUDIT.md:6463   数=77410   cmd=`git diff --stat dc9a33f 77410cb -- docs/DECFY_DESIGN.md`   理由=命令有输出但没有数：""
  docs/AUDIT.md:6467   数=0   cmd=`git diff --numstat`   理由=命令有输出但没有数：""
  docs/BOARD.md:98   数=30   cmd=`ctest 143/143`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/BOARD.md:114   数=41   cmd=`grep -rln -iE 'forge'`   理由=命令有输出但没有数："docs/BOARD.md\ndocs/STATUS.md\nwebsite/scratch/.build/cmdnum_g"
  docs/BOARD.md:142   数=105   cmd=`ctest -j4`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/BOARD.md:146   数=71   cmd=`git ls-files '*.im'`   理由=命令有输出但没有数：""
  docs/BOARD.md:218   数=150   cmd=`ctest -j12`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/BOARD.md:282   数=7   cmd=`git status`   理由=命令有输出但没有数："On branch stream/website-ruling-registration\nYour branch is "
  docs/REQUIREMENTS_ANALYSIS.md:205   数=100   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/STATUS.md:77   数=218   cmd=`git status`   理由=命令有输出但没有数："On branch stream/website-ruling-registration\nYour branch is "
  docs/STATUS.md:161   数=25   cmd=`ctest -j12`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/STATUS.md:212   数=11   cmd=`ctest -j12`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/STATUS.md:264   数=1   cmd=`ctest -j12`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/STATUS.md:737   数=2   cmd=`git diff --diff-filter=D`   理由=命令有输出但没有数：""
  docs/STATUS.md:774   数=89   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/STATUS.md:1011   数=0   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/STATUS.md:1107   数=468   cmd=`git ls-tree main --name-only src/bridge/`   理由=命令有输出但没有数："src/bridge/InimerseBridgeMain.java\nsrc/bridge/bridge_abi.c\ns"
  docs/STATUS.md:1767   数=41   cmd=`grep -rln -iE 'forge'`   理由=命令有输出但没有数："docs/BOARD.md\ndocs/STATUS.md\nwebsite/scratch/.build/cmdnum_g"
  docs/STATUS.md:2506   数=1   cmd=`git diff`   理由=命令有输出但没有数：""
  docs/STATUS.md:2860   数=1   cmd=`ctest -R contract_suite_runtime`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/STATUS.md:3533   数=5   cmd=`ctest -R verse_closed_loop`   理由=命令有输出但没有数："Test project /home/sakiko/inimerse/.worktrees/website-ledger"
  docs/STATUS.md:4079   数=30   cmd=`git ls-files '*.im'`   理由=命令有输出但没有数：""
  docs/TYPESET_V06.md:226   数=346   cmd=`git ls-files '*.im'`   理由=命令有输出但没有数：""
  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:187   数=97   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  docs/streams/surgery-verify-report.md:289   数=348   cmd=`git ls-files '*.im'`   理由=命令有输出但没有数：""
  docs/streams/xlang-bridge.md:91   数=95   cmd=`ctest`   理由=命令有输出但没有数："Usage\n\n  ctest [options]"
  website/scratch/fourth_ruler.md:12   数=12   cmd=`git ls-files -- 'website/scratch/*.js'`   理由=命令有输出但没有数：""
  website/scratch/kill_width_reading.md:4   数=1   cmd=`git merge-base --is-ancestor cf1dda8 HEAD`   理由=命令有输出但没有数：""
  website/scratch/rename_visibility.md:56   数=13   cmd=`git diff`   理由=命令有输出但没有数：""
  website/scratch/self_carrier_criteria.md:15   数=18   cmd=`git ls-files -- 'website/scratch/*.js'`   理由=命令有输出但没有数：""
  website/scratch/self_carrier_reading.md:30   数=18   cmd=`git ls-files -- 'website/scratch/*.js'`   理由=命令有输出但没有数：""
  website/scratch/self_carrier_reading.md:48   数=12   cmd=`git ls-files -- 'website/scratch/*.js'`   理由=命令有输出但没有数：""
```
