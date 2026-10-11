#!/usr/bin/env node
/* 报告用读数 —— ★ 存在的理由是一条被证伪过的口径。
 *
 * exact-otter 核我的一笔分支时撞到：我报的是
 *     `git diff --stat 0217b34..71040c2`  →  5 files, +567 −76
 * 而合并方 `git diff HEAD^1 HEAD` 只看到 **1 个文件**（`+68/−7`）。
 * 两个数都对，但它们回答的是两个不同的问题：
 *   · 分支级 `--stat <base>..<tip>` 的**分母是【那条分支的起点】** —— 它回答
 *     「这条线相对它的起点长什么样」；分支被 rebase 过之后，起点可能已经在 main 上了。
 *   · 合并方要问的是**「这一笔带来了什么」** —— 那要问 `git diff HEAD^1 HEAD`。
 * ⇒ ★ **一笔干净的合并可以让 `git show --stat` 显示 0 个文件**（它印的是 combined diff，
 *   只列与两个父都不同的文件），所以「带来了什么」不能问 `git show --stat`。
 *
 * ★ 所以这个脚本把两个数都印出来，**每一行都带它是对哪个 base 算的**。
 *   它不消除那两个数的差别，它让那个差别在报告里看得见 ——
 *   因为读报告的人会把第一个数当成「这一轮带来了什么」，而它不是。
 *
 * 用法：node website/scratch/report_diff.js [base]
 *       base 省略时取 `origin/main` 在运行时的 sha（一个指针，与 markerScanRef 同理）。
 */
const path = require('path');
const { execFileSync } = require('child_process');

const HERE = __dirname;
const ROOT = path.resolve(HERE, '..', '..');
const git = (...a) => execFileSync('git', a, { cwd: ROOT, encoding: 'utf8' }).trim();

const base = process.argv[2] || git('rev-parse', 'origin/main');
const baseLabel = process.argv[2] ? `写死的 ${base.slice(0, 7)}` : `origin/main 解析出的 ${base.slice(0, 7)}（一个指针，不是钉）`;
const tip = git('rev-parse', 'HEAD');
const branch = git('rev-parse', '--abbrev-ref', 'HEAD');

console.log('=== 报告用读数（每一行都带它是对哪个 base 算的）===');
console.log(`分支：${branch}    tip：${tip.slice(0, 7)}`);
console.log(`base：${baseLabel}`);
console.log('');

const one = (range) => {
  const out = git('diff', '--stat', range).split('\n').filter(Boolean).pop() || '（空）';
  return out.replace(/\s+$/, '');
};

console.log(`① 分支级   git diff --stat ${base.slice(0, 7)}..${tip.slice(0, 7)}`);
console.log(`   ${one(`${base}..${tip}`)}`);
console.log(`   分母 = 【分支起点】。★ 它回答「这条线相对起点长什么样」——`);
console.log(`   ★ 它**不**回答「这一轮带来了什么」：分支被 rebase 过之后，起点可能已经在 main 上了。`);
console.log('');

console.log('② 逐笔   git diff --stat <c>^ <c>   ← 这才回答「每一笔带来了什么」');
const commits = git('rev-list', '--reverse', `${base}..${tip}`).split('\n').filter(Boolean);
commits.forEach((c, i) => {
  const parents = git('rev-list', '--parents', '-n', '1', c).split(' ').slice(1);
  const subject = git('log', '-1', '--format=%s', c);
  if (parents.length > 1) {
    console.log(`   ${i + 1}. ${c.slice(0, 7)}  ${subject}`);
    console.log(`      ★ 这是一笔合并（${parents.length} 个父）⇒ 「带来了什么」要问 git diff HEAD^1 HEAD，不问 git show --stat`);
    console.log(`      HEAD^1..HEAD: ${one(`${parents[0]}..${c}`)}`);
  } else {
    console.log(`   ${i + 1}. ${c.slice(0, 7)}  ${one(`${c}^..${c}`)}   ${subject}`);
  }
});
console.log('');

console.log(`③ 与远端的关系   git rev-list --left-right --count origin/${branch}...${branch}`);
console.log(`   ${git('rev-list', '--left-right', '--count', `origin/${branch}...${branch}`)}`);
console.log(`   左 = 远端有而本地没有（应为 0）；右 = 本地有而远端没有（未推的笔数）。`);
console.log('');

const docs = git('diff', '--stat', `${base}..${tip}`, '--', 'docs/');
console.log(`④ 写域检查   git diff --stat ${base.slice(0, 7)}..${tip.slice(0, 7)} -- docs/`);
console.log(`   ${docs ? docs : '（空）'}   ← 空 = 这一条线一个字都没动 docs/`);
