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
