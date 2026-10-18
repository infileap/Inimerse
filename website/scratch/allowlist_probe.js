#!/usr/bin/env node
// 戊⁷ 探针：把「⑤ 我的射程之外」做成一份可复核的允许表。
//
// 用法：node website/scratch/allowlist_probe.js <repoRoot>
//
// ★ 本探针【不跑任何被允许表挡下的命令】—— 那 239 处之所以被挡下，正是因为其中含 shell 元字符
//   与 git 非只读子命令。跑它们就是让文档决定工作树状态（判据 F2）。
//   本件只报【代理量】：那个可执行名能不能解析、那个 git 子命令存不存在。
//
// ★★ 判据 F8：分母的定义与 R1 那条规则【共用同一个函数】。
//   所以本件**分母一律用丁⁷ 那一版（不认引号）**，把【认引号】的新口径只用在【⑤ 内部的再分类】上。
//   否则一次「修正一条规则」会同时动【分母】与【分子】，而两个动在报告里看起来是一个动。
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const SHELL_META_NAIVE = /[;&|<>$()`\n\\]/;          // 丁⁷ 那一版：不认引号
const GIT_READONLY = new Set(['ls-files', 'ls-tree', 'rev-list', 'rev-parse', 'show', 'grep', 'cat-file', 'merge-base', 'count-objects', 'log', 'diff', 'status']);
const SAFE_HEAD = new Set(['grep', 'wc', 'ctest', 'git', 'sed', 'python3', 'node', 'cmake', 'cargo', 'bash', 'ls-files', 'ls-tree']);

// ★★★ 判据 F7：shell 元字符只有在【引号外面】才有意义。
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

// ---- 五条规则，分开返回（判据 F1）。`meta` 参数选哪一版元字符检测。----
function rulesHit(cmd, meta) {
  const s = cmd.trim();
  const hit = [];
  if (!s) { hit.push(['R0', '空']); return hit; }
  const m = meta === 'quoted' ? metaOutsideQuotes(s) : (s.match(SHELL_META_NAIVE) || [null])[0];
  if (m) hit.push(['R1', '含 shell 元字符' + (meta === 'quoted' ? '（引号外）' : '（不认引号）') + '：' + JSON.stringify(m)]);
  const head = s.split(/\s+/)[0];
  if (!SAFE_HEAD.has(head)) hit.push(['R2', '可执行名不在允许表：' + head]);
  if (head === 'git') {
    const sub = gitSub(s);   // ★ 判据 F12：懂 -C/-c 这类带参数的选项
    if (!sub) hit.push(['R3', 'git 后面没有子命令']);
    else if (!GIT_READONLY.has(sub)) hit.push(['R4', 'git 子命令非只读：' + sub]);
  }
  if (['bash', 'node', 'python3', 'cmake', 'cargo'].includes(head)) hit.push(['R5', '本探针不跑这一族：' + head]);
  return hit;
}
const MINE_SIDE = /shell 元字符|不在允许表|git 子命令不在只读表|git 后面没有子命令|本探针不跑这一族/;
// ★★★ 判据 F15：把「这条规则命中了吗」写成「这条规则的描述里含某个词吗」——
//   两者只有在描述字符串**恰好一致**时才相等。
//   上面那个 MINE_SIDE 里写的是「git 子命令**不在只读表**」，而 rulesHit 印出的是
//   「git 子命令**非只读**：mv」⇒ **R4 挡下的每一处都在 MINE_SIDE 上落空**，
//   而分母只会小一点、不会报错。⇒ 判定一律**按规则的键**，不按它的描述。
const isMine = (hit) => hit.some(([k]) => k !== 'R0');
// ★★★ 判据 F12：`git` 的子命令不是「第一个不以 - 开头的词」——
//   带参数的选项（-C <dir>、-c <k=v>、--git-dir=<d>）会让位置扫描把【选项的参数】读成【子命令】。
const GIT_OPTS_WITH_ARG = new Set(['-C', '-c', '--git-dir', '--work-tree', '--namespace', '--exec-path']);
function gitSub(cmd) {
  const t = cmd.trim().split(/\s+/);
  if (t[0] !== 'git') return null;
  for (let i = 1; i < t.length; i++) {
    const x = t[i];
    if (GIT_OPTS_WITH_ARG.has(x)) { i++; continue; }          // 跳过它的参数
    if (x.startsWith('--') && x.includes('=')) continue;
    if (x.startsWith('-')) continue;
    return x;
  }
  return null;
}
// ★ 一个 git 子命令「实际上会不会改工作树」—— 白名单回答的是「在不在表上」，不是「它做什么」
const GIT_WRITES = new Set(['add', 'commit', 'mv', 'rm', 'checkout', 'stash', 'merge', 'worktree', 'reset', 'restore', 'rebase', 'cherry-pick', 'revert', 'clean', 'apply', 'am', 'push', 'fetch', 'pull', 'tag', 'branch', 'switch', 'update-index', 'write-tree', 'commit-tree', 'gc', 'prune', 'repack', 'filter-branch']);
const GIT_READONLY_ACTUAL = new Set(['check-ignore', 'archive', 'reflog', 'cat-file', 'ls-files', 'ls-tree', 'rev-list', 'rev-parse', 'show', 'grep', 'merge-base', 'count-objects', 'log', 'diff', 'status', 'describe', 'for-each-ref', 'name-rev', 'symbolic-ref', 'var', 'version', 'help', 'config', 'hash-object', 'fsck', 'verify-pack', 'shortlog', 'blame', 'whatchanged', 'notes']);

const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', '*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);
const N1 = /\*\*(\d[\d,]*)\*\*/g, N2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const s1 = (l) => [...l.matchAll(N1)].map((m) => m[1]);
const s2 = (l) => [...l.matchAll(N2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean);
const nums = (l) => { const a = s1(l); return a.length ? a : s2(l); };

const rows = [];
for (const f of files) {
  let t;
  try { t = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  t.split('\n').forEach((line, i) => {
    const ns = nums(line);
    if (!ns.length) return;
    const spans = [...line.matchAll(/`([^`]*)`/g)].map((m) => m[1]);
    // ★ 分母的定义：与丁⁷ 逐字相同（用丁⁷ 那一版规则集）
    const cmds = spans.filter((s) => !isMine(rulesHit(s, 'naive')) || SAFE_HEAD.has(s.trim().split(/\s+/)[0]));
    rows.push({ file: f, line: i + 1, num: ns[0], spans, cmds, text: line });
  });
}
const withCmd = rows.filter((r) => r.cmds.length > 0);
const five = [];
for (const r of withCmd) {
  const cmd = r.cmds[0];
  const hit = rulesHit(cmd, 'naive');
  const why = hit.length ? hit.map((x) => x[1]).join(' ／ ') : null;
  if (!isMine(hit)) continue;   // ★ 判据 F15：按规则的键判，不按它的描述
  five.push({ ...r, cmd, hit, hitQ: rulesHit(cmd, 'quoted') });
}

const say = (s = '') => console.log(s);
say('=== 判据 F0：载体与分母');
say('  分母 B（带命令的处，承丁⁷ 的定义）  = ' + withCmd.length + '  ⇒ 丁⁷ 是 572，差 ' + (withCmd.length - 572));
say('  ★ 本件的分母（⑤ 我的射程之外）      = ' + five.length + '  ⇒ 丁⁷ 是 239，差 ' + (five.length - 239));
say('  ⇒ 差的原因见读数第三节（丁⁷ 的产物成了载体）。本件不压回。');
say();
say('=== 判据 F1：允许表逐条 —— 每条挡下的处数');
const NAMES = { R0: '空', R1: '含 shell 元字符', R2: '可执行名不在允许表', R3: 'git 后面没有子命令', R4: 'git 子命令非只读', R5: '本探针不跑这一族' };
const FIRST = ['R0', 'R1', 'R2', 'R3', 'R4', 'R5'];
const byRule = {}, byFirst = {};
for (const r of five) { for (const [k] of r.hit) byRule[k] = (byRule[k] || 0) + 1; const k = FIRST.find((x) => r.hit.some(([y]) => y === x)); byFirst[k] = (byFirst[k] || 0) + 1; }
let ruleSum = 0;
say('  --- 命中（有重叠）');
for (const k of FIRST) if (byRule[k]) { ruleSum += byRule[k]; say('  ' + k + '  ' + NAMES[k].padEnd(22) + ' = ' + String(byRule[k]).padStart(4)); }
say('  ★ 命中总数 = ' + ruleSum + '   分母 = ' + five.length + '   差 ' + (ruleSum - five.length) + ' = 一条命令命中多条');
say('  --- 按【首次命中】划分（互斥）');
let fSum = 0;
for (const k of FIRST) if (byFirst[k]) { fSum += byFirst[k]; say('  ' + k + '  ' + NAMES[k].padEnd(22) + ' = ' + String(byFirst[k]).padStart(4)); }
say('  ★ 和 = ' + fSum + '  ⇒ ' + (fSum === five.length ? '= 分母 ' + five.length + ' ✓' : '★ ≠ 分母'));
say();
say('=== ★★★ 判据 F7：R1 那条规则，认引号 vs 不认引号');
const r1 = five.filter((r) => r.hit.some(([k]) => k === 'R1'));
const stillR1 = r1.filter((r) => r.hitQ.some(([k]) => k === 'R1'));
say('  R1（不认引号，丁⁷ 那一版）挡下  = ' + r1.length);
say('  R1（认引号，只算引号外的元字符）= ' + stillR1.length);
say('  ★ 差 = ' + (r1.length - stillR1.length) + ' 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】');
const metaNaive = {}, metaQ = {};
for (const r of r1) { const m = (r.cmd.match(SHELL_META_NAIVE) || [null])[0]; metaNaive[JSON.stringify(m)] = (metaNaive[JSON.stringify(m)] || 0) + 1; }
for (const r of stillR1) { const m = metaOutsideQuotes(r.cmd); metaQ[JSON.stringify(m)] = (metaQ[JSON.stringify(m)] || 0) + 1; }
say('  --- 不认引号时，元字符分布');
for (const [k, v] of Object.entries(metaNaive).sort((a, b) => b[1] - a[1])) say('    ' + k.padEnd(6) + ' = ' + v);
say('  --- 认引号后，元字符分布');
for (const [k, v] of Object.entries(metaQ).sort((a, b) => b[1] - a[1])) say('    ' + k.padEnd(6) + ' = ' + v);
say('  --- 被认引号【放行】的那批逐字（前 20）');
r1.filter((r) => !r.hitQ.some(([k]) => k === 'R1')).slice(0, 20).forEach((r) => {
  say('    ' + r.file + ':' + r.line + '   cmd=`' + r.cmd.slice(0, 72) + '`');
});
say();
say('=== 判据 F2：代理量 —— 那 ' + five.length + ' 处里，代理上跑得动几处');
const resolvable = [], notResolvable = [];
for (const r of five) {
  const head = r.cmd.trim().split(/\s+/)[0];
  let ok = false, detail = '';
  try { execFileSync('sh', ['-c', 'command -v "$1"', 'sh', head], { encoding: 'utf8' }); ok = true; } catch { ok = false; detail = '可执行名解析不到'; }
  (ok ? resolvable : notResolvable).push({ ...r, detail });
}
say('  代理上跑得动（可执行名能解析）= ' + resolvable.length);
say('  代理上跑不动（解析不到）      = ' + notResolvable.length);
say('  ★★ 这是【代理量】，不是「真的跑得动」—— 判据 F2：允许表的存在就是让后者不可测。');
const subOf = (r) => gitSub(r.cmd);
const mutating = resolvable.filter((r) => subOf(r) && GIT_WRITES.has(subOf(r)));
const roActual = resolvable.filter((r) => subOf(r) && GIT_READONLY_ACTUAL.has(subOf(r)));
const unknownSub = resolvable.filter((r) => subOf(r) && !GIT_WRITES.has(subOf(r)) && !GIT_READONLY_ACTUAL.has(subOf(r)));
say('  其中【会改工作树】的 git（代理上跑得动，但跑了就违反「一个受跟踪文件都不改」）= ' + mutating.length);
mutating.forEach((r) => say('    [会改树] ' + r.file + ':' + r.line + '   cmd=`' + r.cmd.slice(0, 72) + '`'));
say('  --- ★★★ 判据 F16：按【它做什么】判 vs 按【在不在白名单上】判');
say('    R4 那条规则（不在 11 项只读白名单上）挡下        = ' + five.filter((r) => r.hit.some(([k]) => k === 'R4')).length);
say('    实际上【会改工作树】的 git（按子命令语义判）      = ' + mutating.length);
say('    实际上【只读】的 git（在白名单【之外】的真只读）   = ' + roActual.length);
say('    子命令语义未知的                                = ' + unknownSub.length);
say('    ★ 白名单回答的是「在不在表上」，不是「它做什么」。两个数并排：');
roActual.forEach((r) => say('      [真只读·不在白名单上] ' + r.file + ':' + r.line + '   git ' + subOf(r) + '   cmd=`' + r.cmd.slice(0, 60) + '`'));
unknownSub.forEach((r) => say('      [语义未知] ' + r.file + ':' + r.line + '   git ' + subOf(r) + '   cmd=`' + r.cmd.slice(0, 60) + '`'));
say();
say('=== 判据 F3：四档（分母 = ' + five.length + '）');
const g1 = notResolvable, g2 = mutating;
const g3 = stillR1.filter((r) => !g1.includes(r) && !g2.includes(r));
say('  ① 对象的性质（可执行名不存在）                = ' + g1.length);
say('  ② 允许表的性质（跑得动、会改工作树、是我不许） = ' + g2.length);
say('  ③ 提取器的性质（那段反引号里根本不是一条命令） = ' + g3.length + '  ★ 见下逐处读');
say('  ④ 判不出                                      = ' + (five.length - g1.length - g2.length - g3.length));
g1.forEach((r) => say('    ① ' + r.file + ':' + r.line + '   cmd=`' + r.cmd.slice(0, 72) + '`  ' + r.detail));
say();
say('=== ★ ③ 那批逐处（认引号后仍含元字符的，前 40）');
g3.slice(0, 40).forEach((r) => {
  const m = metaOutsideQuotes(r.cmd);
  say('  ' + r.file + ':' + r.line + '  元字符=' + JSON.stringify(m) + '  cmd=`' + r.cmd.slice(0, 72) + '`');
  say('      ' + r.text.replace(/\s+/g, ' ').slice(0, 130));
});
say();
say('=== ★ R4 / R3 逐处（git 非只读 / git 无子命令）');
five.filter((r) => r.hit.some(([k]) => k === 'R4' || k === 'R3')).forEach((r) => say('  ' + r.file + ':' + r.line + '   cmd=`' + r.cmd.slice(0, 72) + '`   ' + r.hit.map((x) => x[1]).join(' ／ ')));
say();
say('=== ★ R5 逐处（本探针不跑的族）');
const r5 = five.filter((r) => r.hit.some(([k]) => k === 'R5'));
const r5h = {};
for (const r of r5) { const h = r.cmd.trim().split(/\s+/)[0]; r5h[h] = (r5h[h] || 0) + 1; }
for (const [k, v] of Object.entries(r5h).sort((a, b) => b[1] - a[1])) say('  ' + k.padEnd(10) + ' = ' + v);
r5.slice(0, 12).forEach((r) => say('  ' + r.file + ':' + r.line + '   cmd=`' + r.cmd.slice(0, 72) + '`'));
fs.writeFileSync(path.join(ROOT, 'website/scratch/.build/allowlist_out.json'), JSON.stringify({ B: withCmd.length, five: five.length, byFirst, r1: r1.length, stillR1: stillR1.length, g1: g1.length, g2: g2.length, g3: g3.length }, null, 1));
