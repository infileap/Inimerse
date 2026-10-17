丁⁶ 原始输出留档（三条，逐字）
每一条都在 /home/sakiko/inimerse/.worktrees/ding6* 这棵临时 worktree 里跑出，
跑完立刻 git worktree remove --force，主工作树与我的工作树全程 status 为空。

=== 甲 · :335 那一节变红之后（变体 sha e566664d4069323d，输出 1194 行，rc=1）===
输出第 4 行，逐字：
FAIL  [覆盖：带身份标记的 docs 文件 vs 规范页清单] 带身份标记的 docs 文件都在手抄对照清单里   ← 不在清单里：docs/EIDOS_V06.md（那些标记在规范页上一条都不显示）

同一份输出第 3 行（另一节，顺带）：
FAIL  [覆盖：台账的出处都在手抄对照清单里] 台账引用的每个 docs 文件都在手抄对照清单里   ← 清单缺：docs/EIDOS_V06.md（那些裁定在规范页上一条都不会显示）

=== 乙 · :327 那一处（注释）在那棵红树上 ===
规范页元信息 data/spec.js 之外，:327 的文本「⑧ ⑦ 的反方向：带身份标记的 docs 文件 vs 规范页清单」
在输出里出现 0 次 —— 树是红的，它仍然一个字都不印。

=== 丙 · :254 那一节变红之后（变体 sha 0f75089d3da05823，输出只有 17 行，rc=1）===
逐字 17 行：
fatal: invalid object name 'undefined'.
/home/sakiko/inimerse/.worktrees/ding6c/website/scratch/check_citations.js:2749
  specs.push({ what: '本页的主观测点 spec.js.ref', ref: S.ref, file: null });
                                                   ^

TypeError: Cannot read properties of undefined (reading 'ref')
    at Object.<anonymous> (/home/sakiko/inimerse/.worktrees/ding6c/website/scratch/check_citations.js:2749:52)
    at Module._compile (node:internal/modules/cjs/loader:1705:14)
    at Object..js (node:internal/modules/cjs/loader:1838:31)
    at Module.load (node:internal/modules/cjs/loader:1441:32)
    at Module._load (node:internal/modules/cjs/loader:1279:12)
    at TracingChannel.traceSync (node:diagnostics_channel:328:14)
    at wrapModuleLoad (node:internal/modules/cjs/loader:237:24)
    at Function.executeUserEntryPoint [as node:internal/entry_main:36:49]
    at node:internal/main/run_main_module:36:49

Node.js v22.22.1
