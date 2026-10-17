# 戊⁶ 逐字留档（名单 + 两次运行）

## 一、648 条登记名单的来源
把 check_citations.js:35 改成：
  let __N=0;
  function check(name, cond, detail) { __N++; fs.appendFileSync(<abs>/names.tsv, __N+"\t"+String(current)+"\t"+name+"\t"+(!!cond?"T":"F")+"\n"); results.push({...}); }

基线运行：rc=1，输出 1176 行，末行 = ✗ 有 3 项失败，names.tsv 行数 = 648，其中 F = 3

## 二、(A) 全假运行的末行与计数
rc=1；输出 1826 行；grep -c ^FAIL = 648；末行 = ✗ 有 648 项失败

## 三、(B) 侧那个 ② 运行的 17 行逐字
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

## 四、唯一无保护的顶层解引用（第 45 处命中里唯一没有判空的那个）
2749:  specs.push({ what: '本页的主观测点 spec.js.ref', ref: S.ref, file: null });
