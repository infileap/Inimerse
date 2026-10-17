#!/usr/bin/env node
/* 庚⁶ 那把尺子（把上一轮留在 .build 里的重扫器搬到树上）。
   形状与己⁶ 公布的同一句：顶层（列 0~2）× 不以 if/!/?./??/?/&&/|| 起头 × 解引用表内变量。
   ★ 唯一差别：**不跳过含 const/let/var/function 的行** —— 因为那些行里的解引用是一次【读】。 */
const fs = require('fs');
const path = require('path');
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
      if (/^\s*\/\//.test(l)) continue;
      if (!/^\s{0,2}(?=\S)/.test(l)) continue;
      const m = re.exec(l); if (!m) continue;
      const h = l.replace(/^\s*/, '');
      if (/^(if|!|\?\.|\?\?|\?|&&|\|\||\/\*|\*)/.test(h)) continue;
      hits.push({ file: f, line: n, v: m[2], text: l.trim() });
    }
  }
  return hits;
}
const root = process.argv[2] || '.';
const hits = scan(root);
console.log('RULER=庚⁶  DENOM=' + hits.length);
for (const h of hits) console.log(h.file + ':' + h.line + '\t[' + h.v + ']\t' + h.text.slice(0, 80));
