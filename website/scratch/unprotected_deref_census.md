# 己⁶ · 「同一处无保护解引用」普查

```
pwd  = /home/sakiko/inimerse/.worktrees/website-ledger-readat
读法 = 工作树文件（本件【只读】，一个受跟踪文件也没有改）
```

## 零、先报载体与分母

**载体 = `git ls-files -- 'website/scratch/*.js'` ⇒ 12 份**：`check_citations.js`／`render_check.js`／`report_diff.js`／`run_snippets.js`／`run_tutorial.js`，以及 `snippets/` 下 7 份。

**★ 表（本件判据之一，必须先写下来）**：「外部**数据**变量」= 由仪器从站点文件／git 读入、**运行期可能为 `undefined`** 的标识符 ⇒ `S`／`D`／`CENSUS`／`RULING_ROWS`／`A_ROWS`／`MSR_SHA`／`ATT_SHA`／`manifest`／`SPEC`。
**排除 Node 内建模块（`fs`／`vm`／`path`）**：它们由 `require` 得到，`require` 失败是**模块加载错误**，与「数据为空」**不在同一个定义域**。

**形状** = **顶层**（列 0~2）解引用（`.x`／`[i]`／调用），且该语句**不以 `if`／`!`／`?.`／`??`／`?`／`&&`／`||` 起头**。

**分母 = 54 处**：`check_citations.js` **49**／`render_check.js` **2**／`run_snippets.js` **3**／其余 **0**。

## 一、四档（和 = 54 ✔，但**必须说清包含关系**）

| 档 | 处数 |
|---|---|
| **① 今天会崩**（把世界造成它说的那样 ⇒ 进程死） | **1** |
| **② 今天不崩但没有保护** | **28** |
| **③ 有保护** | **25** |
| **④ 判不出** | **0** |
| **合计** | **54** ✔ |

★★★ **① 是 ② 的子集，不是与它并列的第四栏。** `:2749` 在形状上属于 ②（它确实没有任何保护），只是**恰好被实测过**。把 ① 与 ② 并列而不写明包含，和会变成 **55**，而**两个数各自都对**。
⇒ **判据（署我名）：一个「已被实测的例子」不是一个独立的档 —— 它与它所从属的那一档重叠。把例子与类别并列，就是在一次普查里把同一处数两遍，而四档的和会安静地多一。**

## 二、① 那一处（逐字）
```
check_citations.js:2749  [S]  specs.push({ what: '本页的主观测点 spec.js.ref', ref: S.ref, file: null });
```
把 `data/spec.js` 改成不导出 `INFIVERSE_SPEC` ⇒ `rc=1`、**输出 17 行**、`FAIL = 0`。**它是 49 处里唯一一处完全没有任何判空保护的 `S` 解引用**（同族的 `:263`／`:264` 都写着 `S && …`）。

## 三、② 无保护的全部 28 处（`:2749` 之外）
- website/scratch/check_citations.js:309  [D]  check(`planV07Note（§${D.planV07Note.section}）引文逐字存在`,
- website/scratch/check_citations.js:348  [MSR_SHA]  : `${MSR_SHA.slice(0, 7)}（钉）`;
- website/scratch/check_citations.js:360  [ATT_SHA]  : `${ATT_SHA.slice(0, 7)}（钉）`;
- website/scratch/check_citations.js:631  [RULING_ROWS]  tierSum === RULING_ROWS.length,
- website/scratch/check_citations.js:632  [RULING_ROWS]  `${tierSum} / ${RULING_ROWS.length}`);
- website/scratch/check_citations.js:654  [RULING_ROWS]  RULING_ROWS.forEach((r, i) => {
- website/scratch/check_citations.js:669  [RULING_ROWS]  printed.length === RULING_ROWS.length * 3,
- website/scratch/check_citations.js:670  [RULING_ROWS]  `${printed.length / 3} / ${RULING_ROWS.length}`);
- website/scratch/check_citations.js:674  [RULING_ROWS]  info(`★ 一条裁定的差：手读的 **${RULING_ROWS.length}** 行，读自记录的那棵树 ${ROWS_SHA.slice(0, 7)}（钉）`);
- website/scratch/check_citations.js:693  [A_ROWS]  info(`★ A 档（要登记）${A_ROWS.length} 行的命中判词（手读）：${Object.keys(readCount).map((k) => `${k} ${readCount[k]}`).
- website/scratch/check_citations.js:695  [A_ROWS]  A_ROWS.forEach((r) => {
- website/scratch/check_citations.js:1706  [D]  pinBox(D.builtAxes.axes, 'builtAxes.axes');
- website/scratch/check_citations.js:1707  [D]  pinBox(D.builtAxes.collision, 'builtAxes.collision');
- website/scratch/check_citations.js:1807  [D]  info(`㉖ 丙的分母 = \`status === 'ruled'\` 的条目数 = ${ruledEntries.length}（台账共 ${D.entries.length} 条）—— ★ 分母由 w
- website/scratch/check_citations.js:2263  [D]  D.entries.forEach((e) => {
- website/scratch/check_citations.js:2274  [D]  `　★ 分母 = D.entries.length，由 decisions.js 的 entries 数组定，不是 docs 定的。`);
- website/scratch/check_citations.js:3634  [CENSUS]  info(`㉛ 乙 ⑦【分母：本文件里 ${'check'} 调用总数】= ${CENSUS.sites.length}`
- website/scratch/check_citations.js:3636  [CENSUS]  info(`㉛ 乙 ⑦【第 3 个实参含 \${...} 插值 —— 读数被送进那个信道】= ${CENSUS.interp.length}`
- website/scratch/check_citations.js:3639  [CENSUS]  info(`㉛ 乙 ⑦【其中今天【绿】的 ⇒ 它们今天什么都没说】= ${CENSUS.green}`
- website/scratch/check_citations.js:3670  [D]  console.log(`条目数：${D.entries.length}`);
- website/scratch/check_citations.js:3673  [D]  console.log(`引文钉：${D.entries.length} 条，涉及 ${docsTouched.size} 个 docs 文件（读自 ${Object.keys(byRef).map((r) 
- website/scratch/check_citations.js:3674  [D]  console.log(`节号钉：${D.entries.length} 个`);
- website/scratch/check_citations.js:3682  [D]  .map((k) => `ruledBy=${k} ${D.entries.filter((e) => e.ruledBy === k).length} 条`)
- website/scratch/render_check.js:729  [D]  check(`台账页：筛选栏里有「${D.statuses[k].label}」这个按钮`, !!btn);
- website/scratch/render_check.js:764  [D]  check(`规范页：${D.entries.length} 条裁定全部挂到了各自文档下面`,
- website/scratch/run_snippets.js:287  [manifest]  languages: manifest.languages,
- website/scratch/run_snippets.js:288  [manifest]  labels: manifest.labels,
- website/scratch/run_snippets.js:332  [manifest]  console.log(`  语言：${manifest.languages.map((l) => manifest.labels[l]).join(' / ')}`);

## 四、★ 分母由「什么算外部」定义 —— 而两次输出都内部自洽
第一版的表**把 `fs`／`vm`／`path` 也算成外部**（它们确实是「从外面来的」）⇒ 命中 **66 处**；本版写成判据的表把它们排除 ⇒ 命中 **54 处**。
⇒ **同一份代码、同一把扫描器、同样的形状，只是「外部」两个字换了定义，分母就从 66 变成 54，而两次输出逐项自洽、都能逐处指出来。**
⇒ **判据：一次普查的分母不是被量出来的，是被定义出来的；而定义住在判据里，不在扫描器里。**

## 五、两个反方向对照
**(a) 造一个会崩的 —— 通过**：`:2749` 由丁⁶ 实测定档 ① ✔（落在预期档上：**进程死、`FAIL` = 0**）
**(b) 造一个不会崩的 —— 通过**：`RULING_ROWS` 那一族（`:631`／`:632`／`:654`／`:669`／`:670`／`:674`）无保护但只做 `.length` 与 `forEach` ⇒ 世界造到 `RULING_ROWS = []` 时进程**活着** ⇒ 落在 **②** 而非 ① ✔（同一形状、不同变量，落进不同档）
**(c) 空集必须让对照红 —— 通过**：只在表上改一个定义，分母动了 **12** 处 ⇒ **分母是表造出来的，不是空集能产生的** ✔

## 六、本件没有做的事（不许写成 ④）
② 那 29 处里**只有 1 处（`:2749`）被实测过**「把世界造成它说的那样」会崩；其余 **28 处只判到「没有保护」这一层**。
⇒ **「没有保护」与「会崩」是两个断言。** 按已裁定判据，**不写成 ④，只写成「只做到这一层」。**

## 七、产物留档
- `website/scratch/unprotected_deref_census.md`（本文件）
- `website/scratch/unprotected_deref_census_transcript.md`（扫描器源码 ＋ 两次运行的逐字输出）
★ 两个名字**都用 `.md`**（`.gitignore:3:*.txt` 会吃掉 `.txt`）。
