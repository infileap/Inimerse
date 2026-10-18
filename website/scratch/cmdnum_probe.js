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
