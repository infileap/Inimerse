# v0.6 决定点清单（覆盖 `docs/PLAN_V06.md` §3.1 与 §3.2）

**这一件要答的问题（判据逐字，`exact-otter` 派单）：**

> **「同一条语义有几个决定点」这个数，只有在你能指出每个决定点被谁消费时才算量过；一个只有名字、没有消费者的决定点，是候选不是决定点。**

**观测点**：本文件全部读数取自 **`f6e22f5`**（本支 `stream/v06-decision-points` 的基树，也就是 `docs/PLAN_V06.md` 落盘那一笔）。**行号一律相对 `f6e22f5`**；行号会漂，所以每条的主键是**符号名**，复现靠 `git grep -n <symbol>`，不是靠记住数字。

**读法**：`git grep -n <term> -- <path>` 找**生产点**（谁发射、谁判定），然后**逐处读调用者**（`git grep -n <symbol> -- src/ tools/ mods/ selfhost/`）确认它有没有消费者。**不按出现次数判定**。

**三分（本文件用的定义，先写在前面以免歧义）：**

| 标记 | 判据 |
| --- | --- |
| **已收敛** | 这条语义的判定只有一处，且每个消费者都经过它；有测试盯着。 |
| **部分收敛** | 各处**今天给出相同答案**，但判定写在 ≥2 处（改这条语义必须改 ≥2 个文件）；或部分消费者共享一处、其余各判各的。 |
| **未收敛** | 各处**今天答案不同**；或某个决定点没有消费者（**候选**）；或没有测试盯着。 |

---

## 0. 两条读法教训（都是本会话实测，不是转述）

**（1）出现次数 ≥ 3 不证明可达。** `bytecode_check_compatible` 在仓库里有 3 次出现（声明 `src/compiler/bytecode.h:148`、定义 `src/compiler/bytecode.c:726`、调用 `src/compiler/bytecode.c:717`），而那一处调用在 `bytecode_read_file_compat` 体内 —— 本会话复核：`bytecode_read_file_compat` 全仓只有**声明 + 定义**两处（`src/compiler/bytecode.h:147`、`src/compiler/bytecode.c:716`），**没有入口**。⇒ 一对互相支撑、但没人调的符号。这一条已由 `docs/AUDIT.md:4324` 记为判据，此处只作为读法依据引用。

**（2）一个用过滤器得到的「无」，只和那个过滤器一样可靠。** 我第一次找「`in` 运算符有没有测试」时用了 `grep -v 'for \|case \|_ in '`，结果为空，我差点写成「运算符形态的 `in` 没有测试」。**那个过滤器吃掉了答案**：`300 in x.range` 里含 `0 in `，正好命中 `_ in `。改成分形态枚举后，运算符形态的 `in` 有测试（见 §4）。⇒ 报「无」之前，先证明过滤器没有把它删掉。

---

## 1. 总表

| 语义 | 今天有几个决定点 | 分别在哪个文件（`f6e22f5`） | 各自被谁消费 | 有没有测试盯着 |
| --- | --- | --- | --- | --- |
| `and` / `or` | **7**（答案今天一致） | `src/parser/parser.c:714-725`；`src/compiler/compiler.c:657-689`；`src/vm/vm.c:3088-3089` + `:3508` / `:3515`；`src/compilation/aot_native.c:420-425`；`src/compilation/wasm_backend.c:780-800` + `:1317`；`selfhost/compiler.im:254-258`；`selfhost/eval.im:114-119` | 解析器→编译器；编译器→VM；AOT/wasm 各自从 AST 生成；selfhost 工具链 | 有：`logic_semantics_regression`（三后端）。selfhost 那两处**无** |
| `%` | **6**（答案今天一致） | `src/parser/parser.c:653`；`src/compiler/compiler.c:818`（→`OP_MOD`）；`src/vm/vm.c:4161`（`L_MOD`）；`src/compilation/aot_native.c:246-251`（`nv_mod`）；`src/compilation/wasm_backend.c:994-1012`；`selfhost/eval.im:100` | 同上 | 有：`mod_semantics_regression`（三后端，16 格）。selfhost **无** |
| 真值 | **5**（VM 内已收敛为 1） | `src/vm/vm.c:347`（`vm_truthy`）；`src/compilation/aot_native.c:193`（`nv_tru`）；`src/compilation/wasm_backend.c:777`（`cg_cond`）；`selfhost/eval.im:21`（`truthy`）；`selfhost/compiler.im:116`（`truthy`） | VM 全部判定点 + `src/runtime/runtime.c:81`、`src/runtime/runtime_posix.c:100`；AOT 生成代码的 `if`/`while`/`not`/`and`/`or`；wasm 的 `cg_cond` 调用点；selfhost 两侧 | 有：`truthiness_single_point_runtime`（只盯 VM）+ `logic_semantics_regression`（三后端）。selfhost **无** |
| `in`（成员） | **1 个活入口**（内部按右操作数类型分派到 2 个判定）**+ 2 个候选** | 活：`src/vm/vm.c:2293`（`set_contains_or_subset`）→ `:1932`（`set_contains`）/ `:2252`（`set_subset`）；候选：`src/types/typeset.c:105`、`:132` | 活：`OP_IN`，由 `src/compiler/compiler.c:721-727`、`:1714`、`:1749` 发射；候选：只有 `src/types/*_probe.c` | 有：`collection_comprehension_runtime`、`case_collection_patterns_runtime`、`range_metadata_probe` 系列（见 §4） |
| 子集 | **2**（1 活、1 候选） | 活：`src/vm/vm.c:2252`（`set_subset`）；候选：`src/types/typeset.c:132`（`im_typeset_subset`）。**同病还有并集**：`set_union`（`src/vm/vm.c`）对 `im_typeset_union`（`src/types/typeset.c`） | 活：`A in B`（两个集合）经 `L_IN`；候选：`src/types/typeset_probe.c:36-38` | 活：`collection_core_runtime` 一族；候选：`typeset_probe`（它同时是**唯一的消费者**） |
| `be` 约束绑定 | **1 个检查点 + 2 个声明形状** | 检查点：`src/vm/vm.c:4262`（`L_BIND`，判定在 `:4282`）；形状 1：`src/parser/parser.c:1411-1419` → `src/compiler/compiler.c:2266-2273`（`OP_BIND`）；形状 2：`src/compiler/compiler.c:2256-2264`（`STMT_TYPE` → `OP_STORE_GLOBAL`，**不校验**） | 形状 1：`name: 集合 [= 初值]`，经 `OP_BIND`；形状 2：`type Name = 集合`，只存全局 | 有：`bind_init_overflow_runtime`（钉住消息 `initial value out of range`）；`be_removed_{decl,assign,bare}_runtime` 盯的是**拼写被移除** |

---

## 2. `and` / `or`：7 个决定点，答案今天一致

**七个点（`git grep -n 'TOK_AND\|TOK_OR\|OP_AND\|OP_OR'`）：**

1. **`src/parser/parser.c:714-725`** —— 解析：`and` 比 `or` 绑得紧、两者左结合（`while (match(p, TOK_AND))` / `while (match(p, TOK_OR))` 的两级循环就是这条语义）。消费者：编译器。
2. **`src/compiler/compiler.c:657-689`** —— 发射 `OP_AND`/`OP_OR`（`:689` = `emit(comp->curBC, is_and ? OP_AND : OP_OR, result, result, right)`），短路形状在这一处定。消费者：VM。
3. **`src/vm/vm.c:3088-3089`**（`case OP_AND: goto L_AND;` / `case OP_OR: goto L_OR;`）→ **`:3508`**（`L_AND`）/ **`:3515`**（`L_OR`）—— VM 的判定。
4. **`src/compilation/aot_native.c:420-425`** —— AOT **自己**判：`:425` = `buf_str(b, op == TOK_AND ? ") && nv_tru(" : ") || nv_tru(")`。消费者：生成的 C 源。
5. **`src/compilation/wasm_backend.c:780-800`**（`cg_cond` 里的 `TOK_AND`/`TOK_OR` 分支）+ **`:1317`** —— wasm **自己**判；`:1317` 的注释逐字记着三个答案的历史：`This used to be a hard refusal, which made `x = a and b` a compile error here while the interpreter returned an operand and AOT returned a boolean: three answers for one operator.`
6. **`selfhost/compiler.im:254-258`** —— `.im` 写的**第二套编译器**，也判一次（注释引 `docs/AUDIT.md §1.0/§5 O0`）。消费者：host VM 执行它发出的字节码。
7. **`selfhost/eval.im:114-119`** —— `.im` 写的**第二套解释器**，也判一次。

**消费者读法**：VM 侧所有 `and`/`or` 都经过第 2 点的发射与第 3 点的判定；AOT/wasm 各自从 AST 走，**不经过 `OP_AND`/`OP_OR`**（§3.1 的 `emit_from_bytecode` 在 `src/` **零命中**，本会话复核为 0；引用 opcode 的行数：`src/compilation/aot_native.c` 0、`src/compilation/wasm_backend.c` 1（在注释里）、`src/vm/vm.c` 143）。

**测试**：`logic_semantics_regression`（`CMakeLists.txt:399-403`，`tools/logic_semantics.test.py`，标签含 `wasm`）—— 三后端逐格比对，且**短路**用除零证明（「refused」和「ran」是同一个观测）。**selfhost 那两处没有测试盯着**：`selfhost_toolchain_parses`（`CMakeLists.txt:250`）只解析 `selfhost/compiler.im`。

**三分：部分收敛。** 七个点今天答案一致（这是 `docs/DECFY_DESIGN.md` §0.5 的「已修」所指），但**判定仍写在七处** —— 改这条语义要改七个文件。

---

## 3. `%`：6 个决定点，答案今天一致

1. **`src/parser/parser.c:653`** —— `%` 与 `*`、`/` 同级（优先级是一处语义）。
2. **`src/compiler/compiler.c:818`** —— `case TOK_PERCENT: op = OP_MOD; break;`。
3. **`src/vm/vm.c:4161`**（`L_MOD`）—— intlike 分支 `ia % ib`，`ib == -1` 特判为 0（`INT64_MIN % -1` 会触发 SIGFPE），零除数抛 `division_by_zero`；浮点分支先按用户写的值判零、再窄化。
4. **`src/compilation/aot_native.c:246-251`**（`nv_mod`）—— `long long x = nv_asi(a), y = nv_asi(b);` + `if (y == -1) return nv_int(0);` + `nv_die_division_by_zero()`。
5. **`src/compilation/wasm_backend.c:994-1012`** —— `i64.rem_s`，零除数走 `IMP_ERROR`；**没有 `-1` 特判**，因为 wasm 的 `rem_s` 已定义 `x % -1 == 0`（同一语义，第三种写法）。
6. **`selfhost/eval.im:100`** —— `if op == "%" { return l % r }`：`.im` 写的解释器把 `%` 交给宿主引擎的 `%`。

**测试**：`mod_semantics_regression`（`CMakeLists.txt:386-390`，`tools/mod_semantics.test.py`，标签含 `wasm`）—— 16 格三后端比对，逐字记着三处旧分歧（`3000000000 % 7` 读 −2、`-2147483648 % -1` 死 SIGFPE、wasm 饱和转换把 3000000000 变 INT32_MAX）。**selfhost 的 `%` 无测试。**

**三分：部分收敛。** 三后端今天一致（§0.5 的「16/16 三后端一致」），但判定在六处；而且**同一个语义今天有三种写法**（VM 显式特判 `-1`、AOT 显式特判 `-1`、wasm 靠指令语义）—— 三者等价这件事本身没有一条测试直接盯着，是靠 16 格比对间接盯住的。

---

## 4. 真值：VM 内 1 个点，全局 5 个点

**五个点：**

1. **`src/vm/vm.c:347`**（`vm_truthy`）—— VM 的**唯一入口**。消费者：`src/vm/vm.c:3510-3524`（`L_AND`/`L_OR`）、`:3707`/`:3712`（条件跳转）、`src/runtime/runtime.c:81`（`builtin_bool`）、`src/runtime/runtime_posix.c:100`。**VM 内已收敛**，并有测试。
2. **`src/compilation/aot_native.c:193`** —— `static inline int nv_tru(NV a) { return a.t == NV_FLT ? (int)(a.f != 0.0) : (int)(a.i != 0); }`。消费者：`:218`（`nv_not`）、`:423`/`:425`（`and`/`or`）、`:460`、`:556`（`if`）、`:570`（`while`）。**AOT 有自己的一条真值规则，不经过 `vm_truthy`。**
3. **`src/compilation/wasm_backend.c:777`**（`cg_cond`）—— 逐类型发射真值，第三条规则。消费者：`:780`/`:789`（`and`/`or`）、`:1293`（`not`）、条件与循环的发射点。
4. **`selfhost/eval.im:21`**（`truthy`）—— 第四条。
5. **`selfhost/compiler.im:116`**（`truthy`）—— 第五条。

**测试**：`truthiness_single_point_runtime`（`CMakeLists.txt:914-921`，`vtest/truthiness_single_point_v06.im`）—— 它的注释逐字记着**收敛前的五个点**：`Five places each decided truthiness for themselves and disagreed.  `if ""`, `"" or f()`, `"" and f()`, `not ""` and `bool("")` did not share a rule` ⇒ `They now all call one helper, vm_truthy().` 注意：**这五个点是 VM 内部的五个调用点**，收敛成 `vm_truthy` 之后，**后端各自的真值规则一条没动**。另有 `logic_semantics_regression` 三后端比对（它的文档字符串把真值规则写成断言：`false`/`0`/`0.0`/`nil` 假，其余真）。

**三分：部分收敛（VM 内已收敛）。** 「真值产生点唯一」这句话在 **VM 内**成立且有测试；**跨后端不成立**：AOT、wasm、selfhost 各有一条独立规则，且 `vm_truthy` 与 `nv_tru` 的**类型域也不同**（前者认 `Value`，后者认 `NV`）。

---

## 5. `in`（成员）：1 个活入口 + 2 个候选

**活的（值层）：**

- **`src/vm/vm.c:2293`**（`set_contains_or_subset`）—— `in` 运算符的**唯一入口**，按右操作数类型分派：
  - `b` 是集合、`a` 是数组 → 逐个 `set_contains`；
  - `b` 是集合、`a` 是集合 → **`set_subset`**（`:2252`，即「子集」那一支）；
  - 否则 → `set_contains`（`:1932`）。
- 消费者：`OP_IN`，由 `src/compiler/compiler.c:721-727`（普通 `in`）、`:1714`、`:1749`（`case ... in ...` 模式）发射；VM 侧 `src/vm/vm.c:3130`（`case OP_IN: goto L_IN;`）→ **`:4246`**（`L_IN`）。
- 另有两个**共享** `set_contains` 的判定点，但它们不是 `in` 的语义：`src/vm/vm.c:2092-2130`（集合交/差/并的构造）、`:3683`、`:4282`（`L_BIND` 的约束校验）。

**候选的（类型层，`src/types/typeset.c`）：**

- **`:105`**（`im_typeset_contains`）与 **`:132`**（`im_typeset_subset`）。全仓调用者只有 `src/types/typeset.c` 自己（`:121`/`:123`/`:125`/`:127`/`:138`/`:142`/`:144`/`:153`/`:166`/`:167`/`:235`/`:236`/`:241`）与 probe（`src/types/typeset_probe.c:36-38`、`src/types/enum_probe.c`、`src/types/error_types_probe.c:21`、`src/types/registry_probe.c:12`）。**引擎里零消费者** ⇒ 按判据，这是**候选，不是决定点**。

**测试**（运算符形态确实有，见 §0 教训 2）：

- `collection_comprehension_runtime`（`CMakeLists.txt:646`，`vtest/collection_comprehension_v04.im` 里 `selected = { x in source | x > 1 }`）；
- `case_collection_patterns_runtime`（`CMakeLists.txt:779`，`vtest/case_collection_patterns_v04.im:10`/`:14`：`in Positive:`、`in [0~10]:`）；
- `vtest/range_metadata_probe_v06.im`（`say "A2 42 in x.range = " + str(42 in x.range)` 一族，`vtest/xrange_t2_v06.im` 里也有 `5 in R[0~9]`）；
- 类型层的 `typeset_probe`（`CMakeLists.txt:157-159`）—— 它同时是那个候选的**唯一消费者**。

**三分：已收敛（值层）。** `in` 的判定只有一处入口，三个发射点都汇进它，有测试。**类型层是候选**，不算进这条语义的决定点数。

---

## 6. 子集：1 活 + 1 候选（并集同病）

- **活：`src/vm/vm.c:2252`（`set_subset`）** —— `A in B`（两个集合）走 `set_contains_or_subset:2306`。它内部把区间集合展开成逐个 `set_contains`（`:2259`/`:2262`/`:2270`）。
- **候选：`src/types/typeset.c:132`（`im_typeset_subset`）** —— 符号层实现，引擎零消费者（同上）。
- **同病（并集，`docs/DECFY_DESIGN.md` §3.7 已判，此处只复核今天的读数）**：`set_union` 在 `src/vm/vm.c` 有 3 处（活）；`im_typeset_union`（声明 `src/types/typeset.h:41`）的调用者只有 `src/types/typeset_probe.c:19`、`:50`。⇒ **同一个语义两个生产点，一个被执行、一个永远到不了。**

**测试**：值层靠 `collection_core_runtime`（`CMakeLists.txt:648`）一族；候选层靠 `typeset_probe`。

**三分：未收敛。** 不是「答案不同」，而是**判定分裂在两层**：运行期那一层已经在跑，编译期那一层写好了没人用。`docs/TYPESET_V06.md:28` 自己就是这么记的，逐字：**「本仓库确实已有雏形（`src/types/` 的 TypeSet 内核 + VM 的 `SetObj` 运行时 + `be`/`type`/`in` 的语言层路径），但两套并行、只有一套被语言消费」**。

---

## 7. `be` 约束绑定：1 个检查点 + 2 个声明形状

**先说清一件容易被写成两件的事**（我上一轮差点写错）：**`be` 这个拼写被移除了，但「约束绑定」这条语义活着。**

- **拼写已移除**：`src/lexer/lexer.h:33` 逐字记着墓碑 `TOK_TILDE, TOK_BE /* 保留字墓碑：构造已移除，词法保留的理由见 lexer.c:43 */`；`src/parser/parser.c:1376-1379` 报 `` `be` declarations were removed (docs/SYNTAX.md 6.3) ``。**`OP_BE` 在 `src/` 今天零命中。**

- 盯这三个拼写的是三个用例：`be_removed_decl_runtime`（`CMakeLists.txt:661`）、`be_removed_assign_runtime`（`CMakeLists.txt:663`）、`be_removed_bare_runtime`（`CMakeLists.txt:665`）。
- **语义活着**：`src/parser/parser.c:1411-1419` 是**唯一声明形状** `名字: 集合 [= 初值]`，注释逐字说明「约束登记发生在编译产物上（见 compiler.c 的 STMT_BIND / OP_BIND），**与旧的 `名字 be 集合 [: 初值]` 走同一条运行时路径**」；→ `src/compiler/compiler.c:2266-2273` 发 `OP_BIND`；→ `src/vm/vm.c:4262`（`L_BIND`），判定在 **`:4282`**：`if (sidx >= 0 && !set_contains(vm, sidx, &init))` ⇒ 抛 `initial value out of range`。
- **第二个声明形状，语义不同**：`type Name = 集合` 走 `src/compiler/compiler.c:2256-2264`（`STMT_TYPE` → `OP_STORE_GLOBAL`），**没有任何约束校验**。⇒ 今天「一个名字属于哪个集合」这件事，由**两个形状**分别决定，其中一个不校验初值。

**测试**：`bind_init_overflow_runtime`（`CMakeLists.txt:668`，`vtest/bind_init_overflow_v04.im:6` = `x: Byte = 300`，`PASS_REGULAR_EXPRESSION` 钉住消息 `initial value out of range`；该文件 `:3` 的注释逐字记着这条消息**以前写作 `be: initial value out of range`**）。`type` 形状由 `type_collection_runtime`（`CMakeLists.txt:650`）一族覆盖。

**三分：未收敛。** 检查点唯一（这是好消息，且与 §3.2 R1 的「`type` 成为唯一定义形式」正好相反 —— **今天有两个形状**，而 §3.2 说的「`be` 约束绑定：解析器 → 编译器 → VM 三段已通」在**语义上成立、在拼写上已过期**：`be` 本身今天不解析）。

---

## 8. 诚实边界（本件没量的部分）

1. **我读的是代码，不是运行结果。** 本件全部结论是「判定写在哪、被谁调用」；**没有一条是跑出来的行为断言**。测试名来自 `CMakeLists.txt` 的 `add_test`，**我没有跑它们**（本件是只读读码件）。
2. **我读了 `src/`、`selfhost/`、`mods/`、`tools/`、`vtest/`、`CMakeLists.txt`**；`src/platform/`（41 个文件，OS 服务）**只做了否证性检查**：`git grep -ln 'OP_AND\|vm_truthy\|L_MOD\|set_contains' -- src/platform/` 为空 ⇒ 那里没有第二套解释器。**这不是「读过了」，是「找过、没有」。**
3. **`mods/` 里我只找到一处相关命中**：`mods/debug/debug_mod.c:137` 是**反汇编器**里 `OP_AND` 的打印分支 —— 它是**消费者**（改了 opcode 集合要跟着改），**不是语义决定点**。`mods/` 其余部分是 `.im` 模块与构建产物，不判语言语义。
4. **`selfhost/` 是被低估的第二实现。** 它有三套并行的判定（`and`/`or`、`%`、真值），全部**没有测试盯着**；本件只做了「它在哪」的定位，没有做「它与 C 侧是否逐格一致」的比对。
5. **`docs/DECFY_DESIGN.md` §0.5 的「已修」我没有当证据用。** 凡引用它的结论，我都重新量了今天的读数（七个点仍在、`vm_truthy` 只在 VM 内唯一）。**设计说已收敛，与「判定点只剩一个」是两件事** —— 本文件量的是后者。
6. **行号会漂。** 每条的主键是符号名；本文件的 `file:N` 形态引用会被 `tools/check_line_refs.py` 计数（它是 `EXP_LINE_REFS` 的被测集合），**所以写下本文件这件事本身会移动那个计数** —— 与 `docs/BOARD.md:64` 同一形状的不动点问题，见该行与 `tools/README.md:159`。

---

## 9. 复现（每个读数一条命令）

```sh
git grep -n 'TOK_AND\|TOK_OR\|OP_AND\|OP_OR' -- src/ selfhost/      # §2 七个点
git grep -n 'TOK_PERCENT\|OP_MOD\|nv_mod' -- src/ selfhost/          # §3 六个点
git grep -n 'vm_truthy\|nv_tru\|func truthy' -- src/ selfhost/       # §4 五个点
git grep -n 'set_contains\|set_subset\|im_typeset_contains\|im_typeset_subset' -- src/   # §5/§6
git grep -n 'OP_BIND\|STMT_BIND\|STMT_TYPE\|TOK_BE' -- src/          # §7
git grep -n 'emit_from_bytecode' -- src/                            # §2 消费者读法：0
```
