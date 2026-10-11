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
| §1.1 `and`/`or` 六个决定点、2 值 : 4 布尔 | **已修**：统一为**布尔**语义（短路保留），判据换成三后端逐格比对 | `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2485`）` §10.42；`src/compiler/compiler.c:648-672`、`selfhost/compiler.im:254-268`、wasm 改为复用 `cg_cond`；AOT 本就正确、未动 |
| §1.1 的语义方向：本文档主张**值语义胜出**（O0 选项①） | **已被用户裁定推翻**：`and`/`or` 返回**布尔** | `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2547`）-2531`（该节明写「与本节冲突时以本节为准」）；用户裁定见 `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2491`）` |
| §1.2 `%` 三决定点、32 位截断 | **已修**：`im_dbl_to_i64()` 饱和、零检查前移、`y == -1` 特判、AOT 新增 `nv_die_division_by_zero()`、wasm 改用 `e_trunc_sat_i64`；16/16 三后端一致 | `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2485`）` §10.42 |
| §1.3 宽度契约分裂（`Value` 整数槽 32 位 vs AOT `NV` 64 位） | **已修，且正是按本文档 §2 的约束修的**：用**匿名 union** 把 `ival` 改 `long long` 而 `sizeof(Value)` 仍为 **32**，AOT 的 `NV` 完全未动 | `docs/STATUS.md **§10.49**（`@ e9debfd` → `:2931`）` §10.49（该节直接引用 `docs/DECFY_DESIGN.md:76` 当约束） |
| §1.4 「死指令」`OP_AND` / `OP_OR` | **已过期，需改写**：两者今天**都是活的**——§10.42 给它们定了含义（跳转路径上 `result` 已知为真（and）/假（or），故对两操作数求真值**恰好等于右操作数的真值**），短路路径改发 `OP_LOADK_BOOL`；发射点 `src/compiler/compiler.c:689` 与 `:859`。另新增 `OP_LOADK_I64`（追加在 `src/compiler/bytecode.h:58` 枚举**末尾**，旧编号无位移 ⇒ 本文档 `src/compiler/bytecode.h:12` 的引用**成立**，目录本来就是对的；`src/vm/bytecode.h` 不存在） | `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2485`）` §10.42；实测 `grep -n "OP_AND\|OP_OR" src/compiler/compiler.c` → `:689`、`:859` |
| §3.3 第 2 条「真值产生点唯一」 | **已实现**：`vm_truthy()` 单入口，六处调用点全部改调用它，旧三目链 `grep -c` 归零 | `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3131`）-3128`（该节直接引用本文档 `:125` 当规格、`:24` 当真值规则） |
| §3「IR 收敛」（后端改吃字节码） | **未做——是「未做的下一步」，不是「已被取代」**：`emit_from_bytecode` 在 `src/` **零命中**（本文档 §3.1.5 保留原草图、§3.1.3 给出修正签名）⇒ 后端今天仍各自遍历 AST，结构成因**一点没动**。**2026-10 实测把这件事量化了（§3.1.1）**：`src/compilation/aot_native.c` 与 `src/compilation/wasm_backend.c` 引用的 opcode 数是 **0 与 1**（后者唯一一个 `OP_LT` 还在注释里），而 VM 是 **69/69**。§10.53 的「真值产生点收敛为一处」是**往 §3 走的一步**（该节自己逐字引用本文档 §3.3 第 2 条当要求），按分类属「消除同一语义的两个决定点」；§3 属「让分歧在结构上不可表达」，**后者更大、没做完** | 实测 `grep -rn "emit_from_bytecode" src/` 零命中；`docs/STATUS.md **§10.53**（`@ e9debfd` → `:3116`）` §10.53；对照 `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3192`）` 诚实边界①；opcode 覆盖数见 §3.1.1 |

**两条复核后新增、对本设计有利的证据**（原文没有）：

1. **本文档已被实现方当规格引用。** `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3131`）` 引用 `docs/DECFY_DESIGN.md:125` 的「真值产生点唯一」作为该节要达成的判据；`docs/STATUS.md **§10.53**（`@ e9debfd` → `:3151`）` 引用 `:24` 的三目链尾句作为选定语义的理由；`docs/STATUS.md **§10.49**（`@ e9debfd` → `:2943`）` 引用 `:76` 的 32 字节冻结作为「选匿名 union 而非加宽字段」的理由。⇒ 设计文档写下的**约束是可被执行的**，不只是描述。**注意（2026-10 复核，已实测）**：上述行号（以及 `docs/AUDIT.md:778` 引的 `:130`）**在本档创建提交 `022d002`（当时 249 行）上逐字全对** —— `:24` 是含三目链尾句的那一行、`:76` 是 §2 的「值表示 + 集合运行时」行、`:125` 是「真值产生点唯一」、`:130` 是那张 `OpCode → { … }` 表；本档此后长到 **1156 行（4.6×）** ⇒ **今天 `:76` 与 `:125` 已是空行（`sed -n '<n>p' | wc -c` = 1）、`:24` 是 §0.5 的 `%` 行、`:130` 是 §3.1.1 的标题**；四个目标今天分别在 **§1.1 表第 3 行**、**§2 (a) 层次划分表 item ⑤**、**§3.3 第 2 条**、**§3.4**。⇒ **对本文档的引用请引节号（§3.3、§1.3、§2），不要引行号**。**外引普查给命令、不给现值**：`git ls-files -z | xargs -0 grep -n 'DECFY_DESIGN.md:[0-9]'` 去掉本档自身那些行，就是引用方清单。**⚠ 这个数在本轮写它的过程中就变小过一次**：我量得 **26**（`@ fa8247e`），`exact-otter` 随后在 `2303496` 把 `docs/AUDIT.md` 与 `docs/TYPESET_V06.md` 的 6 行改成了节号 ⇒ 同一命令读到 **18** —— **这正是本条要治的东西，而它治的对象包括写下本条的这一刻**（§9 第 33 条）。这条是本文档给引用方的一条使用说明，也是本档自己犯过同一个错（原 §0.5 表引 `:111`、依赖段引 `:125`）之后的结论。
2. **§1.1 的论点被实测加强，而不是削弱。** `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2552`）` 原文：「修 `and`/`or` 要**同时**动 C 编译器、`.im` 编译器、wasm 后端**三处**，而 `selfhost/eval.im`（`.im`·布尔）**不需要动**…**决定点的数量就是修复要碰的文件数**。」这正是 §1.1 的论点。

**仍未做**：§3 的 IR 收敛；以及 `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2557`）-2537` 记的一条诚实边界（O2 的 BigInt / 小整数快路径未做）仍成立。§10.42 当时记的「`tools/im_diff_fuzz.py` 未接门禁」**已被 `docs/STATUS.md **§10.48**（`@ e9debfd` → `:2872`）` §10.48 解决**（差分模糊测试已进门禁，判据为 `0 DIVERGE / 0 THREW / 0 untranslated`，见 `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3190`）` 的门禁逐阶段输出）。

### 0.5.1 当前阻碍（六类，截至 HEAD `ec3008f`）

按「卡住的程度」排序。**每条都带可复核的证据**，不是印象。

| # | 阻碍 | 证据 | 性质 |
| --- | --- | --- | --- |
| 1 | **IR 收敛一点没动**——五个消费者仍各自吃 `Expr*`、各自遍历 AST | 实测 `grep -rn "emit_from_bytecode" src/` **零命中**；**2026-10 量化（§3.1.1）**：`src/compilation/aot_native.c` 引用 opcode **0** 个、`src/compilation/wasm_backend.c` **1** 个（在注释里）、`selfhost/eval.im` **0** 个，而 `src/vm/vm.c` 是 **69/69**；两个后端连 `Bytecode` / `RegInstruction` 这两个类型名都不出现 | **结构成因**。`docs/STATUS.md **§10.42**（`@ e9debfd` → `:2552`）` 的「决定点数量＝修复要碰的文件数」因此仍然成立：这一轮修的 §10.42 / §10.49 / §10.53 **全是逐个决定点打补丁** |
| 2 | **「怎么证明搬对了」覆盖面不够**——三后端一致性只覆盖到整数与布尔 | `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3192`）` 诚实边界①：两个编译后端 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` **都是 0** ⇒ 字符串/容器真值**根本走不到**（**2026-10 更正：这条 grep 是坏的，`EXPR_ARRAY`/`EXPR_SET` 都不是枚举成员，见 §6.1 更正三；数组在 wasm 上其实可达且已跑通，字符串与字典/集合才走不到**）。**2026-10 实测更正两处**：① wasm 的 `cg_cond` 对非 INT/BOOL/FLOAT 返回 `1` **不是缺陷**——`vm_truthy()`（`src/vm/vm.c:347`）的规则就是「零与 `nil` 为假，**其余（含空串与所有容器）为真**」，`cg_cond` 的 `else 1` 与之逐字对应（`src/compilation/wasm_backend.c:834-847`）；② AOT 的 `nv_tru` **补不了分支**，因为 `NV` 的三个 tag（`NV_INT 0`/`NV_FLT 1`/`NV_BOO 2`，`src/compilation/aot_native.c:175-177`，结构体在 `:174`）**全部只描述标量**、**根本没有 nil / 字符串 / 容器的表示** ⇒ 这一半其实是「值表示」问题。**2026-10 人类裁定**：AOT 半**不做**「扩 `NV`」，改走 §3 第 2 步（AOT 吃带 `Value` 语义的同一份 IR、链接 `Value` 运行时）⇒ **本条的 AOT 半并入第 1 条**（§6.1 末段） | **证明能力的缺口**；**2026-10 实测后两半都没有「可修的缺陷」**：wasm 半的语义已经是对的（18/18 无分歧）、缺的是**非标量本身**（= §2 第 ⑤ 类，另立 1.5c）；AOT 半已被裁定并入第 1 条 |
| 3 | 五类搬不动的 C（§2 表逐条给了理由） | ① `src/vm/vm.c`（~5000 行）substrate；② `src/platform/` + `src/runtime/runtime_posix.c` + `src/common/`（socket/线程/mutex/fork）；③ AOT 最后一跳（`src/compilation/aot_native.c:168` 的 `kPreamble` + 宿主 `cc`）；④ 引导链（`.im` 前端仍由 C VM 跑）；⑤ `Value` 32 字节布局 + 集合运行时（BigInt/窄化整数/位图，`docs/archive/ROADMAP_3.1.md`） | **边界**，非排期问题 |
| 4 | **根因仍在持续产出新缺陷** | `docs/STATUS.md` §10.50–§10.56 连着七节，几乎全是「同一类错误还有第二、第三个副本」：`sum()` 两个静默错误答案 → WIN32 副本 `len()`/`size()` 同样缺分量回退 → `str()` 把集合读成空集 → 16 条 `-Wformat-truncation` → `+` 里非字符串左操作数被静默丢掉 | **代价在复利**。`docs/STATUS.md **§10.56**（`@ e9debfd` → `:3257`）`：`L_ADD` 与 `L_CONCAT` 有**逐字相同的不对称**，2 项链走 `OP_ADD`、3 项链走 `OP_CONCAT`，**两条路径必须同规则**——今天只是「被改成一致」，不是「结构上不可能不一致」 |
| 5 | 三项待决 | ① `OP_AND`/`OP_OR` 去留（**已不是「删死指令」**，§10.42 给了活语义）；② `tools/aot_native.test.py:187` 的 `DIVERGENCE` 仍钉 **2 条**（`func_no_return`、`global_write_from_func`；原第三条 `lcg_float_promotion` 已升 `EQUIVALENCE`）；③ wasm 固定导入表是否补 ABI 版本号 | **已识别的决定**，只差裁决 |
| 6 | 双构建等价证明自身有缺口 | `tools/selfhost_compare.py` 有 skipped 项（工作订单转述 `10 target(s) byte-identical, 23 skipped`）；本文档未在本会话实测该数字 | **基础设施** |

**这六条的依赖关系（决定了先做哪一条）**：第 2 条是第 1 条的**前提**——不先把判据扩到非标量，§3 的第 2/3 步做完也只能证明整数与布尔，等于把「无法验证」原样带进新架构。用户已于 2026-10 选定 **先做第 2 条（§6.1）**；**但随后按 §6.1 的判据实测，第 2 条自身没有「可修的缺陷」**：wasm 半的语义已经与 `vm_truthy()` 一致（18/18、0 分歧）、非标量**根本没实现**（三个 `grep -c` 的 **0 是「特性不存在」，不是「存在但没接上」**——**2026-10 更正：这条 grep 有三个分支、两个是死分支（`EXPR_ARRAY`/`EXPR_SET` 非枚举成员），而它掩盖了「wasm 已实现数组」，见 §6.1 更正三；准确的边界是「字符串与字典/集合没实现」，不是「非标量没实现」**），AOT 半已被裁定并入第 1 条。⇒ **第 2 条作为「扩覆盖」已核实关闭；它作为「实现非标量」是 §2 第 ⑤ 类的新工作量（§6.1 的 1.5c），不构成第 1 步的前提。** 于是**当前唯一的结构性下一步回到第 1 条（§3 的 IR 收敛）**。

---

## 1. 核心问题：同一语义的多个独立决定点

**这个病不是本文档发现的，也不是假设。** 仓库已经为**另一个**注册表（`builtin`）诊断过同一个病、修好过一次、并把修法写成门禁断言——注释原文是 `one name with two answers` / `dead code that reads as live` / `no single test file can see it`。§1 下面这些实例与它是同一个病；§3.1.8 记着那个先例的三件套。**读这一节时请把它当成同一个病的第二、三、四个实例，而不是新的病。**

### 1.1 `and` / `or`：六个决定点，横切 C 与 `.im` **[读码，全部逐行读过]**

| # | 位置 | 语言 | 语义 | 机制 |
|---|---|---|---|---|
| 1 | **取证当时** `src/compiler/compiler.c:648-670`；锚 = `compile_expr` 的 `EXPR_BINARY` 里判 `TOK_AND`/`TOK_OR` 的那一支 | C | **值**（**取证当时**的记录，对应 `d4b65c6` **之前**的树；该提交之后此处已是**布尔** —— `OP_JUMP_IF_FALSE`/`OP_JUMP_IF_TRUE` + `OP_AND`/`OP_OR` + `OP_LOADK_BOOL`，见 §10.42） | **取证当时** `OP_JUMP_IF_FALSE`（and）/ `OP_JUMP_IF_TRUE`（or）+ `OP_MOV result, right`；跳转目标回填 `comp->curBC->code[jmp_pos].r2 = end`（`jmp_pos` 是取证当时的名字） |
| 2 | **取证当时** `selfhost/compiler.im:254-266`；锚 = `compile_expr` 的 `t == "bin"` 里判 `op == "and" or op == "or"` 的那一支（今天在 `:254-286`） | `.im` | **值**（**取证当时**的记录，对应 `d4b65c6` **之前**的树；该提交之后此处已是**布尔** —— `OP_JUMP_IF_FALSE`/`OP_JUMP_IF_TRUE` + `OP_AND`/`OP_OR` + `OP_LOADK_BOOL`，见 §10.42） | **取证当时** 与 #1 同形：`jpos = len(ctx["code"])`，`ctx["code"][jpos][2] = end`（`d4b65c6^` 上逐行核过：`:257` 是 `jpos = len(ctx["code"])`、`:264` 是 `emit(ctx, OP_MOV, result, right, 0)`、`:266` 是 `ctx["code"][jpos][2] = end`；今天这两个名字是 `jshort`/`jend`） |
| 3 | **取证当时** `src/vm/vm.c:3166-3172`（`L_AND`）/ `:3173-3179`（`L_OR`）；**现状（2026-10 实测）** `:3486-3491` / `:3493-3498` | C | **布尔** | `value_set(&R[ins.r1], VAL_BOOL, (a && b) ? 1 : 0, …)`；**取证当时**的真值函数是三目链 `(va.type == VAL_BOOL) ? … : (va.type == VAL_NIL) ? 0 : 1`，**§10.53 已把六个产生点收敛为唯一一处 `vm_truthy()`（`src/vm/vm.c:347`）** |
| 4 | `src/compilation/aot_native.c:420-428`（`EXPR_BINARY` 在 `:418`，特判在 `:420`，发射在 `:425-428`；**取证当时记作 `:334-339`**） | C | **布尔** | `buf_str(b, "nv_boo(nv_tru(")` … `op == TOK_AND ? ") && nv_tru(" : ") || nv_tru("` … `buf_str(b, "))")` |
| 5 | `src/compilation/wasm_backend.c:769-776`（AND）/ `:778-786`（OR） | C | **布尔** | `cg_cond()` 递归：AND 为 `W_IF` 左→右→`W_ELSE`+`e_i32c(0)`→`W_END`；OR 为左→`W_IF`+`e_i32c(1)`→`W_ELSE`→右→`W_END` |
| 6 | `selfhost/eval.im:114-118`（and）/ `:119-123`（or） | `.im` | **布尔** | `if !truthy(l) { return false }` / `if truthy(l) { return true }`；成功路径 `return truthy(eval_expr(e["r"], env))` |

⇒ **（取证当时的分布）** **2 值 : 4 布尔，横切两种语言。** 且**同语言内部同样分歧**：C 侧 `compiler.c`（值）对 `vm.c`/`aot_native.c`/`wasm_backend.c`（布尔）；`.im` 侧 `compiler.im`（值）对 `eval.im`（布尔）。★ **今天不是这个分布**：`d4b65c6` 之后**六处全部为布尔**（0 值 : 6 布尔）—— 这一行与上面那张表都是**取证当时**的记录，不是今天的现值。

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
    const char *declared_at;    /* 谁声明它：列名从「生产点」降级为「声明点」的原因见 §3.1.9 边界④ */
    int  arity;                 /* 用到的操作数个数（1..3） */
    int  touches_state;         /* 是否写当前帧之外的状态（§3.1.7 的 P/C/S，D14 的同一判据） */
    int  can_raise;             /* 正交第二轴：块内是否调 vm_throw（§3.1.7 实测 16 个） */
    const char *vm_semantics;   /* 唯一语义来源：src/vm/vm.c 的对应 L_X: 块 */
    const char *aot_emit;       /* 派生物，不是手写第二份 */
    const char *wasm_emit;      /* 同上 */
} OpSemantics;
```

**两列都是必需的，不是冗余**：§3.1.7 实测出 `touches_state` 与 `can_raise` **互相正交**（**用词订正：原文写「互相独立」—— 这里「独立」是轴义「正交」，不是验证义「第二个人复核过」。同一个词在同一份文档里承担三种意思，正是本档 §1 那个病的形状，见 §3.1.11 末尾的用词表**），交叠处有 11 个「纯值但会抛」的 opcode（`+` 与全部关系运算）——只带一列会把它们映射错。

**`declared_at` 列（原名 `producers`，由 `exact-lumen` 提出、我已实测复核，见 §3.1.9）是全表唯一**能用 `grep` 客观判定**的列**——**但它只能判定「声明」，判定不了「生产」，理由见 §3.1.9 边界④**（`OP_POP_REG` 的裸常量定义看起来像生产点、实际从不发射；且 `emit(comp->curBC, OP_…)` 不是唯一形状，有宏与包装）。其余各列——`touches_state`、`can_raise`、`vm_semantics`、两个 `*_emit`——**都要读实现才能填**。一张每一格都靠读实现来填的表**它自己就没有判据**；`declared_at` 这一列给了它一个能自动复核的锚点，而且它立刻抓出两类**不该出现在表里**的行（§3.1.9）。**改名的代价是诚实的代价：这一列从此不承诺「谁发射它」，只承诺「它出现在哪里」——而「生产点」需要逐 opcode 读码 69 次，本文档不假装有那个口径。**

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

**正交第二轴 `can_raise`：16 个**（块内出现 `vm_throw`，helper 在 `src/vm/vm.c:2481`）—— `ADD`、`CONCAT`、`SUB`、`MUL`、`DIV`、`NEG`、`LT`、`GT`、`LE`、`GE`、`INDEX_SET`、`STORE_GLOBAL`、`CALL_BUILTIN`、`MOD`、`BE`、`THROW`。

**⇒ 本轮最重要的结论：两个轴是正交的，`touches_state` 一个布尔不够。** 交叠处有一格是空的：
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

3. **计数也一起断言**，免得「悄悄少了一条」通过——`tools/gate.sh:132-139`：`EXP_CTEST` 必须与 ctest 报的 `0 tests failed out of N` 相等，注释逐字 `Assert the count too, so that a dropped add_test( ) cannot pass silently.`
   **⚠ 本档不再写 `EXP_CTEST` 的绝对值（2026-10，`exact-lumen` 建议、我采纳）**：那个数**在它一轮工作里被改过三次**（134 → 136 → 137 → 138 → 139 → 140 → **141**），本档每写一次就多一处**必然过期**的抄本。⇒ **唯一真值源是 `tools/gate.sh:54` 的 `EXP_CTEST="${EXP_CTEST:-…}"`**，本节与 §10 只写**「见 `tools/gate.sh:54`」**。**这不是懒，是 §3.1.8 那个先例的同一条纪律**：一个数有两处抄本时，两处会漂开，而**没有任何东西在检查它们相等**（这正是 §9 第 23 条登记的洞）。**已实测的沿革**（作为「它会漂」的证据保留，不当作现值）：134 →（`exact-lumen` 报）136 → 137（`a7d405d` merge）→ 138 → 139（`#139 fixture_parse_runtime`）→ 140（`substr_boundary_runtime`）→ 141（`literal_resolve_runtime`）；门禁阶段数**始终 12**。**本会话实测确认的最后一个值**：`sed -n '54p' tools/gate.sh` → `EXP_CTEST="${EXP_CTEST:-141}"`。

**这段注释就是本文档 §1 的论点，由仓库自己用英文写下的版本**：「one name with two answers」「dead code that reads as live」「no single test file can see it」。
§1 的六条 `and`/`or`、三个 `%`、六个真值点，与它是**同一个病**；§3.1 的 `OpSemantics` 表判据与它是**同一个处方**。⇒ §3 的可行性论据不是「应该能行」，而是「**这里已经行过一次**」。

**当前状态：我按 CMake 的真实源列表逐平台重算了这个注册表（2026-10 实测）。**

| 平台 | 源文件 | 注册次数 | distinct 名字 | 重名 |
|---|---|---|---|---|
| WIN32 | 27 base + 26 | **431** | **430** | **1** |
| POSIX | 27 base + 27 | **241** | **240** | **1** |

那唯一一个重名是 `isolate_run`（`src/isolate_mod.c:227` 与 `:318`），且是**假阳性**：两处分别在 `#ifdef _WIN32` 与 `#else` 分支里（已读预处理上下文确认），只有一个会被编译。
⇒ **今天两个平台都是重名自由的**，`gui_fullscreen` 已不再出现（`src/mod/gui_mod.c:3690` 是唯一注册点，`:3691-3692` 留着说明它曾经被注册两次的注释）⇒ §1.53 登记的那个实例**已修**，我的扫描与之一致。

**诚实边界（这条很重要，因为两个独立计数不一致）**：我的正则是 `vm_register_builtin(_full)?\s*\(\s*\w+\s*,\s*"([^"]+)"`，只匹配**字面量名字 + 简单首参**；用宏或变量传名字的会漏掉。`exact-lumen` 独立数出的是 WIN32 **398** / POSIX **128**，与我这里的 **431 / 241** 不一致——**我无法解释这个差**（很可能是它只扫了 `CMakeLists.txt @ e3da33c:412-436` 那一段的文件子集，而 POSIX 的 `list(APPEND …)` 在 `@ e3da33c:438-443`）（**本会话复核：这两个号从未漂移** —— `set(INIMERSE_ENGINE_SOURCES` 在 `66b12ca`/`e313c0f`/`e9debfd`/`e3da33c` 上都在 `:412`、WIN32 段末行都在 `:436`、POSIX 段都在 `:438-443`；`exact-otter` 给的「现为 `:427-436`」会把这段窄化成只剩 `if(WIN32)` 的 `list(APPEND)`，改变这句话在说的东西，故未采纳）。**两个数都留着，不调和一个我解释不了的差异。**

#### 第二个先例，而且它比第一个更接近 §3.1 要做的事（2026-10 新增）

第一个先例是「**在创建点拒绝**」——`builtin` 注册表能拒绝，因为第二次注册在语义上就是死代码。**但 §3.1 的表拒绝不了任何东西**：它只是把 69 个 opcode 与它们的消费者**摆在一起比较**。所以第一个先例证明的是「这个仓库肯为一条语义纪律加断言」，**没有**证明「跨两个artifact 的集合比对在这个仓库里跑通过」。

**现在它跑通过了。** 第二个先例（来源：`exact-lumen` 本轮的实测与修复，**我未独立复核，标为 [转述]**；**可核坐标（本会话重取，观测点 `main @ 07cfd1d`）：它的产物今天都在树里** —— `tools/check_orphan_fixtures.py`（**第 12 阶段**：`grep -n 'orphan_fixtures' tools/gate.sh` 印出阶段函数、它调的那条命令、与阶段表里那一行）与它自己 `python3 tools/check_orphan_fixtures.py` **今天 rc=0**，逐字 `check_orphan_fixtures: 128 input(s) checked (81 vtest fixtures, 34 python harnesses, 13 node harnesses); 77 fixtures registered, 4 allowed with a stated reason.`）：

- **症状**：四处文档声称有 CTest 覆盖、四处都没有 —— `docs/API.md` 第 2.1 节的证据列标题就是「证据（CTest）」，而 `:114` 把 `lint_case_missing_default_v04.im` 写成「相关 CTest」、`:115` 把 `lint_case_exhaustive_v04.im`（**是文件名，不是测试名**）与四个真实测试名并列；`docs/REQUIREMENTS_ANALYSIS.md:177` 与 `docs/STATUS.md **§3**（`@ e9debfd` → `:286`；`ab70a71` 与 `main` 该行**同文**、未漂移）` 把 `tools/migrate_report.py` 与 `bindgen_regression` / `scan_tools_regression` 并列，而 `tools/` 下**没有** `migrate_report.test.py`，那两个 CTest 跑的是 `bindgen.test.py` 与 `scan_tools.test.py`、**都不碰它**。
- **能力是真的，覆盖不是**：两个 fixture 今天 `--lint` 各出一条 `[WARN]`、rc=1；`migrate_report.py` 也 rc=0 出真报告。⇒ **不是「文档描述了不存在的东西」，而是「文档描述了一个从未接上的东西」** —— fixture 写好了、诊断是对的、表格把它当证据引用了，**而注册那一行从来没有被加过**。
- **为什么没人发现**：`CMakeLists.txt @ e3da33c:756-766` 的五个 `lint_case_*` 是**手写列举**的（`@ e3da33c:767` 的注释自己数着「The five lint_case_* tests above …」），第六、第七个 fixture 落地时**没有任何东西要求把它们加进去**；仓库里**没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**。**⚠ 指针已按内容重取**：原文这里写的是 `CMakeLists.txt:733-743` 与 `:744` —— 它们是**陈旧指针**（在 `e313c0f` 及更早的树上正确，随 main 前进 **+23** 而失效）；锚在 `@ e3da33c:756` 的 `add_test(NAME lint_case_try_runtime COMMAND inimerse --lint …lint_case_try_v04.im)` 与 `:767` 的 `# The five lint_case_* tests above …`。
- **修法的两半**：① 注册 **#135** / **#136**，各带 `PASS_REGULAR_EXPRESSION` **和一条断言对方那条警告不出现的 `FAIL_REGULAR_EXPRESSION`**（两个 fixture 只差一行、走同一个 `--lint` 通道，只断言自己的发现**分不开**「因正确的理由触发」与「对每个 case 都触发」）；② 新增 `tools/check_orphan_fixtures.py` 与**门禁第 12 阶段 `orphan-fixtures`**。
- **反向验证**：把 `CMakeLists.txt` 退回 `HEAD` ⇒ `2 orphaned input(s) out of 110 checked` + 逐条点名 + `exit 1`。
- **普查（记录：`exact-lumen` 那一轮）**：67 个 `vtest/*.im` 中 5 个未被 `CMakeLists.txt` 提到 —— 两个真缺口、三个合法；30 个 `tools/*.test.py` 与 13 个 `tools/*.test.js` **0 孤儿**。**⚠ 这四个数是「记录」，不是「现状」—— 它们数的是一个别人每天都在往里加东西的总体**：读你自己手上那棵树用 `python3 tools/check_orphan_fixtures.py`，**退出码 0 = 今天没有孤儿**，逐行给出 `N input(s) checked (… fixtures, … python harnesses, … node harnesses)`（观测点 `main @ f307b23`：`122 input(s) checked (76 vtest fixtures, 33 python harnesses, 13 node harnesses); 72 fixtures registered, 4 allowed with a stated reason.`）。**「0 孤儿」那一半有门禁站岗**（`tools/gate.sh:513` 的 `orphan-fixtures` 阶段，标签逐字 `test inputs that no CTest runs (expect 0)`）⇒ 它过期时会**变红**，不会静默变假；**上面那四个数没有站岗的** ⇒ 已经烂了三个（67→**76**、5→**0**、30→**33**；13 未变）。这是同一族病的**第五个实例**，也是「**修一处、按同形搜它的邻域**」这条方法的第二次验证。

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
④ **关于 `producers` 这一列本身，`exact-lumen` 给了一个我接受的降级**：那一列**不能用一次字面 grep 客观判定**，反例正是本节刚报的 `OP_POP_REG`（`selfhost/compiler.im:37` 的 `OP_POP_REG = 29` 是**裸常量定义**，看起来像生产点、实际从不发射），且 `src/compiler/compiler.c` 里 `emit(comp->curBC, OP_…)` **不是唯一形状**（有宏与包装）。⇒ **列名从「生产点」改成「声明点」是诚实的**；「生产点」需要**逐 opcode 读码 69 次**，**本文档不假装有那个口径**（§3.1.3 的表已按此改名）。

### 3.1.10 两后端改吃 `Bytecode` 时，**第一件要指定的事不是接口，是「同一个语义归谁」**

**`exact-lumen` 的更正二（我独立复跑确认）给出了一个比 §3.1.9 更值钱的实例：有人发射，但发射的是另一个实现。**

**事实（实测 + 读码）**：`type CombinedError = FileError + ParseError` 的**集合并集真的算出来了**，但**不是在类型层算的**：

```
$ printf 'type FileError = "not_found","permission_denied"\ntype ParseError = "invalid_syntax","unexpected_token"\ntype CombinedError = FileError + ParseError\nsay FileError\nsay ParseError\nsay CombinedError\n' > /tmp/un.im
$ ./build/inimerse --no-mods /tmp/un.im
set(2)
set(2)
set(4)        ← 并集算出来了，rc=0
```

机制（读码）：
- `parse_type_stmt` 的 RHS 走 `parse_expr(p)`（`src/parser/parser.c:1224-1226`）⇒ `FileError + ParseError` 是**一个二元 `+` 表达式节点**，不是类型层的集合运算。
- `src/compiler/compiler.c:2224-2231` 的 `case STMT_TYPE:` 只做 `compile_expr(comp, stmt->typeStmt.set)` + `emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0)` ⇒ **整条 RHS 在运行期求值**。
- 并集发生在 **`src/vm/vm.c:3192-3200` 的 `L_ADD`**：`if (a->type == VAL_SET || b->type == VAL_SET) { … n = set_union(vm, a->ival, b->ival); … }`（`set_union` 定义在 **`src/vm/vm.c:2054`**，第二个调用点 `:3289`）。
- 而 **`im_typeset_union`（声明 `src/types/typeset.h:41`、定义 `src/types/typeset.c:56`）在引擎里仍然零消费者** —— 全部调用点只有两个 probe：`src/types/typeset_probe.c:19`、`:50` 与 `src/types/enum_probe.c:91`、`:117`。

⇒ **同一个语义（集合并集）有两个生产点**：**活的那个在 `src/vm/vm.c`（`L_ADD` → `set_union`）**，**符号的那个在 `src/types/typeset.c`（`im_typeset_union`），引擎永远到不了。** **§3.7「`src/types/typeset.c` 零消费者」确认成立，且比原先记的更严重**：不是「写好了没人用」，而是「**有人用了，用的是另一个实现**」——`OP_POP_REG` 那一条是「无人发射」，这一条是「**有人发射、但发射的是另一份代码**」。**这是 §1 那个病的第八个实例，也是第一个「两份实现都活着、只有一份被执行」的实例。**

**为什么这件事必须在 §3.1.10 里先解决**：AOT 与 wasm 从 AST 改成吃 `Bytecode` 之后，**「集合并集」这个语义将有三个可能的家**——① `L_ADD` 的 `set_union`（今天活的）、② `src/types/typeset.c` 的 `im_typeset_union`（今天死的）、③ 将来 IR 上的某个 opcode。**如果 §3.1.10 不先指定并集归谁，两个后端各自接一份的概率很高**——那就是本档 `:10-12` 记的病，换个位置再长一次（**这次会同时长在两个后端里，且都不在 VM 里，于是没有任何一条现有 CTest 会红**）。

**我给出的指定（作为 §3.1.10 的提案，非裁定）**：
1. **并集的唯一语义来源 = `src/vm/vm.c:3192-3200` 的 `L_ADD` 分支**（它是今天唯一被执行的那份），**不是 `im_typeset_union`**。理由：`im_typeset_union` 的输入是 `ImTypeSet *`（编译期集合对象），而 `L_ADD` 的输入是两个 `Value` 里的 `ival`（运行期集合槽）——**两者不是同一个东西，不能「选一个」**；真正要定的是**在哪一层做并集**。
2. **`im_typeset_union` 与 `set_union` 必须被明确标注为「两个不同层的同名语义」**，并在 §3 的 `OpSemantics` 表里给并集**一个 opcode 行**（今天它藏在 `OP_ADD` 里，`OP_ADD` 一行的 `vm_semantics` 列必须写出「数字加法 **或** 集合并集 **或** 字符串拼接」三态——**这正是「一个 opcode 三个语义」的写法，比拆 opcode 更诚实**）。**判据**：`grep -c 'set_union' src/vm/vm.c` 的调用点与 `OpSemantics` 里声明并集的行数必须能互相对上；**故意删掉表里那一行，§6 的断言必须变红。**
3. **`im_typeset_union` 的去留 —— 已由人类裁定（2026-10）：接上，不删。** 裁定走 **§9 第 20 条的选项 ①**：**让编译期类型推断真的调用 `im_typeset_union`**（= §3.7 的 B″ 落地）。⇒ 三条直接后果：① **§3.7 的 B″ 从「设计」变成「要实现的编译期类型求值器」**，且它与 §3.1.10 的并集归属**不冲突**——两层各有一个并集，**编译期那层今天必须被接上、运行期那层今天已经在跑**；② **不要删 `im_typeset_union`**，也**不要**把并集语义「唯一地」定在 `L_ADD`（那是选项 ②，已被否决）；③ **`OP_ADD` 的三态 `vm_semantics` 写法因此更重要**，因为同一份源码里 `+` 的两个实例会在**不同层**解析（静态模式下由编译器算、动态模式下由 `L_ADD` 算）——**这正是 §3.8「契约式双层判定」在并集上的第一次具体落地**，也是 §6.2「保守性」判据的第一个真实用例。**判据（可证伪）**：静态模式下 `type CombinedError = FileError + ParseError` 的并集**必须在编译期算出**（`im_typeset_union` 被调用），且**运行期不得再算一次**；**把编译期那半去掉、断言必须变红**。

**共享代码边界（同一批实测）**：`src/compilation/aot_native.h` 逐字写 "The accepted subset is deliberately the one the Wasm backend already defines (`src/compilation/wasm_backend.h`)"，**而两个文件零共享代码**：缓冲区各一份（AOT `buf_init`/`buf_need`/`buf_fmt` `src/compilation/aot_native.c:35-57` vs wasm `bput`/`bleb_u`/`bf64` `src/compilation/wasm_backend.c:29-52`），符号表各一份（`Sym`/`sym_find`/`local_add` vs `Named`/`global_lookup`/`resolve_var`），**成功约定还相反**（`aot_native_translate` 返 **1 成功 / 0 失败**，`wasm_compile_program` 返 **0 成功 / −1 失败**）。⇒ **§3.1.3 的 `backend_emit_unit(Ctx *c, const Bytecode *bc)` 接口草图必须补一句：两个后端在改吃 `Bytecode` 的同一批里，先抽出一个共享的「字节码游标 + 符号表 + 缓冲区」层**，否则「两个后端都吃 IR」会变成「两个后端各自写一份 IR 消费者」——**同一个病，第二次。** 这一条与 §3.5 的 A′ 后果 ②（wasm 与 AOT 必须同一批改）是同一个要求的两面。


### 3.1.11 `OpSemantics` 表的 **69 行全表**（2026-10，本会话实测填充）

§3.1.7 给了骨架（类 / `can_raise` / 12 行机制与名字不符），本节把**其余列**填满。**这是 §3 的第一个可验收物**：第 2、3 步的判据 ③ 说「表行数 = 枚举成员数」，从这一节起它有了被填满的版本可对照。

**列的含义与取证方式**（每列都必须能机械复核，否则它就不是判据）：

| 列 | 含义 | 怎么复核 |
| --- | --- | --- |
| `#` | 枚举序号 | `src/compiler/bytecode.h` 的 `typedef enum { … } OpCode;` 内出现顺序（`awk` 抽取） |
| `块` | VM 里该指令的语义体（**不是** `case`，见 §3.1.6） | 标签行到下一标签行 − 1；`sed -n '<区间>p' src/vm/vm.c` |
| `regs` | 用到的操作数下标 | 块内 `grep -o 'ins\.r[123]' \| sort -u` |
| `类` | `P` 纯值 / `C` 仅控制流 / `S` 触碰帧外状态 | §3.1.7 的分类规则（手工） |
| `raise` | 块内 `vm_throw` 出现次数 | `grep -c vm_throw` |
| `声明点` | 该 opcode 名字出现在哪些文件 | 见下面的字母表；**这是「声明点」不是「生产点」**（§3.1.9 边界④） |
| `vm_semantics` | 一句机制 | 读块 |

**声明点字母表**：`h`=`src/compiler/bytecode.h`、`c`=`src/compiler/compiler.c`、`v`=`src/vm/vm.c`、`s`=`selfhost/*.im`、`a`=`src/compilation/aot_native.c`、`w`=`src/compilation/wasm_backend.c`、`j`=`src/vm/jit_mode.c`。**实测结论：`a` 与 `j` 对全部 69 个 opcode 都是零命中，`w` 只有 1 个（`OP_LT`，且在注释里 `src/compilation/wasm_backend.c:1500`）** ⇒ **表的后两列（`aot_emit` / `wasm_emit`）今天对 68–69 行是空的，这不是表的缺陷，是 §6 第 2/3 步要消灭的那个零。**

| # | opcode | 块 | regs | 类 | raise | 声明点 | `vm_semantics`（一句） |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `OP_MOV` | `3146-3148` | 1,2 | P | 0 | h c v s | `value_assign(&R[r1], &R[r2])`，引用计数复制 |
| 2 | `OP_LOADK_INT` | `3149-3151` | 1,2 | P | 0 | h c v s | `R[r1] = INT(r2)`（**立即数，不是池**） |
| 3 | `OP_LOADK_FLOAT` | `3162-3164` | 1,2 | P | 0 | h c v s | `R[r1] = FLOAT(float_pool[r2])` |
| 4 | `OP_LOADK_STRING` | `3165-3180` | 1,2 | P | 0 | h c v s | `R[r1] = 字符串(string_pool[r2])`；**块内 intern 改写 `sval`/`ival`** |
| 5 | `OP_LOADK_BOOL` | `3181-3185` | 1,2 | P | 0 | h c v s | `R[r1] = BOOL(r2 ? 1 : 0)` |
| 6 | `OP_ADD` | `3186-3236` | 1,2,3 | P | **1** | h c v s | **三态**：数字加 / **集合并集（`set_union`，§3.1.10）** / 字符串拼接；越界提升到 i64 |
| 7 | `OP_SUB` | `3354-3378` | 1,2,3 | P | **1** | h c v s | 数字减；越界提升 |
| 8 | `OP_MUL` | `3379-3403` | 1,2,3 | P | **1** | h c v s | 数字乘；越界提升 |
| 9 | `OP_DIV` | `3404-3430` | 1,2,3 | P | **1** | h c v s | 数字除；**§10.49 新增整数路径**；除零抛 |
| 10 | `OP_NEG` | `3431-3448` | 1,2 | P | **1** | h c v s | 取负；非数字抛 |
| 11 | `OP_EQ` | `3449-3457` | 1,2,3 | P | 0 | h c v s | 相等（AOT 对应 `nv_eq`） |
| 12 | `OP_NEQ` | `3458-3465` | 1,2,3 | P | 0 | h c v s | 不等 |
| 13 | `OP_LT` | `3466-3475` | 1,2,3 | P | **1** | h c v s **w** | 序比较；`val_orderable` 失败抛。**全表唯一被 wasm 提到的 opcode，且在注释里** |
| 14 | `OP_GT` | `3476-3485` | 1,2,3 | P | **1** | h c v s | 同上 |
| 15 | `OP_LE` | `3486-3495` | 1,2,3 | P | **1** | h c v s | 同上 |
| 16 | `OP_GE` | `3496-3507` | 1,2,3 | P | **1** | h c v s | 同上 |
| 17 | `OP_AND` | `3508-3514` | 1,2,3 | P | 0 | h c v s | **真值语义**（§10.42 裁定后的活含义）；读 `R[r2]`、`R[r3]` |
| 18 | `OP_OR` | `3515-3521` | 1,2,3 | P | 0 | h c v s | 同上 |
| 19 | `OP_NOT` | `3522-3529` | 1,2 | P | 0 | h c v s | 真值取反 |
| 20 | `OP_NEW_ARRAY` | `3530-3557` | 1,3 | P | 0 | h c v s | `vm_array_new`；`r3` = 元素数。**wasm 已实现对应能力**（§6.1 更正三） |
| 21 | `OP_INDEX_GET` | `3585-3626` | 1,2,3 | P | 0 | h c v s | 读容器；dict 读加锁；**数组越界读保持 nil**（注释逐字 `out-of-range read stays nil (compat)`） |
| 22 | `OP_INDEX_SET` | `3627-3645` | 1,2,3 | P | **1** | h c v s | 写**已有**容器（`array_set`/`dict_set`） |
| 23 | `OP_LOAD_GLOBAL` | `3646-3661` | 1,2 | S | 0 | h c v s | 读全局；**多线程时条件加锁**（`need_lock = vm->active_threads > 1`）；字符串 intern |
| 24 | `OP_STORE_GLOBAL` | `3662-3702` | 1,2 | S | **1** | h c v s | 写全局 |
| 25 | `OP_JUMP` | `3703-3705` | 2 | C | 0 | h c v s | `t->ip = r2` |
| 26 | `OP_JUMP_IF_FALSE` | `3706-3710` | 1,2 | C | 0 | h c v s | `vm_truthy(&R[r1])` 为假则跳（**唯一真值来源，§10.53**） |
| 27 | `OP_JUMP_IF_TRUE` | `3711-3717` | 1,2 | C | 0 | h c v s | 同上，为真则跳 |
| 28 | `OP_CALL_BUILTIN` | `3770-3832` | 1,2,3 | S | **3** | h c v s | 调内建；名字来自 `string_pool[r2]`。**全表 `raise` 最多的一行** |
| 29 | `OP_PUSH_REG` | `3718-3760` | 1 | P | 0 | h c v s | 把 `R[r1]` 压到捕获栈。**这是今天真正的捕获协议**（§3.1.9） |
| 30 | `OP_POP_REG` | `3761-3769` | 1 | P | 0 | h **v** s | 弹捕获栈。**零生产点 opcode**（§3.1.9）：`s` 那处是 `selfhost/compiler.im:37` 的裸常量 `OP_POP_REG = 29`，从不发射 |
| 31 | `OP_SAY` | `3833-3844` | 1 | S | 0 | h c v s | 打印。**绕过平台层**直接 `WriteFile`（§3.1.7 的机制与名字不符） |
| 32 | `OP_WAIT` | `3845-3931` | 1 | S | 0 | h c v s | 交给调度器（`SwitchToFiber` 系） |
| 33 | `OP_STOP` | `3932-3934` | — | S | 0 | h c v s | `t->running = false` |
| 34 | `OP_HALT` | `3935-3939` | — | S | 0 | h c v s | 停（与 `STOP` 相邻但不同块） |
| 35 | `OP_CALL_FUNC` | `3977-4031` | 1,2,3 | S | 0 | h c v s | 按函数索引调用；`fidx`/`res`/`argc` 都在操作数里 |
| 36 | `OP_RETURN` | `4032-4063` | 1 | S | 0 | h c v s | 返回；`prof_enabled` 时记 profile |
| 37 | `OP_IS_NIL` | `4064-4067` | 1,2 | P | 0 | h c v s | `R[r1] = (R[r2].type == VAL_NIL)` |
| 38 | `OP_THREAD_START` | `4348-4435` | 1,2,3 | S | 0 | h c v s | 起线程 |
| 39 | `OP_THREAD_CTRL` | `4436-4500` | 1,2 | S | 0 | h c v s | 线程控制（按 `r2` 的 op 分派） |
| 40 | `OP_THREAD_JOIN` | `4535-4560` | 1,2 | S | 0 | h c v s | 汇合 |
| 41 | `OP_THREAD_WAIT` | `4561-4580` | 1,2 | S | 0 | h c v s | 等 |
| 42 | `OP_THREAD_STATE` | `4581-4596` | 1,2,3 | S | 0 | h c v s | 读线程状态属性（`prop = r3`） |
| 43 | `OP_LOCK` | `4597-4611` | 1,2 | S | 0 | h c v s | 锁 |
| 44 | `OP_SEND` | `4612-4649` | 1,2 | S | 0 | h c v s | 发消息 |
| 45 | `OP_RECV` | `4650-4680` | 1,2 | S | 0 | h c v s | 收消息；**69 个块里的最后一个** |
| 46 | `OP_NEW_DICT` | `3558-3584` | 1,3 | P | 0 | h c v s | `vm_array_new` 起的 dict 构造 |
| 47 | `OP_EQK` | `4068-4083` | 1,2,3 | P | 0 | h c v s | 与常量集合比相等（`sidx = r3`） |
| 48 | `OP_NEQK` | `4084-4100` | 1,2,3 | P | 0 | h c v s | 同上，不等 |
| 49 | `OP_DECLARE` | `4101-4111` | 1,2 | S | 0 | h c v | **不是「声明」，是写 VM 资源限额**（`limit_mem`/`threads`/`time`/`inst`/`vram`，按 `r1` 的 kind 0–4）。**`s` 零命中** |
| 50 | `OP_RECORD` | `4112-4160` | 1,2,3 | S | 0 | h c v | 取 `VM_LOCK(vm)`、`realloc` `record_meta`/`record_names`。**`s` 零命中** |
| 51 | `OP_MOD` | `4161-4194` | 1,2,3 | P | **2** | h c v | 取模。**§1.2 的三个决定点之一**（`docs/AUDIT.md` §1.6 实测三后端三答案）。**`s` 零命中** |
| 52 | `OP_NEW_SET` | `4195-4223` | 1,3 | P | 0 | h c v | **新建**集合（`vm_set_new`）⇒ 不归 `S` |
| 53 | `OP_SET_INTERVAL` | `4224-4245` | 1,2 | P | 0 | h c v | 区间集合构造；`t->sp < 1` 时给 nil |
| 54 | `OP_IN` | `4246-4251` | 1,2,3 | P | 0 | h c v s | 成员测试 |
| 55 | `OP_MIN` | `4252-4256` | 1,2 | P | 0 | h c v | 取小。**`s` 零命中** |
| 56 | `OP_MAX` | `4257-4261` | 1,2 | P | 0 | h c v | 取大。**`s` 零命中** |
| 57 | `OP_BE` | `4262-4308` | 1,2,3 | S | **1** | h c v | 约束检查；取全局分片锁 `VM_GSHARD` + `vm_global_grow`；命中时 `vm->be_bound[g] = sidx + 1`。**§3.8/§3.9 的主角** |
| 58 | `OP_TRY_START` | `4309-4332` | 2 | S | 0 | h c v | 建 `TryEntry` |
| 59 | `OP_TRY_END` | `4333-4336` | — | S | 0 | h c v | **只做** `if (t->exc_depth > 0) t->exc_depth--;`（线程内） |
| 60 | `OP_THROW` | `4337-4341` | 1 | S | **1** | h c v | `vm_throw(vm, t, &R[r1])` |
| 61 | `OP_SET_ADD` | `4342-4347` | 1,2 | S | 0 | h c v | 写**已有**集合（`R[r1]` 经 `vm_set_add`） |
| 62 | `OP_THREAD_GOTO` | `4501-4534` | 1,2 | S | 0 | h c v | 线程跳转 |
| 63 | `OP_CONCAT` | `3237-3353` | 1,2,3 | P | **1** | h c v | **与 `L_ADD` 同语义**（块内注释逐字 `Same per-step semantics as L_ADD; all-string chains allocate once.`）；§10.53 修过它的区间连续性 |
| 64 | `OP_YIELD` | `3940-3944` | — | S | 0 | h c v | `if (t->is_task && t->fiber_sched) SwitchToFiber(...)` |
| 65 | `OP_MAKE_FUNC` | `3956-3971` | 1,2,3 | S | 0 | h c v | 建闭包（`im_closure_env_new(r3)`）；**只在分配失败时**置 `t->running = false; vm->last_error = 1` |
| 66 | `OP_CALL_VALUE` | `3972-3976` | 1 | S | 0 | h c v | 可调用性检查；不可调用时 `fprintf(stderr, "error: value is not callable")` 并停 |
| 67 | `OP_LOAD_CAPTURE` | `3945-3951` | 1,2 | P | 0 | h c v | 读闭包环境；越界静默 |
| 68 | `OP_STORE_CAPTURE` | `3952-3955` | 1,2 | P | 0 | h **v** | 写闭包环境。**零生产点 opcode**：`s` 与 `c` **都零命中**（`grep -rl OP_STORE_CAPTURE src/ selfhost/` 只回 `bytecode.h` 与 `vm.c`） |
| 69 | `OP_LOADK_I64` | `3152-3161` | 1,2,3 | P | 0 | h c v | **64 位立即数**：`r2` 低 32、`r3` 高 32（块内注释逐字 `both read as UNSIGNED halves`）；§10.49 追加在枚举末尾 ⇒ 旧编号无位移 |

**这张表填完之后的三个结论**（都是**从表里读出来的**，不是另找的证据）：

1. **69 行里，两列 `*_emit` 今天全空**：`a`/`j` 对 69 个 opcode 零命中、`w` 只有 1 个且在注释里。⇒ **§6 第 2/3 步判据 ① 的「从 0 变成 69」有了精确的起点：不是 0，是「0 / 0 / 1（注释）」三个不同的零。**
2. **`s`（`selfhost/`）零命中的有 20 行**，其中 **11 行是 `S` 类**：`OP_DECLARE`、`OP_RECORD`、`OP_BE`、`OP_TRY_START`、`OP_TRY_END`、`OP_THROW`、`OP_SET_ADD`、`OP_THREAD_GOTO`、`OP_YIELD`、`OP_MAKE_FUNC`、`OP_CALL_VALUE`（另 9 行是 `P` 类：`OP_MOD`、`OP_NEW_SET`、`OP_SET_INTERVAL`、`OP_MIN`、`OP_MAX`、`OP_CONCAT`、`OP_LOAD_CAPTURE`、`OP_STORE_CAPTURE`、`OP_LOADK_I64`）。⇒ **`selfhost/` 引用的 48 个 opcode、C 编译器发射的 67 个、VM 分派的 69 个，是三个互不相同的集合**（§3.1.1 的三个数），而**这张表让「哪一个 opcode 只活在 VM 里」一眼可见**：**`selfhost/` 前端根本不知道 `OP_BE`、`OP_THROW`、`OP_DECLARE`、`OP_RECORD`、线程族与 `OP_MOD` 的存在** —— 这正是 §1 那个病的量化形式（`.im` 前端与 C 前端对同一门语言有不同看法）。**加强（2026-10，本会话实测，`exact-lumen` 提议的判据）**：这 11 个名字**在 `selfhost/` 的任何一个文件里出现次数都是 0**（`for op in …; do grep -rho "\b$op\b" selfhost/ | wc -l; done` → 11 个全部 **0**）—— **包括字符串字面量与注释**。⇒ 「`.im` 前端不知道它们存在」是**逐字成立**的（连名字都没被写过），而不只是「知道名字但不发射」。**这一条把结论从「没发射」升到「不存在」**，两者的修复成本差一个量级（前者是补发射点，后者是先要在前端定义这 11 个概念）。
3. **`raise` 列有 16 行非零、53 行零**；**`raise` 与「是否写帧外状态」正交**，这就是 §3.1.7 说「一个布尔不够」的量化形式。
   **⚠ 更正（2026-10，`exact-lumen` 指出、我核实后确认）：这不是两条独立路径得同一个数，是同一次测量被记了两遍。** §3.1.7 对 `can_raise` 的判据原文是「**块内出现 `vm_throw`**」，而本表 `raise` 列的判据是「**块内 `grep -c vm_throw`**」—— **同一个谓词**。⇒ 「16 = 16」证明的是**转录没错字**（手判那份 16 个名字的清单与机械复算一致），**不是**这条轴被独立验证。**轴是否成立仍要靠别的证据**：本节四条诚实边界与 §3.1.7 边界③（只读了 `src/vm/vm.c` 一个文件）**都还没被越过**。**我原先写「与 §3.1.7 的 `can_raise` 轴一致（可复核）」暗示了独立性，已按此更正。**

**诚实边界**：① `regs` 是「块内出现过的 `ins.rN`」的**上界**，不等于「该指令语义上使用的操作数」——例如 `OP_ADD` 用 `r1/r2/r3` 是真的，但某块若在诊断输出里提到 `ins.r2` 也会被计入；**逐行填 `aot_emit`/`wasm_emit` 时必须重新核**；② `声明点` 是**字面 grep**（`o in text`），因此**会把注释里的提及算进去**——`OP_LT` 的 `w` 就是这么来的；③ `vm_semantics` 那一列是**我读块写的摘要**，不是自动生成，**一条一句、不承诺穷尽**；④ `OP_RECV` 的块尾我取 `4680`（与 §3.1.7 声明的 `3146-4680` 一致），**`4681-4683` 是 `switch` 与函数的收尾，不属于指令体**。

**三种「两个来源一致」的价值分级（本会话同一轮里犯了第一种，故单列）**：本档多处写「X 与 Y 一致，可复核 / 互为独立复核」，但这些句子**不是同一种证据**，混用会让一个数看起来比它实际的支撑更强：

| 形态 | 例子 | 它证明了什么 | 它抓不住什么 |
| --- | --- | --- | --- |
| **① 同一个谓词录了两遍** | §3.1.11 结论 3 原写「`raise` = 16 与 §3.1.7 的 `can_raise` = 16 一致」——两处的判据原文都是「**块内出现 `vm_throw`**」 | **只证明转录没错字**（这一句话里的 16 是从上一句话抄对的） | 共读误读、谓词本身选错、以及「这个轴到底成不成立」——**全部** |
| **①′ 同一个根因录了两遍**（比 ① 更宽：**同根因也是同来源**） | §6.2 的 O13 行原写「这是 §3 的**第二个独立论据**」，同一句末尾却写「**两件事同一个根因**」——两句不能同时为真 | 同 ①：**只证明同一条根因有两种表现** | 它**不增加「根因存在」的证据**；只增加「代价是什么种类」（正确性 vs 性能） |
| **② 两个读者读同一份源** | §3.6 的 `RegInstruction` 无类型字段：`exact-lumen` 提出、我去读同一份 `src/compiler/bytecode.h` | **转述是否走样**（行号、字段名、是否真有那张表） | **共读误读**：两个人都把同一个形状读错时，两次复核一起错 |
| **②′ 两个读者 + 重跑同一条命令**（比 ② 多一个机械动作） | 本节末尾列的六处「**我独立复跑确认**」 | ② 的全部 **+ 命令有没有抄错** | **仍然抓不住谓词选错** ⇒ **不是 ③** |
| **③ 不同谓词 / 不同产物** | §3.1.8 的两个先例（`builtin_lookup` 的注册拒绝 + 门禁的 `grep -qF`；`orphan-fixtures` 的「`vtest/` 有什么 vs CTest 跑什么」） | **该性质本身成立** | 仍然抓不住「谓词选错了」——这要靠反向验证（§7 第 4 条、§6 第 2/3 步判据 ④⑦） |

| **④「体量差」不是行为差的证据**（`exact-lumen` 本轮自首，我登记） | 它这轮的三条推进全部由**体量差**驱动，**三条全错**；唯一正确的第四条来自**读循环上界** | **不成立**（连 ① 都不是：**不是同一个谓词，是一个没有谓词的推断**） | **它比 ① 更低，因为 ① 至少量过同一个东西** |

**④ 的三例（逐条，作为「长度差 ≠ 行为差」的实例集）**：
① `range(-2147483648, 2147483647)` 打印 `0`，判成缺陷 ⇒ **错** —— 本仓 `range` 是**类型域构造器**（`range(v)` ⇒ 该类型的全集），**不是数值序列生成器**；给两个实参时它按「第二个实参是 be-bound 索引、第一个是值」解释 ⇒ **它的探针发明了一个语义，然后从一个不接受该语义的实现那里报了一个「错误答案」**。
② `lower` Windows 249 字符 vs Linux 34 字符，判成分叉 ⇒ **错** —— Linux 那份是 `{ return posix_core_case(vm, 0); }`，而 `posix_core_case`（`src/runtime/runtime_posix.c:541`）逐字也是 `*p >= 'A' && *p <= 'Z'` 的 **ASCII-only** 实现 ⇒ **两份行为相同，只是拼写不同**。
③ `match` Windows 444 vs Linux 1336，判成「两个正则引擎、六个转义 Windows 不支持」⇒ **错** —— `src/runtime/runtime.c:846` 的 `re_class_char` **实现了** `\d\w\s\D\W\S`（裸用与 `[...]` 内都有），`re_match_elem` 见到 `\` 就派发给它 ⇒ **它是从体量差推断的，没读码**。
⇒ **④ 与 §9 第 21 条同族**：第 21 条说「**退出码**不是解析谓词」，这条说「**源码长度**不是行为差谓词」；两者与第 22/23 条合起来是**同一个洞的四个投影** —— **作者/检查器答的是「它们看起来一样吗」，而不是「它们算出同一个值吗」**。
**机制命名（它的归纳，我同意并采用）**：**「推断冒充测量」** —— 与它给 `vivid-anchor` 的「**机制没复现就不要声称机制**」是同一条纪律。
**本档自用规矩（立即生效）**：任何以「A 比 B 长 / 多 / 少」为**唯一**依据的行为主张，**必须同时给出读码位置或一次实测**，否则只能写「**未解释的差异**」—— §3.1.8 的 builtin 普查（我 431/241 vs 它 398/128）正是这种写法。
**⑤「注释声称的性质比断言实际检查的多」（本会话实测；`stream/builtin-contract-rulings @ 056e67b` 的 `CMakeLists.txt:1355-1359`，`release/051-final @ c71ea00` 上同一块是 `:1358-1363`）**：这条与前四条**方向不同**，所以单列 —— 前四条是「**检查器在看错的量**」，这一条是「**检查器看的量完全正确，只是看漏了那句注释自己承诺要看的一半**」。机制逐字：`:1352-1353` 的注释写 `Each length is paired: the impossible pattern must be false AND the possible one must be true, so that a non-fix which simply always answers false cannot pass`，而 `:1359` 的 `PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"` **只命名 `impossible` 那一句** ⇒ **一个恒 `return false` 的假修法照样通过**。fixture 那一侧**真的成对**（本会话实测：`./build/inimerse --no-mods vtest/match_long_pattern_v06.im` 输出六行、`RC=0`）⇒ **缺的不是数据，是断言**。**为什么它值得单列成一级**：它是 §3.1.8 那个先例的**反面** —— 那里门禁断言了「没有任何单个测试文件能看到」的性质；**这里注释写了一个比代码强的性质，而没有任何东西比较「注释说的」与「断言做的」**。⇒ **本档自用规矩（立即生效之二）**：凡注释里出现 `AND` / `both` / `paired` / `must also` 一类**并列承诺**，**必须能在断言里逐项指出对应物**，否则那条承诺只能当注释、不能当证据。详见 §9 第 26 条。

**本级的技术形状（`exact-lumen` 读 CMake 侧给出，本会话复核语法，已并入 §9 第 26 条）**：`PASS_REGULAR_EXPRESSION` 是**分号分隔的列表、每项都必须命中**（先例 `CMakeLists.txt @ e3da33c:690`；**原文这里写的是 `:673`，是陈旧指针**，随 main 前进 **+17** —— 锚在 `@ e3da33c:690` 的 `set_tests_properties(posix_runtime_parity PROPERTIES … PASS_REGULAR_EXPRESSION "posix-runtime-parity-ok;${INIMERSE_PARITY_LOAD_MISSING}" …)`），而 CMake 的 `cmsys::RegularExpression` **不带 `REG_NEWLINE`** ⇒ **`^`/`$` 只锚整段输出的首尾、不锚行**。⇒ 「把两件事塞进一条正则」时**第二半永远不命中**（`^` 要的是整段行首）—— **这不是正则写得含糊，是在一个只锚整段首尾的方言里写了以为在锚行的模式**。**⑤ 的两次实例要按 ① 级记，不是 ② 级**（`exact-lumen` 自己要求这么记）：它这一轮犯了**两次同形**的错 —— 一次在 `^` 上（**读起来像「两行都锚了」**）、一次在 `;` 上（**读起来像「两项都要满足」**，实际列表语义是**或**）—— **两次都没验机制，两次都只是「照抄了看起来对的形式」** ⇒ **① 级：只证明我照抄了一个看起来对的形式，不证明形式本身被验过。** 这一级比 §3.1.11 原表里的 ①（同谓词录两遍）**更强地适用于「模仿形态」**：写入者与写入者是同一人，且中间没有任何测量。

⇒ **并成的判据（本节结论，已按实测改正）**：**注释里的并列承诺必须有对应物，而对应物不能塞进同一个 `PASS_` 列表**（那只是「或」）—— 需要两个**不同种类**的属性（`PASS_` + `FAIL_`）或两条断言各管一半；找不到对应物时要么补断言、要么删承诺 —— **注释不成立比断言不够更坏，因为注释会被下一个读者当成证据**（与 §3.1.11「读起来像 ③、实际是 ①、永远高估」同向）。

⇒ **规矩：写「一致」时必须同时写明是上面哪一种**，否则读者只能从上下文猜，而猜错的方向总是**高估**（因为「一致」读起来像 ③）。

**全文普查结果（2026-10，`grep -n '一致' docs/DECFY_DESIGN.md` = 48 处命中，含本节）**：48 处里**只有 7 处是「把一致当证据用」**，其余是规范句（「必须与之一致」「保持一致性」）、纯对照表标题、或与 `docs/AUDIT.md` §5 的一致性**声明**（§8），那三者都**不是证据主张**、不需要分级。7 处逐条判级：

| 位置 | 主张 | 级 | 理由 |
| --- | --- | --- | --- |
| §1.2 | `%` 修复后「**16/16 三后端一致**」 | **③** | 解释器 / AOT / wasm 是**三个不同实现**，逐格比对是在拿三份产物互证；这是本档**最强的**一类 |
| §6.1 更正三 | wasm 数组「与解释器在**四条用例**上逐格一致」 | **③** | 同上（两个不同后端 + 实跑用例） |
| §6.1 1.5a | **18/18** 标量真值用例解释器与 wasm 相同、0 分歧 | **③** | 同上；**但注意它同时是「18 个自选用例」**——覆盖面是选择性的，见 §6.1 已有的诚实边界 |
| 本节 | 「**37 + 3 + 29 = 69**（与 §3.1.1 的数一致，可复核）」 | **①** | 分区与总数多半来自**同一份枚举** ⇒ 等式抓得住**加法错**，抓不住**分类错**；应改读为「算术自洽」，不是「数被复核过」 |
| 本节 结论 3 | `raise` = 16 与 §3.1.7 的 `can_raise` = 16 一致 | **①** | 已更正（本节开头那张表的第一行） |
| §3.6 | `RegInstruction` 无类型字段，「我在此独立复核」 | **②** | 已降级措辞（见 §3.6 的订正块） |
| §3.1.8 builtin 普查 | 「我的扫描与之一致」 | **②** | 两份 grep 读**同一份源**；而且它们**其实不一致**（我 431/241 vs `exact-lumen` 398/128，差异未解释、未调和）⇒ 连 ② 的「转述没走样」都没拿到，**§3.1.8 的普查一节已诚实登记** |
| §6.2 的 O13 行 | 「这是 §3 的**第二个独立论据**」+ 同句末尾「两件事同一个根因」 | **①′** | `exact-lumen` 指出（我复核：**这两句不能同时为真**）。**已改读为「同一条根因的第一个『不同种类』的症状 —— 不是第二个论据」**；它支持的是 **§3 的诊断本身**（承重结论），故这一处比本节结论 3 更值钱 |

**六处「我独立复跑确认」是 ②′ 级，不是 ③ 级**（`exact-lumen` 指出我上一轮的普查键锁死在一个拼法上）。它们在文档里原样留着，逐字片段与新标的级：

| 节 | 级 | 说明 |
| --- | --- | --- |
| §3.1.9（零生产点 opcode） | **②′** | 「这条由 `exact-lumen` 提出，**我在本会话独立复跑确认**」 |
| §3.1.10（同一个语义归谁） | **②′** | 「`exact-lumen` 的更正二（我独立复跑确认）」 |
| §3.9 更正一（`be` 的 `=` / `:`） | **②′** | 「更正一（2026-10，来源 `exact-lumen`，**我独立复跑确认**）」 |
| §3.9 更正一的实测表 | **②′** | 「**实测（本会话独立复跑，两条程序逐字）**」 |
| §10 的「第八轮：零生产点 opcode + 注释损坏」`[实测]` 行 | **②′** | 「① **零生产点**（`exact-lumen` 提出、**我独立复跑确认**）」 |
| §10 的「第十一轮它给的四条」`[转述]` 行 | **②′** | 「① **更正一**（10/10 → 8/10，`be` 只认 `:`）——**我已独立复跑确认，见上两行**」 |

**注**（`exact-lumen` 的观察，我同意）：上表后三行（§3.9 更正一 ×2、§10「第十一轮」×1）的**降级损失最小**（两个 fixture 的证据是**一条退出码与一个字面错误串**，不是一次阅读），**但它们仍然不是 ③**，仍该带标。**这六处的用词没有逐处改**（改一次要重排整段），而是**集中记在这里并给出节号** —— 若将来有人要引它们当证据，来这张表看级数。

**另一个词：`独立` 在本文档里承担三种意思**（`exact-lumen` 指出，我复核时 `grep -c '独立' docs/DECFY_DESIGN.md` = **37**，是**改动前**的计数；写下这一段之后该数是 **49**，因为**本表自己就在引用这个词** —— 这正是「grep 一个词」和「测量一个形状」的差别）。这正是 §1 那个病**出现在文档自己的身体里**：

| 义 | 意思 | 例（逐字） | 处置 |
| --- | --- | --- | --- |
| **轴义** | **正交**的第二维 | **§3.1.3** 的 `can_raise` 注释（原文 `int  can_raise; /* 独立第二轴 … */`，今天读作「正交第二轴」）、**§3.1.3** 紧随其后的用词订正段（原文「互相独立」）、**§3.1.7** 的 `can_raise` 清单（原文「独立第二轴」）、**§9** 里那处 ③（原文「独立第二轴」；**该号今天读到的是 §9 的开场句，不是它**） | **已全部改成「正交」**（四处）—— 这一步让 `grep -n 独立` 第一次成为本普查的**正确口径** |
| **验证义** | 第二个人／第二次复核 | **§3.1.8** 的「第二个先例」一节、**§3.1.9**、**§3.1.10**、**§3.1.11**（「本档自用规矩」段等）、**§3.2**、**§3.9 更正一**（两处）、**§10**（两行）—— **原列的十个号今天已有两个落在空行上** | **不改词，改级**（上表：② 或 ②′） |
| **归属义** | **不是我们写的** | **§6.1** 的诚实边界段 「`node tools/wasm_run.js` 是仓库自带的运行器、**不是独立第三方 wasm 运行时**」 | **不改** —— 此处「独立」是对的说法，指的是宿主 |
| （第四种，**本档论题本体**） | **分开的、各自的**决定点 | **§0**、**§1**、**§2 (a) 层次划分表**「多个独立决定点」「第三个独立语义决定点」 | **不改** —— 这是核心论题用词，「独立」= 「互相分离的」 |

⇒ **教训（比这一条本身重要）**：我上一轮把「普查一个语义」等价于「grep 一个词」，**而这个语义在中文里有至少四个拼法/义项**。**`grep -c` 出来的那个数是「这个词出现几次」，不是「这个形状发生几次」** —— 与 §7 第 4 条「一个从未在真实反例上跑过的检查不是证据」同源：一个**键选错了的普查**会给出一个整洁的、看起来完整的、且**结构上不可能抓到漏掉那一半**的数。

⇒ **普查后没有发现新的 ① 级冒充 ③ 级的用法**（唯一那处 §3.1.11 结论 3 是 `exact-lumen` 先抓出来的）。**但这条结论本身是 ② 级**——是我一个人读全部 48 处，**没有第二个人复核我的判级**。§10 `[待复核]` 的 ④ 按此更新。



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
| **B′**：第 2 步只统一控制流，wasm 保留自己的 16 字节槽 | 1.5c（在 wasm 实现字符串与字典/集合；**数组已实现**）**是那时的前置** | 未选 |

**A′ 的三条后果（与 §6.1 末段 A 的三条后果同形，写下来免得下次有人又去扩 `SLOT_BYTES`）。**

1. **不要为「让 `cg_cond` 的 `else 1` 可达」去单独扩 wasm 的值表示**——那会造出第三套要在第 2 步整体删掉的值表示，正是本档 `:10-12` 记的病。
2. **wasm 后端与 AOT 后端改成同一件事**：都从「自己遍历 `Expr*` 并自带值表示」变成「消费同一份 IR，按 IR 的类型信息落值」。⇒ 两边的改动应当**在同一批**里做，否则会出现「AOT 已吃 IR、wasm 还在遍历 AST」的中间态，而这个中间态**没有任何判据能证明它是对的**。
3. **`src/compilation/wasm_backend.c:4` 的陈旧头注释（8 字节）必须与 `:66` 的 16 字节一起改**，否则下一个读注释的人会把槽宽写错——本档 §7 第 4 条的第一例（`src/compilation/aot_native.c:420-428` 那句 "C's && and || short-circuit exactly as the interpreter's do"）就是这么来的。

**本文档不替人做这个裁定**这一句**已作废**：裁定已于 2026-10 由人类给出，为 **A′**。

### 3.6 裁定 A′ 里有一个它自己没说的地方：**IR 今天没有类型信息**

上面 §3.5 后果 2 的措辞是「按 **IR 的类型信息** 落值」。**这句话里的「IR 的类型信息」今天不存在。**
（本条由 `exact-lumen` 在复核 §3.1.4 时提出，我在此**复核**：`src/compiler/bytecode.h:69-74` 的 `RegInstruction` 是 `{ OpCode op; int r1, r2, r3; }` —— **三个字段全是下标，没有任何类型字段**；`:87-128` 的 `Bytecode` 也没有类型表。）
> **措辞订正（2026-10，与 §3.1.11 结论 3 的更正同源）**：我原先在这里写「**独立**复核」。按 §1 那个病的判据，这只是**两个读者读同一份源**，**不是**独立验证 —— 它**能**抓住转述走样（它引的行号/字段是否真是那样），**抓不住**共读误读（两个人都把同一个形状读错了）。真正的独立验证需要**不同的谓词或不同的产物**（例如它与 §3.1.11 那次用的是同一个 `vm_throw` 谓词，因此两次记录**连转述走样都抓不住**）。三种「看起来一致」的价值分级见本节末的分类。

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
- **它被构建、被测试**：`CMakeLists.txt @ e3da33c:417` 把它编进引擎；`@ e3da33c:157-159` 有 `typeset_probe` 这个 CTest（**本会话复核：两个号均未漂移** —— `:417` 在 `66b12ca`/`e313c0f`/`e9debfd`/`e3da33c` 上都是 `src/types/typeset.c src/types/enum.c src/types/error_types.c src/types/registry.c`；`:157-159` 是 `add_executable(typeset_probe …)`/`target_include_directories(…)`/`add_test(NAME typeset_probe COMMAND typeset_probe)`），断言内容正是 ROADMAP 点名的那几条 —— `im_typeset_int_interval(0, 255, true, true)`（**u8 窄化**）、与 `im_typeset_int_interval(0, INT64_MAX, false, true)` 的**交**、与枚举集合的**并**（`src/types/typeset_probe.c`）。
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
3. **「无法证明 ⇒ 编译错误」是一条今天不存在的语言规则，且它会让今天合法的程序编译不过。** 今天 `be` 是**纯运行期检查**，编译器**从不拒绝**（`src/compiler/compiler.c:2232-2240` 无条件发射 `OP_BE`）。⇒ 这条规则**新增了一项编译器的否决权**，而**没有任何东西量过有多少 `.im` 会因此被拒**。同样，`@runtime_check` 是**新语法**，今天不存在（`grep -rn "runtime_check" src/ selfhost/ docs/` 待做，我**没有**做）。**⇒ 这两项在 §6 里各自需要一条「先量后改」的前置判据，不能直接写进实现步。** **⚠ 这是记录，不是现状**：`be` / `STMT_BE` 已于 2026-10 移除（`c89e077`，2026-10-05），见 §3.9 抬头的记录牌。

#### 这条裁定对 §3 与 §6 的直接后果（登记为 §9 第 17 条）

- **§3 的 69 行表要多一列**：每个 opcode 需要标注**它是否受编译期契约覆盖**（即：在静态模式下这个 opcode 是否可能被编译期消除）。今天表里的 `can_raise` 与 `declared_at`（原 `producers`）两列都不表达这件事。**这是 B″ 与 (ii) 叠加后的新列，不是原表的冗余。**
- **§6 需要两条新判据**（都可证伪）：
  1. **保守性判据**：对每一个由编译器标注了类型的寄存器，**运行期实际 `Value.type` 必须属于该编译期集合**（`type_of(实际值) ∈ CompilerSet`）；**故意把某处 `CompilerSet` 收窄成不含实际值的一格，该测试必须变红。**
  2. **主从判据**：在静态模式下，**凡编译期已证明安全的位置，`OP_BE` 不得出现**；凡 `OP_BE` 出现的位置，必须是动态模式 / 显式 `@runtime_check` / Debug 断言三者之一。**判据形式**：对同一段 `.im`，静态模式产出的字节码里 `OP_BE` 的出现次数为 **0**（或全部落在上述三类白名单位置），而动态模式产出里 `OP_BE` 保留 —— **同一份源码两种模式，产出必须都符合各自的规则**。

### 3.9 裁定已下：**先量爆炸半径，以「静态模式 `OP_BE` 为 0」为终极目标**（2026-10，第五个人类裁定）+ 实测结果

§3.8 指出裁定文本内部 `OP_BE` 有两个角色（「编译期无法证明时的兜底」vs「静态模式下无法证明 ⇒ 编译错误」），不能同时成立。我提了三选一；**人类裁定：短期执行 (C) 先量爆炸半径，长期锚定 (A)「静态模式下 `OP_BE` 必须为 0」**。裁定原文的要点： **⚠ 记录牌（2026-10，`exact-otter` 裁定）：本节是 `be` 构造移除（`c89e077`，2026-10-05）之前的记录。** 下面每一处 `STMT_BE` 引证、两条负向 fixture、十文件普查**都是那棵树上的读数，今天全部不再指向现行代码**；现行形式是 `src/parser/parser.c` 的 `STMT_BIND`（`:1415-1420`）、`docs/SYNTAX.md` §6.3「`be` 语句（已移除）」、以及新增的 `vtest/be_removed_*` 负向 fixture。**降级不是改写 —— 下面每一个读数在被取的那一刻都是真的。**

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
| ~~`vtest/lint_case_membership_v04.im:2`~~ | `dir be Direction = "N"` | **~~具名类型~~ ⇒ 解析错误**（`=` 不是 `be` 的分隔符，见本节末「更正一」） | **❌ rc=1** |
| ~~`vtest/lint_case_enum_v04.im:2`~~ | `dir be Direction = "N"` | **同上** | **❌ rc=1** |
| `vtest/type_collection_v04.im:3` | `x be Byte: 42` | 具名类型，声明在同文件 `:2` = `type Byte = [0~255]`（**字面量区间**） | ✅ |
| `big_globals_test.im:168` | `g200 be N : 42` | **内建类型** `N`（`src/vm/vm.c:1871` 的 `"N","Z","Z+"…` 名单，`:1880` `if (strcmp(name,"N") == 0) return 0;`） | ✅ |
| `nsadv_mod.im:3` | `q be N : 5` | 同上 | ✅ |

⇒ **今天爆炸半径的上界 = 0 个「不可解析约束」的实例。** 按裁定自己的执行路径（「**如果只炸几个，立刻执行 A 的落地口径**」），数据支持**直接采用 (A) 的口径**——**但有一个前提必须先说清**：这些条是我**按分类规则手工判定**的，**不是分析器跑出来的**；「不可证明」这个词在分析器存在之前**没有可执行的判据**。⇒ **正确的读法是「上界 10，且可解析的那 8 条全部属于『可解析』一类」，不是「已证明 0 处不可证明」。** **⚠ 记录牌（2026-10，本会话实测）：本表的 ✅ 只保证该约束表达式可解析，不保证该文件能跑。** 实测（`main @ 28ade0a`）：10 个里 9 个 rc=0，`meta_test.im` rc=1，原因与 `be` 无关（§7.2 M2 的 `->` 双重身份，`:14` 的 `b->float`；移除前快照即报同一个错），且**没有任何 CTest 观测它**（`grep -c meta_test CMakeLists.txt` = 0）。另：10 个里 7 个没有 `add_test` 守卫。

**顺带两条与 `docs/SYNTAX.md` 的互相印证**：`docs/SYNTAX.md:550` 原说 `be` 是「仓库里最不常见的语句形式之一」——**346 个文件里 10 处，实证了这句话**；**⚠ 那句话与它描述的构造今天都已不在文档/语言里**：`be` 已于 2026-10 被移除（`docs/SYNTAX.md` 现 §6.3「`be` 语句（已移除）」，旧写法现在是一条带行号的解析错误），原句随构造一起删掉，**原号 `:550` 留作记录**；而 `docs/SYNTAX.md:546`（**该号今天已不是它，那两句话现在住在 §6.3**）给的形式是 `name be <集合或表达式> [: init]`，上表里两处用了 `=` 而不是 `:` ⇒ **本节第一版把它登记成「文档与语法的不一致」，是错的：`docs/SYNTAX.md` 是对的，错的是 fixture。见下面的「更正一」。**

**诚实边界**：① 上表 10 条是**我手工分类**的，判据是「约束表达式能否只靠字面量/内建/同文件具名声明解析」，**不是**任何静态分析器的输出；② 正则只匹配「行首 标识符 空白 `be` 空白」这一种形状，**漏掉**别名/带下标的名字、`be` 前有换行的续行、以及非常规空白；**误报**（把表达式里恰好叫 `be` 的标识符当语句）**未排查**；③ ~~我**没有**检查 `type X = <依赖运行期值的表达式>` 这种**类型声明本身不可静态求值**的情况~~ —— **已在下面第二轮补量，结论是该情形在全仓不存在**；④ 「内建类型 `N` 可解析」是从 `src/vm/vm.c:1871/1880` 的**名字名单**读出的，**没有实跑**编译器去确认它对 `be N` 是否真的在编译期求值。

#### 补量：**`type` 声明侧也没有不可静态求值的实例**（同一轮，随后实测）

§3.7 记的真问题是「今天类型是**运行期的值**」（`type X = <集合表达式>` 编译成 `OP_STORE_GLOBAL`，`src/compiler/compiler.c:2224-2231`），所以只查 `be` 的**使用点**不足以支撑「上界 = 0」——还必须查**类型声明本身**能不能静态求值。命令（同样覆盖全仓）：

```
grep -rnE '\btype[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=' --include=*.im .
```

**结果：全仓只有 11 处 `type X = …` 声明，全部在 `vtest/` 下，且 RHS 全部是可静态求值的三种形状之一：**

| 形状 | 实例 | 计数 |
|---|---|---|
| 字面量枚举 | `type Direction = "N", "S", "E", "W"`（`vtest/lint_case_membership_v04.im:1`、`vtest/lint_case_enum_v04.im:1`）、`type FileError = "not_found", "permission_denied", …`（`vtest/lint_case_try_alias_v04.im:1`、`vtest/lint_case_try_members_v04.im:1`、`vtest/lint_case_try_v04.im:3`、`vtest/case_try_v04.im:23`）、`type ParseError = "invalid_syntax"`（`vtest/lint_case_try_alias_v04.im:10`） | **7** |
| 字面量区间 | `type Byte = [0~255]`（`vtest/type_collection_v04.im:2`）、`type Positive = [1~100]`（`vtest/case_collection_patterns_v04.im:8`） | **2** |
| **具名类型引用（别名 / 并集）** | `type AppError = FileError`（`vtest/lint_case_try_alias_v04.im:2`，**别名**）、`type CombinedError = FileError + ParseError`（`vtest/lint_case_try_alias_v04.im:11`，**类型层的集合并集**） | **2** |

⇒ **没有任何一处 `type X = <依赖运行期值的表达式>`。** 那两种「具名引用」形状也是可静态求值的（沿引用传递，并集就是两个可解析集合的并），**前提是类型求值器支持传递引用与 `+`**——**这正是 `src/types/typeset.c` 已有的能力**（`im_typeset_union`，`src/types/typeset.h:37-45`）。⇒ **`be` 使用点与 `type` 声明两侧的普查都指向同一个结论：今天不可静态求值的实例是 0 个（手工分类口径）。**

#### 更正一（2026-10，来源 `exact-lumen`，**我独立复跑确认**）：不是 10/10 可解析，是 **8/10 可解析、2/10 是解析错误**

本节第一版写「10/10 可解析」并把它当作「上界 0」的证据。**这半句是错的，必须更正**，否则本节会替一个假前提背书。

**语法事实（读码）**：`be` 与 `type` 是**两个语句、两种分隔符** ——
- `be`：`src/parser/parser.c:1383-1391`，`stmt->beStmt.set = looks_like_set_start(p) ? parse_set_literal(p) : parse_expr(p);` 之后 `if (match(p, TOK_COLON)) stmt->beStmt.init = parse_expr(p);` ⇒ **只认 `:`，不认 `=`**。
- `type`：**另一个函数** `parse_type_stmt`（`src/parser/parser.c:1218-1227`），`consume(p, TOK_EQ, "'='");` ⇒ **只认 `=`**。

**实测（本会话独立复跑，两条程序逐字）**：

| 程序 | 结果 |
|---|---|
| `type Direction = "N","S","E","W"` + `dir be Direction = "N"` + `say dir` | **rc=1**，stderr `Error: expected 'expression', but got '=' (type 83)` |
| 同上但 `dir be Direction : "N"` | **rc=0**，stdout `N` |

⇒ 上表那 **2 处**（都在 `vtest/`，都是 `dir be Direction = "N"`）**是解析错误，不是「具名类型」**。其余 8 处全是 `:`、全部 rc=0。**「同文件具名类型」那一类实际是 1 处合法（`vtest/type_collection_v04.im:3` 的 `x be Byte: 42`）+ 2 处解析错误。** 「上界 0 个不可静态求值实例」这个结论**不受影响**（那两处的约束表达式本身仍是字面量/具名类型），但**「10/10 可解析」这句作废**。

**为什么一年没人发现（这一段比错误本身更重要）**：这两个 fixture **只被 `--lint` 跑，而 `--lint` 路径不打印解析错误**。本会话实测：

```
$ ./build/inimerse --no-mods --lint vtest/lint_case_enum_v04.im ; echo rc=$?
[lint] line 3 [WARN] finite case type 'Direction' is missing members: E, W
rc=1
$ ... | grep -c "expected 'expression'"   # → 0
```

rc 是 1，但**一个字都没说为什么**；而 CTest 的 `PASS_REGULAR_EXPRESSION` **优先于进程退出码**（匹配上就算通过）⇒ **绿着，而 fixture 描述的程序从来没有被解析过。** 全仓 67 个 `vtest/*.im` 里只有这 2 个吐解析错误。⇒ **这是 §7 第 4 条那个病的新形态：不是「检查是坏的」，而是「检查看得见的那个通道，和程序真正走的那个通道，不是同一个」**（`--lint` 的 stdout 有警告，`--lint` 的解析失败不在这条通道上）。**它同时是 §3.1.8 第二个先例的反面教材**：那个检查（`orphan-fixtures`）至少会因孤儿而变红，这两个 fixture 的「覆盖率」永远不会。

**修法**：把两处 `=` 改成 `:`（`exact-lumen` 实测 `--lint` 输出逐字不变）。**我未改任何 `vtest/` 文件**（不在本文档写域），已由 `exact-lumen` 报给 `vivid-anchor` 定是否进 0.5.1。**这条也回填 §9 第 17 条的第 ① 项**：那份「先量后改」的普查上界应从 **10** 修正为 **8**（可解析的实例），并额外记下**2 处 fixture 解析错误**这个与 (A) 无关但必须修的事实。

**「补量：`type` 声明侧」那一轮新增的诚实边界（保留）**：⑤ 「11 处」是**同一套正则口径**下的计数（`\btype\s+ident\s*=`），**没有**排除 `type` 出现在注释或字符串里的误报；⑥ 那 2 处具名引用（别名 / 并集）我**判定**为可静态求值，但**没有实跑**编译器确认它对 `AppError` / `CombinedError` 真的在编译期求值——**而更正二（见 §3.1.10）实测证明：`CombinedError` 的并集确实算出来了，但不是在类型层算的，是在 VM 运行期由 `L_ADD` 调 `set_union` 算的。⇒ 边界⑥ 从「未确认」升级为一个确凿的新发现。**

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
| **0** | 冻结唯一语义表；采纳 `docs/AUDIT.md` §5 的 **O0 选项①**（~~值语义胜出~~ → **已由用户裁定改为布尔**，见 §0.5 / `docs/STATUS.md **§10.42**（`@ e9debfd` → `:2485`）`；改动落在解释器与 wasm，AOT 本就正确） | ① `tools/im_diff_fuzz.py` 重跑，`and`/`or` 类分歧 **= 0**（§0.5：已进门禁并归零）；② `docs/API.md:90` 明写 `and`/`or` 返回**操作数**还是布尔 —— **已定为布尔**；③ `AUDIT.md` §1.6 的八行表格**三后端逐格相同**（解释器 / AOT / wasm；wasm 必须从 `error: wasm MVP subset: 'and'/'or' outside a condition is not supported` 变成给出值） |
| **1** | 两个编译器统一 `and`/`or` 降级（短路跳转 + 真值常量）；顺带定夺 `OP_AND`/`OP_OR` 去留（§0.5 复核：§10.42 已给两者活语义，**该决定仍未做**） | `tools/selfhost_compare.py` 的 opcode 序列判据在 `or` 探针上**零差异**（AUDIT §6 的 `orv.im` 探针） |
| **1.5a** | **已核实关闭（2026-10，实现会话实测）**：`src/compilation/wasm_backend.c` 的 `cg_cond` 语义**已经**与 `vm_truthy()` 同表，缺的是路径**可达**；而「可达」= 在 wasm 实现字符串与字典/集合（见 1.5c；**2026-10 更正三：数组已经可达**） | **无可修，故无新判据**：原三条判据分别「已由实测满足（18/18）」「无对象」「无需触发」。**未新增任何 CTest**（§6.1） |
| **1.5b** | **AOT 半：已由人类裁定走 A（2026-10）⇒ 不做。** `NV`（`src/compilation/aot_native.c:174`，三个 tag `:175-177`）无 nil/字符串/容器表示，但第 2 步让 AOT 消费带 `Value` 语义的同一份 IR ⇒ `NV` 被整体替换 | **不适用（已裁定不做）**；原判据 `func_no_return` 分歧**归入第 2 步**（§6.1 末段） |
| **1.5c** | **新立：在 wasm 实现字符串与字典/集合**（值表示 + 打印导入 + 堆生命周期）。**2026-10 更正后范围收窄**：数组**已经实现**（`EXPR_LIST` + `EXPR_INDEX`，见 §6.1 更正三），原写「非标量」过宽。**已由人类裁定 A′（2026-10）⇒ 不做「独立扩值表示」，改为「把 IR 的值语义映射到 wasm」，归入第 2 步**（§3.5）。**与 1.5a 分开命名**，否则计划里会一直写着「1.5a 便宜」 | 随第 2/3 步的表一起钉（§3.1.3 的 `OpSemantics` 非标量行） |
| **1.6** | **补全反汇编器缺的 21 个 opcode 命名**（§9 第 12 条，`src/vm/vm.c:5208-5266` 的 `vm_disasm_ins`；清单逐字见 §3.1.6）。**已由 `exact-lumen` 与人类裁定排在**第 2 步**之前**：缺的 21 个恰好是集合/区间/闭包捕获/线程 —— 正是 §3.1.4 与 1.5c 最难的一半，而人类可读 dump 是调试它的主要手段；它**便宜、独立、零回归**（不改变任何发射或分派） | ① `grep -c 'case OP_'` 在 `:5208-5266` 区间内从 **48** 变 **69**；② 断言「反汇编器的 case 集合 == `src/compiler/bytecode.h` 的枚举集合」，**正反两向差集都为空**（与 §3.1.8 两个先例同形的集合关系判据）；③ **反向验证**：删掉一个 `case OP_BE:`，② 的断言必须变红。**注意不要与 `src/main.c:675-681` 的数字 dump 混淆** —— 那条路故意不经过反汇编器（注释逐字 `Kept byte-comparable on purpose`），是稳定判据，**不要改它** |
| **2** | `src/compilation/aot_native.c` 改吃字节码（§3.1.3 / §3.2） | ① `grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c \| sort -u \| wc -l` 从**今天的 0** 变成 **69**（或逐条列出未实现的 opcode 与理由）；② `grep -c 'RegInstruction' src/compilation/aot_native.c` **> 0**；③ `OpSemantics` 表的行数 **= 69**，且有 CTest 断言**表行数 == `bytecode.h` 枚举成员数**（防漏行）；④ **反向验证**：故意改错表中一行，③ 的 CTest 必须变红；⑤ `tools/aot_native.test.py` 的 `DIVERGENCE` / `EQUIVALENCE` 计数**不变**（零回归）。**⚠ 判据 ① 的措辞有两处必须写清（2026-10，§3.1.11 填表后才有据可依）**：(a) 起点不是「0」而是 **「`a`=0 / `j`=0 / `w`=1」三个不同的零**（见第 3 步 ⑥）；(b) **注释不算生产点** —— `w` 那唯一 1 个命中是 `src/compilation/wasm_backend.c:1500` **注释里**的 `(OP_LT double compare)`，它会被 `grep -c` 算成命中。⇒ 判据必须写明「计数只统计**代码中的** `case OP_`」，否则将来有人**删掉那条注释**就会看到计数从 1 掉到 0、误以为进度**倒退**（而实际什么都没变）。**一个把注释也算进去的计数不是进度表，是一张会被注释编辑改写的表。** |
| **3** | `src/compilation/wasm_backend.c` 同改（§3.1.3 / §3.3），**且必须与第 2 步同批**（§3.5 A′ 后果 2：只改一边会造出没有判据可证的中间态） | 同第 2 步的 ①–⑤（**含「注释不算生产点」那条限定**），跑 wasm 目标；外加 ⑥ `grep -oE 'case OP_[A-Z0-9_]+' src/compilation/wasm_backend.c \| sort -u \| wc -l` 从**今天的 1（`src/compilation/wasm_backend.c:1500` 的注释，不是代码）** 变成 **69**。**⑦ 补一条反向验证**：把 `:1500` 那条注释里的 `OP_LT` 删掉，**⑥ 的计数若被断言成「必须 ≥ 1」就会变红** —— 这条验证的作用是**证明判据里真的写了「注释不算」**，否则它就是一句没人验过的声明（与 §7 第 4 条「一个从未在真实反例上跑过的检查不是证据」同形） |
| **4** | `%` 与宽度契约统一（§1.2 / §1.3） | `2147483648 % 7` 等 **四组**预测值在**五条通道**给出同一整数（`docs/AUDIT.md` §1.1 已备好该四组值；五条通道见同文件 §2.1） |
| **5** | 回头重新审视 `tools/aot_native.test.py` 里三条 `DIVERGENCE` 钉死项（`AUDIT.md` §5 末尾明写要求） | 三条中与 §1.1/§1.2 同源者可升为 `EQUIVALENCE`；升不了的必须写明为何**不是**同源 |

**顺序是先决关系，不是偏好**：第 0 步不落地，第 1 步就没有正确目标（会照着错的语义表统一）；第 1 步不做，第 2/3 步改完后 `and`/`or` 仍会与解释器不一致。**第 1.5 步原先被写成第 2/3 步的前提，2026-10 实测后这条依赖改了**（§0.5.1）：1.5a 无可修、1.5b 已裁定不做、1.5c 被 A′ 收进第 2 步 ⇒ **第 1.5 步不再挡在第 2 步前面**。**第 2 与第 3 步必须同批**（§3.5 A′ 后果 2）。**新增第 1.6 步（2026-10）**：它是**唯一的「越靠前越便宜」的步骤** —— 不改变任何发射或分派，只让缺的 21 个 opcode 在 dump 里可读，而第 2/3 步的调试正要依赖那份可读性；**它与第 1.5 步无关，也不被 A′ 吸收**，`exact-lumen` 与人类都把它的位置定在第 2 步**之前**。

### 6.1 已选定的下一步：先把证明覆盖扩到字符串与容器（第 1.5 步）

> **本节在 2026-10 被实测更正过一次，且更正的方向是「成本比原先写的高」。** 原文说「给 AOT 的 `nv_tru` 与 wasm 的 `cg_cond` 补上与 `vm_truthy()` 同表的非标量分支」——实测发现**两半都不成立**：wasm 那半**不需要改语义**（它已经同表），AOT 那半**改不了**（`NV` 根本没有对应表示）。原文把这条估成一个小步骤，**估错了**；下面是更正后的版本。

**为什么是这一步，而不是直接做第 2 步。** §3 的 IR 收敛是结构修法，做完之后要回答的问题正是「你怎么知道搬对了」。今天的判据（`tools/aot_native.test.py` 的 `EQUIVALENCE` / `DIVERGENCE`、`tools/im_diff_fuzz.py`、`tools/selfhost_compare.py`）**只覆盖标量**：两个编译后端 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` 都是 **0**（**2026-10 更正：该 grep 里 `EXPR_ARRAY`/`EXPR_SET` 都不是枚举成员，故它的一部分恒为 0；见 §6.1 更正三**）。⇒ 在扩覆盖之前动 §3，等于**把「无法验证」原样搬进新架构**。

> **2026-10 实测结论：这一步的「便宜的那半」不存在。** 实现会话（`exact-lumen`）按本节判据去测 wasm 能否产出一个字符串，三条程序全部拒绝：
>
> ```
> say 1 + 2        RC=0   compiled
> say "hi"         RC=1   error: wasm MVP subset: strings are not supported by the wasm MVP subset (line 1)
> s = "hi"; say s  RC=1   error: wasm MVP subset: expression type 2 not supported by wasm MVP subset
>                         (strings/collections need the interpreter) (line 1)
> ```
>
> ⇒ **wasm 连一个字符串字面量都产不出来**，所以 `TAG_STR 4`（`:58-63`）**今天不可达**。**「让非标量可达」= 在 wasm 后端实现字符串与容器 = §2 表第 ⑤ 类**，与 1.5b **同类工作量，只是落在另一个后端上**。⇒ 本节原先写「1.5a 便宜」是错的；理由**不是**语义要改（语义**确实**不用改），而是缺的是**整个值表示**。
> **2026-10 更正（见更正三）**：本段原文把 `cg_cond` 的 `else 1` 与 `TAG_STR 4` 并列成「都不可达」，**这是错的**——`else 1` 今天可达（数组走进去），只有 `TAG_STR 4` 是死的；「非标量」也过宽，数组已实现。**「1.5a 便宜」是错的这个结论仍然成立**，只是原因精确化为「字符串与字典/集合的整个值表示不存在」。

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

**更正一：wasm 那半不需要改语义，缺的是「走得到」——但「走不到」的范围被本节原先写大了。** `cg_cond` 的尾部（`src/compilation/wasm_backend.c:834-847`）实测已经是：`NIL → 0`；`INT`/`BOOL → i != 0`；`FLOAT → f != 0.0`；**其余 tag → 1**。这与 `vm_truthy()` 的 `default: return 1` **逐字对应**。⇒ `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3192`）` 里「对非 INT/BOOL/FLOAT 直接返回 1」**不是缺陷，是正确行为**。wasm 的 tag 空间**已经有** `TAG_STR 4` / `TAG_ARR 5`（`src/compilation/wasm_backend.c:58-63`），且 `:406`、`:1063` 已有 `TAG_STR` 发射点。
> **本句原文写「真正的缺口是那条路径**不可达**（字符串/容器表达式根本不编译）」，2026-10 被实测**部分推翻**——见下方更正三：**数组那一路是可达的、且已经跑通**；不可达的只有字符串与字典/集合。**更正一仍然成立的那一半**：wasm 的真值语义确实一行不用改，语义已经是对的。

**更正三（2026-10，本会话实测；**推翻了本节与 §0.5.1 里一处共同论断**）：那条 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` = 0 的判据本身是坏的，而它掩盖了「wasm 已经实现了数组」。**

- **`EXPR_ARRAY` 在整个 `src/` 里出现 0 次**（`grep -rn 'EXPR_ARRAY' src/ | wc -l` → **0**）——**它不是 `ast.h` 的枚举成员**。`EXPR_SET`（裸名）也不是；真名是 `EXPR_SETLIT` / `EXPR_SETCOMP` / `EXPR_SETINTERVAL`。**数组字面量的真名是 `EXPR_LIST`。** ⇒ 那条 grep 有三个分支里**两个永远不可能命中**，它的「0」是**恒真**，不是证据。`src/parser/ast.h` 的全部 20 个表达式种类：`EXPR_ARROW_CAST` `EXPR_BINARY` `EXPR_BOOL` `EXPR_CALL` `EXPR_CHAIN_COMPARE` `EXPR_DICT` `EXPR_FLOAT` `EXPR_IDENT` `EXPR_INDEX` `EXPR_LAMBDA` `EXPR_MEMBER` `EXPR_NUMBER` `EXPR_PROPAGATE` `EXPR_SETCOMP` `EXPR_SETINTERVAL` `EXPR_SETLIT` `EXPR_STRING` `EXPR_TAG_ACCESS` `EXPR_UNARY`（`EXPR_LIST` 在内，共 20）。
- **两个后端实际处理的种类**（`grep -oE 'EXPR_[A-Z_]+' <file> | sort -u`）：
  - **wasm 处理 11 种**：`EXPR_BINARY` `EXPR_BOOL` `EXPR_CALL` `EXPR_CHAIN_COMPARE` `EXPR_FLOAT` `EXPR_IDENT` **`EXPR_INDEX`** **`EXPR_LIST`** `EXPR_NUMBER` `EXPR_STRING` `EXPR_UNARY`（`src/compilation/wasm_backend.c:1336` `case EXPR_LIST:`、`:1340` `case EXPR_INDEX:`）。
  - **AOT 处理 8 种**：`EXPR_BINARY` `EXPR_BOOL` `EXPR_CALL` `EXPR_CHAIN_COMPARE` `EXPR_FLOAT` `EXPR_IDENT` `EXPR_NUMBER` `EXPR_UNARY`（`src/compilation/aot_native.c:401/404/407/410/413/418/439/470`），其余走 `:497` 的 `default: fail(g, "expression kind %d is outside the numeric subset", (int)e->type);`——**显式拒绝，是好的**。
- **`src/compilation/wasm_backend.h` 自己就写着**："Translates a numeric subset of the AST (…) **plus arrays**" ⇒ **代码与它自己的头注释是一致的；被推翻的是我们那条 grep，不是这个后端。**

**执行证据（本会话实测，命令与输出逐字如下）**——先要找到正确的 CLI 形式，这是一个**容易踩的坑**：`--abi-target wasm` 是**全局旗标，必须紧跟子命令词**（`src/main.c:1024-1027` 的 `base = (argv[1] 是 compile/buildc/run/profile/symbols) ? 2 : 1`，随后只看 `argv[base]`），写成 `compile in.im out.wasm --abi-target wasm` 会**静默退回 host 目标**、产出一个 `INIM` 文件（`WebAssembly.Module(): expected magic word 00 61 73 6d, found 49 4e 49 4d`）。正确形式：

```
./build/inimerse compile --abi-target wasm <in.im> <out.wasm>      # 输出 "... (wasm MVP subset)"
head -c4 <out.wasm> | xxd     # → 0061 736d  （真 wasm 魔数）
node tools/wasm_run.js <out.wasm>
```

| 程序 | 解释器 | wasm | 一致？ |
|---|---|---|---|
| `a = [1,2]` + `if a { say 1 } else { say 0 }` | `1` | `1` | ✅ |
| `a = []` + 同上（**空数组**） | `1` | `1` | ✅ |
| `a = [1,2]` + `say a[0]` | `1` | `1` | ✅ |
| `a = [1,2]` + `say a[5]`（**越界读**） | `nil` | `nil` | ✅ |
| `s = "hi"` + `if s { say 1 } else { say 0 }` | `1` | **拒绝编译**：`error: wasm MVP subset: expression type 2 not supported by wasm MVP subset (strings/collections need the interpreter) (line 1)` | 拒绝（非分歧） |
| `s = ""` + 同上（**空串**） | **`1`** | 同上拒绝 | 拒绝（非分歧） |
| `if nil { say 1 } else { say 0 }` | `0` | **拒绝**：`error: wasm MVP subset: variable 'nil' used before assignment (line 1)` | 拒绝（**另一条消息**） |

⇒ **更正后的准确表述**：**wasm 已经实现了数组与下标访问，且与解释器在四条用例上逐格一致**（非空数组为真、空数组为真、下标读、越界读为 `nil`）；**不可达的只有字符串与字典/集合**。`cg_cond` 的 `else 1` 分支**今天可达**（数组走进去），并且**给出的答案与 `vm_truthy()` 相同**。⇒ **`TAG_ARR 5` 是活的**；`TAG_STR 4` 仍是死的。
**另外两条同批实测**：① `src/compilation/aot_native.h` 逐字写 "The accepted subset is deliberately the one the Wasm backend already defines (`src/compilation/wasm_backend.h`)"，而两个文件**零共享代码**——连缓冲区（AOT `buf_init`/`buf_need`/`buf_fmt` `src/compilation/aot_native.c:35-57` vs wasm `bput`/`bleb_u`/`bf64` `src/compilation/wasm_backend.c:29-52`）与符号表（`Sym`/`sym_find`/`local_add` vs `Named`/`global_lookup`/`resolve_var`）都是各写一份；② **两个后端的成功约定相反**：`aot_native_translate` 返 **1 成功 / 0 失败**，`wasm_compile_program` 返 **0 成功 / −1 失败**。**这两条是「同一件事有多个独立决定点」的又一个实例，落在后端的基础设施层。**
**诚实边界**：① 上表 7 条是本会话实跑的，但**只用了一个宿主、一个 node 版本（v24.20.0）**，且 `node tools/wasm_run.js` 是仓库自带的运行器、**不是独立第三方 wasm 运行时**；② 我**没有**测字典/集合（`EXPR_DICT`/`EXPR_SETLIT`/`EXPR_SETCOMP`/`EXPR_SETINTERVAL`）在 wasm 的拒绝消息，只从「11 种里没有它们」读出「不支持」；③ 那张 11/8 的表是 **`grep` 出来的种类名**，**不保证每一种都被完整实现**（例如 wasm 命中 `EXPR_STRING` 是为了**拒绝**它，不是支持它）——「命中」与「支持」在这张表里是两件事。

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

**因此第 1.5 步拆成两半——但实测之后，两半都不便宜，而且都没有「可修的缺陷」。** 实现会话（`exact-lumen`）的差分结论：**18 个标量真值用例，解释器与 wasm 逐条相同（18/18，0 分歧）**；AOT 侧既有套件 `aot_native.test: 104 cases (86 equivalence, 2 pinned divergences, 6 runtime errors, 10 refusal), 0 failures`。⇒ **标量一致已经由两个既有测试文件断言了**，「三后端真值表 CTest」这一半**加不了新信息**；而非标量那一半**今天根本跑不起来**（两个后端都拒绝）——**2026-10 更正：准确边界是「字符串与字典/集合跑不起来」；数组在 wasm 上跑得起来且与解释器逐格一致，见 §6.1 更正三。因此 1.5c 的范围应从「实现非标量」收窄为「实现字符串与字典/集合」**。**1.5a 今天没有可钉的东西**，故**未新增任何 CTest**（来源：`exact-lumen`）。

| 子步 | 内容 | 成本 |
| --- | --- | --- |
| **1.5a** | wasm：让 `cg_cond` 的 `else 1` **可达** | **原估「小」已被实测推翻**（2026-10）：见上方实测框 ⇒ 实为「在 wasm 实现字符串与字典/集合」= §2 第 ⑤ 类，与 1.5b **同类**。**2026-10 更正三**：`else 1` **本来就可达**（数组），故这一格的对象是字符串与字典/集合 |
| **1.5b** | **AOT 半，已重新归类，且已被人类裁定为「不做」**：`NV`（`src/compilation/aot_native.c:174`，三个 tag `NV_INT 0`/`NV_FLT 1`/`NV_BOO 2` 在 `:175-177`）**无 nil/字符串/容器表示**。**这已不是「补一个分支」，而是 §2 表第 ⑤ 类（值表示）** | **已裁定走 A（2026-10，人类）**：AOT 改吃带 `Value` 语义的同一份 IR ⇒ `NV` 被整体替换 ⇒ **1.5b 不做**；AOT 改为**链接 `Value` 运行时**，归入第 2 步。见 §6.1 末段 |
| **1.5c（新立，与 1.5a 分开命名）** | **在 wasm 后端实现字符串与字典/集合**（值表示 + 打印导入 + 堆生命周期）。与 1.5b 同类。**2026-10 更正三后范围收窄**：数组已实现（`EXPR_LIST` + `EXPR_INDEX`），原写「字符串与容器」里的「容器」应读作「字典/集合」 | 大。**已由人类裁定 A′（2026-10）⇒ 不做「独立扩值表示」，改为「把 IR 的值语义映射到 wasm」，归入第 2 步**（§3.5）。**必须与 1.5a 用不同的名字**，否则计划里会一直写着「1.5a 便宜」 |

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

1. **有多少今天合法的 `.im` 会因「无法证明 ⇒ 编译错误」而编译不过？** 今天 `be` 是纯运行期检查（`src/compiler/compiler.c:2232-2240` 无条件发射 `OP_BE`），编译器**从不拒绝**。这条规则**新增了一项编译器的否决权**，而**没有任何东西量过它的爆炸半径**——本档 §0.5 记过一条教训：「凡『改动会破坏 N 处』的断言都应先量后写」（那次实测真正受影响的只有 **1** 个文件）。⇒ **先对 `vtest/`、`projects/`、`mods/` 全量统计「有 `be` 且编译器无法静态证明」的实例数，再决定这条规则是否分期生效。** **⚠ 见 §3.9 抬头的记录牌：那次普查（10 处、8 可解析、2 fixture 错）是 `be` 移除前的读数，今天不再指向现行代码。**
   **→ 已量（2026-10，第五个人类裁定要求的那次），结论见 §3.9**：全仓 **346 个 `.im` 只有 10 处 `be`**，其中 **8 处可解析**（约束表达式只依赖字面量 / 内建类型 / 同文件具名声明），**另 2 处是 fixture 的解析错误**（`dir be Direction = "N"`，`=` 不是 `be` 的分隔符 —— **不是本文档要修的，是 `vtest/` 的 bug，见 §3.9 更正一**）⇒ **上界 = 0 个不可解析实例**，按裁定自己的路径支持**直接采用 (A) 的口径**。**但 §3.9 的四条诚实边界必须一起读**（手工分类、正则只覆盖一种形状、只看了 `be` 使用点而非 `type` 声明、内建 `N` 未实跑验证）。
2. **`@runtime_check` 的语法与它落到哪些 opcode 上。** 今天不存在这个语法（我**没有**跑过 `grep -rn "runtime_check" src/ selfhost/ docs/`，**未测**）。
3. **断言里 `CompilerSet` 与运行期值的比较必须写成 `∈` 而不是 `==`**（§3.8 已说明理由：可能类型的集合与一个具体值之间没有 `==`）。**这条不是测量，是写法约束**——写错了断言就编不出来，或者编出来是恒真的。

### 6.3 (A) 的判据（**由我写、由实现方实现**，2026-10 与 `exact-lumen` 约定的分工）

`exact-lumen` 提出的顺序是 **我先把判据写成 §6 的一条可证伪条目 → 它按判据实现拒绝点 → 反向验证**。理由（它的原话大意）：**谁先谁后决定的是「判据有没有被实现牵着走」**。所以下面这条**不是**描述它打算怎么实现，而是**规定它必须做到什么**。

**判据主体（可证伪，落点在编译路径）**：

> 对任意一份 `.im`，若某个 `be` 语句的约束表达式**只**由以下三类构成，则该语句在**静态模式**下不得发射 `OP_BE`：
> (i) 字面量枚举 / 字面量区间（`0,1,2,3`、`(0,10)`、`[0~255]`）；
> (ii) 内建类型名（`N`、`Z`、`R` …，名单来源 `src/vm/vm.c:1871`）；
> (iii) **同一份 `.im` 里已声明的 `type X = …`，其 RHS 递归地也满足 (i)(ii)(iii)**。
> 否则（含依赖运行期值、含未声明名字、含跨文件引用）**必须**走 §3.8 的三处白名单之一，**且若既无法证明又未标 `@runtime_check`，则为编译错误**。

**为什么落点必须是编译路径而不是 `--lint`（这是这条判据最关键的一句）**：`exact-lumen` 实测发现，**今天 `--lint` 路径根本不打印解析错误**——`./build/inimerse --no-mods --lint vtest/lint_case_enum_v04.im` 的 `rc=1`，stdout 只有 `[lint] line 3 [WARN] …`，**`grep -c "expected 'expression'"` = 0**（§3.9 更正一）。⇒ **如果这条判据只落在 `--lint` 侧，它会变成第二个「绿着但没检查」**：判据存在、退出码非零、而它答的是另一个问题。**落点写死为 `src/parser/parser.c`（拒绝）或 `src/compiler/compiler.c`（不发射 `OP_BE`），二者之一，不得只在 lint 侧。**

**四条可证伪判据**（每条都能被一次命令或一个反例证否）：

| # | 判据 | 证伪方式 |
| --- | --- | --- |
| A1 | **正向**：`vtest/type_collection_v04.im`（`x be Byte: 42`，`Byte = [0~255]` 是同文件字面量区间）在静态模式下**能编译**，且产出的字节码里 `OP_BE` 次数为 **0** | 数 `OP_BE`（`./build/inimerse --no-mods bytecode <f> \| grep -c '^OP_BE'`）；**非 0 即红** |
| A2 | **反向（判据有牙）**：构造一个约束依赖运行期值的 `be`（如 `n = 3` 后 `x be n : 1`，或跨文件的 `type`），**静态模式下必须被拒**，且**错误种类串可辨**（不是 `rc=1` 而无消息） | 该程序 rc≠0 **且** stderr 含可辨识的错误串；**若 rc=1 但无消息，判据视为未实现**（这正是 §3.9 更正一暴露的形状） |
| A3 | **不误伤**：§3.9 那 **8** 处可解析的 `be`（`exc_test.im:11`、`projects/exc_test.im:11`、`meta_test.im:45`、`set_test.im:28`、`inf_set_test.im:115`、`vtest/type_collection_v04.im:3`、`big_globals_test.im:168`、`nsadv_mod.im:3`）在静态模式下**全部编译通过** | 逐个跑；**任一被拒即红**（这条防的是「判据太紧」，与 A2 防的「太松」是一对） |
| A4 | **两种模式都要成立**：同一份 `vtest/type_collection_v04.im`，**静态模式**产出里 `OP_BE` 为 0、**动态模式**产出里 `OP_BE` **保留** | 两份字节码对比；**两份相同即红**（那说明模式没有生效） |

**A3 的存在理由必须写下来**：第五个人类裁定否决「(A) 直接上」的理由是**它会让 Inimerse 比 Rust 还难写**。⇒ **A3 就是这条理由的可执行形式**：如果 (A) 的判据把今天合法的 8 处也拒了，那它就不是「新规则」，而是「破坏性变更」，**而 A3 会在第一时间把它标红**。**没有 A3 的 (A) 判据是不完整的。**

**前置（`exact-lumen` 明确要求的一条）**：**A1 必须在 `--lint` 之外独立可跑** —— 即判据的观测点不能复用 `--lint` 的输出通道，因为那条通道已被证明会吞掉解析错误。

---

## 7. (f) 风险与「不做什么」

### 不做

1. **不把 `src/vm/vm.c`（5,346 行）重写成 `.im`**。它是 substrate，且是唯一的引导运行器（第 1 类 + 第 4 类）。这是本设计明确的**上界**。
2. **不做 NaN-boxing / 特化 `Value`**。`docs/AUDIT.md` §5 已明确列为「明确不值得先做」。
3. **不只改其中一路的 `and`/`or`**。改一路会把分歧从「三处不一致」变成「两处不一致」，不减少可观测缺陷。
4. **不把注释当语义证据**。`src/compilation/aot_native.c:420-428` 的注释与行为相反；`src/compilation/wasm_backend.c:6` 声称 "mirror the C VM exactly" 却与 `:938` 冲突。这类「注释与代码互相担保」正是分歧能长期存活的原因。
   **第三例（2026-10 实测，比前两例更硬）**：本文档指定为**唯一语义来源**的 `src/vm/vm.c` 里，**有 58 行注释是乱码**（`锟斤拷` 一类损坏的编码），**其中 30 行落在 69 个指令体区间 `:3146-4680` 之内** —— 例如 `L_POP_REG` 上方那条分隔注释与 `L_CALL_BUILTIN` 体首的参数说明。**门禁的 `text-integrity` 阶段对此是绿的**，因为 `tools/check_text_integrity.py` 的判据是**「文件里没有 NUL 字节」**（该脚本 docstring 逐字写明它查的是 NUL），**不是「注释可读」**。⇒ 后果有两层：① 「读 VM 的 `L_X:` 块来填表」这件事，在 30 个格子上遇到的是**读不懂的注释**（代码本身可读，故不影响 §3.1.7 的分类，但会影响任何以注释为线索的读者）；② 一个**只查 NUL 的完整性检查**会给「注释已被损坏」发绿灯 —— 这是 §3.1.8 那个病的又一面：**被检查的东西与被断言的东西不是同一个**。
   **第四例（2026-10 新增，来源 `exact-lumen`，标为 [转述]）——这一例最该记住，因为它说的不是注释，是「检查」**：`tools/check_orphan_fixtures.py`（门禁第 12 阶段 `orphan-fixtures`，见 §3.1.8 第二个先例）**第一次跑反向验证时自己崩了**：`NameError: name 'CMAKE_SOURCE_DIR' is not defined` —— 提示串是 f-string，`${CMAKE_SOURCE_DIR}` 的花括号被当成替换字段。⇒ **一个「能发现孤儿」的检查，在真的发现孤儿时抛异常而不是报告：它存在、它退出非零、而它答的是另一个问题。** 修法是 `${{CMAKE_SOURCE_DIR}}`；**没有那次反向验证，不会有人发现**。
   **第五例（2026-10 新增，**本会话实测**，比第四例更安静，因此更危险）——一个「判据」里有两个永远不会命中的分支**：本文档与 `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3192`）` 都引用过的那条
   ```
   grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"
   ```
   **`EXPR_ARRAY` 在整个 `src/` 里出现 0 次**（`grep -rn 'EXPR_ARRAY' src/ | wc -l` → 0），**它不是 `ast.h` 的枚举成员**；`EXPR_SET`（裸名）也不是（真名 `EXPR_SETLIT`/`EXPR_SETCOMP`/`EXPR_SETINTERVAL`）。⇒ 三个分支里**两个恒为 0**，这条 grep 的「0」**是恒真的，不是证据**；而它掩盖的事实是**数组字面量的真名 `EXPR_LIST`，且 wasm 已经实现了它**（`src/compilation/wasm_backend.c:1336`，见 §6.1 更正三）。
   ⇒ **第四例与第五例的差别值得写下来**：第四例**崩了**（吵、可见、被反向验证抓到），第五例**静静地返回了它一直在返回的值**——**第四例的错误会被人发现，第五例的错误会被引用。** 两者都是「一个从未在真实反例上跑过的检查」，但只有第四例**有牙**：它会抛异常，于是有人去查。⇒ 规矩再加一条：**检查的判据里不允许出现「不可能为真」的分支；如果某个分支今天恒为 0，必须证明它「今天恰好为 0」而不是「不可能非 0」**（对这条 grep 的正确写法是先确认 `EXPR_ARRAY`/`EXPR_SET` 是枚举成员，或直接改用 `ast.h` 的枚举集合做双向差集——与 §3.1.8 第二个先例同一种断言）。
   ⇒ **这五例合起来给出本设计的一条硬规矩：一个从未在真实反例上跑过的检查不是证据。** 所以 §3.1 与 §6 的每条判据都写成「**故意改错必须变红**」/「**从 0 变成 69**」，而不是「加一个检查」——**一个只被正向跑过的检查，与被损坏的注释、与只查 NUL 的完整性检查、与一条含死分支的 grep，是同一类东西：被检查的对象与被断言的对象不是同一个。**
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

**不冲突。** 本设计与 §5（`docs/AUDIT.md §5`，@ `2157cc1` → `:3891-3965`；`O0` @ `:3897`、`O3` **已完成** @ `:3922`、`O13` @ `:3948`。**原引 `docs/AUDIT.md:328-386` 是写下时的号** —— 落笔时该档只有 392 行、`:328` 逐字是 `**O6. emitter 目标转发**`，此后它长到 4017 行，§5 整体下移）的关系：

- **O0**：本设计**当时采纳 O0 选项①**（值语义胜出），并把「决定」落到 §3.4 的唯一语义表上。**该方向已被用户裁定推翻为布尔语义**（§0.5、`docs/STATUS.md **§10.42**（`@ e9debfd` → `:2547`）-2531`）；但 O0 的「必须先把决定写进唯一一张表」这一要求不受影响，且已落地。
- **O1 / O2**：§6 第 4 步即 O1 的宽度版本；本设计指出 O1 的根因比 `%` 更广（§1.3 的 `Value` vs `NV` 宽度），O2（整数溢出可诊断）与 §5.3 的 ABI 宽度标签是同一件事的两个面。
- **O11**：`docs/AUDIT.md:3944`（O11 @ `:3942`；**原引 `:368` 是写下时的号**）把「`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致」列为**风险①**；本设计把它当作**待根治的缺陷本身**（§1.1 证明 `.im` 重写不消除分歧）。两者方向一致，本设计是该风险的解法。
- **O13**：`docs/AUDIT.md:3948`（**原引 `:372` 是写下时的号**）诊断 wasm 后端「算术发射质量差」（`arith` 只有 2.72× vs AOT 155×），怀疑「每次算术都要把 `Value`（32 字节）装箱再拆箱」。**这是 §3 的同一条根因的第一个「不同种类」的症状 —— 不是第二个论据。**（原文写「第二个独立论据」，同一句的末尾却写「两件事同一个根因」—— **两句不能同时为真**，见下面的分级表。）wasm 后端**既**因逐个 AST 重判语义而分歧（正确性），**也**因缺少统一 IR 层而无法做跨后端的发射质量改进（性能）。⇒ **它不增加「根因存在」的证据**（第一条症状已经证明了），**但它增加「根因的代价是什么种类」的证据** —— 一个是**正确性**，一个是**性能**，而 §3 的设计要能被这两类代价分别检验。**可证伪的判据（`exact-lumen` 提出，我未做）**：若能指出「修好 IR 层之后其中一个现象仍然存在」，则它们确实是两个根因、该句原样成立；否则只能读作「两条现象、一个论据」。**我没有跑这个判据**，故此处按较弱但可辩护的读法写（「同根因的两个症状」），并把它列进 §10 `[待复核]`。
- **§5 末尾三条 `DIVERGENCE`**：§6 第 5 步照其要求「修好后重新审视」（`docs/AUDIT.md:3954-3965`，三条 @ `:3958`/`:3959`/`:3960`；**原引 `:378-386` 是写下时的号**）。**⚠ 该处今天已自标为「写下时的三处」**：第三条 `lcg_float_promotion` 已**提升**为 `EQUIVALENCE` 的 `int_lcg_second_step`，读今天的条数用 `sed -n '/^DIVERGENCE = \[/,/^\]/p' tools/aot_native.test.py`（本会话实测 = **2**）。

**唯一需要 Lead 裁决的张力**：O0 说无论选哪条都要在 `docs/API.md:90` 写明返回值。本设计当时建议写明**值语义**（即返回操作数），理由是 AUDIT 论证改解释器会静默改变现有脚本结果（如 `name or "anonymous"`）。**该建议未被采纳，裁定为布尔语义**（§0.5）。事后看，这条建议的代价估计**过重**：实现方先量了爆炸半径，真正会变的值选择只有 **1 个文件**（`t_sugar_desugared.im:7` 的 `k = 0 or 1`）且它不被任何 CTest 或门禁引用 ⇒ 门禁零回归（`docs/STATUS.md **§10.42**（`@ e9debfd` → `:2491`）-2479`）。**教训：本设计的「代价」判断当时是读码推断，没有像实现方那样先量一遍；凡是「改动会破坏 N 处」的断言，都应当先量后写。**

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
| 10 | `src/compilation/wasm_backend.c:834-847`（`cg_cond` 尾部） | **语义一行不改**（`else 1` 已与 `vm_truthy()` 的 `default: return 1` 逐字对应）。**但原写「只加可达性与三后端真值表 CTest」已被实测推翻**：wasm 连字符串字面量都产不出来 ⇒ 「可达性」= **在 wasm 实现字符串与字典/集合**（1.5c，大，归 §2 第 ⑤ 类）；标量真值表已由既有测试断言（18/18 + `aot_native.test` 104 cases/0 failures），**加不了新信息**。**2026-10 更正三**：`else 1` **今天可达**（数组），`TAG_ARR 5` 是活的、`TAG_STR 4` 才是死的 | §0.5.1 第 2 条、§6.1 更正一、§6.1 更正三、§6.1 的 1.5a/1.5c |
| 11 | `src/compilation/wasm_backend.c:66`（`#define SLOT_BYTES 16`）vs `src/vm/vm.h:40-45`（`Value` 32 字节）vs `src/compilation/aot_native.c:174`（`NV`） | **wasm 的值表示同样未被裁定**：裁定 A 只覆盖 AOT ⇒ **已由人类裁定 A′（2026-10）**：wasm 也吃带 `Value` 语义的同一份 IR、**不要单独扩 `SLOT_BYTES`**（§3.5）。另：**同文件 `:4` 的头注释说「8-byte slot」，与 `:66` 的 16 字节矛盾**，必须一并修；引用行号时必须引定义处、不能引文件头 | §3.5、§7 第 4 条 |
| 12 | `src/vm/vm.c:5208-5266`（`vm_disasm_ins` 的 switch，`default:` 在 `:5265`） | **补全反汇编器缺的 21 个 opcode 命名**（清单逐字见 §3.1.6）。**便宜、独立、且应在第 2 步之前做**：缺的 21 个恰好是集合/区间/闭包捕获/线程——正是 §3.1.4 与 1.5c 最难的一半，而人类可读 dump 是调试它的主要手段。**判据**：`grep -c 'case OP_' src/vm/vm.c` 在 `:5208-5266` 区间内从 **48** 变 **69**；且断言「反汇编器的 case 集合 == `bytecode.h` 的枚举集合」（差集为空，正反两向都查）。**注意不要与 `src/main.c:675-681` 的数字 dump 混淆**——那条路故意不经过反汇编器，是稳定判据，**不要改它** | §3.1.6 |
| 13 | `docs/SYNTAX.md` §7.1 的 **D14** 裁定 | **登记一个口径缺口，不改 D14**：D14 是二分的（操作共享状态 ⇒ 抛 `type_mismatch`；只产出值 ⇒ 按已裁定定义值作答），而 §3.1.7 实测出 **11 个 opcode 属「纯值但会抛」**（`ADD` `CONCAT` `SUB` `MUL` `DIV` `NEG` `LT` `GT` `LE` `GE` `MOD`，全部调 `vm_throw`）。D14 讲的是**内建**、这 11 个是**运算符**，故未必冲突；但 `OpSemantics` 表**必须同时带 `touches_state` 与 `can_raise` 两列**，否则后端作者会把 `+`/`lt` 映射错。**请 D14 的所有者（不是本文档）决定要不要补第三个分支** | §3.1.7 |
| 14 | `OP_STORE_CAPTURE`（`src/vm/vm.c:3952-3955`）、`OP_POP_REG`（`src/vm/vm.c:3761-3767`） | **两个零生产点 opcode：声明、分派、实现齐全，全仓库没有任何东西发射它们。** 它们是一对**没人用的第二套捕获协议**（今天真正在用的是 `OP_PUSH_REG` × N + `OP_MAKE_FUNC` 自己从 `t->stack` 取值）。⇒ **要么删、要么接上——这是人的选择，不是表能回答的。** 无论选哪条，**§3 的 69 行表都不该把它们算进「后端要映射的 opcode」**。**判据**：`LC_ALL=C comm -23 <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/bytecode.h \| sort -u) <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/compiler.c \| sort -u)` 今天输出恰好这两行 ⇒ 裁定后该输出应为**空**（若删）或**不再含它们**（若接上，则 `compiler.c` 侧必须出现发射点） | §3.1.9、§3.1.8 |
| 15 | `src/compiler/bytecode.h:69-74`（`RegInstruction`）、`:87-128`（`Bytecode`） | **裁定 A′ 未覆盖的第三个岔口：类型信息放不放进 IR。** A′ 的措辞是「按 IR 的类型信息落值」，而 **IR 今天没有类型字段**（`{ OpCode op; int r1, r2, r3; }` 全是下标）。**A″**＝IR 保持无类型、后端照抄 VM 的运行期动态分派（忠实，但 wasm/AOT 不做类型特化，`docs/AUDIT.md` §5 O13 的性能账在 §3 里还不上）；**B″**＝IR 带类型标注（可特化，但**编译器要生产并维护类型** ⇒ 立了第二个类型决定点，把 §1 的病搬到「编译期 vs 运行期」这一维）。**这不是顺序问题，是架构问题，必须人裁定。** 本文档不替人做 —— **已裁定（2026-10）：`var` 动态、其余静态 ⇒ 走 B″，见 §3.7** | §3.6、§3.1.4、§3.7 |
| 16 | `src/compiler/bytecode.h:64-66`（`INIM_BYTECODE_VERSION 3` / `INIM_ABI_VERSION 2`）、`:69-74`（`RegInstruction`） | **裁定 B″ 的格式后果**：IR 要携带类型 ⇒ `RegInstruction` 加字段 ⇒ **`.inim` 磁盘格式改变 ⇒ 版本号必须升**，且三个 `.inim` 读者（`src/compiler/bytecode.c`、`src/main.c`、`src/compilation/deps.h`）必须**同批**改。另：**`var` 需要 IR 里有一个「动态」类型值**（语义等于 `im_typeset_any()`），使 `var` 走与 A″ 相同的运行期分派 ⇒ **B″ 的实现里同时存在静态特化与动态分派两条路，它们的边界就是新的必须被断言的东西**。**不要在没有 §6 那条新证明义务（编译器单例标量类型 vs VM 运行期 `R[i].type`）的情况下先加字段。** | §3.7、§3.6 |
| 17 | `src/compiler/compiler.c:2232-2240`（`case STMT_BE` 无条件发射 `OP_BE`）、`src/compiler/bytecode.h:69-74` | **裁定「契约式双层判定」（§3.8）的实现后果，三项，都还没做**：① **静态模式下「编译期无法证明 ⇒ 编译错误」是一条今天不存在的语言规则** —— 今天 `be` 是纯运行期检查、编译器从不拒绝，这条规则**新增了一项编译器的否决权**，而**没有任何东西量过有多少 `.im` 会因此被拒** ⇒ **先量后改**（**已量，见 §3.9：346 个 `.im` 只有 10 处 `be`，10/10 的约束只依赖字面量 / 内建类型 / 同文件具名声明 ⇒ 上界 0 个不可解析实例**）；② **`@runtime_check` 是新语法**，今天不存在（我**没有**跑过 `grep -rn "runtime_check" src/ selfhost/ docs/`）⇒ 需要先定它的语法与它落到哪些 opcode 上；③ **`CompilerSet == RuntimeSet` 这个等式不能直接实现** —— 一个可能类型的集合与一个具体值之间没有 `==`，**可实现的形状是成员关系 `type_of(实际值) ∈ CompilerSet`**，等式只在编译期集合恰为单例时等价。**⇒ 落地前必须先把断言写成 `∈`，否则那条 Debug 断言没有可写的形式。** 另：§3 的 69 行表需**新增一列**「该 opcode 在静态模式下是否可能被编译期消除」（`can_raise` 与 `producers` 两列都不表达这件事）。**判据（可证伪）**：对同一段 `.im`，**静态模式产出的字节码里 `OP_BE` 出现次数为 0**（或全部落在「动态模式 / 显式 `@runtime_check` / Debug 断言」三类白名单位置），**动态模式产出里 `OP_BE` 保留** —— 同一份源码两种模式，产出各自符合各自的规则 | §3.8、§6、§3.7 **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| 18 | `docs/STATUS.md **§10.53**（`@ e9debfd` → `:3192`）` 的 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` 判据、以及 `docs/DECFY_DESIGN.md` 转述它的三处（§0.5.1 第 2 条、§6.1 开头、§6.1 的 1.5a/1.5c） | **一条含两个死分支的判据必须被换掉，并把「wasm 已实现数组」登记为既有能力**：`EXPR_ARRAY` 与 `EXPR_SET`（裸名）**都不是 `src/parser/ast.h` 的枚举成员**（`grep -rn 'EXPR_ARRAY' src/ \| wc -l` → 0），所以那条 grep 的三个分支里两个**恒为 0**。**建议的替换判据（与 §3.1.8 第二个先例同形：两个 artifact 之间的集合关系）**：把 `src/parser/ast.h` 的枚举集合与「各后端实际处理的 `EXPR_*` 集合」做**双向差集**，输出两侧各自的差集（今天应为：wasm 缺 `EXPR_DICT`/`EXPR_LAMBDA`/`EXPR_MEMBER`/`EXPR_PROPAGATE`/`EXPR_SETCOMP`/`EXPR_SETINTERVAL`/`EXPR_SETLIT`/`EXPR_TAG_ACCESS`/`EXPR_ARROW_CAST` 共 9 个、AOT 再缺 `EXPR_INDEX`/`EXPR_LIST`/`EXPR_STRING` 共 12 个），并**故意删掉一个 `case EXPR_LIST:` 让断言变红**。**配套**：`src/compilation/wasm_backend.h` 早已逐字写 "plus arrays"，但**没有任何 CTest 钉住数组在 wasm 上的真值/越界行为**（§6.1 更正三的四条实测是手工跑的）⇒ 应为数组补一条 CTest（`a = []` 为真、`a[5]` 为 `nil`），**这是今天唯一「已实现却未被钉住」的非标量能力**。**`docs/STATUS.md` 是 `exact-lumen` 的写域，我不动它**，只登记 | §6.1 更正三、§7 第 4 条第五例、§10 |
| 19 | `src/main.c:1024-1027`（`--abi-target` 的解析位置） | **一个会静默给错产物的 CLI 陷阱**：`--abi-target` 是全局旗标，但解析循环的 `base` 只看 `argv[1]`，故**必须紧跟子命令词**（`compile --abi-target wasm in.im out.wasm`）。写成 `compile in.im out.wasm --abi-target wasm` 时旗标**不被识别、不报错**，直接退回 host 目标产出 `INIM` 文件（实测：`WebAssembly.Module(): expected magic word 00 61 73 6d, found 49 4e 49 4d`）。⇒ **两种调用形式只有一种是对的，而错的那种不报错** —— 这与本档 §1 的病同形（同一件事两个决定点，其中一个静默）。**建议**：位置不对的旗标应被拒绝（`unknown option` 而非静默忽略），或至少在 `--abi-target` 缺席时也把目标写进产物；**登记，不改** | §6.1 更正三、§7 第 4 条第五例 |
| 20 | `src/types/typeset.h:41`（`im_typeset_union` 声明）、`src/types/typeset.c:56`（定义）、`src/vm/vm.c:3192-3200`（`L_ADD` 的 `set_union` 分支）、`src/vm/vm.c:2054`（`set_union` 定义） | **同一个语义（集合并集）有两个生产点，只有一份被执行——`im_typeset_union` 的处置需要人裁定。** 实测：`type CombinedError = FileError + ParseError` + `say CombinedError` → `set(4)`，**并集是在 VM 运行期由 `L_ADD` 调 `set_union` 算的**；`im_typeset_union` 的全部调用点只有两个 probe（`src/types/typeset_probe.c:19`、`:50`、`src/types/enum_probe.c:91`、`:117`），**引擎零消费者**。⇒ 与 `OP_POP_REG`（无人发射）不同，这一条是「**有人发射、但发射的是另一份实现**」。**处置二选一**：① **接上**——让编译期类型推断真的调用 `im_typeset_union`（= §3.7 的 B″ 落地）；② **删掉**——承认类型层不参与，把并集语义唯一地定在 `L_ADD`（并把 §3.7 的 B″ 收窄）。**已由人类裁定（2026-10）走 ①：接上**（理由：它是 `docs/archive/ROADMAP_3.1.md` 的集合化类型系统的实现，且与「IR 携带类型」的 B″ 裁定同向）。⇒ **本节从「待裁定」变为「已定」**，落地判据见 §3.1.10 第 3 点。**判据（无论选哪个）**：`grep -rn 'im_typeset_union' src/` 的结果集合**必须与 `OpSemantics` 表里声明并集归属的那一行一致**；**故意在表里把并集归给另一层，§6 的断言必须变红**。**⚠ 本条裁定之后，`exact-lumen` 提了一个我采纳的补充论证，它把这条裁定的含义说清楚了（我转述并同意）**：**「接上还是删掉」本身是个错的问题**，因为处置必须**从层级裁定里推出来** —— 若并集归 **VM 层**（今天的实际行为），则「接上」等于**主动制造第三个生产点**（类型层一份 + VM 层一份），「删掉」才是自洽的；若并集归 **类型层**，则 `L_ADD` 的 `set_union` 才是要移走的那一份。⇒ **人类裁定走 ①「接上」，其逻辑含义就是「并集归类型层」**，因此 §3.1.10 第 3 点里「唯一语义来源 = `L_ADD` 的 `set_union`」那条**提案已被这条裁定间接否决**（我保留它作为「若走 ② 则是它」的分支记录，并加注）。**两条限定**：① **删掉一个已声明的 API 也是行为改动**，与 parity 那次一样需要人批（`src/types/typeset.h:41` 是声明点）—— 本裁定是「接上」，故这条只作为将来回退时的约束记录；② **落地排到 0.5.2**（发版冻结期，`vivid-anchor` 要求「推完停手」） | §3.1.10、§3.7 |
| 21 | `src/parser/parser.c:1383-1391`（`be` 只认 `TOK_COLON`）、`:1218-1227`（`type` 只认 `TOK_EQ`）、`vtest/lint_case_enum_v04.im:2`、`vtest/lint_case_membership_v04.im:2` | **`--lint` 路径吞掉解析错误 ⇒ 两个 fixture 绿着，而它们描述的程序从来没有被解析过。** 实测：`./build/inimerse --no-mods --lint vtest/lint_case_enum_v04.im` → `rc=1`，stdout 只有 `[lint] line 3 [WARN] finite case type 'Direction' is missing members: E, W`，**`grep -c "expected 'expression'"` = 0**；而直接跑同一个文件（非 `--lint`）→ `Error: expected 'expression', but got '=' (type 83)`。**两处 fixture 把 `:` 写成了 `=`**（`dir be Direction = "N"`），CTest 的 `PASS_REGULAR_EXPRESSION` **优先于退出码**，于是匹配上那条 warning ⇒ 绿。**两件事要分开**：(a) **fixture 的修法**是把 `=` 改成 `:`（`exact-lumen` 实测 `--lint` 输出逐字不变）——**`vtest/` 不在本文档写域**，已由它报给 `vivid-anchor` 定是否进 0.5.1；(b) **`--lint` 不打印解析错误**这件事本身是一个独立缺陷：**一个退出非零但不说原因的通道，会被任何以正则匹配输出的消费者当成通过**。**建议（登记，不改）**：`--lint` 在解析失败时**必须把解析错误打到它自己的输出通道**（或把 `rc=1` 的成因与「有 warning」区分开）。**判据（可证伪）**：对一个含解析错误的文件，`--lint` 的 stdout **必须**含该解析错误串；**故意让 `--lint` 只在 stderr 打印，断言必须变红**。**⚠ 加强（2026-10，`exact-lumen` 本轮被这条绊到之后给的更强形态）**：判据必须写成「**谓词是对输出内容的匹配，不能是对退出码的匹配**」—— 只写「stdout 必须含该解析错误串」还不够，因为**退出码路径也能碰巧过**。它给的实证：它新写的守卫 `tools/fixture_parse.test.py` 判的是「直接跑引擎有没有 parse error」；**若当初它拿 `--lint` 的 rc 当解析谓词**，那两个 fixture 的 rc 恰好是 **1**（因为它们**另有** WARN/unreachable 发现）⇒ 它会得到「这两个文件有问题」的**正确结论、全错的理由**，而 `x = = 5`（rc=0、零输出）会被它判成「干净」。⇒ **同一批文件、两个不同的谓词、其中一个在另一个的正确输出上给出正确答案。** 这是本档 §1 那个病在**测试谓词**上的实例：**「这个通道非零」与「这个文件坏了」是两个不同的命题，谁都不蕴含谁**。**⚠ 追加（2026-10，`exact-lumen` 的独立证据 + 我采纳的更强结论）**：`docs/SYNTAX.md` 有一条 **M11**，标题逐字 `#### M11. \`--lint\` 恒返回 0`，正文说 `src/main.c:1252-1257` 的 `lint_check()` **无条件 `return 0`** —— **这个标题早已过期**（`docs/STATUS.md **§10.42**（`@ e9debfd` → `:2510`）` 记着修法：**退出码承载判据，`1` = 有发现 / `2` = 文件不可读 / `0` = 干净**）。**但「退出码非零」同样不是解析成功的证据**，实测两条：`x = = 5`（M11 原例）→ `--lint` **rc=0 且一条 lint 输出都没有**（直接跑则 rc=1、`Error: expected 'expression', but got '=' (type 83)`）；`dir be Direction = "N"` → `--lint` **rc=1、只出那条 `[WARN]`**。⇒ **`--lint` 的退出码回答的是「我发现了什么」，不是「这能不能解析」；把退出码改成非零并没有把它变成解析谓词。** `exact-lumen` 已在 M11 就地加了 2026-10 更正块（保留原文 + 对照表），并同步改了 `docs/SYNTAX.md:882` 与 §8 第 9 条的引用 —— **引 M11 请引更正后的版本**（`docs/SYNTAX.md` 不在本文档写域，我只登记） | §3.9 更正一、§6.3、§7 第 4 条 **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| 22 | 两个文档门禁阶段：`tools/check_doc_paths.py` 与 `tools/gate.sh` 的 `links` 阶段 | **门禁对文档的结构完整性零覆盖 —— 一次「标题被吃掉」会拿到满分绿。** 实测（本会话）：我用 `edit` 插 §3.1.11 时把 `### 3.2 \`aot_native.c\` 具体要改什么` **整行标题**当 `old_string`，`new_string` 结尾没带回来 ⇒ **`### 3.2` 标题被删**、其下编号列表失去父标题挂到了 §3.1.11 后面；而 **`bash tools/gate.sh --only doc-paths` 与 `--only links` 双双 RC=0**。机制：`check_doc_paths.py` 只查**内联反引号 span 内、以 `.md` 结尾**的引用（`docs/…`/`future/…`），`links` 阶段只查引用目标存在与否 —— **两个检查都在答「引用的东西在不在」，没有一个问「文档自身还是不是那个文档」**。⇒ **建议（登记，不改；`tools/` 不在本文档写域）**：新增一个纯文本结构断言，**判据必须是机械的、不依赖标题文字**，例如「`### 3.x` 的编号连续无缺」（本档 §3 的小节号序列**今天实测为** `3.1` `3.1.6` `3.1.7` `3.1.8` `3.1.9` `3.1.10` `3.1.11` `3.2` `3.3` `3.4` `3.5` `3.6` `3.7` `3.8` `3.9`，共 15 个 —— `grep -c '^### 3\.' docs/DECFY_DESIGN.md` = 15；**注意 `3.1.1`–`3.1.5` 不是 `###` 级标题**（它们在 `### 3.1` 内部），所以这条判据必须写成「明示的期望序列」而不是「从 3.1 数到 3.9」）或更一般的「标题层级计数与本文件在上一版中的计数相同」。**判据（可证伪）**：**把任意一个 `###` 标题行删掉，该断言必须变红** —— 若删掉后仍然绿，这条断言就是又一次「跑了的检查在答别的问题」。**⚠ 这是 0.5.2 候选，冻结期不做**（`exact-lumen` 同意方向，但明确「现在不做」）。**本轮（2026-10）它又拿这条绊了一次，且这次绊点正是本条要防的那个东西**：它往 `docs/AUDIT.md` 插 §1.62/§1.63 时，锚点**凭记忆**写成 `## §2 执行通道效率对比`，而文件里逐字是「**比较**」（`grep -n '^## §2' docs/AUDIT.md` ⇒ `:2785`，全文件一处）⇒ **锚点匹配失败是「静默零匹配」** —— 一个 `count(anchor) == 1` 的断言就是**目前唯一的防线**，而它**不在门禁里**。⇒ **本条的价值被这轮独立证实第二次**：不是「将来可能有人手滑」，是「本轮又发生了一次」。**它的价值不在这一条，而在它是本档第一次试图让「结构」也进入可证伪范围**：§9 第 12/18/20/21 条全是「集合关系」，这一条是「同一份文档的两个版本之间的结构关系」 | §7 第 4 条、§9 第 18 条 |
| 23 | `docs/BOARD.md` 的阶段表（`:54` 一带）、`tools/gate.sh` | **文档里抄的门禁输出会静默过期 —— 没有任何东西检查「抄的那句话」还在不在输出里。** 实测（`exact-lumen` 报、本会话复核）：`docs/BOARD.md` 的阶段表**逐字写着** `**137 / 137 通过，且 0 跳过**` 与 `0 tests failed out of 137`，而当时 `tools/gate.sh:54` 已经是 **138** —— **「阶段表引的期望串必须真的出现在门禁输出里」这件事没有任何东西在检查**：`links` 答「引用的文件在不在」，`check_doc_paths` 答「反引号里的路径在不在」，**两条都不看内容**。本轮加一个 harness 又让同一张表引的 `113 input(s)` 变成 **114**，它手工改了。⇒ **这跟本表第 22 条是同一个洞的两个投影**：第 22 条说「文档自己的结构没被检查」，这一条说「**文档抄的别处输出没被检查**」；两者的共同特征是**检查器只答「目标存在吗」，没有一条答「内容还对吗」**。**判据（可证伪）**：把阶段表里每一条**反引号包的期望串**抽出来，逐条断言它**出现在对应阶段的日志里**；**故意把表里某一条改一个字，断言必须变红**。**⚠ 0.5.2 候选**：`exact-lumen` 已把这条报给 `vivid-anchor`，本档只登记（`docs/BOARD.md` 虽在本文档写域内，但**改它需要与实现方同批**，冻结期不做）。**本会话实测的当前值**（作为「它会漂」的证据，不当作判据）：`docs/BOARD.md:54` 现为 `**141 / 141 通过，且 0 跳过**` 与 `0 tests failed out of 141`，与 `tools/gate.sh:54` 的 `141` **此刻一致**。**⚠ 用词更正（2026-10，`exact-lumen` 要求收紧，我采纳）**：我原先顺笔写成「**那处不一致已被别人修好，不是遗留缺陷**」——**前半句对、后半句错，且错的方向正是本条要防的**。**修的是那个数（它本轮手工同步 139→141），不是那条缺陷**：缺陷的陈述从来不是「某个数写错了」，而是「**没有任何东西比较文档抄的期望串与门禁的真实输出**」⇒ **它仍然是活的，只是此刻恰好没有实例**。⇒ **本条必须以「判据」形态存在，不能以「实例」形态存在**：写成「我们发现过一处 137/138」迟早会被人读成「已经查过、已修」；写成「把表里每条期望串逐条断言出现在对应阶段日志里，改一个字必须变红」才是**任何时刻都成立**的陈述。**这条更正本身是本档 §3.1.11 分级表的又一面**：`docs/BOARD.md:54` 与 `tools/gate.sh:54` 的两个 `141` 是 **① 级一致**（同一个数被抄了两遍），**它只证明抄对了，不证明「抄的东西被检查过」**——**我把它当成过「已解决」的证据，正是「读起来像 ③ 但实际是 ①」的判错方向（高估）**。 | §9 第 22 条、§3.1.8 |
| 24 | `src/runtime/runtime_posix.c`（`substr` 的钳位）、`tools/*.py` 的 20 份 `find_engine` | **两条与 §1 同族、但形状各异的实例（`exact-lumen` 本轮报、行号与计数由我复核）。** **(a)「检查在算一个已经不是它要防的那个量」** —— `substr` 的钳位原文 `if (start + len > sl) len = sl - start;`：`start + len` 在 int 里算，`5 + 2147483647` **回绕成负数** ⇒ **守卫恒假**、`len` 保持 `2147483647`。修法是比较两个操作数而不是它们的和：`if (len > sl - start) len = sl - start;`（已修，见 `docs/AUDIT.md` §1.62）。⇒ **它不是「漏了检查」，是「检查在算一个已经不等于它要防的那个量」** —— 这是 §3.1.11 那个病（同一语义多个决定点）的**兄弟形状**，且**更隐蔽**：代码里**有一行检查**，任何人读过去都以为防住了。**这一条值得写进设计文档，因为它解释了为什么 §3 的「唯一语义表」必须能被逐格检验，而不只是「有检查」**：一个算了错量的检查在形式上与正确的检查无法区分。**(b)「同一约定函数名只允许一处定义」的检查必须把签名分叉也算分叉** —— `tools/` 下 `def find_engine` 实测 **20 处**（`grep -rln 'def find_engine' tools/ | wc -l` = 20）；`tools/aot_native_bench.py:68` 是异类：`def find_engine(build)` —— **签名多一个实参、无 `.exe`、不读 `INIMERSE_BIN`**，Windows 上必定失败。⇒ 若将来立「同名约定函数只能有一处定义」的检查，**必须把「签名不同」也算作分叉**，否则那 1/20 会被漏掉。**⚠ 本条的两条都只登记、不改**（冻结期；`tools/` 与 `src/**` 均不在本文档写域，且 `exact-lumen` 已自首它本轮「一边同意先立检查、一边又添了第 20 份拷贝」—— **这说明「先立检查再收敛实现」的顺序是对的，理由是纪律不可靠，不是有人不听话**） | §3.1.11、§3.1.8、§9 第 12 条 |
| 25 | `src/runtime/runtime_posix.c:131`（`char translated[2048]`）、`src/runtime/runtime.c:986`（Windows `regex_match`，无定长缓冲） | **POSIX 的 `match` 静默截断模式（`exact-lumen` 本轮报、机制由它读码给出；本条我已复核代码形状，但未独立复跑那两条对照）。** 机制：`:131` 定长 `char translated[2048]`，复制循环边界 `j + 16 < sizeof translated` ⇒ 最多 **2031** 字节，**超出无声丢弃**，然后拿**截断后**的串 `regcomp`。它给的对照（只差长度）：`match("a"*2028, "^" + "a"*2028 + "$zzz")` ⇒ **false** ✓；`match("a"*2030, "^" + "a"*2030 + "$zzz")` ⇒ **true** ✗（模式 2035 字节）；同形状短模式 `^a{50}$zzz` ⇒ **false** ✓。`$zzz` 要求串尾之后再出现字面 `zzz`，**永远不可能匹配**；截断把尾部约束整条丢掉，剩下 `^aaa…` 匹配前缀。**翻转点 N=2030 与 `j+16<2048` 推出的 2031 字节上限逐字吻合**（⇒ 这是**读码得到的机制 + 实测得到的阈值**，比我§9 第 24 条 (a) 的推断强一级）。**对 §3 的意义**：`char translated[2048]` **只在 `runtime_posix.c:131`**，Windows 那份没有定长缓冲 ⇒ **这是「同一个名字、两个答案」落在「两个平台副本」上的实例，而且基线是没有 bug 的那份**——与 §3.1.10 的「并集归哪一层」是**同一类判断：不是选一个实现，是定谁代表契约**。**覆盖为零但原因必须写对**：`match` 全仓只用 **1 个 vtest 文件**，`vtest/` 里最长模式 **59 字节**，低于阈值 **34 倍** ⇒ **不是「用户会踩」，是「阈值远在任何正常用法之外、而越界时它不说话」**——缺陷是**沉默**，不是概率。**判据（`exact-lumen` 要求收紧一次，我采纳；不许写成「长模式不准」）**：**必须是成对两句** —— ① 超长**不可能**模式必须 `false`（`>2035` 字节）；② **同长度的可能模式必须 `true`**（`"^" + N 个 a + "$"`），另加 50 字节短对照。**为什么必须成对（它抓出的漏，我复核同意）**：只写 ① 的话，**一个恒 `return false` 的假修法也满足它** —— 这正是本表第 21/22/23 条反复出现的那类漏（**判据能被一个不满足其意图的实现通过**）。**反向验证②**：**把整个函数改成恒 `false`，② 必须变红**。**已落地**（它实测，走 CTest 本身）：修好 ⇒ `Passed 0.07 sec`；把容量缩回旧的 2048 ⇒ `***Failed  Required regular expression not found`，**两档 `impossible` 变红、而两档 `possible` 与短对照仍然全对** ⇒ 正好只有该红的红。**本会话独立实测**：`./build/inimerse --no-mods vtest/match_long_pattern_v06.im` 输出六行、`RC=0`，逐字 `match-long 2030 impossible=false` / `match-long 2030 possible=true` / `match-long 3000 impossible=false` / `match-long 3000 possible=true` / `match-long 50 impossible=false` / `match-long 50 possible=true`。**⚠ 处置已变：`vivid-anchor` 已批准现在修，指定走方案 1（动态分配）、并要求守卫跨平台注册**（理由正是本档 §3.1.10 那句：Windows 那份本来就对，只在 POSIX 注册会让「Windows 是对的」**没被钉住**）。`exact-lumen` 已落地，**本会话复核**（**读于 `711a339`**）：`src/runtime/runtime_posix.c:145-147` 当时是 `size_t tcap = 16 * strlen(pattern) + 1; char *translated = (char *)malloc(tcap); if (!translated) { pop(vm); pop(vm); push_bool(vm, 0); return 1; }`，循环边界 `:149` `j + 16 < tcap`，`:180` `free(translated);`。★ **这三个号今天全部漂了（`main @ 3df3a9b` 实测）**：同一段代码今天在 `:182`（`size_t tcap = 16 * strlen(pattern) + 1;`）与 `:183`（`malloc`），循环边界在 `:186`，`free(translated);` 在 `:217`。⇒ **认这段代码请认锚、不要认号**；本行前三处 `:131` 同理 —— 那三个号的树是 `7ba57aa`，而那个定长缓冲**已被 `3743b99` 删除**（删它的正是 `7ba57aa` 的下一笔）。**登记点（本会话在 `stream/builtin-contract-rulings` 上实测）**：`docs/AUDIT.md` §1.64 = **`:2859`**（§1.63 在 `:2832`、`## §2 执行通道效率比较` 在 `:2940`）；`docs/STATUS.md` §10.97 = **`:3962`**；`docs/BOARD.md` 新行 `match-pattern-truncated` = **`:272`**（`:271` 是 `literal-host-skips-resolver`）。**⚠ 行号按 ref 而异，引用必须同时写 ref + SHA（`exact-otter` 裁定，采纳；见 §10 的口径说明）**：它驳回了「删掉行号只留纪律」的倾向，理由是「**行号本身没错，错的是没记口径**」—— 删掉之后下一个人还得重新量、且**没有东西告诉他两个 ref 上的行号不一样**。格式定为本档统一口径：`<ref> @ <sha>` → `<file> <总行数>，<符号> :<行>`。　★ **同一个号上的内容在那两棵树之间变了**：号没有漂，是那一行换了；本行的坐标读于写下它的那一刻。[obs: 07ffd69929f9 -> main @ 711a339]

**成因已查明（不是「内容没进」）**：`:1348` 起那两行并列承诺注释**两边都在**；差异是 `release/051-final` 多 **4 行注释**（`Both halves are asserted -- an expectation built by joining lines with "^" anchors to the end of the merged output and never matches, so they are separate list items instead.`）+ 多 **1 行 `FAIL_REGULAR_EXPRESSION`**，净 +5 行 ⇒ 1359 与 1363 之别。**`git merge-base --is-ancestor a85cdf8 stream/builtin-contract-rulings` → NO**（merge-base 是 `517e7dc`）⇒ **那个提交不在本分支上**。**这正是 §9 第 23 条那个洞的近亲：一个行号被两处抄、而没有任何东西比较它们 —— 并且它已经咬过一次**（见下条）。**⚠ 16 倍是上界不是测量（它的诚实边界，我采纳并原样登记）**：`tcap = 16 * strlen(pattern) + 1` 的 16 倍是**推出来的上界**（最大展开 `\w` → `[[:alnum:]_]` 是 13 倍），**不是实测的最大展开比**。 | §9 第 24 条 (a)、§3.1.10、§3.1.8 |
| 26 | `CMakeLists.txt` 的 `match_long_pattern_runtime` 块 —— **两个 ref 上行号不同，必须按 ref 读**：`stream/builtin-contract-rulings @ 056e67b` → **1359 行，守卫 `:1355`（`:1359` 只有 `PASS_`）**；`release/051-final @ c71ea00` → **1363 行，`add_test` `:1358`、两条断言 `:1362`/`:1363`**；`vtest/match_long_pattern_v06.im` | **守卫注释声称的性质比守卫实际断言的多 —— 而成对的那一半只在 fixture 里打印、没有任何断言读它。** 本会话实测（读码 + 跑 fixture）：`CMakeLists.txt @ c71ea00:1352-1353` 的注释逐字写 `Each length is paired: the impossible pattern must be false AND the possible one must be true, so that a non-fix which simply always answers false cannot pass`；但 `@ 056e67b:1359` 的 `PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"` **只命名 `impossible` 那一句** ⇒ **一个恒 `return false` 的假修法照样通过**（它仍然会打印那一行、正则仍然命中）。fixture 那一侧是**真的成对**的：`./build/inimerse --no-mods vtest/match_long_pattern_v06.im` 实测输出六行且 `RC=0`（两档 `impossible=false` **与**两档 `possible=true`、另有 50 字节短对照两行）⇒ **缺的不是数据，是断言**。**⇒ 这是第 21/22/23 条那个洞的第五个投影，而且是这五个里最尖锐的一个**：前四个是「检查器在看错的量」（退出码、文档结构、抄来的输出、源码长度），**这一个的检查器看的量完全正确，只是看漏了那句注释自己承诺要看的一半** —— **注释写了一个比代码强的性质，而没有任何东西比较「注释说的」与「断言做的」**。`docs/AUDIT.md` §1.64 与 `docs/STATUS.md` §10.97 的说明文字都复述了这个成对意图，**同样没有被任何断言支撑**。**机制（`exact-lumen` 本轮读 CMake 侧读出来的，本会话复核语法格式，我采纳并单列 —— 它让这一类漏有一个确定的技术形状）**：`PASS_REGULAR_EXPRESSION` 是**列表**（分号分隔，本仓已有先例：`CMakeLists.txt @ e3da33c:690` 的 `posix_runtime_parity`（**原文这里写的是 `:673`，陈旧指针**，+17） 用 `"posix-runtime-parity-ok;${INIMERSE_PARITY_LOAD_MISSING}"`）；**⚠ 此处原写「列表里每一项都必须命中（不是任一命中）」，与同一格下方 ⚠⚠ 的实测结论（任一命中即通过）直接矛盾，已删**（更正写进同一格而原文不删 = 自相矛盾，§9 第 32 条）。**每一项都对整段输出扫描，而 CMake 的 `cmsys::RegularExpression` 不带 `REG_NEWLINE`** ⇒ **`^` 只锚定整段输出的开头、`$` 只锚定整段输出的末尾，不是每行的首尾**。⇒ 所以「把两件事写进一条正则」的写法里，**第二半永远不可能命中**（`^` 要的是整段行首）—— **这不是「正则写得含糊」，是「在只锚整段首尾的方言里写了一个以为在锚行的模式」**。**⚠⚠ 判据措辞已被实测证伪一次，必须按改正后的形态写（`exact-lumen` 本轮撤回它自己的方案①）**：它原建议「把 `:1359` 拆成 `"…impossible=false;possible=true"`」，被 `vivid-anchor` 实测推翻 —— **`PASS_REGULAR_EXPRESSION` 的列表语义是「任一命中即通过」，是或、不是与**：把 `posix_core_match` 改成恒 `push_bool(vm, 0)`（正是要抓的假修法）重建后，那条测试**仍然 `100% tests passed`**。⇒ **我原来那句「必须在列表里逐项找到对应物」的措辞会把人引向这个错**（按字面读，加一项就是「逐项找到了」）。**改正后的形态**：落地的**不是**同一个列表加一项，而是 **`PASS_` 一项 + `FAIL_` 一项（两个不同种类的属性）** —— 只有 `FAIL_` 是**命中即红**，才有读者看着「**没有任何可能模式答 false**」这件事。`release/051-final` 上的落地逐字：`CMakeLists.txt:1362` `PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"` + `:1363` `FAIL_REGULAR_EXPRESSION "match-long [0-9]+ possible=false"`。**⇒ 判据（改正后，可证伪）**：**注释里的并列承诺，必须有对应物；而对应物不能塞进同一个 `PASS_` 列表**（那只是「或」）—— 需要两个**不同种类**的属性（`PASS_` + `FAIL_`），或两条断言各管一半；找不到对应物时要么补断言、要么删承诺。**注释不成立比断言不够更坏，因为注释会被下一个读者当成证据**（与 §3.1.11「读起来像 ③、实际是 ①」同向）。**⚠ 子串陷阱的机制补全（`exact-lumen` 给我、我采纳成可执行形式）**：裸模式 `possible=false` 锚不上左边界，**是因为它前面是 `im`**，而 `impossible=false` 里的 `im` **恰好也是 `match-long 2030 ` 的末尾** ⇒ **受害者不是「长得像」，而是「前缀边界落在词内」**。⇒ 「带左边界」不够可执行，改成：**`FAIL_` 模式必须锚在行首或一个非 `[[:alnum:]_]` 字符之后**；否则它在输出里找的是「任意位置出现的一串字符」，而 **fixture 输出的空格分词会让它在词内命中**。 **⚠ 观测点更正（2026-10，`exact-otter` 实测；本段原写 `@ 5cf4aa1`，是错的）**：`5cf4aa1` 与 `056e67b` **不是同一棵树** —— `5cf4aa1` 是 `056e67b` 的**祖先**，中间隔着本档自己的 `e2d44e2`；两棵 tree 分别是 `f5988c13eca99c1b2c1fbd1335c16d9d68ec660f` 与 `68a1849a7744441da450263bd94822a16904dfed`，**`git diff --stat 5cf4aa1 056e67b` 只差 `docs/DECFY_DESIGN.md`（+9/−3）** ⇒ **被引用的那个 `CMakeLists.txt` 事实恰好在两个观测点上相同**。**正确读法：这是「两个不同的观测点被当成一个来引用，而它们恰好给出同一个读数 —— 是巧合，不是性质」**；若这两个提交之间有人动过 `CMakeLists.txt`，本档第 25/26/27 条的行号就会**静默地互相打架，而没有任何东西会看见** —— 这是 §9 第 23 条（一个行号被两处抄、没有东西比较它们）的**近亲，只是这次抄的是观测点而不是行号**。⇒ **本档口径因此再加一句：引用的观测点必须是「给出该读数的那个提交」，不是一个附近的提交**（本会话已把此处统一到 `056e67b`）。**⚠ 位置更正（本会话第十六轮）**：这段原先落在**第 25 条**的行里（其宾语是 `CMakeLists.txt` 的块，属本条），而且**第 25、26 两行各抄了一遍同一组行数** ⇒ 同一个读数录了两遍、没有东西比较它们。已整段移入本条。 **⚠ 裁定（2026-10，`exact-otter`）：定论 = 选项 (1)（`PASS_` 与 `FAIL_` 并存），且已在 main 上落地。** `main @ e9debfd` → `:1398` `PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"`、`:1399` `FAIL_REGULAR_EXPRESSION "match-long [0-9]+ possible=false"`（`FAIL_` 数 = 1）；`release/051-final @ c71ea00` → `:1362`/`:1363` 同形（`FAIL_` 数 = 1）。**⇒ 唯一没有 `FAIL_` 的树是 `stream/builtin-contract-rulings @ 056e67b`**（1359 行、`:1359` 是末行、只有 `PASS_`）——**缺陷只活在落败的那一侧，main 上不需要任何 CMake 改动**。**⚠ 观测点更正（本会话按实测核）**：裁定文字里把「无 `FAIL_` 的那一侧」写成「`release/051-final @ c71ea00` 与 `stream/builtin-contract-rulings @ 056e67b`」，**但它自己给的读数显示 `c71ea00` 的 `FAIL_` 数 = 1** ⇒ **`c71ea00` 是有 `FAIL_` 的那一侧**；本条按实测写：**无 `FAIL_` 的只有 `056e67b`**。 **⚠ 当前树读数（本会话实测，`@ e3da33c`）**：`main` 的 `CMakeLists.txt` 已 **1412 行**（`e9debfd` 上 **1399 行**，差 **+13**，来自 `4ca013d` 的 hunk `@@ -1031,8 +1031,21 @@`）⇒ 同一个块现在在注释 `:1401-1402`、`add_test` `:1407`、`PASS_` `:1411`、`FAIL_` `:1412`；**本条前面那些号一律按各自点名的 ref 读，不要按当前树读。**
**⚠ 子串陷阱（这条是配套纪律，`exact-lumen` 报、我立）**：`vivid-anchor` 的第一版 `FAIL_REGULAR_EXPRESSION` 写 `"possible=false"`，**它匹配上了 `impossible=false` 里的子串**（`im` + `possible=false`）⇒ **真修好的代码也红**。这与 `strstr("audio","io")` 过授 `CAP_IO` 是**同一个形状：子串匹配把「看起来像」当成「就是」**。锚成 `match-long [0-9]+ possible=false` 之后两个方向才对。⇒ **规矩（立即生效之三）**：**`FAIL_` 正则默认必须带左边界**，因为 `FAIL_` 是「命中即红」，一个真阴性会被子串吃成假阳性；`PASS_` 同理会把假阳性吃成真阴性。**这条目前只以「踩过」的记录存在，没有判据** —— 见 §9 第 27 条。**具体到本条的反向验证（已被 `exact-lumen` 实测做到，在 `release/051-final` = `c71ea00` 上只动一个 `.c`、走 CTest 本身）**：真修法 `1/1 Test #142 ... Passed 0.16 sec`；假修法（体首插 `pop(vm); pop(vm); push_bool(vm, 0); return 1;`）`***Failed  Error regular expression found in output. Regex=[match-long [0-9]+ possible=false]`；还原后 `cmp` 对 `a85cdf8:src/runtime/runtime_posix.c` **逐字节相同**、重建回绿、worktree 干净。**⇒ 成对性今天真的被钉住了。****⚠ 处置已定并已落地（2026-10，`exact-lumen` 授权后动手）**：它**先报了一声**（`vivid-anchor` 本轮要求「摸到别的 `src/**` 静默错误先报一声再动手 —— 我不想在最终复核跑到一半时再挪基线」），随后按授权改掉。**它自己的方案①被实测证伪并已撤回**（见上）。**它原来的注释那两行没被删**（`:1343`–`:1357` 的注释块里 `Each length is paired: the impossible pattern must be false AND the possible one must be true …` 仍在，后面接了三行新注释）—— ⇒ **注释与断言现在一致了**，这是本条要的结果。**独立锚点**：CMake 自己的 `_BACKTRACE_TRIPLES` 把行号记为 `CMakeLists.txt;1358`（`build/CTestTestfile.cmake:290`）。**以下是被取代的旧记录，保留作「方案错了怎么发现的」**：它原先给的方案：① 正则拆成两项、补上 `possible=true`，并加一行注释记跨行锚定教训，反向验证用「恒 `false` 必须变红」；② 若被否，**把 `:1352-1353` 的注释改成它今天实际断言的东西**（删掉 `AND the possible one must be true` 那半句，写明成对性由 fixture 保障、见本档本条）。**它拿到答复后一次给新行号，本档那时再同步第 25/26 条引用**（两个方案都会移动行号）。**⚠ 登记不改**（`CMakeLists.txt` 与 `vtest/` 都不在本文档写域，且发版窗口内 `exact-lumen` 说它此后不动受跟踪文件） | §9 第 25 条、第 21–24 条 |
| 27 | `CMakeLists.txt` 的 `FAIL_REGULAR_EXPRESSION "match-long [0-9]+ possible=false"`（**`release/051-final @ c71ea00` 的 `:1363`**；`stream/builtin-contract-rulings @ 056e67b` 上**该行不存在**）、`src/runtime/runtime_posix.c` | **`FAIL_` 正则没有左边界时，一个真阴性会被子串吃成假阳性 —— 而这条规矩目前只以「踩过」的记录存在，没有任何判据。** 实例（`exact-lumen` 报、机制清楚）：`vivid-anchor` 的第一版写 `FAIL_REGULAR_EXPRESSION "possible=false"`，**它匹配上了 `impossible=false` 里的子串**（`im` + `possible=false`）⇒ **真修好的代码反而变红**；锚成 `match-long [0-9]+ possible=false` 之后两个方向才对。**同形状的既有实例**：`strstr("audio","io")` 过授 `CAP_IO` —— **子串匹配把「看起来像」当成「就是」**。**为什么它够格单列**：`FAIL_REGULAR_EXPRESSION` 是**命中即红**，所以它的**假阳性方向是「把对的判成错的」**（与 `PASS_` 相反：`PASS_` 的子串会把假阳性吃成真阴性）。**两个方向的后果不同，所以规矩也不同：`FAIL_` 必须带左边界。****判据（可证伪）**：对每一个 `FAIL_REGULAR_EXPRESSION`，断言其模式**不匹配任何 `PASS_` 期望串的子串**；**反向验证**：把 `:1363` 改回裸 `"possible=false"`，**必须能观察到真修好的代码变红**（即复现这个陷阱本身）。**⚠ 登记不改**（`CMakeLists.txt` 不在本文档写域；此条是「立一条纪律」，而纪律的落地位置属于 `tools/`/`CMakeLists.txt` 的写域） | §9 第 26 条、§3.1.11 ④ | **⚠ 不升格为跨仓规矩（2026-10，`exact-otter` 裁定）**：这是 `FAIL_REGULAR_EXPRESSION` 的**正则匹配语义（左边界）**，**不是本仓的约定** —— 升格会把一条 CMake 的既有行为写成「我们的规矩」，而**没有东西会去核对它**。**故只留在本条当实例**；本条判据里的「必须带左边界」应读作「必须按 CMake 的正则匹配语义写模式」，而不是「本仓另有一条规矩」。
| 28 | `docs/DECFY_DESIGN.md` §9 第 25/26 条（本档自身）、`CMakeLists.txt` 两分支 | **「文件总行数」被从「某一行存在」推断出来 —— 而这是本档自己的纪律（§3.1.11 ④）刚刚禁止的那一步。** 经过（`exact-lumen` 自首，我核实）：它上一封信写 `CMakeLists.txt 现在是 1363 行（旧 1359）`，但它**只在 `/home/sakiko/wt-051` 上量到 `:1362`/`:1363` 是那两条断言、块尾不在那里** ⇒ **`wc -l` 它没跑过，1363 是从「`:1363` 有内容」推出来的**。**本会话实测**：`wc -l` 在 `release/051-final @ c71ea00` 上**恰好也是 1363** ⇒ **它的推断这次偶然对了**，但**得到它的那一步是无效的**。**⇒ 这是 §3.1.11 ④「体量差不是行为差的证据」的镜像**：④ 说「**A 比 B 长**不能推出 A 与 B 行为不同」，这条说「**`x` 行有内容**不能推出**文件总共有 `x` 行**」—— **两者都是「从一个没有跑过的测量里读出一个数」。** 我把 §3.1.11 那条自用规矩因此扩一句：**任何行数/字数/条目数的主张，必须来自 `wc -l` / `grep -c` 一类真正计数的命令，不能来自「我看到最大行号是 N」。****⚠ 登记不改**（本档文字已按实测改正；无代码改动） | §3.1.11 ④、§9 第 25 条 |
| 29 | `tools/fixture_parse.test.py:76`（`"inimerse.exe"` 分支）、`CMakeLists.txt` 给 `argv[1]` 的调用路径、`build/CTestTestfile.cmake:230` | **一段代码在某平台上声明了另一个平台的保证，而没有任何东西比较「它说了什么」与「它跑没跑到」。** `exact-lumen` 本轮量到：`tools/fixture_parse.test.py:76` 的 `"inimerse.exe"` 分支**在任何 Linux 运行里都不可达** —— 唯一调用路径是 `CMakeLists.txt` 给 `argv[1]`，而 `build/CTestTestfile.cmake:230` 那条属性**完全不带 `ENVIRONMENT`** ⇒ **`INIMERSE_BIN` 在门禁里未设**。**它同时是兼容性诱饵**：那处 `os.access(cand, os.X_OK)` 少了 ⇒ **把它抄到 Windows 上会得到更弱的复制品**。**⇒ 与第 27 条是同一个洞的两侧**：第 27 条是「`FAIL_` **在词内**命中」，这条是「**一段代码在平台 A 上声明平台 B 的保证**」——两者都是「**没有任何东西比较「它说了什么」和「它跑没跑到」**」，正落在 §1 的核心命题（**同一语义的多个决定点，且没有任何一处勘验它们**）上。**判据（可证伪）**：对该文件断言「在 Linux 门禁路径下，那个 `.exe` 分支的命中计数为 **0**」；**反向验证**：把 `INIMERSE_BIN` 设成一个存在的路径，该计数**必须变成 1**（证明这个断言真的在测可达性，而不是恒真）。**⚠ 登记不改（REQUIRED）**：`exact-lumen` 明确说它**未提交、也未跑过删改后的版本**（三次工具调用被 `[LOOP_GUARD_DUPLICATE]` 拒），文件是 `:68-81` 那版 17 行原文、**没有半成品** ⇒ **本档不得写成「已修」**。`tools/` 也不在本文档写域。 | §9 第 27 条、§1、§3.1.11 ⑤ |
| 30 | `docs/AUDIT.md` §1.65、`docs/STATUS.md **§2**（`@ 28a49f3^` → `:51`；**被引文字已由 `28a49f3` 删除**，`@ e9debfd` 上 `grep -c "Total Tests: 131"` = **0**）`（**只引节号，不引 `docs/AUDIT.md` 的行号 —— 那份正在被改**）；修法落在 `28a49f3` | **一般式（2026-10，`exact-otter` 采纳 `ivory-ember` 的更强表述；§9 第 32 条已并入本条）：一条关于「引用形态」的规矩，必须对写这条规矩的文字本身成立。** **实例①（断言）**：**「写下结论的动作本身让结论失效」—— 断言的主语是「仓库里有没有某串字符」，而写下这个断言的动作会往仓库里加一个。** 实例（`exact-otter` 报、真缺陷、已修）：那两处都写「全库 `grep -rn 'Total Tests: 131' docs/ tools/` **零命中**」，而**在合并树上该命令有 4 处命中，其中两处就是断言行自己**。**加重子检（在它自己那棵树上）**：`git show 57ece55^2:docs/AUDIT.md | grep -c 'Total Tests: 131'` = **2** ⇒ **与合并无关，写下那一刻就自足地假**。**修法**：删掉「几处」这个代理，换成**直接断言**（`131` 作为 Windows 读数的出处是 `docs/RELEASE_0.5.1.md:14`），并写明「**本条不写『几处』**」⇒ **只枚举产出它的命令，不枚举输出。** **判据（可证伪）**：凡断言形如「仓库里不存在字符串 X」，**该断言本身不得包含 X 的字面量**；若必须给出处，**给出产出读数的命令，不给读数本身**。**⚠ 与 §9 第 22/23 条的关系**：那两条是「**没有东西比较文档抄的期望串与真实输出**」；这一条更窄也更毒 —— **比较是有的（`grep` 就在断言行里），但它把「我写下的这句话」也数进去了** ⇒ **一个自指的测量**。 **实例②（解释，原 §9 第 32 条，2026-10 并入）**：**解释一条违规的文字，不得采用那条违规的形态。** 来源是 `exact-otter` 的自首：它用来解释「`docs/AUDIT.md:3851` 指向一个不存在的行」的那两句，**本身就写着 `docs/AUDIT.md:3851` 与 `docs/STATUS.md:3851`** ⇒ **解释违规的文字自己违反了刚立的判据**（它在复跑那条判据时被抓，已改成非引用形态）。**为什么并进本条而不是单列**：第 30 条的病是「**写下结论的动作本身让结论失效**」，而实例②**正是同一种自指** —— 断言与解释都是「关于仓库的文本」，**都会成为仓库的一部分**，因此都受同一条约束；单列会把「断言 vs 解释」这个区分当成两种病，而这条病恰恰是从那个缝里出来的。**判据（可证伪）**：一条用来描述某违规的句子，必须能通过「该句本身不触发该判据」的检查；若该句采用了被它描述的违规形态，它自己就会被判据抓住。**反向验证**：写一句解释「不得出现裸行号」的话、句内带一个裸行号，该检查必须变红。 **⚠ 本条自己也复述了那个违规形态**（上面那句里的 `docs/STATUS.md:3851` 与 `docs/AUDIT.md:3851`）：**复述一个反例必须引用它的原文**，这是本条的**唯一例外** —— 而例外与犯规的区别是：**例外是「引用」且标明了自己在引用，犯规是「采用」**。**而引文里那个号本身在动（用命令，不用值）**：`git show <你读的那棵树>:docs/AUDIT.md \| sed -n '3851p'` —— **无输出** = 该号越尾（写下时就是这一支）；**空行** = 该号存在但不指任何东西（`main @ ce403cb` 那一代）；**有内容** = 该号存在且指着某个东西，**而它是不是这句话说的那件事要另判**（`main @ 2157cc1` 这一代读到 §9 内存表里的一行）。**命令必须绑在它要回答的那棵树上**：不带 `git show <ref>:` 的形式读的是工作区，答的是「你这棵树的那一行」。若把引用也删掉，下一个读者就无法判断反例到底长什么样（与 §3.1.11「读起来像 ③、实际是 ①」同向）。 | §9 第 23 条、§3.1.11 |
| 31 | `CMakeLists.txt` 的 `if(WIN32)` 块（**只引符号，不引行号**）、`src/runtime/runtime.c`、`src/runtime/runtime_posix.c` | **「空绿」—— 一个在构造上无法为红的判据不是判据。** 实例（agent3 在 `exact-otter` 要引用它之前**自己报备**）：`CMakeLists.txt` 的 `if(WIN32)` ⇒ **`src/runtime/runtime.c` 只在 Windows 上编译**，POSIX 编的是 `src/runtime/runtime_posix.c`（libc `regcomp`，里面没有 `re_seq`）；而 agent3 要改的正是 `runtime.c` ⇒ **同树 Linux 门禁根本不编译被改的文件**，那条判据**在构造上不可能变红**，它**绿得毫无信息**。它主动说「**别把它当成『Linux 也验过了』**」。**三个成因（都要能识别）**：(a) **它不编译被改的文件**（本例）；(b) **它不测那个平台**；(c) **它量的是它自己**（§9 第 30 条就是 (c)）。**同族的第四个**：differential fuzz 在 Linux 上比 interp vs AOT，而 `runtime.c` 在那里不参与构建 ⇒ **对这条修同样是空绿**。**判据（可证伪）**：**每一条验收判据都必须能点出一个会让它变红的具体观测**；**点不出来，它就不是证据** —— 报告里必须**显式标注为「空绿」**，且**永远不许当作该修的验证引用**。**⚠ 与 §6 判据的关系**：本档 §6 那些「从 0 变成 69」的判据**必须逐条自问属于哪一种绿**；`grep` 类判据要特别小心 (a)/(c)，因为**它们可以在完全不编译代码的情况下通过**。 | §9 第 30 条、§6、§9 第 26 条 |
| 33 | **本文档自己**（§0.5 证据项 ① 的外引 + §3.1.11 的自引；**第 32 条已并入第 30 条，故本条起用 33**） | **引用一份会生长的文件，不许用行号 —— 行号是位置，节号是名字；位置随插入失效，名字不会。** 本档 §0.5 早已写下这条规矩（「对本文档的引用请引节号，不要引行号」），**而它自己没有遵守，也没有管住引用方**。**普查给命令、不给现值**：`git ls-files -z \| xargs -0 grep -n 'DECFY_DESIGN.md:[0-9]'`（去掉本档自身那些行）—— 写这句时 `@ 2303496` 读得 **18 行 / 13 个号**，其中 **7 处仍成立**（`:8-12` ×6、`:10-12` ×1）、**11 处已指错对象**（`:76` ×6 与 `:125` ×3 今天都是**空行**、`:24` ×2 是 §0.5 的 `%` 行）。**⚠ 这个数在本轮写它的过程中变小过一次**（我 `@ fa8247e` 量得 **26**；`exact-otter` 在 `2303496` 把 `docs/AUDIT.md` 与 `docs/TYPESET_V06.md` 的 6 行改成节号 ⇒ **18**）—— **指针指向一份正在被改的文件时，「最后取」也只是一个瞬间。** **最刺眼的两处不在文档里，在引擎源码的注释里**：`src/vm/vm.c:338`/`:339` 与 `src/vm/vm.h:26`/`:48` 的注释把 `:125`、`:24`、`:76` 写在括号里引本档 —— **四条都指着空行**（`@ 2303496`）。**它们写的时候全是对的**（`022d002` 上四个号逐字全对，当时 **249 行**；今天 **1156 行**）⇒ **这一族病的定义就是「写下时是对的，而『是对的』不阻止它变假」。** **判据（可证伪）**：任何指向会生长的文件的位置，必须是**名字**（节号、符号名），不得是**位置**（行号）；**若必须给位置，就同时给出产出它的命令**，且命令要绑在它要回答的那棵树上（`git show <ref>:<file> \| sed -n '<n>p'`）。**⚠ 射程（本会话自查）**：上面那条普查**只认「文件名:数字」这一种形状**，**漏掉裸 `` `:NNN` `` 形式的自引**（本档 §3.1.11 里就有十几处）⇒ **它能证明「找到 N 处」，不能证明「只有 N 处」**。**射程规矩（`exact-otter` 2026-10 裁定）：位置形式的引用必须与它的文件名同段** —— 裸号的指代从**最近的段内文件名**继承，病发在「文件名在另一段」（与 §1.74 第 12 条「更正的单位是段落」同一条）。 | §0.5 证据项 ①、§3.1.11、§9 第 30 条 |

**一条关于本文档自身维护的教训（写给下一个改这张表的人，也写给未来的我）**：本会话**五次**用 `edit` 插入新内容时，`old_string` 取的是**上一行/目标行的行首片段或整行标题**，结果**把被锚定那行的前缀吃掉**（第一次是 §9 第 16 条，第二次是第 19 条；**第五次最严重——见下**）。**三次都是事后用 `python3` 补回前缀 + 按行首编号重排才修好。**

**第五次（2026-10，本会话）与前面四次不是同一种损坏，必须单独记**：我在 §3.1.10 之后插 §3.1.11 时，把 `old_string` 取成 `### 3.2 \`aot_native.c\` 具体要改什么` 这**一整行标题**，而 `new_string` 结尾**没有把它带回来** ⇒ **`### 3.2` 这个标题被删掉了，它下面那条编号列表（「1. 删除 `EXPR_BINARY`…」）失去了父标题，直接挂在 §3.1.11 的诚实边界后面。** 后果与吃表行前缀同源但更隐蔽：**文档里没有任何一行看起来是坏的**，`--only doc-paths` 与 `--only links` **都仍然 RC=0**（链接检查器只查反引号里的路径，不查标题结构），我是**为了核对 §3.1.11 的 69 行有没有数对、顺手 `grep -n '^### 3\.'` 才发现的**（`### 3.2` 在列表里消失了）。⇒ **两条新规矩**：① **`old_string` 是整行标题时，`new_string` 必须把那一行逐字带回来**（或者改用「在标题行之前插入」的写法，即把标题行连同新内容一起放进 `new_string`）；② **改完任何一节，必须跑一次 `grep -n '^#\+ ' <file>` 并核对标题层级与数量没有变化** —— 这是本文档**唯一**能发现「标题被吃」的检查，两个文档门禁阶段都看不见它。**这两条与上面那条合起来是同一个断言：结构损坏和内容损坏不是同一种损坏，只有分别的 grep 能区分它们。**

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
| **[实测]（本会话，2026-10 第六轮：69 行骨架逐块读出）** | ① 69 个指令体 = `L_X:` 标签块，**连续覆盖 `src/vm/vm.c:3146-4680`**（`^ *(L_[A-Z0-9_]+):` 匹配 69 个，第 k 块区间 = 第 k+1 标签行 − 1）；② 按「是否写帧外状态」手工分类得 **P=37 / C=3 / S=29 = 69**；③ 正交第二轴 `can_raise`（块内含 `vm_throw`，helper 在 `src/vm/vm.c:2481`）= **16 个**；④ **交叠空格的发现**：`P ∩ can_raise` = **11 个**（`ADD` `CONCAT` `SUB` `MUL` `DIV` `NEG` `LT` `GT` `LE` `GE` `MOD`），`S ∩ can_raise` = **5 个**（`INDEX_SET` `STORE_GLOBAL` `CALL_BUILTIN` `BE` `THROW`），`C ∩ can_raise` = 0；⑤ 12 行的机制与名字不符（`DECLARE` 写 `vm->limit_*`、`RECORD` 写 `vm->record_*` 并取 `VM_LOCK`、`BE` 取全局分片锁 + `vm_global_grow`、`LOAD_GLOBAL` 条件加锁、`INDEX_GET` 的 dict 读加锁、`NEW_SET`/`SET_INTERVAL` 是**新建**而非共享写、`SAY` 绕过平台层走 `src/vm/vm.c:160-161` 的 POSIX shim），逐行见 §3.1.7；⑥ 命令：`python3` 按标签区间切块 + `grep -c vm_throw`；`sed -n '<区间>p' src/vm/vm.c` 可逐格复核。**分类是手工判定，不是自动推导**（§3.1.7 诚实边界）。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 第二个先例（§3.1.8「拒绝不了就比对」）：① **四处文档声称有 CTest 覆盖、四处都没有** —— `docs/API.md` 第 2.1 节标题逐字「证据（CTest）」，`:114` 把 `lint_case_missing_default_v04.im` 写成「相关 CTest」、`:115` 把 `lint_case_exhaustive_v04.im`（**文件名，不是测试名**）与四个真实测试名并列；`docs/REQUIREMENTS_ANALYSIS.md:177` 与 `docs/STATUS.md **§3**（`@ e9debfd` → `:286`；`ab70a71` 与 `main` 该行**同文**、未漂移）` 把 `tools/migrate_report.py` 与 `bindgen_regression`／`scan_tools_regression` 并列，而 `tools/` 下**没有** `migrate_report.test.py`；② **根因**：`CMakeLists.txt @ e3da33c:756-766` 的五个 `lint_case_*` 是**手写列举**（`@ e3da33c:767` 注释自己数着「The five lint_case_* tests above …」；**原文这里写的是 `:733-743` 与 `:744`，陈旧指针**，随 main 前进 **+23**），**仓库里没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**；③ **反向验证**：把 `CMakeLists.txt` 退回 `HEAD` ⇒ `2 orphaned input(s) out of 110 checked` + 逐条点名 + `exit 1`；④ **普查**：67 个 `vtest/*.im` 中 5 个未被 `CMakeLists.txt` 提到（两个真缺口、三个合法）、30 个 `tools/*.test.py` 与 13 个 `tools/*.test.js` **0 孤儿**；⑤ **新设施**：注册 **#135** / **#136**（各带 `PASS_REGULAR_EXPRESSION` + 一条断言对方警告不出现的 `FAIL_REGULAR_EXPRESSION`）、新增 `tools/check_orphan_fixtures.py` 与**门禁第 12 阶段 `orphan-fixtures`**；⑥ **该脚本自己崩过**：`NameError: name 'CMAKE_SOURCE_DIR' is not defined`（f-string 里的 `${…}` 被当替换字段），修为 `${{CMAKE_SOURCE_DIR}}`；⑦ **门禁**：`GATE_RC=0`、`gate: OK — every stage passed (12/12 stages ran).`、**`100% tests passed, 0 tests failed out of 136`**、`0 skipped`。**我未独立复核以上任何一条**；其中「12 阶段 / 136 测试」与我 §3.1.8 记的 `EXP_CTEST = 134` **不一致**（该值在 `tools/gate.sh:54`，本轮门禁正在被 merge 修改，**以仓库现状为准，我未重测**）。 |
| **[转述，来源：人类裁定，非本会话实测]** | 2026-10 的**第四个人类裁定**（§3.8「契约式双层判定」）：走 (ii) 的强化版 —— 编译期保守推断（`CompilerSet ⊇ RuntimeSet`）、运行期契约执行（`OP_BE` 是编译期未竟事业的兜底）；Debug/Strict 模式下在已证明路径插等价断言，断言失败按**编译器级错误**处理；按执行模式分层（静态模式强制 (ii)，动态/`var` 模式**退化为 (iii)**，跨边界插显式转换检查）；否决 (iii) 的理由是**形式化验证与 Proof 会失去根基**、集合论类型沦为摆设。**本行的内容是转述，裁定以人的原话为准**；§3.8 里我另外标出了**三处「裁定措辞与可实现的断言之间还有距离」**（`==` 应写成 `∈`；`OP_BE` 在裁定内部有两个角色、需按「恰好三处」的读法落地并请人确认；「无法证明 ⇒ 编译错误」是新语言规则且**未量过**会让多少 `.im` 编译不过），**那三条是我读裁定后的判断，不是裁定的内容**。 |
| **[实测]（本会话，2026-10 第九轮：类型代数零消费者 + 两个集合实现）** | 裁定 B″（`var` 动态、其余静态）之后的复核：① **类型代数存在且完整**：`src/types/typeset.c` **248 行**、`src/types/typeset.h:37-45` 导出 `im_typeset_empty`/`any`/`enum`/`int_interval`/`union`/`intersection`/`difference`/`complement`，`:47-56` 导出 `contains`/`subset`/`intersects`/`kind`/`cardinality`/`materialize_enum`；`src/types/` 共 957 行；② **它被构建、被测试**：`CMakeLists.txt @ e3da33c:417` 编进引擎、`@ e3da33c:157-159` 有 `typeset_probe` CTest（**本会话复核：两个号均未漂移**），`src/types/typeset_probe.c` 断言的正是 ROADMAP_3.1 点名的 `im_typeset_int_interval(0,255,true,true)`（u8 窄化）与区间交、枚举并；③ **它零消费者**：`grep -rl "im_typeset" --include=*.c --include=*.h src/ \| grep -v "^src/types/"` → **0 个文件**（全仓只在 `src/types/` 内 9 个文件里出现）；`grep -c "typeset\|TypeSet\|type_of\|infer" src/compiler/compiler.c` → **0**；④ **今天类型是运行期的值**：`src/compiler/compiler.c:2224-2231` 的 `case STMT_TYPE` = `register_global(comp, tname)` + `compile_expr(comp, stmt->typeStmt.set)` + `emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0)`；`STMT_BE` 同形（`:2232-2240`，发射 `OP_BE`）；⑤ **`OP_BE` 用 VM 自己的集合，不用 `im_typeset_*`**：`src/vm/vm.c:4262-4308` 内实测命中 `vm->be_bound[g] = sidx >= 0 ? sidx + 1 : 0;` 与 `set_contains(vm, sidx, &init)`；VM 的集合 API 面为 `vm_set_new`/`vm_set_add`/`vm_set_add_comp`/`vm_set_add_comp_dedup`/`vm_set_cur_thread`/`vm_set_free_objs`/`vm_set_slot`/`vm_set_to_array`。**诚实边界**：③ 是字面结论（证明的是「除 `src/types/` 外没有这个字符串」）；⑤ 是**命中行**，我**没有通读 `L_BE` 全文**；④ 是从**发射形状**读出的，**没有实跑 `.im` 观察全局**；**未检查 `mods/`、`projects/`、`vtest/` 里有没有人用 `type`**。 |
| **[实测]（本会话，2026-10 第八轮：零生产点 opcode + 注释损坏）** | ① **零生产点**（`exact-lumen` 提出、**我独立复跑确认**）：`LC_ALL=C comm -23 <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/bytecode.h \| sort -u) <(grep -oE '\bOP_[A-Z0-9_]+' src/compiler/compiler.c \| sort -u)` → **恰好 `OP_POP_REG` 与 `OP_STORE_CAPTURE` 两行**；反向差集 `comm -13 …` → **空**；`grep -c 'emit(comp->curBC, OP_' src/compiler/compiler.c` → **330**；枚举 **69** 个名字 / `compiler.c` **67** 个；② `grep -rl '\bOP_STORE_CAPTURE\b' src/ selfhost/ tools/` → **只有 `src/compiler/bytecode.h` 与 `src/vm/vm.c`**（⇒ 零生产点）；`OP_POP_REG` 多一个 `selfhost/compiler.im:37` 的裸常量定义 `OP_POP_REG = 29`（全文件仅此一处）；③ 两个实现体实测存在：`src/vm/vm.c:3761-3767`（`L_POP_REG`）与 `:3952-3955`（`L_STORE_CAPTURE`）；④ **第二套捕获协议**的证据：`L_MAKE_FUNC`（`:3957` 起）自己从栈取捕获值 `Value *captured = &t->stack[t->sp--];`，而 `OP_PUSH_REG` 由 `compiler.c` 在 `:460`/`:562`/`:575`/`:587`/`:637-638`/`:879`/`:891`/`:903`/`:929` 等十余处发射；⑤ **注释损坏**：`grep -c '锟' src/vm/vm.c` → **58 行**，`awk 'NR>=3146&&NR<=4680 && /锟/' src/vm/vm.c \| wc -l` → **30 行落在指令体区间内**；`grep -n "vm\.c" tools/check_text_integrity.py` → 该脚本 docstring 只提到 `gui_mod.c`/`lexer.c`/`lexer.h` 三个文件的历史 NUL 缺陷，其判据（docstring 逐字）是 **NUL 字节**，故对乱码**无覆盖**。**诚实边界**：③「不可达」是从「零生产点」读出来的、**非运行时实测**（要造含该 opcode 的 `.inim` 才可真测）；② `grep -rl` **未扫 `mods/`、`projects/`、`vtest/`**；④ `enum − compiler.c` **只覆盖 C 编译器**这一个生产者，`selfhost/compiler.im` 的生产点是逐名字看的、**没做集合比对**。 |
| **[实测]（本会话，2026-10 第七轮：builtin 注册表先例，§3.1.8）** | ① **机制拒绝**：`grep -rn "is already registered" src/` → `src/vm/vm.c:1684` 与 `:1708`（两个注册入口各一份），守卫 `if (builtin_lookup(vm, name) >= 0)` 在 `:1683`/`:1707`，注释在 `:1676-1682`/`:1700-1706`（逐字 `One name, one handler.` … `Refuse the duplicate and name it, instead of losing it silently.`）；② **门禁断言**：`tools/gate.sh:140-151`（在 **ctest 阶段内部**）用 `grep -qF "is already registered"` 让阶段失败，注释逐字含 `one name with two answers` / `dead code that reads as live` / `no single test file can see it`；③ **计数断言**：`tools/gate.sh:132-139` 断言 `0 tests failed out of $EXP_CTEST`，`EXP_CTEST` 在 `:54` = **134**，注释 `Assert the count too, so that a dropped add_test( ) cannot pass silently.`；④ **逐平台重算**（按 `CMakeLists.txt` 真实源列表：base `:412-425` 27 个文件、WIN32 追加 `:427-436` 26 个、POSIX 追加 `:438-443` 27 个；正则 `vm_register_builtin(_full)?\s*\(\s*\w+\s*,\s*"([^"]+)"`）：WIN32 **431 次 / 430 distinct / 1 重名**，POSIX **241 / 240 / 1**；唯一重名是 `isolate_run`（`src/isolate_mod.c:227` 与 `:318`），**假阳性**——两处在 `#ifdef _WIN32` / `#else` 分支里，只有一个编译；⑤ `gui_fullscreen` **已不再重复**（`src/mod/gui_mod.c:3690` 是唯一注册点，`:3691-3692` 留注释说明曾重复）⇒ §1.53 登记项**已修**。**诚实边界**：我的正则只认字面量名字 + 简单首参，宏/变量传名会漏；`exact-lumen` 独立数出 WIN32 **398** / POSIX **128**，与我的 **431 / 241** 不一致，**差异未解释、不调和**。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 实现会话的只读测量：① 三条 wasm 程序 —— `say 1 + 2` **RC=0**、`say "hi"` **RC=1**（`error: wasm MVP subset: strings are not supported by the wasm MVP subset (line 1)`）、`s = "hi"; say s` **RC=1**（`error: wasm MVP subset: expression type 2 not supported by wasm MVP subset (strings/collections need the interpreter) (line 1)`）⇒ **wasm 连字符串字面量都产不出来**；② **18 个标量真值用例，解释器与 wasm 逐条相同（18/18、0 分歧）**；③ AOT 侧 `aot_native.test: 104 cases (86 equivalence, 2 pinned divergences, 6 runtime errors, 10 refusal), 0 failures`；④ `grep -c 'EXPR_ARRAY\|EXPR_SET\|EXPR_DICT'` 在 `src/compilation/aot_native.c` 与 `src/compilation/wasm_backend.c` 都是 **0**，`grep -rn 'emit_from_bytecode' src/` 也是 **0** ⇒ **这三个 0 是「该特性不存在」，不是「存在但没接上」**；⑤ **未新增任何 CTest**（没有尚未被钉的行为）。它声明**未改任何 wasm/AOT 源码**，本轮全是只读测量；18 个用例是它自选的、非穷举，且**未测 `NaN`**。 |
| **[读码，2026-10 复核轮补上]** | `docs/STATUS.md` §10.42（`:2464`）/ §10.49（`:2910`）/ §10.53（`:3095`）关键段，以及 §10.50–§10.56 的小节标题（`:2999`/`:3048`/`:3058`/`:3095`/`:3174`/`:3195`/`:3217`）；§10.56 全文（`:3217-3242`，含 `L_ADD` 与 `L_CONCAT` 的「逐字相同的不对称」）；`tools/aot_native.test.py:187-207` 的 `DIVERGENCE` 列表原文 |
| **[转述]（引用 AUDIT，非本会话实测）** | 150 例 fuzz → 41/150 = 27.3% 分歧（28 例 and/or、13 例 int32/`%`、0 例无法归因）；`%` 四组预测值；`(2147483647 + 1).type == float`；解释器 RSS 68.5 MB vs 原生 23.2 MB；AOT 快 7.7×–146.6×，`fib` 比手写 C++ 慢 14×；`tools/selfhost_compare.py` 的 `10 target(s) byte-identical, 23 skipped`（来自工作订单转述，本会话**未实测**） |
| **[读码，本轮补上]** | `docs/archive/RELEASE_0.5.0.md:12/23-28/48/102-106` 的 Native ABI 面、C ABI 类型映射、ABI 版本号、以及「三个后端共享同一字节码格式」这条与现状冲突的承诺；`docs/SYNTAX.md` **§7** 的 §7 分类（危险·静默 / 危险·误导 / 冗余 / 卫生）；**§8** 「退出码经常区分不出对错，必须断言输出数值」 |
| **[实测]（本会话，2026-10 第十轮：`be` 爆炸半径普查，§3.9）** | ① **普查命令**：`grep -rnE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]' --include=*.im .`（覆盖全仓，排除 `build/`、`.worktrees/`、`.verify/`）→ **346 个受跟踪 `.im` 里只有 10 处 `be`**，逐条见 §3.9 的表；② **10/10 的约束表达式只依赖字面量枚举（4 处）/ 字面量区间（1 处）/ 同文件具名类型声明（3 处）/ 内建类型名（2 处）** ⇒ **上界 = 0 个不可解析实例**；③ 内建类型名单实测：`grep -n '"N"' src/compiler/compiler.c src/vm/vm.c` → `src/compiler/compiler.c:2864`、`src/vm/vm.c:1463`/`:1871`，且 `src/vm/vm.c:1880` `if (strcmp(name,"N") == 0) return 0;`（`:1881` `"Z"` → 1、`:1884` `"R"` → 24）⇒ **`N`/`Z`/`R` 等是内建类型名**；④ 语法来源：`docs/SYNTAX.md:546`（`name be <集合或表达式> [: init]`）、`:550`（称 `be` 是「仓库里最不常见的语句形式之一」，**10/346 实证了这句**）、`src/parser/parser.c:1357-1365`（`STMT_BE`）；⑤ **顺带发现一处文档与语法不一致**：`vtest/lint_case_membership_v04.im:2` 等用的是 `be Direction = "N"`（**`=`**），而 `docs/SYNTAX.md:546` 只写了 `:`。**诚实边界（四条，详见 §3.9）**：手工分类而非分析器输出；正则只匹配一种形状（漏别名/下标名/续行，误报未排查）；**「内建 `N` 可解析」是从名字名单读出的、未实跑编译器**确认 `be N` 真在编译期求值；**`type` 声明侧已在同轮补量**（见下一格）。 **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| **[实测]（本会话，2026-10 第十轮补：`type` 声明侧普查，§3.9）** | 命令 `grep -rnE '\btype[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=' --include=*.im .`（全仓，排除 `build/`、`.worktrees/`、`.verify/`）→ **全仓只有 11 处 `type X = …`，全部在 `vtest/` 下**，RHS 形状分布：**字面量枚举 7 处**（`type Direction = "N", "S", "E", "W"` 等）、**字面量区间 2 处**（`type Byte = [0~255]`、`type Positive = [1~100]`）、**具名类型引用 2 处**（`type AppError = FileError` 别名、`type CombinedError = FileError + ParseError` 类型层并集）⇒ **没有任何 `type X = <依赖运行期值的表达式>`**，即 §3.9 原先的诚实边界③**已被补量关闭**。并集的实现能力在 `src/types/typeset.c`（`im_typeset_union`，声明 `src/types/typeset.h:37-45`），但 §3.7 已记它**零消费者** ⇒ 「有实现」与「被使用」在这里是两件事。**新增诚实边界**：「11 处」未排除注释/字符串里的误报；那 2 处具名引用是我**判定**可静态求值，**未实跑**编译器确认。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 门禁已到 **12 个阶段**（新增第 12 阶段 `orphan-fixtures`），`GATE_RC=0`、`gate: OK — every stage passed (12/12 stages ran).`、**`100% tests passed, 0 tests failed out of 136`**、`0 skipped`、`check_text_integrity: 771 text file(s), 0 with NUL bytes.`；`docs/BOARD.md:62` 与 `docs/README.md:55` 的清单已同步为十二个阶段。**我未独立复核**；其中「12 阶段 / 136 测试」与我在 §3.1.8 记的 `EXP_CTEST = 134`（`tools/gate.sh:54`）**不一致，未调和**。 **⚠ 本会话重取（观测点 `main @ 07cfd1d`），四项全变了，每项给出读它的命令** —— ① 阶段数 = **15**（`grep -c '^stage_[a-z_]*()' tools/gate.sh`；门禁自印 `15 stages are registered`）；② `EXP_CTEST` = **153**（`grep -n 'EXP_CTEST=' tools/gate.sh` —— **它今天已不在本行引的那一行**）；③ `check_text_integrity: 946 text file(s), 0 with NUL bytes; 44 suffix(es) + 4 name(s) in the allow-list, 13 of them match nothing today (stated).`（`python3 tools/check_text_integrity.py`）；④ 阶段清单今天在 `docs/BOARD.md` 的「合入 main 前必须全绿。十五个阶段…」那一行（`grep -n '十五个阶段' docs/BOARD.md`）与 `docs/README.md` 里 `tools/gate.sh` 那一行 —— **本行引的那个 `docs/BOARD.md` 行号，今天是 `orphan-fixtures` 那一行**。⇒ **一串「现在是 N」排成链，链上每一格都会独立过期**，所以这里给的是命令；本档不写绝对值（§3.1.8 第 3 点）。 |
| **[实测]（本会话，2026-10 第十一轮：更正一 —— `be` 的 `=`/`:`）** | ① `sed -n '1218,1227p' src/parser/parser.c` → `parse_type_stmt` 里 `consume(p, TOK_EQ, "'='");`（**`type` 认 `=`**）；`sed -n '1383,1391p'` → `be` 分支 `if (match(p, TOK_COLON)) stmt->beStmt.init = parse_expr(p);`（**`be` 只认 `:`**）⇒ **两个语句、两种分隔符**；② 实跑两条程序：`dir be Direction = "N"` → **rc=1**、stderr `Error: expected 'expression', but got '=' (type 83)`；`dir be Direction : "N"` → **rc=0**、stdout `N`；③ `./build/inimerse --no-mods --lint vtest/lint_case_enum_v04.im` → **rc=1**，stdout 只有 `[lint] line 3 [WARN] finite case type 'Direction' is missing members: E, W`，**`grep -c "expected 'expression'"` = 0** ⇒ **`--lint` 通道吞掉解析错误**（§3.9 更正一、§9 第 21 条）。**`=`/`:`` 那一处原文登记为「文档与语法不一致」，结论反了：`docs/SYNTAX.md:546` 对、fixture 错。** **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| **[实测]（本会话，2026-10 第十一轮：集合并集的两个生产点，§3.1.10）** | ① `sed -n '3192,3200p' src/vm/vm.c` → `L_ADD` 的 `if (a->type == VAL_SET \|\| b->type == VAL_SET)` 分支，`:3195` `n = set_union(vm, a->ival, b->ival);`（`set_union` 定义 `:2054`、第二调用点 `:3289`）；② `sed -n '2224,2231p' src/compiler/compiler.c` → `case STMT_TYPE:` 只做 `compile_expr` + `emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0)` ⇒ **RHS 运行期求值**；③ `grep -rn 'im_typeset_union' --include=*.c --include=*.h .`（排除 `build/`、`.worktrees/`）→ 只有 `src/types/typeset.h:41`（声明）、`src/types/typeset.c:56`（定义）、`src/types/typeset_probe.c:19`/`:50`、`src/types/enum_probe.c:91`/`:117` ⇒ **引擎零消费者**；④ 实跑 `type FileError = "not_found","permission_denied"` + `type ParseError = "invalid_syntax","unexpected_token"` + `type CombinedError = FileError + ParseError` + 三个 `say` → `set(2)` / `set(2)` / **`set(4)`**、rc=0 ⇒ **并集确实算出来了，但是在 VM 运行期算的**。 |
| **[转述，来源 `exact-lumen`，非本会话实测]** | 2026-10 第十一轮它给的四条：① **更正一**（10/10 → 8/10，`be` 只认 `:`）——**我已独立复跑确认，见上两行**；② **`OP_POP_REG` 的「生产点」列无法用一次字面 grep 判定**（`selfhost/compiler.im:37` 的 `OP_POP_REG = 29` 是裸常量定义；`emit(comp->curBC, OP_…)` 不是唯一形状，有宏与包装）⇒ **列名降级为「声明点」**（我接受，§3.1.3 已改名）；③ **当时 `EXP_CTEST` = 138**（链路 134 → 136 → 137 `a7d405d` → 138），`tools/gate.sh:54`；**我当时实测确认 `:54` 的值是 138** —— **但该值此后又前进到 139**（`exact-lumen` 新增 CTest `#139 fixture_parse_runtime`；本会话已实测 `sed -n '54p' tools/gate.sh` → `EXP_CTEST="${EXP_CTEST:-139}"`，链路末尾加 139；**本节保留 138 作为当时的转述原文，最新值以 139 为准**）；④ **(A) 的分工**：我写判据、它实现拒绝点，且**判据必须落在编译路径而非 `--lint`**（理由就是上两行的实测）——**已按此写成 §6.3**。 **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| **[实测]（本会话，2026-10 第十二轮：69 行全表 + 一次标题损坏）** | ① **69 个指令体**：`grep -nE '^ *L_[A-Z0-9_]+:' src/vm/vm.c` → 69 个标签、无重复，连续覆盖 **`src/vm/vm.c:3146-4680`**（`OP_RECV` 块尾 `4680`，`4681-4683` 是 `switch` 与函数收尾）；② **枚举顺序**：`awk '/^typedef enum \{/{f=1;next} /^\} OpCode;/{f=0} f' src/compiler/bytecode.h \| grep -oE '\bOP_[A-Z0-9_]+' \| sort -u \| wc -l` → **69**；③ **`regs` 列**：逐块 `grep -o 'ins\.r[123]' \| sort -u`；④ **`raise` 列**：逐块 `grep -c vm_throw` → **非零 16 行 / 零 53 行**。**⚠ 更正（`exact-lumen` 指出、我核实后确认）：原写「与 §3.1.7 的 `can_raise` = 16 一致，互为独立复核」是错的** —— §3.1.7 对 `can_raise` 的判据原文就是「块内出现 `vm_throw`」，与本列**同一个谓词** ⇒ 这是**同一次测量被记了两遍**，只证明转录没错字，**不构成独立验证**；已按此改掉 §3.1.11 结论 3；⑤ **`声明点` 列**（七个文件）：`src/compilation/aot_native.c` 与 `src/vm/jit_mode.c` 对 **全部 69 个 opcode 零命中**；`src/compilation/wasm_backend.c` **只有 1 个**（`OP_LT`，且在 **`:1500` 的注释**里）；`selfhost/*.im` **零命中 20 行**（11 行 `S` 类 + 9 行 `P` 类，清单见 §3.1.11 结论 2）；**⑧ 加强（`exact-lumen` 提议的判据，本会话实测）**：那 11 个 S 类名字**在 `selfhost/` 任何文件里的出现次数都是 0**（`for op in …; do grep -rho "\b$op\b" selfhost/ \| wc -l; done` → 11 个全 **0**，**含字符串字面量与注释**）⇒ 「`.im` 前端不知道它们存在」**逐字成立**（连名字都没被写过），而非「知道名字但不发射」—— 两者的修复成本差一个量级；⇒ **§6 第 2/3 步判据 ① 的起点是「0 / 0 / 1（注释）」三个不同的零**，且该判据必须写明**注释不算生产点**；⑥ **逐行读出的新事实**（此前未记）：`OP_LOADK_I64` 的 `r2`/`r3` 是**无符号半字**、`OP_DECLARE` **不是声明而是写 `vm->limit_*`**、`OP_TRY_END` 只做 `exc_depth--`、`OP_CALL_VALUE` 用 `fprintf` 而非 `vm_throw`、`OP_CONCAT` 块内注释逐字 `Same per-step semantics as L_ADD`、`OP_INDEX_GET` 块内注释逐字 `out-of-range read stays nil (compat)`；⑦ **一次结构性损坏（本档自身，已修）**：插入 §3.1.11 时 `old_string` 取了 `### 3.2 …` **整行标题**而 `new_string` **没有把它带回来** ⇒ **`### 3.2` 标题被删、其下编号列表失去父标题**；**两个文档门禁阶段都 RC=0**（链接检查器不看标题结构），是靠 `grep -n '^### 3\.'` 才发现的。⇒ §9「本文档自身维护的教训」已加第五次记录与两条新规矩。**诚实边界**：`regs` 是上界、`声明点` 是字面 grep（会把注释算进去）、`vm_semantics` 是我读块写的摘要、`类` 是手工判定（四条详见 §3.1.11）。 |
| **[转述，来源 `exact-lumen`，非本会话实测；带本会话实测的更正]** | 2026-10 第十二轮它的四条：① **`EXP_CTEST` 现在是 139**（新增 CTest `#139 fixture_parse_runtime`，注册在末尾、既有 `#N` 一个没动），链路 134 → 136 → 137 `a7d405d` → 138 → **139**；**本会话已实测 `sed -n '54p' tools/gate.sh` → `EXP_CTEST="${EXP_CTEST:-139}"`**，全量门禁实测 `gate: OK — every stage passed (12/12 stages ran).`、`100% tests passed, 0 tests failed out of 139`、`0 skipped`（后三条是它的转述，**我未独立复核**）；② **我的提交 `46682ca` 已由它推上去**（它推 `308cbe9..50216cb` 时一并带上；远端 tip `50216cbceac8e712d92f68d748f1560b01ecc3fb`）；③ **`vtest/sayout.txt` 的写入者找到了**：`vtest/say_pair_probe_v06.im:11` 的 `r = say_file("FILETEXT", "sayout.txt")` —— **写在脚本目录而不是 cwd**（从仓库根跑该 fixture，落点仍是 `vtest/sayout.txt`），未跟踪、**不在 `.gitignore`**，所以任何 `git add -A` 都会把它带进去；它删掉了但会再回来，已报 `vivid-anchor`。**⚠ 处置状态（2026-10，0.5.2 之后；`agent3` 原话、`exact-otter` 转交）**：**已于 0.5.2 之后处置：`.gitignore` 收录 `/vtest/sayout.txt`（`8d28c60`）。写入者是手跑探针 `vtest/say_pair_probe_v06.im:11`，门禁不重新生成它；条目带出处注释，未改 fixture —— 副产物的名字本身是探针的观测量。**（原文记「留到 0.5.2」，**那个期限已过**，故按已处置改写。**⚠ 观测点（刻意不写死现值；我写这句时在 `main @ 58f1a44` 上读得 **0**）**：`8d28c60` 是那一笔 `.gitignore` 的提交（记录：它当时在分支 `stream/sayout-ignore` 上）—— **两个问题各问一条命令，读你自己手上那棵树**：① 条目在不在：`git show <你读的那棵树>:.gitignore | grep -cxF '/vtest/sayout.txt'`，得 **0** 即**尚未收录**、得 **1** 即**已落地**；② 那一笔进没进：`git merge-base --is-ancestor 8d28c60 <你读的那棵树>`，**rc=0 即在、rc=1 即不在**（写这句时 `58f1a44` 上 rc=1、`d27413a` 上 rc=0）；条目本体在 `8d28c60:.gitignore:122`，出处注释在 `:101` 与 `:119`。**数的是条目本体本身，不是提到它的行** —— 原先写 `grep -c sayout` 得 `3`，那 `3` 里有两行是**注释**，任何人往注释里再写一次 `sayout` 它就成了 `4`、「得 `3` 即已落地」当场变假。`-x` 锚整行、`-F` 按字面 ⇒ **一个计数的对象必须是它要证明的那件事，不是它附近的名字**）；④ **两个 lint fixture 的 `=` → `:` 已随 `50216cb` 提交，`--lint` 的输出逐字没变** ⇒ §3.9 更正一里引的那两条 warning 串**仍然有效**。**另**：它确认本轮结账时 `git diff --cached --name-only` 不含 `docs/DECFY_DESIGN.md`，提交全部按路径显式给（`git commit -F msg -- <paths>`），**没有 `git add -A`**；发版冻结期它不再动 `src/**`。 |
| **[实测]（本会话，2026-10 第十二轮补：`EXP_CTEST` 与提交位置）** | ① `sed -n '54p' tools/gate.sh` → **`EXP_CTEST="${EXP_CTEST:-139}"`** ⇒ §3.1.8 与 §10 各处写的 134/136/138 **全部过期，最新是 139**；② `git log --oneline -1` → **`8b8c093 merge: stream/typeset-v06 (the collection type system becomes a v0.6 target)`**；③ `git status --short` 显示工作树里**只有** `M docs/DECFY_DESIGN.md`（⇒ 别人未提交的改动此刻不在工作树，与上一轮不同）。**本会话未跑全量门禁、未跑 ctest**（发版冻结期 + 本档只需两个文档阶段）。 |
| **[转述，来源 `exact-lumen`，非本会话实测；带本会话实测的更正]** | 2026-10 第十三轮它报的六条：① **`EXP_CTEST` 现在是 141**（139 → 140 `substr_boundary_runtime` → 141 `literal_resolve_runtime`），阶段数仍 **12**；**本会话已实测 `sed -n '54p' tools/gate.sh` → `EXP_CTEST="${EXP_CTEST:-141}"`** ⇒ 本档自此**不再写绝对值**（见 §3.1.8 第 3 点）；② **`docs/BOARD.md` 的阶段表曾写 `137 / 137` 而门禁已是 138** ⇒ 已在 §9 第 23 条登记为「文档抄的门禁输出会静默过期」；**本会话实测当前 `docs/BOARD.md:54` 已是 `141 / 141` 与 `0 tests failed out of 141`，与 `tools/gate.sh:54` 一致** ⇒ 那处不一致**已被别人修好**；③ **`find_engine` 实测 20 份拷贝**（`grep -rln 'def find_engine' tools/ \| wc -l` = **20**），异类是 `tools/aot_native_bench.py:68` 的 `def find_engine(build)`（**签名多一个实参**）——**本会话复核全部确认**；④ **`docs/AUDIT.md` 新增 §1.62（`:2785`）与 §1.63（`:2832`）**，`## §1.62` 逐字为 ``substr` 的钳位整型溢出：`start + len` 溢出后守卫恒假` —— **本会话复核确认两节都在**；⑤ 它又用第 22 条绊了一次（锚点写成「对比」、文件里是「比较」，**静默零匹配**）⇒ 已写进 §9 第 22 条；⑥ 它自首**「一边同意先立检查、一边又添了第 20 份 `find_engine` 拷贝」**，并指出这证明**「先立检查再收敛实现」的顺序是对的、理由是纪律不可靠而非有人不听话** —— 我同意并已写进 §9 第 24 条。**它本轮推送的 tip = `f35d209`**（`platform: a literal host no longer pays for a resolver round trip` + `bab466b runtime: a substr bound that overflowed made its own guard false`），**本会话用 `git log`/`git ls-remote` 复核确认**（远端 `refs/heads/stream/builtin-contract-rulings` = `f35d209`；`refs/heads/main` 仍是 `a7d405d`）。**它声明此后不再改任何文件。** |
| **[转述，来源 `exact-lumen`，非本会话实测；其中机制与阈值我要它读码给出]** | 2026-10 第十四轮它报的三件：① **它自首三条负结果、全部由「体量差」驱动、全部错** ⇒ 已落成 §3.1.11 的**第四行（④「体量差不是行为差的证据」）** 与机制命名「**推断冒充测量**」，并落成本档自用规矩「**以长度差为唯一依据的行为主张必须附读码位置或实测**」；② **`match` 的 POSIX 静默截断**（`src/runtime/runtime_posix.c:131` 的 `char translated[2048]`、复制边界 `j + 16 < sizeof translated`、截断串直接进 `regcomp`；**这两个号是记录、不是现状 —— 它们的树是写下本行的 `7ba57aa`**）⇒ 已落成 **§9 第 25 条**；**其对照（N=2028 `false` / N=2030 `true`）与 2031 字节上限的逐字吻合是它给的，本会话未独立复跑**，我只核了 `:131` 与 `:541` 的代码形状（**`7ba57aa` 上逐字**：`:131` = `char translated[2048]; size_t j = 0; int in_class = 0;`、`:541` = `static int posix_core_case(VM *vm, int upper) {`。**★ 对象的下场，今天实测**：`:131` 那个定长缓冲**已删** —— 删它的正是本行的下一笔 `3743b99`（`git log -1 --format=%p 3743b99` ⇒ `7ba57aa`），今天 `:173-180` 是**墓碑注释**、`:182` 是 `size_t tcap = 16 * strlen(pattern) + 1;`、`:183` 是 `malloc`；`:541` 的 `posix_core_case` **没被删，只搬了家**，今天在 `:623`。⇒ **记录用号写、号要绑树；现状用锚写** —— 本行是记录，所以号留着、树补上；把 `:131` 改成 `:173` 会把一句「我核过那段代码」指成「我核过一段注释」。）；③ **它要求我收紧自己写的「不是遗留缺陷」** ⇒ 我采纳并已改（§9 第 23 条）：**修的是那个数，不是那条缺陷**；两个 `141` 是 **① 级一致**（同一个数抄两遍）而**我读成了 ③ 级**——**这正是 §3.1.11 警告的那个高估方向**。**它声明在 `vivid-anchor` 答复前不动 `src`。** |
| **[口径]（2026-10，`exact-otter` 裁定，本会话采纳）** | **本档所有文件行号必须写成 `<ref> @ <sha>` + 行号**，因为同名的文件在两个 ref 上内容可以不同：`CMakeLists.txt` 在 `stream/builtin-contract-rulings @ 056e67b` 上 **1359 行**、守卫 `:1355`；在 `release/051-final @ c71ea00` 上 **1363 行**、`add_test` `:1358`、两条断言 `:1362`/`:1363`（成因已查明：`release/051-final` 多 **4 行注释 + 1 行 `FAIL_`**，而那两行并列承诺注释**两边都在**）。**依据是一条元规则**（`docs/SYNTAX.md` §7.4 H4「一次观测 ≠ 一个性质」）：**行号本身没错，错的是没记口径** —— 删掉行号会让下一个人重新量、且**没有东西告诉他两个 ref 上的行号不一样**。**量法**：`wc -l` 取总行数、`grep -n` 取符号行；**不许从「我看到的最大行号」推**（见 §9 第 28 条）。 |
| **[口径补：`docs/STATUS.md` 的引用必须同时给节号与带观测点的行号]（2026-10，`exact-otter` 裁定，本会话采纳并逐条落地）** | **起因**：`docs/STATUS.md` 是**会生长的账本** —— 它**自己在 §2 的标题里就写着「不写行号，行号会随本档增删漂移」**（`main @ e9debfd` → `:38`）。因此本档对它的引用一律写成 `docs/STATUS.md §<节号>（@ <sha> → :<行>）`：**节号是主键**（稳定、可检索），**行号带观测点**（可定位、且不会假装自己是永久坐标）。 **实测漂移规则**：`ab70a71` → `main @ e9debfd` 在 `docs/STATUS.md @ ab70a71:488` 之后插入 21 行（**该行在两树上同文——都是空行**，它就是这个漂移的锚点，不是一处引用）（`diff` hunk 头 `488a489,509`），此后**整段 `+21`**。**本档原有 15 个被引行号落在漂移区，逐条实测 `ab70a71:N` 与 `main @ e9debfd:N+21` 同文 —— 15/15。** **映射（写作时 `ab70a71` → `@ e9debfd`）**：`2464`→`2485`（§10.42）、`2470`→`2491`（§10.42）、`2489`→`2510`（§10.42）、`2526`→`2547`（§10.42）、`2531`→`2552`（§10.42）、`2536`→`2557`（§10.42）、`2851`→`2872`（§10.48）、`2910`→`2931`（§10.49）、`2922`→`2943`（§10.49）、`3095`→`3116`（§10.53）、`3110`→`3131`（§10.53）、`3130`→`3151`（§10.53）、`3169`→`3190`（§10.53）、`3171`→`3192`（§10.53）、`3236`→`3257`（§10.56）。 **⚠ 不许把 `N` 一律 `+21` 了事**：每一处都要**先验证那一行确实是被引的那句话**（本会话逐条核过内容断言），因为漂移区的行号里混着**不漂移的**（`§3` 那处 `:286` 在两树上**同文、未漂移**），也混着**被引文字已被删掉**的（`§2` 的 `:51`，`28a49f3` 删除了那句自指断言 ⇒ 只能写成 `@ 28a49f3^` 的观测点）。 **★ 一条反例（`ivory-ember` 的判断被 `exact-otter` 更正、本会话复核实测）**：它把 `:3110`/`:3130` 定性成「**从来没对过、不是漂移**」并提议改成 `:3135`。**实测 `ab70a71:3110` 逐字**是「`if s` 认为空串为真，`not s`、`s or x` 与 `bool(s)` 认为它为假。而 `docs/DECFY_DESIGN.md:125` 要求的是…」⇒ **那正是本档 §0.5 说它引的东西** ⇒ `:3110` 是**普通漂移**（`@ e9debfd` → `:3131`）；它提的 `:3135`（`@ e9debfd`）是同一节里的**另一行**（`**修法：唯一入口 `vm_truthy`。**`），**不是节首**。 **教训**：**按内容搜会搜到「一行」匹配，不一定是被引的那一行** —— 这是本节 `[口径]` 的**反面实例**：观测点不是「某处出现过这句话」，而是「这句话当时在这一行」。 |
| **[自检：`docs/STATUS.md` 的引用可以重跑复核]（2026-10，`exact-otter` 要求「一条能重跑出这些处的命令 + 期望输出形状」，本会话落地）** | **不写「我查过 30 处」，写一条能重跑出这 30 处的命令** —— 本会话实测的版本在下面的围栏块里，逐条 `git show <ref>:docs/STATUS.md` 取行。**不变式**：每条引用的**行号非空**，且**它落在它所命名的那个二级节里**（回退找最近的 `^## ` 标题，要求它以 `## <节号>` 开头）。**注意不是**最近的 `^#{2,3} ` 标题：先按后者写，**9/30 误报**，因为 `§10.42` / `§10.49` 下有多层 `###` 子节（`### 与 §10.41 的一处冲突`、`### 先测爆炸半径，再动语义`、`### 三、为什么是匿名 union 而不是加宽字段` …）——**引到子节里的行是对的**，把不变式写成「最近标题就是它」是把子节误判成漂移。**期望输出形状**：一行一条 `ok  §10.42 e9debfd:2485 ## 10.42 把三条…`，末行 `--- 30 citations, 30 ok, 0 bad ---`，退出码 0。**计数更正**：我先前报的 **31 是错的，实际 30**（26 处标准形态 + `§2` 2 处 + `§3` 2 处）；其中 `§10.53` 的 `:3192` 一条被引用 **5 次**、`:2485` 4 次。**它有牙 —— 五条真实反例，各打中一种不同形态**：① 把 `:2485` 退回本档改前的旧值 `:2464` ⇒ `1 bad`，因为该行落进 `## 10.41`（**陈旧行号**）；② 把 `:2557` 换成 `:3257` ⇒ `1 bad`（**行对、节错**）；③ 把 ref 换成不存在的 `deadbee` ⇒ `1 bad`（`git show` 失败、取到空）；④ 行号越过文件尾 `:99999` ⇒ `1 bad`；⑤ 节号写成 `§10.54` ⇒ `1 bad`（**节号差一**）。**③ 第一次跑时它不是报错而是崩了**：回退找标题的生成器在 `k >= len(L)` 时先求值 `L[k]` ⇒ `IndexError`，加了 `min(i, len(L))` 边界才成为一条检查 ——**一个在真实反例上会崩的东西不是检查**（§7 第 4 条的第四个实例，与 `tools/check_orphan_fixtures.py` 那次崩溃同形）。**它自己的边界（必须写明）**：只扫 `docs/DECFY_DESIGN.md` **一个文件**，且只认 `docs/STATUS.md **§X**（`@ <ref>` → `:<N>`）` 这一种形态 ⇒ `docs/STATUS.md` 对**它自己**的引用、以及 `docs/BOARD.md` / `docs/HYGIENE.md` / `docs/HANDOFF_INFIVERSE.md` / `docs/streams/*` 里的引用**都在它视野外**（已派给 agent2）；本档里 `docs/STATUS.md:3851` 那两处**故意**不写成这种形态（§9 第 30 条实例②的**被引原文**）⇒ 既不计入 30、也不会被它检查 —— **这不是漏洞，是范围声明**。 |
| **[转述，来源 `exact-otter`，非本会话实测；其中树/tree 与祖先关系我已独立复核]** | 2026-10 它的三条：① **F3 更正** —— `5cf4aa1` 与 `056e67b` **不是同一棵树**：前者是后者的**祖先**（中间隔着本档自己的 `e2d44e2`），tree 分别是 `f5988c13eca99c1b2c1fbd1335c16d9d68ec660f` 与 `68a1849a7744441da450263bd94822a16904dfed`，`git diff --stat 5cf4aa1 056e67b` **只差 `docs/DECFY_DESIGN.md`（+9/−3）** ⇒ 被引用的 `CMakeLists.txt` 事实**恰好两边相同**（**本会话逐条复核确认**：`git merge-base --is-ancestor 5cf4aa1 056e67b` 为真、两 tree 值如上、`git diff --stat` 输出如上、两 ref 上 `git show <sha>:CMakeLists.txt | wc -l` 都是 **1359**、`:1355` 都是 `add_test(NAME match_long_pattern_runtime`）。⇒ 定性：**「两个不同的观测点被当成一个来引用，而它们恰好给出同一个读数 —— 是巧合，不是性质」**；已按此改写第 25/26 条。② **§9 第 30 条**（写下结论的动作本身让结论失效）：`docs/AUDIT.md` §1.65 与 `docs/STATUS.md **§2**（`@ 28a49f3^` → `:51`；**被引文字已由 `28a49f3` 删除**，`@ e9debfd` 上 `grep -c "Total Tests: 131"` = **0**）` 写「全库 `grep -rn 'Total Tests: 131' docs/ tools/` 零命中」，**合并树上 4 处命中、其中两处是断言行自己**；加重子检 `git show 57ece55^2:docs/AUDIT.md | grep -c 'Total Tests: 131'` = **2** ⇒ **与合并无关**；修法落在 **`28a49f3`**（改成直接断言 + 写明「本条不写『几处』」）。③ **§9 第 31 条**（空绿）：`CMakeLists.txt` 的 `if(WIN32)` ⇒ `src/runtime/runtime.c` **只在 Windows 上编译**，POSIX 编 `src/runtime/runtime_posix.c`；agent3 要改的正是 `runtime.c` ⇒ **Linux 门禁不编译被改的文件、判据在构造上不可能变红**；三个成因 (a) 不编译被改的文件 / (b) 不测那个平台 / (c) 量的是它自己。**②③ 的原始证据（`28a49f3`、`57ece55`、agent3 的报备）本会话未独立复跑，只核了 ①。** |
| **[待复核]** | 无（原两项已补读）。**本轮关闭的**：`docs/SYNTAX.md:546` 的 `=`/`:` 那处**已实跑定案（文档对、fixture 错）**；§3.9 的 10 条 `be` 分类**已更正为 8 可解析 + 2 解析错误**。**仍待复核**：① `docs/SYNTAX.md` 各条 D/M 编号的现状我未逐条复核是否仍成立；② §6.3 的 A1–A4 **一条都还没被实现方跑过**（那是 (A) 落地时的事）；③ `--lint` 吞解析错误的**范围**我只测了 2 个 fixture，**没有普查**其他 `--lint` 消费者（例如 lint 的 `rc=1` 与「有 warning」是否在所有路径上都不可区分）；④ **全文「一致」用法已做一轮普查**（2026-10）：`grep -n '一致'` = **48 处**，其中**只有 8 处是证据主张**，已在 §3.1.11 末尾的分类表里逐条判级（③ 级 3 处、① 级 2 处、①′ 级 1 处、② 级 2 处），其余 40 处是规范句/对照表标题/一致性声明，不属证据主张。**这一轮普查本身是 ② 级**（我一人判级、无第二人复核），且**判级是判断不是测量**；⑤ **`独立` 的分词普查（2026-10，`exact-lumen` 指出我的普查键锁死在一个拼法上）**：`grep -c '独立'` = **37 处**（**改动前**的计数；本表自身引用该词之后升到 **49** ⇒ **再次说明 grep 一个词 ≠ 测量一个形状**），四种义项已在同一张表里分类（轴义四处**已改词为「正交」**、验证义十处**改级不改词**、归属义一处不改、论题本体三处不改）；**六处「我独立复跑确认」判为 ②′ 级**（两读者 + 重跑同一命令，**不是 ③**），行号已列。⇒ **仍待复核**：⑥ **O13（`:989`）的「两个现象是否同根因」我没有跑那条判据** —— `exact-lumen` 提的是「若能指出修好 IR 层之后其中一个现象仍存在，则该句成立」，**我没做，故此处的读法（同根因）是较弱但可辩护的那一半，不是已证事实**。 **⚠ 记录，不是现状：见 §3.9 抬头的记录牌（`be` / `STMT_BE` 已于 2026-10 移除）。** |
| **[实测 + 更正]（本会话，2026-10 第十六轮：一处分节错位、一处同格自相矛盾）** | ① **`⚠ 观测点更正` 段落原先落在 §9 第 25 条的行里**（第 25 条的宾语是 POSIX `match` 截断，而该段讲的是 `CMakeLists.txt` 的 `match_long_pattern_runtime` 块 ⇒ **属于第 26 条**）；**并且第 25、26 两行各自抄了一遍同一组 `CMakeLists.txt` 行数** ⇒ 同一个读数录了两遍、没有任何东西比较它们（§9 第 23 条的病，一行之隔）。已把该段从第 25 条**整段移入第 26 条**，第 25 条只留它自己的登记点。② **第 26 条同一格内自相矛盾**：前文写 `PASS_REGULAR_EXPRESSION`「列表里每一项都必须命中（不是任一命中）」，后文 ⚠⚠ 写「任一命中即通过，是或、不是与」—— 后者才是实测结论（`exact-lumen` 在 `release/051-final @ c71ea00` 上把 `posix_core_match` 改成恒 `push_bool(vm, 0)` 重建后**仍 `100% tests passed`**）。已删掉前文那句并就地标注。**机制**：更正写进了同一格、原文没删 ⇒ 自相矛盾；与 `exact-otter` 刚在 `docs/AUDIT.md` §1.65 自首的「**处方不是执行**」同形，**这次的现场在我这份文件里**。③ 新增 §9 第 32 条（解释违规的文字不得采用违规的形态）。 |
| **[实测]（本会话，2026-10 第十七轮：`CMakeLists.txt` 引用的逐条复核与重取）** | **起因**：`exact-otter` 给三处陈旧引用（`:307`/`:317`/`:1097`），并**要求我自己按内容核、不许照抄它的映射**。**量法**（`pwd` = `/home/sakiko/inimerse/.worktrees/decfy-citations`；`git rev-parse HEAD` = `9ee20c3`）：`git show <sha>:CMakeLists.txt \| sed -n '<N>p'`、`grep -n`、`wc -l`、`git rev-parse <sha>:CMakeLists.txt`。**两个观测点不是同一个 blob**：`main @ e3da33c` = **1412 行 / `7932c50b5d`**；`e9debfd` = **1399 行 / `885cfc180c`**（差 **+13**，来自 `4ca013d` 的 hunk `@@ -1031,8 +1031,21 @@`）⇒ **1031 行之前两树同号、之后整体 +13**。**逐条结论（9 行 / 11 个号）**：① **重取 4 处** —— `:733-743` → **`@ e3da33c:756-766`**（两处：`:317`/`:1097`）、`:744` → **`@ e3da33c:767`**（两处）、`:673` → **`@ e3da33c:690`**（两处：`:506`/`:1072`）；② **补归属 1 处** —— `:1072` 的 `:1352-1353` 属 `@ c71ea00`、`:1359` 属 `@ 056e67b`（原句两个号并排却没写归属，读者分不清）；③ **未漂移 6 个号、只补观测点** —— `:412-436`、`:438-443`、`:417`（×2）、`:157-159`（×2）；④ **记录保留原值** —— `:1355-1359`（`@ 056e67b`）、`:1358-1363`（`@ c71ea00`）、`:1362`/`:1363`（`@ c71ea00`，原文逐字写「`release/051-final` 上的落地逐字」）。**指针 / 记录的判据**（`exact-otter` 的第 1 条硬规矩）：句中若已点名树 ⇒ **记录**，保留原值；若在说「去那里看」⇒ **指针**，必须指向现在并就地标出原文写的号。**历史取证（证明是「陈旧指针」而不是「本来没有这个号」）**：逐树扫 `git log -40 -- CMakeLists.txt`，`add_test(NAME lint_case_try_runtime` 在 `01da596`/`a85cdf8`/`87f57b7`/`3743b99`/`ec23c19`/`bab466b`/`9f7fbad`/`50216cb`/`308cbe9`/`a7d405d`/`3b19013`/`66b12ca`/`e313c0f` 上**全是 `:733`**、`set_tests_properties(posix_runtime_parity PROPERTIES` 全是 `:673`，而在 `c89e077`/`e99f437`/`05c3a3e`/`de5ae8d`/`fe24342` 上变成 **`:756`/`:690`** ⇒ 两个号**曾是正确指针**、随 main 前进（**+23 / +17，无统一偏移量**）变陈旧。**⚠ 更正 `exact-otter` 一处**：它给的 `:307 → 现为 :427-436` **不成立** —— `set(INIMERSE_ENGINE_SOURCES` 在 `66b12ca`/`e313c0f`/`e9debfd`/`e3da33c` 上**都是 `:412`**、WIN32 段末行都是 `:436`、POSIX 段都是 `:438-443` ⇒ 该号**从未漂移**；改成 `:427-436` 会把范围从「base + WIN32 两段」窄化成只剩 `if(WIN32)` 的 `list(APPEND)`，**改变这句话在说的东西**（该句同时点名 POSIX 在 `:438-443`）。**「逐条找过」也是结论**：`docs/DECFY_DESIGN.md` 里 `CMakeLists.txt:<N>` 形态共 9 行 11 个号，**全部在上表内**；**未**扫 `docs/BOARD.md`/`docs/STATUS.md`（`exact-otter` 明说那 14 行归 agent2 的检查器）。**诚实边界**：① 只扫本档一个文件；② 只认 `CMakeLists.txt:<N>` 这种**字面形态**，`CMakeLists.txt` 后接 `@ <sha>:<N>` 的新形态**不在**这次扫描的计数内（它们已自带观测点）；③ 「指针 / 记录」是**判断**不是搜索，每处判断与理由都写在同一行；④ 锚逐条贴过，但**只贴了那一行**，未通读上下文。 |

**§10 自检（可重跑，本会话实测）** —— 在仓库根执行。它做两件事：**数出本档对 `docs/STATUS.md` 的引用条数**，并逐条核对「该行非空」且「该行落在它所命名的二级节里」。

```bash
python3 - <<'PY'
import re, subprocess
pat = re.compile(r'docs/STATUS\.md \*\*(§[0-9.]+)\*\*（`@ ([^`]+)` → `:(\d+)`')
bad = n = 0
for sec, ref, N in pat.findall(open('docs/DECFY_DESIGN.md', encoding='utf-8').read()):
    L = subprocess.run(['git', 'show', f'{ref}:docs/STATUS.md'],
                       capture_output=True, text=True).stdout.split('\n')
    n += 1; i = int(N)
    line = L[i-1] if i <= len(L) else ''
    head = next((L[k] for k in range(min(i, len(L))-1, -1, -1) if L[k].startswith('## ')), '')
    ok = bool(line.strip()) and head.startswith('## ' + sec[1:])
    bad += not ok
    print(('ok  ' if ok else 'BAD '), sec, ref + ':' + str(i), head[:48])
print(f'--- {n} citations, {n-bad} ok, {bad} bad ---')
raise SystemExit(1 if bad else 0)
PY
```

期望末行匹配 `^--- [0-9]+ citations, [0-9]+ ok, 0 bad ---$`，且**同一行里 `citations` 的数 == `ok` 的数**，退出码 0。**`30` 只是我落笔时的读数（观测点：`9826a5c`），不是断言** —— 任何人往本档加一条引用这个数就变；**能断言的是 `ok == citations` 与 `bad == 0`，不是 `30`**（同一族病的第四个实例，就在本档里）。**改了本档的引用而没跑它，就等于没改。**

> **一条独立写在这里的规矩（`exact-lumen` 要求写满，我同意）：`②` 级的限定不会被「再找一个读者」解除。** 它替我抽验了 §1.2 那条「16/16 三后端一致」，判级是对的（③）—— 但它读的是**同一句话**，所以**它那一票本身也是 ② 级**：第二个读者判同样的级，**抓得住「级被判错」，抓不住「谓词选错」**。⇒ **要把一处从 ② 升到 ③，唯一的办法是换谓词或换产物**（对 §1.2 那条就是去核 `docs/STATUS.md` §10.42 的原始记录，或自己重跑那 16 条用例）—— **它一个都没做，我也一个都没做**。⇒ 所以「已请第二人抽验」**不等于**「已升到 ③」；本档任何地方都不应这样读。

### 关于 Lead 的「wasm 与 AOT 一致」

据 **[读码]**，二者在 `%` 的 **int%int** 路径上确实同为 64 位（`src/compilation/aot_native.c:208-211` 与 `src/compilation/wasm_backend.c:938`）。但：
1. 这**只是读码，不是实测**；
2. wasm 的 **general** 路径（`:940-961`）反而先截断到 i32，**与 AOT 不一致**。

⇒ 准确的表述是「wasm 与 AOT 在 `%` 的 int%int 路径上一致」，而不是「wasm 与 AOT 一致」。
