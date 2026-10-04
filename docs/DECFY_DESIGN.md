# 底层去C化设计（DECFY_DESIGN）

> 工作订单：task-16。状态：**设计（未实现的部分仍未实现）**——但本设计交付后已被实现方当作规格引用并落地多节，见 **§0.5 落地现状**。本文档只做设计，不改任何引擎源码。
> 证据标注约定：**[读码]** = 逐行读过该文件该行；**[实测]** = 本会话实际执行命令所得；**[转述]** = 引用 `docs/AUDIT.md` 的实测数字，非本会话实测；**[待复核]** = 只见引用、未见原文。

## 0. 结论摘要

**「用 `.im` 重写」不是修法。** 本题的难点不是「哪个文件是 C」，而是同一条语言语义在仓库里有多个独立决定点，且这个分裂横切实现语言。

最硬的证据：`selfhost/compiler.im:254-266` **[读码]** 与 `selfhost/eval.im:114-123` **[读码]** 都是 `.im` 写的，却对 `and`/`or` 给出相反答案（前者值语义，后者布尔）。⇒ 「换成 `.im`」本身不消除分歧，只是改变分歧发生在哪两个文件之间。

真正的修法是让所有后端消费**同一个 IR（字节码）**，把语义从「每个消费者各自重判一次」收敛成**一张表**，使分歧在结构上不可表达。

---

## 0.5 落地现状（截至 HEAD `3484a47`，2026-10 复核）

本节由 decfy-design 会话在 2026-10 复核 `docs/STATUS.md` 后补写。**原文（§1–§10）保留原样，作为当时的取证记录**；凡与本节现状表冲突处，**以现状表为准**（现状表锚定 commit 与 `docs/STATUS.md` 小节，可逐条复核）。

| 本文档的原论断 | 现状 | 证据 |
| --- | --- | --- |
| §1.1 `and`/`or` 六个决定点、2 值 : 4 布尔 | **已修**：统一为**布尔**语义（短路保留），判据换成三后端逐格比对 | `docs/STATUS.md:2464` §10.42；`src/compiler/compiler.c:648-672`、`selfhost/compiler.im:254-268`、wasm 改为复用 `cg_cond`；AOT 本就正确、未动 |
| §1.1 的语义方向：本文档主张**值语义胜出**（O0 选项①） | **已被用户裁定推翻**：`and`/`or` 返回**布尔** | `docs/STATUS.md:2526-2531`（该节明写「与本节冲突时以本节为准」）；用户裁定见 `docs/STATUS.md:2470` |
| §1.2 `%` 三决定点、32 位截断 | **已修**：`im_dbl_to_i64()` 饱和、零检查前移、`y == -1` 特判、AOT 新增 `nv_die_division_by_zero()`、wasm 改用 `e_trunc_sat_i64`；16/16 三后端一致 | `docs/STATUS.md:2464` §10.42 |
| §1.3 宽度契约分裂（`Value` 整数槽 32 位 vs AOT `NV` 64 位） | **已修，且正是按本文档 §2 的约束修的**：用**匿名 union** 把 `ival` 改 `long long` 而 `sizeof(Value)` 仍为 **32**，AOT 的 `NV` 完全未动 | `docs/STATUS.md:2910` §10.49（该节直接引用 `docs/DECFY_DESIGN.md:76` 当约束） |
| §1.4 死指令 `OP_AND` / `OP_OR` | **已定**：`and`/`or` 降级为短路跳转 + 真值常量；另**新增 `OP_LOADK_I64`** ⇒ **opcode 表已变，本文档对 `src/compiler/bytecode.h:12` 的行号引用需重核** | `docs/STATUS.md:3095` §10.53；`docs/STATUS.md:2935`（新 opcode 追加在枚举末尾以保旧编号与 DLL ABI） |
| §3.3 第 2 条「真值产生点唯一」 | **已实现**：`vm_truthy()` 单入口，六处调用点全部改调用它，旧三目链 `grep -c` 归零 | `docs/STATUS.md:3110-3128`（该节直接引用本文档 `:125` 当规格、`:24` 当真值规则） |
| §3「IR 收敛」（后端改吃字节码） | **未做，仍是结构性下一步**：现状是「把语义判断抽成单一函数」——**分歧被消除，但每个后端各自遍历 AST 的结构成因仍在** | 本文档 §3；对照 `docs/STATUS.md:3171` 诚实边界①（两个编译后端根本走不到字符串/容器的真值） |

**两条复核后新增、对本设计有利的证据**（原文没有）：

1. **本文档已被实现方当规格引用。** `docs/STATUS.md:3110` 引用 `docs/DECFY_DESIGN.md:125` 的「真值产生点唯一」作为该节要达成的判据；`docs/STATUS.md:3130` 引用 `:24` 的三目链尾句作为选定语义的理由；`docs/STATUS.md:2922` 引用 `:76` 的 32 字节冻结作为「选匿名 union 而非加宽字段」的理由。⇒ 设计文档写下的**约束是可被执行的**，不只是描述。
2. **§1.1 的论点被实测加强，而不是削弱。** `docs/STATUS.md:2531` 原文：「修 `and`/`or` 要**同时**动 C 编译器、`.im` 编译器、wasm 后端**三处**，而 `selfhost/eval.im`（`.im`·布尔）**不需要动**…**决定点的数量就是修复要碰的文件数**。」这正是 §1.1 的论点。

**仍未做**：§3 的 IR 收敛；以及 `docs/STATUS.md:2536-2537` 记的一条诚实边界（O2 的 BigInt / 小整数快路径未做）仍成立。§10.42 当时记的「`tools/im_diff_fuzz.py` 未接门禁」**已被 `docs/STATUS.md:2851` §10.48 解决**（差分模糊测试已进门禁，判据为 `0 DIVERGE / 0 THREW / 0 untranslated`，见 `docs/STATUS.md:3169` 的门禁逐阶段输出）。

---

## 1. 核心问题：同一语义的多个独立决定点

### 1.1 `and` / `or`：六个决定点，横切 C 与 `.im` **[读码，全部逐行读过]**

| # | 位置 | 语言 | 语义 | 机制 |
|---|---|---|---|---|
| 1 | `src/compiler/compiler.c:648-670` | C | **值** | `OP_JUMP_IF_FALSE`（and）/ `OP_JUMP_IF_TRUE`（or）+ `OP_MOV result, right`；跳转目标回填 `comp->curBC->code[jmp_pos].r2 = end` |
| 2 | `selfhost/compiler.im:254-266` | `.im` | **值** | 与 #1 同形：`jpos = len(ctx["code"])`，`ctx["code"][jpos][2] = end` |
| 3 | `src/vm/vm.c:3166-3172`（`L_AND`）/ `:3173-3179`（`L_OR`） | C | **布尔** | `value_set(&R[ins.r1], VAL_BOOL, (a && b) ? 1 : 0, …)`；真值函数是三目链 `(va.type == VAL_BOOL) ? (va.ival != 0) : … : (va.type == VAL_NIL) ? 0 : 1` |
| 4 | `src/compilation/aot_native.c:334-339` | C | **布尔** | `buf_str(b, "nv_boo(nv_tru(")` … `op == TOK_AND ? ") && nv_tru(" : ") || nv_tru("` … `buf_str(b, "))")` |
| 5 | `src/compilation/wasm_backend.c:769-776`（AND）/ `:778-786`（OR） | C | **布尔** | `cg_cond()` 递归：AND 为 `W_IF` 左→右→`W_ELSE`+`e_i32c(0)`→`W_END`；OR 为左→`W_IF`+`e_i32c(1)`→`W_ELSE`→右→`W_END` |
| 6 | `selfhost/eval.im:114-118`（and）/ `:119-123`（or） | `.im` | **布尔** | `if !truthy(l) { return false }` / `if truthy(l) { return true }`；成功路径 `return truthy(eval_expr(e["r"], env))` |

⇒ **2 值 : 4 布尔，横切两种语言。** 且**同语言内部同样分歧**：C 侧 `compiler.c`（值）对 `vm.c`/`aot_native.c`/`wasm_backend.c`（布尔）；`.im` 侧 `compiler.im`（值）对 `eval.im`（布尔）。

**本仓现有实测（[转述]，`docs/AUDIT.md` §1.6「三通道差分实测」）**：`and`/`or` 三通道 **8/8 分歧、三种行为**——解释器给操作数（`1 and 5` → `5`）、AOT 给布尔（→ `true`）、**wasm 在条件之外直接拒绝编译**（`error: wasm MVP subset: 'and'/'or' outside a condition is not supported (use it in if/while) (line 1)`）。⇒ 上表的「值 / 布尔」二分之外还有第三种行为：**拒答**（wasm 的 `cg_cond` 只在条件位置工作）。

**注释不可作为语义证据。** `src/compilation/aot_native.c:334-339` 的注释原文声称 "C's && and || short-circuit exactly as the interpreter's do" **[读码]** —— 该注释与解释器行为**相反**，是错的。

### 1.2 `%`：三个决定点

| 位置 | 语言 | 语义 | 判定 |
|---|---|---|---|
| `src/vm/vm.c:3824-3838`（`L_MOD`） | C | 两条路径：`:3827` int 快路径 `(int)da % bi`；`:3830-3837` 一般路径把 double 转 int 后再取模，**零检查在截断之后** | **坏**：只有 32 位可用；越界时 `cvttsd2si` 给 `INT_MIN`（实测 `3000000000 % 7` → `-2`，见 `docs/AUDIT.md` §1.6） |
| `src/compilation/aot_native.c:208-211` | C | `long long x = nv_asi(a), y = nv_asi(b); return nv_int(y ? x % y : 0);` | **对**：64 位 |
| `src/compilation/wasm_backend.c:938` / `:960` | C | `W_I64_REM_S` | **对**：64 位（int%int 路径 `:926-939`；general 路径 `:940-961` 反而先 `e_trunc_sat_i32` 再 `W_I64_EXTEND_I32_S`） |

**文件头注释自相矛盾 [读码]**：`src/compilation/wasm_backend.c:9` 写 `L_MOD: int%int, else (int)as_double(a) % (int)as_double(b)`——描述的是**坏的那个形状**，而它自己的代码在 `:938` 用的是 64 位 `W_I64_REM_S`；同文件 `:6` 还声称 "mirror the C VM exactly"。

### 1.3 比 `%` 更广的根因：宽度契约分裂 **[读码]**

- `src/vm/vm.h:24-26`：`typedef struct { int type; int ival; double fval; char *sval; void *ptr; } Value;` ⇒ LP64 下 **32 字节**，整数载荷 **32 位 `int`**。
- `src/compilation/aot_native.c:172`（`kPreamble` 内）：`typedef struct { int t; long long i; double f; } NV;` ⇒ AOT 整数 **64 位 `long long`**（`nv_asi` 在 `:182` 返回 `long long`）。

⇒ **VM 32 位 vs AOT 64 位是系统性分裂，`%` 只是它的一个可观测面。** `AUDIT.md` §1.2 的 int32→double 提升同源。

### 1.4 死指令 `OP_AND` / `OP_OR` **[读码]**

- 声明：`src/compiler/bytecode.h:12` `OP_AND, OP_OR,`。
- `OP_OR` 全仓引用**仅三处**：`bytecode.h:12`、`src/vm/vm.c:2826 case OP_OR: goto L_OR;`、`src/vm/vm.c:4873` 反汇编器 ⇒ **`src/` 下零 emit**。
- `OP_AND` 引用四处：`bytecode.h:12`、`vm.c:2825`、`vm.c:4872`、以及**唯一 emit 点** `src/compiler/compiler.c:827` `else emit(comp->curBC, OP_AND, result, result, cmp);`——位于 `EXPR_CHAIN_COMPARE` 分支（`:815-835`），即链式比较 `1 < x < 10`，那里**需要布尔**。

⇒ **结论：`OP_AND`/`OP_OR` 作为内部布尔原语保留；而 `and`/`or` 运算符必须降级为「短路跳转 + `OP_MOV`」。** 这两件事是两个决定，不要合并。

---

## 2. (a) 层次划分表

判定取值：**可搬** / **暂不可搬** / **永久不可搬**。每条给出理由，不写裸结论。

| 层 | 位置 | 判定 | 理由 |
|---|---|---|---|
| 词法 / 语法 | `selfhost/lexer.im`、`selfhost/parser.im` | **可搬（已搬）** | 已是 `.im`；`selfhost/` 已有 48 个 `.im`、2,316 行，前端已搬一半。 |
| AST → 字节码 | `src/compiler/compiler.c` / `selfhost/compiler.im` | **可搬** | 两个实现都已存在，且**语义已一致（均为值语义）**。要搬的是「决定保留哪一个」，不是「再写一遍」。 |
| AST → 值（求值器） | `selfhost/eval.im` | **可搬，但建议废止** | 它是**第三个**独立语义决定点，且与 `selfhost/compiler.im` 相反（`:114-123` 布尔 vs `:254-266` 值）。留着它 = 留着分歧。 |
| AST → C / AST → wasm | `src/compilation/aot_native.c`（`EXPR_BINARY` 在 `:332-351`）、`src/compilation/wasm_backend.c`（`cg_cond` 在 `:766-789`） | **可搬（改为吃字节码）** | 二者都是 `Expr*` 进、各自重判语义，是分歧的**结构成因**。 |
| VM 解释器本体 | `src/vm/vm.c`（5007 行） | **暂不可搬** | substrate：`.im` 的一切执行都由它提供。**最强理由：它同时是唯一的引导运行器**（见第四类）——搬它需要已有东西能跑它，构成循环依赖。 |
| 平台 / OS 层 | `src/platform/`、`src/runtime/runtime_posix.c`、`src/common/` | **暂不可搬** | socket / 线程 / mutex / fork 是 `.im` 无权直接表达的宿主能力；搬 = 先造 FFI，而 FFI 本身又要 substrate 托底。 |
| AOT 最后一跳 | `src/compilation/aot_native.c:168`（`kPreamble`）、宿主 `cc`、`NV`/`nv_*` 前导 | **暂不可搬** | 它产出的是**宿主 C 源码**并交给 `cc` 编译、链接出 `main`；`.im` 既不能代替 `cc`，也不能在没有 C 运行时的前提下提供进程入口。 |
| 引导（bootstrap）链 | C VM ← `.im` 编译器 | **暂不可搬** | 没有 C VM 就没有运行 `.im` 的东西。**这是唯一一条无法靠「重写」消除的依赖**，它决定了去C化有理论上界。 |
| 值表示 + 集合运行时 | `src/vm/vm.h:24-26`（`Value`，32 字节）、数组/字典/集合、以及集合化所需的 BigInt / 窄化整数 / 位图 | **永久不可搬（宽度契约部分）**——**§0.5 复核：该约束已被遵守**（整数槽改 64 位而 `sizeof` 仍 32，AOT 的 `NV` 未动） | `Value` 是 VM 寄存器、AOT 生成的 C、wasm 线性内存槽、固定导入表**共同的形状**；`docs/archive/ROADMAP_3.1.md:23-25` 明确要引入 BigInt 与 u8/i16 窄化，并规定「动态容器与跨模块边界保留 boxed `Value`」；`:28` 要求 BigInt / 窄化整数 / 枚举值在模块 ABI 与 `.vverse` 序列化中**可逆映射**。宽度契约一旦可被 `.im` 单方面修改，ABI 与序列化可逆性即失效。**可搬的是其上的策略，不是它本身。** |

**一处必须点名的误判**：把 `src/vm/vm.c:3166-3179` 的布尔语义归入「永久不可搬」是**错的**。那段是**政策**（policy），不是 substrate **能力**（capability）。政策必须上移到语义表，否则去C化会把缺陷一起固化。

---

## 3. (b) IR 收敛方案

### 3.1 接口草图（草图，非现状）

现状：五个消费者都是 `Expr*` 进、各自判断——

- `src/compiler/compiler.c`：AST → 字节码
- `src/compilation/aot_native.c`：AST → C
- `src/compilation/wasm_backend.c`：AST → wasm
- `selfhost/compiler.im`：AST → 字节码
- `selfhost/eval.im`：AST → 值

目标接口：

```c
/* 形状取自 src/vm/vm.c 的 ins.r1 / ins.r2 / ins.r3 与 src/compiler/compiler.c 的 code[jmp_pos].r2 回填 */
typedef struct { OpCode op; int r1, r2, r3; } Instr;

typedef struct {
    Instr *code;
    int    count;
    int    ntemps;
    /* …常量池… */
} Bytecode;

/* 现状（每个后端各写一份） */
void emit_expr(Ctx *c, const Expr *e);

/* 目标：C 与 wasm 两个后端共用的唯一入口 */
void emit_from_bytecode(Ctx *c, const Bytecode *bc);
```

把 AOT / wasm 改成字节码进之后，`and`/`or` 在字节码里已经定死（原文写「值语义 + `OP_MOV`」；**语义方向已由用户裁定改为布尔**，见 §0.5，`OP_MOV` 那半句仍成立），后端只剩 `opcode → 目标指令` 的映射表，**分歧在结构上不可能发生**。

### 3.2 `aot_native.c` 具体要改什么

1. 删除 `EXPR_BINARY` 分支里对 `TOK_AND` / `TOK_OR` 的特判与 `:334-339` 的 `nv_boo(nv_tru(...) && ...)` 发射。
2. 改为仅按 `Instr.op` 查表发射。
3. **保留** `NV` / `nv_*` 前导（`:172`、`:181-183`）——那是宿主 C 的运行时，属第 3 类「暂不可搬」。

### 3.3 `wasm_backend.c` 具体要改什么

1. 删除 `cg_cond()`（`:766-789`）里 AND / OR 的递归（`:769-786`）。
2. `OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，真值产生点唯一。
3. 保留固定导入表（`:13-15`）：`env.im_print_int(i64)` / `im_print_float(f64)` / `im_print_bool(i32)` / `im_print_nil()` / `im_error(i32)`。

### 3.4 语义表如何收敛成唯一一张

一张 `OpCode → { VM 行为, AOT 行为, wasm 行为 }` 表，作为唯一真值源。三列不一致即构建失败。这张表就是 O0 那个「先做决定」的决定的**落点**：决定一旦写进表，就不再有第二处可以重新决定它。

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
- 固定导入表里的原语。wasm 侧的边界已经写死 **[读码]**：`src/compilation/wasm_backend.c:13-15` 的 `env.im_print_int(i64)` / `im_print_float(f64)` / `im_print_bool(i32)` / `im_print_nil()` / `im_error(i32)`。

**边界怎么定**：跨边界只传**已定宽的标量**，绝不过 `Value*`。今天 `Value` 是 32 字节、整数载荷 32 位（`src/vm/vm.h:24-26` **[读码]**），而 AOT 的 `NV` 整数是 64 位（`src/compilation/aot_native.c:172` **[读码]**）——**边界两侧宽度不同，这本身就是 ABI 缺陷**，比 `%` 更广。

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
| **1** | 两个编译器统一 `and`/`or` 降级（短路跳转 + `OP_MOV`）；顺带定夺 `OP_OR` 死指令去留 | `tools/selfhost_compare.py` 的 opcode 序列判据在 `or` 探针上**零差异**（AUDIT §6 的 `orv.im` 探针） |
| **2** | `src/compilation/aot_native.c` 改吃字节码（§3.2） | `tools/aot_native.test.py` 的 `DIVERGENCE` / `EQUIVALENCE` 计数**不变**（零回归）；且 and/or 类不再需要 `DIVERGENCE` 钉死 |
| **3** | `src/compilation/wasm_backend.c` 同改（§3.3） | 同第 2 步判据，跑 wasm 目标 |
| **4** | `%` 与宽度契约统一（§1.2 / §1.3） | `2147483648 % 7` 等 **四组**预测值在**五条通道**给出同一整数（`docs/AUDIT.md` §1.1 已备好该四组值；五条通道见同文件 §2.1） |
| **5** | 回头重新审视 `tools/aot_native.test.py` 里三条 `DIVERGENCE` 钉死项（`AUDIT.md` §5 末尾明写要求） | 三条中与 §1.1/§1.2 同源者可升为 `EQUIVALENCE`；升不了的必须写明为何**不是**同源 |

**顺序是先决关系，不是偏好**：第 0 步不落地，第 1 步就没有正确目标（会照着错的语义表统一）；第 1 步不做，第 2/3 步改完后 `and`/`or` 仍会与解释器不一致。

---

## 7. (f) 风险与「不做什么」

### 不做

1. **不把 `src/vm/vm.c`（5007 行）重写成 `.im`**。它是 substrate，且是唯一的引导运行器（第 1 类 + 第 4 类）。这是本设计明确的**上界**。
2. **不做 NaN-boxing / 特化 `Value`**。`docs/AUDIT.md` §5 已明确列为「明确不值得先做」。
3. **不只改其中一路的 `and`/`or`**。改一路会把分歧从「三处不一致」变成「两处不一致」，不减少可观测缺陷。
4. **不把注释当语义证据**。`src/compilation/aot_native.c:334-339` 的注释与行为相反；`src/compilation/wasm_backend.c:6` 声称 "mirror the C VM exactly" 却与 `:938` 冲突。这类「注释与代码互相担保」正是分歧能长期存活的原因。
5. **不在本设计内改动门禁脚本**（`tools/**` 不属本文档写域）。

### 风险

| 风险 | 机制 | 缓解 |
|---|---|---|
| IR 收敛改变寄存器分配 → 字节级 parity 破裂 | 后端改吃字节码后寄存器编号来源改变 | 判据用 **opcode 序列**（`ops_only()`）而非字节相同（§4.2） |
| `Value` 宽度统一会波及 ABI / 序列化 | `ROADMAP_3.1.md:28` 的可逆性要求 | 宽度变更必须与 BigInt / 窄化设计同批，且带 ABI 版本号 |
| 删 `OP_AND` 会破坏链式比较 | `src/compiler/compiler.c:827` 是唯一 emit 点且语义需要布尔 | `OP_AND` **保留**；只有 `OP_OR` 可删（§1.4） |
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
| 2 | `src/compilation/aot_native.c:334-339` | 删除布尔发射，改按字节码查表 | §1.1 #4、§3.2 |
| 3 | `src/compilation/wasm_backend.c:769-786` | 删除 `cg_cond` 的 AND/OR 递归 | §1.1 #5、§3.3 |
| 4 | `src/vm/vm.c:3166-3179` | `L_AND` / `L_OR` 语义随唯一语义表定夺 | §1.1 #3、§2 的误判点名 |
| 5 | `src/compiler/bytecode.h:12` | `OP_OR` 死指令去留（`OP_AND` 必须保留） | §1.4 |
| 6 | `src/vm/vm.h:24-26` vs `src/compilation/aot_native.c:172` | ~~宽度契约统一（32 位 vs 64 位）~~ → **已解决**：匿名 union 使 `ival` 为 64 位而 `sizeof(Value)` 仍 32（§0.5） | §1.3、§5 |
| 7 | `src/compilation/wasm_backend.c:9` | 修正与代码矛盾的 `L_MOD` 描述 | §1.2 |
| 8 | `docs/API.md:90` | 写明 `and`/`or` 返回操作数还是布尔 —— **已裁定为布尔**（§0.5） | AUDIT §5 O0 |

---

## 10. 证据诚实度总表

| 类别 | 内容 |
|---|---|
| **[读码]（本会话逐行读过）** | 六个 `and`/`or` 决定点全部行号；三个 `%` 决定点；`OP_AND`/`OP_OR` 全部引用点；`Value` 与 `NV` 的字段与宽度；`src/compilation/wasm_backend.c:1-16` 头注释与其 `:938` 的冲突；`src/compiler/compiler.c:815-835` 链式比较；`tools/selfhost_compare.py:87-96` 的 `ops_only()` |
| **[实测]（本会话执行命令）** | **无。** 本会话未构建、未跑 `tools/im_diff_fuzz.py`、未跑任何执行通道。 |
| **[转述]（引用 AUDIT，非本会话实测）** | 150 例 fuzz → 41/150 = 27.3% 分歧（28 例 and/or、13 例 int32/`%`、0 例无法归因）；`%` 四组预测值；`(2147483647 + 1).type == float`；解释器 RSS 68.5 MB vs 原生 23.2 MB；AOT 快 7.7×–146.6×，`fib` 比手写 C++ 慢 14× |
| **[读码，本轮补上]** | `docs/archive/RELEASE_0.5.0.md:12/23-28/48/102-106` 的 Native ABI 面、C ABI 类型映射、ABI 版本号、以及「三个后端共享同一字节码格式」这条与现状冲突的承诺；`docs/SYNTAX.md:551-556` 的 §7 分类（危险·静默 / 危险·误导 / 冗余 / 卫生），`:901` 「退出码经常区分不出对错，必须断言输出数值」 |
| **[待复核]** | 无（原两项已补读；`docs/SYNTAX.md` 各条 D/M 编号的现状我未逐条复核是否仍成立） |

### 关于 Lead 的「wasm 与 AOT 一致」

据 **[读码]**，二者在 `%` 的 **int%int** 路径上确实同为 64 位（`src/compilation/aot_native.c:208-211` 与 `src/compilation/wasm_backend.c:938`）。但：
1. 这**只是读码，不是实测**；
2. wasm 的 **general** 路径（`:940-961`）反而先截断到 i32，**与 AOT 不一致**。

⇒ 准确的表述是「wasm 与 AOT 在 `%` 的 int%int 路径上一致」，而不是「wasm 与 AOT 一致」。
