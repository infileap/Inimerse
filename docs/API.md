# Inimerse 语言与 API 参考

本文档是 Inimerse（Intuitive Narrative Immersive Script Engine）**语言 / API / 平台事实的唯一合并参考**。它合并了以下 11 份文件的仍然有效的内容：`docs/archive/API_REFERENCE.md`、`docs/archive/API_CATALOG.md`、`docs/archive/API_BUILTIN_TABLE.md`、`docs/archive/SYNTAX_SUGAR.md`、`docs/archive/PORTABILITY.md`、`docs/archive/WASM.md`、`docs/archive/WASM_ABI.md`、`docs/archive/NUMERIC_MODEL_V04.md`、`docs/archive/PARAM_FORMAT.md`、`docs/archive/inimerse_compile_guide.md`、`docs/archive/protocol_v1.md`。这 11 份源文件已于 2026-10-01 **移入 [`archive/`](archive/)**（未删除），本文取代其作为权威参考；其失效声明见 [`archive/README.md`](archive/README.md)。

## 状态口径

- 本文对**每一条能力断言**都给出状态标记：**已实现** / **部分实现** / **设计未实现**。
- 断言能力的条目在括号内给出证据，形如 `（证据：src/parser/parser.c:675-686；CTest null_coalesce_runtime）`。找不到实现证据的写 `（无实现证据）`。
- **项目状态与路线图的唯一权威是 [`STATUS.md`](STATUS.md)**。本文档只描述"现在代码里有什么"，**不做**进度或路线图承诺。
  > ✅ 更新（2026-10-01）：本文初稿生成时仓库内尚无 `docs/STATUS.md`；**它现已落地**，状态、路线图与版本裁定一律以其为准。原先的 `docs/archive/V04_STATUS.md` 已移入 [`archive/`](archive/)。
- 基准二进制：`build/inimerse` = **inimerse 0.5.0**（803856 字节，2026-10-01 15:54）。本文所有"实测"输出均由该二进制产生，并加 `--no-mods` 以隔离世界模组（默认会加载 `mods/utils`、`infiverse`、`verse_dist`）。仓库根的 `./inimerse` 已于 2026-10-01 用该构建产物刷新，现同为 **0.5.0**（gitignored）；此前遗留的 `build-local/`（0.4.0）等陈旧构建树已删除。
- 证据类型缩写：**CTest** = `CMakeLists.txt` 中的 `add_test(NAME ...)`，运行方式 `ctest --test-dir build --output-on-failure`。

| 状态标记 | 含义 |
|---|---|
| 已实现 | 源码里有实现，且（多数）有 CTest 或实测支撑 |
| 部分实现 | 有实现但语义/覆盖面小于文档描述，或平台受限 |
| 设计未实现 | 仅存在于 `docs/` 或 `future/` 的设计稿，仓库无实现证据 |
| 无实现证据 | 找不到任何源码或测试支撑的声明 |

### ⚠️ 两条必须知道的命名法则

1. **本引擎的内建函数是扁平命名（`前缀_动作`），没有 `namespace::member` 形式。**
   实际写法是 `verse_biome_get`、`say_*`、`ai_*`、`record_*`；**不是** `verse::Layer`、`block::Block`、`cell::Cell`。
   下列命名空间在 `src/`、`selfhost/`、`mods/`、`examples/` 中**全部零命中**，任何文档中出现它们都与实现不符：

   `window::` `audio::` `device::` `net::` `io::` `sys::` `thread::` `async::` `scene::` `verse::`
   `block::` `cell::` `log::` `sprite::` `tilemap::` `camera::` `anim::` `input::` `collision::`
   `engine::` `save::` `script::` `event::`

   复核：`grep -rn 'scene::\|window::\|verse::\|audio::\|thread::' src/ selfhost/ mods/ examples/` → 无输出。

2. **不存在的 CLI 子命令同样不得引用**：`inim os …`、`inim package …`、`inim verse …`、`inim bundle …`
   均不在 `src/main.c:174-190` 的 `usage()` 中。实际存在的子命令以 §10.2 为准。

---

## 1. 词法与会话级语法

### 1.1 语句与注释

| 语法 | 状态 | 证据 |
|---|---|---|
| 换行或 `;` 结束语句（分号容忍） | 已实现 | `src/lexer/lexer.c`；实测 `x = 1;print x` 与多行无分号均成功 |
| `#` 到行尾注释 | 已实现 | `src/lexer/lexer.c`；实测 |
| `//` 到行尾注释 | 已实现 | `src/lexer/lexer.c:75-81,102-107`；实测（`t_sugar_demo.im`） |
| 尾逗号容忍 | 部分实现 | 仅见 `docs/archive/SYNTAX_SUGAR.md` 声明，未单独核对源码 |
| 非 ASCII（含中文）标识符 | 部分实现 | `src/lexer/lexer.c:239-241` 允许 `c & 0x80` 的字节；语义未测试 |

### 1.2 关键字（源码快照）

来源：`src/lexer/lexer.c:9-`（关键字表）。注意**同一 TOK 可对应多个拼写**——这是"内糖"最直接的证据：

| 关键字 | TOK | 说明 |
|---|---|---|
| `say` / `print` | `TOK_SAY` | `print` 是 `say` 的核心别名，**不需要脱糖** |
| `func` / `fn` | `TOK_FUNC` | `fn` 是核心别名 |
| `and` / `or` / `not` | `TOK_AND`/`TOK_OR`/`TOK_NOT` | `&&`/`\|\|` 也映射到 `TOK_AND`/`TOK_OR`（`lexer.c:303,306`） |
| `unless` | `TOK_UNLESS` | 核心前端直接支持 |
| `at` | `TOK_AT` | 是关键字 `at`，**不是** `@` 符号 |
| `if` `else` `while` `for` `in` `return` `break` `continue` `repeat` | 各自 TOK | |
| `case` `record` `recorded` `with` `const` `declare` `clone` `autosave` | 各自 TOK | `recorded` 与 `record` 同 TOK |
| `forever` `when` `on` | 各自 TOK | `parser.c:874` |
| `show` `hide` `layer` `text` `new` `cursor` `fullscreen` `fixed` `ghost` `clickable` `drag` `secret` `tag` `quit_on_escape` | 各自 TOK | UI / 宿主关键字 |
| `int` `str` `float` `bool` | `TOK_INT`/`TOK_STR`/`TOK_FLOAT` + `TOK_BOOL` | `lexer.c:25`：既是类型名也是转换函数名 |

### 1.3 字符串、f-string 与数值字面量

| 语法 | 状态 | 证据 |
|---|---|---|
| 单/双引号字符串，`\` 转义 | 已实现 | `src/lexer/lexer.c:117-130`（`has_esc` 处理） |
| f-string `$"hello {name}"` | 已实现（基础插值） | 词法 `src/lexer/lexer.c:299`（`TOK_FSTRING`），消费方 `src/parser/parser.c`；**实测** 输出 `hello world` |
| 十进制整数 → `strtoll` | 已实现 | `src/lexer/lexer.c:234` |
| 小数字面量 → `strtod` | 已实现 | `src/lexer/lexer.c:199-232`（含指数、前导点 `.5`） |
| 十六进制 `0x1F` / `0Xab` | 已实现 | `src/lexer/lexer.c:208`（非法则回滚） |
| 区间 `..` | 已实现 | `src/lexer/lexer.c:308`（`TOK_RANGE`） |
| 集合区间 `[a,b)` / `(a~b]` | 已实现 | `src/lexer/lexer.c:304`（`TOK_TILDE`）+ `src/parser/parser.c:495-500`（`looks_like_interval`/`parse_set_interval`） |

> ⚠️ **实测语义提醒（区间 vs 迭代）**：`[0,3)` 求值为**集合值** —— `say [0,3)` → `set(R interval)`，`say range(0,3)` → `set(Z interval)`；而 `for i in [0,3) { say i }` **零次迭代**。要迭代整数区间请用 `for i in 0..3` 或 `for i in range(0,3)`（两者实测均输出 `0 1 2`）。

---

## 2. 运算符与核心语义

| 语法 | 语义 | 状态 | 证据 |
|---|---|---|---|
| `+ - * / %` | 算术 | 已实现 | `src/compiler/compiler.c` + `src/vm/vm.c`（`L_DIV` 等在 Wasm 后端对 int 恒用 double，见 §8） |
| `== != < <= > >=` | 比较 | 已实现 | `src/parser/parser.c` + `src/vm/vm.c` |
| 链式比较 `1 < x < 9` | 复合比较 | 已实现 | `src/parser/parser.c`（`EXPR_CHAIN_COMPARE`）；CTest `chained_comparison_runtime` ← `vtest/chained_comparison_v04.im` |
| `and` `or` `not` ／ `&&` `\|\|` | 逻辑（两套拼写同 TOK） | 已实现 | `src/lexer/lexer.c:303,306`；实测 `0 \|\| 1` |
| `??` | 空值合并（nil-coalescing，**非** Result 传播） | 已实现 | `src/parser/parser.c:675-686`（`parse_coalesce()`，注释明确此约定）；`src/compiler/compiler.c:635-639` 发射 `OP_IS_NIL`；`src/vm/vm.c:2850` 实现 `case OP_IS_NIL`、`vm.c:4895` 反汇编；CTest `null_coalesce_runtime` ← `vtest/null_coalesce_v04.im`；实测 `a = nil; say a ?? 7` → `7`，`say 0 ?? 7` → `0` |
| `?.` | 安全成员访问（可写 `obj?.type`） | 已实现 | `src/parser/parser.c:501-511`（置 `EXPR_MEMBER.member.safe = true`）；`src/compiler/compiler.c:1039-1043` 发射 `OP_IS_NIL`；CTest `optional_member_runtime` ← `vtest/optional_member_v04.im`；实测 `say r?.value ?? -1` 可运行 |
| `\|>` | 管道（降为 `fn(value, a)`） | 已实现 | `src/lexer/lexer.c:306`（`TOK_PIPELINE`）、`src/parser/parser.c:721`（`EXPR_PIPELINE`）；CTest `pipeline_runtime` ← `vtest/pipeline_v04.im`；实测 `[1,2,3,4] \|> len` → `4` |
| `>>` | 高阶组合 | 已实现 | `src/lexer/lexer.c:280`（`TOK_COMPOSE`）、`src/parser/parser.c:713`（`EXPR_CHAIN`）；CTest `composition_runtime` ← `vtest/composition_v04.im` |
| `expr?` | Result 错误传播（顶层 unwrap；函数体内 Err 返回，且先执行活跃 `finally`） | 已实现 | `src/parser/parser.c:540-545`（`EXPR_PROPAGATE`）；CTest `result_question_runtime`、`result_propagation_runtime`、`result_finally_propagation_runtime` |
| `lambda`：`x -> expr`、`(a,b) -> expr`、`(x) -> expr` | 函数值 | 已实现 | `src/parser/parser.c`（`parse_lambda_prefix`，`EXPR_LAMBDA`）；CTest `lambda_runtime`、`lambda_capture_runtime`、`lambda_nested_runtime`、`function_value_lifetime_runtime`、`function_thread_lifetime_runtime`；实测 `(x -> x + 1)(4)` → `5` |
| 集合推导 `{ x in source \| cond }` | 筛选式推导 | **部分实现** | `src/parser/parser.c:310`（`EXPR_SETCOMP`）；CTest `collection_comprehension_runtime` ← `vtest/collection_comprehension_v04.im`（`selected = { x in source \| x > 1 }; say len(selected) + 100`）；实测 `{ x in [1,2,3] \| x > 1 }` → `set(2)`。设计文档写的映射式 `{ f(x) \| x in S, cond }` **无实现证据** |
| `++` / `--` | 自增 / 自减 | 已实现 | `src/lexer/lexer.c:261,264`（`TOK_PLUS_PLUS`/`TOK_MINUS_MINUS`，编译器直接处理）；外糖层另有等价展开（§3） |
| 后缀条件 `expr if cond` / `expr unless cond` | 后缀条件 | 已实现 | `src/parser/parser.c:1650-1651,1689-1690`（`invert` 取反）；CTest `postfix_condition_runtime` |
| `try/catch/finally` | 异常式清理 | 已实现 | CTest `try_finally_runtime`、`finally_return_override_runtime` |
| cleanup 中 `break`/`continue`/`goto`（需传播路径） | 编译期报错 | 已实现 | CTest `finally_break_diagnostic`、`finally_continue_diagnostic`、`finally_goto_diagnostic` |
| `in` | 标量/数组/集合成员判定 | 已实现 | CTest `type_collection_runtime`、`range_meta_runtime` ← `vtest/range_meta_v04.im` |

### 2.1 `case` 家族

| 能力 | 状态 | 证据（CTest） |
|---|---|---|
| 等值 / 比较 / `in` / `match` / `else`，首个命中分支 | 已实现 | `case_structural_runtime` |
| 多条件 guard `n \| p, q` | 已实现 | `case_collection_patterns_runtime` |
| 别名 `case ... as name` | 已实现 | `case_alias_runtime` ← `vtest/case_alias_v04.im` |
| 定长数组解构 `[a,b]` | 已实现 | `case_array_runtime` ← `vtest/case_array_v04.im` |
| 嵌套 / 结构模式 | 已实现 | `case_nested_patterns_runtime` ← `vtest/case_nested_patterns_v04.im` |
| `case try`（Result 分支） | 部分实现 | `case_try_runtime` ← `vtest/case_try_v04.im` |
| `case value { _: ... }` 默认分支 | 已实现 | `case_structural_runtime`（`vtest/case_structural_v04.im:14,18,22,29` 的 `_:` 分支）、`case_nested_patterns_runtime`（`vtest/case_nested_patterns_v04.im:5,10,16`）；`--lint` 对**缺失**默认分支的告警由 `lint_case_missing_default_runtime` 钉住 ← `vtest/lint_case_missing_default_v04.im` |
| `--lint` 的 case 覆盖诊断（穷尽性 / 成员 / 别名） | 部分实现 | `lint_case_enum_runtime`、`lint_case_membership_runtime`、`lint_case_try_members_runtime`、`lint_case_try_alias_runtime`；通配符排在其它分支之前（其后分支不可达）由 `lint_case_exhaustive_runtime` 钉住 ← `vtest/lint_case_exhaustive_v04.im` |
| 谓词模式、`in TypeOrSet`、字典字段模式 | 部分实现 | `docs/archive/API_CATALOG.md` 列 部分实现；对应 CTest 覆盖有限 |

### 2.2 类型层内核（C API）

| 组件 | 源码 | 状态 |
|---|---|---|
| TypeSet（枚举、整数区间、并/交/差/补、成员/子集/相交；`im_typeset_cardinality` 有限返回成员数、无限/未知返回 `SIZE_MAX`） | `src/types/typeset.c/.h` | 已实现（CTest `typeset_probe`） |
| 枚举描述符（8/16 位自动宽度、名称↔编码、`im_error_domain_enum`、`im_typeset_materialize_enum`、`im_enum_from_finite_set`、`im_enum_is_exhaustive`、`im_enum_missing`、`im_enum_fingerprint`(FNV-1a 64)、`im_enum_compatible_append`(仅末尾追加)、`im_enum_qualified_member`/`im_enum_parse_qualified`） | `src/types/enum.c/.h` | 已实现（CTest `enum_probe`） |
| 闭包环境（基础已接 VM；`im_closure_env_copy_slot` 部分实现；retain/release 为原子操作而槽读写需外部锁；`im_closure_env_refs`） | `src/vm/closure.c/.h` | 部分实现（CTest `closure_probe`、`replay_closure_regression`） |
| 命名 TypeSet 注册表；`type Name = 集合表达式`（编译为命名集合全局值，`x be Name` 复用 `OP_BE`） | `src/types/registry.c/.h` | 已实现（CTest `type_registry_probe`、`type_collection_runtime`） |
| 错误类型目录 | `src/types/error_types.c/.h` | 部分实现（CTest `error_types_probe`） |

---

## 3. 内糖（internal sugar）vs 外糖（external sugar）

**定义**

- **内糖**：`src/lexer`（`lexer.c/.h`）、`src/parser`（`parser.c`）、`src/compiler`（`compiler.c`/`bytecode.c`）**直接解释**的语法。
- **外糖**：由**翻译层**改写的语法 —— 引擎侧 `src/desugar_mod.c`（`inimerse --desugar <in> [out]`）或外部转换器 `tools/eidos_desugar.py` 等。

### 3.1 ⚠️ 重要修正：`fn`/`print`/`&&`/`||`/`//`/`unless` 是**内糖**

`docs/archive/SYNTAX_SUGAR.md` 把这六者归入"外糖（需翻译层）"，在当前源码里它们**同时是核心前端的一等语法**：

| 糖 | 核心前端证据 |
|---|---|
| `fn` | `src/lexer/lexer.c:9-` 关键字表 `fn → TOK_FUNC` |
| `print` | 同表 `print → TOK_SAY` |
| `&&` / `\|\|` | `src/lexer/lexer.c:303,306` → `TOK_AND`/`TOK_OR` |
| `//` | `src/lexer/lexer.c:75-81,102-107` 行注释 |
| `unless` | `src/lexer/lexer.c:9-`（`TOK_UNLESS`）+ `src/parser/parser.c:1380`（语句）、`:1650-1651,1689-1690`（后缀条件取反） |

**实测（`build/inimerse --no-mods`，无需 `--desugar`）**：`print "a"` / `print "b"` 输出两行；`fn add(a, b) { return a + b }` + `print add(1, 2)` → `3`；`if true { print 1 }` → `1`；`x = 1` + `print x` → `1`。→ 结论：这六者应当记为**已实现的内糖**；外糖层只是**额外**提供一份文本级等价改写，供不想依赖前端糖的渠道使用。

### 3.2 外糖层 `src/desugar_mod.c`（171 行，CLI-only）

`desugar_mod_register(VM*)` 是空实现（注释：`CLI-only: inimerse --desugar in.im out.im`）。它实际只做 6 类改写：

| # | 改写 | 证据（`src/desugar_mod.c`） |
|---|---|---|
| 1 | `unless <cond> {` → `if !(<cond>) {` | `:37-38` |
| 2 | `say@target expr` → `say_target("target", expr)` | `:76-79`；CTest `desugar_runtime`（`tools/desugar.test.py`） |
| 3 | `print` → `say` | `:83` |
| 4 | `fn` → `func` | `:90` |
| 5 | `&&` → `and`，`\|\|` → `or` | `:97-98` |
| 6 | `x++` / `x--` → `x = x + 1` / `x = x - 1` | `:99-121` |

它还会**删除代码区行尾的 `;`**（`:127-146` 的 `last_code_semi` 处理），因此脱糖产物与手写等价物在分号上存在差异。它**不处理** `eidos`/`ed`（grep 无命中）。

### 3.3 真正必须走外糖的语法

| 语法 | 状态 | 证据 |
|---|---|---|
| `say@target expr` | 已实现（**仅经脱糖**） | 核心前端不支持：实测 `say@console 42` → `Error: expected 'expression', but got '@' (type 142)`；同文件 `--desugar` 后成功产出 `say_target("console", 42)`。`@` 字符在 `src/lexer/lexer.c` **无任何 case**（`at` 是独立关键字）。CTest `desugar_runtime`（`tools/desugar.test.py`）逐条断言脱糖产物的三行改写，并带一条负对照：同源不加 `--desugar` 必须被拒 —— 若哪天通过了，说明 `say@target` 已成本语法、本节结论过期 |
| `eidos Name { ... }` / `ed Name { ... }` | 已实现（**仅经外部工具**） | 核心前端零实现：`"eidos"`/`"ed"` 在 `src/lexer`、`src/parser`、`src/compiler` 均无命中。全部由 `tools/eidos_desugar.py`（516 行）实现；CTest `eidos_desugar_runtime`（`tools/eidos_desugar.test.py`）+ `eidos_runtime`（`tools/eidos_runtime.test.py`） |
| 用户自定义别名 | 无实现证据 | 仅 `docs/archive/SYNTAX_SUGAR.md` 声明"支持用户自定义别名" |

### 3.4 V0.4 内糖 / 外糖准入规则（保留自 `docs/archive/SYNTAX_SUGAR.md`）

1. 新增**内糖**必须带 `parser`/`compiler` 级测试与解释器回归。
2. 新增**外糖**必须带脱糖前后等价性测试，且不得误改字符串、注释与嵌套结构。
3. `?` Result 传播、`|>`、lambda、组合 `>>`、集合推导定为**内糖**；复杂部分应用与表达式级 `case` 属后续。
4. `eidos`/`ed` 定为**外糖**，且只冻结"可执行子集"（§4）。
5. 集合到类型/对象的映射由翻译器负责，不由 VM 承担。

---

## 4. Eidos 可执行子集

Eidos 完全由外糖工具实现（见 §3.3）。本节的"支持"= `tools/eidos_desugar.py` 能正确脱糖并且引擎能运行其产物。

### 4.1 支持

| 能力 | 说明 |
|---|---|
| `eidos Name { ... }` / `ed Name { ... }` | 生成同名工厂函数 |
| 字段 `name = expression` | 按声明顺序成为参数；调用时不传则用默认值（`nil` 走默认值） |
| 方法 `name(args) { ... }` | 闭包写入实例字典，调用形式 `object["name"](...)`，方法体可读写实例字段 |
| 单行方法 `name(args) -> expression` | 等价于返回该表达式的方法 |
| `init` | 实例化时自动调用 |
| 单继承 `eidos Child: Base` | 父类必须先声明（`:404`） |
| 方法覆盖；`super.method(...)` | 仅有限支持 |
| 子类字段覆盖同名父类字段 | 支持 |

**实测证据**（`tools/eidos_runtime.test.py` 的脚本）：`c = Counter(4)`、`say c["get"]()`、`say c["inc"](3)`、`eidos Child: Base` + `super.inc(n)`、`say Base()["inc"](2)` 均通过。样例脚本 `vtest/eidos_object_probe_v04.im` 展示工厂函数返回字典 + 闭包方法（`obj["get"] = (ignored -> obj["value"])`、`obj["inc"] = (n -> obj["value"] + n)`）。

### 4.2 不支持

mixin（被显式拒绝）、可见性（`private`/`public`）、`sealed`/`frozen`/`invariant`、热修改、自动无括号方法调用、表达式级 `case`、复杂部分应用。

脱糖器的错误串（可直接用于定位）：`unclosed Eidos body`(`:59`)、`unclosed Eidos parameter list`(`:84`)、`unclosed Eidos block comment`(`:119`)、`Eidos members must start with an identifier`(`:138`)、`Eidos field {name} needs a default expression`(`:145`)、`Eidos method {name} has invalid parameters`(`:154`)、`Eidos method {name} needs an expression`(`:166`)、`unsupported Eidos member {name}`(`:170`)、`Eidos {name} inheritance needs a parent name`(`:398`)、`Eidos mixins are not supported by the v0.4 desugar subset`(`:402`)、`Eidos parent {parent} must be declared before {name}`(`:404`)。

**总体状态：部分实现**（工具链产物；引擎本身不认识 Eidos）。

---

## 5. 内建函数目录

本节表格由**源码提取**生成，命令：

```
grep -a -oE 'vm_register_builtin(_full|_safe)?\s*\(\s*\w+\s*,\s*"[^"]+"' <文件> \
  | sed 's/.*"\(.*\)"/\1/' | sort -u
```

注册 API 定义：`vm_register_builtin(VM*, const char*, BuiltinFunc)`、`vm_register_builtin_full(vm, name, fn, flags, since)`、`vm_register_builtin_safe(...)`（`src/vm/vm.c:1722/1746/1768`）。

因为名称直接来自注册调用点，**表内每一条的状态都是"已实现"**；真正的差别是**平台可用性**（"仅 Windows"/"仅 POSIX"/"两侧"）。注册编排见 `src/main.c:571-593`：先 `runtime_register_builtins(&vm)`（`src/main.c:1066-1068`），再 core 模组（`isolate`/`lint`/`vm_debug_builtins`/`gui`/`result`/`io`/`net`/`json`/`server`/`say`/`identity`/`social`/`ai`/`record`/`replay`），最后 world 模组（`infiverse`/`verse_dist`/`build`，受 `--no-mods` 控制，`src/main.c:1096-1099`）。

> 本表是**名称级**目录。每个函数的参数与返回值以对应模组源码为准（本文不臆造签名）。

<!--BUILTIN_TABLE-->

### 5.19 与 `docs/archive/API_BUILTIN_TABLE.md` 的差异（已核对）

| 问题 | 事实 | 证据 |
|---|---|---|
| `gui_mod` 计数虚高 | 表列 163，源码唯一名 **162**；原因是表内 `gui_fullscreen` 重复出现两次 | 源码提取 + 集合比对 |
| `verse_dist_mod` 漏列 13 个 | 表列 19，源码 **32**。漏：`verse_econ_audit` `verse_econ_balance` `verse_econ_domain` `verse_econ_mint` `verse_econ_settle` `verse_idem_begin` `verse_node_advertise` `verse_node_discover` `verse_node_handoff` `verse_node_schedule` `verse_session_authority` `verse_session_reattach` `verse_session_state` | 源码提取 |
| `identity_mod` 漏列 5 个 | 表列 3，源码 **8**。漏：`oauth_authorize` `oauth_bind` `oauth_config` `oauth_status` `oauth_unbind` | 源码提取 |
| `thread_*` 归属错误 | 表把 `thread_result`/`thread_await`/`thread_release` 记在 runtime，实际注册于 `src/mod/result_mod.c:232-234` | 源码 |
| 三个模组完全未收录 | `result_mod.c`(11)、`replay_mod.c`(10)、`say_mod_windows.c`/`say_mod_posix.c`(各 32) | 源码提取 |
| `runtime` 的平台差异 | 表列 59（= `src/runtime/runtime.c`）；POSIX 构建用 `src/runtime/runtime_posix.c` 的 **79** 个替代 | 源码提取 |

---

## 6. 模块与 C / Python / Java 扩展接口

### 6.1 原生模组装载

| 项目 | 事实 | 证据 |
|---|---|---|
| 模组目录布局 | `mods/<name>/mod.st`（JSON 文本，键 `script` 与 `native_lib`） | `src/mod/mod.h`；仓库内 `mods/utils/mod.st`、`mods/build/mod.st` |
| 装载 API | `void mod_load_all(VM*, const char *mod_dir);` / `void mod_load_by_name(VM*, const char *mod_dir, const char *name);` | `src/mod/mod.h` |
| Windows 装载 | `LoadLibrary` + `GetProcAddress(h, "mod_init")`，调用 `void (*)(VM*)`；有 `script` 则 `vm_exec_script_file` | `src/mod/mod.c` |
| POSIX 装载 | `dlopen(p, RTLD_NOW\|RTLD_LOCAL)` + `dlsym(handle, "mod_init")`，句柄存静态 `g_native_handles[128]`（满则跳过，无 `mod_init` 则 `dlclose`）；`mod_load_all` 用 `opendir` 跳过 `.` 开头项 | `src/mod/mod_posix.c` |
| 仓库内实际模组 | `mods/utils`（`main.im` + `mod.st`）、`mods/debug`（`main.im` + `debug_mod.c` + 若干 `.dll` 历史副本）、`mods/build`（`build_mod.c` + `mod.st`） | `ls mods/` |

### 6.2 IDL 与绑定生成（bindgen）

- 接口定义文件：`examples/interface.def` —— `interface inimerse_native { fn add(a: i32, b: i32) -> i32; fn greet(name: string) -> string; fn fail(code: i32) -> error }`，头注释给出用法：
  `python3 tools/bindgen.py interface.def --language c --out build/c/inimerse_native.h`（把语言换成 `java`/`python` 亦然）。
- 生成器：`tools/bindgen.py`，位置参数 `idl`，`--language` 必填且 choices=`[c,c++,java,python]`，`--out` 必填；C/Java/Python 生成分别见 `tools/bindgen.py:114/143/185`。状态：已实现（CTest `bindgen_regression`）。
- C++ 扫描：`tools/cpp_scan.py <sources...> [--emit-idl] [-o/--output]`，输出 JSON inventory，生成的 `.def` 头部注释为 `// Generated by tools/cpp_scan.py --emit-idl (roadmap §3.1);`；函数指针、标量指针、按值结构体等不可转换签名会被标为**需手工适配**而非静默包装。状态：已实现（CTest `scan_tools_regression`）。
- Python 扫描：`tools/python_scan.py <sources...> [--emit-idl] [-o/--output]`，扫描带类型标注的标量函数。状态：已实现（同上 CTest）。
- 桥接示例：`examples/java_bridge.im`、`examples/python_bridge.im`、`examples/build.gradle`、`examples/pom.xml`、`examples/cpp_native.im`。

> ⚠️ **`native` 关键字无实现证据**：`src/lexer` 与 `src/parser` 没有 `TOK_NATIVE`/"native" 声明语法，`ast.h` 无双列项。`examples/cpp_native.im` 实际只是一句说明脚本（内容为 `say "see tools/cpp_scan.py --help and docs/archive/ROADMAP_0.5-0.6.md §3.1"`），其中的 `native mylib { fn add(...) }` 写在注释里示意流程。grep 到的 "native" 全部是 C 侧"原生库"概念（`src/platform/socket.h`、`src/platform/platform.c`、`src/mod/mod.c`、`src/mod/mod_posix.c`）。**结论：语言层没有 `native` 模块声明；原生接入走 `mod.st` + `mod_init` 或 bindgen 生成的 C/Java/Python 绑定。**

### 6.3 C 层密码学接口（`src/common/`）

内建库直接调用，不经语言层暴露；此处记录是因为签名/哈希行为可被外部实现验证。

- **SHA-256**：`Sha256Ctx` + `sha256_init/sha256_update/sha256_final`（`src/common/sha256.h`），另有 `sha256_hex` / `sha256_hex_of_digest`。已实现，广泛使用（账本链、内容寻址、`.vverse` 清单）。
- **SHA-512**：`Sha512Ctx { uint64_t h[8]; uint64_t total; unsigned char buf[128]; size_t buflen; }` + `sha512_init/sha512_update/sha512_final`，`sha512_buf(data,len,out)` 是一次性包装（`src/common/ed25519.h`）。**流式接口是 2026-10-01 新增的**——此前 `ed25519_sign`/`ed25519_verify` 把 `R‖A‖M` 拷进固定 8320 字节缓冲并静默截断，导致 >8256 字节的消息签名永远无法验证。现在三处（`r`、两处 `k`）都走流式。
- **Ed25519**：`ed25519_pubkey(seed,pub)`、`ed25519_sign(seed,msg,msglen,sig)`、`ed25519_verify(pub,msg,msglen,sig) -> int`（`src/common/ed25519.h`）。⚠️ **`ed25519_verify` 的参数顺序是 `(pub, msg, msglen, sig)`**，而语言层内建 `verse_verify(data, sig, pub)` 是 `(data, sig, pub)` —— 两者顺序不同，别混。
- 契约：签名必须是**真正的 RFC 8032 签名**，可被 `cryptography` / OpenSSL 验证。回归防线是 `ed25519_probe`（CTest），四组守卫见 `docs/STATUS.md` §2.4；**任何对 `src/common/ed25519.c` 的改动都必须让它通过**。
- 长度上限：消息长度受调用方缓冲约束，库本身无上限（流式）。实测通过 20000 字节。

> ⚠️ **数值一律为整数**：canonical JSON 写入器（`src/verse/eventlog.h` 的 `vl_cjson_*`）与 `src/verse/json_min.c` 的解析器**只处理整数**，遇到 `.`/`e`/`E` 会失败（`non-integer number unsupported`）。这是刻意的：让每条记录字节级往返。新增任何带小数的字段会同时打破哈希链与解析。

---

## 7. 平台与可移植性（PAL）

### 7.1 平台抽象层 API（已统一）

| 领域 | API | 头文件 |
|---|---|---|
| 时间 | `im_platform_now_ms(void)`、`im_platform_sleep_ms(unsigned)` | `src/platform/platform.h` |
| 路径/环境 | `im_platform_mkdirs`、`im_platform_path_join`、`im_platform_executable_path`、`im_platform_getenv`、`im_platform_read_file`、`im_platform_write_file`、`im_platform_lan_ip` | 同上 |
| 能力查询 | `im_platform_has_capability(const char*)` | 同上 |
| 目录 | `ImDir`、`im_dir_open`、`im_dir_next`、`im_dir_next_ex`、`im_dir_close` | `src/platform/dir.h` |
| 互斥锁 | `ImMutex`、`im_mutex_new/free/lock/unlock` | `src/platform/sync.h` |
| 线程 | `ImThreadProc`、`im_thread_start/join/detach/close` | `src/platform/thread.h` |
| Fiber | `ImFiberProc`、`im_fiber_convert_current/create/switch/destroy` | `src/platform/fiber.h` |
| 进程 | `im_process_spawn/pid/alive/wait/wait_kill/kill/exit_code/close/capture` | `src/platform/im_process.h` |
| Socket | `im_socket_init/shutdown/listen/connect/connect_timeout/accept/send/recv/peek/set_nonblocking/last_error/would_block/local_port/port_open/port_available/close` | `src/platform/socket.h` |

实现细节（已核实）：Windows 用 Win32 / WinSock，POSIX 用 BSD sockets / pthread / `ucontext` / `fork-exec`；`src/platform/socket.c` 对 `EINTR` 重试（`:130,170,184,193,203`），发送前在支持时加 `MSG_NOSIGNAL`（`:181-182`）；`src/platform/fiber.c` 在 POSIX 用 `ucontext.h` + `makecontext`，在 Windows 用 `ConvertThreadToFiber(NULL)`。

### 7.2 能力查询

`inimerse capabilities` 逐个打印 `im_platform_has_capability` 为真的名字（`src/main.c:794`）。`im_platform_has_capability` 的唯一实现（`src/platform/platform.c:162-174`）：

- **Windows**：`native_dll`、`gui`、`threads`、`fiber`、`process`、`socket`
- **POSIX**：`posix_fs`、`threads`、`fiber`、`process`、`socket`

不可用能力应当返回明确错误，并可先由 `has_capability` 查询（设计约定，见 §7.4 的实际实现状态）。

### 7.3 平台矩阵

| 模组 / 能力 | Windows | POSIX | 证据 |
|---|---|---|---|
| 核心 VM / 词法 / 语法 / 编译器 / 纯语言 runtime | ✅ | ✅ | `CMakeLists.txt:175-202` 公共核心 |
| `runtime` 内建 | ✅ 59（`runtime.c`） | ✅ 79（`runtime_posix.c`） | 源码提取 |
| `platform` / `thread` / `fiber` / `dir` / `process` / `socket` | ✅ | ✅ | PAL |
| `net_mod` 内建 | ✅ 11 | ✅ 7（**无 `udp_*`**） | `src/mod/net_mod.c` / `net_mod_posix.c` |
| `server_mod`（端口/房间） | ✅ 10 | ✅ 10（同名） | `src/mod/server_mod.c` / `server_mod_posix.c` |
| headless 帧服务 | ✅ | ✅ | `src/headless_server.c` / `headless_server_posix.c` |
| HTTP / 串口 / WebSocket 基础握手 / CRP session | ✅ | ✅ | `src/platform/http_posix.c`、`http_client.c`、`serial_posix.c`、`websocket.c`、`crp_session.c` |
| `io_mod`（44） | ✅ | ❌ 不可用 | `src/platform/posix_stubs.c` 对 `io_mod_register` 空实现 |
| `gui_mod`（162） | ✅ | ❌ 不可用 | posix_stubs 空实现；GUI/键鼠为宿主专用 |
| `identity_mod`（8） | ✅ | ❌ 不可用 | posix_stubs 空实现 |
| `social_mod`（5） | ✅ | ❌ 不可用 | posix_stubs 空实现（Windows 侧亦为本地存根） |
| `ai_mod`（6） | ✅ | ❌ 不可用 | posix_stubs 空实现 |
| `build` 模组（打包） | ✅ | ❌ 不可用 | `build_project_impl` 打印 `inimerse: capability '%s' is not available on this POSIX build yet` 并 `return -1` |
| 其余 common 模组（`infiverse` 24、`verse_dist` 32、`record` 12、`replay` 10、`json` 2、`result` 11、`isolate` 1、`lint` 1） | ✅ | ✅ | `CMakeLists.txt:175-202` 两个分支 |

POSIX 构建**不包含**这些源文件：`mod/io_mod.c`、`mod/gui_mod.c`、`mod/social_mod.c`、`mod/ai_mod.c`、`mod/identity_mod.c`、`mod/server_mod.c`、`mod/net_mod.c`、`mods/build/build_mod.c`（改由 `net_mod_posix.c`、`server_mod_posix.c` 与 `posix_stubs.c` 承担）。

### 7.4 已知缺口与修正

- **`im_platform_lan_ip` 在 Windows 无实现**：`src/platform/platform.c:176-187` 的 `#ifdef _WIN32` 分支直接 `return -1`；POSIX 分支用 `getifaddrs`/`inet_ntop` 选首个 `UP` 且非 `LOOPBACK` 的 IPv4。
- **VFS 是部分实现**：`src/platform/vfs.h`(17 行)+`vfs.c`(69 行) 只有 `im_vfs_create/destroy/mount_os/read_file/write_file/normalize`，头注释为 "V0.4 virtual filesystem. Paths are resolved through a named mount prefix"。**没有权限 / 只读 / 配额**概念；`docs/archive/API_CATALOG.md` 提到的 `vfs_*` 内建与 `vfs_probe`（无实现证据）。
- **⚠️ 修正：`docs/archive/PORTABILITY.md` 的"Makefile 门禁"清单已过时**。仓库根 `Makefile` 只有 **一个**目标：`bytecode_capture_probe:`（335 字节）。文档声称的 `platform_probe`/`fiber_probe`/`process_probe`/`socket_probe`/`thread_probe`/`dir_probe`/`headless_probe` 现由 `CMakeLists.txt` 的 `add_test`（即 CTest）承载 —— 这些名字确实存在于 CTest 全量列表（见 §10.4）。
- **⚠️ 修正：`docs/archive/WASM.md` 的 `make wasm` 目标不存在**（根 `Makefile` 只有上面那一个目标）。工具链探测脚本 `tools/wasm_check.js` 本身存在（见 §8）。
- 构建命令：`inimerse capabilities`；`cmake -S . -B build -DINIMERSE_BUILD_ENGINE=ON`；`cmake --build build`；`ctest --test-dir build --output-on-failure`。

---

## 8. WASM 后端

### 8.1 定位：数值子集 MVP（不是完整引擎）

`src/compilation/wasm_backend.c`（1253 行）头注释与实现约定：

- **支持**：int / float / bool、算术、`if`/`while`/`repeat`、用户函数与递归、全局变量。
- **拒绝**：字符串、集合、线程、SIMD、GC —— 在**编译期**带行号拒绝，错误前缀 `wasm MVP subset: %s (line %d)`（`wasm_backend.c:182`），可通过 `wasm_backend_last_error()`（`:173`）取出。
- 语义（`wasm_backend.c` 头注释）：`L_ADD`/`L_SUB`/`L_MUL` 对 int 为 32 位运算、溢出转 float；`L_DIV` 恒用 double、`int/0` 抛 `division_by_zero`；`L_MOD` 的 `int % int` 用整数、否则 `(int)as_double(a) % (int)as_double(b)`；`val_eq` 同 tag 比较、跨 int/float 经 double 比较。

验收：CTest `wasm_backend_regression` = `tools/wasm_backend.test.py`，13 个等价用例（`say_const`、`globals`、`call`、`fib`、`while`、`repeat`、`float_div`、`int_overflow`、`negative`、`mod`、`nested_calls`、`bools`、`mixed_num`，每例与 `inimerse run` 的解释器输出逐字对比，有 `wabt` 时再过 `wasm-validate`）+ 4 个拒绝用例（`strings` → `strings are not supported`；`unknown_fn` → `function 'nosuch' not found`；`and_outside` → `'and'/'or' outside a condition`；`array` → 表达式类型错误），收尾打印 `wasm backend: ok (13 equivalence cases, 4 rejections)`。

### 8.2 后端 ABI（`--abi-target wasm`）

导出（`src/compilation/wasm_backend.c:1156-1164`）：`inimerse_run`、`inimerse_probe`、`inimerse_abi_version`、`inimerse_capabilities`；探针值（`:1223-1231`）**`probe = 0x0500`、`abi = 1`、`capabilities = 0`（0 = 不需要宿主能力）**。

导入表（`env.*`）：

| 导入 | 签名 | 用途 |
|---|---|---|
| `env.im_print_int` | `(i64)` | 打印整数 |
| `env.im_print_float` | `(f64)` | 打印浮点 |
| `env.im_print_bool` | `(i32)` | 打印布尔 |
| `env.im_print_nil` | `()` | 打印 nil |
| `env.im_error` | `(i32)` | 运行期错误，码 1/2/3 → `division_by_zero` / `call_frame_overflow` / `call_stack_overflow` |

宿主：`tools/wasm_run.js`（70 行）——读 `.wasm` → 注入 5 个 `env` 导入（错误码映射后 `exit 1`）→ 校验 `probe == 0x0500` 且 `abi == 1`（否则 `exit 2`，报 `error: unexpected wasm probe marker 0x..` / `error: unsupported wasm ABI revision ..`）→ 调 `inst.exports.inimerse_run(0)`。其中 `fmtFloat` 复刻 `src/vm/vm.c` 的 `vts_double`（nan→`nan`；0→`0`；整数值且 `|d| < 2^63` 走 BigInt；`|d| >= 1e15` 用 `toPrecision(17)`；否则整数部分 + 最多 6 位小数去尾零）。用法：`node tools/wasm_run.js <script.wasm>`。

### 8.3 第二套 ABI 探针（`tools/wasm_probe.c`）—— 勿与 §8.2 混用

`tools/wasm_probe.c`（12 行）导出 `inimerse_probe() -> 0x0400`、`inimerse_abi_version() -> 1`、`inimerse_capabilities() -> 0`。`docs/archive/WASM_ABI.md` 描述的正是这一套（`0x0400` = v0.4 标记）。

构建与测试门禁：`CMakeLists.txt:151-168` 仅当找到 `clang`（`find_program(INIMERSE_CLANG clang)`）且 `NOT WIN32` 时，用 `--target=wasm32-wasi -nostdlib -Wl,--no-entry` 编译 `tools/wasm_probe.c` 为 `${CMAKE_BINARY_DIR}/wasm_probe.wasm`（`add_custom_target(wasm_probe ALL)`，并 OPTIONAL 安装到 `share/inimerse`）；并且**仅当**找到 node 时才注册两个 CTest：`wasm_probe`(`tools/wasm_probe_check.js`)、`wasm_host`(`tools/wasm_host.test.js`)，LABELS `wasm;smoke`，TIMEOUT 10。

**⚠️ 修正：两套标记不要混用** —— 后端（`--abi-target wasm`）用 `0x0500`，独立探针用 `0x0400`；`docs/archive/WASM_ABI.md` 只覆盖后者。

### 8.4 工具链门槛与宿主适配器

- `tools/wasm_check.js`（6 行）：探测 `emcc` 与 `clang --target=wasm32-wasi`，两者都缺时打印 `WASM toolchain not found. Install emscripten or wasi-sdk/clang.` 并 **`exit 2`**（不伪造成功）；成功时输出 JSON `{emscripten, wasiClang, target:"wasm32-wasi", status:"toolchain-ready"}`。
- 产物忽略规则：`.gitignore:35` 的 `tools/*.wasm` → `wasm_probe.wasm` 不入库。
- 浏览器/Node 宿主适配器 `tools/wasm_host.js`（46 行）：`class InimerseWasmHost`，`options.maxPages` 默认 256；**只注入调用方显式提供的 imports**（`env.inimerse_now_ms` 等），遇到未提供的导入抛 `unsupported WASM import: <module>.<name>`；校验 `abi_version() === 1`，否则抛 `unsupported Inimerse WASM ABI`；暴露 `probe()`（缺导出抛 `missing inimerse_probe export`）与 `capabilities()`（缺导出返回 0）。**FS / 网络 / DOM 永不隐式可用。** 用法：`node tools/wasm_host.test.js tools/wasm_probe.wasm`。

### 8.5 平台无关性设计原则（保留自 `docs/archive/WASM.md`）

1. 字节码与集合语义不依赖 OS。
2. 缺能力经 `has_capability` 返回 false，而不是崩溃。
3. 不直接用 Win32 / POSIX fd / 动态库。
4. 用固定 ABI 导入表传递时间、随机数、文件、网络。

---

## 9. 数值模型

### 9.1 已实现（部分实现）

| 行为 | 事实 | 证据 |
|---|---|---|
| 整数字面量 | 十进制 `strtoll`；十六进制 `0x..` | `src/lexer/lexer.c:234,208` |
| 小数字面量 | 直接交给 `strtod` → IEEE-754 double | `src/lexer/lexer.c:232` |
| 浮点 → 字符串 | 17 位有效数字保护网，避免 `%g` 截断（仍非任意精度） | `src/vm/vm.c` 的 `vts_double`（`tools/wasm_run.js` 的 `fmtFloat` 为其复刻） |
| 精度回归 | `vtest/float_precision_v04.im`：`value = 1.2345678901234567; say str(value)` | CTest `float_precision_runtime` |
| 整型区间元数据 | `in` 对整型与超大浮点边界的行为 | CTest `range_meta_runtime` ← `vtest/range_meta_v04.im` |

### 9.2 设计未实现：数值塔 `Number = Z ∪ Q ∪ D ∪ F`

`docs/archive/NUMERIC_MODEL_V04.md` 提出但**未实现**（源码内无 `bigint`/`rational`/`decimal`/`BigFloat` 相关实现，grep `src/` 零命中；`docs/archive/API_CATALOG.md` 亦明确把 `BigInt`/`Q`/`Dec`/`BigFloat` 列为"尚未提供"）。此处的数值塔**仅作为意图引用**：

- `Z` 任意精度整数（BigInt）；`Q` 有理数 `(p,q)`，`p ∈ Z`、`q ∈ Z\{0}`，始终约分且 `q > 0`；`D` 任意精度十进制 `significand × 10^exponent`（不转二进制）；`F` IEEE-754，仅显式近似 / 图形 / 性能路径使用。
- 嵌入关系 `Z ↪ Q`；默认字面量不应静默降级到 `F`。
- 运算规则（意图）：`Z op Z` 保持 `Z`，溢出提升 BigInt；`Z`/`Q` 混合提升 `Q` 且精确，除零是运行时错误；`D` 与 `Z`/`Q` 先提升、不经过 double；只有显式 `float(x)`/`approx(x)` 或外部 API 才进入 `F`；比较与集合去重按规范化数学值，`1 == 1/1 == 1.0` 哈希一致。
- 实现顺序（意图）：① 保留数字 token 原文 + 常量池 decimal/bigint 文本入口；② `Num` tagged union + BigInt/Rational 最小实现、小整数内联；③ 改造四则/比较/序列化/集合哈希并补回归；④ 最后接可选 `F` 快路径。
- 验收标准（意图）：`0.1 + 0.2` 保持十进制语义；100 位小数与超 64 位整数在四则、比较、集合去重、序列化后不丢信息。

**结论：当前实现是 int64 + double 两档；`Q`/`D`/`Z`（任意精度）与数值塔的全部规则都是设计未实现。** v3.1 起 int64 这一档是**真的** —— 此前 VM 的整数槽只有 32 位，超过 int32 的字面量被编译器静默降级成 double，于是 `Value` 里根本没有超过 int32 的整数（见 [AUDIT.md](AUDIT.md) §1.14）。三条具体规则已落地并被三后端逐格断言：① 整数超出 int64 抛 `numeric_overflow`（**不**提升为 double、也**不**回绕）；② `a / b` 能整除时留整数、有余数时给浮点（`4/2` 打 `2`、`7/2` 打 `3.5`、`6/4` 打 `1.5`）；③ `==` 跨类型按数值等价（`1 == 1.0`、`true == 1` 为真），同类型时按该类型精确比较（`9007199254740993 == 9007199254740992` 为假）。

---

## 10. 编译与构建 CLI

### 10.1 顶层用法与全局选项

```
inimerse <script.im> [参数]
inimerse <script.inim> [参数]
```

| 选项 | 说明 | 证据（`src/main.c`） |
|---|---|---|
| `--version` / `-V` | 版本 | `:777` |
| `where` | 打印可执行文件绝对路径 | `:784` |
| `capabilities` | 逐个打印为真的能力名 | `:794` |
| `changelog` | **仅 Windows**（`#ifdef _WIN32`） | `:898` |
| `--gui` | 进入 GUI 模式 | `:819` |
| `--jit[=off\|template\|optimized]` | 解释器回退开关，**不产生加速**（见 §10.5） | `:824`，非法值报 `error: --jit expects off\|template\|optimized` 并退出 2 |
| `--headless` | 隐含 GUI 模式 | `:834` |
| `--port N` | headless 帧服务端口，默认 `11440` | `:816,839` |
| `--http-port N` | HTTP API 端口，默认 `11470` | `:818,850` |
| `--params <file>` | 载入参数文件 | `:845` |
| `--time-limit N` | 秒 → 毫秒 | `:860` |
| `--err-json` | 错误输出为 JSON | `:860` 区段 |
| `--desugar <in> [out]` | 外糖显式转换（§3.2） | `:905` |
| `--safe` | 能力受限的安全模式 | `:905` 区段 |
| `--no-mods` | 不加载 world 模组 | 同上 |
| `--lint` | 静态诊断 | 同上 |
| `--limit-mem MB` / `--limit-vram MB` / `--limit-time SEC` | 资源上限 | 同上 |
| `--low-config` | 低配预设（64 / 32 / 10） | `:914` |

> ⚠️ **修正：`--no-gui` 不存在**。`docs/archive/API_REFERENCE.md` 把 `--no-gui` 列为常用选项，但 `src/main.c` 未找到该选项（请用 `--headless` / `--no-mods` 等实际存在的选项）。

### 10.2 子命令

| 子命令 | 行为 | 证据（`src/main.c`） |
|---|---|---|
| `debug <script.im>` | 优先 `vm.debug_script` DLL；否则把 `mods/debug/main.im` 接在用户脚本前 | `:1023` |
| `build <input.im> [output.exe]` | 需 `build` 模组；未安装报"打包功能未安装，请加 build 模组"；输入以 `.imbuild` 结尾时走 `build_project_impl`；缺省输出同名 `.exe` | `:1049` |
| `buildc` / `compile <input.im> [out]` | 编译为 `.inim` 字节码（见 §10.3） | `:1086` |
| `run <script.im\|script.inim> [args...]` | 运行脚本或字节码 | `:1138` |
| `profile <script.im> [out.prof]` | 性能剖析 | `:1160` |
| `symbols <input.im> [out.symbols]` | 导出符号表 | `:1184` |
| 无子命令 | 直接跑脚本 | `:1215` |

### 10.3 编译期选项

`src/main.c:934-954`（可置于子命令前或后）：`--abi-version N`、`--abi-target host|wasm|wasm32`、`--aot`、`--profile`、`--symbols`、`--incremental`、`--reproducible`、`--debug-info`、`--force|-f`。

| 选项 | 实际行为 | 证据 |
|---|---|---|
| `--abi-version N` | 与 `INIM_ABI_VERSION` 不符即报 `error: ABI version mismatch: requested %d, toolchain provides %d` 并退出 2 | `src/main.c:666-669` |
| `--abi-target wasm\|wasm32` | 走 Wasm 后端；缺省输出后缀变 `.wasm`；成功打印 `compiled: <in> -> <out> (wasm MVP subset)`；`run --abi-target wasm` 被拒（`error: wasm backend is not in the stable channel yet (v0.5 roadmap); run interpreted instead`，退出 2） | `src/main.c:700-708`、`:1123`、`:1145` |
| `--aot` | **打包通道**：解析 + 编译后把引擎自拷贝并追加字节码，产物运行时经 `bytecode_load_from_exe` 自执行。头注释明确 `The optimizing AOT backend remains future work`；`run --aot` 被拒（`error: \`run --aot\` is not supported; produce an executable with \`compile --aot\` first`） | `src/main.c:633-656`（`main_aot_package`）、`:1091-1110`、`:1140-1141`；实测打印 `aot: <in> -> <out>` |
| `--symbols` | 写 `<output>.symbols` | `src/main.c:745-750` |
| `--debug-info` | 写 `<output>.dbg` 与 `<output>.debug_line` | `src/main.c:752-758`；`src/compilation/debug_info.c`(156 行，头注释 "debug sidecar emission (text line table + DWARF 5 line program)")，`:123` 为 `%s.dbg`、`:148` 为 `%s.debug_line` |
| `--reproducible` | 写 `<output>.build.json` 并打印 `reproducible: ok (<sha256>)` | `src/main.c:606-630`（`main_write_build_record`）、`:761-765` |
| `--incremental` | 若已有依赖尾块且全部依赖的 SHA-256 仍匹配，跳过重建并打印 `up to date: <out>` | `src/main.c:673-695` |
| `--force` / `-f` | 跳过 `--incremental` 的短路 | 同上 |

### 10.4 产物、依赖尾块与 CTest

- **符号表** `<out>.symbols`（`src/main.c:584-604`）：文本，头为 `Inimerse Script Symbol Table` / `Script: <in>` / `Bytecode Format: INIMBC/<INIM_BYTECODE_VERSION>` / `ABI Version: <n>` / `Target: <abi_target>`，随后是 `Global Functions:` / `Threads:` / `Globals:` 三段。
- **`<out>.build.json`**（`src/main.c:606-630`）：`record_version`、`engine`（`"inimerse <INFIVERSE_VERSION>"`）、`bytecode_format`、`abi_version`、`target`、`bytecode_sha256`、`dependencies: [{path, sha256}]`。
- **`.inim` 依赖尾块**（`src/compilation/deps.c`，133 行）：`deps_write_trailer(bc_path, DepEntry*, count, abi_version)` 追加每项 `le32 path_len` + `path` + 64 字节 SHA-256 hex，最后 16 字节 footer（`DEPS_TRAILER_MAGIC`、abi、count、trailer_len）；`deps_read` 从尾部 `-16` 读 footer 校验 magic，`count > 65536` 或 `path > 4096` 判非法。路径**相对产物目录**记录（`deps_bc_dirname` + `deps_relative_path`），以保证相同工程布局在任何宿主上哈希一致（可复现构建）。
- **CTest**：`CMakeLists.txt` 共 **85** 个 `add_test(NAME ...)`。常用回归：`inim_regression`、`cli_incremental_regression`、`selfhost_benchmark`、`bindgen_regression`、`scan_tools_regression`、`wasm_backend_regression`、`release_verify_regression`；探针类：`jit_mode_probe`、`ed25519_probe`、`bytecode_capture_probe`、`verse_eventlog_probe`、`verse_layer_probe`、`verse_protocol_probe`、`verse_closed_loop`、`closure_probe`、`typeset_probe`、`enum_probe`、`error_types_probe`、`type_registry_probe`、`crp_session_probe`、`platform_probe`、`fiber_probe`、`process_probe`、`socket_probe`、`thread_probe`、`dir_probe`、`headless_probe`、`http_probe`、`hub_probe`、`wasm_probe`、`wasm_host`；协议/流程类：`hub_dist_regression`、`verse_pack_regression`、`node_discovery_regression`、`lease_handoff_regression`、`reconnect_generation_regression`、`economy_domain_regression`、`protocol_regression`、`replay_closure_regression`、`crp_session_flow_regression`。运行：`ctest --test-dir build --output-on-failure`。

> ⚠️ **修正**：`docs/archive/inimerse_compile_guide.md` 列的"测试探针"中有 **`websocket_probe`**，但 `CMakeLists.txt` 里没有该 CTest（`websocket.c` 在 POSIX 平台源列表；握手与帧层读/写都有实现，而该探针实测以 `rc=124` 挂住 —— 等不到回显）。

### 10.5 JIT 与性能（必须按此口径陈述）

**`--jit=template|optimized` 是解释器回退开关，不提供任何加速。** 证据：`src/vm/jit_mode.c` 全文仅 15 行，只做枚举与名字的互转（`im_jit_mode_parse` 认 `"template"`/`"optimized"`，`im_jit_mode_name` 输出）；`im_jit_mode` 只被 `src/main.c:826-828` 赋值、被 `src/vm/jit_mode_probe.c` 校验，**在执行路径上零引用**。

**`inimerse compile --aot` 是打包通道，不是原生代码生成。** 它复用同一个 C 解释器（引擎拷贝 + 追加已编译字节码），只是分发手段。

性能实测（源：`docs/archive/SELFHOST_BENCHMARK.md`；inimerse 0.5.0、Linux x86_64、Python 3.14.4；每例 5 次取中位与 P95；生成于 2026-09-29 13:49 UTC）：

| 通道 | 工作负载 `sum(1..2000000)` 中位 | 相对加速 |
|---|---|---|
| 解释器 | 88 ms | 1.00x |
| AOT 打包通道 | 81 ms | 1.09x（单点样本；分布式测量中位 **0.98x**，与解释器等同） |
| Wasm MVP（Node 宿主） | 58 ms | 1.51x |

排除了 Node 启动的数百毫秒基线（校准扣除空脚本基线）。**结论：`≥2x` 的目标属于未来的优化型 AOT 后端（真正的原生代码生成），当前三个通道都不满足；Wasm MVP 是无优化的直接翻译且宿主为 JS。** 回归门禁：任一用例的运行中位劣化 >20% 不得宣称发布。复现：`python3 tools/selfhost_bench.py --runs 5 --write-docs`。

同批基准的套例数据（编译中位/P95 ms，运行中位/P95 ms）：collections 2.0/2.6、32.3/47.0；case-try 2.3/35.1、29.2/62.6；vfs 2.2/36.3、132.9/148.2；selfhost 3.2/5.5、28.7/30.1。编译计时命令 `inimerse buildc <script> <out>`，运行计时命令 `inimerse run <out>`；规范化字节码哈希（产物 `.inim` 含依赖尾块的 SHA-256，跨宿主必须一致）分别以 `1edb937201a3a50f…`、`ca2a5a3dfbf0057d…`、`85c00d0b84beacde…`、`39167d8f6b3428d9…` 开头。

### 10.6 `inim-server` / `inim-client`（P1 最小 Layer 闭环）

两个独立的可执行文件，**只通过 stdin/stdout 上的一行一个 canonical-JSON 对象通信**。

```
inim-server <root> [verse_id]                       # 权威端；缺省 verse_id = "main"
inim-client [--server <path>] <root> <verse_id> [scenario]   # 客户端；缺省从 stdin 读剧本
```

请求（客户端 → 服务端）：

| op | 字段 | 说明 |
|---|---|---|
| `hello` | `protocol`（必须为 1）、`client`、`caps` | 建立会话。能力表由服务端给出：`drain`/`put`/`status`/`undo` |
| `put` | `key`（幂等键）、`cell`、`value` | 提交意图；`seq`/`rev` 由服务端分配 |
| `undo` | `key`、`target`（某条 put 的 seq） | 逆操作；同一 `key` 重复提交不再生效 |
| `drain` | — | 持久化屏障：flush 日志 → 写快照 → 复验锚点 |
| `status` | — | 读回 `seq`/`head`/`cells`/`rejected` |
| `bye` | — | 隐式排空并结束会话 |

错误码（`code` 字段，均可由客户端区分，**不存在静默降级**）：`no_session`、`malformed`、`protocol_mismatch`、`capability_refused`、`client_authority`、`bad_request`、`conflict`、`not_found`、`rejected`、`recovery_required`、`durability_failed`、`io`。

**结构性约束**：`seq`、`rev`、`head`、`balance`、`state_hash`、`committed` 是服务端权威字段。请求里出现其中任何一个即被拒绝（`client_authority`，且错误信息点名该字段），**不是被忽略**；内核层还有第二道防线——`vl_layer_put`/`vl_layer_undo` 自行分配 `seq`/`rev`，客户端自报不符则在写入任何字节之前 `VL_ERR_REJECTED`。

**会话规则**：**变更类操作**（`put`/`undo`）必须先成功 `hello`，否则 `no_session`。**只读/诊断类操作**（`drain`/`status`）**不需要会话**——这是有意的：当 Layer 因锚点不一致而拒绝 `hello` 时，运维恰恰需要这两个 op 来查看和推进恢复；而 `status` 本身也带锚点检查，不会把被篡改的值当成已提交状态呈现。

**锚点闸门**：Layer 的持久提交指针（`verse/<verse_id>/commit.head`）与从 `events.log` 重算出的链不一致时，服务端**不授予会话**——`hello` 返回 `code:"recovery_required"` 且不设置 `negotiated`，`status` 同样返回 `recovery_required`，`drain` 返回 `drained:false`。理由：日志的哈希链是**自洽**的，就地改写一条记录后重算的链依然自洽，唯一能识别篡改的就是这个外部指针；在没人能担保的状态之上发号（`seq`）会让客户端在坏状态上继续提交。<br>此前 `status` 会照常把篡改后的值返回给客户端（实测：把 `"value":2` 改成 `"value":9` 后 `status` 返回 `9`，只有 `drain` 报错）——这是第一版的一个真实缺口，已按上述规则收紧。

`inim-server` 退出码：`0` 干净结束、`2` 收尾时锚点校验不通过（Layer 处于不一致状态）、`3` 无法打开或创建 Layer。`inim-client` 的 `#crash` 指令会 `SIGKILL` 服务端子进程并自身退出 `137`；非 POSIX 平台不实现子进程派生，直接报未实现并退出 `4`。

验证：`verse_protocol_probe`（49 项进程内会话检查）与 `verse_closed_loop`（67 项，**跨真实子进程**覆盖 创建/进入/同步/排空/撤销/回放/崩溃恢复/篡改检测）。

### 10.7 引擎 C API 与常见错误

```c
Program  *parse_program(const char *source);
Program  *parse_program_file(const char *path);
Compiler *compiler_new(void);
void      compiler_compile(Compiler *c, Program *p);
Bytecode *compiler_get_main_bytecode(Compiler *c);
void      compiler_free(Compiler *c);
void      vm_load_bytecode(VM *vm, Bytecode *bc);
void      vm_run(VM *vm);
void      vm_params_load_v2_or_legacy(VM *vm, const char *path);
```

编译流程 6 步：`parse_program_file` → `compiler_new` → 打开 params 注册全局 → `register_global`（稳定索引）→ `compiler_compile` → `vm_load_bytecode` + `vm_run`。

常见错误串：`cannot load bytecode ... (old format? recompile with buildc)`、`cannot read script '...'`、解析错误（形如 `Error: expected 'expression', but got '<token>' (type <n>)`）。

> ⚠️ **修正**：`docs/archive/inimerse_compile_guide.md` 抄录的 `INIMERSE_ENGINE_SOURCES` 列表（约在第 147 行）**已过时**——它仍列 `src/vm/jit_mode.c` 而没有 `src/compilation/*`。当前实际列表见 `CMakeLists.txt:175-202`：公共核心含 `src/compilation/{checksum,deps,profiler,debug_info,wasm_backend}.c`，并按 `WIN32` / `else` 分支追加（见 §7.3 矩阵）。该文档引用的 `src/main.c` 行号（266-273 / 284-301）也已迁移。

---

## 11. 包格式

### 11.1 已实现

| 格式 | 事实 | 证据 |
|---|---|---|
| `.inim` | 编译后的字节码，魔数/格式标识 `INIMBC/<INIM_BYTECODE_VERSION>`，可带依赖尾块（§10.4） | `src/compiler/bytecode.c`、`src/compilation/deps.c` |
| `.imbuild` | `build` 子命令的输入后缀，走 `build_project_impl`（Windows 有 `build` 模组；POSIX 不可用） | `src/main.c:1049` |
| `manifest.json` | 必填 `name`/`version`/`entry`；可选 `dependencies`（对象） | `tools/inim.py:172-178`；`tools/vverse_validate.js` |
| `lock.json` | `{"lock_version": 1, "packages": {...}}`；`lock_version != 1` 报 `inim: unsupported lock.json format` | `tools/inim.py:190,504` |
| `.inim-cache/` | 归档缓存 `<target>/.inim-cache/archives/<sha256小写>.inim`；安装落地 `<target>/.inim-cache/<name>/<version>` | `tools/inim.py:212-215,276` |
| 离线安装 | `install --offline` 按 `lock.json` + 归档缓存的 SHA-256 重放 | `tools/inim.py`（`--offline`） |
| 签名 | Ed25519：`keygen` / `publish --signing-key` / `verify --require-signature --trusted-key` | `tools/inim.py` |
| `.vverse` | JSON 容器 `{"format": "vverse-1", "files": {<相对路径>: <base64>}}` | `tools/vverse_pack.js:15,22,34`（`pack`/`unpack`/`preview`） |
| `.vverse` 结构校验 | 需 `manifest.json` + `blueprint.json`；`strictStructure` 时还要求 `laws`/`assets`/`mods`/`signatures` 四个目录；`manifest.entry` 不得逃出包根 | `tools/vverse_validate.js` |
| `.vverse` 签名 | `signatures/sha256.json`（逐文件 SHA-256 清单）+ `signatures/ed25519.json`（`algorithm`/`publicKey`/`signature`，签署 sha256 清单按文件名字典序的 JSON） | `tools/vverse_validate.js`（`writeSignature`/`signaturePayload`）、`tools/vverse_pack.js` |
| `.vverse` 错误串 | `invalid vverse package`、`digest mismatch: <name>`、`unsigned file: <name>`、`missing signatures/sha256.json`、`invalid ed25519 signature`、`ed25519 signature verification failed` | 同上 |

`inim` 包管理器（`tools/inim.py`，525 行）**真实**子命令：`init`、`pack`、`install`、`add`、`remove`、`run`、`publish`、`verify`、`update`、`doctor`、`list`、`bootstrap`、`keygen`（`tools/inim.py:510-523` 的 argparse 定义）。`pack` 缺省产物名为 `<name去掉斜杠>-<version>.inim`，本质是 zip（内含 `manifest.json`），打包时跳过 `.inim-cache`、`lock.json`、`manifest.json`。

> ⚠️ **修正：没有 `uninstall` 子命令**（叫 `remove`）；也**没有** `bundle`、`params`、`gc`、`graph` 子命令。

CTest：`verse_pack_regression` = `tools/verse_pack.test.py`（另有 `tools/vverse_pack.test.js`、`tools/vverse_validate.test.js`）。

### 11.2 `.params`（兼容格式，已实现）

`src/vm/params_v2.c`（56 行）实现 V0.4 分节格式：

- 必须出现 `params_version = 2`；`[section]` 必须独占一行；`key = value`；支持 `#` 与 `//` 行注释；无 `=` 报错；key 仅限 `[A-Za-z0-9_]`。
- lowering 生成**受限赋值** `%s%s%s = %s;\n`（有 section 前缀时拼成 `section_key`）；`vm_params_v2_lower(const char*)` 返回 malloc 串或 NULL，`vm_params_v2_lower_into(text, out, cap)` 供工具探测。
- `vm_params_load_v2_or_legacy(VM*, path)` 读文件（>1 MiB 或读取失败则回落旧的 `vm_params_load`），lowering 成功则写临时文件 `<path>.v2.tmp` 交给 `vm_params_load` 后删除。
- 样例：`vtest/params_v2.params`（`params_version = 2` / `[engine]` / `speed = 42` / `title = "hello"`）。

状态：部分实现；**无 params 相关 CTest**。

`docs/archive/PARAM_FORMAT.md` 另描述的 `.params` 运行时覆盖 API（`load_params`/`list_params`/`save_params`，每行一个赋值、右侧为 Inimerse 表达式、<1 MiB、解析失败整文件拒绝）**在源码中无实现证据**。

### 11.3 `.param` 项目清单（v0.4 提案）—— 设计未实现

`docs/archive/PARAM_FORMAT.md` 的第二类格式 `.param`（YAML 子集；根键 `version`/`project`/`sources`/`bundles`/`profiles`/`overrides`，未知键报错）以及配套 CLI **在仓库中零实现证据**：

- `inim bundle resolve <project.param> --lock <file>`、`inim bundle graph <project.param>`、`inim bundle verify <lock> --offline`、`inim bundle gc --dry-run`、`inim params validate <file>`、`inim params print --resolved --redact` —— 全部无实现（`grep` 无命中，且 `tools/inim.py` 的子命令表里没有这些）。
- `.param.lock`、RID（`sha256:<hex>` 内容摘要）、package（名称/版本/ABI/平台能力/依赖/导出符号）、bundle（有序依赖图按 RID 去重、求版本交集、报冲突）、`resource ... as` 逻辑挂载、profile/default 解析顺序、`INIM_PLAYER__SPEED` 式环境变量双下划线映射 —— 全部为**设计意图**。
- `import`（模块符号，可缓存、有 ABI 检查）与 `include`（源码/模板文本展开，仅本地显式路径）的边界也属提案。

保留这些内容是为了记录设计意图；**不要把它们当作可用功能**。

---

## 12. 协议草案（`docs/archive/protocol_v1.md`）

**定性：设计文档，不是已实现的协议。** 该文件顶部自述"本文件为设计/规范文档，实现状态可能已变化"，并曾把权威总览指向 `docs/archive/API_REFERENCE.md`（现已由本文件取代）。

### 12.1 帧协议 v1（headless 热联机，设计）

服务器 → 客户端：每渲染帧**一行 JSON**（`\n` 分隔），字段：`f` 帧号、`w`/`h` 视口、`t` 文本 `[{x,y,s}]`、`r` 形状 `[{t,x,y,w,h,c,s}]`、`c` 控件、`sp` 精灵元数据 `[{n,x,y,w,h,c}]`。`r[].t` 形状类型：`0` 矩形填充 / `1` 边框 / `2` 横线 / `3` 竖线 / `4` 文本 / `5` 面板 / `6` 圆角。每帧全量（可断线重连），慢客户端丢帧（非阻塞 send + `WSAEWOULDBLOCK` 丢弃）。

客户端 → 服务器：`{"key":"left","down":1}`、`{"mouse":"move","x":123,"y":45}`、`{"mouse":"click",...,"btn":1}`。

统一语义（设计）：`send_frame(json)` / `poll_input()` / `open_uri(verse://...)`。

### 12.2 传输后端

| 后端 | 状态 | 证据 |
|---|---|---|
| TCP | ✅ **现役** | `src/headless_server.c`（Windows）/ `src/headless_server_posix.c`；CTest `headless_probe`。房间端口规律：`11510 + 10n`（`11510 ≤ port < 11700`），证据 `src/mod/server_mod_posix.c:24,36,82` |
| HTTP | ✅ **现役** | verse hub / headless http api；引擎默认 `--http-port 11470`（`src/main.c:818,850`）；CTest `http_probe`、`hub_probe` |
| WebSocket | ⬜ **预留** | 握手与帧层读/写都已实现（`im_ws_accept` / `im_ws_read_text` / `im_ws_send_text` / `im_ws_send_pong`）；**探针等不到回显**（`websocket_probe` 实测 `rc=124`，观测点 `main @ 124a213`）；**无 `websocket_probe` CTest** |
| UDP | ⬜ **预留**（NAT 打洞） | 无实现证据（POSIX 侧 `net_mod_posix.c` 也没有 `udp_*`） |
| IPFS | ⬜ **预留**（`verse://ipfs/<cid>`，CID = SHA-256） | 无实现证据 |

> ⚠️ `docs/archive/protocol_v1.md` 列出的端口 `11490/11500/11510/11530`（TCP）与 `11470/11520/11540`（HTTP）**只有部分能在源码中核对到**：`11470` 是引擎默认 HTTP 端口，`11510 + 10n` 是 POSIX `server_mod` 的房间端口规律；其余端口号属设计值（未核对）。

### 12.3 三模式与安全边界（设计）

| 模式 | 状态 |
|---|---|
| 单机 | ✅ 已实现 |
| 热联机 | 帧协议 v1 设计已就绪；其中 TCP/HTTP 传输已实现，帧字段级契约（`f/w/h/t/r/c/sp`）**未逐字段核对** |
| 冷联机 | ✅ 已实现（`verse_open` 校验 / 解包 / 启动，`ref://sha256` 素材复用） |

远程调试安全边界（设计）：本地 stdin 控制台无网络面，`--safe` 已拦注入；未来远程 attach 必须 `--debug-token` + 只读命令 + `safe_mode`，独立端口 + token 握手（HMAC），**禁止与帧流复用**；`vm_exec`/`dbg_exec` 已注册为危险 builtin。

素材引用链（设计）：verse 素材缓存 `universe/_cache/<sha256>` → `ref://sha256` → 帧流 `sp[].c` → 客户端缓存解析 → im2d 精灵渲染，同一 sha256 全域唯一。

---

## 附录 A：与源文档的差异总表

| 源文档 | 处理 | 具体差异 |
|---|---|---|
| `docs/archive/API_REFERENCE.md` | 保留 + 修正 | 删去不存在的 `--no-gui`；补证据 |
| `docs/archive/API_CATALOG.md` | 保留为主干 | 状态词、平台 C API、弃用项保留；`--jit` 按"解释器回退"口径重述 |
| `docs/archive/API_BUILTIN_TABLE.md` | 由源码重新生成 | 见 §5.1 的 6 处差异（gui 重复计数、verse_dist/identity 漏列、thread_* 归属、3 个模组未收录、runtime 平台差异） |
| `docs/archive/SYNTAX_SUGAR.md` | 修正 | ① 第 18 行"`?.`/`??` 当前仍是设计项"**已过时**（两者均已实现，见 §2）；② `fn`/`print`/`&&`/`\|\|`/`//`/`unless` 属**内糖**而非纯外糖（§3.1） |
| `docs/archive/PORTABILITY.md` | 修正 | "Makefile 门禁包含 …_probe"**已过时**：根 Makefile 只有一个 `bytecode_capture_probe` 目标，探针现由 CTest 承载（§7.4） |
| `docs/archive/WASM.md` | 修正 | `make wasm` 目标**不存在**；工具链探测脚本 `tools/wasm_check.js` 存在（§7.4、§8.4） |
| `docs/archive/WASM_ABI.md` | 限定范围 | 其 `0x0400` 属于 `tools/wasm_probe.c` 这套独立探针；`--abi-target wasm` 后端用 `0x0500`（§8.2、§8.3） |
| `docs/archive/NUMERIC_MODEL_V04.md` | 降级为设计 | 数值塔 `Z ∪ Q ∪ D ∪ F` **未实现**，全文标为意图（§9.2） |
| `docs/archive/PARAM_FORMAT.md` | 拆分 | `.params` 分节格式已实现（`params_v2.c`，无 CTest）；`.param` 与 `inim bundle/params` CLI **全部设计未实现**（§11.3） |
| `docs/archive/inimerse_compile_guide.md` | 修正 | `INIMERSE_ENGINE_SOURCES` 列表与 `src/main.c` 行号已过时（§10.6）；其探针清单中的 `websocket_probe` 不存在（§10.4） |
| `docs/archive/protocol_v1.md` | 明确标注为设计文档 | 仅 TCP/HTTP 传输现役；WebSocket/UDP/IPFS 预留（§12） |

## 附录 B：常用核对命令

```bash
# 内建函数名清单（按模组）
grep -a -oE 'vm_register_builtin(_full|_safe)?\s*\(\s*\w+\s*,\s*"[^"]+"' src/mod/<mod>_mod.c \
  | sed 's/.*"\(.*\)"/\1/' | sort -u

# 全量 CTest 列表
grep -n 'add_test(NAME' CMakeLists.txt

# 跑测试
ctest --test-dir build --output-on-failure

# 可移植性自检
./build/inimerse capabilities

# 外糖转换（显式）
./build/inimerse --desugar in.im out.im
```
