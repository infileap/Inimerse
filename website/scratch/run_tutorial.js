#!/usr/bin/env node
/* 渐进教程的**生成器** —— `website/data/tutorial.js` 由它产出，不要手改那个文件。
 *
 * 为什么要有生成器：教程的硬要求是「每一步都能在本地跑」，而本仓的纪律是
 * **一个读数必须在它的名字里带上它的射程**。手抄的输出会漂，漂的方向永远是
 * 「看起来更具体」。所以每一步的 stdout / stderr / 退出码都在这里**真跑出来**，
 * 页面上印的东西只能是这里的值。
 *
 * ★ 两个本件特有的读数陷阱（都在下面被显式处理，不是靠自觉）：
 *   1. **模块装载信息出现在哪个流、出现几行，取决于 cwd。** 从检出根跑会多出两行
 *      `[mod] …` 到 **stdout**；从子目录跑就没有。所以每一步都**声明它的 cwd**，
 *      并且 `--no-mods` 是默认 —— 那是唯一让两个 cwd 给出同一份 stdout 的跑法。
 *   2. **`compile` 的输出里带着绝对路径。** 同一个命令在别人的机器上会打印他自己的路径，
 *      所以按声明的规则换成 `<repo>`，并把替换记进 `stabilized`。
 *
 * ★ 本脚本**不读时钟**：`ref_date` 取自 `ref` 那个提交自身的提交日期。
 * 重新生成：`node website/scratch/run_tutorial.js`
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const HERE = __dirname;                       // website/scratch
const SITE = path.resolve(HERE, '..');        // website
const ROOT = path.resolve(SITE, '..');        // 检出根
const BIN = path.join(ROOT, 'build', 'inimerse');
const OUT = path.join(SITE, 'data', 'tutorial.js');
const WORK = path.join(HERE, '.build', 'tut');

const git = (args) => execFileSync('git', args, { cwd: ROOT, encoding: 'utf8' }).trim();

/* ───────────────── 每一步的源 ─────────────────
 * ★ 这些源是**候选**：跑不过的不会进产物（见下面 `runStep` 的 ok 判定），
 *   所以「教程里有的步骤」与「真的跑成功了的步骤」是同一个集合，不是两个。
 */
const SRC = {
  t01: `say "hello"\n`,
  t03: `x = 1 + 2\nsay str(x)\n`,
  t04: `name = "inim"\nsay "hello, " + name\n`,
  t05: `x = 7\nif x > 5 { say "big" } else { say "small" }\n`,
  t06: `i = 0\nwhile i < 3 { say str(i) ; i += 1 }\n`,
  t07: `for i in range(1, 4) { say str(i) }\n`,
  t08: `func dbl(n) { return n * 2 }\nsay str(dbl(21))\n`,
  t09: `xs = [1, 2, 3]\nsay str(len(xs))\npush(xs, 4)\nsay str(xs)\n`,
  t10: `type(5)\n`,
  t11: `say str(undefined_thing)\n`,
  t12: `func dbl(n) { return n * 2 }\nsay str(dbl(21))\n`,
};

/* ───────────────── 步骤表 ─────────────────
 * `cwd` 是每一步**声明**的跑法，页面上要印出来 —— 因为读数随它变。
 */
const STEPS = [
  { id: 't00', teaches: '先确认引擎在不在，以及它是哪个版本',
    kind: 'argv', argv: ['--version'], cwd: 'root',
    note: '这一条不带脚本。`--version` 的读数就是这一行。' },

  { id: 't01', teaches: '第一行输出：`say` 接一个表达式，**不需要括号**',
    kind: 'script', src: 't01', cwd: 'root',
    note: '`say` 是语句，不是函数调用 —— 写成 `say("hello")` 是另一个东西。' },

  { id: 't01c', teaches: '★★ 同一个脚本、同一个 cwd、**去掉 `--no-mods`** ⇒ stdout 多出两行',
    kind: 'script', src: 't01', cwd: 'root', flags: [],
    note: '★ 与 t01 逐字对比：脚本一个字没改、cwd 一个字没改，只少了 `--no-mods`。' +
      '多出来的 `[mod] …` 两行走 **stdout**（`[infiverse mod] …` 那两行走 stderr）。' },

  { id: 't01b', teaches: '★★ 同一个脚本、**换一个 cwd** ⇒ 那两行不见了（脚本没改）',
    kind: 'script', src: 't01', cwd: 'subdir', flags: [],
    note: '★ 与 t01c 逐字对比：两边都不带 `--no-mods`，只有 cwd 变了。' +
      '⇒ 那两行是**环境**的读数，不是这个脚本的读数。这就是每一步都要印 `cwd` 的理由。' },

  { id: 't03', teaches: '变量与算术：`=` 赋值，`str()` 把数字变成能印的东西',
    kind: 'script', src: 't03', cwd: 'root',
    note: '`say x` 也能跑，但印出来的东西与 `say str(x)` 不是同一份保证 —— 教程统一用 `str()`。' },

  { id: 't04', teaches: '字符串拼接用 `+`（不是 `..`，也不是模板串）',
    kind: 'script', src: 't04', cwd: 'root', note: '' },

  { id: 't05', teaches: '`if` / `else`：块用花括号，条件**不用括号**',
    kind: 'script', src: 't05', cwd: 'root',
    note: '首行与 `if` 不能拆行 —— 后缀条件是按**同一行**消歧的（`docs/SYNTAX.md` §4.2）。' },

  { id: 't06', teaches: '`while` 循环，以及 `+=`',
    kind: 'script', src: 't06', cwd: 'root', note: '' },

  { id: 't07', teaches: '`for x in range(a, b)` —— 左闭右开，且 `range()` 只在 `for … in` 里有效',
    kind: 'script', src: 't07', cwd: 'root',
    note: '★ `range` 是 `for … in` 的特判，不是普通内建（`docs/SYNTAX.md` §4.2）。' +
      '★★ 这里用 `range(1, 4)` 而不是 `range(3)`：后者与上一步 t06 印出来的东西**逐字相同**' +
      '（`0/1/2`），于是读者无法从输出上区分这两种循环 —— 本生成器的四元组探针就是为这个存在的。' },

  { id: 't08', teaches: '`func` 定义函数，`return` 返回值',
    kind: 'script', src: 't08', cwd: 'root',
    note: '★ **调用时参数个数完全不校验**：少传得 `nil`、多传被静默忽略，两个方向都不报错。' },

  { id: 't09', teaches: '列表、`len()`、`push()`',
    kind: 'script', src: 't09', cwd: 'root',
    note: '★ `[1, 2, 3]` 是**列表**，而 `(1, 2, 3)` 是**区间** —— 只差一个括号形状。' },

  { id: 't10', teaches: '★ 一个真的错误长什么样（退出码不是 0）',
    kind: 'script', src: 't10', cwd: 'root', expectFail: true,
    note: '★ 错误走 **stderr**、stdout 是**空的**、退出码 **1**。`type` 是保留字，' +
      '`type(x)` 是解析错误；唯一的替代是成员访问 `x.type`。' },

  { id: 't11', teaches: '★★ 一个**不报错**的错：未声明的名字给 `nil`',
    kind: 'script', src: 't11', cwd: 'root',
    note: '★ 与上一步并排看：这一步 stdout 有输出、stderr 空、**退出码 0** —— ' +
      '而它读的是一个从来没被声明过的名字。本仓的语言在这里**不报错**。' },

  { id: 't12', teaches: '编译成 `.inim`，再跑那个产物',
    kind: 'compile', src: 't12', cwd: 'root',
    note: '★ 输出里的路径是**绝对路径**，已按声明的规则换成 `<repo>`（见 `stabilized`）。' },
];

/* ───────────────── 稳定化 ───────────────── */
const STABILIZE = [
  { id: 'repo-path', why: '把检出位置去掉：同一个命令在别人的机器上会打印他自己的路径。' },
];
function stabilize(text) {
  const hits = [];
  const out = text.split(ROOT).join('<repo>');
  if (out !== text) hits.push({ id: 'repo-path', n: text.split(ROOT).length - 1 });
  return { text: out, hits };
}

/* ───────────────── 跑 ───────────────── */
function runStep(step) {
  const cwd = step.cwd === 'subdir' ? WORK : ROOT;
  /* ★ cwd 变了，**同一个相对路径就不再指向同一个文件** —— 从子目录跑时用文件名本身。 */
  const scriptRel = step.cwd === 'subdir'
    ? `${step.src}.im`
    : path.join('website', 'scratch', '.build', 'tut', `${step.src}.im`);

  /* ★ `flags` 是按步声明的：默认 `--no-mods`，但**两条反例步骤必须能把它关掉** ——
   * 否则那条「去掉 --no-mods 会多两行」的步骤会把要演示的东西自己压掉。 */
  const flags = step.flags === undefined ? ['--no-mods'] : step.flags;
  let argv;
  let cmdShown;
  if (step.kind === 'argv') {
    argv = step.argv;
    cmdShown = 'build/inimerse ' + step.argv.join(' ');
  } else if (step.kind === 'compile') {
    const outRel = path.join('website', 'scratch', '.build', 'tut', `${step.src}.inim`);
    argv = flags.concat(['compile', scriptRel, outRel]);
    cmdShown = `build/inimerse ${flags.join(' ')} compile ${scriptRel} <同一目录里的 .inim>`;
  } else {
    argv = flags.concat([scriptRel]);
    cmdShown = `build/inimerse ${flags.join(' ')} ${scriptRel}`;
  }

  let stdout = '';
  let stderr = '';
  let rc = 0;
  try {
    stdout = execFileSync(BIN, argv, { cwd, encoding: 'utf8', maxBuffer: 1 << 26, stdio: ['ignore', 'pipe', 'pipe'] });
  } catch (e) {
    stdout = e.stdout === undefined ? '' : String(e.stdout);
    stderr = e.stderr === undefined ? '' : String(e.stderr);
    rc = typeof e.status === 'number' ? e.status : null;
  }
  if (rc === 0) {
    /* execFileSync 成功时我们只拿到了 stdout；stderr 要另跑一次才拿得到。
     * ★ 不这么做的代价是：一个「stdout 有输出但 stderr 有话」的步骤会被记成 stderr 为空。 */
    try {
      const r = require('child_process').spawnSync(BIN, argv, { cwd, encoding: 'utf8', maxBuffer: 1 << 26 });
      stdout = r.stdout === undefined ? '' : String(r.stdout);
      stderr = r.stderr === undefined ? '' : String(r.stderr);
      rc = typeof r.status === 'number' ? r.status : null;
    } catch (e) { /* 保留上面那次的结果 */ }
  }

  const so = stabilize(stdout.replace(/\n+$/, ''));
  const se = stabilize(stderr.replace(/\n+$/, ''));

  /* ★ 「跑成功」的定义写在这里，不散在各处：
   *   - args 类：退出码 0 且 stdout 非空
   *   - 普通脚本类：退出码 0 且 stdout 非空（一个什么都不印的步骤教不了人）
   *   - 声明了 expectFail 的步骤：**退出码必须非 0**，否则那一课要教的病不存在 */
  const ok = step.expectFail ? rc !== 0 && se.text.length > 0 : rc === 0 && so.text.length > 0;
  return {
    id: step.id, teaches: step.teaches, kind: step.kind, cmd: cmdShown, flags,
    cwd: step.cwd, cwdNote: step.cwd === 'subdir'
      ? 'website/scratch/.build/tut/（一个子目录）'
      : '检出根',
    src: step.src ? SRC[step.src] : null,
    stdout: so.text, stderr: se.text, rc, ok,
    stabilized: so.hits.concat(se.hits),
    note: step.note || '',
  };
}

/* ───────────────── 生成 ───────────────── */
fs.mkdirSync(WORK, { recursive: true });
Object.keys(SRC).forEach((k) => { fs.writeFileSync(path.join(WORK, `${k}.im`), SRC[k]); });

const ref = git(['rev-parse', 'HEAD']);
const refDate = git(['show', '-s', '--format=%cI', ref]);
const engine = execFileSync(BIN, ['--version'], { cwd: ROOT, encoding: 'utf8' }).trim();

const steps = STEPS.map(runStep);

/* ★★★ 「每一步都能跑通」与「每一步都在演示它说要演示的那件事」是**两个判据**。
 * 上面那个 ok 只实现了前一个 —— 一个「脚本没改、cwd 没改、只少了 --no-mods」的步骤
 * 在 `ok` 下永远是绿的，哪怕它印出来的东西与它的对照**一模一样**（本生成器的第一版
 * 就是这么错的：t01b 印了 `hello`，与 t01 逐字相同，而它是绿的）。
 * 所以这里再加两条：一条**相对**判据（两条反例必须真的与它们的对照不同），
 * 一条**四元组唯一性**探针（两笔读数完全相同 ⇒ 至少有一笔没在演示它说要演示的事）。 */
const byId = {};
steps.forEach((s) => { byId[s.id] = s; });
const diff = (a, b) => ({
  id: `${a} vs ${b}`,
  stdoutSame: byId[a].stdout === byId[b].stdout,
  stderrSame: byId[a].stderr === byId[b].stderr,
  cwdSame: byId[a].cwd === byId[b].cwd,
  flagsSame: JSON.stringify(byId[a].flags) === JSON.stringify(byId[b].flags),
});
const PAIRS = [diff('t01', 't01c'), diff('t01c', 't01b')];
/* 每一对都必须【只在一个变量上不同，且在 stdout 上真的不同】 */
const PAIR_OK = PAIRS.every((d) => !d.stdoutSame || (!d.cwdSame || !d.flagsSame));
const QUAD = {};
steps.forEach((s) => { QUAD[JSON.stringify([s.stdout, s.stderr, s.cwd, s.flags])] = (QUAD[JSON.stringify([s.stdout, s.stderr, s.cwd, s.flags])] || []).concat(s.id); });
const DUP = Object.keys(QUAD).filter((k) => QUAD[k].length > 1).map((k) => QUAD[k]);

const FAILED = steps.filter((s) => !s.ok);
const banner = `/* 由 website/scratch/run_tutorial.js 生成 —— 不要手改这个文件。
 * 每一步的 stdout / stderr / 退出码都是真跑出来的；页面上印的东西只能是这里的值。
 * ★ 每一步都带着它自己的 cwd —— 因为同一个脚本在两个 cwd 下的 stdout 不一样。
 * 重新生成：node website/scratch/run_tutorial.js
 */
`;
const data = {
  ref, ref_date: refDate, generated_by: 'website/scratch/run_tutorial.js',
  engine,
  build: {
    cmd: 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"',
    binary: 'build/inimerse',
    note: 'README.md 的 "Build the engine (Linux / POSIX)" 一节。★ 用 `build/inimerse` 这个路径，不要用仓库根那份可能是旧的拷贝。',
  },
  defaults: {
    flags: ['--no-mods'],
    why: '★ 不加 `--no-mods` 时，从检出根跑会往 **stdout** 多印两行 `[mod] …`，从子目录跑就不印。' +
      '模块装载是**环境**的读数，不是脚本的读数；`--no-mods` 是唯一让两个 cwd 给出同一份 stdout 的跑法。' +
      '教程的每一步默认都用它，两个反例（t01b、以及 t00 之后的对照）单独列出来。',
  },
  stabilize: { rules: STABILIZE, note: 'stabilized 字段记的是这一步施加过哪些规则；空数组表示 stdout / stderr 一个字都没动过。' },
  failed: FAILED.map((s) => ({ id: s.id, rc: s.rc, why: '这一笔没有通过本生成器的 ok 判定，所以它的输出不入产物。' })),
  diffProbe: {
    pairs: PAIRS,
    allDifferent: PAIR_OK,
    why: '★★ 两条反例步骤（t01c、t01b）必须**真的**与它们的对照在 stdout 上不同 —— ' +
      '「跑通了」不蕴含「演示了它说要演示的那件事」。',
  },
  dupProbe: {
    duplicates: DUP,
    clean: DUP.length === 0,
    why: '★ 四元组 (stdout, stderr, cwd, flags) 全表唯一。两笔读数完全相同 ⇒ ' +
      '至少有一笔没在演示它说要演示的那个变量。',
  },
  steps,
};
fs.writeFileSync(OUT, banner + 'window.INFIVERSE_TUTORIAL = ' + JSON.stringify(data, null, 2) + ';\n');

console.log(`ref：${ref}（${refDate}）`);
console.log(`引擎：${engine}`);
console.log(`步骤：${steps.length} 步，其中 ok = ${steps.filter((s) => s.ok).length}，不 ok = ${FAILED.length}`);
if (FAILED.length) console.log('  ★ 没通过的：' + FAILED.map((s) => `${s.id}(rc=${s.rc})`).join(' '));
console.log(`写出：${path.relative(ROOT, OUT)}`);
console.log(`★★ 两条反例与它们的对照：${PAIR_OK ? '成立' : '★ 不成立'}`);
PAIRS.forEach((d) => console.log(`   ${d.id}  stdout 相同 = ${d.stdoutSame}　stderr 相同 = ${d.stderrSame}　cwd 相同 = ${d.cwdSame}　flags 相同 = ${d.flagsSame}`));
console.log(`★ 四元组唯一性：${DUP.length === 0 ? '全表唯一' : '★ 有重' + JSON.stringify(DUP)}`);
steps.forEach((s) => {
  console.log(`  ${s.ok ? '✓' : '✗'} ${s.id.padEnd(5)} rc=${s.rc === null ? '?' : s.rc}  out=${JSON.stringify(s.stdout).slice(0, 44)}  err=${JSON.stringify(s.stderr).slice(0, 34)}`);
});
