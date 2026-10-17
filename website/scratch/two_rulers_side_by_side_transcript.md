# 辛⁶ 逐字留档

## 一、两把尺子的源码
### website/scratch/ruler_geng6.js
```js
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
```
### website/scratch/ruler_jig6_recon.js
```js
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
```
## 二、三次运行（逐字）
```
$ RULER_SKIP_DECL=1 node website/scratch/ruler_jig6_recon.js . | head -1
RULER=己⁶′(重建)  SKIP_DECL=true  DENOM=49
$ RULER_SKIP_DECL=0 node website/scratch/ruler_jig6_recon.js . | head -1
RULER=己⁶′(重建)  SKIP_DECL=false  DENOM=105
$ node website/scratch/ruler_geng6.js . | head -1
RULER=庚⁶  DENOM=105
```
## 三、并集四档（逐字）
```
己⁶′(重建,跳声明) = 49   庚⁶ = 105   并集 = 105
① 两把都收 = 49
② 己⁶′收／庚⁶不收 = 0 
③ 己⁶′不收／庚⁶收 = 56
④ 两者都不收 = 0

★ 两个真死点各落哪一档
  check_citations.js:87  ⇒  己⁶′收=false  庚⁶收=true  ⇒  ③
  render_check.js:661  ⇒  己⁶′收=false  庚⁶收=false  ⇒  ④（并集里没有它）

③ 那 56 处的前 8 处
  check_citations.js:81
  check_citations.js:82
  check_citations.js:87
  check_citations.js:208
  check_citations.js:238
  check_citations.js:266
  check_citations.js:268
  check_citations.js:308
```
## 四、③ 那 56 处（逐字全表）
```
  check_citations.js:81
  check_citations.js:82
  check_citations.js:87
  check_citations.js:208
  check_citations.js:238
  check_citations.js:266
  check_citations.js:268
  check_citations.js:308
  check_citations.js:320
  check_citations.js:321
  check_citations.js:344
  check_citations.js:355
  check_citations.js:364
  check_citations.js:371
  check_citations.js:606
  check_citations.js:616
  check_citations.js:619
  check_citations.js:686
  check_citations.js:687
  check_citations.js:742
  check_citations.js:1689
  check_citations.js:1699
  check_citations.js:1717
  check_citations.js:1743
  check_citations.js:1749
  check_citations.js:1760
  check_citations.js:1767
  check_citations.js:1768
  check_citations.js:1774
  check_citations.js:1797
  check_citations.js:1892
  check_citations.js:1929
  check_citations.js:1962
  check_citations.js:2005
  check_citations.js:2131
  check_citations.js:2377
  check_citations.js:2378
  check_citations.js:2606
  check_citations.js:2646
  check_citations.js:2655
  check_citations.js:2789
  check_citations.js:3166
  check_citations.js:3668
  check_citations.js:3672
  check_citations.js:3680
  render_check.js:673
  render_check.js:681
  render_check.js:690
  render_check.js:694
  render_check.js:700
  render_check.js:719
  render_check.js:727
  render_check.js:728
  run_snippets.js:192
  run_snippets.js:217
  run_snippets.js:219
```
## 五、己⁶ 的留档里没有扫描器源码（逐字）
```
12:```
85:```
```
## 六、:661 的缩进与上下文（逐字）
```
    host.children.le$
  check('台账页：渲染出的条目数 = 数据里的条目数',
    host.children.length === D.entries.length, `${host.children.length} / ${D.entries.length}`);
```
