# 己⁶ 逐字留档

## 一、扫描器形状判据
顶层（列 0~2）× 不以 if/!/?./??/?/&&/|| 起头 × 解引用外部数据变量(表见 md 第零节)

## 二、第一次（宽表，含 fs/vm/path）分母 = 66

## 三、本版（写进判据的表）分母 = 54；③ 有保护 = 25；② 无保护 = 29（:2749 已实测为 ①）

## 四、原始扫描逐字

```
FILES=12
DENOM=54  GUARDED=25  BARE=29
---BARE---
website/scratch/check_citations.js:309  [D]  check(`planV07Note（§${D.planV07Note.section}）引文逐字存在`,
website/scratch/check_citations.js:348  [MSR_SHA]  : `${MSR_SHA.slice(0, 7)}（钉）`;
website/scratch/check_citations.js:360  [ATT_SHA]  : `${ATT_SHA.slice(0, 7)}（钉）`;
website/scratch/check_citations.js:631  [RULING_ROWS]  tierSum === RULING_ROWS.length,
website/scratch/check_citations.js:632  [RULING_ROWS]  `${tierSum} / ${RULING_ROWS.length}`);
website/scratch/check_citations.js:654  [RULING_ROWS]  RULING_ROWS.forEach((r, i) => {
website/scratch/check_citations.js:669  [RULING_ROWS]  printed.length === RULING_ROWS.length * 3,
website/scratch/check_citations.js:670  [RULING_ROWS]  `${printed.length / 3} / ${RULING_ROWS.length}`);
website/scratch/check_citations.js:674  [RULING_ROWS]  info(`★ 一条裁定的差：手读的 **${RULING_ROWS.length}** 行，读自记录的那棵树 ${ROWS_SHA.slice(0, 7)}（钉）`);
website/scratch/check_citations.js:693  [A_ROWS]  info(`★ A 档（要登记）${A_ROWS.length} 行的命中判词（手读）：${Object.keys(readCount).map((k) => `${k} ${readCount[k]}`).
website/scratch/check_citations.js:695  [A_ROWS]  A_ROWS.forEach((r) => {
website/scratch/check_citations.js:1706  [D]  pinBox(D.builtAxes.axes, 'builtAxes.axes');
website/scratch/check_citations.js:1707  [D]  pinBox(D.builtAxes.collision, 'builtAxes.collision');
website/scratch/check_citations.js:1807  [D]  info(`㉖ 丙的分母 = \`status === 'ruled'\` 的条目数 = ${ruledEntries.length}（台账共 ${D.entries.length} 条）—— ★ 分母由 w
website/scratch/check_citations.js:2263  [D]  D.entries.forEach((e) => {
website/scratch/check_citations.js:2274  [D]  `　★ 分母 = D.entries.length，由 decisions.js 的 entries 数组定，不是 docs 定的。`);
website/scratch/check_citations.js:2749  [S]  specs.push({ what: '本页的主观测点 spec.js.ref', ref: S.ref, file: null });
website/scratch/check_citations.js:3634  [CENSUS]  info(`㉛ 乙 ⑦【分母：本文件里 ${'check'} 调用总数】= ${CENSUS.sites.length}`
website/scratch/check_citations.js:3636  [CENSUS]  info(`㉛ 乙 ⑦【第 3 个实参含 \${...} 插值 —— 读数被送进那个信道】= ${CENSUS.interp.length}`
website/scratch/check_citations.js:3639  [CENSUS]  info(`㉛ 乙 ⑦【其中今天【绿】的 ⇒ 它们今天什么都没说】= ${CENSUS.green}`
website/scratch/check_citations.js:3670  [D]  console.log(`条目数：${D.entries.length}`);
website/scratch/check_citations.js:3673  [D]  console.log(`引文钉：${D.entries.length} 条，涉及 ${docsTouched.size} 个 docs 文件（读自 ${Object.keys(byRef).map((r) 
website/scratch/check_citations.js:3674  [D]  console.log(`节号钉：${D.entries.length} 个`);
website/scratch/check_citations.js:3682  [D]  .map((k) => `ruledBy=${k} ${D.entries.filter((e) => e.ruledBy === k).length} 条`)
website/scratch/render_check.js:729  [D]  check(`台账页：筛选栏里有「${D.statuses[k].label}」这个按钮`, !!btn);
website/scratch/render_check.js:764  [D]  check(`规范页：${D.entries.length} 条裁定全部挂到了各自文档下面`,
website/scratch/run_snippets.js:287  [manifest]  languages: manifest.languages,
website/scratch/run_snippets.js:288  [manifest]  labels: manifest.labels,
website/scratch/run_snippets.js:332  [manifest]  console.log(`  语言：${manifest.languages.map((l) => manifest.labels[l]).join(' / ')}`);
---GUARDED---
website/scratch/check_citations.js:67  [D]  check('有 ref', !!(D && D.ref), D && D.ref);
website/scratch/check_citations.js:68  [D]  check('有 entries 且非空', !!(D && Array.isArray(D.entries) && D.entries.length), D && D.entries && D.entrie
website/scratch/check_citations.js:91  [D]  check('有 status', !!e.status && !!D.statuses[e.status], e.status);
website/scratch/check_citations.js:244  [D]  !(D.humanRuled && D.humanRuled.ids),
website/scratch/check_citations.js:245  [D]  (D.humanRuled && D.humanRuled.ids) ? `还有 ids：${D.humanRuled.ids.join('、')}` : '没有 ids');
website/scratch/check_citations.js:263  [S]  !!(S && D && S.ref === D.ref), S && S.ref);
website/scratch/check_citations.js:264  [S]  check('每份文档都声明了 role 是本站概括', !!(S && S.docs.every((x) => typeof x.role === 'string' && x.role.length)));
website/scratch/check_citations.js:347  [MSR_SHA]  ? `${MSR_SPEC.resolve} 解析出的 ${MSR_SHA.slice(0, 7)}（一个指针，不是钉）`
website/scratch/check_citations.js:356  [D]  || { kind: 'pin', resolve: (D.attribution && D.attribution.readAt) || D.ref };
website/scratch/check_citations.js:359  [ATT_SHA]  ? `${ATT_SPEC.resolve} 解析出的 ${ATT_SHA.slice(0, 7)}（一个指针，不是钉）`
website/scratch/check_citations.js:625  [RULING_ROWS]  notVerbatim.length ? `不是逐字：${notVerbatim.slice(0, 5).map((r) => `${r.file} §${r.id}`).join('、')}` : `${R
website/scratch/check_citations.js:628  [RULING_ROWS]  RULING_ROWS.forEach((r) => { tierCount[r.tier] = (tierCount[r.tier] || 0) + 1; });
website/scratch/check_citations.js:690  [A_ROWS]  missingRead.length ? `没有判词：${missingRead.map((r) => `${r.file} §${r.id}`).join('、')}` : `${A_ROWS.length
website/scratch/check_citations.js:692  [A_ROWS]  A_ROWS.forEach((r) => { readCount[r.auditRead] = (readCount[r.auditRead] || 0) + 1; });
website/scratch/check_citations.js:1821  [D]  Array.isArray(D.ruledByRoutes) && D.ruledByRoutes.length >= 3
website/scratch/check_citations.js:1823  [D]  `D.ruledByRoutes = ${(D.ruledByRoutes || []).map((r) => r.key).join(', ') || '（无）'}　★ site.js 必须从这一栏读，不写
website/scratch/check_citations.js:1831  [D]  + `${Array.isArray(D.ruledByRoutes) ? (D.ruledByRoutes || []).map((r) => r.key).join(', ') : '（不是数组）'}`
website/scratch/check_citations.js:1946  [D]  D.entries.some((e) => e.ruledIn && e.source && e.ruledIn.section !== e.source.section && !e.ruledIn.sect
website/scratch/check_citations.js:1947  [D]  `射程不同的条目数 = ${D.entries.filter((e) => e.ruledIn && e.source && e.ruledIn.section !== e.source.section &&
website/scratch/check_citations.js:2750  [S]  (S.docs || []).forEach((d) => { if (d.ref) specs.push({ what: `${d.file} 的逐份 ref`, ref: d.ref, file: d.f
website/scratch/check_citations.js:3218  [D]  D.entries.forEach((e) => { if (e.ruledBy) corpusKeys[e.ruledBy] = (corpusKeys[e.ruledBy] || 0) + 1; });
website/scratch/check_citations.js:3247  [D]  D.entries.forEach((e) => { if (e.ruledBy) counted[e.ruledBy] = (counted[e.ruledBy] || 0) + 1; });
website/scratch/check_citations.js:3672  [D]  D.entries.forEach((e) => { const r = readAtOf(e); byRef[r] = (byRef[r] || 0) + 1; });
website/scratch/check_citations.js:3685  [D]  + D.entries.filter((e) => e.source.file === AT.file && e.status === 'ruled' && !e.ruledBy).length + ' 条'
website/scratch/check_citations.js:3689  [S]  console.log(`手抄对照清单：${((S && S.docs) || []).length} 份文档（标题 / 行数 / 文件头引文）`
---PERFILE---
49  website/scratch/check_citations.js
2  website/scratch/render_check.js
0  website/scratch/report_diff.js
3  website/scratch/run_snippets.js
0  website/scratch/run_tutorial.js
0  website/scratch/snippets/caught-error-payload.js
0  website/scratch/snippets/div-zero-float.js
0  website/scratch/snippets/div-zero-int.js
0  website/scratch/snippets/index-out-of-range.js
0  website/scratch/snippets/int-div-truncation.js
0  website/scratch/snippets/manifest.js
0  website/scratch/snippets/mod-zero.js

```
