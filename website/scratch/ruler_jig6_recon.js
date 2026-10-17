#!/usr/bin/env node
/* 己⁶′ —— 己⁶ 那把尺子的【重建】。★ 它不是己⁶ 的尺子：己⁶ 的尺子落在
   website/scratch/.build/（第五档），不在任何树上，`git ls-files` 为空。
   本文件只复述 unprotected_deref_census_transcript.md 第 4 行公布的那条形状：
     顶层（列 0~2）× 不以 if/!/?./??/?/&&/|| 起头 × 解引用表内变量
   ★ 并**加上**那条从输出里反推出来的规则：跳过含 const/let/var/function 的行。
   把 SKIP_DECL 改成 false，分母就从 49 变成 105 —— 两条都满足上面那句公布的形状。 */
const fs = require('fs');
const path = require('path');
const SKIP_DECL = process.env.RULER_SKIP_DECL !== '0';
const TABLE = ['S','D','CENSUS','RULING_ROWS','A_ROWS','MSR_SHA','ATT_SHA','manifest','SPEC'];
const FILES = ['check_citations.js','render_check.js','report_diff.js','run_snippets.js','run_tutorial.js',
  'snippets/caught-error-payload.js','snippets/div-zero-float.js','snippets/div-zero-int.js',
  'snippets/index-out-of-range.js','snippets/int-div-truncation.js','snippets/manifest.js','snippets/mod-zero.js'];
const re = new RegExp('(^|[^A-Za-z0-9_.$])(' + TABLE.join('|') + ')[.\\[]');
function scan(root) {
  const hits = [];
  for (const f of FILES) {
    const p = path.join(root, 'website/scratch', f);
    if (!fs.existsSync(p)) continue;
    const src = fs.readFileSync(p, 'utf8').split('\n');
    for (let n = 1; n <= src.length; n++) {
      const l = src[n - 1];
      if (/^\s*\/\//.test(l)) continue;                      // 注释
      if (!/^\s{0,2}(?=\S)/.test(l)) continue;                // 顶层：列 0~2
      const m = re.exec(l); if (!m) continue;
      const h = l.replace(/^\s*/, '');
      if (/^(if|!|\?\.|\?\?|\?|&&|\|\||\/\*|\*)/.test(h)) continue;  // 有保护
      if (SKIP_DECL && /\b(const|let|var|function)\b/.test(l)) continue; // ← 反推出来的那条
      hits.push({ file: f, line: n, v: m[2], text: l.trim() });
    }
  }
  return hits;
}
const root = process.argv[2] || '.';
const hits = scan(root);
console.log('RULER=己⁶′(重建)  SKIP_DECL=' + SKIP_DECL + '  DENOM=' + hits.length);
for (const h of hits) console.log(h.file + ':' + h.line + '\t[' + h.v + ']\t' + h.text.slice(0, 80));
