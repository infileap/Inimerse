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
