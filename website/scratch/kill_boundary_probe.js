#!/usr/bin/env node
/* 甲⁷ 探针 —— 「kill 表宽一格」的三种边界变体，以及【是不是只有这一条规则在收它】。
 *
 * 它**不修改** ruler_gui6.js（已有产物，零字节改动）。它把那条取反模式抄成四个变体，
 * 对 check_citations.js:87 那条语句的**真实文本**逐一试。
 *
 *   V0 原样        `!D\b`                    —— 今天源码里就是这一条（右边界本来就有）
 *   V1 加左边界    `(?:^|[^!\w])!D\b`        —— 派件说的「加上词边界」的**有效**形态
 *   V2 整条删掉    不试任何取反模式            —— 用来分辨「是不是只有这一条在收它」
 *   V3 无边界      `!D`                      —— 用来证明右边界不是关键
 */
'use strict';
const fs = require('fs');
const path = require('path');

/* 与 ruler_gui6.js 同一套语句切分（只抄这一段，不改原文件） */
function splitStatements(src) {
  const out = []; let buf = [], from = null, depth = 0;
  const flush = () => { if (buf.length) { out.push({ from, to: buf[buf.length - 1].n, text: buf.map((b) => b.text).join('\n') }); buf = []; } };
  for (let i = 0; i < src.length; i++) {
    const l = src[i];
    if (buf.length === 0) { depth = 0; from = i + 1; }
    const t = l.replace(/\/\/.*$/, '');
    for (const ch of t) { if ('([{'.includes(ch)) depth++; else if (')]}'.includes(ch)) depth--; }
    buf.push({ n: i + 1, text: l });
    const cont = /[+\-*/%=<>!&|,?:.]\s*$/.test(t.trim());
    const hdr = /^\s*(if|for|while|switch|catch)\b/.test(t) && /\{\s*$/.test(t.trim());
    if (hdr) { buf.pop(); flush(); buf = [{ n: i + 1, text: l }]; from = i + 1; continue; }
    if (depth <= 0 && !cont && /[;{}]\s*$/.test(t.trim())) flush();
    if (depth <= 0 && !cont && /^\s*\}\s*$/.test(t.trim())) flush();
  }
  flush(); return out;
}

const root = process.argv[2] || '.';
const rel = 'website/scratch/check_citations.js';
const src = fs.readFileSync(path.join(root, rel), 'utf8').split('\n');
const stmts = splitStatements(src);
const at = (n) => stmts.find((s) => s.from <= n && s.to >= n);

const NAME = 'D';
const n = NAME;
const V = {
  'V0 原样 `!D\\b`（= 今天源码）': new RegExp('!' + n + '\\b'),
  'V1 加左边界 `(?:^|[^!\\w])!D\\b`': new RegExp('(?:^|[^!\\w])!' + n + '\\b'),
  'V3 无边界 `!D`': new RegExp('!' + n),
};
const st87 = at(87);
const text87 = st87.text.replace(/\s+/g, ' ').trim();
console.log('=== check_citations.js:87 所在的那条语句（切分器给的边界）');
console.log('  语句跨度 = ' + st87.from + '-' + st87.to + '   （' + (st87.to - st87.from + 1) + ' 行）');
console.log('  文本前 160 字 = ' + JSON.stringify(text87.slice(0, 160)));
console.log();
console.log('=== 三种边界变体，对这条语句的**真实文本**');
for (const [k, re] of Object.entries(V)) console.log('  ' + k.padEnd(36) + ' ⇒ isKill = ' + re.test(text87) + '   (' + re.source + ')');
console.log();
console.log('=== V2：把 `!D` 这一条**整条删掉**之后，这条语句还剩几条 kill 规则命中？');
const KILL_A = ['length', 'includes', 'indexOf', 'some', 'every', 'forEach', 'map', 'filter', 'join', 'slice', 'trim', 'split'];
const rest = [
  ['if ( !D', new RegExp('if\\s*\\(\\s*!' + n + '\\b')], ['if ( D', new RegExp('if\\s*\\(\\s*' + n + '\\b')],
  ['D &&', new RegExp('\\b' + n + '\\s*&&')], ['D ||', new RegExp('\\b' + n + '\\s*\\|\\|')], ['D ?', new RegExp('\\b' + n + '\\s*\\?')],
  ['D != null', new RegExp('\\b' + n + '\\s*!=\\s*null')], ['D === undefined', new RegExp('\\b' + n + '\\s*===\\s*undefined')],
  ['Array.isArray(D)', new RegExp('Array\\.isArray\\s*\\(\\s*' + n + '\\b')], ['typeof D', new RegExp('typeof\\s+' + n + '\\b')],
  ['D ??', new RegExp('\\b' + n + '\\s*\\?\\?')],
  ['D.<KILL_A 方法>', new RegExp('\\b' + n + '\\s*\\.\\s*(?:' + KILL_A.join('|') + ')\\b')],
  ['D.<成员> ==/!=', new RegExp('\\b' + n + '\\s*\\.\\s*[A-Za-z_$][\\w$]*\\s*(?:===|!==|==|!=)')],
];
let hits = 0;
for (const [k, re] of rest) if (re.test(text87)) { console.log('  ★ 仍然命中：' + k + '   (' + re.source + ')'); hits++; }
console.log('  ⇒ 去掉 `!D` 之后，剩余命中 = ' + hits + ' 条' + (hits === 0 ? '  ⇒ **只有 `!D` 这一条在收它**' : '  ⇒ **另有规则在收它 —— 不许把 :87 归回 !D 这一条**'));
