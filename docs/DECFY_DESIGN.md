# 底层去C化设计（DECFY_DESIGN）

> 工作订单：task-16。状态：**设计（未实现的部分仍未实现）**——但本设计交付后已被实现方当作规格引用并落地多节，见 **§0.5 落地现状**。本文档只做设计，不改任何引擎源码。
> 证据标注约定：**[读码]** = 逐行读过该文件该行；**[实测]** = 本会话实际执行命令所得；**[转述]** = 引用 `docs/AUDIT.md` 的实测数字，非本会话实测；**[待复核]** = 只见引用、未见原文。

## 0. 结论摘要

**「用 `.im` 重写」不是修法。** 本题的难点不是「哪个文件是 C」，而是同一条语言语义在仓库里有多个独立决定点，且这个分裂横切实现语言。

最硬的证据：`selfhost/compiler.im:254-266` **[读码]** 与 `selfhost/eval.im:114-123` **[读码]** 都是 `.im` 写的，却对 `and`/`or` 给出相反答案（前者值语义，后者布尔）。⇒ 「换成 `.im`」本身不消除分歧，只是改变分歧发生在哪两个文件之间。

真正的修法是让所有后端消费**同一个 IR（字节码）**，把语义从「每个消费者各自重判一次」收敛成**一张表**，使分歧在结构上不可表达。

---

## 0.5 落地现状（截至 HEAD `ec3008f`，2026-10 复核）

本节由 decfy-design 会话在 2026-10 复核 `docs/STATUS.md` 后补写。**原文（§1–§10）保留原样，作为当时的取证记录**；凡与本节现状表冲突处，**以现状表为准**（现状表锚定 commit 与 `docs/STATUS.md` 小节，可逐条复核）。

| 本文档的原论断 | 现状 | 证据 |
| --- | --- | --- |
| §1.1 `and`/`or` 六个决定点、2 值 : 4 布尔 | **已修**：统一为**布尔**语义（短路保留），判据换成三后端逐格比对 | `docs/STATUS.md:2464` §10.42；`src/compiler/compiler.c:648-672`、`selfhost/compiler.im:254-268`、wasm 改为复用 `cg_cond`；AOT 本就正确、未动 |
| §1.1 的语义方向：本文档主张**值语义胜出**（O0 选项①） | **已被用户裁定推翻**：`and`/`or` 返回**布尔** | `docs/STATUS.md:2526-2531`（该节明写「与本节冲突时以本节为准」）；用户裁定见 `docs/STATUS.md:2470` |
| §1.2 `%` 三决定点、32 位截断 | **已修**：`im_dbl_to_i64()` 饱和、零检查前移、`y == -1` 特判、AOT 新增 `nv_die_division_by_zero()`、wasm 改用 `e_trunc_sat_i64`；16/16 三后端一致 | `docs/STATUS.md:2464` §10.42 |
| §1.3 宽度契约分裂（`Value` 整数槽 32 位 vs AOT `NV` 64 位） | **已修，且正是按本文档 §2 的约束修的**：用**匿名 union** 把 `ival` 改 `long long` 而 `sizeof(Value)` 仍为 **32**，AOT 的 `NV` 完全未动 | `docs/STATUS.md:2910` §10.49（该节直接引用 `docs/DECFY_DESIGN.md:76` 当约束） |
| §1.4 「死指令」`OP_AND` / `OP_OR` | **已过期，需改写**：两者今天**都是活的**——§10.42 给它们定了含义（跳转路径上 `result` 已知为真（and）/假（or），故对两操作数求真值**恰好等于右操作数的真值**），短路路径改发 `OP_LOADK_BOOL`；发射点 `src/compiler/compiler.c:689` 与 `:859`。另新增 `OP_LOADK_I64`（追加在 `src/compiler/bytecode.h:58` 枚举**末尾**，旧编号无位移 ⇒ 本文档 `src/compiler/bytecode.h:12` 的引用**成立**，目录本来就是对的；`src/vm/bytecode.h` 不存在） | `docs/STATUS.md:2464` §10.42；实测 `grep -n "OP_AND\|OP_OR" src/compiler/compiler.c` → `:689`、`:859` |
| §3.3 第 2 条「真值产生点唯一」 | **已实现**：`vm_truthy()` 单入口，六处调用点全部改调用它，旧三目链 `grep -c` 归零 | `docs/STATUS.md:3110-3128`（该节直接引用本文档 `:125` 当规格、`:24` 当真值规则） |
| §3「IR 收敛」（后端改吃字节码） | **未做——是「未做的下一步」，不是「已被取代」**：`emit_from_bytecode` 在 `src/` **零命中**（本文档 §3.1.5 保留原草图、§3.1.3 给出修正签名）⇒ 后端今天仍各自遍历 AST，结构成因**一点没动**。**2026-10 实测把这件事量化了（§3.1.1）**：`src/compilation/aot_native.c` 与 `src/compilation/wasm_backend.c` 引用的 opcode 数是 **0 与 1**（后者唯一一个 `OP_LT` 还在注释里），而 VM 是 **69/69**。§10.53 的「真值产生点收敛为一处」是**往 §3 走的一步**（该节自己逐字引用本文档 §3.3 第 2 条当要求），按分类属「消除同一语义的两个决定点」；§3 属「让分歧在结构上不可表达」，**后者更大、没做完** | 实测 `grep -rn "emit_from_bytecode" src/` 零命中；`docs/STATUS.md:3095` §10.53；对照 `docs/STATUS.md:3171` 诚实边界①；opcode 覆盖数见 §3.1.1 |

**两条复核后新增、对本设计有利的证据**（原文没有）：

1. **本文档已被实现方当规格引用。** `docs/STATUS.md:3110` 引用 `docs/DECFY_DESIGN.md:125` 的「真值产生点唯一」作为该节要达成的判据；`docs/STATUS.md:3130` 引用 `:24` 的三目链尾句作为选定语义的理由；`docs/STATUS.md:2922` 引用 `:76` 的 32 字节冻结作为「选匿名 union 而非加宽字段」的理由。⇒ 设计文档写下的**约束是可被执行的**，不只是描述。**注意（2026-10 复核）**：上述三个行号是**引用当时**的行号，本文档此后多次增长，今天已漂移 ⇒ **对本文档的引用请引节号（§3.3、§1.3、§2），不要引行号**。这条是本文档给引用方的一条使用说明，也是本档自己犯过同一个错（原 §0.5 表引 `:111`、依赖段引 `:125`）之后的结论。
2. **§1.1 的论点被实测加强，而不是削弱。** `docs/STATUS.md:2531` 原文：「修 `and`/`or` 要**同时**动 C 编译器、`.im` 编译器、wasm 后端**三处**，而 `selfhost/eval.im`（`.im`·布尔）**不需要动**…**决定点的数量就是修复要碰的文件数**。」这正是 §1.1 的论点。

**仍未做**：§3 的 IR 收敛；以及 `docs/STATUS.md:2536-2537` 记的一条诚实边界（O2 的 BigInt / 小整数快路径未做）仍成立。§10.42 当时记的「`tools/im_diff_fuzz.py` 未接门禁」**已被 `docs/STATUS.md:2851` §10.48 解决**（差分模糊测试已进门禁，判据为 `0 DIVERGE / 0 THREW / 0 untranslated`，见 `docs/STATUS.md:3169` 的门禁逐阶段输出）。

### 0.5.1 当前阻碍（六类，截至 HEAD `ec3008f`）

按「卡住的程度」排序。**每条都带可复核的证据**，不是印象。

| # | 阻碍 | 证据 | 性质 |
| --- | --- | --- | --- |
| 1 | **IR 收敛一点没动**——五个消费者仍各自吃 `Expr*`、各自遍历 AST | 实测 `grep -rn "emit_from_bytecode" src/` **零命中**；**2026-10 量化（§3.1.1）**：`src/compilation/aot_native.c` 引用 opcode **0** 个、`src/compilation/wasm_backend.c` **1** 个（在注释里）、`selfhost/eval.im` **0** 个，而 `src/vm/vm.c` 是 **69/69**；两个后端连 `Bytecode` / `RegInstruction` 这两个类型名都不出现 | **结构成因**。`docs/STATUS.md:2531` 的「决定点数量＝修复要碰的文件数」因此仍然成立：这一轮修的 §10.42 / §10.49 / §10.53 **全是逐个决定点打补丁** |
| 2 | **「怎么证明搬对了」覆盖面不够**——三后端一致性只覆盖到整数与布尔 | `docs/STATUS.md:3171` 诚实边界①：两个编译后端 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` **都是 0** ⇒ 字符串/容器真值**根本走不到**。**2026-10 实测更正两处**：① wasm 的 `cg_cond` 对非 INT/BOOL/FLOAT 返回 `1` **不是缺陷**——`vm_truthy()`（`src/vm/vm.c:347`）的规则就是「零与 `nil` 为假，**其余（含空串与所有容器）为真**」，`cg_cond` 的 `else 1` 与之逐字对应（`src/compilation/wasm_backend.c:834-847`）；② AOT 的 `nv_tru` **补不了分支**，因为 `NV` 的三个 tag（`NV_INT 0`/`NV_FLT 1`/`NV_BOO 2`，`src/compilation/aot_native.c:175-177`，结构体在 `:174`）**全部只描述标量**、**根本没有 nil / 字符串 / 容器的表示** ⇒ 这一半其实是「值表示」问题。**2026-10 人类裁定**：AOT 半**不做**「扩 `NV`」，改走 §3 第 2 步（AOT 吃带 `Value` 语义的同一份 IR、链接 `Value` 运行时）⇒ **本条的 AOT 半并入第 1 条**（§6.1 末段） | **证明能力的缺口**；**2026-10 实测后两半都没有「可修的缺陷」**：wasm 半的语义已经是对的（18/18 无分歧）、缺的是**非标量本身**（= §2 第 ⑤ 类，另立 1.5c）；AOT 半已被裁定并入第 1 条 |
| 3 | 五类搬不动的 C（§2 表逐条给了理由） | ① `src/vm/vm.c`（~5000 行）substrate；② `src/platform/` + `src/runtime/runtime_posix.c` + `src/common/`（socket/线程/mutex/fork）；③ AOT 最后一跳（`src/compilation/aot_native.c:168` 的 `kPreamble` + 宿主 `cc`）；④ 引导链（`.im` 前端仍由 C VM 跑）；⑤ `Value` 32 字节布局 + 集合运行时（BigInt/窄化整数/位图，`docs/archive/ROADMAP_3.1.md`） | **边界**，非排期问题 |
| 4 | **根因仍在持续产出新缺陷** | `docs/STATUS.md` §10.50–§10.56 连着七节，几乎全是「同一类错误还有第二、第三个副本」：`sum()` 两个静默错误答案 → WIN32 副本 `len()`/`size()` 同样缺分量回退 → `str()` 把集合读成空集 → 16 条 `-Wformat-truncation` → `+` 里非字符串左操作数被静默丢掉 | **代价在复利**。`docs/STATUS.md:3236`：`L_ADD` 与 `L_CONCAT` 有**逐字相同的不对称**，2 项链走 `OP_ADD`、3 项链走 `OP_CONCAT`，**两条路径必须同规则**——今天只是「被改成一致」，不是「结构上不可能不一致」 |
| 5 | 三项待决 | ① `OP_AND`/`OP_OR` 去留（**已不是「删死指令」**，§10.42 给了活语义）；② `tools/aot_native.test.py:187` 的 `DIVERGENCE` 仍钉 **2 条**（`func_no_return`、`global_write_from_func`；原第三条 `lcg_float_promotion` 已升 `EQUIVALENCE`）；③ wasm 固定导入表是否补 ABI 版本号 | **已识别的决定**，只差裁决 |
| 6 | 双构建等价证明自身有缺口 | `tools/selfhost_compare.py` 有 skipped 项（工作订单转述 `10 target(s) byte-identical, 23 skipped`）；本文档未在本会话实测该数字 | **基础设施** |

**这六条的依赖关系（决定了先做哪一条）**：第 2 条是第 1 条的**前提**——不先把判据扩到非标量，§3 的第 2/3 步做完也只能证明整数与布尔，等于把「无法验证」原样带进新架构。用户已于 2026-10 选定 **先做第 2 条（§6.1）**；**但随后按 §6.1 的判据实测，第 2 条自身没有「可修的缺陷」**：wasm 半的语义已经与 `vm_truthy()` 一致（18/18、0 分歧）、非标量**根本没实现**（三个 `grep -c` 的 **0 是「特性不存在」，不是「存在但没接上」**），AOT 半已被裁定并入第 1 条。⇒ **第 2 条作为「扩覆盖」已核实关闭；它作为「实现非标量」是 §2 第 ⑤ 类的新工作量（§6.1 的 1.5c），不构成第 1 步的前提。** 于是**当前唯一的结构性下一步回到第 1 条（§3 的 IR 收敛）**。

---

## 1. 核心问题：同一语义的多个独立决定点

**这个病不是本文档发现的，也不是假设。** 仓库已经为**另一个**注册表（`builtin`）诊断过同一个病、修好过一次、并把修法写成门禁断言——注释原文是 `one name with two answers` / `dead code that reads as live` / `no single test file can see it`。§1 下面这些实例与它是同一个病；§3.1.8 记着那个先例的三件套。**读这一节时请把它当成同一个病的第二、三、四个实例，而不是新的病。**

### 1.1 `and` / `or`：六个决定点，横切 C 与 `.im` **[读码，全部逐行读过]**

| # | 位置 | 语言 | 语义 | 机制 |
|---|---|---|---|---|
| 1 | `src/compiler/compiler.c:648-670` | C | **值** | `OP_JUMP_IF_FALSE`（and）/ `OP_JUMP_IF_TRUE`（or）+ `OP_MOV result, right`；跳转目标回填 `comp->curBC->code[jmp_pos].r2 = end` |
| 2 | `selfhost/compiler.im:254-266` | `.im` | **值** | 与 #1 同形：`jpos = len(ctx["code"])`，`ctx["code"][jpos][2] = end` |
| 3 | **取证当时** `src/vm/vm.c:3166-3172`（`L_AND`）/ `:3173-3179`（`L_OR`）；**现状（2026-10 实测）** `:3486-3491` / `:3493-3498` | C | **布尔** | `value_set(&R[ins.r1], VAL_BOOL, (a && b) ? 1 : 0, …)`；**取证当时**的真值函数是三目链 `(va.type == VAL_BOOL) ? … : (va.type == VAL_NIL) ? 0 : 1`，**§10.53 已把六个产生点收敛为唯一一处 `vm_truthy()`（`src/vm/vm.c:347`）** |
| 4 | `src/compilation/aot_native.c:420-428`（`EXPR_BINARY` 在 `:418`，特判在 `:420`，发射在 `:425-428`；**取证当时记作 `:334-339`**） | C | **布尔** | `buf_str(b, "nv_boo(nv_tru(")` … `op == TOK_AND ? ") && nv_tru(" : ") || nv_tru("` … `buf_str(b, "))")` |
| 5 | `src/compilation/wasm_backend.c:769-776`（AND）/ `:778-786`（OR） | C | **布尔** | `cg_cond()` 递归：AND 为 `W_IF` 左→右→`W_ELSE`+`e_i32c(0)`→`W_END`；OR 为左→`W_IF`+`e_i32c(1)`→`W_ELSE`→右→`W_END` |
| 6 | `selfhost/eval.im:114-118`（and）/ `:119-123`（or） | `.im` | **布尔** | `if !truthy(l) { return false }` / `if truthy(l) { return true }`；成功路径 `return truthy(eval_expr(e["r"], env))` |

⇒ **2 值 : 4 布尔，横切两种语言。** 且**同语言内部同样分歧**：C 侧 `compiler.c`（值）对 `vm.c`/`aot_native.c`/`wasm_backend.c`（布尔）；`.im` 侧 `compiler.im`（值）对 `eval.im`（布尔）。

**本仓现有实测（[转述]，`docs/AUDIT.md` §1.6「三通道差分实测」）**：`and`/`or` 三通道 **8/8 分歧、三种行为**——解释器给操作数（`1 and 5` → `5`）、AOT 给布尔（→ `true`）、**wasm 在条件之外直接拒绝编译**（`error: wasm MVP subset: 'and'/'or' outside a condition is not supported (use it in if/while) (line 1)`）。⇒ 上表的「值 / 布尔」二分之外还有第三种行为：**拒答**（wasm 的 `cg_cond` 只在条件位置工作）。

**注释不可作为语义证据。** `src/compilation/aot_native.c:420-428` 的注释原文声称 "C's && and || short-circuit exactly as the interpreter's do" **[读码]** —— 该注释与解释器行为**相反**，是错的。

**同一形状的第二例（2026-10 实测）**：`src/compilation/wasm_backend.c:4` 的文件头注释说每个变量/临时量是「8-byte linear-memory slot `[tag: i32][payload: i64]`」，而**同文件 `:66`** 是 `#define SLOT_BYTES 16`（自带注释 `/* [tag i32 @+0][pad][i64 payload @+8] */`）⇒ **头注释与定义互相矛盾**，真实的槽是 **16 字节**（头注释陈旧）。⇒ 引用行号时**必须引定义处**，不能引文件头。

### 1.2 `%`：三个决定点

| 位置 | 语言 | 语义 | 判定 |
|---|---|---|---|
| `src/vm/vm.c:3824-3838`（`L_MOD`） | C | 两条路径：`:3827` int 快路径 `(int)da % bi`；`:3830-3837` 一般路径把 double 转 int 后再取模，**零检查在截断之后** | **坏**：只有 32 位可用；越界时 `cvttsd2si` 给 `INT_MIN`（实测 `3000000000 % 7` → `-2`，见 `docs/AUDIT.md` §1.6） |
| `src/compilation/aot_native.c:208-211` | C | `long long x = nv_asi(a), y = nv_asi(b); return nv_int(y ? x % y : 0);` | **对**：64 位 |
| `src/compilation/wasm_backend.c:938` / `:960` | C | `W_I64_REM_S` | **对**：64 位（int%int 路径 `:926-939`；general 路径 `:940-961` 反而先 `e_trunc_sat_i32` 再 `W_I64_EXTEND_I32_S`） |

**文件头注释自相矛盾 [读码]**：`src/compilation/wasm_backend.c:9` 写 `L_MOD: int%int, else (int)as_double(a) % (int)as_double(b)`——描述的是**坏的那个形状**，而它自己的代码在 `:938` 用的是 64 位 `W_I64_REM_S`；同文件 `:6` 还声称 "mirror the C VM exactly"。

### 1.3 比 `%` 更广的根因：宽度契约分裂 **[读码]**

- `src/vm/vm.h:40-45`：**取证当时**的形状是 `typedef struct { int type; int ival; double fval; char *sval; void *ptr; } Value;` ⇒ LP64 下 **32 字节**，整数载荷 **32 位 `int`**。**现状（2026-10 实测）**：整数槽已改为**匿名 union** —— `int type;` / `union { long long ival; double fval; };`（原文注释 `/* anonymous union (C11) */`）/ `char *sval;` / `void *ptr;` ⇒ **`sizeof` 仍 32，整数载荷已 64 位**（§10.49 正是按本档 §2 的「32 字节冻结」约束修的，见 §0.5）。
- `src/compilation/aot_native.c:174`（`kPreamble` 起于 `:168`；`NV` 在 `:174`，三个 tag 在 `:175-177`）：`typedef struct { int t; long long i; double f; } NV;` ⇒ AOT 整数 **64 位 `long long`**（`nv_asi` 在 `:187` 返回 `long long`）。

⇒ **VM 32 位 vs AOT 64 位是系统性分裂，`%` 只是它的一个可观测面。** `AUDIT.md` §1.2 的 int32→double 提升同源。

### 1.4 「死指令」`OP_AND` / `OP_OR`：**曾经**是死指令 **[读码；§0.5 复核后本节结论已改写]**

- 声明：`src/compiler/bytecode.h:12` `OP_AND, OP_OR,`。（目录是 `src/compiler/`；`src/vm/bytecode.h` **不存在**，`grep` 会直接报 `No such file or directory`。）
- **当时（本会话取证时，`8c9f0f3` 附近）的实测**：`OP_OR` 全仓引用**仅三处**——`src/compiler/bytecode.h:12`、`src/vm/vm.c:2826 case OP_OR: goto L_OR;`、`src/vm/vm.c:4873` 反汇编器（**均为当时的行号；现状是 `:3066-3067` 与 `:5211-5212`**） ⇒ `src/` 下**零 emit**，是死指令；`OP_AND` 的唯一 emit 点是 `src/compiler/compiler.c:827`，位于 `EXPR_CHAIN_COMPARE` 分支（`:815-835`，即链式比较 `1 < x < 10`，那里**需要布尔**）。
- **现状（`ec3008f` 实测）**：两者**都是活的**。§10.42 把 `and`/`or` 的**短路路径**改发 `OP_LOADK_BOOL`，而**跳转路径上 `result` 已知为真（and）/假（or），故对两操作数求真值恰好等于右操作数的真值**——这给了 `OP_AND`/`OP_OR` 一条有定义的活含义。发射点变成**两处**：`src/compiler/compiler.c:689` `emit(comp->curBC, is_and ? OP_AND : OP_OR, result, result, right);` 与 `:859` `else emit(comp->curBC, OP_AND, result, result, cmp);`。

⇒ **结论（仍成立，且更强）：`OP_AND`/`OP_OR` 作为内部布尔原语保留；而 `and`/`or` 运算符本身降级为「短路跳转 + 真值常量」。** 这两件事是两个决定，不要合并——§10.42 正是分开做的。**订正**：本节原标题写「死指令」、结论写「只有 `OP_OR` 可删」，两句都**已过期**：`OP_OR` 今天由 `src/compiler/compiler.c:689` 发射，不是死指令。

---

## 2. (a) 层次划分表

判定取值：**可搬** / **暂不可搬** / **永久不可搬**。每条给出理由，不写裸结论。

| 层 | 位置 | 判定 | 理由 |
|---|---|---|---|
| 词法 / 语法 | `selfhost/lexer.im`、`selfhost/parser.im` | **可搬（已搬）** | 已是 `.im`；`selfhost/` 已有 **48 个 `.im`、2,338 行**（2026-10 实测：`ls selfhost/**/*.im | wc -l` = 48；合计 2,338 行；工作订单记的 2,316 行是当时的数），前端已搬一半。 |
| AST → 字节码 | `src/compiler/compiler.c` / `selfhost/compiler.im` | **可搬** | 两个实现都已存在，且**语义已一致（均为值语义）**。要搬的是「决定保留哪一个」，不是「再写一遍」。 |
| AST → 值（求值器） | `selfhost/eval.im` | **可搬，但建议废止** | 它是**第三个**独立语义决定点，且与 `selfhost/compiler.im` 相反（`:114-123` 布尔 vs `:254-266` 值）。留着它 = 留着分歧。 |
| AST → C / AST → wasm | `src/compilation/aot_native.c`（`EXPR_BINARY` 在 `:418-430`）、`src/compilation/wasm_backend.c`（`cg_cond` 在 `:778-847`） | **可搬（改为吃字节码）** | 二者都是 `Expr*` 进、各自重判语义，是分歧的**结构成因**。 |
| VM 解释器本体 | `src/vm/vm.c`（**5,346 行**，2026-10 实测；工作订单记的 5,007 行是当时的数） | **暂不可搬** | substrate：`.im` 的一切执行都由它提供。**最强理由：它同时是唯一的引导运行器**（见第四类）——搬它需要已有东西能跑它，构成循环依赖。 |
| 平台 / OS 层 | `src/platform/`、`src/runtime/runtime_posix.c`、`src/common/` | **暂不可搬** | socket / 线程 / mutex / fork 是 `.im` 无权直接表达的宿主能力；搬 = 先造 FFI，而 FFI 本身又要 substrate 托底。 |
| AOT 最后一跳 | `src/compilation/aot_native.c:168`（`kPreamble`）、宿主 `cc`、`NV`/`nv_*` 前导 | **暂不可搬** | 它产出的是**宿主 C 源码**并交给 `cc` 编译、链接出 `main`；`.im` 既不能代替 `cc`，也不能在没有 C 运行时的前提下提供进程入口。 |
| 引导（bootstrap）链 | C VM ← `.im` 编译器 | **暂不可搬** | 没有 C VM 就没有运行 `.im` 的东西。**这是唯一一条无法靠「重写」消除的依赖**，它决定了去C化有理论上界。 |
| 值表示 + 集合运行时 | `src/vm/vm.h:40-45`（`Value`，32 字节）、数组/字典/集合、以及集合化所需的 BigInt / 窄化整数 / 位图 | **永久不可搬（宽度契约部分）**——**§0.5 复核：该约束已被遵守**（整数槽改 64 位而 `sizeof` 仍 32，AOT 的 `NV` 未动） | `Value` 是 VM 寄存器、AOT 生成的 C、wasm 线性内存槽、固定导入表**共同的形状**；`docs/archive/ROADMAP_3.1.md:23-25` 明确要引入 BigInt 与 u8/i16 窄化，并规定「动态容器与跨模块边界保留 boxed `Value`」；`:28` 要求 BigInt / 窄化整数 / 枚举值在模块 ABI 与 `.vverse` 序列化中**可逆映射**。宽度契约一旦可被 `.im` 单方面修改，ABI 与序列化可逆性即失效。**可搬的是其上的策略，不是它本身。** |

**一处必须点名的误判**：把 `src/vm/vm.c:3486-3497` 的布尔语义归入「永久不可搬」是**错的**。那段是**政策**（policy），不是 substrate **能力**（capability）。政策必须上移到语义表，否则去C化会把缺陷一起固化。

---

## 3. (b) IR 收敛方案

### 3.1 接口：IR 已经存在，缺的是「谁消费它」

**本节的草图已被 2026-10 实测修正过两次**（第一次：签名不够；第二次：见 §3.1.4「吃 IR 不等于分歧消失」）。原草图保留在 §3.1.5，因为它记录的是当时的推理。

#### 3.1.1 现状是可复现的数字，不是印象

「五个消费者都吃 `Expr*`」这句今天可以量。**每个消费者引用了几个 opcode**：

| 消费者 | 语言 | 进 | 出 | 引用的 opcode（distinct / 总 token） |
| --- | --- | --- | --- | --- |
| `src/vm/vm.c` | C | 字节码 | 值 | **69 / 69**（`case OP_` 覆盖全部 69 个） |
| `selfhost/compiler.im` | `.im` | AST | 字节码 | **48 / 164** |
| `selfhost/eval.im` | `.im` | AST | 值 | **0 / 0** |
| `src/compilation/aot_native.c` | C | AST | C | **0 / 0** |
| `src/compilation/wasm_backend.c` | C | AST | wasm | **1 / 1** —— 且**在注释里**（`:1500` 的 `(OP_LT double compare)`） |

命令（可复现，2026-10 实测）：

```
awk '/^typedef enum \{/{f=1;next} /^\} OpCode;/{f=0} f' src/compiler/bytecode.h \
  | sed 's,/\*.*\*/,,' | grep -oE '\bOP_[A-Z0-9_]+' | sort -u | wc -l   # → 69
grep -oE 'case OP_[A-Z0-9_]+' src/vm/vm.c | sort -u | wc -l              # → 69（双向一一对应，两个 comm 都是空集）
grep -oE '\bOP_[A-Z0-9_]+' selfhost/compiler.im | wc -l                  # → 164（distinct 48）
grep -c 'Bytecode'        src/compilation/aot_native.c                   # → 0
grep -c 'RegInstruction'  src/compilation/wasm_backend.c                 # → 0
```

⇒ **两个编译后端里没有一个 opcode 出现在代码中，也完全不提 `Bytecode` / `RegInstruction` 这两个类型名。** 「后端不吃 IR」从读码印象变成了数字。

#### 3.1.2 IR 不是一个要新建的东西，它今天已经有版本号和序列化格式

`src/compiler/bytecode.h`：

- `:69-74` `RegInstruction { OpCode op; int r1, r2, r3; }` —— **固定宽度**，这正是 §3.4 那张表要挂的键。
- `:87-128` `Bytecode`：`code/count/capacity` + `string_pool` + `float_pool` + `funcs[1024]` + `threads[32]` + `try_entries` + `labels` + `global_names` + `capture_names` + `dbg_lines`。
- `:64-66` `INIM_BYTECODE_MAGIC "INIMBC"` / `INIM_BYTECODE_VERSION 3` / `INIM_ABI_VERSION 2`。

⇒ **`.inim` 就是这份 IR 的序列化形式**（读者：`src/compiler/bytecode.c`、`src/main.c`、`src/compilation/deps.h`）。而 `docs/archive/RELEASE_0.5.0.md:48` 已经承诺过「三个后端共享同一字节码格式」——**§3 是把这句承诺兑现，不是发明新架构**。

#### 3.1.3 目标接口（修正后的签名）

原草图的 `void emit_from_bytecode(Ctx *c, const Bytecode *bc);` **不够**：`Bytecode` 不是一个 `code` 数组，它带着常量池、函数表、线程表、try 表、全局名与捕获名。后端要的是**整份 `Bytecode`** 加**一张表**：

```c
/* 后端消费整份 IR（不是一段回调），并查同一张表 */
void backend_emit_unit(Ctx *c, const Bytecode *bc);

/* 唯一真值源：一行 = 一个 opcode 在三个后端上的行为 */
typedef struct {
    OpCode op;
    const char *name;
    const char *producers;      /* 谁能发射它：见下，这是全表唯一能用 grep 客观判定的列 */
    int  arity;                 /* 用到的操作数个数（1..3） */
    int  touches_state;         /* 是否写当前帧之外的状态（§3.1.7 的 P/C/S，D14 的同一判据） */
    int  can_raise;             /* 独立第二轴：块内是否调 vm_throw（§3.1.7 实测 16 个） */
    const char *vm_semantics;   /* 唯一语义来源：src/vm/vm.c 的对应 L_X: 块 */
    const char *aot_emit;       /* 派生物，不是手写第二份 */
    const char *wasm_emit;      /* 同上 */
} OpSemantics;
```

**两列都是必需的，不是冗余**：§3.1.7 实测出 `touches_state` 与 `can_raise` **互相独立**，交叠处有 11 个「纯值但会抛」的 opcode（`+` 与全部关系运算）——只带一列会把它们映射错。

**`producers` 列（由 `exact-lumen` 提出、我已实测复核，见 §3.1.9）是全表唯一**能用 `grep` 客观判定**的列**。其余各列——`touches_state`、`can_raise`、`vm_semantics`、两个 `*_emit`——**都要读实现才能填**。一张每一格都靠读实现来填的表**它自己就没有判据**；`producers` 这一列给了它一个能自动复核的锚点，而且它立刻抓出两类**不该出现在表里**的行（§3.1.9）。

**这张表的每一行都必须能对着 `src/vm/vm.c` 的那一个指令体读出来**——注意**不是** `case` 块：VM 的分派是 goto-threaded（§3.1.6），69 个 `case OP_X: goto L_X;` 只是跳板，**真正的语义体是 `L_X:` 标签块**（连续覆盖 `src/vm/vm.c:3146-4680`）。因为 VM 是 **69/69** 的唯一完整实现（§3.1.1），它是**唯一语义来源**，`aot_emit` / `wasm_emit` 两列是**派生物**。这与 §6.1 的做法同构（`vm_truthy()` 唯一来源、其余调用它）。**§3.1.7 已经把 69 行逐块读了一遍。**

#### 3.1.4 为什么「后端吃 IR」不等于「分歧消失」（本节最重要的一句）

**IR 里没有类型。** `RegInstruction` 的 `r1/r2/r3` **只是寄存器下标**，不带类型；VM 的寄存器堆是**动态类型**的——`src/vm/vm.c:2912` 是 `Value *R = t->R;`，每条指令在运行期看 `R[ins.r2].type` 才决定行为。

⇒ 后端要吃这份 IR，就必须**自己实现一份同样的动态分派**（这正是裁定 A/A′ 说的「链接 `Value` 运行时 / 把 IR 的值语义映射到 wasm」）。因此：

- **§3 的真实工作量 = 69 个 opcode × 「动态分派怎么落到目标」**，不是「换一个遍历入口」；
- 但它仍然**值得做**，而且理由比原文更强：分歧从「**结构上可发生**」变成「**每个 opcode 一处、可被 §3.4 的表逐格钉住**」。今天 `and`/`or` 有 6 个决定点，是因为两个后端各自重新遍历 AST、各自重新决定；吃 IR 之后 `OP_AND` 只有一个实现、一行表。
- **代价要说清**：这不是小改动。**69 行表 + 两套动态分派**，而 §3.1.1 已证明两个后端今天在这件事上是**零起点**（0 个 opcode 引用）。

#### 3.1.5 原文草图（保留，记录当时的推理）

```c
/* 形状取自 src/vm/vm.c 的 ins.r1 / ins.r2 / ins.r3 与 src/compiler/compiler.c 的 code[jmp_pos].r2 回填 */
typedef struct { OpCode op; int r1, r2, r3; } Instr;

typedef struct { Instr *code; int count; int ntemps; /* …常量池… */ } Bytecode;

void emit_expr(Ctx *c, const Expr *e);               /* 现状：每个后端各写一份 */
void emit_from_bytecode(Ctx *c, const Bytecode *bc); /* 目标：共用唯一入口 */
```

原文的结论「后端只剩 `opcode → 目标指令` 的映射表，**分歧在结构上不可能发生**」——**后半句已被 §3.1.4 修正为「分歧在每个 opcode 上各发生一次，且可被表逐格钉住」**。原文里 `and`/`or` 的「值语义 + `OP_MOV`」半句的语义方向已由用户裁定改为布尔（见 §0.5），`OP_MOV` 那半句仍成立。

### 3.1.6 第四个消费者：**反汇编器只命名 48/69 个 opcode**（2026-10 实测）

§3.1.1 列了五个消费者，本轮实测发现**还有第六个**——它不在 AST 侧，而在**证明侧**：

| 事实 | 命令 / 位置 | 结果 |
| --- | --- | --- |
| VM 的**分派**是 goto-threaded 且**完全双射** | `grep -cE '^ *L_[A-Z0-9_]+:' src/vm/vm.c` + 抽 `case OP_X: goto L_X;` | **69 个 case ↔ 69 个 label，两个方向的差集都是空集** ⇒ **没有死 opcode、没有孤儿 label**（这修正了 §1.4 取证时「`OP_AND`/`OP_OR` 零 emit」给人的「死指令」印象：它们在**分派**侧一直是活的，缺的只是**发射**侧） |
| 分派 switch 的 `default` **存在且有定义** | `src/vm/vm.c:3066-3143`（69 个 `case OP_X: goto L_X;` 在 `:3068-3138`，`default:` 在 `:3139-3143`） | 坏 opcode → `fprintf(stderr, "[vm] bad opcode %d at ip %d\n", …)` + `t->running = false` |
| **反汇编器只覆盖 48 个** | `src/vm/vm.c:5205-5267` 的 `vm_disasm_ins`，switch 在 `:5208`、**48 个 `case OP_`**、`default:` 在 **`:5265`** | 缺的 **21 个**逐字是：`OP_BE`、`OP_CALL_VALUE`、`OP_DECLARE`、`OP_IN`、`OP_LOAD_CAPTURE`、`OP_LOCK`、`OP_MAKE_FUNC`、`OP_MAX`、`OP_MIN`、`OP_NEW_SET`、`OP_RECORD`、`OP_RECV`、`OP_SEND`、`OP_SET_ADD`、`OP_SET_INTERVAL`、`OP_STORE_CAPTURE`、`OP_THREAD_GOTO`、`OP_THREAD_JOIN`、`OP_THREAD_STATE`、`OP_THREAD_WAIT`、`OP_YIELD` |
| 它**降级而不崩** | `:5265` `default: snprintf(out, outsz, "OP_%d", (int)ins->op); return;` | ⇒ **不是缺陷**（无未初始化输出），但 21/69 在人类可读的 dump 里只剩数字 |
| 它**面向 `.im`** | `src/vm/vm.c:5316` `builtin_dbg_disasm` 调它（`:5329`），`:5365` 注册为 builtin `dbg_disasm` | 所以这是一条**用户可见**的路径，不是纯内部工具 |
| **但验证要用的那条路不依赖它** | `src/main.c:675-681`：`printf("%d,%d,%d,%d\n", (int)bc->code[i].op, …)`，注释在 `:675` 写「Kept byte-comparable on purpose」 | ⇒ **CLI `bytecode` 子命令输出的是纯数字，故意不经过反汇编器** ⇒ 这条 dump **天然是稳定判据**，不受 48/69 影响 |

**为什么这条要写进设计文档**：§3 的验收（§6 第 2/3 步的 ①–⑥）要靠 dump 来证明「两个后端产出的 IR 等价」。好消息是**判据那条路（`src/main.c:675-681`）是数字的、故意的、稳定的**；坏消息是**缺的 21 个恰好集中在非标量那一侧**（集合 `OP_NEW_SET`/`OP_SET_ADD`/`OP_SET_INTERVAL`/`OP_IN`、区间约束 `OP_BE`、闭包与捕获 `OP_MAKE_FUNC`/`OP_LOAD_CAPTURE`/`OP_STORE_CAPTURE`/`OP_CALL_VALUE`、线程 `OP_THREAD_*`/`OP_LOCK`/`OP_SEND`/`OP_RECV`/`OP_YIELD`、`OP_DECLARE`/`OP_RECORD`）——**正是 §3.1.4 与 1.5c 最难的那部分**。⇒ 补这 21 个是**便宜且独立**的，应当在第 2 步**之前**做掉，否则调试最难的一半时，dump 里只有数字。已列入 §9 待批准变更第 12 条。

### 3.1.7 `OpSemantics` 表的 **69 行骨架**（从 VM 的 `L_X:` 标签块逐块读出，2026-10 实测）

§3.1.3 说表的每一行必须能对着 `src/vm/vm.c` 的那一个块读出来。本轮**真的把它读了一遍**，这就是那 69 行的第一列可填内容。

**方法与范围**：69 个指令体是 `L_X:` 标签块，**连续覆盖 `src/vm/vm.c:3146-4680`**（抽取：`^ *(L_[A-Z0-9_]+):` 匹配 69 个，第 k 块的区间 = 第 k+1 个标签行 − 1）。**每一格都能用 `sed -n '<区间>p' src/vm/vm.c` 复核。**

**分类规则（写明，便于反驳）**：按「该 opcode 是否**写**当前帧寄存器文件 `R[]` **之外**的状态」分三类——
- **`P` 纯值**（只读操作数、只写 `R[r1]`，可能新建对象）：**37 个** —— `MOV`、`LOADK_INT`、`LOADK_I64`、`LOADK_FLOAT`、`LOADK_STRING`、`LOADK_BOOL`、`ADD`、`CONCAT`、`SUB`、`MUL`、`DIV`、`NEG`、`EQ`、`NEQ`、`LT`、`GT`、`LE`、`GE`、`AND`、`OR`、`NOT`、`NEW_ARRAY`、`NEW_DICT`、`INDEX_GET`、`PUSH_REG`、`POP_REG`、`LOAD_CAPTURE`、`STORE_CAPTURE`、`IS_NIL`、`EQK`、`NEQK`、`MOD`、`NEW_SET`、`SET_INTERVAL`、`IN`、`MIN`、`MAX`
- **`C` 仅控制流**（只改 `t->ip`）：**3 个** —— `JUMP`（`:3703-3705`）、`JUMP_IF_FALSE`（`:3706-3710`）、`JUMP_IF_TRUE`（`:3711-3717`）
- **`S` 触碰帧外状态**：**29 个** —— `INDEX_SET`、`LOAD_GLOBAL`、`STORE_GLOBAL`、`CALL_BUILTIN`、`SAY`、`WAIT`、`STOP`、`HALT`、`YIELD`、`MAKE_FUNC`、`CALL_VALUE`、`CALL_FUNC`、`RETURN`、`DECLARE`、`RECORD`、`BE`、`TRY_START`、`TRY_END`、`THROW`、`SET_ADD`、`THREAD_START`、`THREAD_CTRL`、`THREAD_GOTO`、`THREAD_JOIN`、`THREAD_WAIT`、`THREAD_STATE`、`LOCK`、`SEND`、`RECV`

**37 + 3 + 29 = 69**（与 §3.1.1 的数一致，可复核）。

**独立第二轴 `can_raise`：16 个**（块内出现 `vm_throw`，helper 在 `src/vm/vm.c:2481`）—— `ADD`、`CONCAT`、`SUB`、`MUL`、`DIV`、`NEG`、`LT`、`GT`、`LE`、`GE`、`INDEX_SET`、`STORE_GLOBAL`、`CALL_BUILTIN`、`MOD`、`BE`、`THROW`。

**⇒ 本轮最重要的结论：两个轴是独立的，`touches_state` 一个布尔不够。** 交叠处有一格是空的：
| | `can_raise = 1` | `can_raise = 0` |
| --- | --- | --- |
| `P`（不写帧外状态） | **11 个**：`ADD` `CONCAT` `SUB` `MUL` `DIV` `NEG` `LT` `GT` `LE` `GE` `MOD` | 26 个 |
| `C`（仅控制流） | 0 | 3 个 |
| `S`（写帧外状态） | **5 个**：`INDEX_SET` `STORE_GLOBAL` `CALL_BUILTIN` `BE` `THROW` | 24 个 |

`docs/SYNTAX.md` §7.1 的 **D14** 是二分的（「操作共享状态 ⇒ 抛 `type_mismatch`；只产出值 ⇒ 按已裁定定义值作答」），**对那 11 个「纯值但会抛」没有分支**。这**未必**是冲突（D14 讲的是**内建**，这 11 个是**运算符**），但表里**必须两列都带**：只带 `touches_state` 的后端作者会把 `+` 和 `lt` 映射错。已列入 §9 待批准变更第 13 条（**只登记，不改 D14**）。

**12 行的机制与名字不符**（这几行是表的 `note` 列必须写的）：

| opcode | VM 块 | 机制（逐字读出的） |
| --- | --- | --- |
| `OP_DECLARE` | `src/vm/vm.c:4101-4111` | **不是「声明」，是写 VM 资源限额**：`vm->limit_mem` / `limit_threads` / `limit_time` / `limit_inst` / `limit_vram`（按 `ins.r1` 的 kind 0–4 分派） |
| `OP_RECORD` | `src/vm/vm.c:4112-4160` | 取 `VM_LOCK(vm)`、`realloc` `vm->record_meta` 与 `record_names`、写 `vm->record_default_store`（`gidx == -1` 分支在 `:4115`） |
| `OP_BE` | `src/vm/vm.c:4262-4308` | 取全局分片锁 `im_mutex_lock((ImMutex*)VM_GSHARD(vm, g))` + `vm_global_grow(vm, g)` |
| `OP_LOAD_GLOBAL` | `src/vm/vm.c:3646-3661` | **条件加锁**（`int need_lock = (vm->active_threads > 1);`）+ 字符串 intern 改写 `sval`/`ival` |
| `OP_INDEX_GET` | `src/vm/vm.c:3585-3626` | dict 读取 `VM_LOCK(vm)`；**array 越界读保持 nil**（注释逐字 `out-of-range read stays nil (compat)`） |
| `OP_INDEX_SET` | `src/vm/vm.c:3627-3645` | 写**已有**容器（`array_set` / `dict_set`）+ `vm_throw` |
| `OP_SET_ADD` | `src/vm/vm.c:4342-4347` | 写**已有**集合（`R[ins.r1]` 经 `vm_set_add`） |
| `OP_NEW_SET` / `OP_SET_INTERVAL` | `:4195-4223` / `:4224-4245` | 两者都是 `int sidx = vm_set_new(vm);` **新建**集合；块内出现的 `set_add` 是**构造**、不是共享写 ⇒ **不归 `S`** |
| `OP_MAKE_FUNC` | `src/vm/vm.c:3956-3971` | 只在分配失败时置 `t->running = false; vm->last_error = 1`（`:3958`、`:3962`、`:3968`） |
| `OP_TRY_END` | `src/vm/vm.c:4333-4336` | 只有 `if (t->exc_depth > 0) t->exc_depth--;`（线程内） |
| `OP_SAY` | `src/vm/vm.c:3833-3844` | **绕过平台层**：直接 `GetStdHandle` / `WriteFile`，靠 `src/vm/vm.c:160-161` 的 POSIX shim（`static inline HANDLE GetStdHandle(int x)`、`static inline int WriteFile(...)`）；`_WIN32` 分支在 `:29-31`，另一分支 `:154` `#include <unistd.h>` ⇒ **§2 说平台层「搬不动」时，边界本身已经是漏的** |
| `OP_WAIT` | `src/vm/vm.c:3845-3931` | 交给调度器（`:3851` 起 `if (t->is_task && ms > 0)` 分支 `SwitchToFiber` 系） |

**诚实边界**：① 分类是**按上面写明的规则手工判定**的，不是自动推导；② `P` 里的 `INDEX_GET` 读的是**已有容器**（别的线程可能正在写），我按 D14 的「**写**」口径归 `P` —— **这是口径选择，不是事实**；③ 只读了 `src/vm/vm.c` 一个文件：AOT 与 wasm 今天对 69 个里的 **0 与 1** 个有实现（§3.1.1）⇒ 表的「三后端行为」那几列**今天无从填**，这就是 §6 第 2/3 步 ① 判据要盯的东西。

### 3.1.8 这一节不是新纪律：仓库里已有一个**跑通了的先例**（builtin 注册表）

上面那些判据（表行数 == 枚举成员数、双向差集为空、故意改错必须变红）读起来像在**发明**一套新纪律。
不是。这个仓库**已经**对另一个注册表做过同一件事，并且三件套齐全。**§3 要做的是把它推广，不是发明它。**

**先例：`builtin` 注册表的「一名一实现」。**

1. **机制自己拒绝**第二个决定点——不是在文档里约定，而是在创建点拒绝并报出名字。
   `src/vm/vm.c:1676-1686`（孪生体在 `:1700-1710`，两个注册入口各一份），注释逐字：
   > `One name, one handler.  builtin_lookup returns the FIRST slot on the probe chain whose name matches, so a second registration of the same name is unreachable code that reads as a live one -- and builtin_insert would give it a slot of its own, leaving two entries for one name with only one of them reachable.  Refuse the duplicate and name it, instead of losing it silently.  This changes no dispatch: the first registration already won.`

   实现是 `if (builtin_lookup(vm, name) >= 0) { fprintf(stderr, "[vm] builtin '%s' is already registered; the first one stays\n", name); return; }`（`:1683-1686`）。

2. **门禁从任何单个文件之外断言这条性质**——`tools/gate.sh:140-151`，在 **ctest 阶段内部**（因为它是「刚跑完那些套件」的性质，不是某个文件的性质）：把套件输出里的 `is already registered` 捞出来就**让阶段失败**。注释逐字：
   > `A builtin name registered twice is dead code that reads as live: the name resolves to whichever handler landed first on the probe chain, and the second entry is unreachable.  vm_register_builtin now refuses the duplicate and says so on stderr, so a suite that prints this line is a suite whose engine carries one name with two answers.  Asserted here because it is a property of the suites this stage just ran, and because no single test file can see it.`

3. **计数也一起断言**，免得「悄悄少了一条」通过——`tools/gate.sh:132-139`：`EXP_CTEST`（`:54`，当前 **134**）必须与 ctest 报的 `0 tests failed out of N` 相等，注释逐字 `Assert the count too, so that a dropped add_test( ) cannot pass silently.`

**这段注释就是本文档 §1 的论点，由仓库自己用英文写下的版本**：「one name with two answers」「dead code that reads as live」「no single test file can see it」。
§1 的六条 `and`/`or`、三个 `%`、六个真值点，与它是**同一个病**；§3.1 的 `OpSemantics` 表判据与它是**同一个处方**。⇒ §3 的可行性论据不是「应该能行」，而是「**这里已经行过一次**」。

**当前状态：我按 CMake 的真实源列表逐平台重算了这个注册表（2026-10 实测）。**

| 平台 | 源文件 | 注册次数 | distinct 名字 | 重名 |
|---|---|---|---|---|
| WIN32 | 27 base + 26 | **431** | **430** | **1** |
| POSIX | 27 base + 27 | **241** | **240** | **1** |

那唯一一个重名是 `isolate_run`（`src/isolate_mod.c:227` 与 `:318`），且是**假阳性**：两处分别在 `#ifdef _WIN32` 与 `#else` 分支里（已读预处理上下文确认），只有一个会被编译。
⇒ **今天两个平台都是重名自由的**，`gui_fullscreen` 已不再出现（`src/mod/gui_mod.c:3690` 是唯一注册点，`:3691-3692` 留着说明它曾经被注册两次的注释）⇒ §1.53 登记的那个实例**已修**，我的扫描与之一致。

**诚实边界（这条很重要，因为两个独立计数不一致）**：我的正则是 `vm_register_builtin(_full)?\s*\(\s*\w+\s*,\s*"([^"]+)"`，只匹配**字面量名字 + 简单首参**；用宏或变量传名字的会漏掉。`exact-lumen` 独立数出的是 WIN32 **398** / POSIX **128**，与我这里的 **431 / 241** 不一致——**我无法解释这个差**（很可能是它只扫了 `CMakeLists.txt:412-436` 那一段的文件子集，而 POSIX 的 `list(APPEND …)` 在 `:438-443`）。**两个数都留着，不调和一个我解释不了的差异。**

#### 第二个先例，而且它比第一个更接近 §3.1 要做的事（2026-10 新增）

第一个先例是「**在创建点拒绝**」——`builtin` 注册表能拒绝，因为第二次注册在语义上就是死代码。**但 §3.1 的表拒绝不了任何东西**：它只是把 69 个 opcode 与它们的消费者**摆在一起比较**。所以第一个先例证明的是「这个仓库肯为一条语义纪律加断言」，**没有**证明「跨两个artifact 的集合比对在这个仓库里跑通过」。

**现在它跑通过了。** 第二个先例（来源：`exact-lumen` 本轮的实测与修复，**我未独立复核，标为 [转述]**）：

- **症状**：四处文档声称有 CTest 覆盖、四处都没有 —— `docs/API.md` 第 2.1 节的证据列标题就是「证据（CTest）」，而 `:114` 把 `lint_case_missing_default_v04.im` 写成「相关 CTest」、`:115` 把 `lint_case_exhaustive_v04.im`（**是文件名，不是测试名**）与四个真实测试名并列；`docs/REQUIREMENTS_ANALYSIS.md:177` 与 `docs/STATUS.md:286` 把 `tools/migrate_report.py` 与 `bindgen_regression` / `scan_tools_regression` 并列，而 `tools/` 下**没有** `migrate_report.test.py`，那两个 CTest 跑的是 `bindgen.test.py` 与 `scan_tools.test.py`、**都不碰它**。
- **能力是真的，覆盖不是**：两个 fixture 今天 `--lint` 各出一条 `[WARN]`、rc=1；`migrate_report.py` 也 rc=0 出真报告。⇒ **不是「文档描述了不存在的东西」，而是「文档描述了一个从未接上的东西」** —— fixture 写好了、诊断是对的、表格把它当证据引用了，**而注册那一行从来没有被加过**。
- **为什么没人发现**：`CMakeLists.txt:733-743` 的五个 `lint_case_*` 是**手写列举**的（`:744` 的注释自己数着「The five lint_case_* tests above …」），第六、第七个 fixture 落地时**没有任何东西要求把它们加进去**；仓库里**没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**。
- **修法的两半**：① 注册 **#135** / **#136**，各带 `PASS_REGULAR_EXPRESSION` **和一条断言对方那条警告不出现的 `FAIL_REGULAR_EXPRESSION`**（两个 fixture 只差一行、走同一个 `--lint` 通道，只断言自己的发现**分不开**「因正确的理由触发」与「对每个 case 都触发」）；② 新增 `tools/check_orphan_fixtures.py` 与**门禁第 12 阶段 `orphan-fixtures`**。
- **反向验证**：把 `CMakeLists.txt` 退回 `HEAD` ⇒ `2 orphaned input(s) out of 110 checked` + 逐条点名 + `exit 1`。
- **普查**：67 个 `vtest/*.im` 中 5 个未被 `CMakeLists.txt` 提到 —— 两个真缺口、三个合法；30 个 `tools/*.test.py` 与 13 个 `tools/*.test.js` **今天都是 0 孤儿**。

**它为什么是更好的先例**：§3.1 的判据（表行数 == 枚举成员数、双向差集为空）与 `orphan-fixtures` **是同一种断言** —— **两个 artifact 之间的集合关系，没有任何单个文件能看到**。第一个先例证明「能拒绝就拒绝」，第二个先例证明「**拒绝不了就比对**」。

**而且这个先例自带一个教训，必须抄进 §7 第 4 条**：那个检查脚本**第一次跑 A/B 时自己崩了** —— `NameError: name 'CMAKE_SOURCE_DIR' is not defined`，因为提示串是 f-string，`${CMAKE_SOURCE_DIR}` 的花括号被当成了替换字段。⇒ **一个「能发现孤儿」的检查，在真的发现孤儿时抛异常而不是报告**：**它存在、它退出非零、而它答的是另一个问题。** 修法是把它写成 `${{CMAKE_SOURCE_DIR}}`。**没有那次反向验证，不会有人发现。** —— 这正是 §3.1 每条判据都要求「**故意改错必须变红**」而不是「加一个检查」的原因：**一个从未在真实反例上跑过的检查，不是证据。**

### 3.1.9 表里有一类行**不是「还没实现」，而是「不需要实现」**：零生产点 opcode

`producers` 那一列（§3.1.3）第一次填就抓出**两个枚举里有、全仓库没有任何东西会发射**的 opcode。
这条由 `exact-lumen` 提出，**我在本会话独立复跑确认**（命令与结果见 §10 第八轮行）。

| opcode | 声明 | VM 分派 | 实现体 | 生产者 | 判定 |
|---|---|---|---|---|---|
| `OP_STORE_CAPTURE` | `src/compiler/bytecode.h`（枚举内） | `src/vm/vm.c:3111` `case OP_STORE_CAPTURE: goto L_STORE_CAPTURE;` | **`src/vm/vm.c:3952-3955`** | **无** | 全仓库只出现在 `src/compiler/bytecode.h` 与 `src/vm/vm.c` **两个文件**里 ⇒ 实现体**不可达** |
| `OP_POP_REG` | 同上 | `src/vm/vm.c` 有 `case` | **`src/vm/vm.c:3761-3767`** | **无** | 第三个出现处是 `selfhost/compiler.im:37` 的 **`OP_POP_REG = 29`** —— 一个**裸常量定义**，全文件仅此一处，**从不用于发射** |

**判定方法**（客观、可复跑）：`enum − src/compiler/compiler.c` = **恰好这两个**（`enum` 69 个名字、`compiler.c` 67 个、330 个 `emit(comp->curBC, OP_…)` 调用点；**反向差集为空**）。

**这两个不是孤立的怪东西，它们是一对「同一件事的第二套协议」——而且那套协议没人用。**
`OP_MAKE_FUNC` 的实现体（`src/vm/vm.c:3957` 起）**自己从栈上取捕获值**：
```c
for (int ci = ins.r3 - 1; ci >= 0; --ci) {
    if (t->sp < 0) { … }
    Value *captured = &t->stack[t->sp--];
```
⇒ **今天的闭包捕获协议是 `OP_PUSH_REG` × N + `OP_MAKE_FUNC`**。而 `OP_POP_REG`（`value_move(&R[ins.r1], &t->stack[t->sp]); t->sp--;`）与 `OP_STORE_CAPTURE`（`im_closure_env_set(t->closure_env, ins.r2, &R[ins.r1])`）是**另一套**：把栈上的值显式取回寄存器、再显式写进捕获环境。**`OP_PUSH_REG` 由 `src/compiler/compiler.c` 在十余处发射（`:460`、`:562`、`:575`、`:587`、`:637-638`、`:879`、`:891`、`:903`、`:929` …），而它的两个搭档零发射。**

**这正是 §1 那个病的**第七个实例**，而且是最干净的一个**：一个语义（捕获值怎么送到闭包里）有**两个决定点**，其中一个**声明齐全、分派齐全、实现齐全、生产点为零**——`dead code that reads as live`（§3.1.8 的仓库自述原文，逐字适用）。**它和 `and`/`or` 是同一个形状**：不是「哪个文件是 C」，而是**同一条语义被独立决定了两次，且没有东西断言只有一处**。

**对 §3 的表意味着什么**：表若按「69 个 opcode × 三后端」建，**至少两行的三列都是空的，而其中 VM 列的空不是「未实现」而是「不可达」**。⇒ 表的每一行必须先过 `producers` 这一关；**一个零生产点的 opcode 不该出现在「后端要映射的 69 个」里**，它要么被删、要么被接上——**这是一个需要人裁定的选择，不是表能自己回答的**（已登记为 §9 第 14 条）。

**诚实边界**：① 「`OP_STORE_CAPTURE` 不可达」是**从「全仓库零生产点」读出来的，不是运行时实测**——要真测需造一个含该 opcode 的 `.inim`；② 我只用 `grep -rl` 扫了 `src/`、`selfhost/`、`tools/`，**没有扫 `mods/`、`projects/`、`vtest/`**；③ `enum − compiler.c` 只覆盖**C 编译器**这一个生产者，`selfhost/compiler.im` 的生产点我是**逐名字看的，没有做集合比对**。

### 3.2 `aot_native.c` 具体要改什么

1. 删除 `EXPR_BINARY`（`:418`）分支里对 `TOK_AND` / `TOK_OR` 的特判（`:420`）与 `:425-428` 的 `nv_boo(nv_tru(...) && ...)` 发射。
2. 改为仅按 `Instr.op` 查表发射。
3. **保留** `NV` / `nv_*` 前导（`:174-193`：`NV` 在 `:174`、tag 在 `:175-177`、`nv_int`/`nv_flt`/`nv_boo` 在 `:179-181`、`nv_asf` `:183`、`nv_asi` `:187`、`nv_tru` `:193`）——那是宿主 C 的运行时，属第 3 类「暂不可搬」。**但见 §6.1 末段**：2026-10 人类裁定走 A（AOT 改吃带 `Value` 语义的 IR）⇒ `NV` 将被**整体替换**，故**不要**在这套前导上新增 tag。

### 3.3 `wasm_backend.c` 具体要改什么

1. 删除 `cg_cond()`（定义在 `:778`，函数体到 `:847`）里 AND / OR 的递归（`:781-783` 与 `:790-794`）。
2. `OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，真值产生点唯一。
3. 保留固定导入表（`:14-15`）：`env.im_print_int(i64)` / `im_print_float(f64)` / `im_print_bool(i32)` / `im_print_nil()` / `im_error(i32)`。

### 3.4 语义表如何收敛成唯一一张

一张 `OpCode → { VM 行为, AOT 行为, wasm 行为 }` 表，作为唯一真值源。三列不一致即构建失败。这张表就是 O0 那个「先做决定」的决定的**落点**：决定一旦写进表，就不再有第二处可以重新决定它。

### 3.5 三套值表示：裁定 A 只覆盖了 AOT，wasm 那半仍未说

§3 的「所有后端消费同一份 IR」有一个**没写进 IR 的部分**：每个后端**用什么类型持有值**。2026-10 实测今天有**三套**：

| 后端 | 表示 | 位置 | 大小 |
| --- | --- | --- | --- |
| VM | `Value` | `src/vm/vm.h:40-45`（`int type` + 匿名 union `union { long long ival; double fval; }` + `char *sval` + `void *ptr`） | **32 字节** |
| AOT | `NV` | `src/compilation/aot_native.c:174`（三个 tag 在 `:175-177`） | 24 字节（`int` + `long long` + `double`） |
| wasm | 线性内存槽 | `src/compilation/wasm_backend.c:66` 的 `#define SLOT_BYTES 16`（文件头 `:4` 说的 8 字节是**陈旧的**） | **16 字节** |

**裁定 A（§6.1 末段）只说了 AOT**：「AOT 改吃带 `Value` 语义的同一份 IR ⇒ `NV` 被整体替换」。**wasm 那半没有被裁定**：wasm 的 16 字节槽同样是**第二套值表示**，它今天既没有 `nil`、也没有字符串/容器（1.5c）。⇒ **这是与 A/B 同形的岔口，必须在第 2 步之前裁定**：

| 走法 | wasm 的处置 | 裁定 |
| --- | --- | --- |
| **A′**：wasm 后端也吃带 `Value` 语义的 IR，槽布局由 IR 的类型信息决定 | 16 字节槽被 IR 的类型定义取代 ⇒ 1.5c 变成「把 IR 的值语义映射到 wasm」 | **✅ 已选（2026-10，人类裁定）** |
| **B′**：第 2 步只统一控制流，wasm 保留自己的 16 字节槽 | 1.5c（在 wasm 实现非标量）**是那时的前置** | 未选 |

**A′ 的三条后果（与 §6.1 末段 A 的三条后果同形，写下来免得下次有人又去扩 `SLOT_BYTES`）。**

1. **不要为「让 `cg_cond` 的 `else 1` 可达」去单独扩 wasm 的值表示**——那会造出第三套要在第 2 步整体删掉的值表示，正是本档 `:10-12` 记的病。
2. **wasm 后端与 AOT 后端改成同一件事**：都从「自己遍历 `Expr*` 并自带值表示」变成「消费同一份 IR，按 IR 的类型信息落值」。⇒ 两边的改动应当**在同一批**里做，否则会出现「AOT 已吃 IR、wasm 还在遍历 AST」的中间态，而这个中间态**没有任何判据能证明它是对的**。
3. **`src/compilation/wasm_backend.c:4` 的陈旧头注释（8 字节）必须与 `:66` 的 16 字节一起改**，否则下一个读注释的人会把槽宽写错——本档 §7 第 4 条的第一例（`src/compilation/aot_native.c:420-428` 那句 "C's && and || short-circuit exactly as the interpreter's do"）就是这么来的。

**本文档不替人做这个裁定**这一句**已作废**：裁定已于 2026-10 由人类给出，为 **A′**。

### 3.6 裁定 A′ 里有一个它自己没说的地方：**IR 今天没有类型信息**

上面 §3.5 后果 2 的措辞是「按 **IR 的类型信息** 落值」。**这句话里的「IR 的类型信息」今天不存在。**
（本条由 `exact-lumen` 在复核 §3.1.4 时提出，我在此**独立复核**：`src/compiler/bytecode.h:69-74` 的 `RegInstruction` 是 `{ OpCode op; int r1, r2, r3; }` —— **三个字段全是下标，没有任何类型字段**；`:87-128` 的 `Bytecode` 也没有类型表。）

⇒ **A′ 实际上假定了「类型信息在 IR 里」这件事已经成立**，而它是一个**独立的、更上游的岔口**，且**不在 §3.5 的 A′/B′ 里**。两条路：

| 走法 | IR 里放什么 | 后端要做什么 | 代价 / 风险 |
|---|---|---|---|
| **A″：IR 保持无类型（动态）** | 不变（今天的 `RegInstruction`） | **在目标侧实现一份完整的动态分派**：值的类型是**运行期**属性，与 VM 逐字同构。AOT/wasm 各自带一套 `Value` 形状的运行时 | 忠实、无新义务给编译器；**但后端无法按类型特化**，wasm 的 `arith` 质量（`docs/AUDIT.md` §5 O13：2.72× vs AOT 155×）不会因吃 IR 而改善 |
| **B″：IR 带类型信息** | 每个寄存器/每条指令带类型标注 | 后端可特化发射；类型是**编译期**属性 | **编译器必须生产并维护类型信息** ⇒ **这就在编译器里立了第二个类型决定点**，而 VM 在运行期还会自己再判一次 —— **正是 §1.3 那个「宽度契约分裂」的预演**（同一个语义两个决定点，横切「编译期 vs 运行期」而不是「C vs `.im`」） |

**这一条为什么必须交给人**：A″ 与 B″ 不是「先做哪个」的顺序问题，而是**「类型信息是不是 IR 的一部分」这个架构问题**；选 B″ 会让 §3 的工作量**再上一层**（~~先给编译器加类型推导~~ **—— 见 §3.7：这半句已被实测更正，类型代数 `src/types/typeset.c` 已经存在且零消费者，缺的是「推导遍 + IR 字段」，不是「写类型系统」**），并且**把 §1 的病搬到一个新的维度**（编译期类型 vs 运行期类型）；选 A″ 则**接受 wasm/AOT 不做类型特化**，`docs/AUDIT.md` §5 O13 那条性能账**不能在 §3 里还清**。

**今天已有的证据只说了一件事**：VM 的寄存器堆是**动态类型**的（`src/vm/vm.c:2912` `Value *R = t->R;`，每条指令运行期看 `R[ins.r2].type`，§3.1.4）⇒ **A″ 是「照抄 VM」的路，B″ 是「离开 VM 的模型去换性能」的路**。本文档**不替人做这个裁定**（已登记为 §9 第 15 条，并在 §10 标注为「需要人裁定」）。

### 3.7 裁定已下：**`var` 动态、其余静态 ⇒ 走 B″**（2026-10，第三个人类裁定）

**裁定原文**：「var 类型动态，其余静态走选项 2」（选项 2 = B″）。下面是**裁定之后**的实测复核，其中**一条更正了 §3.6 的代价估计**，**另一条是 B″ 必须回答、而 §3.6 没写到的问题**。

#### 更正：B″ 不需要「写一个类型系统」——它已经存在，只是**零消费者**

§3.6 的 B″ 行写「编译器必须生产并维护类型信息 ⇒ 工作量再上一层（先给编译器加类型推导）」。**「先加类型推导」这半句是错的，实测更正如下。**

- **类型代数已经建好了**：`src/types/typeset.c`（**248 行**）导出 `im_typeset_empty` / `im_typeset_any` / `im_typeset_enum` / `im_typeset_int_interval` / `im_typeset_union` / `im_typeset_intersection` / `im_typeset_difference` / `im_typeset_complement`（`src/types/typeset.h:37-45`）与 `contains` / `subset` / `intersects` / `kind` / `cardinality` / `materialize_enum`（`:47-56`）。这正是 `docs/archive/ROADMAP_3.1.md` 承诺的**集合化类型系统**：区间 + 枚举 + 四则运算构成格。
- **它被构建、被测试**：`CMakeLists.txt:417` 把它编进引擎；`:157-159` 有 `typeset_probe` 这个 CTest，断言内容正是 ROADMAP 点名的那几条 —— `im_typeset_int_interval(0, 255, true, true)`（**u8 窄化**）、与 `im_typeset_int_interval(0, INT64_MAX, false, true)` 的**交**、与枚举集合的**并**（`src/types/typeset_probe.c`）。
- **而它没有任何消费者**：`grep -rl "im_typeset" --include=*.c --include=*.h .` 排除 `./src/types/` 之后**为空**（只命中 `./.worktrees/` 下的其它 checkout）。`src/compiler/compiler.c` 对 `typeset|TypeSet|type_of|infer` **零命中**。

⇒ **B″ 的真实工作量 = 「一个把 AST 走成『每寄存器一个 `ImTypeSet`』的推导遍」+「IR 里放得下它的字段」**，**不是**「写一个类型系统」。**这比 §3.6 写的小；但仍比 A″ 大，因为推导遍是新的。**

#### 而同一个实测抓到了 B″ 必须回答的那个问题：**今天类型是运行期的值，不是编译期的事实**

- `type X = <set expr>` 今天被编译成 **一次全局写入**：`src/compiler/compiler.c:2224-2231` 的 `case STMT_TYPE` 是 `register_global(comp, tname)` → `compile_expr(comp, stmt->typeStmt.set)` → `emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0)`。**类型是一个运行期全局变量，装着集合的值。** `STMT_BE`（`:2232-2240`）同形，发射 `OP_BE`。
- 而 `OP_BE` 的实现体用的是 **VM 自己的运行时集合**：`vm->be_bound[g] = sidx >= 0 ? sidx + 1 : 0;` 与 `set_contains(vm, sidx, &init)`（`src/vm/vm.c:4262-4308` 内，实测命中）—— **不是 `im_typeset_*`**。
- ⇒ **仓库里有两个互不相识的集合实现**：`ImTypeSet`（区间/枚举格，**建好、测好、零消费者**）与 VM 的 `vm_set_*`（`vm_set_new` / `vm_set_add` / `vm_set_interval` / `set_contains` …，**真正在跑**）。

**这是 §1 那个病的第六个实例，而且是最大的一个**：同一条语义——「这个值在不在这个集合里」——**有两个实现**，其中一个**完全没有消费者**。它比 `and`/`or` 更大，因为它不是一条运算符语义，而是**整个类型系统**。

⇒ **裁定 B″ 的可行性直接压在这上面**：要让「其余静态」成立，`type X = …` 必须在**编译期**求值 ⇒ **一个依赖运行期值的类型表达式要变成非法**（今天它合法）；而 `OP_BE` 的运行期检查**要么被编译期判定取代、要么与它共存——共存就是立第二个决定点**。**这是 B″ 必须回答的问题，§3.6 没写到它。**

#### B″ 新增的证明义务（必须进 §6）

> **对每一个编译器给出「单例标量」类型的寄存器，VM 在该寄存器上的运行期类型必须与之一致。**
> 判据（可证伪）：差分测试中，凡编译器标注为「单例 `IM_TYPE_INT`」的寄存器，运行到该指令时 `R[i].type` 必须为 `VAL_INT`；出现一个反例即红。

这条义务之所以必需，是因为 B″ 让**同一个值的类型有两个判断者**（编译器一处、VM 运行期一处）。§6 今天没有任何判据覆盖它。

#### 格式后果（登记为 §9 第 16 条）

IR 要放类型 ⇒ `RegInstruction`（`src/compiler/bytecode.h:69-74`）加字段 ⇒ **`.inim` 的磁盘格式改变** ⇒ `INIM_BYTECODE_VERSION 3`（`:64-66`，与 `INIM_ABI_VERSION 2` 并列）**必须升**，且 `.inim` 的三个读者（`src/compiler/bytecode.c`、`src/main.c`、`src/compilation/deps.h`）**必须同批**。

**`var` 那一半不是「另一条路」，是 B″ 内部必须保留的一格**：IR 需要一个「动态」类型值（语义上等于 `im_typeset_any()`），使 `var` 声明的值走与 **A″ 完全相同**的运行期分派。⇒ **A″ 与 B″ 不是二选一**：B″ 的实现里**同时**有静态特化路径与动态分派路径，而**这两条路径的边界（哪些声明算 `var`）就是新的、必须被断言的东西**——否则「var 动态」会变成「有的地方静态、有的地方动态、没有东西说清是哪个」。

**诚实边界**：① 「`im_typeset_*` 零消费者」是 `grep -rl` 的**字面结论**，它证明的是「`src/`（除 `src/types/`）里没有 `im_typeset` 这个字符串」；② 「`OP_BE` 用 `set_contains` 而非 `im_typeset_*`」是**读 `src/vm/vm.c:4262-4308` 命中行**得出的，我**没有**通读该块全文；③ 「`type X` 是运行期全局」是从**编译器发射形状**读出的，**没有实跑一个 `.im` 去观察全局**；④ 我**没有**检查 `mods/`、`projects/`、`vtest/` 里有没有人用 `type`。

### 3.8 裁定已下：**契约式双层判定**（2026-10，第四个人类裁定）

我在 §3.7 末尾把「编译期类型与 `OP_BE` 运行期检查的关系」提成三选一（(i) 编译期取代运行期；(ii) 两层都留但强制断言一致；(iii) 运行期仍是唯一判对错的人、编译期类型只是优化提示）。**人类裁定：走 (ii) 的强化版**，原文要点如下（下面是对裁定的转述与落地，**裁定本身以人的原话为准**）。

#### 裁定的内容

- **主从关系**：**编译期保守推断，运行期契约执行。** 不是「谁服从谁」，而是「**编译期建立契约，运行期执行契约**」。
- **编译期的义务是保守**：编译器推断出的集合（记作 `CompilerSet`）必须是运行期实际可能出现的集合（`RuntimeSet`）的**超集** —— 即 `CompilerSet ⊇ RuntimeSet`。编译器只负责证明「**如果运行，绝不会因为类型/边界崩溃**」。
- **运行期的地位**：`OP_BE` 是「编译期未竟事业的兜底」，**只在编译期无法证明安全性处插入**。若 `OP_BE` 触发，意味着**编译期的静态分析是错的**（或遇到了 `unsafe` / 动态逃逸）。
- **一致性断言（Debug / Strict 模式）**：在编译期已证明安全的路径上，插入等价断言；**断言失败 ⇒ 编译器有 bug ⇒ Panic 并记录全息数据，按「极严重的编译器级错误」处理**。
- **按执行模式分层**：
  - **静态编译模式（默认，无 `var`）**：强制走 (ii)。编译期做极限推断（基于集合运算），**能证明的消除 `OP_BE`**，**不能证明的触发编译错误**，要求开发者显式提供 `@runtime_check` 或改写为 `Result`。
  - **动态 / 脚本模式（`var` 或解释执行）**：**退化为 (iii)** —— 明确告知：此模式为快速原型，**运行期是最终真理**，`OP_BE` 是唯一判错者。
  - **跨边界（静态与动态互调）**：必须插入**显式的边界转换检查**；这是两层结论一致性的**对接点**。
- **对 AI 生成代码的三条准则**：① **绝不依赖运行期兜底**（要给出精确的集合类型标注，让编译器静态证明）；② **明确降级**（无法保证类型安全时，主动用 `Result` 或显式 `@runtime_check`，不得假装安全）；③ **尊重 `var`**（用了 `var` 就要明确告知此处依赖运行期 `OP_BE`，并做好错误处理）。
- **一句话**：「**编译期负责『依法证明』，运行期负责『违法必究』，两者不得有二义**」；用 (ii) 的原则实现，`OP_BE` 做最终铡刀，**但必须用断言防止铡刀砍错人**。

**裁定否决 (iii) 的理由（要记下来，因为它决定了 §1 的哪些修法不可接受）**：若运行期是唯一判对错的人、编译期类型只是优化提示，则**形式化验证与 Proof 失去根基**（编译期不能保证 `x in Z+`，关于 `hp >= 0` 的证明就是空中楼阁），且**集合论类型（`Z * [0~255]`）从「给开发者与 AI 的契约」沦为摆设**，「编译通过即安全」的哲学无法成立。

#### 这条裁定与 §3.1.8 的先例**形状不同**，必须写清（否则读者会以为我们在重复已有的纪律）

- **§3.1.8 的先例是「拒绝第二个决定点」**：builtin 注册表在**创建点**就拒绝重名（`src/vm/vm.c:1676-1686`），因为**它能拒绝** —— 第二次注册在语义上就是死代码。
- **本裁定是「允许第二个决定点，但要求它有主从关系并被断言」**：编译期与运行期**都在判断类型**，而**编译期那一半不能被删掉**（删了就是 (iii)，已被否决）。⇒ 这里**不存在**「在创建点拒绝重复」的位置，只能**用断言钉住两者一致**。
- **两者不矛盾**，但**它们是两个不同的模式**：**能拒绝就拒绝；拒绝不了就断言。** §3 的表与 §6 的判据必须同时容纳这两种模式，**不能把本裁定当成 §3.1.8 的推广**。

#### 我要指出的三处「裁定的措辞与可实现的断言之间还有距离」（**不是反对裁定，是反对把它直接抄进代码**）

1. **`CompilerSet == RuntimeSet` 这个等式不能直接实现。** `CompilerSet` 是**可能类型的集合**（一个上界），而运行期在某个点上是**一个具体的值 / 一个具体的 `Value.type`** —— 一个集合与一个元素之间没有 `==`。**可实现的形状是成员关系**：`type_of(实际值) ∈ CompilerSet`。等式只在**编译期集合恰好是单例**时才与成员关系等价（这正好是 §3.7 那条证明义务覆盖的情形）。**⇒ 落地时必须把它写成 `∈`，并说明 `==` 是它在单例情形下的特例**；否则那条 Debug 断言没有可写的形式。
2. **`OP_BE` 在裁定里有两个不同角色，必须分清。** 「运行期的地位」段说 `OP_BE` **是编译期无法证明时的兜底**；而「静态编译模式」段说**无法证明时触发编译错误**。两者同时成立时，**静态模式下 `OP_BE` 永远不会被插入**（因为无法证明的程序根本编译不过）。**自洽的读法只有一条**：`OP_BE` 恰好出现在三处 —— ① **动态 / 脚本模式**（唯一判错者）；② 静态模式下**开发者显式写了 `@runtime_check` 的地方**；③ **Debug 断言路径**。**建议按这个读法落地，并请人类确认**——因为这是「一条规则两份说法」的形状，正是本文档 §1 在治的那个病，只不过这次它在**裁定的文本内部**。
3. **「无法证明 ⇒ 编译错误」是一条今天不存在的语言规则，且它会让今天合法的程序编译不过。** 今天 `be` 是**纯运行期检查**，编译器**从不拒绝**（`src/compiler/compiler.c:2232-2240` 无条件发射 `OP_BE`）。⇒ 这条规则**新增了一项编译器的否决权**，而**没有任何东西量过有多少 `.im` 会因此被拒**。同样，`@runtime_check` 是**新语法**，今天不存在（`grep -rn "runtime_check" src/ selfhost/ docs/` 待做，我**没有**做）。**⇒ 这两项在 §6 里各自需要一条「先量后改」的前置判据，不能直接写进实现步。**

#### 这条裁定对 §3 与 §6 的直接后果（登记为 §9 第 17 条）

- **§3 的 69 行表要多一列**：每个 opcode 需要标注**它是否受编译期契约覆盖**（即：在静态模式下这个 opcode 是否可能被编译期消除）。今天表里的 `can_raise` 与 `producers` 两列都不表达这件事。**这是 B″ 与 (ii) 叠加后的新列，不是原表的冗余。**
- **§6 需要两条新判据**（都可证伪）：
  1. **保守性判据**：对每一个由编译器标注了类型的寄存器，**运行期实际 `Value.type` 必须属于该编译期集合**（`type_of(实际值) ∈ CompilerSet`）；**故意把某处 `CompilerSet` 收窄成不含实际值的一格，该测试必须变红。**
  2. **主从判据**：在静态模式下，**凡编译期已证明安全的位置，`OP_BE` 不得出现**；凡 `OP_BE` 出现的位置，必须是动态模式 / 显式 `@runtime_check` / Debug 断言三者之一。**判据形式**：对同一段 `.im`，静态模式产出的字节码里 `OP_BE` 的出现次数为 **0**（或全部落在上述三类白名单位置），而动态模式产出里 `OP_BE` 保留 —— **同一份源码两种模式，产出必须都符合各自的规则**。

### 3.9 裁定已下：**先量爆炸半径，以「静态模式 `OP_BE` 为 0」为终极目标**（2026-10，第五个人类裁定）+ 实测结果

§3.8 指出裁定文本内部 `OP_BE` 有两个角色（「编译期无法证明时的兜底」vs「静态模式下无法证明 ⇒ 编译错误」），不能同时成立。我提了三选一；**人类裁定：短期执行 (C) 先量爆炸半径，长期锚定 (A)「静态模式下 `OP_BE` 必须为 0」**。裁定原文的要点：

- **「无法证明」不等于「报错」，而是「强制显式降级」** —— 这是消解那两个角色的关键一句。⇒ **§3.8 那三处白名单（动态模式 / 显式 `@runtime_check` / Debug 断言）被确认为最终口径**；「无法证明且没有标 `@runtime_check`」才**是编译错误**，并要给出 AI 友好的提示（「编译器无法静态证明此处的类型约束，请添加 `@runtime_check` 或修改逻辑返回 `Result`」）。
- **量完之后要把实例分成三类**（裁定原文的分类）：① **AI 生成的垃圾代码**（滥用 `be` 或缺上下文）⇒ 改提示词，不改编译器；② **复杂业务逻辑**（外部输入、第三方库）⇒ 天生不可证，需要逃生舱；③ **编译器能力不足**（本可证明但编译器太笨）⇒ 编译器团队的优化目标。**这个分类很重要，因为它决定了「不可证明的实例」该由谁负责修。**
- **为什么否决 (A) 直接上**：那会让 Inimerse 比 Rust 还难写，违背「AI 易于生成、面向目标编程」的初衷；**为什么否决 (C) 全凭运行期**：集合论与形式化验证（Proof）将彻底失去意义。当前裁定「量 + 显式降级」保留了**契约精神**：想用 `var`/动态可以，但承认了风险；想用静态集合，必须承担证明义务；**编译器绝不悄悄替你兜底（隐式 `OP_BE`），一切都摆在台面上。**
- **对 AI 的三条**（裁定原文）：**优先尝试证明**（拆分逻辑、用 `if x in Z+` 收窄，帮编译器完成证明）；**必须显式降级**（确实无法证明时主动把返回类型改成 `Result` 并处理 `Err`，而不是让编译器插隐式 `OP_BE`）；**绝不隐瞒不确定性**（含运行期不确定性的代码必须有显式路径捕获 `OP_BE` 可能引发的异常）。

#### 实测：爆炸半径的**上界**是 10 处 / 346 个 `.im`，且这 10 处的约束**全部可由字面量或内建类型解析**（2026-10，本会话实测）

裁定要求「先跑脚本量一下数据，看看如果『强制显式降级』到底会炸掉多少个文件」。**今天没有静态分析器，所以「不可证明」这个谓词不可计算**；可计算的是**上界**：**每一个 `be` 语句**，按「它的约束表达式能否只靠字面量 / 内建类型 / 具名类型声明解析出来」分类。

**普查命令**（覆盖整个仓库，排除 `build/`、`.worktrees/`、`.verify/`）：

```
grep -rnE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]' --include=*.im .
```

**结果：346 个受跟踪 `.im` 文件中，只有 10 处 `be` 语句，分布在 10 个文件里。逐条分类如下。**

| 文件:行 | 语句 | 约束表达式的种类 | 能否解析 |
|---|---|---|---|
| `exc_test.im:11` | `gamemode be 0,1,2,3:0` | 字面量枚举 | ✅ |
| `projects/exc_test.im:11` | `gamemode be 0,1,2,3:0` | 字面量枚举 | ✅ |
| `meta_test.im:45` | `g be 0,1,2,3:0` | 字面量枚举 | ✅ |
| `set_test.im:28` | `gamemode be 0,1,2,3:0` | 字面量枚举 | ✅ |
| `inf_set_test.im:115` | `gv be (0,10):5` | 字面量区间 | ✅ |
| `vtest/lint_case_membership_v04.im:2` | `dir be Direction = "N"` | 具名类型，声明在同文件 `:1` = `type Direction = "N","S","E","W"`（**字面量枚举**） | ✅ |
| `vtest/lint_case_enum_v04.im:2` | `dir be Direction = "N"` | 同上 | ✅ |
| `vtest/type_collection_v04.im:3` | `x be Byte: 42` | 具名类型，声明在同文件 `:2` = `type Byte = [0~255]`（**字面量区间**） | ✅ |
| `big_globals_test.im:168` | `g200 be N : 42` | **内建类型** `N`（`src/vm/vm.c:1871` 的 `"N","Z","Z+","Z-"…` 名单，`:1880` `if (strcmp(name,"N") == 0) return 0;`） | ✅ |
| `nsadv_mod.im:3` | `q be N : 5` | 同上 | ✅ |

⇒ **今天爆炸半径的上界 = 0 个「不可解析约束」的实例。** 按裁定自己的执行路径（「**如果只炸几个，立刻执行 A 的落地口径**」），数据支持**直接采用 (A) 的口径**——**但有一个前提必须先说清**：这 10 条是我**按分类规则手工判定**的，**不是分析器跑出来的**；「不可证明」这个词在分析器存在之前**没有可执行的判据**。⇒ **正确的读法是「上界 10，且 10/10 属于『可解析』一类」，不是「已证明 0 处不可证明」。**

**顺带两条与 `docs/SYNTAX.md` 的互相印证**：`docs/SYNTAX.md:550` 说 `be` 是「仓库里最不常见的语句形式之一」——**346 个文件里 10 处，实证了这句话**；而 `docs/SYNTAX.md:546` 给的形式是 `name be <集合或表达式> [: init]`，上表里 `be Direction = "N"` 用的是 `=` 而不是 `:`（`docs/SYNTAX.md` 没写 `=` 这种 init 分隔符）⇒ **文档与语法的又一处不一致，登记为待核**（我**没有**去读 `src/parser/parser.c:1357-1365` 确认 `=` 是否合法）。

**诚实边界**：① 上表 10 条是**我手工分类**的，判据是「约束表达式能否只靠字面量/内建/同文件具名声明解析」，**不是**任何静态分析器的输出；② 正则只匹配「行首 标识符 空白 `be` 空白」这一种形状，**漏掉**别名/带下标的名字、`be` 前有换行的续行、以及非常规空白；**误报**（把表达式里恰好叫 `be` 的标识符当语句）**未排查**；③ 我**没有**检查 `type X = <依赖运行期值的表达式>` 这种**类型声明本身不可静态求值**的情况——那才是 §3.7 记的真问题（今天类型是运行期全局），上表只看了 `be` 的**使用点**；④ 「内建类型 `N` 可解析」是从 `src/vm/vm.c:1871/1880` 的**名字名单**读出的，**没有实跑**编译器去确认它对 `be N` 是否真的在编译期求值。

---

## 4. (c) 引导（bootstrap）方案

前提：C VM 仍是 substrate。目标：`.im` 前端成为 **load-bearing**，且等价性被**持续**证明，而不是一次性宣称。

1. **复用既有对比工具**。`tools/selfhost_compare.py` 已能报 `selfhost parity OK: 10 target(s) byte-identical, 23 skipped` **[转述工作订单/既有事实]**。
2. **判据要升格**。该工具内有 `ops_only()`（`:87-96`）**[读码]**，比较的是**操作序列**而非寄存器号——**这正是 IR 等价的正确定义**（寄存器分配是自由，操作序列不是自由）。门禁判据应从「byte-identical」升格为「opcode 序列相同」，否则：寄存器分配一改就假红，而真分歧可能被字节相同掩盖。
3. **23 skipped 必须逐个定性**。每个 skip 要么给出不能比较的**结构性理由**并记录在案，要么转成可比较。存在跳过的「等价」不可证。
4. **`.im` 作为定义方**：以 `selfhost/compiler.im` 为语义基准，`src/compiler/compiler.c` 是「必须与基准一致」的镜像；两者差异全部由第 2 条的判据捕获。
5. **持续性的落地方式**：把上述判据挂进既有门禁（见 §7 的「不做什么」——本设计不自行改门禁）。

---

## 5. (d) FFI / Native ABI 边界

**`.im` 需要从 C 拿什么**：
- 内存分配与回收；
- 打印 / 报错等侧效原语；
- 宿主 `cc` 的调用（AOT 最后一跳）；
- 固定导入表里的原语。wasm 侧的边界已经写死 **[读码]**：`src/compilation/wasm_backend.c:14-15` 的 `env.im_print_int(i64)` / `im_print_float(f64)` / `im_print_bool(i32)` / `im_print_nil()` / `im_error(i32)`。

**边界怎么定**：跨边界只传**已定宽的标量**，绝不过 `Value*`。今天 `Value` 是 32 字节、整数载荷 **64 位**（`src/vm/vm.h:40-45` 的匿名 union **[读码]**），而 AOT 的 `NV` 整数也是 64 位（`src/compilation/aot_native.c:174` **[读码]**）——**取证当时** `Value` 的整数载荷是 32 位 `int`，那时边界两侧宽度不同，**这本身就是 ABI 缺陷**，比 `%` 更广；§10.49 把 `Value` 整数槽改成 64 位后，宽度这一条**已对齐**（见 §0.5）。

**稳定性怎么保证**：
- ABI 需要显式版本号 + 宽度标签，而不是靠注释承诺；
- `docs/archive/RELEASE_0.5.0.md` 已划定 ABI 面 **[读码]**：`:23-28`「Stable Native ABI」——`native` 模块类型 + capability sandbox 限制 `unsafe`；**C ABI 兼容**（`i32/int`、`i64/long`、`f64/double`、`string/char*+length`）；`:102-106` ABI 版本号 `infiverse.mv1/abi/1.0` + `--abi-version` 构建标志，版本不匹配在**构建期**失败；
- **一条与现状直接冲突的书面承诺** **[读码]**：`docs/archive/RELEASE_0.5.0.md:48` 写「**Unified bytecode format**: All backends (interpreter, AOT, Wasm) share the same bytecode format and ABI for consistent execution」。而 §1 证明 AOT（`src/compilation/aot_native.c`）与 wasm（`src/compilation/wasm_backend.c`）今天是 **`Expr*` 进**、各自重判语义，**并不消费字节码**。⇒ 本设计的 §3 **不是新造需求，而是让 0.5.0 已经宣称的事情成真**；
- `docs/archive/ROADMAP_3.1.md:28` 强制要求 BigInt / 窄化整数 / 枚举值在模块 ABI 与 `.vverse` 序列化中**可逆**，故任何宽度表示都必须是可逆映射，禁止把内部编号泄露为用户值。

---

## 6. (e) 迁移顺序 + 每步的可证伪判据

**禁止出现「重构完成后」这类不可测表述。** 每一步都必须有能跑出通过/失败的命令。

| 步 | 动作 | 可证伪判据（含命令） |
|---|---|---|
| **0** | 冻结唯一语义表；采纳 `docs/AUDIT.md` §5 的 **O0 选项①**（~~值语义胜出~~ → **已由用户裁定改为布尔**，见 §0.5 / `docs/STATUS.md:2464`；改动落在解释器与 wasm，AOT 本就正确） | ① `tools/im_diff_fuzz.py` 重跑，`and`/`or` 类分歧 **= 0**（§0.5：已进门禁并归零）；② `docs/API.md:90` 明写 `and`/`or` 返回**操作数**还是布尔 —— **已定为布尔**；③ `AUDIT.md` §1.6 的八行表格**三后端逐格相同**（解释器 / AOT / wasm；wasm 必须从 `error: wasm MVP subset: 'and'/'or' outside a condition is not supported` 变成给出值） |
| **1** | 两个编译器统一 `and`/`or` 降级（短路跳转 + 真值常量）；顺带定夺 `OP_AND`/`OP_OR` 去留（§0.5 复核：§10.42 已给两者活语义，**该决定仍未做**） | `tools/selfhost_compare.py` 的 opcode 序列判据在 `or` 探针上**零差异**（AUDIT §6 的 `orv.im` 探针） |
| **1.5a** | **已核实关闭（2026-10，实现会话实测）**：`src/compilation/wasm_backend.c` 的 `cg_cond` 语义**已经**与 `vm_truthy()` 同表，缺的是路径**可达**；而「可达」= 在 wasm 实现非标量（见 1.5c） | **无可修，故无新判据**：原三条判据分别「已由实测满足（18/18）」「无对象」「无需触发」。**未新增任何 CTest**（§6.1） |
| **1.5b** | **AOT 半：已由人类裁定走 A（2026-10）⇒ 不做。** `NV`（`src/compilation/aot_native.c:174`，三个 tag `:175-177`）无 nil/字符串/容器表示，但第 2 步让 AOT 消费带 `Value` 语义的同一份 IR ⇒ `NV` 被整体替换 | **不适用（已裁定不做）**；原判据 `func_no_return` 分歧**归入第 2 步**（§6.1 末段） |
| **1.5c** | **新立：在 wasm 实现非标量**（值表示 + 打印导入 + 堆生命周期）。**已由人类裁定 A′（2026-10）⇒ 不做「独立扩值表示」，改为「把 IR 的值语义映射到 wasm」，归入第 2 步**（§3.5）。**与 1.5a 分开命名**，否则计划里会一直写着「1.5a 便宜」 | 随第 2/3 步的表一起钉（§3.1.3 的 `OpSemantics` 非标量行） |
| **2** | `src/compilation/aot_native.c` 改吃字节码（§3.1.3 / §3.2） | ① `grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c \| sort -u \| wc -l` 从**今天的 0** 变成 **69**（或逐条列出未实现的 opcode 与理由）；② `grep -c 'RegInstruction' src/compilation/aot_native.c` **> 0**；③ `OpSemantics` 表的行数 **= 69**，且有 CTest 断言**表行数 == `bytecode.h` 枚举成员数**（防漏行）；④ **反向验证**：故意改错表中一行，③ 的 CTest 必须变红；⑤ `tools/aot_native.test.py` 的 `DIVERGENCE` / `EQUIVALENCE` 计数**不变**（零回归） |
| **3** | `src/compilation/wasm_backend.c` 同改（§3.1.3 / §3.3），**且必须与第 2 步同批**（§3.5 A′ 后果 2：只改一边会造出没有判据可证的中间态） | 同第 2 步的 ①–⑤，跑 wasm 目标；外加 ⑥ `grep -oE 'case OP_[A-Z0-9_]+' src/compilation/wasm_backend.c \| sort -u \| wc -l` 从**今天的 1（注释）** 变成 **69** |
| **4** | `%` 与宽度契约统一（§1.2 / §1.3） | `2147483648 % 7` 等 **四组**预测值在**五条通道**给出同一整数（`docs/AUDIT.md` §1.1 已备好该四组值；五条通道见同文件 §2.1） |
| **5** | 回头重新审视 `tools/aot_native.test.py` 里三条 `DIVERGENCE` 钉死项（`AUDIT.md` §5 末尾明写要求） | 三条中与 §1.1/§1.2 同源者可升为 `EQUIVALENCE`；升不了的必须写明为何**不是**同源 |

**顺序是先决关系，不是偏好**：第 0 步不落地，第 1 步就没有正确目标（会照着错的语义表统一）；第 1 步不做，第 2/3 步改完后 `and`/`or` 仍会与解释器不一致。**第 1.5 步原先被写成第 2/3 步的前提，2026-10 实测后这条依赖改了**（§0.5.1）：1.5a 无可修、1.5b 已裁定不做、1.5c 被 A′ 收进第 2 步 ⇒ **第 1.5 步不再挡在第 2 步前面**。**第 2 与第 3 步必须同批**（§3.5 A′ 后果 2）。

### 6.1 已选定的下一步：先把证明覆盖扩到字符串与容器（第 1.5 步）

> **本节在 2026-10 被实测更正过一次，且更正的方向是「成本比原先写的高」。** 原文说「给 AOT 的 `nv_tru` 与 wasm 的 `cg_cond` 补上与 `vm_truthy()` 同表的非标量分支」——实测发现**两半都不成立**：wasm 那半**不需要改语义**（它已经同表），AOT 那半**改不了**（`NV` 根本没有对应表示）。原文把这条估成一个小步骤，**估错了**；下面是更正后的版本。

**为什么是这一步，而不是直接做第 2 步。** §3 的 IR 收敛是结构修法，做完之后要回答的问题正是「你怎么知道搬对了」。今天的判据（`tools/aot_native.test.py` 的 `EQUIVALENCE` / `DIVERGENCE`、`tools/im_diff_fuzz.py`、`tools/selfhost_compare.py`）**只覆盖标量**：两个编译后端 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` 都是 **0**。⇒ 在扩覆盖之前动 §3，等于**把「无法验证」原样搬进新架构**。

> **2026-10 实测结论：这一步的「便宜的那半」不存在。** 实现会话（`exact-lumen`）按本节判据去测 wasm 能否产出一个字符串，三条程序全部拒绝：
>
> ```
> say 1 + 2        RC=0   compiled
> say "hi"         RC=1   error: wasm MVP subset: strings are not supported by the wasm MVP subset (line 1)
> s = "hi"; say s  RC=1   error: wasm MVP subset: expression type 2 not supported by wasm MVP subset
>                         (strings/collections need the interpreter) (line 1)
> ```
>
> ⇒ **wasm 连一个字符串字面量都产不出来**，所以 `TAG_STR 4`（`:58-63`）与 `cg_cond` 的 `else 1` **今天都不可达**。**「让非标量可达」= 在 wasm 后端实现字符串与容器 = §2 表第 ⑤ 类**，与 1.5b **同类工作量，只是落在另一个后端上**。⇒ 本节原先写「1.5a 便宜」是错的；理由**不是**语义要改（语义**确实**不用改），而是缺的是**整个值表示**。

**先确认唯一语义来源（实测）。** 真值规则**不是新定的**。唯一来源是 `src/vm/vm.c:347` 的 `vm_truthy()`（声明 `src/vm/vm.h:362`）：

```c
int vm_truthy(const Value *v) {
    switch (v->type) {
        case VAL_BOOL:  return v->ival != 0;
        case VAL_INT:   return v->ival != 0;
        case VAL_FLOAT: return v->fval != 0.0;
        case VAL_NIL:   return 0;
        default:        return 1;      /* 空串、数组、字典、集合全部为真 */
    }
}
```

其上方注释（`src/vm/vm.c:340-346`）逐字写着规则：「**零与 `nil` 为假，其余一切——包括空串与每一个容器——为真**」，并说明这正是 §10.53 收敛六处调用点要解决的旧分歧。**注意**：本会话实测 `vm_truthy` 在 `src/vm/vm.c:347` / `src/vm/vm.h:362`，与 `docs/STATUS.md` §10.53 记的 `:293` / `:356` **不一致**（代码在其上方增长所致）——**以实测为准**。

**更正一：wasm 那半不需要改语义，缺的是「走得到」。** `cg_cond` 的尾部（`src/compilation/wasm_backend.c:834-847`）实测已经是：`NIL → 0`；`INT`/`BOOL → i != 0`；`FLOAT → f != 0.0`；**其余 tag → 1**。这与 `vm_truthy()` 的 `default: return 1` **逐字对应**。⇒ `docs/STATUS.md:3171` 里「对非 INT/BOOL/FLOAT 直接返回 1」**不是缺陷，是正确行为**；真正的缺口是那条路径**不可达**（字符串/容器表达式根本不编译）。wasm 的 tag 空间**已经有** `TAG_STR 4` / `TAG_ARR 5`（`src/compilation/wasm_backend.c:58-63`），且 `:406`、`:1063` 已有 `TAG_STR` 发射点 ⇒ 这一半**不是改语义**（语义确实已对，见上方实测框），但也不是「补一条分支」——它是**在 wasm 实现非标量**，即 §2 表第 ⑤ 类。

**更正二：AOT 那半不是「补分支」，是「补表示」。** 实测 `NV` 的定义是：

```c
typedef struct { int t; long long i; double f; } NV;      /* src/compilation/aot_native.c:174 */
#define NV_INT 0                                            /* :175 */
#define NV_FLT 1                                            /* :176 */
#define NV_BOO 2                                            /* :177 —— 三个 tag 全部只描述标量 */
static inline int nv_tru(NV a) { return a.t == NV_FLT ? (int)(a.f != 0.0) : (int)(a.i != 0); }   /* :193 */
```

`t == 0` 即整数，**没有 `NV_NIL`、没有字符串、没有容器**。三个 tag **全部只描述标量**（`NV_INT` / `NV_FLT` / `NV_BOO`），所以 `nv_tru` 的 `else` 分支不是「漏了字符串」，而是「**只能读 `.i`**」——**给它补分支是做不到的**，必须先扩 `NV` 的 tag 空间与整套 `nv_*` 运行时。`nv_tru` 对 `NV_BOO` 是**对的**（`nv_boo`（`:181`）把 `.i` 写成 `0/1`）。**没有绕过 `t` 的旁路**（复核：`nv_asf` `:183`、`nv_asi` `:187`、`nv_tru` `:193`、二元算术 `:213`/`:221`… 全部先看 `.t == NV_FLT`，否则读 `.i`；`.t` 的引用形状一致，唯一的例外是 `:311` 的 `say` 用 `t == NV_BOO` 去选 `true`/`false` 字面量）⇒ `NV` 的类型信息**只有 `t` 一个来源**。这解释了 `tools/aot_native.test.py:187` 的第一条 `DIVERGENCE`：`func nothing() { x = 1 }` + `say nothing()` → 解释器 `nil`、AOT `0`（零值 `NV` 的 `t=0, i=0` 就是整数 0）。**这不是「真值判断不同」，是「表示不同」。**

**行号边界**：`src/compilation/aot_native.c` 整个文件是 C 源码**生成器**，上述行都带 `"…\n"` 前缀 ⇒ 行号是**该文件里的行号**，不是被生成代码的行号。

⇒ **这一半属于 §2 表第 ⑤ 类（值表示与集合运行时），不是「扩覆盖」的小步骤。** 把它算作第 1.5 步的一部分，就是原文最严重的估算错误。

**因此第 1.5 步拆成两半——但实测之后，两半都不便宜，而且都没有「可修的缺陷」。** 实现会话（`exact-lumen`）的差分结论：**18 个标量真值用例，解释器与 wasm 逐条相同（18/18，0 分歧）**；AOT 侧既有套件 `aot_native.test: 104 cases (86 equivalence, 2 pinned divergences, 6 runtime errors, 10 refusal), 0 failures`。⇒ **标量一致已经由两个既有测试文件断言了**，「三后端真值表 CTest」这一半**加不了新信息**；而非标量那一半**今天根本跑不起来**（两个后端都拒绝）。**1.5a 今天没有可钉的东西**，故**未新增任何 CTest**（来源：`exact-lumen`）。

| 子步 | 内容 | 成本 |
| --- | --- | --- |
| **1.5a** | wasm：让 `cg_cond` 的 `else 1` **可达** | **原估「小」已被实测推翻**（2026-10）：见上方实测框 ⇒ 实为「在 wasm 实现非标量」= §2 第 ⑤ 类，与 1.5b **同类** |
| **1.5b** | **AOT 半，已重新归类，且已被人类裁定为「不做」**：`NV`（`src/compilation/aot_native.c:174`，三个 tag `NV_INT 0`/`NV_FLT 1`/`NV_BOO 2` 在 `:175-177`）**无 nil/字符串/容器表示**。**这已不是「补一个分支」，而是 §2 表第 ⑤ 类（值表示）** | **已裁定走 A（2026-10，人类）**：AOT 改吃带 `Value` 语义的同一份 IR ⇒ `NV` 被整体替换 ⇒ **1.5b 不做**；AOT 改为**链接 `Value` 运行时**，归入第 2 步。见 §6.1 末段 |
| **1.5c（新立，与 1.5a 分开命名）** | **在 wasm 后端实现字符串与容器**（值表示 + 打印导入 + 堆生命周期）。与 1.5b 同类 | 大。**已由人类裁定 A′（2026-10）⇒ 不做「独立扩值表示」，改为「把 IR 的值语义映射到 wasm」，归入第 2 步**（§3.5）。**必须与 1.5a 用不同的名字**，否则计划里会一直写着「1.5a 便宜」 |

**1.5a 的处置：已核实，无可修（2026-10，实现会话实测）。** 三条原判据的现状：

1. 原判据①「真值表在解释器与 wasm 两侧逐格相同」——**已由实测满足**：18 个标量真值用例 **18/18 相同、0 分歧**；AOT 侧 `aot_native.test` **104 cases (86 equivalence, 2 pinned divergences, 6 runtime errors, 10 refusal)、0 failures**。⇒ **加不了新信息**（标量一致已由两个既有测试文件断言）。
2. 原判据②「故意改错 `vm_truthy()` 一格必须变红」——**无对象**：非标量在两侧都跑不起来，没有尚未被钉的行为可钉。
3. 原判据③「`selfhost_compare` 的 skipped 数不增加」——**无需触发**（未新增覆盖）。

⇒ **1.5a 的正确记录是「已核实：wasm 真值语义与 `vm_truthy()` 一致；非标量未实现且被诚实拒绝」，而不是「做完了」。** 关键区分（实现会话要求照此记）：三个 `grep -c` 的 **0 是「该特性不存在」，不是「存在但没接上」**；而且**拒绝这一侧是有牙的**——`tools/aot_native.test.py:211` 的 `REFUSAL` 十条（含 `list_literal`/`dict_literal`/`index_access`/`builtin_call`）与 `tools/wasm_backend.test.py:74` 的 `REJECT_CASES`（`("strings", 'say "hello"\n', "strings are not supported")`）⇒ **今天不存在「静默答错」的路径**。

**顺带记录一处「一个情形两个答案、只记了一个」（不是缺陷级，但形状正是本档在追的那个）。** wasm 对「程序里出现字符串」有**两条不同的拒绝消息**：`say "hi"` → `src/compilation/wasm_backend.c:1429` 的 `strings are not supported by the wasm MVP subset`（**已钉**）；`s = "hi"` → `:1353` 的 `expression type %d not supported by wasm MVP subset (strings/collections need the interpreter)`（**未钉**）。两条都拒绝、都退出 1，故不是缺陷；已交由 `vivid-anchor`（`tools/*.test.py` 是它的 lane）。

**1.5b 的处置：已由人类裁定（2026-10）——走「AOT 改吃带 `Value` 语义的同一份 IR」⇒ 1.5b 不做。**

`NV` 是 AOT 的**第二套值表示**。§3 要的是「所有后端消费**同一份 IR**」。这个岔口原先必须由人定，因为它决定 1.5b 是「必要前置」还是「白干」；**现已裁定走 A**：

| 第 2 步的走法 | 1.5b 的处置 | 裁定 |
| --- | --- | --- |
| A：AOT 改吃**带 `Value` 语义的同一份 IR** | `NV` 被整体替换 ⇒ **不必做**；AOT 改为链接 `Value` 运行时 | **✅ 已选（2026-10，人类裁定）** |
| B：第 2 步**只统一控制流**，AOT 保留自己的值表示 | 要扩 `NV` 的 tag 空间，**那时**它才是前置 | 未选 |

**裁定的三条后果（写下来，免得下次有人又去扩 `NV`）。**

1. **不要在 `NV` 上加 `NV_NIL` / `NV_STR`** —— 那是给一套第 2 步要整体删掉的值表示添砖，正是本档 `:10-12` 记的那个病：同一个语义多出一个生产点。
2. AOT 的活从「扩 `NV`」变成「**让 AOT 链接 `Value` 运行时**」，即 AOT 生成代码的操作数类型从 `NV` 换成 `Value`。这**更大**，但与 §3 同向，不是弯路。
3. `tools/aot_native.test.py:187` 的 `func_no_return` 分歧（解释器 `nil` vs AOT `0`）**归入第 2 步**解决，**不再作为 1.5b 的判据**。

**留档（若将来回退到走法 B 才需要）**：`NV` 能表示 `nil` 与字符串之后，`tools/aot_native.test.py:187` 的 `func_no_return` 分歧应当**消失**，或写明它为何与值表示无关而必须继续钉死。

**风险与不做。** 两半都要动引擎源码，**本文档只写不改**（见 §9 第 9、10 条）。不在这一步里顺手做 §3 的字节码收敛——两件事混在一起，失败时无法归因。也**不要**为了让三后端「看起来一致」而去改 `vm_truthy()`：它是唯一语义来源，改它等于把分歧换个地方发生。

### 6.2 B″ + 契约式双层判定（§3.7 / §3.8）新增的判据

§3.8 的裁定给这一步加了**三项证明义务**。它们**不在第 0–5 步的任何一条里**，必须单独列，否则「双层判定」会以「两层都在判断、没有东西说清谁对」的形状落地——那正是 §1 在治的病。

| 判据 | 内容 | 可证伪形式 |
| --- | --- | --- |
| **保守性** | 编译器标了类型的寄存器，运行期实际类型必须落在该编译期集合**内** | 差分测试里对每个被标注的寄存器断言 `type_of(R[i]) ∈ CompilerSet`；**故意把某处 `CompilerSet` 收窄成不含实际值的一格，该测试必须变红** |
| **主从（`OP_BE` 的位置）** | 静态模式下 `OP_BE` **不得**出现在编译期已证明安全处；它只允许在「动态/`var` 模式 / 显式 `@runtime_check` / Debug 断言」三处 | **同一份 `.im` 两种模式**：静态模式产出的字节码里 `OP_BE` 次数为 **0**（或全部落在白名单三类），动态模式产出里 `OP_BE` 保留；两条各自成立 |
| **单例一致（§3.7 原有）** | 编译器给出「单例标量」的寄存器，VM 运行期类型必须一致 | 凡标注为单例 `IM_TYPE_INT` 的寄存器，运行到该指令时 `R[i].type` 必须为 `VAL_INT`；一个反例即红 |

**三项都还缺一个前置测量**（**先量后改**，见 §9 第 17 条）：

1. **有多少今天合法的 `.im` 会因「无法证明 ⇒ 编译错误」而编译不过？** 今天 `be` 是纯运行期检查（`src/compiler/compiler.c:2232-2240` 无条件发射 `OP_BE`），编译器**从不拒绝**。这条规则**新增了一项编译器的否决权**，而**没有任何东西量过它的爆炸半径**——本档 §0.5 记过一条教训：「凡『改动会破坏 N 处』的断言都应先量后写」（那次实测真正受影响的只有 **1** 个文件）。⇒ **先对 `vtest/`、`projects/`、`mods/` 全量统计「有 `be` 且编译器无法静态证明」的实例数，再决定这条规则是否分期生效。**
   **→ 已量（2026-10，第五个人类裁定要求的那次），结论见 §3.9**：全仓 **346 个 `.im` 只有 10 处 `be`**，**10/10 的约束表达式都只依赖字面量 / 内建类型 / 同文件具名声明** ⇒ **上界 = 0 个不可解析实例**，按裁定自己的路径支持**直接采用 (A) 的口径**。**但 §3.9 的四条诚实边界必须一起读**（手工分类、正则只覆盖一种形状、只看了 `be` 使用点而非 `type` 声明、内建 `N` 未实跑验证）。
2. **`@runtime_check` 的语法与它落到哪些 opcode 上。** 今天不存在这个语法（我**没有**跑过 `grep -rn "runtime_check" src/ selfhost/ docs/`，**未测**）。
3. **断言里 `CompilerSet` 与运行期值的比较必须写成 `∈` 而不是 `==`**（§3.8 已说明理由：可能类型的集合与一个具体值之间没有 `==`）。**这条不是测量，是写法约束**——写错了断言就编不出来，或者编出来是恒真的。

---

## 7. (f) 风险与「不做什么」

### 不做

1. **不把 `src/vm/vm.c`（5,346 行）重写成 `.im`**。它是 substrate，且是唯一的引导运行器（第 1 类 + 第 4 类）。这是本设计明确的**上界**。
2. **不做 NaN-boxing / 特化 `Value`**。`docs/AUDIT.md` §5 已明确列为「明确不值得先做」。
3. **不只改其中一路的 `and`/`or`**。改一路会把分歧从「三处不一致」变成「两处不一致」，不减少可观测缺陷。
4. **不把注释当语义证据**。`src/compilation/aot_native.c:420-428` 的注释与行为相反；`src/compilation/wasm_backend.c:6` 声称 "mirror the C VM exactly" 却与 `:938` 冲突。这类「注释与代码互相担保」正是分歧能长期存活的原因。
   **第三例（2026-10 实测，比前两例更硬）**：本文档指定为**唯一语义来源**的 `src/vm/vm.c` 里，**有 58 行注释是乱码**（`锟斤拷` 一类损坏的编码），**其中 30 行落在 69 个指令体区间 `:3146-4680` 之内** —— 例如 `L_POP_REG` 上方那条分隔注释与 `L_CALL_BUILTIN` 体首的参数说明。**门禁的 `text-integrity` 阶段对此是绿的**，因为 `tools/check_text_integrity.py` 的判据是**「文件里没有 NUL 字节」**（该脚本 docstring 逐字写明它查的是 NUL），**不是「注释可读」**。⇒ 后果有两层：① 「读 VM 的 `L_X:` 块来填表」这件事，在 30 个格子上遇到的是**读不懂的注释**（代码本身可读，故不影响 §3.1.7 的分类，但会影响任何以注释为线索的读者）；② 一个**只查 NUL 的完整性检查**会给「注释已被损坏」发绿灯 —— 这是 §3.1.8 那个病的又一面：**被检查的东西与被断言的东西不是同一个**。
   **第四例（2026-10 新增，来源 `exact-lumen`，标为 [转述]）——这一例最该记住，因为它说的不是注释，是「检查」**：`tools/check_orphan_fixtures.py`（门禁第 12 阶段 `orphan-fixtures`，见 §3.1.8 第二个先例）**第一次跑反向验证时自己崩了**：`NameError: name 'CMAKE_SOURCE_DIR' is not defined` —— 提示串是 f-string，`${CMAKE_SOURCE_DIR}` 的花括号被当成替换字段。⇒ **一个「能发现孤儿」的检查，在真的发现孤儿时抛异常而不是报告：它存在、它退出非零、而它答的是另一个问题。** 修法是 `${{CMAKE_SOURCE_DIR}}`；**没有那次反向验证，不会有人发现**。
   ⇒ **这四例合起来给出本设计的一条硬规矩：一个从未在真实反例上跑过的检查不是证据。** 所以 §3.1 与 §6 的每条判据都写成「**故意改错必须变红**」/「**从 0 变成 69**」，而不是「加一个检查」——**一个只被正向跑过的检查，与被损坏的注释、与只查 NUL 的完整性检查，是同一类东西：被检查的对象与被断言的对象不是同一个。**
5. **不在本设计内改动门禁脚本**（`tools/**` 不属本文档写域）。

### 风险

| 风险 | 机制 | 缓解 |
|---|---|---|
| IR 收敛改变寄存器分配 → 字节级 parity 破裂 | 后端改吃字节码后寄存器编号来源改变 | 判据用 **opcode 序列**（`ops_only()`）而非字节相同（§4.2） |
| `Value` 宽度统一会波及 ABI / 序列化 | `ROADMAP_3.1.md:28` 的可逆性要求 | 宽度变更必须与 BigInt / 窄化设计同批，且带 ABI 版本号 |
| 删 `OP_AND` 会破坏链式比较 | `src/compiler/compiler.c:859` 是链式比较的 emit 点且语义需要布尔 | `OP_AND` **保留**；`OP_OR` 由 `:689` 发射，也已不是死指令（§1.4、§0.5） |
| `.im` 与 C 两个编译器双维护再次漂移 | `AUDIT.md` §5 的 O11 已把「`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致」列为风险② | §4 的持续对比判据；这正是本设计要根治的问题 |

---

## 8. 与 `docs/AUDIT.md` §5 的一致性声明

**不冲突。** 本设计与 §5（`docs/AUDIT.md:328-386`，当前为 **O0–O13**，含已完成的 O3）的关系：

- **O0**：本设计**当时采纳 O0 选项①**（值语义胜出），并把「决定」落到 §3.4 的唯一语义表上。**该方向已被用户裁定推翻为布尔语义**（§0.5、`docs/STATUS.md:2526-2531`）；但 O0 的「必须先把决定写进唯一一张表」这一要求不受影响，且已落地。
- **O1 / O2**：§6 第 4 步即 O1 的宽度版本；本设计指出 O1 的根因比 `%` 更广（§1.3 的 `Value` vs `NV` 宽度），O2（整数溢出可诊断）与 §5.3 的 ABI 宽度标签是同一件事的两个面。
- **O11**：`docs/AUDIT.md:368` 把「`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致」列为**风险①**；本设计把它当作**待根治的缺陷本身**（§1.1 证明 `.im` 重写不消除分歧）。两者方向一致，本设计是该风险的解法。
- **O13**：`docs/AUDIT.md:372` 诊断 wasm 后端「算术发射质量差」（`arith` 只有 2.72× vs AOT 155×），怀疑「每次算术都要把 `Value`（32 字节）装箱再拆箱」。**这是 §3 的第二个独立论据**：wasm 后端既因逐个 AST 重判语义而分歧，也因缺少统一的 IR 层而无法做跨后端的发射质量改进。两件事同一个根因。
- **§5 末尾三条 `DIVERGENCE`**：§6 第 5 步照其要求「修好后重新审视」（`docs/AUDIT.md:378-386`）。

**唯一需要 Lead 裁决的张力**：O0 说无论选哪条都要在 `docs/API.md:90` 写明返回值。本设计当时建议写明**值语义**（即返回操作数），理由是 AUDIT 论证改解释器会静默改变现有脚本结果（如 `name or "anonymous"`）。**该建议未被采纳，裁定为布尔语义**（§0.5）。事后看，这条建议的代价估计**过重**：实现方先量了爆炸半径，真正会变的值选择只有 **1 个文件**（`t_sugar_desugared.im:7` 的 `k = 0 or 1`）且它不被任何 CTest 或门禁引用 ⇒ 门禁零回归（`docs/STATUS.md:2470-2479`）。**教训：本设计的「代价」判断当时是读码推断，没有像实现方那样先量一遍；凡是「改动会破坏 N 处」的断言，都应当先量后写。**

---

## 9. 待批准变更（本文档只写不改）

**本次未修改任何引擎源码。** 以下变更必须由 Lead 批准后另行执行：

| # | 位置 | 建议变更 | 依据 |
|---|---|---|---|
| 1 | `src/compiler/compiler.c:648-672` | ~~`and`/`or` 降级统一为值语义~~ → **已按布尔语义实现**（§0.5） | §1.1 #1 |
| 2 | `src/compilation/aot_native.c:420-428` | 删除布尔发射，改按字节码查表 | §1.1 #4、§3.2 |
| 3 | `src/compilation/wasm_backend.c:781-783`、`:790-794` | 删除 `cg_cond`（`:778`）的 AND/OR 递归 | §1.1 #5、§3.3 |
| 4 | `src/vm/vm.c:3486-3497` | `L_AND` / `L_OR` 语义随唯一语义表定夺 | §1.1 #3、§2 的误判点名 |
| 5 | `src/compiler/bytecode.h:12` | `OP_AND`/`OP_OR` 去留 —— **§0.5 复核：两者已是活指令**（`:689`、`:859`），不再是「删死指令」 | §1.4 |
| 6 | `src/vm/vm.h:40-45` vs `src/compilation/aot_native.c:174` | ~~宽度契约统一（32 位 vs 64 位）~~ → **已解决**：匿名 union 使 `ival` 为 64 位而 `sizeof(Value)` 仍 32（§0.5） | §1.3、§5 |
| 7 | `src/compilation/wasm_backend.c:9` | 修正与代码矛盾的 `L_MOD` 描述 | §1.2 |
| 8 | `docs/API.md:90` | 写明 `and`/`or` 返回操作数还是布尔 —— **已裁定为布尔**（§0.5） | AUDIT §5 O0 |
| 9 | `src/compilation/aot_native.c:174-177`（`NV` / `NV_INT` / `NV_FLT` / `NV_BOO`）、`:193`（`nv_tru`） | **不是补分支，是补表示**：三个 tag 全部只描述标量，无 nil/字符串/容器。**归入 §2 第 ⑤ 类；且 2026-10 人类已裁定「不做」——不要扩 `NV`，改走第 2 步（AOT 链接 `Value` 运行时）** | §0.5.1 第 2 条、§6.1 更正二、§6.1 末段 |
| 10 | `src/compilation/wasm_backend.c:834-847`（`cg_cond` 尾部） | **语义一行不改**（`else 1` 已与 `vm_truthy()` 的 `default: return 1` 逐字对应）。**但原写「只加可达性与三后端真值表 CTest」已被实测推翻**：wasm 连字符串字面量都产不出来 ⇒ 「可达性」= **在 wasm 实现非标量**（1.5c，大，归 §2 第 ⑤ 类）；标量真值表已由既有测试断言（18/18 + `aot_native.test` 104 cases/0 failures），**加不了新信息** | §0.5.1 第 2 条、§6.1 更正一、§6.1 的 1.5a/1.5c |
| 11 | `src/compilation/wasm_backend.c:66`（`#define SLOT_BYTES 16`）vs `src/vm/vm.h:40-45`（`Value` 32 字节）vs `src/compilation/aot_native.c:174`（`NV`） | **wasm 的值表示同样未被裁定**：裁定 A 只覆盖 AOT ⇒ **已由人类裁定 A′（2026-10）**：wasm 也吃带 `Value` 语义的同一份 IR、**不要单独扩 `SLOT_BYTES`**（§3.5）。另：**同文件 `:4` 的头注释说「8-byte slot」，与 `:66` 的 16 字节矛盾**，必须一并修；引用行号时必须引定义处、不能引文件头 | §3.5、§7 第 4 条 |
| 12 | `src/vm/vm.c:5208-5266`（`vm_disasm_ins` 的 switch，`default:` 在 `:5265`） | **补全反汇编器缺的 21 个 opcode 命名**（清单逐字见 §3.1.6）。**便宜、独立、且应在第 2 步之前做**：缺的 21 个恰好是集合/区间/闭包捕获/线程——正是 §3.1.4 与 1.5c 最难的一半，而人类可读 dump 是调试它的主要手段。**判据**：`grep -c 'case OP_' src/vm/vm.c` 在 `:5208-5266` 区间内从 **48** 变 **69**；且断言「反汇编器的 case 集合 == `bytecode.h` 的枚举集合」（差集为空，正反两向都查）。**注意不要与 `src/main.c:675-681` 的数字 dump 混淆**——那条路故意不经过反汇编器，是稳定判据，**不要改它** | §3.1.6 |
| 13 | `docs/SYNTAX.md` §7.1 的 **D14** 裁定 | **登记一个口径缺口，不改 D14**：D14 是二分的（操作共享状态 ⇒ 抛 `type_mismatch`；只产出值 ⇒ 按已裁定定义值作答），而 §3.1.7 实测出 **11 个 opcode 属「纯值但会抛」**（`ADD` `CONCAT` `SUB` `MUL` `DIV` `NEG` `LT` `GT` `LE` `GE` `MOD`，全部调 `vm_throw`）。D14 讲的是**内建**、这 11 个是**运算符**，故未必冲突；但 `OpSemantics` 表**必须同时带 `touches_state` 与 `can_raise` 两列**，否则后端作者会把 `+`/`lt` 映射错。**请 D14 的所有者（不是本文档）决定要不要补第三个分支** | §3.1.7 |
| 14 | `OP_STORE_CAPTURE`（`src/vm/vm.c:3952-3955`）、`OP_POP_REG`（`src/vm/vm.c:3761-3767`） | **两个零生产点 opcode：声明、分派、实现齐全，全仓库没有任何东西发射它们。** 它们是一对**没人用的第二套捕获协议**（今天真正在用的是 `OP_PUSH_REG` × N + `OP_MAKE_FUNC` 自己从 `t->stack` 取值）。⇒ **要么删、要么接上——这是人的选择，不是表能回答的。** 无论选哪条，**§3 的 69 行表都不该把它们算进「后端要映射的 opcode」**。**判据**：`LC_ALL=C comm -23 <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/bytecode.h \| sort -u) <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/compiler.c \| sort -u)` 今天输出恰好这两行 ⇒ 裁定后该输出应为**空**（若删）或**不再含它们**（若接上，则 `compiler.c` 侧必须出现发射点） | §3.1.9、§3.1.8 |
| 15 | `src/compiler/bytecode.h:69-74`（`RegInstruction`）、`:87-128`（`Bytecode`） | **裁定 A′ 未覆盖的第三个岔口：类型信息放不放进 IR。** A′ 的措辞是「按 IR 的类型信息落值」，而 **IR 今天没有类型字段**（`{ OpCode op; int r1, r2, r3; }` 全是下标）。**A″**＝IR 保持无类型、后端照抄 VM 的运行期动态分派（忠实，但 wasm/AOT 不做类型特化，`docs/AUDIT.md` §5 O13 的性能账在 §3 里还不上）；**B″**＝IR 带类型标注（可特化，但**编译器要生产并维护类型** ⇒ 立了第二个类型决定点，把 §1 的病搬到「编译期 vs 运行期」这一维）。**这不是顺序问题，是架构问题，必须人裁定。** 本文档不替人做 —— **已裁定（2026-10）：`var` 动态、其余静态 ⇒ 走 B″，见 §3.7** | §3.6、§3.1.4、§3.7 |
| 16 | `src/compiler/bytecode.h:64-66`（`INIM_BYTECODE_VERSION 3` / `INIM_ABI_VERSION 2`）、`:69-74`（`RegInstruction`） | **裁定 B″ 的格式后果**：IR 要携带类型 ⇒ `RegInstruction` 加字段 ⇒ **`.inim` 磁盘格式改变 ⇒ 版本号必须升**，且三个 `.inim` 读者（`src/compiler/bytecode.c`、`src/main.c`、`src/compilation/deps.h`）必须**同批**改。另：**`var` 需要 IR 里有一个「动态」类型值**（语义等于 `im_typeset_any()`），使 `var` 走与 A″ 相同的运行期分派 ⇒ **B″ 的实现里同时存在静态特化与动态分派两条路，它们的边界就是新的必须被断言的东西**。**不要在没有 §6 那条新证明义务（编译器单例标量类型 vs VM 运行期 `R[i].type`）的情况下先加字段。** | §3.7、§3.6 |
| 17 | `src/compiler/compiler.c:2232-2240`（`case STMT_BE` 无条件发射 `OP_BE`）、`src/compiler/bytecode.h:69-74` | **裁定「契约式双层判定」（§3.8）的实现后果，三项，都还没做**：① **静态模式下「编译期无法证明 ⇒ 编译错误」是一条今天不存在的语言规则** —— 今天 `be` 是纯运行期检查、编译器从不拒绝，这条规则**新增了一项编译器的否决权**，而**没有任何东西量过有多少 `.im` 会因此被拒** ⇒ **先量后改**（**已量，见 §3.9：346 个 `.im` 只有 10 处 `be`，10/10 的约束只依赖字面量 / 内建类型 / 同文件具名声明 ⇒ 上界 0 个不可解析实例**）；② **`@runtime_check` 是新语法**，今天不存在（我**没有**跑过 `grep -rn "runtime_check" src/ selfhost/ docs/`）⇒ 需要先定它的语法与它落到哪些 opcode 上；③ **`CompilerSet == RuntimeSet` 这个等式不能直接实现** —— 一个可能类型的集合与一个具体值之间没有 `==`，**可实现的形状是成员关系 `type_of(实际值) ∈ CompilerSet`**，等式只在编译期集合恰为单例时等价。**⇒ 落地前必须先把断言写成 `∈`，否则那条 Debug 断言没有可写的形式。** 另：§3 的 69 行表需**新增一列**「该 opcode 在静态模式下是否可能被编译期消除」（`can_raise` 与 `producers` 两列都不表达这件事）。**判据（可证伪）**：对同一段 `.im`，**静态模式产出的字节码里 `OP_BE` 出现次数为 0**（或全部落在「动态模式 / 显式 `@runtime_check` / Debug 断言」三类白名单位置），**动态模式产出里 `OP_BE` 保留** —— 同一份源码两种模式，产出各自符合各自的规则 | §3.8、§6、§3.7 |

---

## 10. 证据诚实度总表

| 类别 | 内容 |
|---|---|
| **[读码]（本会话逐行读过）** | 六个 `and`/`or` 决定点全部行号；三个 `%` 决定点；`OP_AND`/`OP_OR` 全部引用点；`Value` 与 `NV` 的字段与宽度；`src/compilation/wasm_backend.c:1-16` 头注释与其 `:938` 的冲突；`src/compiler/compiler.c:815-835` 链式比较；`tools/selfhost_compare.py:87-96` 的 `ops_only()` |
| **[实测]（本会话执行命令）** | **2026-10 复核轮新增**：① `grep -rn "emit_from_bytecode" src/` → **零命中**（§0.5.1 第 1 条、§0.5 的 §3 行）；② `grep -n "OP_AND\|OP_OR" src/compiler/compiler.c` → `:689`、`:859`（§1.4、§0.5）；③ `grep -n "DIVERGENCE" tools/aot_native.test.py` → 列表在 `:187`，**仍 2 条**（§0.5.1 第 5 条）；④ `ls src/vm/bytecode.h` → **No such file or directory**（§1.4）；⑤ `grep -n "vm_truthy" src/vm/vm.c src/vm/vm.h` → **`:347` / `:362`**（与 `docs/STATUS.md` §10.53 记的 `:293` / `:356` **不一致，以实测为准**）；⑥ `grep -n "define NV_\|} NV;" src/compilation/aot_native.c` → `NV` 在 `:174`、**三个 tag** `NV_INT 0`/`NV_FLT 1`/`NV_BOO 2` 在 `:175-177`（**本人第一次只数到一个 tag，经 exact-lumen 复核更正**）；`grep -n "\.t\b"` 排除 `\.t == NV_FLT` 后只剩 `:179-181` 的赋值、`:188` 的 `a.t != NV_FLT` 与 `:311` 的 `t == NV_BOO` ⇒ **无旁路**（§6.1 更正二）；⑦ `grep -n "^#define TAG_" src/compilation/wasm_backend.c` → `TAG_NIL 0` / `TAG_INT 1` / `TAG_FLOAT 2` / `TAG_BOOL 3` / **`TAG_STR 4`** / **`TAG_ARR 5`**（`:58-63`）。**仍未构建、未跑任何执行通道、未跑 `tools/im_diff_fuzz.py`、未实测 `tools/selfhost_compare.py` 的 skipped 数。** |
| **[实测] 行号与规模审计（2026-10，第二轮）** | 本档多处行号是取证当时（`8c9f0f3` 附近）记的，代码在其上方增长后**全部漂移**；本轮逐条重测并更正 **16 处**：`Value` `vm.h:24-26` → **`:40-45`**；`NV` `aot_native.c:172` → **`:174`**（tag 在 `:175-177`、`nv_asi` `:182` → **`:187`**）；AOT `EXPR_BINARY` `:332-351`/`:334-339` → **`:418-430`**（特判 `:420`、发射 `:425-428`）；wasm `cg_cond` `:766-789` → **`:778`**（AND/OR 递归 `:769-786` → **`:781-783` / `:790-794`**）；wasm 导入表 `:13-15` → **`:14-15`**；`vm.c` 的 `L_AND`/`L_OR` `:3166-3179` → **`:3486-3497`**；`OP_AND`/`OP_OR` 引用点 `:2826`/`:4873` → **`:3066-3067` / `:5211-5212`**。规模数同样漂移：`src/vm/vm.c` 5,007 → **5,346 行**；`selfhost/**/*.im` 2,316 → **2,338 行**（仍是 48 个文件）；`src/**/*.c` 48,491 → **50,340 行**（112 个文件）；全仓已跟踪 `.c` 51,236 → **53,511 行**（116 个文件）。命令：`grep -n` / `sed -n` / `wc -l` / `git ls-files '*.c' \| xargs cat \| wc -l`。**审计只改本档文字，未改任何引擎源码。** |
| **[实测]（本会话，2026-10 第三轮）** | ① `grep -n "strings are not supported\|expression type %d not supported\|strings/collections need the interpreter" src/compilation/wasm_backend.c` → **`:1429`**（`say "hi"` 的拒绝）与 **`:1353`**（`s = "hi"` 的拒绝）⇒ 一个情形两条消息（§6.1 末）；② `grep -n "say \\\\\"hello\|strings are not supported" tools/wasm_backend.test.py` → **`:74`**（前者**已钉**）；③ `grep -n REFUSAL tools/aot_native.test.py` → 定义在 **`:211`**、消费在 `:365`；④ `sed -n '4p;66p' src/compilation/wasm_backend.c` → `:4` 说「8-byte linear-memory slot」、`:66` 是 `#define SLOT_BYTES 16` ⇒ **注释与定义矛盾**（§7 第 4 条、§3.5）。 |
| **[实测]（本会话，2026-10 第四轮：IR 与 opcode 覆盖）** | ① opcode 表 **69** 个：`awk '/^typedef enum \{/{f=1;next} /^\} OpCode;/{f=0} f' src/compiler/bytecode.h \| sed 's,/\*.*\*/,,' \| grep -oE '\bOP_[A-Z0-9_]+' \| sort -u \| wc -l` → **69**；② VM 的 `case OP_` 也是 **69**，且**双向一一对应**（两个 `comm` 都是空集）；③ 各消费者引用的 opcode：`src/compilation/aot_native.c` → **0 个 token、`grep -c Bytecode` = 0、`grep -c RegInstruction` = 0**；`src/compilation/wasm_backend.c` → **1 个 token**（`:1500` 的注释 `(OP_LT double compare)`）、`Bytecode`/`RegInstruction` 都是 **0**；`selfhost/compiler.im` → **164 个 token（distinct 48）**；`selfhost/eval.im` → **0**；④ IR 的类型与版本实测：`src/compiler/bytecode.h:69-74` 的 `RegInstruction`、`:87-128` 的 `Bytecode`、`:64-66` 的 `INIM_BYTECODE_MAGIC`/`INIM_BYTECODE_VERSION 3`/`INIM_ABI_VERSION 2`；⑤ `.inim` 的读者实测为 `src/compiler/bytecode.c`、`src/main.c`、`src/compilation/deps.h`；⑥ VM 寄存器堆是动态类型的：`src/vm/vm.c:2912` `Value *R = t->R;`（**§3.1.4 的立论依据**）。 |
| **[实测]（本会话，2026-10 第五轮：分派双射 + 反汇编器缺口）** | ① **opcode 原始 token 是 70 个、去重后 69 个**——多出来的那个是 `src/compiler/bytecode.h:56` **块注释里**提到的 `OP_LOADK_INT`（在描述 `:58` 的 `OP_LOADK_I64`）；⇒ 计数必须 `sort -u`，且**单行 `sed 's,/\*.*\*/,,'` 删不掉跨行块注释**（这是本轮第一次数错的成因）；② **VM 分派是 goto-threaded**：`src/vm/vm.c:3066` 的 `switch (ins.op)` 里是 **69 个 `case OP_X: goto L_X;`**（`:3068-3138`）+ `default:`（`:3139-3143`，坏 opcode → `[vm] bad opcode %d at ip %d` + `t->running = false`），**真正的指令体是 `L_X:` 标签块**（第一个是 `:3146` `L_MOV:`）⇒ 我最初按「case 块」抽体会得到 len=1，**是抽取方法错，不是代码怪**；③ **双射成立**：`case OP_X: goto L_X;` 抽出 69 对、`^ *L_[A-Z0-9_]+:` 抽出 69 个，**两个方向的差集都是空集** ⇒ 无死 opcode、无孤儿 label；④ **反汇编器 `src/vm/vm.c:5205-5267` 的 `vm_disasm_ins` 只有 48 个 case**（switch `:5208`、`default:` `:5265` 输出 `OP_%d`），**缺 21 个**（清单见 §3.1.6）；面向 `.im`（`builtin_dbg_disasm` `:5316`、注册 `:5365`）；⑤ **验证要用的那条路不依赖它**：`src/main.c:675-681` 的 `printf("%d,%d,%d,%d\n", …)`，注释 `:675` 写「Kept byte-comparable on purpose」⇒ CLI `bytecode` 子命令输出**纯数字**。 |
| **[实测]（本会话，2026-10 第六轮：69 行骨架逐块读出）** | ① 69 个指令体 = `L_X:` 标签块，**连续覆盖 `src/vm/vm.c:3146-4680`**（`^ *(L_[A-Z0-9_]+):` 匹配 69 个，第 k 块区间 = 第 k+1 标签行 − 1）；② 按「是否写帧外状态」手工分类得 **P=37 / C=3 / S=29 = 69**；③ 独立第二轴 `can_raise`（块内含 `vm_throw`，helper 在 `src/vm/vm.c:2481`）= **16 个**；④ **交叠空格的发现**：`P ∩ can_raise` = **11 个**（`ADD` `CONCAT` `SUB` `MUL` `DIV` `NEG` `LT` `GT` `LE` `GE` `MOD`），`S ∩ can_raise` = **5 个**（`INDEX_SET` `STORE_GLOBAL` `CALL_BUILTIN` `BE` `THROW`），`C ∩ can_raise` = 0；⑤ 12 行的机制与名字不符（`DECLARE` 写 `vm->limit_*`、`RECORD` 写 `vm->record_*` 并取 `VM_LOCK`、`BE` 取全局分片锁 + `vm_global_grow`、`LOAD_GLOBAL` 条件加锁、`INDEX_GET` 的 dict 读加锁、`NEW_SET`/`SET_INTERVAL` 是**新建**而非共享写、`SAY` 绕过平台层走 `src/vm/vm.c:160-161` 的 POSIX shim），逐行见 §3.1.7；⑥ 命令：`python3` 按标签区间切块 + `grep -c vm_throw`；`sed -n '<区间>p' src/vm/vm.c` 可逐格复核。**分类是手工判定，不是自动推导**（§3.1.7 诚实边界）。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 第二个先例（§3.1.8「拒绝不了就比对」）：① **四处文档声称有 CTest 覆盖、四处都没有** —— `docs/API.md` 第 2.1 节标题逐字「证据（CTest）」，`:114` 把 `lint_case_missing_default_v04.im` 写成「相关 CTest」、`:115` 把 `lint_case_exhaustive_v04.im`（**文件名，不是测试名**）与四个真实测试名并列；`docs/REQUIREMENTS_ANALYSIS.md:177` 与 `docs/STATUS.md:286` 把 `tools/migrate_report.py` 与 `bindgen_regression`／`scan_tools_regression` 并列，而 `tools/` 下**没有** `migrate_report.test.py`；② **根因**：`CMakeLists.txt:733-743` 的五个 `lint_case_*` 是**手写列举**（`:744` 注释自己数着「The five lint_case_* tests above …」），**仓库里没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**；③ **反向验证**：把 `CMakeLists.txt` 退回 `HEAD` ⇒ `2 orphaned input(s) out of 110 checked` + 逐条点名 + `exit 1`；④ **普查**：67 个 `vtest/*.im` 中 5 个未被 `CMakeLists.txt` 提到（两个真缺口、三个合法）、30 个 `tools/*.test.py` 与 13 个 `tools/*.test.js` **0 孤儿**；⑤ **新设施**：注册 **#135** / **#136**（各带 `PASS_REGULAR_EXPRESSION` + 一条断言对方警告不出现的 `FAIL_REGULAR_EXPRESSION`）、新增 `tools/check_orphan_fixtures.py` 与**门禁第 12 阶段 `orphan-fixtures`**；⑥ **该脚本自己崩过**：`NameError: name 'CMAKE_SOURCE_DIR' is not defined`（f-string 里的 `${…}` 被当替换字段），修为 `${{CMAKE_SOURCE_DIR}}`；⑦ **门禁**：`GATE_RC=0`、`gate: OK — every stage passed (12/12 stages ran).`、**`100% tests passed, 0 tests failed out of 136`**、`0 skipped`。**我未独立复核以上任何一条**；其中「12 阶段 / 136 测试」与我 §3.1.8 记的 `EXP_CTEST = 134` **不一致**（该值在 `tools/gate.sh:54`，本轮门禁正在被 merge 修改，**以仓库现状为准，我未重测**）。 |
| **[转述，来源：人类裁定，非本会话实测]** | 2026-10 的**第四个人类裁定**（§3.8「契约式双层判定」）：走 (ii) 的强化版 —— 编译期保守推断（`CompilerSet ⊇ RuntimeSet`）、运行期契约执行（`OP_BE` 是编译期未竟事业的兜底）；Debug/Strict 模式下在已证明路径插等价断言，断言失败按**编译器级错误**处理；按执行模式分层（静态模式强制 (ii)，动态/`var` 模式**退化为 (iii)**，跨边界插显式转换检查）；否决 (iii) 的理由是**形式化验证与 Proof 会失去根基**、集合论类型沦为摆设。**本行的内容是转述，裁定以人的原话为准**；§3.8 里我另外标出了**三处「裁定措辞与可实现的断言之间还有距离」**（`==` 应写成 `∈`；`OP_BE` 在裁定内部有两个角色、需按「恰好三处」的读法落地并请人确认；「无法证明 ⇒ 编译错误」是新语言规则且**未量过**会让多少 `.im` 编译不过），**那三条是我读裁定后的判断，不是裁定的内容**。 |
| **[实测]（本会话，2026-10 第九轮：类型代数零消费者 + 两个集合实现）** | 裁定 B″（`var` 动态、其余静态）之后的复核：① **类型代数存在且完整**：`src/types/typeset.c` **248 行**、`src/types/typeset.h:37-45` 导出 `im_typeset_empty`/`any`/`enum`/`int_interval`/`union`/`intersection`/`difference`/`complement`，`:47-56` 导出 `contains`/`subset`/`intersects`/`kind`/`cardinality`/`materialize_enum`；`src/types/` 共 957 行；② **它被构建、被测试**：`CMakeLists.txt:417` 编进引擎、`:157-159` 有 `typeset_probe` CTest，`src/types/typeset_probe.c` 断言的正是 ROADMAP_3.1 点名的 `im_typeset_int_interval(0,255,true,true)`（u8 窄化）与区间交、枚举并；③ **它零消费者**：`grep -rl "im_typeset" --include=*.c --include=*.h src/ \| grep -v "^src/types/"` → **0 个文件**（全仓只在 `src/types/` 内 9 个文件里出现）；`grep -c "typeset\|TypeSet\|type_of\|infer" src/compiler/compiler.c` → **0**；④ **今天类型是运行期的值**：`src/compiler/compiler.c:2224-2231` 的 `case STMT_TYPE` = `register_global(comp, tname)` + `compile_expr(comp, stmt->typeStmt.set)` + `emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0)`；`STMT_BE` 同形（`:2232-2240`，发射 `OP_BE`）；⑤ **`OP_BE` 用 VM 自己的集合，不用 `im_typeset_*`**：`src/vm/vm.c:4262-4308` 内实测命中 `vm->be_bound[g] = sidx >= 0 ? sidx + 1 : 0;` 与 `set_contains(vm, sidx, &init)`；VM 的集合 API 面为 `vm_set_new`/`vm_set_add`/`vm_set_add_comp`/`vm_set_add_comp_dedup`/`vm_set_cur_thread`/`vm_set_free_objs`/`vm_set_slot`/`vm_set_to_array`。**诚实边界**：③ 是字面结论（证明的是「除 `src/types/` 外没有这个字符串」）；⑤ 是**命中行**，我**没有通读 `L_BE` 全文**；④ 是从**发射形状**读出的，**没有实跑 `.im` 观察全局**；**未检查 `mods/`、`projects/`、`vtest/` 里有没有人用 `type`**。 |
| **[实测]（本会话，2026-10 第八轮：零生产点 opcode + 注释损坏）** | ① **零生产点**（`exact-lumen` 提出、**我独立复跑确认**）：`LC_ALL=C comm -23 <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/bytecode.h \| sort -u) <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/compiler.c \| sort -u)` → **恰好 `OP_POP_REG` 与 `OP_STORE_CAPTURE` 两行**；反向差集 `comm -13 …` → **空**；`grep -c 'emit(comp->curBC, OP_' src/compiler/compiler.c` → **330**；枚举 **69** 个名字 / `compiler.c` **67** 个；② `grep -rl '\bOP_STORE_CAPTURE\b' src/ selfhost/ tools/` → **只有 `src/compiler/bytecode.h` 与 `src/vm/vm.c`**（⇒ 零生产点）；`OP_POP_REG` 多一个 `selfhost/compiler.im:37` 的裸常量定义 `OP_POP_REG = 29`（全文件仅此一处）；③ 两个实现体实测存在：`src/vm/vm.c:3761-3767`（`L_POP_REG`）与 `:3952-3955`（`L_STORE_CAPTURE`）；④ **第二套捕获协议**的证据：`L_MAKE_FUNC`（`:3957` 起）自己从栈取捕获值 `Value *captured = &t->stack[t->sp--];`，而 `OP_PUSH_REG` 由 `compiler.c` 在 `:460`/`:562`/`:575`/`:587`/`:637-638`/`:879`/`:891`/`:903`/`:929` 等十余处发射；⑤ **注释损坏**：`grep -c '锟' src/vm/vm.c` → **58 行**，`awk 'NR>=3146&&NR<=4680 && /锟/' src/vm/vm.c \| wc -l` → **30 行落在指令体区间内**；`grep -n "vm\.c" tools/check_text_integrity.py` → 该脚本 docstring 只提到 `gui_mod.c`/`lexer.c`/`lexer.h` 三个文件的历史 NUL 缺陷，其判据（docstring 逐字）是 **NUL 字节**，故对乱码**无覆盖**。**诚实边界**：③「不可达」是从「零生产点」读出来的、**非运行时实测**（要造含该 opcode 的 `.inim` 才可真测）；② `grep -rl` **未扫 `mods/`、`projects/`、`vtest/`**；④ `enum − compiler.c` **只覆盖 C 编译器**这一个生产者，`selfhost/compiler.im` 的生产点是逐名字看的、**没做集合比对**。 |
| **[实测]（本会话，2026-10 第七轮：builtin 注册表先例，§3.1.8）** | ① **机制拒绝**：`grep -rn "is already registered" src/` → `src/vm/vm.c:1684` 与 `:1708`（两个注册入口各一份），守卫 `if (builtin_lookup(vm, name) >= 0)` 在 `:1683`/`:1707`，注释在 `:1676-1682`/`:1700-1706`（逐字 `One name, one handler.` … `Refuse the duplicate and name it, instead of losing it silently.`）；② **门禁断言**：`tools/gate.sh:140-151`（在 **ctest 阶段内部**）用 `grep -qF "is already registered"` 让阶段失败，注释逐字含 `one name with two answers` / `dead code that reads as live` / `no single test file can see it`；③ **计数断言**：`tools/gate.sh:132-139` 断言 `0 tests failed out of $EXP_CTEST`，`EXP_CTEST` 在 `:54` = **134**，注释 `Assert the count too, so that a dropped add_test( ) cannot pass silently.`；④ **逐平台重算**（按 `CMakeLists.txt` 真实源列表：base `:412-425` 27 个文件、WIN32 追加 `:427-436` 26 个、POSIX 追加 `:438-443` 27 个；正则 `vm_register_builtin(_full)?\s*\(\s*\w+\s*,\s*"([^"]+)"`）：WIN32 **431 次 / 430 distinct / 1 重名**，POSIX **241 / 240 / 1**；唯一重名是 `isolate_run`（`src/isolate_mod.c:227` 与 `:318`），**假阳性**——两处在 `#ifdef _WIN32` / `#else` 分支里，只有一个编译；⑤ `gui_fullscreen` **已不再重复**（`src/mod/gui_mod.c:3690` 是唯一注册点，`:3691-3692` 留注释说明曾重复）⇒ §1.53 登记项**已修**。**诚实边界**：我的正则只认字面量名字 + 简单首参，宏/变量传名会漏；`exact-lumen` 独立数出 WIN32 **398** / POSIX **128**，与我的 **431 / 241** 不一致，**差异未解释、不调和**。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 实现会话的只读测量：① 三条 wasm 程序 —— `say 1 + 2` **RC=0**、`say "hi"` **RC=1**（`error: wasm MVP subset: strings are not supported by the wasm MVP subset (line 1)`）、`s = "hi"; say s` **RC=1**（`error: wasm MVP subset: expression type 2 not supported by wasm MVP subset (strings/collections need the interpreter) (line 1)`）⇒ **wasm 连字符串字面量都产不出来**；② **18 个标量真值用例，解释器与 wasm 逐条相同（18/18、0 分歧）**；③ AOT 侧 `aot_native.test: 104 cases (86 equivalence, 2 pinned divergences, 6 runtime errors, 10 refusal), 0 failures`；④ `grep -c 'EXPR_ARRAY\|EXPR_SET\|EXPR_DICT'` 在 `src/compilation/aot_native.c` 与 `src/compilation/wasm_backend.c` 都是 **0**，`grep -rn 'emit_from_bytecode' src/` 也是 **0** ⇒ **这三个 0 是「该特性不存在」，不是「存在但没接上」**；⑤ **未新增任何 CTest**（没有尚未被钉的行为）。它声明**未改任何 wasm/AOT 源码**，本轮全是只读测量；18 个用例是它自选的、非穷举，且**未测 `NaN`**。 |
| **[读码，2026-10 复核轮补上]** | `docs/STATUS.md` §10.42（`:2464`）/ §10.49（`:2910`）/ §10.53（`:3095`）关键段，以及 §10.50–§10.56 的小节标题（`:2999`/`:3048`/`:3058`/`:3095`/`:3174`/`:3195`/`:3217`）；§10.56 全文（`:3217-3242`，含 `L_ADD` 与 `L_CONCAT` 的「逐字相同的不对称」）；`tools/aot_native.test.py:187-207` 的 `DIVERGENCE` 列表原文 |
| **[转述]（引用 AUDIT，非本会话实测）** | 150 例 fuzz → 41/150 = 27.3% 分歧（28 例 and/or、13 例 int32/`%`、0 例无法归因）；`%` 四组预测值；`(2147483647 + 1).type == float`；解释器 RSS 68.5 MB vs 原生 23.2 MB；AOT 快 7.7×–146.6×，`fib` 比手写 C++ 慢 14×；`tools/selfhost_compare.py` 的 `10 target(s) byte-identical, 23 skipped`（来自工作订单转述，本会话**未实测**） |
| **[读码，本轮补上]** | `docs/archive/RELEASE_0.5.0.md:12/23-28/48/102-106` 的 Native ABI 面、C ABI 类型映射、ABI 版本号、以及「三个后端共享同一字节码格式」这条与现状冲突的承诺；`docs/SYNTAX.md:551-556` 的 §7 分类（危险·静默 / 危险·误导 / 冗余 / 卫生），`:901` 「退出码经常区分不出对错，必须断言输出数值」 |
| **[实测]（本会话，2026-10 第十轮：`be` 爆炸半径普查，§3.9）** | ① **普查命令**：`grep -rnE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]' --include=*.im .`（覆盖全仓，排除 `build/`、`.worktrees/`、`.verify/`）→ **346 个受跟踪 `.im` 里只有 10 处 `be`**，逐条见 §3.9 的表；② **10/10 的约束表达式只依赖字面量枚举（4 处）/ 字面量区间（1 处）/ 同文件具名类型声明（3 处）/ 内建类型名（2 处）** ⇒ **上界 = 0 个不可解析实例**；③ 内建类型名单实测：`grep -n '"N"' src/compiler/compiler.c src/vm/vm.c` → `src/compiler/compiler.c:2864`、`src/vm/vm.c:1463`/`:1871`，且 `src/vm/vm.c:1880` `if (strcmp(name,"N") == 0) return 0;`（`:1881` `"Z"` → 1、`:1884` `"R"` → 24）⇒ **`N`/`Z`/`R` 等是内建类型名**；④ 语法来源：`docs/SYNTAX.md:546`（`name be <集合或表达式> [: init]`）、`:550`（称 `be` 是「仓库里最不常见的语句形式之一」，**10/346 实证了这句**）、`src/parser/parser.c:1357-1365`（`STMT_BE`）；⑤ **顺带发现一处文档与语法不一致**：`vtest/lint_case_membership_v04.im:2` 等用的是 `be Direction = "N"`（**`=`**），而 `docs/SYNTAX.md:546` 只写了 `:`。**诚实边界（四条，详见 §3.9）**：手工分类而非分析器输出；正则只匹配一种形状（漏别名/下标名/续行，误报未排查）；**只看了 `be` 使用点，没看 `type X = <运行期表达式>` 这种类型声明本身**（那才是 §3.7 的真问题）；「内建 `N` 可解析」是从名字名单读出的、**未实跑编译器**确认 `be N` 真在编译期求值。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 门禁已到 **12 个阶段**（新增第 12 阶段 `orphan-fixtures`），`GATE_RC=0`、`gate: OK — every stage passed (12/12 stages ran).`、**`100% tests passed, 0 tests failed out of 136`**、`0 skipped`、`check_text_integrity: 771 text file(s), 0 with NUL bytes.`；`docs/BOARD.md:62` 与 `docs/README.md:55` 的清单已同步为十二个阶段。**我未独立复核**；其中「12 阶段 / 136 测试」与我在 §3.1.8 记的 `EXP_CTEST = 134`（`tools/gate.sh:54`）**不一致，未调和**。 |
| **[待复核]** | 无（原两项已补读；`docs/SYNTAX.md` 各条 D/M 编号的现状我未逐条复核是否仍成立；§3.9 的 10 条 `be` 分类与 `=`/`:` 那处不一致待 `src/parser/parser.c:1357-1365` 复核） |

### 关于 Lead 的「wasm 与 AOT 一致」

据 **[读码]**，二者在 `%` 的 **int%int** 路径上确实同为 64 位（`src/compilation/aot_native.c:208-211` 与 `src/compilation/wasm_backend.c:938`）。但：
1. 这**只是读码，不是实测**；
2. wasm 的 **general** 路径（`:940-961`）反而先截断到 i32，**与 AOT 不一致**。

⇒ 准确的表述是「wasm 与 AOT 在 `%` 的 int%int 路径上一致」，而不是「wasm 与 AOT 一致」。
