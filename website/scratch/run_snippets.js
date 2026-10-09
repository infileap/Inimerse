#!/usr/bin/env node
/* run_snippets.js —— 生成 website/data/snippets.js
 *
 * 用法（从任何目录都可以）：
 *     node website/scratch/run_snippets.js
 *
 * ── 为什么这是一个生成物，而不是一份手写的表 ──────────────────────────────
 * 比对页上最危险的一句话是「另一个语言会这样」：读者没有理由怀疑它，
 * 而写它的人当时以为它在。所以这个仓库的规矩是 **每条语言声明都要现场跑出来**。
 * 这里就是「现场」：本脚本把 website/scratch/snippets/ 下的每个片段**真的跑一遍**，
 * 把 stdout / stderr / 退出码逐字记下来，写进 website/data/snippets.js。
 * 页面上印的输出，只能是这个文件里的东西 —— 不是谁抄进去的。
 *
 * ── 确定性（这条是为钉子服务的）────────────────────────────────────────────
 * 本脚本**不读时钟**：data 里的 ref_date 取自 ref 那个提交自身的提交日期，
 * 所以同一棵树跑两次，产物应当逐字节相同（工具链版本变了除外 —— 那是读数变了）。
 * 编译产物落在 website/scratch/.build/（已忽略），命令里记的是仓库根相对路径，
 * 所以换一台机器跑，产物里的路径也不会带上临时目录名。
 *
 * ── 固定噪声 ────────────────────────────────────────────────────────────────
 * Inimerse 每次运行都会先打模块装载信息。它不是程序输出，但**也不许悄悄丢掉**：
 * 被滤掉的行逐字记在 noise 里，页面上可以把它折叠展示。滤除规则见 NOISE。
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const HERE = __dirname;                          // website/scratch
const SITE = path.resolve(HERE, '..');           // website
const ROOT = path.resolve(SITE, '..');           // 仓库根
const SNIP = path.join(HERE, 'snippets');
const BUILD = path.join(HERE, '.build');
const OUT = path.join(SITE, 'data', 'snippets.js');

const BIN = path.join(ROOT, 'build', 'inimerse');
const RUSTC = path.join(os.homedir(),
  '.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc');

/* 每次运行固定的模块装载信息。滤掉，但逐字留着。 */
const NOISE = [
  /^\[mod\] /,
  /^\[infiverse mod\] /,
  /^\[verse_dist mod\] /,
];

const rel = (p) => path.relative(ROOT, p) || '.';
/* 记命令时：仓库内的路径写成仓库根相对（和片段源路径同一套写法），仓库外的把 home 换成 ~。
   这样页面上的命令在任何一台机器上都读得懂，也不会泄露检出目录名与用户名。
   ★ 曾经漏掉一处：编译产物的路径写成绝对路径 ⇒ 命令里出现 `~/inimerse/...`，
   而检出目录名是每台机器都不同的东西。命令本身是读数，读数里不该有偶然。 */
const show = (s) => s.split(ROOT + '/').join('').split(os.homedir()).join('~');

function sh(cmd, args, opts) {
  const r = spawnSync(cmd, args, Object.assign({ cwd: ROOT, encoding: 'utf8' }, opts || {}));
  return {
    rc: r.status === undefined ? null : r.status,
    signal: r.signal || null,
    stdout: r.stdout == null ? '' : String(r.stdout),
    stderr: r.stderr == null ? '' : String(r.stderr),
    error: r.error ? String(r.error.message) : null,
  };
}

/* 稳定化：把「随机器/随进程变化、但与语言语义无关」的部分换成占位符。
 * 这是一次**声明的改写**，不是洗白：施加过哪条规则、命中几次、为什么，记在 stabilized 里；
 * 被摘掉的固定噪声行逐字记在 noise 里。两者合起来完整说明了印出来的文本是怎么来的。
 * ★ 这里**不保留**一份「未施加规则的逐字原文」字段：Rust 那一格的原文里带 OS 线程号，
 *   两次运行不是同一个字符串 —— 把其中一个样本当「逐字原文」写进文件，
 *   等于把一次偶然当成一个事实（而且会让产物每次都不一样，红对照就废了）。
 * 判据：如果一段差异换个进程就会变、而它不改变「这个语言做了什么」，它才配被稳定化。 */
const esc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const STABILIZE = [
  {
    id: 'repo-path',
    langs: null,                                   // null = 所有语言
    re: () => new RegExp(esc(ROOT) + '/', 'g'),
    to: '<repo>/',
    why: '把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。',
  },
  {
    id: 'rust-panic-thread-id',
    langs: ['rust'],
    re: () => /^thread 'main' \(\d+\) panicked/m,
    to: "thread 'main' (<tid>) panicked",
    why: "rustc 1.99 的 panic 消息里带着 OS 线程号（实测每次运行 +1：26 / 27 / 28）。"
       + '它是进程属性，不是语言的语义，所以按声明的规则换成 <tid>。',
  },
];

/** 去固定噪声 + 施加稳定化。
 *  返回：页面要印的文本、被摘掉的噪声行、施加过的规则、以及**施加前**的文本。
 *  这里刻意**不返回**一份「逐字原文」字段 —— 因为对 Rust 那一格来说，
 *  未施加规则的那份文本两次运行不是同一个字符串（线程号在变），
 *  把其中一个样本当成「逐字原文」写进文件，等于把一次偶然当成一个事实。
 *  审计靠的是 noise（逐字留下的被摘行）+ stabilized（规则与理由），
 *  两者合起来完整说明了「印出来的文本是怎么从进程输出变来的」。 */
function proc(text, lang) {
  const noise = [];
  const kept = [];
  for (const line of String(text).split('\n')) {
    if (NOISE.some((re) => re.test(line))) noise.push(line);
    else kept.push(line);
  }
  while (kept.length && kept[0].trim() === '') kept.shift();
  while (kept.length && kept[kept.length - 1].trim() === '') kept.pop();

  const preStabilized = kept.join('\n');
  let out = preStabilized;
  const stabilized = [];
  for (const rule of STABILIZE) {
    if (rule.langs && rule.langs.indexOf(lang) === -1) continue;
    const re = rule.re();
    const hits = out.match(new RegExp(re.source, re.flags.indexOf('g') === -1 ? re.flags + 'g' : re.flags));
    if (!hits) continue;
    out = out.replace(re, rule.to);
    stabilized.push({ id: rule.id, why: rule.why, count: hits.length });
  }
  return { text: out, preStabilized, noise, stabilized };
}

const LANG = {
  inimerse: {
    ext: 'im', build: null,
    run: (f) => [BIN, ['run', rel(f)]],
  },
  python: {
    ext: 'py', build: null,
    run: (f) => ['python3', [rel(f)]],
  },
  javascript: {
    ext: 'js', build: null,
    run: (f) => ['node', [rel(f)]],
  },
  rust: {
    ext: 'rs',
    build: (f, bin) => [RUSTC, ['--edition', '2021', '-O', rel(f), '-o', rel(bin)]],
    run: (f, bin) => [bin, []],
  },
  c: {
    ext: 'c',
    build: (f, bin) => ['cc', ['-O2', rel(f), '-o', rel(bin)]],
    run: (f, bin) => [bin, []],
  },
};

const cmdline = (cmd, args) =>
  [show(cmd)].concat(args.map((a) => (/[\s"]/.test(a) ? JSON.stringify(a) : a))).join(' ');

/** 跑一次；返回「可比较的形状」，用于两次运行的一致性检查。 */
function once(lang, file, bin) {
  const spec = LANG[lang];
  const pack = (cmd, rc, signal, so, se) => ({
    cmd, rc, signal,
    stdout: so.text, stderr: se.text,
    noise: so.noise.concat(se.noise),
    stabilized: so.stabilized.concat(se.stabilized),
    /* verbatim=true 表示这两段就是进程原样输出：没有摘掉任何行、没有替换任何字符 */
    verbatim: so.noise.length === 0 && se.noise.length === 0
      && so.stabilized.length === 0 && se.stabilized.length === 0,
    _pre: [so.preStabilized, se.preStabilized],
  });
  let build = null;
  if (spec.build) {
    const [bc, ba] = spec.build(file, bin);
    const b = sh(bc, ba);
    build = pack(cmdline(bc, ba), b.rc, b.signal, proc(b.stdout, lang), proc(b.stderr, lang));
    if (b.rc !== 0) return { build, run: null, status: 'build-failed' };
  }
  const [rc2, ra] = spec.run(file, bin);
  const r = sh(rc2, ra);
  return {
    build,
    run: pack(cmdline(rc2, ra), r.rc, r.signal, proc(r.stdout, lang), proc(r.stderr, lang)),
    status: 'ran',
  };
}

const fingerprint = (x) => JSON.stringify([x.status, x.build && x.build.rc,
  x.run && x.run.rc, x.run && x.run.signal, x.run && x.run.stdout, x.run && x.run.stderr]);
/* 施加稳定化**之前**是否也一致 —— 和 fingerprint 分开报。
   「stable=true 而 raw_stable=false」正是稳定化救回来的那一格该有的形状。 */
const rawFingerprint = (x) => JSON.stringify([x.run && x.run._pre]);
/* _pre 只用来判断「施加规则之前是否也一致」，**不许进产物** ——
   它带着随进程变化的量，写进文件会让这份产物每次都不一样。 */
const strip = (p) => { if (p) delete p._pre; return p || null; };

/* ─────────────────────────── 开始 ─────────────────────────── */

const manifest = require(path.join(SNIP, 'manifest.js'));
const git = (args) => sh('git', args).stdout.trim();
const ref = git(['rev-parse', 'HEAD']);
const refDate = git(['show', '-s', '--format=%cI', ref]);

if (!fs.existsSync(BIN)) {
  console.error(`找不到 ${rel(BIN)} —— 先构建引擎（inim_build / cmake --build）。`);
  process.exit(2);
}
fs.mkdirSync(BUILD, { recursive: true });

/* 工具链版本：这一栏本身也是读数，不许凭印象写 */
const toolchain = {};
toolchain.Inimerse = { cmd: show(BIN) + ' --version', out: sh(BIN, ['--version']).stdout.trim() };
toolchain.Python = { cmd: 'python3 --version', out: sh('python3', ['--version']).stdout.trim() };
toolchain.JavaScript = { cmd: 'node --version', out: sh('node', ['--version']).stdout.trim() };
toolchain.Rust = { cmd: show(RUSTC) + ' --version', out: sh(RUSTC, ['--version']).stdout.trim(),
  note: '★ 必须写全路径：这台机器上 `rustc` / `cargo` 走 rustup shim 是坏的'
      + '（cannot create transient scope: DBus error ... FileNotFound），'
      + '照抄 `rustc` 的人会看到一个 DBus 报错，而他会以为是 Rust 的问题。' };
toolchain.C = { cmd: 'cc --version', out: sh('cc', ['--version']).stdout.split('\n')[0].trim() };

const problems = [];
const topics = [];

for (const t of manifest.topics) {
  const cells = {};
  for (const lang of manifest.languages) {
    if (t.excluded && t.excluded[lang]) {
      cells[lang] = { lang, status: 'excluded', reason: t.excluded[lang] };
      continue;
    }
    if (!t.cells || !t.cells[lang]) continue;

    const ext = LANG[lang].ext;
    const file = path.join(SNIP, `${t.id}.${ext}`);
    if (!fs.existsSync(file)) {
      problems.push(`${t.id}.${ext}：清单里有这一格，但文件不存在`);
      cells[lang] = { lang, status: 'missing', reason: `缺少 ${rel(file)}` };
      continue;
    }

    const bin = path.join(BUILD, t.id);
    const a = once(lang, file, bin);
    const b = once(lang, file, bin);          // 第二遍：读数必须可复现
    const stable = fingerprint(a) === fingerprint(b);
    const rawStable = rawFingerprint(a) === rawFingerprint(b);

    cells[lang] = Object.assign({
      lang,
      file: rel(file),
      source: fs.readFileSync(file, 'utf8'),
      stable,
      raw_stable: rawStable,
      cellNote: (t.cellNotes && t.cellNotes[lang]) || null,
    }, { build: strip(a.build), run: strip(a.run), status: a.status });

    if (!stable) {
      problems.push(`${t.id}.${ext}：两次运行结果不同 ⇒ 这一格不是可复现的读数`);
      cells[lang].run2 = strip(b.run);
      cells[lang].build2 = strip(b.build);
    }
    if (a.status === 'build-failed') {
      /* 编译期被拒是一个真实读数，不是失败 —— 但要能被看见 */
      cells[lang].cellNote = (cells[lang].cellNote || '')
        + (cells[lang].cellNote ? ' ' : '')
        + '★ 这一格没有运行输出：编译没有通过。';
    }
  }
  topics.push({
    id: t.id,
    title: t.title,
    question: t.question,
    cells,
  });
}

const data = {
  ref,
  ref_date: refDate,
  generated_by: rel(__filename),
  filter: {
    noise: NOISE.map((re) => String(re)),
    note: '每次运行固定的模块装载信息已从 stdout / stderr 中滤除，'
        + '被滤掉的行逐字记在 noise 字段里（没有丢，只是折叠了）。'
        + '首尾空行也已去掉。',
    stabilize: STABILIZE.map((r) => ({ id: r.id, langs: r.langs, why: r.why })),
    stabilizeNote: 'stabilized 字段记的是这一格施加过哪些稳定化规则（含命中次数与理由）；'
        + 'verbatim=true 表示 stdout / stderr 就是进程原样输出，一个字都没动过。'
        + 'stable 判的是稳定化之后的文本，raw_stable 判的是施加规则之前 —— '
        + '「stable=true 而 raw_stable=false」的形状，正是稳定化救回来的那一格。'
        + '★ 不保存「未施加规则的逐字原文」：带 OS 线程号的 panic 消息两次运行不是同一个字符串，'
        + '存一个样本会把一次偶然写成事实，也会让这份产物每次都不一样。',
  },
  toolchain,
  languages: manifest.languages,
  labels: manifest.labels,
  topics,
  problems,
};

const banner = `/* 由 ${rel(__filename)} 生成 —— 不要手改这个文件。
 * 每一条输出都是真跑出来的；页面上印的东西只能是这里的值。
 * 重新生成：node ${rel(__filename)}
 */
`;
const body = banner + 'window.INFIVERSE_SNIPPETS = ' + JSON.stringify(data, null, 2) + ';\n';

/* ★ 自检：产物里不许出现「随进程变化」的量。
   理由是这条红对照本身：exact-otter 要的是「改片段源一个字，这份数据必须变」。
   如果这份数据**本来就每次都在变**，那条红对照就永远绿 —— 一个每次都变的基线
   区分不了任何东西（和「变异没落地的绿」是同一族）。
   所以：产物必须逐字节可复现；凡是带上进程/机器属性的量，不许进文件。
   （实测抓到过两处：一次是 raw_stderr 里的 panic 线程号，一次是内部用的 _pre 字段。） */
const FORBIDDEN = [
  { re: /"_pre"/, why: '内部字段 _pre 泄漏进了产物（它带施加规则之前的原文）' },
  { re: /thread 'main' \(\d+\) panicked/, why: 'panic 消息里的 OS 线程号：两次运行不是同一个字符串' },
];
const forbidden = FORBIDDEN.filter((f) => f.re.test(body));
if (forbidden.length) {
  console.log(`\n✗ 产物里有不可复现的量，拒绝写出：`);
  forbidden.forEach((f) => console.log(`  - ${f.why}`));
  process.exit(1);
}

fs.writeFileSync(OUT, body);

/* ─────────────────────────── 报告 ─────────────────────────── */
const cellCount = topics.reduce((n, t) => n + Object.keys(t.cells).length, 0);
const ran = topics.reduce((n, t) =>
  n + Object.values(t.cells).filter((c) => c.status === 'ran').length, 0);
const built = topics.reduce((n, t) =>
  n + Object.values(t.cells).filter((c) => c.status === 'build-failed').length, 0);
const excl = topics.reduce((n, t) =>
  n + Object.values(t.cells).filter((c) => c.status === 'excluded').length, 0);

console.log(`片段语料：${topics.length} 个主题，${cellCount} 格`);
console.log(`  运行出结果：${ran} 格`);
console.log(`  编译期被拒：${built} 格（这是读数，不是缺测）`);
console.log(`  明确排除：${excl} 格（每一格都带 reason）`);
console.log(`  语言：${manifest.languages.map((l) => manifest.labels[l]).join(' / ')}`);
const allCells = topics.reduce((acc, t) => acc.concat(Object.values(t.cells)), []);
const stab = allCells.filter((c) => (c.run && c.run.stabilized.length)
  || (c.build && c.build.stabilized.length));
console.log(`  施加过稳定化：${stab.length} 格`);
stab.forEach((c) => {
  const rules = ((c.run && c.run.stabilized) || []).concat((c.build && c.build.stabilized) || []);
  const ids = Array.from(new Set(rules.map((r) => r.id)));
  console.log(`    - ${c.file}：${ids.join(', ')}`
    + `（stable=${c.stable}, raw_stable=${c.raw_stable}）`);
});
console.log(`ref：${ref}`);
console.log(`产物：${rel(OUT)}`);
for (const [k, v] of Object.entries(toolchain)) console.log(`  ${k}: ${v.out}`);
if (problems.length) {
  console.log(`\n✗ ${problems.length} 个问题：`);
  problems.forEach((p) => console.log(`  - ${p}`));
  process.exit(1);
}
console.log('\n✓ 每一格都是可复现的读数（每格跑了两遍）');
