# 甲″：一条真的「还不是裁定的意见」—— 找着了，而且它今天走的是一条**你看不见的路**
# 乙″：`website/scratch/**` 里只有你这棵树可核实的东西 —— B 类 4 处

**第五件：开工那一刻 `origin/main = 39fb2bff48a311eb7a98725154fc4d8003a57452`**（你已把 `ae41dad` 合了）。
**读法 = 工作树文件**（本工作树 `ae41dad`）；`decisions.js` / `spec.js` 用 `new Function('window', src)` 加载。

---

## 甲″ · 有。它叫 `docs/streams/i-json-user-statements.md` 的第 6 点

**逐字引用**（`docs/streams/i-json-user-statements.md:4` 那一行里）：

> 6) 未见明确撤回；但有一批悬置提问只问不求，未见用户给出要求或结论：
> U344 多细胞对象优化、U345 与面向目标编程的关联、U355 类Rust变量遮蔽、U357 多继承覆写、
> U363 并发、U364 直接内存访问、U368/U376 性能比得上吗、U369 优化上打败传统汇编、
> U372/U373 要例子、U374 手写汇编何时战胜编译器、U378 汇编语法再简化。

★ **提出者 = 用户本人**（那 12 个编号是这份文档给**用户的原话**编的号），
「**只问不求**」这四个字是这份文档作者对它们的判定 —— 它们是**提问，不是要求，也从来没被裁过**。
★ 这正是派件要的那个形状：**读者／设计者提出的、还没被裁的东西。**

### 它今天是怎么被处理的 —— 逐个数

| 问 | 读数 |
|---|---|
| `decisions.js` 里出现这 12 个编号的 | **0 / 12** |
| `decisions.js` 里**恰好以它们为 `id`** 的条目 | **0 / 58** |
| `decisions.js` 里 `source.file` 含 `streams/` 的条目 | **0 / 58** |
| `spec.js` 的 `docs[]` 里含 `streams/` 的文档 | **0 / 11** |
| `website/**` 里提到 `streams/` 的文件 | **1 个**（`website/scratch/check_citations.js`，而那是**我的守卫在数它**，不是页面在显示它） |

⇒ **它被处理成了「一份分析文档里的第六点」。** 不是「没被处理」，是：
**它被登记了，而没有被裁 —— 而登记与裁定在你自己的纪律里是两个动作。**
★★ 更要紧的那一半：**站点的读者（就是那个设计者本人）在规范页上看不到自己的问题已经被收下了** ——
`docs/streams/` 那 26 份**一份都不在**规范页的 11 份文档清单里。
⇒ 判据：**一个「收下了」如果只发生在读者看不到的地方，它在读者那一侧与「没被收下」同形。**

### 分母（我进去过的候选位置）

**6 个位置，其中 2 个不存在**：

| # | 位置 | 存在吗 | 我读到了吗 |
|---|---|---|---|
| 1 | `docs/*.md`（22 项，含两个子目录） | 是 | 是 |
| 2 | `docs/streams/`（**26 份**） | 是 | 是 —— **答案在这里** |
| 3 | `website/data/decisions.js`（58 条） | 是 | 是 |
| 4 | `website/data/spec.js` 的 `docs[]`（11 份） | 是 | 是 |
| 5 | `notes/` | **不存在** | — |
| 6 | `issues/` | **不存在** | — |

★ 我**没有**去看：`docs/archive/`、`docs/THREADS_V06.md`、以及仓库根下除 `docs/` 与 `website/` 外的目录。
**「没去看」与「看进去是零」分开写** —— 上面那张表里 5、6 两行是「不存在」，不是我找过。

### ★ 第二支：如果下一件这样的意见真的到达，**今天**哪条路会接住它

**今天已有的路径会接住它的第一部分，不会接住第二部分：**

1. **会接住**：它会被写进一份 `docs/streams/<slug>.md` —— 那条路径**今天在跑**（26 份就是证据），
   因为 `grep -rl "悬置提问只问不求" docs/` ⇒ 恰好命中一份，说明这条路径**真的被走到过**。
2. **不会接住**：它**不会**变成台账里的一条，因为台账的 `source.file` 今天只覆盖 **7 个** `docs/*.md`
   （`EIDOS_V06` / `PLAN_V06` / `PLAN_V07` / `TYPESET_V06` / `AUDIT` / `HANDOFF_INVERSE` / `STATUS`），
   **`streams/` 不在其中** —— 一条 `streams/` 里记下的意见，**没有任何一条既有路径把它送进台账**。

⇒ 判据：**问题不是「通道不存在」，是「`streams/` 这一段路今天走到一半就断了」——
断点可以逐字指出：`spec.js` 的 `docs[]` 那 11 项。**

---

## 乙″ · `website/scratch/**` 里只有我这棵树可核实的东西

探针：对 `website/scratch/` 下 **45 份受跟踪文件**逐行扫四类形状。

```
命中行 = 13 ；涉及文件 = 7 / 45
按类：{被忽略的构建产物 .build/ : 12, 临时 worktree 路径 : 1}
（/tmp/ 下的东西 = 0 行 ；绝对路径 /home/ = 1 行（就是那个 worktree）；时间戳/进程号 = 0 行）
```

★ **但 13 与「只有我这棵树可核实」不是同一个集合** —— 按**角色**分三档：

### A 类 · **声明位置**（不需要那个东西存在就能读）—— 5 处，无害
```
website/scratch/.gitignore:5            （就是那条忽略规则本身）
website/scratch/run_snippets.js:17      （注解说编译产物落在 .build/）
website/scratch/run_tutorial.js:184     （同上）
website/scratch/rerun_on_main.md:88     （四件套里的 pwd）
website/scratch/rerun_on_main.md:90     （同上）
```

### ★★ B 类 · **出处断言**（要那个东西存在才能核实）—— 4 处
```
website/scratch/check_citations.js:3645   ← 唯一一处【活的运行期代码】，每次跑都印
website/scratch/exposure_census.md:83     ← 「探针 = .build/probe_domains.js」
website/scratch/span_census.md:8          ← 「探针 = .build/probe_span.js」
website/scratch/span_census.md:73         ← 「用的是 .build/cc.bak 这份副本」
```

### C 类 · **关于这件事的讨论**（我自己写的报告，不算依赖）—— 4 行
```
website/scratch/rerun_on_main.md:71,74,76,77
```

### 你要的那个判断

**B 类 4 处里只有 1 处是活代码**（`check_citations.js:3645` 那条 `info()`）：它**每次跑都印**，
而它宣称的仪器**在一次 CI / 一个新 clone / 一个 `git worktree` 里都不存在**。
另外 3 处是 `.md` 报告里的出处行 —— 它们**不随树漂**（数字是当时量的、当时是对的），
但**出处不可核实**。

★★ **所以：「那些 scratch 产物该不该继续进主干」——**
`website/scratch/` 的**读数本身不依赖 `.build/`**：45 份受跟踪文件里只有 7 份提到它，
其中 4 份是「声明位置」或「关于这件事的讨论」。
**唯一一处需要处置的是 `check_citations.js:3645` 那句活着的 `info()`** ——
它要么把出处改成树上的东西，要么把「这个数由本块自己量」写成真的自己量。
★ 这是**回答**，不是动工：我**没有改它**（本件不是动工）。

---

## 四件套（本件没有在树上写任何东西）

```
pwd    = /home/sakiko/inimerse/.worktrees/website-ledger-readat
HEAD   = ae41dad9f00579bb09136ecac4b7a4a03e463488
读法   = 工作树文件（git worktree 上的 ae41dad）
第五件 = 开工那一刻 origin/main = 39fb2bff48a311eb7a98725154fc4d8003a57452
本件唯一写进树里的是这一份 .md（它自己进的是【受跟踪】那一档，不是第五档）
docs/ 一个字节未碰
```
