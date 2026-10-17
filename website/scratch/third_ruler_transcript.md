# 壬⁶ 逐字留档

## 一、第三把尺子的源码（website/scratch/ruler_ren6.js）
```js
#!/usr/bin/env node
/* 壬⁶ —— 第三把尺子，专打前两把的【共有盲区】。
   前两把的共有一条规则是「顶层：列 0~2」。它把【缩进 > 2 的多行续行】整类排除。
   本尺子**不看缩进**：
     1) 先把源码切成【逻辑语句】（按括号/花括号深度归零 + 不以运算符/逗号结尾来判语句结束）；
     2) 一条语句算「有保护」当且仅当**它自己**含有 if/!/?. /??/?/&&/|| 起头的保护形状，
        或含 `&&`/`||`/`?.`/`??` 与表内变量同现（与庚⁶ 同一条「有保护」定义）；
     3) 只报【无保护】的语句里出现的每一个表内变量解引用，**逐行**报，缩进多少都报。
   ★ 它与前两把的形状不同之处只有一条：**不用缩进当判据**。 */
const fs = require('fs');
const path = require('path');
const TABLE = ['S','D','CENSUS','RULING_ROWS','A_ROWS','MSR_SHA','ATT_SHA','manifest','SPEC'];
const FILES = ['check_citations.js','render_check.js','report_diff.js','run_snippets.js','run_tutorial.js',
  'snippets/caught-error-payload.js','snippets/div-zero-float.js','snippets/div-zero-int.js',
  'snippets/index-out-of-range.js','snippets/int-div-truncation.js','snippets/manifest.js','snippets/mod-zero.js'];
const derefRe = new RegExp('(^|[^A-Za-z0-9_.$])(' + TABLE.join('|') + ')[.\\[]');
const guardRe = /(&&|\|\||\?\.|\?\?)/;

function statements(src) {
  const out = [];
  let depth = 0, start = 1, buf = [];
  for (let i = 0; i < src.length; i++) {
    const l = src[i];
    if (/^\s*\/\//.test(l) && buf.length === 0) { start = i + 2; continue; }
    buf.push({ n: i + 1, text: l });
    // 括号深度
    for (const ch of l) { if ('([{'.includes(ch)) depth++; else if (')]}'.includes(ch)) depth--; }
    const t = l.replace(/\/\/.*$/, '').trimEnd();
    const continues = /[,+\-*/%=<>!&|?:]$/.test(t) || /\b(of|in)\s*$/.test(t);
    // ★ 控制流头（if/for/while/switch）与函数头：语句就是【头那一行】，不含块体
    const headerEnds = /\{\s*$/.test(t) || /^\s*(if|for|while|switch)\b/.test(t) || /=>\s*\{\s*$/.test(t);
    if ((depth <= 0 && !continues) || headerEnds) {
      out.push({ from: start, lines: buf });
      buf = []; start = i + 2;
    }
  }
  if (buf.length) out.push({ from: start, lines: buf });
  return out;
}

function scan(root) {
  const hits = [];
  for (const f of FILES) {
    const p = path.join(root, 'website/scratch', f);
    if (!fs.existsSync(p)) continue;
    const src = fs.readFileSync(p, 'utf8').split('\n');
    const PERLINE = process.env.RULER_PERLINE === '1';
    for (const st of (PERLINE ? src.map((t,i)=>({from:i+1,lines:[{n:i+1,text:t}]})) : statements(src))) {
      const body = st.lines.map((x) => x.text).join('\n');
      const head = st.lines[0].text.replace(/^\s*/, '');
      const protectedStmt = /^(if|!|\?\.|\?\?|\?|&&|\|\|)/.test(head) || guardRe.test(body);
      if (protectedStmt) continue;
      for (const ln of st.lines) {
        if (/^\s*\/\//.test(ln.text)) continue;
        const m = derefRe.exec(ln.text);
        if (!m) continue;
        hits.push({ file: f, line: ln.n, v: m[2], text: ln.text.trim() });
      }
    }
  }
  return hits;
}
const root = process.argv[2] || '.';
const hits = scan(root);
console.log('RULER=壬⁶(不看缩进)  DENOM=' + hits.length);
for (const h of hits) console.log(h.file + ':' + h.line + '\t[' + h.v + ']\t' + h.text.slice(0, 80));
```
## 二、三次运行（逐字）
```
$ node website/scratch/ruler_ren6.js . | head -1
RULER=壬⁶(不看缩进)  DENOM=60
$ RULER_PERLINE=1 node website/scratch/ruler_ren6.js . | head -1
RULER=壬⁶(不看缩进)  DENOM=109
$ node website/scratch/ruler_geng6.js . | head -1
RULER=庚⁶  DENOM=105
$ RULER_SKIP_DECL=1 node website/scratch/ruler_jig6_recon.js . | head -1
RULER=己⁶′(重建)  SKIP_DECL=true  DENOM=49
```
## 三、八路逐格 ＋ 四档覆盖（逐字）
```
己⁶′ = 49   庚⁶ = 105   壬⁶ = 60   并集 = 118
  键 001  = 13
  键 010  = 31
  键 011  = 25
  键 110  = 27
  键 111  = 22
四档能覆盖的 = 001 + 111 = 35
落在四档定义域之外的 = 83
  check_citations.js:87  己⁶′=false  庚⁶=true  壬⁶=true  ⇒ 键=011
  render_check.js:661  己⁶′=false  庚⁶=false  壬⁶=false  ⇒ 键=000
  check_citations.js:631  己⁶′=true  庚⁶=true  壬⁶=true  ⇒ 键=111
  check_citations.js:632  己⁶′=true  庚⁶=true  壬⁶=true  ⇒ 键=111
  check_citations.js:654  己⁶′=true  庚⁶=true  壬⁶=true  ⇒ 键=111
```
## 四、对照 (c)：RULER_PERLINE 开关
```
$ RULER_PERLINE=1 node website/scratch/ruler_ren6.js . | grep -c 'render_check.js:661'
1
```
