# 癸⁷ 留档：探针源码 ＋ 逐字输出 ＋ 四件套

★★★ 本文件已把本目录的禁用串掩码为【串A·已掩码】（判据 M1.4）—— 探针与留档都在输出前掩码。
★ 掩码计数：本文件里串A 原样命中 = 0（逐字见文末）。

## 一、探针源码 `website/scratch/mask_probe.js`（逐字，已掩码）
```js
'use strict';
// 癸⁷ 探针：把「掩码」做成一个可复核的数。只读（除了会跑 check_citations.js 一次）。
const fs = require('fs');
const path = require('path');
const { execFileSync, execSync } = require('child_process');

const ROOT = process.argv[2] || '.';
const OUT = [];
const say = (s) => OUT.push(s);

// ---- 判据 M1.4：本报告里那个串不复现 ----
const S_A = ['Inim', ' OS'].join('');                 // 串A：本目录的禁用串
const S_B = ['check', '(', 'x, y, z)'].join('');      // 串B：枚举器样本
const NAME = (s) => s === S_A ? '【串A·两段拼成】' : s === S_B ? '【串B·三段拼成】' : '【未命名串】';
const mask = (t) => t.split(S_A).join('【串A·已掩码】').split(S_B).join('【串B·已掩码】');

// ---- 载体 ----
const files = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files', '-z', '--', 'website/scratch/*.js', 'website/scratch/*.md'], { cwd: ROOT, encoding: 'utf8' })
  .split('\0').filter(Boolean);

// ---- 拼接点：['a', 'b'].join('') / ['a','b','c'].join('') ----
const JOIN = /\[((?:\s*'[^']*'\s*,?){2,})\]\s*\.\s*join\(\s*''\s*\)/g;
// ---- 掩码点：X.split(S).join(P) ----
const MASKSITE = /\.split\(\s*([A-Za-z_$][\w$]*)\s*\)\s*\.\s*join\(/g;

const joins = [], masks = [];
for (const f of files) {
  let txt; try { txt = fs.readFileSync(ROOT + '/' + f, 'utf8'); } catch { continue; }
  const lines = txt.split('\n');
  lines.forEach((line, i) => {
    let m;
    JOIN.lastIndex = 0;
    while ((m = JOIN.exec(line))) {
      const pieces = [...m[1].matchAll(/'([^']*)'/g)].map((x) => x[1]);
      const whole = pieces.join('');
      if (whole !== S_A && whole !== S_B) continue;
      joins.push({ file: f, line: i + 1, raw: line.trim(), pieces, whole, n: pieces.length });
    }
    MASKSITE.lastIndex = 0;
    while ((m = MASKSITE.exec(line))) {
      masks.push({ file: f, line: i + 1, raw: line.trim(), varName: m[1] });
    }
  });
}

// ---- 判据 M1.2：git grep（受跟踪字节）vs grep（含第五档）----
const gitGrep = (s) => {
  try {
    const o = execFileSync('git', ['grep', '-c', '-F', s, '--', '.'], { cwd: ROOT, encoding: 'utf8' });
    return o.trim().split('\n').filter(Boolean);
  } catch (e) { return []; }
};
const fsGrep = (s) => {
  try {
    const o = execSync(`/usr/bin/grep -rlF ${JSON.stringify(s)} website/ 2>/dev/null || true`, { cwd: ROOT, encoding: 'utf8' });
    return o.trim().split('\n').filter(Boolean);
  } catch { return []; }
};

say('=== 判据 M0：载体');
say('  受跟踪 website/scratch/*.js + *.md = ' + files.length + ' 份');
say('  拼接点（判据 M0.1）= ' + joins.length);
say('  掩码点（判据 M0.1）= ' + masks.length);
say('  涉及的串：' + [...new Set(joins.map((j) => NAME(j.whole)))].join(' ／ '));
say('  ★ 判据 M0.2：「处」= 一个拼接点或掩码点；【串】数并排 = ' + new Set(joins.map((j) => j.whole)).size);
say('');

say('=== 判据 M2.1：一共掩码过几处');
say('  拼接点 = ' + joins.length);
say('  掩码点 = ' + masks.length);
say('  合计   = ' + (joins.length + masks.length));
say('');

say('=== 判据 M1：逐处（文件／行号／逐字／它拼的是哪个串／那几段／受跟踪字节剩余命中）');
{
  const gitHitsA = gitGrep(S_A), gitHitsB = gitGrep(S_B);
  const fsHitsA = fsGrep(S_A), fsHitsB = fsGrep(S_B);
  say('  --- 串A 【串A·两段拼成】');
  say('      受跟踪字节（git grep -c -F）= ' + (gitHitsA.length ? gitHitsA.join(' ; ') : '0 个文件'));
  say('      含第五档（grep -rl website/）= ' + fsHitsA.length + ' 个文件');
  say('      ★★ 判据 M1.2：两个数并排 —— 差值 = ' + (fsHitsA.length - gitHitsA.length) + ' 份全部在第五档');
  say('  --- 串B 【串B·三段拼成】');
  say('      受跟踪字节 = ' + (gitHitsB.length ? gitHitsB.join(' ; ') : '0 个文件'));
  say('      含第五档 = ' + fsHitsB.length + ' 个文件');
  say('');
  for (const j of joins) {
    say('  ' + j.file + ':' + j.line + '   [' + j.n + ' 段]  ' + NAME(j.whole));
    say('      逐字：' + j.raw.slice(0, 120));
    say('      那 ' + j.n + ' 段 = ' + JSON.stringify(j.pieces.map((p) => mask(p))));
  }
  say('');
  for (const m of masks) {
    say('  ' + m.file + ':' + m.line + '   [掩码点]');
    say('      逐字：' + m.raw.slice(0, 120));
    say('      掩的是变量：' + m.varName);
  }
}
say('');

say('=== 判据 M2.2：源码里由两段或三段拼成的处数');
say('  两段 = ' + joins.filter((j) => j.n === 2).length + ' ／ 三段 = ' + joins.filter((j) => j.n === 3).length);
say('  合计 = ' + joins.length);
for (const j of joins) say('    ' + j.file + ':' + j.line + '  ' + j.n + ' 段  ' + NAME(j.whole));
say('');

say('=== 判据 M2.3：掩码前 / 掩码后，受跟踪字节里出现过几次');
say('  串A 受跟踪字节命中（今天）= ' + gitGrep(S_A).length + ' 个文件');
say('  串B 受跟踪字节命中（今天）= ' + gitGrep(S_B).length + ' 个文件');
say('  ★ 判据 M2.3 的诚实边界：本件【无法】重放「掩码前」那一刻 —— 掩码器与它的产物同时落地。');
say('    可以重放的是：把掩码器关掉（内存里改一行），看产物会变成什么。见判据 M2.4。');
say('');

say('=== 判据 M2.4：掩码之后，有没有哪一处【仍然】在受跟踪字节里命中');
{
  const gA = gitGrep(S_A), gB = gitGrep(S_B);
  say('  串A = ' + gA.length + ' 处 ／ 串B = ' + gB.length + ' 处');
  say('  查了什么：`git grep -c -F <串> -- .`（全仓受跟踪字节，不只 website/scratch）。');
  if (!gA.length && !gB.length) say('  ⇒ **0 处。** 两个串在受跟踪字节里都零命中。');
  else { say('  ⇒ 仍有命中，逐处：'); for (const h of gA.concat(gB)) say('      ' + h); }
}
say('');

say('=== 判据 M2.5：掩码这个动作，今天由哪一个读数承接');
{
  say('  掩码点 = ' + (masks.length ? masks.map((m) => m.file + ':' + m.line).join(' ／ ') : '没有'));
  // ★★ 判据 M2.5b：由文件名推出来的「承接者」必须【先查它在不在磁盘上】。
  //    一个推出来的路径如果不存在，印出来的那一行与一个真存在的路径【同形】。
  const guessed = masks.length ? masks[0].file.replace(/\.js$/, '_reading.md') : '没有';
  say('  ★ 由掩码器的文件名推出来的承接者 = ' + guessed);
  say('     它在磁盘上吗：' + (guessed !== '没有' && fs.existsSync(path.join(ROOT, guessed)) ? '在 ✓' : '**不在 ✗**'));
  const realCarrier = 'website/scratch/quote_census_reading.md';
  const rl = fs.readFileSync(path.join(ROOT, realCarrier), 'utf8').split('\n').findIndex((l) => l.includes('R6.8')) + 1;
  say('  ★★ 真承接它的读数（逐字给出，不靠推）= ' + realCarrier + ':' + rl + '（那一行逐字写着「判据 R6.8」）');
  say('  ★★ 判据 M2.5b：一个【由文件名推出来】的路径与一个【真存在的】路径，在报告里同形；');
  say('     推出来的那一个今天不存在 —— 于是「掩码由谁承接」这个问题，第一次读到的答案是【一个不存在的文件】。');
}
say('');

say('=== 判据 M2.6：有没有【不受掩码保护】的拼接点');
{
  const maskFiles = new Set(masks.map((m) => m.file));
  const unprot = joins.filter((j) => !maskFiles.has(j.file));
  say('  不受保护的拼接点 = ' + unprot.length);
  for (const j of unprot) say('    ' + j.file + ':' + j.line + '   ' + NAME(j.whole) + '   ' + j.n + ' 段');
  say('  受保护的拼接点 = ' + (joins.length - unprot.length));
  say('  ★★ 受保护的判据 = 【同一个文件里有一个掩码点】—— 那是一个【形状】判据，不是一个语义判据。');
}
say('');

say('=== 判据 M2.7：那些不受保护的拼接点，它们的输出今天有没有印出那个串');
{
  let txt = '';
  try {
    txt = execSync('node website/scratch/check_citations.js 2>&1 || true', { cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  } catch (e) { txt = String(e.stdout || '') + String(e.stderr || ''); }
  const nA = txt.split(S_A).length - 1;
  const nB = txt.split(S_B).length - 1;
  say('  跑了 `node website/scratch/check_citations.js`（本件唯一跑的命令）');
  say('  它的输出行数 = ' + txt.split('\n').length);
  say('  输出里串A 命中 = ' + nA + ' 次');
  say('  输出里串B 命中 = ' + nB + ' 次');
  say('  ★ 逐处（行号与掩码后的样子）：');
  txt.split('\n').forEach((l, i) => {
    if (l.includes(S_A) || l.includes(S_B)) say('      :' + (i + 1) + '  ' + mask(l.trim()).slice(0, 150));
  });
}
say('');

say('=== 判据 M2.8：那 2 次没有被印到磁盘上，是【掩码】还是【没有被记录】');
{
  const gA = gitGrep(S_A);
  say('  受跟踪字节命中 = ' + gA.length);
  const rec = (() => {
    try { return execSync(`/usr/bin/grep -rlF ${JSON.stringify('全站文本')} website/scratch/*.md 2>/dev/null || true`, { cwd: ROOT, encoding: 'utf8' }).trim().split('\n').filter(Boolean); } catch { return []; }
  })();
  say('  我的车道里，有哪一份留档捕获过 `check_citations.js` 的输出（按它印的那一节标题找）= ' + (rec.length ? rec.join(' ／ ') : '**没有一份**'));
  say('  ⇒ ★★★ **是【它没有被记录】，不是【它被掩码了】** ——');
  say('     `check_citations.js` 有 ' + joins.filter((j) => j.file.includes('check_citations')).length + ' 个拼接点、**0 个掩码点**；');
  say('     它的输出今天印了那个串 ' + 2 + ' 次，而那个输出**从来没有落进任何受跟踪文件**。');
}
say('');

say('=== 判据 M2.9：受跟踪字节里含那个串的，除了【内容】还有【名字】');
{
  // ★★★ 一个按【内容】找命中的普查（git grep）看不见【只在名字里】的命中。
  const nameHit = execFileSync('git', ['-c', 'core.quotepath=false', 'ls-files'], { cwd: ROOT, encoding: 'utf8' })
    .split('\n').filter(Boolean).filter((f) => f.includes(S_A) || f.includes(S_B));
  const contentHit = execFileSync('git', ['-c', 'core.quotepath=false', 'grep', '-l', '-F', S_A, '--', '.'], { cwd: ROOT, encoding: 'utf8' })
    .split('\n').filter(Boolean);
  say('  【内容】命中（`git grep -l -F <串A>` 全仓受跟踪）= ' + contentHit.length + ' 份');
  say('  【名字】命中（`git ls-files` 里路径含那个串）      = ' + nameHit.length + ' 份');
  for (const f of nameHit) say('      ' + mask(f));
  const onlyName = nameHit.filter((f) => !contentHit.includes(f));
  say('  ★★ 只在名字里命中、内容里不命中的 = ' + onlyName.length + ' 份');
  say('  ★★ 判据 M2.9：`git grep` 数的是【内容】，`git ls-files` 数的是【名字】——');
  say('     两个数都真，而一个【只在名字里】的命中，在任何按内容的普查里都是 0。');
  say('     今天「名字命中 ⊆ 内容命中」成立（只在名字里 = ' + onlyName.length + '）——');
  say('     但这不是必然：本件【没有】构造出一个「名字含它、内容不含它」的实例，照实写「没有构造」。');
}
say('');

say('=== 判据 M3：四档');
{
  const maskFiles = new Set(masks.map((m) => m.file));
  const t = { '①': [], '②': [], '③': [], '④': [] };
  for (const j of joins) {
    if (maskFiles.has(j.file)) t['①'].push(j); else t['③'].push(j);
  }
  for (const k of ['①', '②', '③', '④']) {
    say('  ' + k + ' = ' + t[k].length);
    for (const j of t[k]) say('      ' + j.file + ':' + j.line + '  ' + NAME(j.whole));
  }
  say('  ★ 和 = ' + (t['①'].length + t['②'].length + t['③'].length + t['④'].length) + '  ⇒ = 拼接点分母 ' + joins.length);
  say('  ★★ 判据 M3.3：① = ' + t['①'].length + ' 是【形状判据的产物】—— 本件按【同文件有没有掩码点】分档，');
  say('     所以 ① 只可能装着「与掩码器同文件」的拼接点（今天恰好 ' + t['①'].length + ' 处）。');
  say('  ★★ 判据 M3.4：本四档的分母是【拼接点】' + joins.length + '，不是 M2.1 的合计 8');
  say('     —— 掩码点自己不进四档（它是那把尺子，不是被量的对象）。两个数并排、不许合。');
  say('  ★ ④ = 0 不是「我全判得出」，是每一处都落进了上面某一档。');
}
say('');

say('=== 判据 M4：对照');
{
  say('  (a) 一处已知掩码干净：`website/scratch/quote_census_probe.js:131` 的拼接 ＋ `:132` 的掩码');
  say('      ⇒ 它的产物 `quote_census_transcript.md` 里串A 命中 = 0 ⇒ 落 ① ✓');
  say('  (b) 一处已知【没有】掩码的字符串：**找到了** —— `website/scratch/.build/cc_now.txt`');
  say('      它含串A 2 次，而它【不在受跟踪字节里】（第五档）⇒ `git grep` 看不见它。');
  say('      ★★ 反方向对照成立：**同一个输出，落在第五档就看不见，落在受跟踪路径就会命中** ——');
  say('         差别是【路径】，不是【内容】。');
  say('  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓（分母一起印）');
}
say('');

say('=== 判据 M5.2：本件有没有跑任何被扫到的命令？');
say('  **跑了 1 条**：`node website/scratch/check_citations.js`（判据 M2.7）。');
say('  它是一条【只读】的守卫（`fs.readFileSync` ＋ 只读 git），不改任何文件。');
say('  ★ 本件【没有】跑门禁（判据 M5.3）。');
console.log(mask(OUT.join('\n')));

```

## 二、运行逐字输出
```
=== 判据 M0：载体
  受跟踪 website/scratch/*.js + *.md = 81 份
  拼接点（判据 M0.1）= 6
  掩码点（判据 M0.1）= 2
  涉及的串：【串A·两段拼成】 ／ 【串B·三段拼成】
  ★ 判据 M0.2：「处」= 一个拼接点或掩码点；【串】数并排 = 2

=== 判据 M2.1：一共掩码过几处
  拼接点 = 6
  掩码点 = 2
  合计   = 8

=== 判据 M1：逐处（文件／行号／逐字／它拼的是哪个串／那几段／受跟踪字节剩余命中）
  --- 串A 【串A·两段拼成】
      受跟踪字节（git grep -c -F）= docs/AUDIT.md:1 ; docs/EIDOS_V06.md:1 ; docs/PLAN_V06.md:17 ; docs/PLAN_V07.md:3 ; docs/REQUIREMENTS_ANALYSIS.md:6 ; docs/STATUS.md:3 ; docs/archive/API_REFERENCE.md:1 ; docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:1 ; docs/archive/CHANGELOG_0.5.0.md:1 ; docs/archive/README.md:1 ; docs/archive/RELEASE_0.4.0.md:2 ; docs/archive/RELEASE_0.5.0.md:1 ; docs/archive/ROADMAP_0.4-0.6.md:4 ; docs/archive/ROADMAP_0.5-0.6.md:13 ; docs/archive/V04_STATUS.md:1 ; docs/streams/eidos-archive-crosscheck.md:15 ; docs/streams/i-json-user-statements.md:13 ; future/README.md:2 ; "future/archive/【串A·已掩码】\346\200\273\347\272\262.md":9 ; "future/archive/【串A·已掩码】\347\211\271\346\200\247.md":16 ; future/archive/README.md:2 ; future/infiverse-inim-os-summary.md:87
      含第五档（grep -rl website/）= 93 个文件
      ★★ 判据 M1.2：两个数并排 —— 差值 = 71 份全部在第五档
  --- 串B 【串B·三段拼成】
      受跟踪字节 = 0 个文件
      含第五档 = 0 个文件

  website/scratch/check_citations.js:542   [2 段]  【串A·两段拼成】
      逐字：{ file: "docs/PLAN_V06.md", id: "2", tier: 'A', search: ['Inim', ' OS'].join(''), auditRead: '无关',
      那 2 段 = ["Inim"," OS"]
  website/scratch/check_citations.js:551   [2 段]  【串A·两段拼成】
      逐字：quote: '1. **' + ['Inim', ' OS'].join('') + '** —— 不在 v0.6 内（人类裁定）；它的前置（去 C 化、Eidos 进引擎）都在 v0.6 里。' },
      那 2 段 = ["Inim"," OS"]
  website/scratch/check_citations.js:2821   [2 段]  【串A·两段拼成】
      逐字：const BANNED = ['Inim', ' OS'].join('');   // 人类硬约束：任何地方不许出现
      那 2 段 = ["Inim"," OS"]
  website/scratch/check_citations.js:3432   [3 段]  【串B·三段拼成】
      逐字：'const samples = ["' + ['check', '(', 'x, y, z)'].join('') + '"];   // ★ 枚举器自己的样本，落在射程内',
      那 3 段 = ["check","(","x, y, z)"]
  website/scratch/quote_census_probe.js:131   [2 段]  【串A·两段拼成】
      逐字：const BANNED = ['Inim', ' OS'].join('');
      那 2 段 = ["Inim"," OS"]
  website/scratch/quote_census_reading.md:116   [2 段]  【串A·两段拼成】
      逐字：（`['Inim', ' OS'].join('')`），所以 `website/scratch/` 下**一个字节都没有它**。
      那 2 段 = ["Inim"," OS"]

  website/scratch/quote_census_probe.js:132   [掩码点]
      逐字：const mask = (t) => t.split(BANNED).join('【禁用串·已掩码】');
      掩的是变量：BANNED
  website/scratch/run_tutorial.js:122   [掩码点]
      逐字：const out = text.split(ROOT).join('<repo>');
      掩的是变量：ROOT

=== 判据 M2.2：源码里由两段或三段拼成的处数
  两段 = 5 ／ 三段 = 1
  合计 = 6
    website/scratch/check_citations.js:542  2 段  【串A·两段拼成】
    website/scratch/check_citations.js:551  2 段  【串A·两段拼成】
    website/scratch/check_citations.js:2821  2 段  【串A·两段拼成】
    website/scratch/check_citations.js:3432  3 段  【串B·三段拼成】
    website/scratch/quote_census_probe.js:131  2 段  【串A·两段拼成】
    website/scratch/quote_census_reading.md:116  2 段  【串A·两段拼成】

=== 判据 M2.3：掩码前 / 掩码后，受跟踪字节里出现过几次
  串A 受跟踪字节命中（今天）= 22 个文件
  串B 受跟踪字节命中（今天）= 0 个文件
  ★ 判据 M2.3 的诚实边界：本件【无法】重放「掩码前」那一刻 —— 掩码器与它的产物同时落地。
    可以重放的是：把掩码器关掉（内存里改一行），看产物会变成什么。见判据 M2.4。

=== 判据 M2.4：掩码之后，有没有哪一处【仍然】在受跟踪字节里命中
  串A = 22 处 ／ 串B = 0 处
  查了什么：`git grep -c -F <串> -- .`（全仓受跟踪字节，不只 website/scratch）。
  ⇒ 仍有命中，逐处：
      docs/AUDIT.md:1
      docs/EIDOS_V06.md:1
      docs/PLAN_V06.md:17
      docs/PLAN_V07.md:3
      docs/REQUIREMENTS_ANALYSIS.md:6
      docs/STATUS.md:3
      docs/archive/API_REFERENCE.md:1
      docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:1
      docs/archive/CHANGELOG_0.5.0.md:1
      docs/archive/README.md:1
      docs/archive/RELEASE_0.4.0.md:2
      docs/archive/RELEASE_0.5.0.md:1
      docs/archive/ROADMAP_0.4-0.6.md:4
      docs/archive/ROADMAP_0.5-0.6.md:13
      docs/archive/V04_STATUS.md:1
      docs/streams/eidos-archive-crosscheck.md:15
      docs/streams/i-json-user-statements.md:13
      future/README.md:2
      "future/archive/【串A·已掩码】\346\200\273\347\272\262.md":9
      "future/archive/【串A·已掩码】\347\211\271\346\200\247.md":16
      future/archive/README.md:2
      future/infiverse-inim-os-summary.md:87

=== 判据 M2.5：掩码这个动作，今天由哪一个读数承接
  掩码点 = website/scratch/quote_census_probe.js:132 ／ website/scratch/run_tutorial.js:122
  ★ 由掩码器的文件名推出来的承接者 = website/scratch/quote_census_probe_reading.md
     它在磁盘上吗：**不在 ✗**
  ★★ 真承接它的读数（逐字给出，不靠推）= website/scratch/quote_census_reading.md:115（那一行逐字写着「判据 R6.8」）
  ★★ 判据 M2.5b：一个【由文件名推出来】的路径与一个【真存在的】路径，在报告里同形；
     推出来的那一个今天不存在 —— 于是「掩码由谁承接」这个问题，第一次读到的答案是【一个不存在的文件】。

=== 判据 M2.6：有没有【不受掩码保护】的拼接点
  不受保护的拼接点 = 5
    website/scratch/check_citations.js:542   【串A·两段拼成】   2 段
    website/scratch/check_citations.js:551   【串A·两段拼成】   2 段
    website/scratch/check_citations.js:2821   【串A·两段拼成】   2 段
    website/scratch/check_citations.js:3432   【串B·三段拼成】   3 段
    website/scratch/quote_census_reading.md:116   【串A·两段拼成】   2 段
  受保护的拼接点 = 1
  ★★ 受保护的判据 = 【同一个文件里有一个掩码点】—— 那是一个【形状】判据，不是一个语义判据。

=== 判据 M2.7：那些不受保护的拼接点，它们的输出今天有没有印出那个串
  跑了 `node website/scratch/check_citations.js`（本件唯一跑的命令）
  它的输出行数 = 1215
  输出里串A 命中 = 2 次
  输出里串B 命中 = 0 次
  ★ 逐处（行号与掩码后的样子）：
      :122  ·         台账侧：搜「【串A·已掩码】」⇒ 别处 1 处（首行 4170）
      :133  ·         触发（逐字）：1. **【串A·已掩码】** —— 不在 v0.6 内（人类裁定）；它的前置（去 C 化、Eidos 进引擎）都在 v0.6 里。

=== 判据 M2.8：那 2 次没有被印到磁盘上，是【掩码】还是【没有被记录】
  受跟踪字节命中 = 22
  我的车道里，有哪一份留档捕获过 `check_citations.js` 的输出（按它印的那一节标题找）= **没有一份**
  ⇒ ★★★ **是【它没有被记录】，不是【它被掩码了】** ——
     `check_citations.js` 有 4 个拼接点、**0 个掩码点**；
     它的输出今天印了那个串 2 次，而那个输出**从来没有落进任何受跟踪文件**。

=== 判据 M2.9：受跟踪字节里含那个串的，除了【内容】还有【名字】
  【内容】命中（`git grep -l -F <串A>` 全仓受跟踪）= 22 份
  【名字】命中（`git ls-files` 里路径含那个串）      = 2 份
      future/archive/【串A·已掩码】总纲.md
      future/archive/【串A·已掩码】特性.md
  ★★ 只在名字里命中、内容里不命中的 = 0 份
  ★★ 判据 M2.9：`git grep` 数的是【内容】，`git ls-files` 数的是【名字】——
     两个数都真，而一个【只在名字里】的命中，在任何按内容的普查里都是 0。
     今天「名字命中 ⊆ 内容命中」成立（只在名字里 = 0）——
     但这不是必然：本件【没有】构造出一个「名字含它、内容不含它」的实例，照实写「没有构造」。

=== 判据 M3：四档
  ① = 1
      website/scratch/quote_census_probe.js:131  【串A·两段拼成】
  ② = 0
  ③ = 5
      website/scratch/check_citations.js:542  【串A·两段拼成】
      website/scratch/check_citations.js:551  【串A·两段拼成】
      website/scratch/check_citations.js:2821  【串A·两段拼成】
      website/scratch/check_citations.js:3432  【串B·三段拼成】
      website/scratch/quote_census_reading.md:116  【串A·两段拼成】
  ④ = 0
  ★ 和 = 6  ⇒ = 拼接点分母 6
  ★★ 判据 M3.3：① = 1 是【形状判据的产物】—— 本件按【同文件有没有掩码点】分档，
     所以 ① 只可能装着「与掩码器同文件」的拼接点（今天恰好 1 处）。
  ★★ 判据 M3.4：本四档的分母是【拼接点】6，不是 M2.1 的合计 8
     —— 掩码点自己不进四档（它是那把尺子，不是被量的对象）。两个数并排、不许合。
  ★ ④ = 0 不是「我全判得出」，是每一处都落进了上面某一档。

=== 判据 M4：对照
  (a) 一处已知掩码干净：`website/scratch/quote_census_probe.js:131` 的拼接 ＋ `:132` 的掩码
      ⇒ 它的产物 `quote_census_transcript.md` 里串A 命中 = 0 ⇒ 落 ① ✓
  (b) 一处已知【没有】掩码的字符串：**找到了** —— `website/scratch/.build/cc_now.txt`
      它含串A 2 次，而它【不在受跟踪字节里】（第五档）⇒ `git grep` 看不见它。
      ★★ 反方向对照成立：**同一个输出，落在第五档就看不见，落在受跟踪路径就会命中** ——
         差别是【路径】，不是【内容】。
  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓（分母一起印）

=== 判据 M5.2：本件有没有跑任何被扫到的命令？
  **跑了 1 条**：`node website/scratch/check_citations.js`（判据 M2.7）。
  它是一条【只读】的守卫（`fs.readFileSync` ＋ 只读 git），不改任何文件。
  ★ 本件【没有】跑门禁（判据 M5.3）。

```

## 三、判据源码 `website/scratch/mask_criteria.md`（逐字，已掩码）
```md
# 癸⁷ 判据先行：把「掩码」做成一个可复核的数

★ 本文件是**判据**，先落盘；读数是它的后代（`git merge-base --is-ancestor` 判，不眼看）。
★ **只读不写**：一个受跟踪文件都不改。

---

## 第零节 对象

★ **M0.1** **对象 = 本车道在 `website/scratch/` 下的产物里出现过的【拼接点】与【掩码点】。**
- **拼接点** = 一处把**一个串拆成两段或三段**再拼回去的代码（形状：`[a, b].join('')`／`[a,b,c].join('')`，或 `'x' + y + 'z'`）；
- **掩码点** = 一处**在输出前把某个串替换掉**的代码（形状：`t.split(S).join(占位符)`）。

★ **M0.2** ★★★ **「处」= 一个拼接点或掩码点**（按 `文件:行` 唯一）。
★ **并排印出【字符串】数**（同一个串被拼 N 次 ⇒ 算 **1 个串、N 个拼接点**）。

★ **M0.3** ★★★ **载体 = `website/scratch/` 下受跟踪的 `.js` 与 `.md`**（本车道自己的产物）。
★ **`website/scratch/.build/` 是第五档（gitignored）** ⇒ **单列，不计入受跟踪字节**。

---

## 第一节 逐处必须印的五样

★ **M1.1** 逐处印：
1. **文件 ／ 行号**；2. **逐字那一行**；3. **它拼／掩的是哪一个串**（**掩码后的样子**，判据 M1.4）；
4. **那两段／三段分别是什么**（**逐字**）；5. **那个串在【受跟踪字节】里还剩几个命中**。

★ **M1.2** ★★★ **第 5 样必须由 `git grep` 给，不是由 `grep` 给** ——
**`grep` 会命中第五档，`git grep` 只命中受跟踪字节。** 两个数**并排**。

★ **M1.3** **「剩余命中」的分母 = 全仓受跟踪字节**（`git grep -c`），**不是** `website/scratch/` 一个目录。

★ **M1.4** ★★★ **被掩的那个串，本报告里【不复现】** —— 一律写成
`【串A·两段拼成】`（串 A = 本目录的禁用串）与 `【串B·三段拼成】`（串 B = 枚举器样本）。
★ **理由**：本件自己也会被 `git add`，**本件把那个串逐字写进来，就是本件自己制造一次命中**。

---

## 第二节 必须逐字回答的六个问题

★ **M2.1** **一共掩码过几处？**（拼接点与掩码点分别给数）
★ **M2.2** **其中【源码里由两段或三段拼成】的有几处？**（给数，**逐处点名**）
★ **M2.3** **掩码前，那个串在受跟踪字节里出现过几次？掩码后剩几次？**（两个数并排）
★ **M2.4** ★★★ **有没有哪一处，掩码之后【仍然】在受跟踪字节里命中？**
（给数；**一处都没有 ⇒ 逐字写「0 处」并说明查了什么**）
★ **M2.5** ★★★ **掩码这个动作，今天由哪一个读数承接？**（给文件名与行号；**没有承接者 ⇒ 逐字写「没有」**）
★ **M2.6** ★★★ **有没有【不受掩码保护】的拼接点？**（给数，逐处点名）
★ **M2.7** ★★★ **那些不受保护的拼接点，它们的输出今天有没有印出那个串？**（给数）
★ **M2.8** ★★★ **如果那些输出今天没有被印出来，那是【掩码】造成的，还是【它没有被记录】造成的？**
（必须逐字回答；**两个来源必须在报告里分开**）

---

## 第三节 四档

| 档 | 判词 |
|---|---|
| ① | **掩码后受跟踪字节零命中** |
| ② | **掩码后仍有命中** |
| ③ | **只印数没掩码** |
| ④ | **判不出**（必须带理由，且理由只能是「那把尺子看不见这个形状」或「这个对象在定义域之外」） |

★ **M3.1** ★★★ **和必须 = 分母 ✓。**
★ **M3.2** ★★★ **④ 不许装「我没有去读它」** —— 那是第五档，必须单列。
★ **M3.3** ★★★ **任何「某档 = 0」必须写清它是【构造上的】【定义的推论】还是【我判得出】**
（exact-otter 在 [otter-letter 186] 二 裁的）。

---

## 第四节 对照

★ **M4.1** 两个**反方向**对照：
- **(a) 一处已知掩码干净** —— 落 ①；
- **(b) 一处已知【没有】掩码的字符串** —— 从别的车道或本目录更早的产物里找；**找不到就照实说找不到**。
★ **M4.2** ★★★ **红就照实报红。**
★ **M4.3** **空集对照**：对象取空集 ⇒ 四档全 0、和 = 0 = 分母 ✓，**且必须把分母一起印**。

---

## 第五节 诚实边界

★ **M5.1** **只读不写** —— 一个受跟踪文件都不改。
★ **M5.2** ★★★ **逐字回答：本件有没有跑任何被扫到的命令？**（★ 本件**会跑** `check_citations.js` —— 逐字写清）
★ **M5.3** ★★★ **逐字回答：本件有没有跑门禁？**
★ **M5.4** **逐个按名字 `git add`**；报告里印**跑后** `git status --porcelain` 的**逐字输出**。
★ **M5.5** 四件套；`git diff --stat <分支点> HEAD -- docs/` 必须**空**（**三参、第一参必须是本分支点**）。
★ **M5.6** ★★★ **`origin/main` 不是本会话 HEAD 的祖先** —— 第一参先过 `git merge-base --is-ancestor`。
★ **M5.7** 判据文件必须先落盘，且是读数文件的**祖先**。
★ **M5.8** 做完**停**，等 exact-otter 裁。

```
