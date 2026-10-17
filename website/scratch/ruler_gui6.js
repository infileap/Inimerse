#!/usr/bin/env node
/* 第四把尺子 —— 癸⁶。**与前两把不同的不在参数，在种类。**
 *
 * 前三把都在问「这一行长什么样」：缩进几格、语句边界在哪、行内有没有关键词。
 * 这一把问的是**另一个问题**：
 *   「一个【外部数据变量】的值，能不能在到达某个解引用点之前，
 *     经由任何一条【不经过判空/存在性检查】的路径抵达那里。」
 *
 * 所以它跟踪的对象是**变量**（taint），不是**行**（shape）。
 *
 * ── 种子（什么算「外部数据变量」）—— 这是本尺子唯一的形状残留，写死在下面 ──
 *   SEED_CALL   : 右值里出现 readFileSync / execFileSync / JSON.parse / runInNewContext / require(
 *   SEED_CHAIN  : 右值里出现 `INFIVERSE_` 成员链（vm 上下文灌进 sandbox 的那些数据）
 *   SEED_PARAM  : 由环境变量 RULER_SEED_PARAM=1 打开 —— 把【函数参数】也算种子。
 *                 （关着的时候，参数进来的数据流一律看不见 —— 这是它的已知盲区。）
 *
 * ── 传播（fixpoint）──
 *   y = <含 tainted x 的表达式>  ⇒ y tainted（含解构 `const {a} = x`）
 *
 * ── 解引用点 ──
 *   NAME.member / NAME[...]，且不是 NAME?.member
 *
 * ── 判空/存在性检查（kill）──
 *   同一条语句内、或同一函数内最近的前置 `if (...)` 条件、或同一表达式里前置的 && / || / ?:：
 *   if(NAME) / !NAME / NAME&& / NAME|| / NAME? / NAME!=null / NAME===undefined /
 *   Array.isArray(NAME) / typeof NAME / NAME.length / NAME.map|filter|forEach|includes|some|every|join|slice|indexOf / NAME??
 *
 * ── 路径模型 ──
 *   线性化：沿语句顺序**向前回溯**，撞到【对 NAME 的 kill】⇒ 有保护；
 *   撞到【把 NAME 标脏的那次赋值】⇒ 无保护。
 *   ★ 已知盲区：**不做分支合并**。两条支路各自 kill 与否，这里只看线性顺序。
 *
 * 用法：node website/scratch/ruler_gui6.js <repoRoot>
 */
'use strict';
const fs = require('fs');
const path = require('path');

const NO_SEED = process.env.RULER_NO_SEED === '1';   /* 空集对照：种子表清空，DENOM 必须为 0 */
const SEED_CALL = /\b(readFileSync|execFileSync|execSync|JSON\.parse|runInNewContext|createContext)\s*\(|\brequire\s*\(/;
const SEED_CHAIN = /\bINFIVERSE_[A-Z_]*/;
const SEED_PARAM = process.env.RULER_SEED_PARAM === '1';
/* kill 表拆成两张 —— 它们的【定义域】不同，混在一张里会让「取属性」读成「判空」。
 *  A 真判空：对一个可能为空的值做存在性测试（会抛 or 会短路）
 *  B 取属性：只是取成员，它自己不判空 —— `D.entries` 与 `Object.entries(D)` 是两回事
 * 环境变量 RULER_KILL_B=1 把 B 也当 kill（= 修之前的行为），用于对照。 */
const KILL_A = ['length', 'includes', 'indexOf', 'some', 'every', 'forEach', 'map', 'filter', 'join', 'slice', 'trim', 'split'];
const KILL_B = ['keys', 'values', 'entries', 'has', 'get', 'toString'];
const KILL_METHODS = process.env.RULER_KILL_B === '1' ? KILL_A.concat(KILL_B) : KILL_A;

/* ---- 语句切分（与壬⁶ 同一套；种类差异不在这里，所以复用是诚实的） ---- */
function splitStatements(src) {
  const out = [];
  let buf = [], from = null, depth = 0;
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
  flush();
  return out;
}

/* ---- taint：从种子出发的 fixpoint ---- */
function buildTaint(stmts) {
  const tainted = new Set();
  const defLine = new Map();          // NAME -> 把它标脏的那一行
  const ASSIGN = /(?:^|[;{}\s])(?:const|let|var)?\s*([A-Za-z_$][\w$]*)\s*=\s*([^;]*?)(?=;|$)/;
  const DESTOBJ = /(?:const|let|var)\s*\{([^}]*)\}\s*=\s*([^;]*)$/;
  const refsTainted = (expr) => { for (const m of expr.matchAll(/\b([A-Za-z_$][\w$]*)\b/g)) if (tainted.has(m[1])) return true; return false; };
  const isSeed = (expr) => !NO_SEED && (SEED_CALL.test(expr) || SEED_CHAIN.test(expr));
  for (let pass = 0; pass < 6; pass++) {
    let grew = false;
    for (const st of stmts) {
      const one = st.text.replace(/\s+/g, ' ').replace(/[;{}]+\s*$/, '').trim();
      let m = one.match(DESTOBJ);
      if (m && (isSeed(m[2]) || refsTainted(m[2]))) {
        for (const nm of m[1].split(',')) { const k = nm.trim().split(':').pop().trim(); if (k && !tainted.has(k)) { tainted.add(k); defLine.set(k, st.to); grew = true; } }
        continue;
      }
      for (const m2 of one.matchAll(new RegExp(ASSIGN.source, 'g'))) {
        const [, name, rhs] = m2;
        if (isSeed(rhs) || refsTainted(rhs)) { if (!tainted.has(name)) { tainted.add(name); defLine.set(name, st.to); grew = true; } }
      }
      continue;
      // eslint-disable-next-line no-unreachable
      m = one.match(ASSIGN);
      if (!m) continue;
      const [, name, rhs] = m;
      if (isSeed(rhs) || refsTainted(rhs)) { if (!tainted.has(name)) { tainted.add(name); defLine.set(name, st.to); grew = true; } }
    }
    if (!grew) break;
  }
  if (SEED_PARAM) for (const st of stmts) { const m = st.text.replace(/\s+/g, ' ').trim().match(/^(?:function\s+[A-Za-z_$][\w$]*\s*)?\(([^)]*)\)\s*\{?/); if (m) for (const p of m[1].split(',')) { const k = p.trim(); if (/^[A-Za-z_$][\w$]*$/.test(k) && !tainted.has(k)) { tainted.add(k); defLine.set(k, st.from); } } }
  return { tainted, defLine };
}

function isKill(text, name) {
  const n = name.replace(/\$/g, '\\$');
  const pats = [
    new RegExp('if\\s*\\(\\s*!' + n + '\\b'), new RegExp('if\\s*\\(\\s*' + n + '\\b'),
    new RegExp('\\b' + n + '\\s*&&'), new RegExp('\\b' + n + '\\s*\\|\\|'), new RegExp('\\b' + n + '\\s*\\?'),
    new RegExp('!' + n + '\\b'), new RegExp('\\b' + n + '\\s*!=\\s*null'), new RegExp('\\b' + n + '\\s*===\\s*undefined'),
    new RegExp('Array\\.isArray\\s*\\(\\s*' + n + '\\b'), new RegExp('typeof\\s+' + n + '\\b'), new RegExp('\\b' + n + '\\s*\\?\\?'),
    new RegExp('\\b' + n + '\\s*\\.\\s*(?:' + KILL_METHODS.join('|') + ')\\b'),
    new RegExp('\\b' + n + '\\s*\\.\\s*[A-Za-z_$][\\w$]*\\s*(?:===|!==|==|!=)'),
  ];
  return pats.some((p) => p.test(text));
}

function run(root) {
  const files = require('child_process').execFileSync('git', ['ls-files', '--', 'website/scratch/*.js'], { cwd: root, encoding: 'utf8' }).trim().split('\n').filter(Boolean);
  const hits = [];
  for (const f of files) {
    const abs = path.join(root, f);
    if (!fs.existsSync(abs)) continue;
    const src = fs.readFileSync(abs, 'utf8').split('\n');
    const stmts = splitStatements(src);
    const { tainted, defLine } = buildTaint(stmts);
    if (process.env.RULER_DEBUG === '1') { console.error('[' + f + '] stmts=' + stmts.length + ' tainted=' + JSON.stringify([...tainted]) + ' defLine.D=' + defLine.get('D')); }
    if (process.env.RULER_DEBUG2 === '1') { for (const st of stmts) if (st.from <= 654 && st.to >= 654 || st.from <= 661 && st.to >= 661 || st.from <= 44 && st.to >= 44 || st.from <= 87 && st.to >= 87) console.error('  stmt ' + st.from + '-' + st.to + ' :: ' + JSON.stringify(st.text.replace(/\s+/g, ' ').slice(0, 120))); }
    for (const st of stmts) {
      const one = st.text.replace(/\/\/.*$/, '');
      for (const name of tainted) {
        const n = name.replace(/\$/g, '\\$');
        const re = new RegExp('\\b' + n + '\\s*(?!\\?\\.)(?:\\.\\s*[A-Za-z_$][\\w$]*|\\[)');
        if (!re.test(one)) continue;
        /* 回溯：本语句之内先看 kill，再看这条语句之前 */
        if (isKill(one, name)) continue;
        let protectedBy = null;
        for (let i = stmts.indexOf(st) - 1; i >= 0; i--) {
          const p = stmts[i];
          if (isKill(p.text, name)) { protectedBy = p.to; break; }
          if (defLine.get(name) === p.to) break;
        }
        if (protectedBy) continue;
        for (let ln = st.from; ln <= st.to; ln++) if (new RegExp('\\b' + n + '\\s*(?!\\?\\.)(?:\\.\\s*[A-Za-z_$][\\w$]*|\\[)').test(src[ln - 1].replace(/\/\/.*$/, ''))) hits.push(f + ':' + ln);
      }
    }
  }
  const uniq = [...new Set(hits)].sort((a, b) => { const [f1, n1] = a.split(':'), [f2, n2] = b.split(':'); return f1 === f2 ? +n1 - +n2 : f1.localeCompare(f2); });
  console.log('RULER=癸⁶(按数据流，不按形状)  SEED_PARAM=' + (SEED_PARAM ? '1' : '0') + '  DENOM=' + uniq.length);
  for (const h of uniq) console.log(h + '\t');
}
run(process.argv[2] || '.');
