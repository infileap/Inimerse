# Inimerse 语言缺陷审计与执行通道效率报告

本文件是「全方位检查语言 bug + 解释运行 / JIT / 编译运行 / C++ / Rust 的效率与资源占用比较 + 优化方案」这一轮的交付物。全部数字与结论都在本仓库当前树上实测得到，复现命令见 §6。

- §1 是**语言缺陷审计**：四条已复现的缺陷（外加一个差分模糊测试装置）。最严重的一条 —— `and` / `or` 在两个后端返回不同的东西 —— 是模糊测试找出来的，不是手写探针：手写探针只能找到作者已经怀疑的东西。
- §2–§4 是**效率与资源比较**：五条真实存在的执行通道（解释器 / AOT / wasm / C++ / Rust），五个工作负载，墙钟与峰值 RSS。
- §5 是**优化方案**：按 效果 ÷ 风险 排序，每条给出验证方式。

---

## §0 方法与取证环境

| 项 | 值 |
| --- | --- |
| 引擎 | `build/inimerse`（881,168 B） |
| AOT 转译器 | `build/aot-native`（877,192 B） |
| C / C++ | gcc / g++ 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| Rust | rustc 1.99.0 (b940084d7 2026-09-28) |
| 测量装置 | `tools/perf_channels.py`，工作负载在 `tools/bench/channels/` |
| 墙钟与内存 | `os.wait4()` 取**单个子进程**的 rusage；每个通道 1 次 warmup + 5 次测量取中位数 |

**为什么不用 `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss`**：它是「所有已回收子进程的历史最高水位」，第二次运行会静默继承第一次的峰值 —— 用它测出的内存曲线是单调不降的假象。

**两道闸门，任何一道不过就不报时间**：

1. **正确性闸门** —— 同一 N 下所有通道必须打印同一个整数。一个算的是别的东西的通道，不是更快的通道。
2. **缩放闸门** —— 每个通道额外在 `scale_n(name, n)` 处再测一次，时间必须增长到 `SCALING_MIN = 1.4` 倍以上；不增长即报 `OPTIMIZED AWAY` 并丢弃该通道。理由见 §2.3。

---

## §1 语言缺陷审计

### 1.0 `and` / `or` 在两个后端返回不同的东西 —— 本轮最严重的一条（**已修复：布尔语义**）

**这条不是手写探针找到的，是差分模糊测试找到的**（`tools/im_diff_fuzz.py`，见 §1.5）。手写探针只能找到作者已经怀疑的东西。

**位置**：`src/compiler/compiler.c:648-670`（解释器侧）与 `src/compilation/aot_native.c:334-339`（AOT 侧）。

解释器的编译器把 `and` / `or` 编译成**短路跳转并返回操作数**：

```c
if (expr->binary.op == TOK_AND) {
    jmp_pos = comp->curBC->count;
    emit(comp->curBC, OP_JUMP_IF_FALSE, result, 0, 0);
    int right = compile_expr(comp, expr->binary.right);
    emit(comp->curBC, OP_MOV, result, right, 0);
} else {
    jmp_pos = comp->curBC->count;
    emit(comp->curBC, OP_JUMP_IF_TRUE, result, 0, 0);
    int right = compile_expr(comp, expr->binary.right);
    emit(comp->curBC, OP_MOV, result, right, 0);
}
```

AOT 则把同一个运算符编译成布尔：

```c
if (op == TOK_AND || op == TOK_OR) {
    buf_str(b, "nv_boo(nv_tru(");
    ...
    buf_str(b, op == TOK_AND ? ") && nv_tru(" : ") || nv_tru(");
```

**实测**（`/tmp/e13/probe/orv.im`，两列都是真实输出）：

| 表达式 | 解释器 | AOT |
| --- | --- | --- |
| `31 or 1` | `31` | `true` |
| `0 or 2` | `2` | `true` |
| `1 and 5` | `5` | `true` |
| `0 and 5` | `0` | `false` |

解释器给的是**操作数**（Python / JS / Lua 的值语义），AOT 给的是**布尔**。

**这个落差为什么难被发现**：`src/vm/vm.c:3166-3179` 的 `L_AND` / `L_OR` **确实是布尔语义的**（两个操作数取真值后存 `VAL_BOOL`）。也就是说 VM 的这两个 opcode 与 AOT 一致 —— 但**编译器根本不发它们**。`OP_OR` 在 `src/compiler/bytecode.h:12` 声明、**全仓从未被 emit**（`grep -rn "OP_OR" src/compiler/` 只命中声明）；`OP_AND` 只在 `src/compiler/compiler.c:827` 被用于**链式比较**（`1 < x < 10`）。于是「VM 的 opcode 是布尔语义」这件事对 `and`/`or` 运算符毫无影响，读 VM 代码的人会得出与运行时相反的结论。

**文档是沉默的**：`docs/API.md:90` 只写 `and` `or` `not` 是「逻辑」，没有说返回操作数还是布尔。两边都「符合文档」，所以没有任何一方报错。

**实际后果**：`x = a or default` 是常见的默认值写法。解释器下 `name or "anonymous"` 给 `anonymous`（实测），AOT 下同样的代码得到 `true` —— 编译后的程序静默地不再有默认值语义。

**严重度**：最高。触发条件是**最常用的逻辑运算符**，两个后端静默不一致，且读代码无法看出哪边是对的。

**补充（实测，见 §1.6）**：这条其实是**三方**分歧 —— 解释器给操作数、AOT 给布尔、**wasm 直接拒绝编译**。wasm 的加入使「挑一个后端对齐」这个提法失效。

**修复（用户裁定：布尔语义）**：`and` / `or` 一律产出**真值**，同时**保留短路**。三个后端各改一处，语义由 `tools/logic_semantics.test.py`（64 例，三后端逐格比对）钉住：

| 后端 | 位置 | 改法 |
| --- | --- | --- |
| 解释器编译器 | `src/compiler/compiler.c:648-672` | 跳转路径上结果已知真假，故用 `OP_AND`/`OP_OR` 对两操作数求真值（恰好等于右操作数的真值）；短路路径发射 `OP_LOADK_BOOL` 常量 |
| `.im` 自举编译器 | `selfhost/compiler.im:254-268` | 同形状，`OP_AND = 16`/`OP_OR = 17`/`OP_LOADK_BOOL = 4`/`OP_JUMP = 24` |
| wasm | `src/compilation/wasm_backend.c:1237-1246` | 原来是硬拒绝；`cg_cond` 本来就发真值（含短路），直接复用 + `e_store_bool_from_stack` |
| AOT | `src/compilation/aot_native.c:334-339` | 本来就是布尔，**未改** |

**爆炸半径（先测后改）**：`git ls-files '*.im'` 全量分类 —— 条件位置 71 处、值位置但两侧已是布尔 27 处、字符串/注释假命中 11 处、**真正会变的值选择只有 1 处**（`t_sugar_desugared.im:7` 的 `k = 0 or 1`，由 `k=1` 变 `k=true`；该文件被跟踪但不被任何 CTest 或门禁引用）。改后全量 CTest **106/106 通过、0 失败**。

**顺带发现的第二个真缺陷**：写这个测试的当天，它抓出 **wasm 的 `not` 是反的** —— `cg_cond` 在 `TOK_NOT` 分支里已经做了一次 `i32.eqz`，`cg_expr` 的 `TOK_NOT` 分支又做了一次（双重否定），于是 `not 0` 在 wasm 上打印 `false`，而解释器与 AOT 都打印 `true`。**六条 `not` 用例全部恰好相反**。这是既有缺陷，与本次改动无关（`git diff` 可证 `cg_expr` 的 `TOK_NOT` 分支未被本次修改触及）。修法：删掉多余的那次 `eqz`（`src/compilation/wasm_backend.c:1217-1231`）。

**为什么这个测试值钱**：它不是「再写一遍期望值」，而是**三个后端各跑一遍再互相比对**。`not` 那条在解释器和 AOT 上一直是对的，任何只测一个后端的测试都不会发现它。

### 1.1 `%` 在浮点路径把两个操作数截成 32 位 `int` —— 静默算错，且与 AOT 分歧（**已修复：三后端 64 位收敛**）

**位置**：`src/vm/vm.c:3824-3838`（`L_MOD`；初稿写的 `:3815` 是错的，见 §1.6 的更正与两条路径）。

**补充（实测，见 §1.6）**：wasm 通道加进来之后，这条是**三后端三答案**（`3000000000 % 7` → 解释器 `-2` / AOT `4` / wasm `1`），而且「除数为 0」也有三种态度（报错 / 静默 0 / 报错）。

```c
int bi = (int)val_as_double(b);
if (bi == 0) { vm_throw_kind(vm, "division_by_zero"); ... }
double da = val_as_double(a);
value_set(&R[ins.r1], VAL_INT, (int)da % bi, 0, NULL, NULL);
```

**机制**：`(int)da` 在 `da` 超出 int32 范围时是 C 的未定义行为。x86-64 的 `cvttsd2si` 对越界输入返回整数不定值 `0x80000000`（即 -2147483648），于是取余按这个负数进行，**静默给出负结果**。除数一侧同理：`(int)val_as_double(b)` 先把 b 截断，再拿截断后的值当模数。

**实测**（预测值逐一命中，非事后解释）：

| 表达式 | 本引擎 | 正确值 |
| --- | --- | --- |
| `2147483648 % 7` | `-2` | `2` |
| `3000000000 % 10` | `-8` | `0` |
| `2147483648 % 3` | `-2` | `2` |
| `2330089441 % 2147483647` | `-1` | `182605794` |
| `3.9 % 2` | `1` | `1.9`（小数部分被丢掉） |
| `10 % 3000000000` | `10` | `10`（除数被截成 INT_MIN，恰好不影响） |

**与 AOT 分歧**：`src/compilation/aot_native.c` 的 `nv_mod` 用的是 `long long`：

```c
long long x = nv_asi(a), y = nv_asi(b);
return nv_int(y ? x % y : 0);
```

同一份源码 `2330089441 % 2147483647`，**AOT 给 `182605794`（正确），解释器给 `-1`**。也就是说「同一门语言的两个后端对同一程序给出不同答案」，且没有任何一端报错。

**严重度**：高。静默、无警告、两个后端不一致，且触发条件是**普通的模运算**（哈希、分桶、ID 生成、LCG 随机数都是这个形状）。

### 1.2 整数运算静默退化成 double，2^53 以上静默失精

**机制**：任何**超出 int32 范围的整数结果**都被静默提升为 double。实测类型标记：

```
(1 + 1).type            -> int
(2147483647 + 1).type   -> float      # 值 2147483648 本身是对的
```

double 的尾数只有 53 位，所以超过 `2^53 = 9007199254740992` 之后**相邻整数不再可表示**，加法与减法开始静默丢失低位：

| 表达式 | 本引擎 | 正确值 |
| --- | --- | --- |
| `9007199254740992 + 1` | `9007199254740992` | `9007199254740993` |
| `20000000000000000 - 1` | `20000000000000000` | `19999999999999999` |
| `3037000500 * 3037000500` | `9.2233720370002493e+18` | `9223372037000250000` |

**只有字面量越界会警告**（stderr 打 `warning: integer literal … out of 32-bit range, promoted to float`），**计算产生的溢出完全静默**。所以一段「看起来是整数」的累加代码会在某个规模上悄悄开始算错，而编译器、解释器、运行时都不会说一个字。

**端到端**：`/tmp/e13/probe/big.im` 累加 `0..199999999`：

| 通道 | 结果 |
| --- | --- |
| 解释器 | `19999999867108864`（**错**） |
| AOT | `19999999900000000`（对） |
| 精确值 | `19999999900000000` |

同一个分歧来源：AOT 的 `NV.i` 是 `long long`，**没有 int32 溢出 → float 提升**。

**严重度**：高。静默、无警告、与原生后端不一致，且这是「任何规模足够大的整数计算」都会遇到的形状。

### 1.3 `vm.c:3637` 的空体 `if` 把函数索引的越界检查吃掉了

**位置**：`src/vm/vm.c:3637`。

```c
if (fidx >= 0 && fidx < root->func_count && root->func_names[fidx] && strncmp(root->func_names[fidx], "h", 1) == 0)
if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) { ... }
```

第一行**没有花括号也没有语句**，所以第二行那个完整的越界检查变成了它的**函数体**。后果只有一条：

1. 越界检查**只在函数名以 `h` 开头时才执行** —— 其余情况下会继续往下走到 `src/vm/vm.c:3666` 的 `int param_slots = root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc;`，越界读。

> **一处自我更正**：本节初稿还写了第二条「第一个条件自身就对越界 `fidx` 做 `root->func_names[fidx]`，在检查之前就已经越界读」。**这是错的。** 该条件是 `fidx >= 0 && fidx < root->func_count && root->func_names[fidx] && strncmp(root->func_names[fidx], "h", 1) == 0`，C 的短路求值保证 `root->func_names[fidx]` 只在边界检查通过之后才求值，**不存在越界读**。缺陷只有上面那一条。

该文件其余部分的错误串是 GBK 乱码（`閿欒�? 鏃犳晥鍑芥暟绱㈠�?%d`），这条缺陷混在里面更难被读到。

**严重度**：中高。内存安全问题，而且**不需要恶意字节码文件**就能触发。

**已修复（本轮）**。`vm_exec` 接受运行期手工构造的字节码（`src/runtime/vm_exec_builtin.c` 的 `bc_from_data`），所以五行 `.im` 就能构造出越界调用：

```im
bc = {"code": [[34, 999, 0, 0], [33, 0, 0, 0]], "strings": [], "floats": [], "funcs": []}
say vm_exec(bc)
```

`OP_CALL_FUNC` = 34、`OP_HALT` = 33（`src/compiler/bytecode.h:8-23`）。`funcs` 为空 ⇒ `fidx = 999` 全面越界。

| | 修复前 | 修复后 |
| --- | --- | --- |
| `inimerse --no-mods badfidx.im` | **`EXIT=139`（Segmentation fault）** | `EXIT=1`，stderr 报「无效函数索引 999」 |

修复是删掉那个游离的 `if`，让守卫无条件执行（`src/vm/vm.c:3642`）。进树回归 **`tools/vm_bad_fidx.test.py`**（ctest 名 `vm_bad_fidx_regression`）断言：不被信号杀死、退出码非 0、stderr 里出现 `999`，并附一个对照程序（真实函数调用仍须返回 `7`），使「VM 拒绝一切」无法蒙混过关。**双向验证**：把守卫按原样重新包进 `if (... strncmp(root->func_names[fidx], "h", 1) == 0)` 后重建 → 测试报 `the out-of-range call index crashed the VM (signal 11)`；恢复修复后 → `vm bad fidx: ok (out-of-range call index refused, control call works)`。

### 1.4 已核对但不构成本轮新缺陷的行为

以下都已在 `docs/SYNTAX.md` 的 D/M 类里记录过，本轮逐条实测确认，**不作为新缺陷立项**：

| 行为 | 实测 |
| --- | --- |
| 未声明变量 | `zzz` → `nil`（静默） |
| 集合越界 | `a[99]` → `nil`（静默） |
| 字符串 + 数字 | `"5" + 1` → `51`（强制转字符串拼接） |
| 跨类型相等 | `1 == "1"` → `false` |
| 跨类型比较 | `1 < "1"` → `false`（不报错） |
| 整除 | `4/2` → `2`、`7/2` → `3.5`（精确整除才给 int） |
| 负数取余 | `-7 % 3` → `-1`（C 语义，非 Python 语义） |
| 除零 | `7 % 0` → 抛 `division_by_zero`，未捕获时 **exit code = 1**（正确） |
| `int()` 截断 | `int(3.9)` → `3`、`int(-3.9)` → `-3`（向零截断） |

### 1.5 差分模糊测试：这一节才是「全方位」的方法

上面三条是手写探针找到的。手写探针有一个结构性缺陷：**它只能找到作者已经怀疑的东西**。`and` / `or` 那条（§1.0）之所以能出来，靠的是 `tools/im_diff_fuzz.py` —— 它生成随机程序，同一份源码分别送进解释器与 AOT（`aot-native translate` → `cc -O2` → 运行），逐对比较输出。

生成范围限制在 AOT 支持的子集内（int/float/bool、`+ - * / %`、比较、`and/or/not`、`if/else`、`while`、`repeat`、`break`、赋值、用户函数、全局、`say`），所以**「翻译失败」会被报成生成器 bug，而不是记成一次分歧** —— 这两种东西必须分开，否则模糊测试的每一轮都会把「AOT 不支持这个语法」算成「后端不一致」。

它区分两类发现，因为它们是不同的问题：

- `DIVERGE` —— 两个后端都给出了值，而值不同。
- `THREW` —— 一个后端拒绝执行（未捕获异常），另一个没有。

常量刻意选在 int32 与 double 停止一致的那些边界上（`2147483647`、`2147483648`、`9007199254740992`、`3037000500`…），除数为零的字面量在生成时就避开（`division_by_zero` 是有文档的行为，不是分歧），但**求值产生的零**不避开 —— 那种情况下两个后端怎么处理正是要看的东西。

**结果**（`--count 150 --seed 7`）：

| | 数量 | 占比 |
| --- | --- | --- |
| 两个后端一致 | 109 | 72.7% |
| `DIVERGE`（都有值，值不同） | **31** | **20.7%** |
| `THREW`（一边拒绝、一边没拒绝） | **10** | **6.7%** |
| 无法翻译 | 0 | 0% |

也就是说，**在 AOT 自己文档化的子集内随机生成的程序里，超过四分之一（41/150 = 27.3%）在两个后端上给出不同结果。**

**按根因归类这 41 个案例**（`/tmp/e13/fuzz3/`）：

| 根因 | 案例数 |
| --- | --- |
| 含 `and` / `or` ⇒ §1.0 的值语义 vs 布尔 | **28** |
| 含 int32 边界常量或 `%` ⇒ §1.1 / §1.2 | 13 |
| **无法归因** | **0** |

**「无法归因 0」这一行比总数更重要** —— 它说明这些分歧没有第五个未知机制在背后，四条已记录的缺陷就解释完了全部 41 例。68% 的分歧来自 §1.0，这也独立地确认了它是本轮最严重的一条。

**一次生成器自身的教训**：第一版生成器固定给 `f` 传两个实参，而形参数量随机取 1–2，于是 44 个程序在 `aot-native translate` 阶段被拒（`aot-native: function 'f' takes 1 argument(s) but is called with 2`），全被记成「无法翻译」。这既掩盖了真实的分歧率，也掩盖了一个顺带的观察：**解释器不检查实参个数**（`docs/SYNTAX.md` 已记录的 M 类危险），而 **AOT 在翻译期就拒绝** —— 同一个程序解释器照跑、编译不过。修好生成器后「无法翻译」归零。

**这个工具的定位是长期资产，不是一次性的**：修 §1.0 / §1.1 / §1.3 之后应当重跑它，用它来确认分歧数下降；`tools/aot_native.test.py` 里那三条 `DIVERGENCE` 钉死项也应当由它来复核。

**2026-10 复核（这一条待办已经结清）。** §1.13 把它接进了 `tools/gate.sh`，§1.14 把 pin 从 `5 / 4` 抬到 `0 / 0`。上表那个 27.3% 现在是 **0**。为了确认归零不是「种子 1 恰好干净」，另外跑了种子 2–10、每个 `--count 150`（9 × 150 = **1350** 个随机程序），逐种子都是 `150 agreed / 0 DIVERGE / 0 THREW / 0 not translated`、`rc=0`（第 11 个种子被 50 分钟超时切断，没有结论）。三个 `DIVERGENCE` 钉死项也已由这些跑复核过。

**这个工具覆盖不到什么，必须写在明处。** 生成范围就是上面那句「AOT 支持的子集」——int/float/bool 与算术、比较、`and/or/not`、控制流、函数、全局、`say`。**集合、数组、字典、字符串一律不在里面**（`src/compilation/aot_native.c` 的 `emit_expr` 只认 `EXPR_NUMBER`，连 `EXPR_STRING` 都没有 case，AOT 会直接拒绝整个程序）。所以「`0 DIVERGE`」的意思是**那个子集内两个后端一致**，不是「语言没有缺陷了」—— §1.15 / §1.16 / §1.17 那三条静默错答案全都在这个工具的能力之外，是读码与手写探针找到的。这与 §1.13 的结论是同一条：**这个装置测不出「全绿」，只能测「没变宽」。**

---

### 1.6 三通道差分实测：`and`/`or` 与 `%` 的真实爆炸半径

§1.0 与 §1.1 的表格是**两个后端**（解释器 vs AOT）的对比。把 wasm 通道加进来之后，两条缺陷的形状都变了 —— 它们不是「两个后端不一致」，是**三个后端三个答案**。本节是实测，不是读码。

**方法**：一个一次性的三通道探针（`/tmp/e13/backend_probe.py`，**尚未进树**），把一段 `.im` 依次跑在

- 解释器：`./build/inimerse --no-mods p.im`
- AOT：`./build/aot-native translate p.im p.c` → `cc -O2` → `./p.native`
- wasm：`./build/inimerse compile --abi-target wasm p.im p.wasm` → `node tools/wasm_run.js p.wasm`

20 个用例里 **12 个分歧**。

#### `and` / `or`：三种行为，不是两种

| 表达式 | 解释器 | AOT | wasm |
| --- | --- | --- | --- |
| `1 and 5` | `5` | `true` | **编译期拒绝** |
| `2 and 3` | `3` | `true` | **编译期拒绝** |
| `0 and 5` | `0` | `false` | **编译期拒绝** |
| `1 and 0` | `0` | `false` | **编译期拒绝** |
| `0 or 5` | `5` | `true` | **编译期拒绝** |
| `1 or 5` | `1` | `true` | **编译期拒绝** |
| `2 or 3` | `2` | `true` | **编译期拒绝** |
| `0 or 0` | `0` | `false` | **编译期拒绝** |

wasm 的原文：

```
error: wasm MVP subset: 'and'/'or' outside a condition is not supported (use it in if/while) (line 1)
```

也就是说：解释器给**操作数**，AOT 给**布尔**，wasm **拒绝编译**。三条路都对「`and`/`or` 返回什么」给出了自己的答案，其中一条是「不回答」。

AOT 的布尔语义在生成的 C 里看得见：`nv_say(nv_boo(nv_tru(nv_int(1LL)) && nv_tru(nv_int(5LL))));`。

**这改变了 §1.0 的结论**：之前写「VM 的 `L_AND`/`L_OR` 是布尔语义，与 AOT 一致，只是编译器不发它们」—— 那还是把问题当成两方之争。现在是三方，而 wasm 那一方连值都不产出。**O0 不是「挑一个后端对齐」，是「先定这门语言的 `and`/`or` 返回什么」**，因为三个后端里已经有两个各自定了。

#### `%`：三个后端三个答案，且其中两个是 UB 或饱和

| 表达式 | 解释器 | AOT | wasm | 正确值 |
| --- | --- | --- | --- | --- |
| `7 % 3` | `1` | `1` | `1` | 1 |
| `-7 % 3` | `-1` | `-1` | `-1` | -1 |
| `7 % -3` | `1` | `1` | `1` | 1 |
| `2.5 % 1` | `0` | `0` | `0` | 0（三个都丢小数） |
| `7 % 0.5` | **异常** `division_by_zero`（rc=1） | `0` | **异常** | 0 |
| `5 % 0` | **异常** `division_by_zero`（rc=1） | `0` | **异常** | 未定义 |
| `3000000000 % 7` | `-2` | `4` | `1` | **4** |
| `2147483648 % 7` | `-2` | `2` | `1` | **2** |
| `7 % 3000000000` | `7` | `7` | `7` | 7 |
| `10 % 4294967296` | `10` | `10` | `10` | 10 |
| `0 % 5` | `0` | `0` | `0` | 0 |

**wasm 为什么给 `1`** —— 这条值得单独记，因为它的形状与解释器**不同**。wasm 后端有两条 `%` 路径（`src/compilation/wasm_backend.c:920-965`）：

- **`int % int` 快路径**（两个操作数都带 `TAG_INT`）：`W_I64_REM_S`，**64 位，正确**。
- **一般路径**（注释原文 `general: (int)da % (int)db`）：`e_push_as_double` → **`e_trunc_sat_i32`** → `W_I64_EXTEND_I32_S` → `W_I64_REM_S`。

大整数文字被提升成 double（解释器自己会警告 `warning: integer literal 3000000000 out of 32-bit range, promoted to float`），所以走的是**一般路径**。`trunc_sat_i32` 是**饱和**转换：`3000000000.0` 与 `2147483648.0` 都饱和到 **2147483647**，而 `2147483647 % 7 == 1` —— 两个用例都得到 `1`，正是这个机制。

而解释器同一条路径用的是 `(int)da`，x86-64 上是 `cvttsd2si`，越界给 **INT_MIN**（`-2147483648`），`-2147483648 % 7 == -2`。

**所以解释器与 wasm 的差别是「未定义行为 vs 饱和语义」的差别**，不是两个各自正确的选择。这也解释了为什么 `say 3000000000` 在三个后端都正确输出 `3000000000` —— 文字解析没问题，坏的是 `%` 那一步。

**顺带更正 §1.1 的位置**：真正的 `L_MOD` 是 `src/vm/vm.c:3824-3838`，而且它自己也有**两条**路径：

```c
value_set(&R[ins.r1], VAL_INT, a->ival % b->ival, 0, NULL, NULL);   /* :3827  int 快路径，32 位 */
...
int bi = (int)val_as_double(b);                                     /* :3830  先截断 */
if (bi == 0) { vm_throw_kind(vm, "division_by_zero"); ... }         /* :3831  再判零 */
double da = val_as_double(a);                                       /* :3836 */
value_set(&R[ins.r1], VAL_INT, (int)da % bi, 0, NULL, NULL);        /* :3837  越界 (int) 是 UB */
```

零检查在**截断之后**（`:3830` 截断、`:3831` 判零），所以 `7 % 0.5` 与 `5 % 0` 被报成除零；AOT 的 `nv_mod` 则是 `nv_int(y ? x % y : 0)` —— **除数为 0 时静默返回 0**（`src/compilation/aot_native.c:208-210`）。三个后端对「除以零」有三种态度：报错 / 静默 0 / 报错。

#### 对 §5 的影响

- **O0 的判据不是「对齐 AOT」**，而是先定 `and`/`or` 的返回语义，再让三方都执行它。wasm 还要去掉「拒绝编译」这条，否则它是唯一一个把合法程序挡在门外的后端。
- **O1 的判据要写成三方一致**，并且一并处理「除数为 0」：现在的三种态度必须收敛成一种。
- **§1.2 的整数提升不是分歧**：实测 `2147483647 + 1` 在三个后端都是 `2147483648`，三方一致。它是**语言设计**问题（`Z` 是无限整数集合），不是后端不一致 —— 性质与 O0/O1 不同，别混在一起修。（**补记**：这句只对**两项** `+` 成立；三项以上的链在修前解释器会回绕，是真正的后端分歧，见 §1.7。§1.2 的结论本身不变。）

### 1.7 `+` 链按项数换答案：3 项链折成 `OP_CONCAT` 后整数溢出回绕（**已修复：与 `L_ADD` 同步**）

**机制**：`src/compiler/compiler.c:715-786` 把**任何**左结合 `+` 链（≥3 项且操作数都是 `EXPR_STRING/NUMBER/FLOAT/BOOL/IDENT`）折成单个 `OP_CONCAT`，2 项链走 `OP_ADD`（发射点 `src/compiler/compiler.c:777`、`:781`；`src/compiler/compiler.c:731-737` 的守卫只排除「操作数本身是复合表达式」，**不排除纯整数链**）。该处注释声称「identical per-step semantics to OP_ADD」——对整数溢出，这句话是假的：

- `L_ADD` 用 `int64_t` 相加，结果离开 `[-2147483648, 2147483647]` 时提升为 float。
- `L_CONCAT` 的整数快路径直接相加两个 32 位载荷（`src/vm/vm.c:3042-3047`），**回绕**。

于是同一个表达式按项数给出两个答案（`x = 2147483647`）：

| 表达式 | 解释器（修前） | AOT | wasm |
| --- | --- | --- | --- |
| `x + 1` | 2147483648 | 2147483648 | 2147483648 |
| `x + 1 + 0` | **-2147483648** | 2147483648 | 2147483648 |
| `x + 1 + 1` | **-2147483647** | 2147483649 | 2147483649 |
| `x + 1 + 0 + 0` | **-2147483648** | 2147483648 | 2147483648 |
| `y + y + y`（y=1e9） | **-1294967296** | 3000000000 | 3000000000 |

**分歧是 2 比 1，不是 1 比 1**：AOT 与 wasm 自始至终互相一致，解释器的 `OP_CONCAT` 是唯一异类。此前若把本条记成「解释器 vs AOT」，应以此表为准。

**为什么长期没被发现**：`contract_test.im` §1 里**没有任何超过两项的 `+` 链**，而两项形式恰好走没坏的那条路径 —— 整套语言契约的正确性此前只由 2 项链取样。

**修法**：`L_CONCAT` 的整数分支改成与 `L_ADD` 相同的 `int64_t` 折叠、越界提升 float（并 `acc.sval = NULL`，与相邻的泛型 double 分支一致）。这**不触及** §1.2 的结论（那是语言设计问题），只消掉「按项数换答案」。缓解手段：`acc.type == VAL_INT` 时 `acc_fold_owned` 必为 0（字符串分支先判且唯它置 1），故无需 free。

**判据**：10 格并入三后端套件 `tools/logic_semantics.test.py` 的 `CHAIN` 表（该套件现在 **64 例** = 逻辑 24 + 条件 6 + 短路 4 + `+` 链 10 + 打印浮点 20（浮点表在 §1.8 的第二轮自查里由 14 增至 20），× 解释器/AOT/wasm 逐格比对期望值与三方一致性）；**修前实测 6/10 红**，修后全绿。原先为此单开过一条 CTest（`tools/arith_chain_semantics.test.py` / `arith_chain_semantics_regression`），后按裁定**并回既有套件**，`EXP_CTEST` 因此回到 **107**（本轮 §1.9 新增 `list_set_off_by_one_runtime` 才把它推到 **108**）—— 代价是这次并回本身不再显现在计数里，收益是同一个语义面只留一条三后端入口。`contract_test.im` §1 补入 3、4 项链断言（§1 由 7 条增至 12 条），但注意该套件**并未注册进 `CMakeLists.txt`**，是一条手动/dormant 套件，且它在 `contract_test.im:80`（`list(Z[1~3])[0] == 1`）另有一条**先存**的失败 —— 两者都不是本行引入。

### 1.8 打印浮点：三个后端三个格式（**已修复：三份实现都按解释器的规范重写**）

`say` 在解释器里走 `vm_value_to_string` → `value_to_string` → **`vts_double`（`src/vm/vm.c`）**，**不是** `str()` 用的 `"%.17g"`（`src/runtime/runtime_posix.c:54-64`）—— 同一个解释器内部就有两种浮点格式，这解释了为什么 `vtest/float_precision_v04.im`（CTest `float_precision_runtime`，`CMakeLists.txt:739-740`）钉住 `str(value)` 的 17 位，而 `say(1.0 / 3.0)` 打的是 `0.333333`。三个后端**各有一份实现**：

- 解释器：`vts_double`（整数部分 + 最多 6 位小数、去尾零、整值走整数打印、`|v| >= 1e15` 才落 `"%.17g"`）；
- AOT：生成代码前导里的 `nv_say` 用裸 `"%g"`（`src/compilation/aot_native.c` 的 `kPreamble`），并注释断言解释器的格式**不可复现**（"it prints 1e-20 as \"0.\" and mixes 6- and 7-digit precision"）—— 这条断言是错的，那个格式确定且可移植；**正是它把浮点用例长期排除在 `tools/aot_native.test.py` 的语料之外**（旧免责注释见 `tools/aot_native.test.py:34-37`）；
- wasm：`tools/wasm_run.js` 里一份 JS 重实现。

实测（修前，同一份源码三种跑法）：

| `say(…)` | 解释器 | AOT | wasm | 缺陷类别 |
| --- | --- | --- | --- | --- |
| `1.0 / 3.0` | 0.333333 | 0.333333 | 0.333333 | 三方一致（正常行） |
| `123456789.125` | 123456789.125 | **1.23457e+08** | 123456789.125 | AOT 只剩 6 位有效数字 |
| `0.000001` | 0.000001 | **1e-06** | 0.000001 | AOT 走科学计数法 |
| `1e-20` | **0.** | **1e-20** | 0 | **三方三个答案** |
| `2.0000001` | **2.** | 2 | 2 | 解释器留悬挂 `.` |
| `0.9999999` | **0.1** | 1 | **0.1** | 解释器与 wasm 吃掉进位 |
| `0.0 - 0.5` | **0.5** | -0.5 | -0.5 | 解释器丢负号 |
| `0.0 - 1e-20` | **0.** | **-1e-20** | -0 | 解释器丢负号 + 悬挂点 |
| `0.0 - 1.0 / 3.0` | **0.333333** | -0.333333 | -0.333333 | 解释器丢负号 |
| `1e20` | 1e+20 | 1e+20 | **1.e+20** | wasm 尾数修剪出畸形 |
| `2147483648.5` | 2147483648.5 | **2.14748e+09** | 2147483648.5 | AOT 精度 |
| `12345678901234567890.0` | 1.2345678901234567e+19 | **1.23457e+19** | 1.2345678901234567e+19 | AOT 精度 |

**这是三个各自独立的缺陷，不是一个移植问题**：①解释器的小数部分乘 1e6 取整后若恰好进位到 1000000，随后的去尾零把进位吃掉（`0.9999999` → `0.1`）；②小数部分归零时仍输出 `.`（`2.0000001` → `2.`）；③`-1 < v < 0` 时整数部分是 0，符号只活在小数部分里，于是整数打印发的是裸 `"0"`（`-0.5` → `0.5`，`-1e-20` → `0.`）；④AOT 的 `%g` 只有 6 位有效数字，且对 `1e-06` / `2.14748e+09` 这类量级改用科学计数法；⑤wasm 的 `toPrecision(17)` 换成科学计数法时把尾数修成畸形 `1.e+20`。

**修法（裁定：以「改正后的解释器规范」为唯一规范，三份实现都重写）** —— 不把解释器的 bug 移植给另外两个：

- `src/vm/vm.c` 的 `vts_double`：小数部分进位（`frac == 1000000` 时 `ip += 1; frac = 0`）、`frac == 0` 时不再输出 `.`、负号显式处理（`neg && ip == 0` 时单独发 `'-'`，故 `-1e-20` 是 `-0` 而非 `0`）、整数部分改发 `vts_int(..., neg ? -ip : ip)`（`ip` 是**幅值** —— 第一版漏了这点，`-1.5` 打成 `1.5`，由探针当场抓到）；
- `src/compilation/aot_native.c`：`kPreamble` 增加 `#include <string.h>`，新增 `nv_fmt_double`（同一算法的 C 移植），`nv_say` 的浮点分支改为 `nv_fmt_double` + `printf("%s\n", …)`，并删掉那条「不可复现」的错误断言；
- `tools/wasm_run.js`：按同一规范重写 `fmtFloat`，并新增 `fmtG17`（`toPrecision(17)` 后修剪尾数再拼回指数，消掉 `1.e+20`）。`fmtFloat` 是 wasm 唯一的浮点出口（`im_print_float`，`tools/wasm_run.js:82`）。

**修后**：上表各行 × 三后端逐格一致。

**判据（两条，且都反向验证过）：**

1. `tools/logic_semantics.test.py` 的 `FLOAT` 表（**20 格**，三后端逐格比对期望值与三方一致性）。**反向验证**：把 `tools/wasm_run.js` 的 `fmtFloat` 改坏（去符号 + 去进位）⇒ **4/64 红**，报的正是 `FAIL say 0.0 - 0.5: wasm printed '0.5', expected '-0.5'` 一类；还原 ⇒ 64/64 绿。JS 侧不需要重编，所以这条可以很便宜地重跑。
2. `tools/aot_native.test.py` 的 `EQUIVALENCE` 语料：删掉 `:34-37` 那条把浮点排除在外的注释，插入 **15 条**浮点行（等价用例 46 → 65；全量输出 `78 cases (65 equivalence, 3 pinned divergences, 10 refusal), 0 failures`）。**这一步把「AOT 打印浮点」从「文档化的分歧」变成「被断言的等价」** —— 旧注释的存在本身就是这个缺陷活了这么久的原因。

**教训**：`tools/aot_native.test.py` 的旧注释是**把 bug 写成了规范** —— 「解释器不可复现，所以浮点排除在外」。一句写在测试语料里的免责声明比缺陷本身更难发现，因为它让缺口看起来像一个已记录的决定。同类样本见 `docs/STATUS.md` §10.42 第 ④ 条：`REJECT_CASES` 曾把 O0 正要消除的拒绝行为钉成「正确行为」。

**未进门禁（已修，见 §1.12）**：`contract_test.im` 当时是手动/dormant 套件，不在 CTest 内（`grep -in contract CMakeLists.txt` 只有两处无关注释）。它此后的第一个失败曾是 `list set`（`contract_test.im:88`，原 `:80`），**本轮已修**，见 §1.9；修后在本机能走到 `:120`，剩下 4 条**结构性**失败（Windows 专有 mod 的 POSIX 桩），同样见 §1.9。**§1.12 已把它注册进 CTest：`contract_suite_runtime`。**

**第二轮（提交前自查）才发现的一类分歧：平局的舍入规则不同。** 上面那 14 格全绿之后，提交前又问了一次「`%.17g` 与 `toPrecision(17)` 真的等价吗」—— **不等价**。C 的 `%.17g` 按 IEEE 默认舍入（round-half-to-**even**）处理平局，而 ECMAScript 的 `Number.prototype.toPrecision` 规范明文写的是「若有两个这样的 n，取**较大**者」。这不是理论问题：`1.0000000000000002e15` 的精确值就是 **`1000000000000000.25`**，取 17 位有效数字正好卡在 `…0.2` 与 `…0.3` 中间 —— glibc 给 `.2`，JS 给 `.3`。**解释器与 AOT 用的是同一份 glibc，所以两者一致，只有 wasm 那份 JS 是异类。**

| `say(…)` | 解释器 | AOT | wasm（`toPrecision(17)`） |
| --- | --- | --- | --- |
| `1.0000000000000002e15` | `1000000000000000.2` | `1000000000000000.2` | `1000000000000000.3` |
| `2000000000000000.25` | `2000000000000000.2` | `2000000000000000.2` | `2000000000000000.3` |
| `0.0 - 1.0000000000000002e15` | `-1000000000000000.2` | `-1000000000000000.2` | `-1000000000000000.3` |

**为什么 14 格没抓到它。** 平局要求 double 的**精确**十进制展开在第 18 位恰好是 5、其后全为 0。`1e20`、`12345678901234567890.0` 这类被钉住的整数都不是平局 —— **判据的取样区间又一次成了判据的漏点**（与 §1.7 的「样本长度」同型）。

**修法（改动完全封闭在 `tools/wasm_run.js` 一个文件里）。** 给 JS 写一个 `exactDecimal(a)`：double 必是 `m * 2^e`，`e < 0` 时即 `m * 5^-e × 10^e`，所以精确展开有限、BigInt 能精确装下；`fmtG17` 改为在这个精确整数上取 17 位有效数字 —— `2r > 10^k` 进位、`2r == 10^k` 时看 `q` 的奇偶（**五成双**）—— 再按 `%g` 的规则用十进制指数选定点/科学计数。解释器与 AOT 不动（它们本来就是对的）。

**判据（三条，且都反向验证过）。** ①`tools/logic_semantics.test.py` 的 `FLOAT` 表 **14 → 20 格**（新增 6 格覆盖平局、非平局的 `>=1e15` 非整值、以及 `>=1e15` 的整值），该套件 **58 → 64 例**；②`tools/aot_native.test.py` 的 `EQUIVALENCE` 增 5 行，等价 **60 → 65**，全量报 `78 cases (65 equivalence, 3 pinned divergences, 10 refusal), 0 failures`；③**随机扫描**：380 个随机/边界 double（六个量级区间，外加 `10^14…10^19` 的 `+0.25/0.5/0.75`）三后端逐格比对，**0 分歧**。**反向验证**：把 JS 的平局判据改成 ECMAScript 的「取较大者」（`if (twice >= p) q += 1n;`）⇒ 恰好那 **3 条平局格红**（`wasm printed '1000000000000000.3', expected '…0.2'`），其余 61 格全绿；还原后 `cmp` 证 `tools/wasm_run.js` 逐字节一致 ⇒ 64/64 绿。

## §1.9 `list(<集合>)` 差一格：`vm_set_to_array` 把 1-based 句柄交给按 raw 索引解释的调用方

**症状。** `contract_test.im:88` 的 `check(list(Z[1~3])[0] == 1, "list set")` 失败：`list(Z[1~3])` 打印 `[]`、`len(list(Z[1~3]))` 是 **0**、`list(Z[1~3])[0]` 是 nil。`list(Z)` / `list(N)` 也是 nil（那是另一回事，见下）。

**根因：两套索引约定被接在一起。** `VAL_ARRAY` **值**里存的是 **1-based 句柄** —— 解释器唯一的构造点是 `value_set(&R[ins.r1], VAL_ARRAY, aidx + 1, 0, NULL, NULL);`（`src/vm/vm.c:3240`），读者都减一：`vm_array_get(vm, obj->ival - 1, i)`（`src/vm/vm.c:3275`）、`vm_array_set(vm, obj->ival - 1, i, valv)`（`:3323`）、`idx = val->ival - 1`（`:2447`）。而整个 `vm_array_*` **函数族**收的是 **raw 池下标**（`vm_array_len(vm, idx)`，`src/vm/vm.c:789-795`）。`vm_set_to_array`（`src/vm/vm.c:1966`）**两条造数组的分支都以 `return aidx + 1;` 收尾** —— 交出去的已经是句柄 —— 而它的三个调用方全部按 raw 解释：

- `src/runtime/runtime_posix.c:19`（`posix_core_len` 的集合分支）：`n = vm_array_len(vm, a)`
- `src/runtime/runtime_posix.c:236`（`posix_core_list`）：`Value out = { VAL_ARRAY, idx + 1, 0, NULL };`
- `src/runtime/runtime.c:134`（WIN32 的 `builtin_list`）：`ival = r + 1`

⇒ `list(<集合>)` 交回的是**池里的下一个数组**（刚新建、通常是空的），`len(<集合>)` 量的也是它。

**修法。** 两处 `return aidx + 1;` → `return aidx;`，并在函数头加一条约定注释（*returns a RAW array-pool index — the same convention `vm_array_*` take — which is what every caller assumes. (A `VAL_ARRAY` value carries `idx + 1`, so the callers that build a `Value` are the ones that add the 1.) -1 on failure.*）。**改函数而不是改三个调用方**：它就坐在按 raw 索引的 `vm_array_*` 族里，且三个调用方本来就假定 raw。

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `list(Z[1~3])` | `[]` | `[1, 2, 3]` |
| `len(list(Z[1~3]))` | 0 | 3 |
| `len(Z[1~3])` | 0 | 3 |
| `list(Z[1~3])[0]` / `[2]` | nil / nil | 1 / 3 |
| `len(list(Z[1~100]))` / `len(list(N[0~10]))` | 0 / 0 | 100 / 11 |
| `list(<数组>)` | `[1, 2]` | `[1, 2]`（透传，未受影响） |

**判据（且反向验证过）。** 进树回归 `vtest/list_set_off_by_one_v05.im` + CTest `list_set_off_by_one_runtime`（`CMakeLists.txt`，`LABELS "vm;language;regression"`，`PASS_REGULAR_EXPRESSION "listset-ok n=3 first=1 last=3 setlen=3"` / `FAIL_REGULAR_EXPRESSION "n=0|first=nil|setlen=0"`）。**反向验证**：把函数里两处 `return aidx;` 还原成 `return aidx + 1;` 重编 ⇒ 输出 `listset-ok n=0 first=nil last=nil setlen=0`、`listset-lit=0,nil`，FAIL 正则命中 ⇒ 红；还原修复重编 ⇒ `n=3 first=1 last=3 setlen=3`、`listset-lit=3,3` ⇒ 绿，`diff` 证源码逐字节一致。这条缺陷改的是**数值不是退出码**，所以只能断言数值。`EXP_CTEST` **107 → 108**，`docs/BOARD.md` §3 与 `docs/STATUS.md` §2/§2.1 同步为 **108 / 108**。

**`contract_test.im` 剩下的 4 条失败是结构性的，不是缺陷。** 用一遍自动跳过的脚本（跑套件 → 读 stderr 的 `CONTRACT FAIL: <desc>` → 注释掉该行 → 重跑）穷举出**恰好 4 条**：`str2int`、`str2int invalid -> 0`（`:120`/`:121`）、`noise range`（`:170`）、`vram accounting`（`:174`）；跳过这 4 条后该套件 `rc=0`。`str2int` 由 `io_mod_register` 注册（`src/mod/io_mod.c:315`，`vm_register_builtin(vm, "str2int", builtin_str2int);` 在 `:334`），`noise2d`/`gui_canvas`/`gui_px`/`gui_vram_used` 属 gui_mod —— 而 **POSIX 构建根本不编这两个 mod**：`CMakeLists.txt:426-436` 是 `if(WIN32)` 分支（`src/mod/gui_mod.c src/mod/io_mod.c …` 在 `:431`），POSIX 的 `else()`（`:437-443`）挂的是 `src/platform/posix_stubs.c`（`:441`），其正文 `#define STUB_REG(name) void name(VM *vm) { (void)vm; }` 后跟着 `STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)` —— **空实现**。`contract_test.im:2` 自己也写着 `# usage: inimerse.exe --time-limit 60 contract_test.im`（Windows）。

**可诊断性缺陷：调用未注册的函数是静默留栈，不是报错 —— 已修，见 §1.11。** 修前带不带 `--no-mods` 都一样：`say(str2int("42"))` 打印 `42`，但 `str2int("42") == 42` 是 **false**；`say(str2int("abc"))` 打印 `abc`，而 `str2int("abc") == 0` 是 false；`noise2d(1.5, 2.5, 7)` → `7`（它的 seed 实参）；`gui_canvas(8, 8)` → `8`；`gui_vram_used()` → `0`。所以这 4 条失败表现为**值不对**，而不是「未知函数 `str2int`」—— 这正是上面那遍穷举必须靠注释跳过、而不可能靠报错定位的原因。§1.11 之后同样的调用会抛 `unknown builtin function 'str2int'`。

**放进变量的集合曾完全不可枚举 —— 已修，见 §1.10。** 写上面那条回归时撞出来的：`z = Z[1~3]` 会被编成 `OP_SET_INTERVAL`（op 52）**加一条 `OP_NEW_SET`**（op 51），于是 `z` 是一个 `compCount > 0` 的 kind-0 集合，撞上 `vm_set_to_array` 的 `if (s->kind != 0 || s->compCount > 0) return -1;`。修前取证：`len(Z[1~3])` / `len(list(Z[1~3]))` 内联 3/3 正确，而 `z = Z[1~3]` 后 `len(z)` 是 **0**、`list(z)` 与 `size(z)` 是 **nil**；`min(z)` 1 / `max(z)` 3 / `2 in z` true 全对（走 `comps[]`）；`s2 = 1, 2, Z[7~9]` 后 `len(s2)` 是 0（应为 5）；`p = 1, 2, 3` 后 `len(p)` 是 3 正确。上面那条进树回归与 `contract_test.im:88` 钉的都是**内联**形态，因此本轮修复对它们成立；变量形态与复合形态现在由 `vtest/set_components_enumerable_v05.im` 钉住，见 §1.10。另注：`list` 只存在于解释器，AOT 与 wasm 后端都没有实现（`grep '"list"' src/compilation/*.c` 为空），所以这条没有三后端比对可言。

## §1.10 集合字面量是并集：三处「只走一半」的枚举

**一个集合有三个半边。** `SetObj`（`src/vm/vm.h:36-47`）把成员分在三处：`i64`（整数字面量，`iCount`）、`items`（非整数字面量，`count`）、`comps`（区间分量 `SetComp`，`compCount`）。`1, 2, Z[7~9]` 就是「前两处放两个、第三处放三个」的并集。本轮在**枚举**这条路上连着挖出三个缺陷，形态都是「只处理了一半」。

### ① `vm_set_to_array` 拒绝一切带分量的集合

`src/vm/vm.c` 的 `vm_set_to_array` 原文以 `if (s->kind != 0 || s->compCount > 0) return -1;` 收尾，于是 `list`/`len`/`size` 对 `Z[1~3]` 内联之外的一切集合形态瞎掉 —— 而 `min`/`max`/`in` 走 `comps[]`，一直是对的。

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `b = 1, 2, Z[7~9]` 后 `len(b)` | 0 | **5** |
| `c = Z[1~3], Z[5~7]` 后 `len(c)` | 0 | **6** |
| `d = 1, Z[1~3]` 后 `len(d)`（1 落在分量里） | 0 | **3** |
| `e = Z[1~3]` 后 `len(e)` | 0 | **3** |
| `f = Z, 1` 后 `len(f)` / `list(f)`（真无限） | 0 / nil | 0 / nil（**仍拒绝**） |
| `r = R[0~3]` 后 `len(r)` / `size(r)`（R 无格点） | 0 / nil | 0 / nil（**仍拒绝**） |

**修法**：按 `i64` + `items` + `comps` 三部分求并，并**去重** —— `d = 1, Z[1~3]` 是 {1,2,3} 而非四个，字面量可能落在分量里。拒绝的边界必须原样保住：分量只有**两个有限端点且有格点**才可枚举，而 `vm_set_add_comp` 给「整个具名集合」写的哨兵 `(lo,hi,loInc,hiInc) == (0,0,0,0)`（`set_contains` 读作「无界」）因此仍返回 -1，`R`（bi 24）同理。

### ② 浮点点阵按累加步长走，漏掉最后一个成员

`FloatN`/`floatN` 的成员是 `k / 10^N`，必须**按格点下标**走。原实现累加 double 步长，而 `0.1 + 0.1 + 0.1` 是 `0.30000000000000004`，**严格大于** double `0.3`，于是区间最后一个成员被循环条件甩掉，而 `in`（直接问 `builtin_contains`）仍然认它 —— 同一个集合上 `in` 与 `list`/`len` 自相矛盾：

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `s = float1[0~0.3]` 的 `(0.3 in s)` | true | true |
| `len(s)` | **3** | **4** |
| `list(s)` | **`[0, 0.1, 0.2]`** | **`[0, 0.1, 0.2, 0.3]`** |
| `len(Float1[0~0.3])`（大写，不含整数） | **2** | **3** |
| `len(float1[0~0.5])` | 5 | **6** |

`floatN` 含整数、`FloatN` 不含，所以同一区间两者基数不同 —— 这既是修法的对照，也是 `builtin_contains` 仍在参与过滤的证明。

### ③ `vm_set_add_comp` 不复制 `src->i64`：嵌套字面量丢整数半

`vm_set_add_comp` 在 `src->kind == 0` 分支里复制了 `items` 与 `comps`，**唯独没有 `i64` 循环**。于是把一个字面量当元素再套一层时，它的整数成员全丢：

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `p = 1, 2, 3` 后 `len(p)` | 3 | 3 |
| `q = p, 4` 后 `len(q)` | **1** | **4** |
| `list(q)` | **`[4]`** | **`[1, 2, 3, 4]`** |
| `m = 1.5, 2.5` / `n = m, 3.5` 后 `len(n)` | 3 | 3 |

最后一行是**对照**：非整数走 `items`，所以它一直是对的 —— 这正是「只走一半」在修前能藏住的原因。

### 判据与双向验证

两条进树回归，都只断言**数值**（三个缺陷都改值、不改退出码）：

- `vtest/set_components_enumerable_v05.im` ← CTest `set_components_enumerable_runtime`，`PASS_REGULAR_EXPRESSION "setcomp-ok union=5/1,9 two=6/5 overlap=3/3 single=3 nested=4/4 size=3 infinite=0,nil"`，`FAIL_REGULAR_EXPRESSION "union=0|overlap=0|single=0|nested=1|size=nil"`。这一行同时钉住并集基数、首末元素、重叠去重、单分量包一层、嵌套、`size`，以及**真无限仍被拒**（`infinite=0,nil`）。
- `vtest/set_float_lattice_v05.im` ← CTest `set_float_lattice_runtime`，`PASS_REGULAR_EXPRESSION "setlattice-ok n=4 last03=true in03=true upper=3 upper0=false half=6 int=6 big=100"`。`last03=true` 即「`list` 的最后一个成员确实等于 `0.3`」，与 `in03=true` 合起来正是修前那条自相矛盾的反面。（`str()` 走 `%.17g`，所以末元素靠比较断言而不是打印。）

**反向验证**：`cp src/vm/vm.c .verify/` → `git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c` → 重编 ⇒ `ctest -R "set_components_enumerable|set_float_lattice"` 得 **`0% tests passed, 2 tests failed out of 2`**，两条各自命中 `FAIL_REGULAR_EXPRESSION`（`Error regular expression found in output. Regex=[union=0|overlap=0|single=0|nested=1|size=nil]` 与 `Regex=[n=3|last03=false|in03=false|upper0=true]`）；拷回重编 ⇒ 两条 `Passed`，`cmp` 证源码逐字节一致。

**顺带统一**：`posix_core_size` 原先自己算 `(hi-lo)/step + 1`，现在改走同一个枚举器。于是 `size(<集合>)` 与 `len(<集合>)` 在复合/浮点形态上不再可能给出两个答案；修前 `z = Z[1~3]` 的 `size(z)` 是 nil 而 `len(z)` 是 0，本身就是这种分裂的极端例子。

### ④ 两道「拒绝」闸门此前没有任何回归

枚举器在两条边上**主动拒绝**而不是硬走：`kh - kl >= 10000000`（格点跨度一千万以上）与端点越界（`|lo| > 9.0e18`，或整数格点落在 int32 之外，因为 `VAL_INT` 只有 32 位）。这是**契约**：截断成前 N 个成员又便宜又错。但它此前只被上面两条回归**间接**碰过（`infinite=0,nil` 只钉住「真无限」那一种拒绝），而**拒绝与空区间在 `len()` 下都是 0** —— 用 `len()` 断言根本分不开这两者，所以那两条回归看不见闸门有没有被拆掉。

`vtest/set_enumeration_refusal_v05.im` ← CTest `set_enumeration_refusal_runtime`，一行断言七个值：

`setrefuse-ok bigspan=nil edge=nil i32=nil neg32=nil empty=0 ok10=10 ok3=3`

前四个 `nil` 是四种拒绝（跨度 `Z[1~10000001]`、端点 `Z[1~10000000000000000000]`、int32 之外 `Z[1~3000000000]` 与 `Z[-3000000000~-1]`），第五个 `0` 是**空区间不是拒绝**（`list(Z[5~4])` 是 `[]` 而 `len` 为 0），最后两个 `10` / `3` 是**对照**：闸门不许扩大到把普通区间也拒掉。`PASS_REGULAR_EXPRESSION` 是整行字面量，`FAIL_REGULAR_EXPRESSION "ok10=0|ok3=0|empty=nil"` 专抓「拒绝过度」这一侧（三个 token 都带数字或词，池转储里凑不出，避开了 §1.11 记的坑）。

**双向验证**：把跨度闸 `if (kh - kl >= 10000000) return -1;` 临时改成 `>= 3` ⇒ 该 CTest 红（`***Failed  Error regular expression found in output. Regex=[ok10=0|ok3=0|empty=nil]`，因为 `Z[1~10]` 也一起被拒了）；`cp` 还原 + `cmp` 逐字节一致 + 重编 ⇒ 绿。实测 **0.057 s**。

**计数**：①–③ 的两次修复共 `grep -c 'add_test(' CMakeLists.txt` **108 → 110**（`tools/gate.sh` 的 `EXP_CTEST` 那一行同步 **110**）；④ 这条回归再 **112 → 113**（111 是 §1.11，112 是 §1.12）。

## §1.11 调用未注册的内建函数会静默返回栈顶，而不是报错

**症状与机制。** `str2int("42")` 在 POSIX 上并不存在（`io_mod_register` 是空桩，见 §1.9），但调用它不报错，反而把**最后一个入栈的实参**当成返回值：`say "A:" + str(str2int("42"))` 打印 `A:42`，而 `str(str2int("42") == 42)` 打印 `false` —— 返回的是字符串 `"42"` 本身，不是数 42。

`L_CALL_BUILTIN`（`src/vm/vm.c:3519`）先 `int bi = builtin_lookup(vm, name);`（`builtin_lookup` 定义在 `src/vm/vm.c:1486`，表为 `builtins[512]` / `builtinCount`，`src/vm/vm.h:157`），然后 `if (bi >= 0) { … }`。这个 `if` **没有 `else`**，于是名字查不到时控制流直落 case 末尾的收尾代码：

```c
if (t->sp >= 0) { value_move(&R[ins.r1], &t->stack[t->sp]); t->sp--; }
```

（`src/vm/vm.c:3564-3567`）—— 它本来只服务于「有返回值的调用把结果移到 r1」，在查不到函数时就变成了「把最后一个实参塞进 r1」。（`src/vm/vm.c:4983` 的另一处 `OP_CALL_BUILTIN` 只是反汇编器。）

| 探针（修前） | 输出 |
| --- | --- |
| `str(str2int("42"))` | `42`（字符串） |
| `str(str2int("42") == 42)` | `false` |
| `str(str2int("abc"))` | `abc` |
| 退出码 | **0** |

**修法：补上缺失的分支。**

```c
} else {
    char eb[256];
    snprintf(eb, sizeof eb, "unknown builtin function '%s'", name);
    vm_throw_msg(vm, eb);
}
```

修后 `./build/inimerse --no-mods .verify/unreg.im` 的 stdout 为空，stderr 为 `[exception] uncaught: unknown builtin function 'str2int'` 加 `  at ip=4 frames=0`，**退出码 1**。它是一条**普通可捕获的 throw**：`try { x = str2int("42") } catch (e) { say "caught: " + str(e) }` 打印 `caught: unknown builtin function 'str2int'`，随后程序照常继续。

**解释器又是唯一的异类 —— 与「`+` 链整数溢出」（§1.9 之前）和「浮点第 17 位平局」同形。** 那两个都是两个后端一致、解释器单独偏一格；这里同样如此，而且两个编译后端早就在编译期就拒绝同一个程序：

| 后端 | 拒绝文本 |
| --- | --- |
| AOT | `aot_native: call to 'nosuchbuiltin' is not a user-defined function in this program` |
| wasm | `error: wasm MVP subset: function 'nosuchbuiltin' not found (builtins are not in the wasm MVP subset) (line 2)` |

所以这一改是让第三个后端**向另外两个靠拢**，而不是新立一条语义。（两个后端还会整体拒绝用字符串的版本 —— `aot_native: expression kind 2 is outside the numeric subset` / `error: wasm MVP subset: expression type 2 not supported by wasm MVP subset (strings/collections need the interpreter) (line 1)` —— 所以三后端可比对的只有数值形态。）

**影响面：零。** 修后全量 `ctest --test-dir build -j$(nproc)` 得 `100% tests passed, 0 tests failed out of 110`；没有任何测试依赖那条静默栈垃圾。

**判据（且反向验证过）。** 进树回归 `vtest/unknown_builtin_v05.im` ← CTest `unknown_builtin_runtime`（`CMakeLists.txt`，`LABELS "vm;language;regression"`，`PASS_REGULAR_EXPRESSION "unreg-ok state=1 control=true"` / `FAIL_REGULAR_EXPRESSION "unreg-ok state=0|unreg-ok state=2|control=false"`）。测试用 `definitely_not_a_builtin_xyz` 这个名字，任何构建都不存在，所以不依赖平台挂了哪些 mod。**反向验证**：`cp src/vm/vm.c .verify/vm.c.unknown_builtin` → `git checkout HEAD -- src/vm/vm.c` → 重编 ⇒ 该 CTest 红（`Error regular expression found in output. Regex=[unreg-ok state=0|unreg-ok state=2|control=false]`），且 `./build/inimerse --no-mods .verify/q.im` 仍打印 `returned 1` / `alive`；拷回 + `cmp` 证逐字节一致 + 重编 ⇒ 绿。`EXP_CTEST` **110 → 111**。

**一条坑（写这次回归才踩到）：`FAIL_REGULAR_EXPRESSION` 不能引用 `.im` 源码里出现过的字面量。** CTest 同时抓 stdout **和 stderr**，而引擎在 stderr 上还会打印一份**字符串常量池转储**（形如 `[0]="returned" [1]="definitely_not_a_builtin_xyz" …`）。第一版测试的 FAIL 正则里写了 `wrong-msg`，正好命中了池里的 `[6]="wrong-msg:"`，于是**实现已经正确、测试却报红**（一次假阴性）。改法是把两个正则都改成**不会出现在池里**的文本：状态用整数 `state=0/1/2`，打印的键靠字符串拼接得到。`PASS_REGULAR_EXPRESSION` 同理。

## §1.12 手动套件进门禁：`contract_test.im` 成为 CTest

**症状（不是运行错误，是流程缺陷）**：仓库根的 `contract_test.im` 是 API/SPI 契约套件，头注释写着「每次引擎 API 变更必须跑本套件（回归门槛）」—— 但它**根本不在 CTest 里**（`grep -in contract CMakeLists.txt` 只命中两处无关注释）。本轮之前的两个缺陷（§1.8 的 `+` 链整数溢出、§1.10 的 `list(<set>)` 句柄错位）**都是它先抓到的**，却因为要人手动跑，从没在门禁里红过。**一条不跑的套件等于没有套件。**

**为什么它会红、而且比文档记的还多**：POSIX 构建把 `io_mod`/`gui_mod` 桩成空实现（`src/platform/posix_stubs.c` 的三个 `STUB_REG`），`str2int`/`noise2d`/`gui_*` 在本平台不存在。§1.11 修掉「未注册内建静默返回栈顶」之后，这些调用**从「静默返回垃圾」变成「抛异常」**，于是三条原本**靠垃圾值侥幸通过**的断言立刻变红：

| 断言 | 旧行为（静默垃圾） | 修 §1.11 之后 |
| --- | --- | --- |
| `gui_frame_interval(16)` 包在 try 里 | 返回垃圾但不抛 → `fok = true` → **通过** | 抛 → `fok = false` → **失败** |
| `check(gui_canvas(8, 8) >= 0, "canvas create")` | 返回垃圾 `8` → `8 >= 0` → **通过** | 抛 → **失败** |
| `check(noise2d(…) == noise2d(…), "noise deterministic")` | 两次都返回同一个垃圾 `7` → 相等 → **通过** | 抛 → **失败** |

**这是「可诊断性」的复利**：让错误可见，会把此前被静默掩盖的「通过」一起推翻。三条假通过里没有一条是断言写错了 —— 是它们测的东西当时根本不存在。

**修法**：套件开头加两个探针（`io_ok` 试 `str2int`；`gui_ok` 试 `gui_vram_used()`，无副作用），第 9/15/16 段按探针整段跳过；末尾打印探针取值，并加一道**下限断言** `if pass < 60 { throw … }`（全套 73 个 `check`，POSIX 跳过 7 个，实测 66）—— 跳过是可闻的，整段没跑不会静默变绿。

**判据与双向验证**：CTest 名 `contract_suite_runtime`，在 `CMakeLists.txt` 里注册，`PASS_REGULAR_EXPRESSION "contract: [0-9]+ passed"`，`TIMEOUT 30`，标签 `vm;language;regression;contract`；**故意不设 `FAIL_REGULAR_EXPRESSION`** —— 抛出文本 `CONTRACT FAIL` 是套件里的字面量，会命中 §1.11 记的那条池转储坑，绿跑也会被误判；异常让进程 exit 1，靠退出码就够。`tools/gate.sh` 的 `EXP_CTEST` 111 → 112。**双向验证**：注册后 `ctest -R contract_suite_runtime` 绿；`git checkout HEAD -- contract_test.im`（未加探针的旧版）+ 重建 ⇒ 红（`***Failed  Required regular expression not found. Regex=[contract: [0-9]+ passed`，旧版在第一处 `str2int` 就抛、退出码 1）；`cp` 回 + `cmp` 逐字节相同 ⇒ 复绿。这条注册**真的在跑引擎**，不是「注册了一个永远绿的壳」。

**诚实边界**：本平台 io/gui 双缺，所以 66/73；Windows 上两段会真的跑（73/73），下限 60 对两个平台都成立。但 POSIX 门禁**确实没有验证 io/gui 那 7 条契约** —— 这是平台能力边界（模块不在 POSIX 构建里），不是套件偷懒；`src/platform/posix_stubs.c` 是空实现这一现状记录在 `docs/STATUS.md` §10.47。

## §1.13 差分模糊测试进门禁：它测不出「全绿」，只能测「没变宽」

**症状（同样是流程缺陷，与 §1.12 同类）。** `tools/im_diff_fuzz.py` 就是 §1.5 立的方法本身 —— 三后端跑同一批随机程序、逐格比对 —— 但它**不在 `tools/gate.sh` 里**，只在有人想起来时手动跑。§1.5 关于「解释器与两个编译后端的真实爆炸半径」的结论正是它一次跑出来的；这样一个工具不进门禁，等于把「全方位」交给记性。

**它进不了「零期望」的门，这是先测出来的、不是先猜的。** 工具默认 `--count 120 --seed 1`，实测：

| 配置 | agreed | DIVERGE | THREW | not translated |
| --- | --- | --- | --- | --- |
| `--count 20 --seed 1` | 20 | 0 | 0 | 0 |
| `--count 120 --seed 1`（默认） | 111 | **5** | **4** | 0 |
| 把常量池限制成 int32 | — | **3** | **4** | 0 |

**限制常量域不能把它变成零期望** —— 分歧不来自字面量本身，来自**计算**：`x + 1 + 1`（`x = 2147483647`）这样的表达式照样跨过 int32。这与 §1.2 记的是同一件事：`Value` 是 32 位 `ival` + double，超过 int32 时解释器把它当 double（低位没了），而 codegen 的 `NV.i` 是 64 位、算得精确。**没有干净的配置**，所以要么把这条真实分歧藏起来，要么钉住它。

**修法：钉住（ratchet），而且双向都红。**

- 种子与程序数固定（`EXP_FUZZ_COUNT=120`、`EXP_FUZZ_SEED=1`），分歧集合因此是确定的；
- 两份计数**精确断言**（`EXP_FUZZ_DIVERGE=5`、`EXP_FUZZ_THREW=4`）：多一条是新的分歧（不许掩盖），少一条是修好了一条 —— **同样红**，必须把该例提升进 `tools/aot_native.test.py` 的 `EQUIVALENCE` 再抬 pin。这与 `tools/aot_native.test.py:132` 的 `DIVERGENCE` 清单是同一约定：「这些是被钉住的、不是被藏起来的；两边的输出都逐字断言，所以缺口既不能悄悄变宽，修好一条也会让测试大声要求把它提升。」
- `not translated` 双向必须为 **0**：工具把「后端翻不动」单列成生成器 bug，而生成器 bug 不是分歧 —— 放宽生成器不能用来把红变绿。

`stage_fuzz` 用 `sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p'` 读计数（`THREW` / `not translated` 同形），读不到即红并打印原因，失败时分别打印 MORE / FEWER 的处置说明。整阶段约 **45–50 s**，是门禁里最慢的一段。工具的退出码本身（发现任何东西就退 1）**不参与判定** —— 判定只看 pin，否则默认调用永远红。

**双向验证。** ①默认 pin ⇒ `gate: fuzz findings match the pin (5 DIVERGE, 4 THREW, 0 untranslated).`，阶段 PASS；②`EXP_FUZZ_DIVERGE=0 bash tools/gate.sh --fast --only fuzz` ⇒ `gate: differential fuzz found 5 DIVERGE / 4 THREW, pinned 0 / 4` + 两条处置说明 + `✘ differential fuzz (interp vs AOT, pinned 0+4) (exit 1)` + `gate: FAILED — do not merge.`

**诚实边界。** 这条门禁**不证明解释器与 AOT 一致**，只证明「在这 120 个程序上，分歧的数量没有变」。它是一把尺子，不是一张合格证；真正的收敛要靠把 5+4 条分歧逐条修掉，每修一条抬一次 pin。`not translated = 0` 也只覆盖这批程序能被 AOT 翻译，不代表生成器覆盖了全部语法。

**已归零。** 这 5+4 条分歧在 v3.1 里逐条修掉了，pin 从 `5`/`4` 降到 `0`/`0`，阶段从「钉住」改成「零期望」—— 见 §1.14。

**2026-10 补记：pin 只断言了「分歧数」，没断言「跑过」。** 上面这套判定读的是 `DIVERGE` / `THREW` / `not translated` 三个计数，而这三个计数在**一个程序都没跑**的时候同样全是 0 —— 空跑与干净跑打印出同一个绿，和发布门禁跑 24/123、`--lint $FILES` 在 `$FILES` 为空时通过是同一个形状（分母不在被断言的位置上）。0.5.1 按机制修掉：`EXP_FUZZ_SEED=1` 变成种子集 `EXP_FUZZ_SEEDS="${EXP_FUZZ_SEEDS:-1 2 3}"`，`stage_fuzz` 逐种子读回 `seed N, M programs` 并断言 `M == EXP_FUZZ_COUNT`（空跑或缩水的运行不许当成「没有发现」），同时断言四个分桶之和闭合、累计种子数不为 0；pin 与 `not translated` 的判定改在累计总数上做。细节见 [STATUS.md](STATUS.md) §10.91。

**边界不变，且多一条。** 它仍然只证明「在这些种子和这批程序上，分歧的数量没有变」，不是两个后端一致的合格证；分母断言保证的是「跑满了这么多个程序」，**不保证种子集本身选得好** —— 三个种子只是把「种子 1 恰好干净」的疑虑压小，不是覆盖率证明。

## §1.14 v3.1 整数位宽：Value 的整数槽改 int64，模糊测试的分歧归零

**症状。** §1.13 把差分模糊测试接进门禁时，默认配置 `--count 120 --seed 1` 实测 `agreed 111 / DIVERGE 5 / THREW 4`，两份计数被**钉**在 `5` / `4`。那 5 条 DIVERGE 里解释器把 `9007199254740993` 算成 `9007199254740992`、把整数除零算成 `inf`；4 条 THREW 是 AOT 抛 `division_by_zero` 而解释器根本不认为那是错。本节记录把这 9 条逐条修掉的过程 —— 以及一个**不是算术缺陷**的发现。

**根因是一个，不是九个。** `src/vm/vm.h:24` 的 `Value` 是 `{int type; int ival; double fval; char *sval; void *ptr;}`，**32 字节、整数槽 32 位**。于是 `src/compiler/compiler.c` 在字面量超过 int32 时把它降级成浮点（`OP_LOADK_FLOAT` 加一条 warning），整条表达式从此走 double：2^53 以上的低位没了，而且**所有整数守卫都不再被命中** —— `2147483647 / 0` 抛 `division_by_zero`，`2147483648 / 0` 却算出 `inf`，因为操作数已经不是整数了。守卫本身是对的，只是永远够不着。

**为什么不能直接把 `int ival` 改成 `long long`。** `docs/DECFY_DESIGN.md` **§2 (a) 层次划分表 item ⑤**（写下时 `:76`）把 `Value` 声明为**永久宽度冻结**：它同时是 VM 寄存器、AOT 生成 C 的 `NV`、wasm 线性内存槽位的共同形状。加宽会让 `sizeof(Value)` 从 32 变成 40，是真正的 ABI 破坏。**改用匿名 union 保住了 32 字节**：

```c
int type;
union { long long ival; double fval; };
char *sval;
void *ptr;
```

实测 `sizeof` 前后都是 32，`type`/`sval`/`ptr` 的偏移不变（`ival` 与 `fval` 共用偏移 8）。`CMakeLists.txt:19-20` 是 C11，匿名 union 合法。wasm 后端**本来就是这样**的（`src/compilation/wasm_backend.c:66` 的 `SLOT_BYTES 16` 注释即 `[tag i32 @+0][pad][i64 payload @+8]`），所以 AOT 完全没动，只有 VM 和 wasm 要改。

**逐条。** 修的过程里又暴露出六处各自独立的问题：

1. **字节码没有 int64 字面量的载体。** `RegInstruction` 只有 `r1/r2/r3`，`OP_LOADK_INT` 的 `r2` 是**符号扩展的 int32**，无法承载无符号低半。先试过把高低 32 位相加（`r2 + (r3<<32)`），**这是错的**：低半符号位为 1 时 `(int)` 会把它符号扩展，结果少了 2³²（实测 `9223372036854775807` 被读成 `9223372032559808511`）。最终**在 `OpCode` 枚举末尾追加 `OP_LOADK_I64`**（该文件自己的约定就是「追加在末尾以保持旧 opcode 编号」），两半都按无符号拼装；旧字节码的 `OP_LOADK_INT` 语义不变，`INIM_BYTECODE_VERSION` 仍是 3 —— 它在 `src/compiler/bytecode.c:351`、`:731` 是**严格相等**比较的，一升版所有既有 `.inim` 都读不了。
2. **`push_int` 的参数是 `int`。** `src/vm/vm.c:325` / `src/vm/vm.h:379` 原文 `void push_int(VM *vm, int v)`，而 `L_PUSH_REG` 对 `VAL_INT` 直接调它 —— **函数传参路径上每一个 64 位整数都被截成低 32 位**。实测 `func f(a) { return a } g = f(9007199254740993) say g` 解释器打印 `1`，AOT 和 wasm 都是 `9007199254740993`。这个缺陷一直潜伏：字面量以前都被降级成 double 走 `push_float`，`push_int` 根本见不到超过 int32 的值。
3. **布尔操作数把表达式拖回 double。** 守卫写的是 `a->type == VAL_INT && b->type == VAL_INT`，`VAL_BOOL` 不满足，于是 `true * 9007199254740993` 解释器和 wasm 都是 `...992`、AOT 是 `...993`。判据定为「布尔是**整数值**操作数」：新增 `val_is_intlike()`（INT 或 BOOL）与 `val_i64()`（BOOL 取 0/1），用于 `L_ADD`/`L_SUB`/`L_MUL`/`L_DIV`/`L_NEG`/`L_MOD`、`val_cmp` 以及 `val_eq` 的跨类型数值分支；wasm 侧对应新增 `e_push_is_intlike()` / `e_push_is_numeric()`。
4. **`L_DIV` 根本没有整数路径。** 它一律 `val_as_double(a) / val_as_double(b)` 并返回 `VAL_FLOAT`，所以 `6/3` 是 `2.0`。而 AOT 的 `nv_div` 一直按**文档写明**的规则做（其 preamble 原文：*The interpreter yields an int when the division is exact and a float otherwise (4/2 prints 2, 7/2 prints 3.5, 6/4 prints 1.5)*）—— 即「精确整除留整数，有余数才转浮点」。是解释器从没实现过自己这条规则。现在 `L_DIV` 逐字镜像 `nv_div`（`ib == 0` 抛 `division_by_zero`；`ib != -1 && ia % ib == 0` 留整数；否则浮点；`ib == -1` 故意落浮点，因为 `INT64_MIN / -1` 会溢出），wasm 的 `TOK_SLASH` 改成同形状三分支。**`2.0 / 0` 和 `2 / 0.0` 三端仍都是 `inf`** —— 只有 intlike×intlike 的零才抛。
5. **wasm 的关系运算无条件走 f64**（`src/compilation/wasm_backend.c` 约 `:1061-1067`），所以 `9007199254740993 > 9007199254740992` 只有 wasm 是错的。补了缺失的 `W_I64_LE_S 0x57` / `W_I64_GE_S 0x59`，加了 i64 精确快路径。
6. **AOT 的 `nv_eq`/`nv_ne` 判据错了两处。** 旧规则是「任一边是 bool 就按整数比，否则按 double 比」：于是 `true == 1.5` 把 1.5 经 `nv_asi` 截成 1 而答 `true`（另两端 `false`），而两个超过 2^53 的整数舍入到同一个 double，`9007199254740993 == 9007199254740992` 答 `true`（另两端 `false`）。改为镜像 `val_eq`：**同 tag 按该 tag 比，只有混合数值对才提升到 double** —— 后者正是 D2「`==` 跨类型数值等价」的要求，`1 == 1.0` 与 `true == 1` 仍是 `true`。

**第 7 处不是算术缺陷，是编译器寄存器水位。** 修完上面 6 处后 seed 1 已经 `120/0/0`，但 seed 2 还剩 1 条：

```
func f(a) { return ((a or (2147483646 < a)) % 31) }
g = f(4294967296)
say g
```

解释器 `0`，AOT 和 wasm 都是 `1`。这条**不能**套用「解释器是离群者」的老结论 —— 它是解释器**忠实地执行了错误的字节码**。函数体 dump：

```
0,2,1,0          MOV r2, r1            ; result r2 = a
26,2,7,0         JUMP_IF_TRUE r2 -> 7
1,3,2147483646,0 LOADK_INT r3, 2147483646
12,5,3,1         LT r5, r3, r1
0,4,5,0          MOV r4, r5
17,2,2,4         OR r2, r2, r4
24,0,8,0         JUMP -> 8
4,2,1,0          LOADK_BOOL r2, true   ; 短路路径写 result
1,2,31,0         LOADK_INT r2, 31      ; ← `%` 的右操作数复用了 r2
50,2,2,2         MOD r2, r2, r2        ; 31 % 31 = 0
```

`src/compiler/compiler.c` 的逻辑表达式发射器把释放水位 `int r_wm = next_register;` 放在 `result = alloc_reg()` **之前**；而 `alloc_reg()` 返回的就是 `next_register++`，所以 `result == r_wm`，末尾的 `release_to(comp, r_wm)` **把它自己刚分配的结果寄存器释放了**，外层二元表达式随即把同一个寄存器分给右操作数并覆盖真值。只在函数里出现是因为：左操作数是变量时 `last_temp = 0` 才走 `alloc_reg()` 分支；字面量或全局读取返回 `last_temp = 1`，直接复用左寄存器。修法是把水位采集移到结果分配之后。**这条与整数位宽无关**，是被同一轮模糊测试顺带照出来的独立缺陷，`tools/aot_native.test.py` 里以 `or_result_not_clobbered` / `or_result_mod` / `and_result_mod` 三行钉住。

**错误名用已注册的 `numeric_overflow`。** 整数离开 int64 时抛的错**没有**用草稿里自造的 `integer_overflow`：`src/types/error_types.c:6-31` 早已注册 `{"numeric_overflow", IM_ERROR_DOMAIN_ARITHMETIC_VM, 2002}`，而 `docs/archive/ROADMAP_CASE_TYPES_V04.md:44` 把 `ArithmeticVMError` 定义为 `{division_by_zero, numeric_overflow, invalid_numeric_operation}` —— 第四个同义词会是未注册、也未文档化的东西。三端现在的行为：`INT64_MAX + 1`、`0 - x - 2`、`INT64_MAX * 2`、`0 - INT64_MIN` 都 `rc=1` 且 stderr 逐字是 `[exception] uncaught: numeric_overflow`；`2147483648 / 0`、`2147483648 % 0` 同理抛 `division_by_zero`。

**证据与门禁。** `tools/gate.sh` 的 `EXP_FUZZ_DIVERGE` / `EXP_FUZZ_THREW` **5 / 4 → 0 / 0**，阶段标签从「pinned 5+4」改成「expect 0 findings」；实测 seeds 1–6 各 120 个程序、seed 1 400 个程序，**全部 `0 DIVERGE / 0 THREW / 0 not translated`**。`tools/aot_native.test.py` 从 73 例扩到 **104 例**（86 equivalence、2 pinned divergences、6 runtime errors、10 refusal）：新增 `RUNTIME_ERROR` 类别断言「两端都 rc≠0 且错误种类相同」，`EQUIVALENCE` 增补 20 行覆盖 int64 字面量、精确算术、精确整数除法、布尔当整数、精确比较、int64 跨调用，以及 `or` 覆盖回归；原先钉在 `DIVERGENCE` 里的 `lcg_float_promotion` 被**提升**为 `int_lcg_second_step`（O2「整数静默退化成 double」随本节关闭）。

**诚实边界。** 归零的是**这一批程序**上的分歧，不是「三后端处处一致」。生成器只覆盖数值子集，字符串、集合、闭包、模块边界都不在其中；`Value` 的 32 字节契约仍在，超过 int64 的整数**还没有**表示（v3.1 phase 2 的 `VAL_BIG` 盒装 BigInt 走 `Value.ptr`，与 `VAL_STRING`/`VAL_ARRAY` 同构，尚未实现）。`docs/archive/ROADMAP_3.1.md:23` 说的 `Z` = 无限整数集 + BigInt 也仍是路线图而非现状。

## §1.15 `sum()` 的两个静默错误答案：集合分量被跳过、整数在 double 里累加

**症状。** §1.14 把整数槽加宽到 int64 之后，它的诚实边界记下「`sum()` 仍在 `double` 里累加 …… 已记录、未修」。它有**两个**独立的错，都在同一个函数里，而且都不改退出码：

1. **集合分量被跳过，却报成功。** `b = 1, 2, Z[7~9]` 的 `len(b)` 是 5、`list(b)` 是 `[1, 2, 7, 8, 9]`，而 `sum(b)` 是 **0**。集合字面量是三部分的并（i64、items、区间 comps），而 `sum` 只读前两部分，且前提是 `s->kind == 0 && s->compCount == 0`；带分量的集合不满足前提，于是两个循环都没进、`ok` 停在初值 **1**、总额停在初值 0。`sum(Z[1~4])` 是 0，而 `sum(list(Z[1~4]))` 是 10。**没有异常，退出码 0。**
2. **整数在 `double` 里累加。** `sum([9007199254740993])` 答 **9007199254740992**（2⁵³ 以上舍入），`sum([9007199254740993, 1])` 同样是 `...992`。Windows 那份还多一层 `push_int(vm, (int)sum)` 截断。

**同一个函数有两份实现，必须同步。** `CMakeLists.txt` 二选一编译：非 Windows 编 `src/runtime/runtime_posix.c`（`posix_core_sum`，`:241` 定义、`:1042` 注册），WIN32 编 `src/runtime/runtime.c`（`builtin_sum`，`:138` 定义、`:1693` 注册）。**POSIX 上生效的是前者** —— 只读后者会预测错行为，这是本次排查真实踩过的坑。

**修法。** 集合改走 `vm_set_to_array` 枚举 —— 就是 `len()`/`size()`/`list()` 已经走的那条路（§1.10 修完枚举器之后它才看得见分量），拿到的暂存数组由 GC 管，三个既有调用方都不 free 它；整数在 `long long isum` 里累加，同时并行维护 `double fsum`，序列里一出现浮点就改用后者；64 位总额越界抛 `numeric_overflow`（§1.14 定的运算符规则）；Windows 那份的 `(int)` 去掉，并保留它自己的错误风格（非数字元素 `vm_throw_msg(vm, "sum: non-numeric element")`，而不是 POSIX 的 nil）。`vm_set_to_array` 对无界集合返回 -1，`sum` 因此答 **nil**。**这是一处行为改变，而且是一处纠正**：`sum(Z)` 原来答 **0** —— 旧闸门 `s->kind == 0 && s->compCount == 0` 对 `Z` 不成立，而不成立时 `ok` 停在初值 1、总额停在初值 0，于是一个无限集合求和得 0 且不报错。`list(Z)` 是 nil，nil 才是诚实的答案，0 不是。

**判据与双向验证。** 新增 `vtest/sum_components_int64_v06.im` ← CTest `sum_components_int64_runtime`（**#114**），断言 `sum-ok comp=27 two=14 single=10 big=9007199254740993 big2=9007199254740994 plain=6 float=3.5 empty=0 bool=nil inf=nil`，配 `FAIL_REGULAR_EXPRESSION "comp=0|two=0|single=0|big=9007199254740992|bool=0"` —— 前三个正对「分量被跳过」、第四个正对「double 累加」。缺陷只改值不改退出码，所以断言的是数字而不是退出码。溢出分支会 exit 1，放不进这个文件，改由 `contract_test.im` 的 `try`/`catch` 转成一条 `check`。`EXP_CTEST` **113 → 114**，`docs/BOARD.md` §3 与 `docs/STATUS.md` §2 同步 **114 / 114**。

**顺带纠正契约套件里的旧教条。** `contract_test.im` §1 有 5 行写的是 `check(x + 1 == 2147483648.0, "int overflow -> float promote")` 之类。它们在 v3.1 之后**数值上仍然成立**（`==` 跨类型数值等价），所以一直是绿的 —— 但描述已经是旧教条。改成本节的规则：越过 int32 仍是整数（`x + 1 == 2147483648`）、只有除不尽才出浮点（新增 `4 / 2 == 2`）、同类型比较精确（新增 `9007199254740993 != 9007199254740992`），并新增一条 `sum` 溢出检查。套件从 **70 条 `check` / 实测 63** 变成 **73 / 66**。

**诚实边界。** ① `sum` 现在只是与运算符同规则，**不是**任意精度：`sum([INT64_MAX, 1])` 抛 `numeric_overflow` 而不是给出 BigInt（v3.1 phase 2 的 `VAL_BIG` 未实现）。② Windows 那份**没有实测**：本机是 POSIX 构建，`src/runtime/runtime.c` 在 Linux 上连 WinHTTP 段都编不过（`HINTERNET`/`WinHttpOpen`/`URL_COMPONENTS`/`WCHAR` 未声明），只能做语法检查 —— `cc -std=gnu11 -fsyntax-only -Isrc -Isrc/common -Isrc/runtime -Isrc/compiler -Isrc/vm -Isrc/platform -Isrc/types -Isrc/mod src/runtime/runtime.c` 的报错行全部落在 593–1643 的 WinHTTP 段，`builtin_sum` 所在的 138–195 区间内 **0 个错误**。③ `src/mod/io_mod.c:51` 的 `io_push_int(VM*, int)` 仍是 `int` 参数（调用点传的是句柄与字节数，不是本节的累加器），本次未改；`src/mod/replay_mod.c:47` 的 `rp_push_int` 已随 §1.14 改成 `long long`。

### 1.16 WIN32 那份 `len()`/`size()` 也没有分量回退：同一个门、同一个类（**已修，未实测**）

§1.15 是在 `posix_core_sum` 上发现的。同一种「拿快速路径的门当答案」的写法，在 **WIN32 副本**里还有两处。这次不是撞上的，是照着 `grep -rn compCount src/` 把每个调用点看了一遍找出来的：

- `src/runtime/runtime.c` 的 `builtin_len`（改动前 `:78`）：`if (s->kind == 0 && s->compCount == 0) n = s->iCount + s->count;` —— **后面没有 `else`**。`n` 的初值是 `0`，于是 `len(1, 2, Z[7~9])` 答 **0**（POSIX 答 5）、`len(Z[1~4])` 答 **0**（POSIX 答 4）。缺陷只改值、不改退出码。
- 同文件的 `builtin_size`（改动前 `:101`）：同一个门，后面接的 `else if (s->kind == 2 && s->lo > -1e300 && s->hi < 1e300)` 只认**闭区间**。带分量的集合 `kind` 是 0、`compCount` 是 1，两个分支都不成立，`n` 停在初值 **-1** ⇒ `size(1, 2, Z[7~9])` 答 **nil**（POSIX 答 5）。

**为什么这算同一个类。** 集合字面量是 i64 / items / 区间 comps 三部分的并（§1.10），所以「`kind == 0 && compCount == 0`」只说明**能直接数**，不是「这个集合有多少个元素」的定义。§1.10 修好枚举器之后，正确的写法只有一种：**能直接数就直接数，否则交给 `vm_set_to_array` 数**。POSIX 的两份都这么写了，WIN32 的两份都没写。

**修法。** WIN32 的集合分支对齐到 POSIX：`else { int a = vm_set_to_array(vm, v->ival); if (a >= 0) n = vm_array_len(vm, a); }`（现 `:88`、`:116`），并把 `int n` 换成 `long long n` —— `push_int` 收 `long long`，`len(9007199254740993)` 在 `int n` 下会被截断，这是 §10.49 那一类截断的又一处。`builtin_size` 里用 `pow()` 的那段闭区间公式随之删除：它对闭区间与枚举器给同一个答案，对分量集合给 nil，删掉之后两种输入都归枚举器。`pow()` 在本文件已无其它用处。

**为什么两份必须同步。** `CMakeLists.txt:426` 是引擎源清单的 `if(WIN32)`：WIN32 编 `src/runtime/runtime.c`（`:429`），其它平台编 `src/runtime/runtime_posix.c`（`:438`），**一个可执行文件里只会有一份**。（这三个号按内容在 `main` 上重取；原文的 `:383`/`:386`/`:395` 是陈旧行号 —— `:383` 实为注释，且这三处都在 `4ca013d` 那次合并改动的 hunk 之前，**不是那次合并造成的**。）`src/runtime/runtime.c:1716-1717` 把 `len`/`size` 注册到 `builtin_len`/`builtin_size`，`src/runtime/runtime_posix.c:1057-1058` 注册到 `posix_core_len`/`posix_core_size`。只读其中一份会预测错行为 —— §1.15 已经真实踩过一次。

**判据与诚实边界。** 这个修复**在本机不可执行**：Linux 上 `src/runtime/runtime.c` 连 WinHTTP 段都编不过（`cc -std=gnu11 -fsyntax-only` 的报错全在 601–1651 行），只能证明「改动区间 0 错误」。能证明的是**构造上的一致**：改动后两个文件的集合分支逐字相同；而 POSIX 那一份是**实测**的 —— `vtest/set_components_enumerable_v05.im:32` 早已钉着分量集合的 `len`/`size`，本机复测 `len(1, 2, Z[7~9]) = 5`、`size(1, 2, Z[7~9]) = 5`、`len(Z[1~4]) = 4`、`size(Z[1~4]) = 4`，与 POSIX 实现一致。**「两份写法一致」能证明，「Windows 上真的跑对了」不能。**

### 1.17 `str()` 把集合总结成 `set(iCount + count)`：分量集合被读成空集

§1.15 修的是 `sum()`，§1.16 修的是 WIN32 的 `len()`/`size()`。同一个「拿快速路径的门当答案」的写法还有第三处，而且这一处**在本机就能看见**——它在 `src/vm/vm.c` 的 `value_to_string` 里：

```c
else if (s->kind == 0) snprintf(buf, bufsz, "set(%d)", s->iCount + s->count);
```

`iCount + count` 只是**字面量那一部分**。集合字面量是 i64 / items / 区间 comps 三部分的并（§1.10），所以对任何带分量的集合，这个和都不是元素个数。实测（修复前，右边是当时就已经正确的 `len()`）：

| 程序 | `str()` 修复前 | `len()` |
| --- | --- | --- |
| `1, 2, Z[7~9]` | `set(2)` | 5 |
| `Z[1~4]` | `set(0)` | 4 |
| `Z[1~3], Z[5~7]` | `set(0)` | 6 |
| `1, 2, 3` | `set(3)` | 3 |

`set(0)` 对一个四元素集合不是「概括」，是**错答案**：它读起来就是「空集」。而 `z = Z[1~4]` 这种写法编译器仍然包成一个 `NEW_SET`，所以只写一个区间的变量也中招。

**修法。** 带分量时改走 `vm_set_to_array` 枚举，与 `len()`/`size()`/`list()`/`sum()` 走同一条路，于是两者**构造上一致**，而不是各自算一遍：

```c
int n = s->iCount + s->count;
if (s->compCount > 0) {
    int a = vm_set_to_array(vm, sidx);
    if (a >= 0) n = vm_array_len(vm, a);
}
snprintf(buf, bufsz, "set(%d)", n);
```

枚举器会去重，这一步是必需的：字面量可以落在分量里（`1, Z[1~3]` 是 {1,2,3} 而不是四个），两个分量也可以重叠（`Z[1~3], Z[5~7]` 不重叠，`Z[1~3], Z[2~5]` 重叠）。修复后实测这四种写法都给出与 `len()` 相同的数：5 / 4 / 3 / 6，重叠的两种是 3 和 5。

**为什么在异常路径上分配是安全的。** `value_to_string`（`src/vm/vm.c:1093`）不只在 `say`/`str()` 里被调用，还在 `vm_throw`（`src/vm/vm.c:2312`）里格式化异常值（`:2343` 的 `try` 无 `catch` 分支、`:2358` 的 JSON 错误输出）。在那里分配 VM 池对象看起来危险，实际不是：`vm_array_new`（`src/vm/vm.c:656`）只会**置 `gc_pending`**（`src/vm/vm.c:685`），真正的 `gc_collect` 发生在解释器主循环里（`src/vm/vm.c:2814`），**不在这次调用里**。`vm_array_push`（`src/vm/vm.c:718`）同样不收集。

**判据。** 新增 CTest **`set_str_component_count_runtime`（#115）**，钉住一行：

```
setstr-ok union=set(5)/5 single=set(4)/4 overlap=set(3)/3 two=set(6)/6 plain=set(3)/3 refused=set(0)/0 named=set(Z) interval=set(float1 interval)
```

`FAIL_REGULAR_EXPRESSION` 正对缺陷值（`union=set(2)` / `single=set(0)` / `overlap=set(1)` / `two=set(0)` / `plain=set(0)`），并且已用 `grep -E` 验证过它**确实匹配修复前的那一行、不匹配修复后的那一行**——一条永远匹配不上的 FAIL 正则等于没有断言。`EXP_CTEST` **114 → 115**。

**诚实边界。** 枚举器**拒绝**走不通的分量（端点朝无穷、整个具名集合、成员超过一千万），这时 `str()` 与 `len()` 都退回字面量部分——两者仍然一致，`refused=set(0)/0` 就是这一格：`Z[1~10000001]` 的 `str` 是 `set(0)`、`len` 是 0，**两边都拒绝**，不是一边答空集。`kind == 1`（`Z`）和 `kind == 2`（`float1[0~0.3]`）从不声称计数，仍印 `set(Z)` 与 `set(float1 interval)`。另外 `str()` 印的始终是**概括**而不是元素表——元素表是 `list()` 的事，`str(1, 2, 3)` 过去和现在都是 `set(3)`。

## §1.18 真值有五个产生点，而且它们互不一致

**症状。** 每个需要判断真值的地方都自己写了一遍三目链，六处六个答案：

| 位置 | 空串 `""` | 数组 `[1,2]` | 字典 `{1:2}` | 集合 `(1,2)` |
| --- | --- | --- | --- | --- |
| `L_JUMP_IF_FALSE`（`if`） | 真 | 真 | 真 | 真 |
| `L_JUMP_IF_TRUE` | 假 | **真** | 假 | 假 |
| `L_OR` | 假 | 真 | 假 | 假 |
| `L_NOT` | 假 | 真 | 假 | 假 |
| `posix_core_bool`（`bool()`） | **假** | 真 | 真 | 真 |
| WIN32 `builtin_bool` | 假 | **假** | 假 | 假 |

`if s` 认为空串为真，而 `not s`、`s or x` 与 `bool(s)` 认为它为假 —— 同一份值、同一个进程、五个答案。`or` 的短路点也跟着漂：实测（修复前，用带副作用的右操作数观察）`"" or f()`、`{1:2} or f()`、`(1,2) or f()` **都不短路**，只有数组短路。

注意光看**返回值**看不出这件事：`or` 恒产出布尔，所以 `"" or 1` 与 `1 or 1` 都是 `true`。判据必须是**短路**——右操作数有没有被求值。

**设计意图。** `docs/DECFY_DESIGN.md` **§3.3 第 2 条**（写下时 `:125`）要求「`OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，**真值产生点唯一**」；**§3.4**（写下时 `:130`）要求一张 `OpCode → { VM 行为, AOT 行为, wasm 行为 }` 表作为唯一真值源，「三列不一致即构建失败」。**§1.1 表第 3 行**（写下时 `:24`）记下的规则是那条三目链的尾句 `… : (va.type == VAL_NIL) ? 0 : 1`，即**非 nil 且非零为真，空串也算真**。

**修法。** 新增唯一入口 `vm_truthy`（`src/vm/vm.c:293`，声明 `src/vm/vm.h:356`）：

```c
int vm_truthy(const Value *v) {
    switch (v->type) {
    case VAL_BOOL:  return v->ival != 0;
    case VAL_INT:   return v->ival != 0;
    case VAL_FLOAT: return v->fval != 0.0;
    case VAL_NIL:   return 0;
    default:        return 1;   /* 非 nil 的其余类型一律为真 */
    }
}
```

六处调用点全部改为调用它：`src/vm/vm.c:3341-3342`（`L_AND`）、`:3348-3349`（`L_OR`）、`:3355`（`L_NOT`）、`:3538`（`L_JUMP_IF_FALSE`）、`:3543`（`L_JUMP_IF_TRUE`），以及 `src/runtime/runtime_posix.c:69`、`src/runtime/runtime.c:68` 两份 `bool()`。三份文件里旧的三目链已 `grep -c` 为 0。`src/vm/vm.c:3896` 的 `OP_IS_NIL`（`(R[ins.r2].type == VAL_NIL) ? 1 : 0`）判的是 nil 本身而不是真值，**正确地未动**。

**为什么统一到「空串为真」而不是统一到 `bool()` 的「空串为假」。** ① 前者是 `docs/DECFY_DESIGN.md` **§1.1 表第 3 行**（写下时 `:24`）写下的规则，且六处里有三处本来就是这样；② 前者**完全不动 `if` 的控制流**，对既有 `.im` 程序的爆炸半径为零 —— 反过来统一到「空串为假」会让每个 `if s` 在 `s` 为空串时静默换分支。代价是 `bool("")` 从 `false` 变成 `true`，这是本次唯一面向用户的语义变化，单独写在明处。

**判据。** 新增 `vtest/truthiness_single_point_v06.im` ← CTest **`truthiness_single_point_runtime`（#116）**，一行断言：

```
truth estr:T:false:true:0:1 str:T:false:true:0:1 arr:T:false:true:0:1 dict:T:false:true:0:1 set:T:false:true:0:1 zero:F:true:false:1:0 one:T:false:true:0:1 fzero:F:true:false:1:0 nil:F:true:false:1:0
```

每格是 `键:if:not:bool:or调用数:and调用数`。真值一律 `T:false:true:0:1`（`or` 短路、`and` 求值），假值一律 `F:true:false:1:0`。`FAIL_REGULAR_EXPRESSION` 正对修复前的四格（`estr:…:false:1:1`、`str`/`dict`/`set` 的 `…:true:1:1`），并且已用 `grep -E` 验证过它**匹配修复前的那一行、不匹配修复后的那一行**。

**一个写测试的坑，值得单独记。** 计数器不能用全局标量：`.im` 里函数体内的 `n = n + 1` 创建的是**局部变量**，会遮蔽全局，于是 `bump()` 数的是自己的局部、调用方读的全局永远是 0。第一版就是这么写的，九个格子全印 `0:0`，看起来像「短路全对」。计数器改成全局数组的一个槽（`cnt[0] = cnt[0] + 1`）才观察得到。**一个恒为 0 的计数器比没有计数器更危险**——它给的是一个假的全绿。

**诚实边界。** ① 两个编译后端**根本走不到这些值**：AOT 的 `nv_tru`（`src/compilation/aot_native.c:193`）只处理数字，wasm 的 `cg_cond`（`src/compilation/wasm_backend.c:778-845`）对 `INT`/`BOOL`/`FLOAT` 之外的 tag 直接返回 1，而 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` 在两个后端里都是 **0** —— 字符串与容器的真值只在解释器里有定义，所以这一节的三后端一致性无从验证，只能验证解释器自洽。② `src/verse/crp.c:57` 的 `vj_truthy()` 是**另一套类型系统**，它自己的注释就写着「JS `x || fallback` truthiness」，空串为假是刻意的，不在本次范围内，也不应改。

## §1.19 函数里的 `+` 链会把一个参数折进操作数：`CONCAT` 读的是连续区间

**症状。** `func f(k, v) { return k + ":" + "z" }`：

| 调用 | 修复前 | 期望 |
| --- | --- | --- |
| `f("arr", "s")` | `arrs:` | `arr:z` |
| `f("arr", 5)` | `arr5:` | `arr:z` |
| `f("arr", [1,2])` | `Error: '+' is not defined for arrays/dicts …` | `arr:z` |

第二个参数**根本没参与**这个表达式，却出现在了结果里。把函数体改成 `return k + ":" + str(v)` 就正常，所以触发条件是「链的某个操作数不是新分配的临时寄存器」。

**机制。** 字节码把它编成了 `LOADK_STRING r3` / `LOADK_STRING r4` / `CONCAT r5, 1, 3` / `RETURN r5` —— 而 `L_CONCAT`（`src/vm/vm.c:3094`）折的是**连续区间** `R[r2 .. r2+r3-1]`。链的真实操作数在 r1（参数 `k`）、r3、r4，于是 `R[1..3]` 读到的是 `k`、`v`、`":"`，正好是 `"arr" + "s" + ":"`。**参数 `v` 是被「读」进来的，不是被「传」进来的。**

根因在发射器 `src/compiler/compiler.c` 的 `case TOK_PLUS`（`:735-808`）。它把左结合 `+` 链压平成 `ops[0..nops-1]`，注释（`:749-750`）自己就写着「OP_CONCAT assumes contiguous operand registers R[first..first+nops-1]；多寄存器操作数（INDEX/CALL/MEMBER/…）破坏连续性 -> 退回 OP_ADD」，`:751-757` 的守卫也确实挡掉了这些种类 —— 但白名单里留着 **`EXPR_IDENT`**。局部变量与参数标识符解析到**它自己已有的寄存器**，而不是新分配一个临时，于是 `first` 之后的连续区间里出现空洞，而发射器**从未检查过连续性**，直接把 `first` 和 `nops` 交给了 `OP_CONCAT`。

**修法。** 先占住整段区间，再把每个操作数**搬进**它的槽位：

```c
int first = next_register;
for (int i = 0; i < nops; i++) (void)alloc_reg();          /* 先占住 first..first+nops-1 */
for (int i = 0; i < nops; i++) {
    int r = compile_expr(comp, ops[i]);
    if (r != first + i) emit(comp->curBC, OP_MOV, first + i, r, 0);
}
emit(comp->curBC, OP_CONCAT, first, first, nops);
release_to(comp, first + 1);
```

连续性于是**由构造保证**，而不是由守卫的枚举去猜。守卫限制操作数只能是单寄存器种类（`EXPR_STRING`/`NUMBER`/`FLOAT`/`BOOL`/`IDENT`），所以每个操作数最多多分配一个临时，峰值寄存器是 `nops + 1`（原来的写法峰值也是 `nops` 量级）。`release_to(comp, first + 1)` 复现了原来「第一个操作数作为结果复用、其余临时被吃掉」的语义。

**判据。** 三后端同题验证：`.verify/v31/d2.im` 的八行（参数在链首、局部变量在链首、局部变量在链中、全字面量折叠路径、混合字面量）现在解释器、AOT、wasm 三者一致；`f("arr", [1,2])` 由**抛错**变为 `arr:z`。

**诚实边界。** 这次修的是**连续性**，不是「链式折叠本身」；`INDEX`/`CALL`/`MEMBER` 这类多寄存器操作数仍然按原设计退回 `OP_ADD`（那条路本来就正确，只是慢）。另外这个缺陷**没有退出码信号**：它不抛异常、不报错，只是安静地算错，和 §1.15/§1.17 同一类。

## §1.20 `X.type == VAL_INT ? X.ival : (int)X.fval` 读到的是 union 的另一个成员

v3.1 把 `Value` 的整数槽改成 64 位时，为了不破坏 32 字节宽度契约（`docs/DECFY_DESIGN.md` **§2 (a) 层次划分表 item ⑤**，写下时 `:76`）用了匿名 union：`int type; union { long long ival; double fval; }; char *sval; void *ptr;`。`ival` 与 `fval` **共享存储**，于是所有「只有两种类型」的取值写法都成了读错成员。

写法本身长这样：

```c
X.type == VAL_INT ? X.ival : (int)X.fval
```

它对 `VAL_INT` 正确，对 `VAL_FLOAT` 也正确，但 `VAL_BOOL` 会去读 `fval` —— 那 8 个字节里躺着的是 `ival` 的位模式。`float(true)` 因此答 **`4.9406564584124654e-324`**（整数 1 的位模式被当成 double 读出来，正好是 denormal 最小值），`float(false)`/`float(nil)` 答 0 **只是碰巧对**（位模式全零）。

三后端验证过的那批（`.verify/v31/conv3.im`，修复前 → 修复后）：

| 调用 | 修复前 | 修复后 |
|---|---|---|
| `sqrt(true)` | `2.2227587494850775e-162` | `1` |
| `float(true)` | `4.9406564584124654e-324` | `1` |
| `gc_auto(true)` | `0`（**把 GC 关掉**） | `1` |
| `atomic_add("k", true)` | `0` | `1` |
| `atomic_set("j", true)` | `0` | `1` |

`gc_auto(true)` 是最重的一格：它**静默地反向执行**，调用者以为打开了 GC。

**修法。** 补上 `val_as_double` 缺的整数对偶 `val_as_int`（`src/vm/vm.c`，声明 `src/vm/vm.h:355`），每个分支都先看 tag：

```c
long long val_as_int(const Value *v) {
    switch (v->type) {
        case VAL_INT:   return v->ival;
        case VAL_FLOAT: return (long long)v->fval;
        case VAL_BOOL:  return v->ival ? 1 : 0;
        default:        return 0;
    }
}
```

替换点：`src/runtime/runtime_posix.c` 的 `posix_core_float`（`:82-95`）、`posix_sqrt`（`:579`）、`posix_atomic_add`（`:734`）、`posix_atomic_set`（`:757`）、`posix_spi_meta`（`:923`）；`src/runtime/runtime.c` 的 `builtin_sqrt`（`:16`）、`builtin_gc_auto`（`:1550`）、`builtin_spi_meta`（`:1198`）；`src/mod/verse_dist_mod.c` 的两处端口解析（`:1953`、`:2049`）。

**审计过、确认不用改的**：`src/runtime/runtime.c:48` 与 `src/runtime/runtime_posix.c:105`（两处 `round`，上游已有 `if (xv.type != VAL_INT && xv.type != VAL_FLOAT) { … push_nil … }`）、`src/mod/json_mod.c:107`（在 `case VAL_FLOAT:` 内）、`src/vm/vm.c:3420`/`:3443`/`:3463`（本来就写成 `(idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(idxv)`），以及大量 `Value x; x.fval = 0;` 的初始化。

**判据。** 新增 CTest **`union_member_tag_runtime`（#117）**，钉住一行：

```
union-ok sqrt-true=1 sqrt-false=0 sqrt-int=2 float-true=1 float-false=0 float-nil=0 aadd=1 aset=1 gcauto-true=1 gcauto-false=0
```

FAIL 正则 `sqrt-true=2\.22|float-true=4\.94|aadd=0|aset=0|gcauto-true=0`，**已双向验证**（`grep -Ec`：修复前 1，修复后 0）。

**诚实边界。** 只改值、不改退出码 —— 和 §1.15/§1.17/§1.19 同一类，没有一条会抛异常。`sqrt` 仍然不吃字符串（`float("9")` 走 `strtod`，`sqrt("9")` 不是 3），这是另一个未修的既有缺口。WIN32 那份 `builtin_sqrt` 与 `src/mod/verse_dist_mod.c` 的两处端口解析**本机不可执行**（Linux 上不编这两个目标），可证明的只是与 POSIX 写法逐字一致。

## §1.21 门禁的 `warnings: 0` 是个永远不会失败的检查

`tools/gate.sh` 的 `stage_build` 原本这样数警告：

```sh
cmake --build "$BUILD_DIR" -j"$JOBS" || return 1     # 真正编译的那一次
local log; log="$(mktemp)"
cmake --build "$BUILD_DIR" -j"$JOBS" >"$log" 2>&1    # 再编一次，数它的输出
awk '/warning:/ { w++ } …' "$log"
```

第二次 `cmake --build` 在**热树上什么都不做**，所以它永远不产生警告，计数**只可能**是 0。实测：`touch src/runtime/runtime_posix.c` 后第一次构建 1 条警告，紧接着第二次 0 条。

也就是说，门禁每个阶段报告里那句 `✔ build（warnings: 0）` **从来没有被测过**，而它出现过的每一次门禁记录（`docs/STATUS.md` §10.50/§10.51/§10.53 的「门禁实测」段落）都把它当成「构建干净」的证据。

**它藏起来的是 34 条警告 / 23 个不同位置**（全量 `--clean-first` 构建，`.verify/v31/warn_clean.txt`）。修法是把统计挪到真正编译的那一次（日志落盘，失败时 `cat` 出来）：

```sh
local log; log="$(mktemp)"
cmake --build "$BUILD_DIR" --clean-first -j"$JOBS" >"$log" 2>&1 || { cat "$log"; rm -f "$log"; return 1; }
```

**`--clean-first` 是必需的**：只统计「第一次构建」在树恰好是脏的时候才对，而门禁经常在刚构建过的树上跑 —— 那时增量构建一个文件都不重编，计数又变回 0（这正是修完第一版后仍看到 `warnings: 0` 的原因）。清一次再编是唯一能让这个数字有意义的做法，代价是每次门禁多约一分半的编译。

`stage_build` 仍然对警告数返回 0 —— **警告是信息，不是断言**。把它变成断言需要一个与编译器版本绑定的数字，那是脆的；本仓库对已知问题的惯例是「钉住而不是藏起来」，所以这里选择**如实报告**并把这个清单记在明处。

修完后门禁第一次如实报出 **`warnings: 25`**（机械那批修掉 9 条之后），同一轮里 `errors: 0`、`100% tests passed, 0 tests failed out of 117`、`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`、`gate: OK — every stage passed.`、`GATE_RC=0`。

**已经修掉的（本轮）：**

- `-Wunused-result` 的 `fread` 七处：`src/compiler/bytecode.c:566`、`src/mod/verse_dist_mod.c:319`、`src/mod/record_mod.c:66` 与 `:193`、`src/mod/mod_posix.c:23`、`src/runtime/runtime_posix.c:582`、`src/runtime/runtime.c:17`。全部改成接住返回值并按**实际读到的字节数**收尾 —— 短读时原来的 `buf[n] = 0` 会留下未初始化字节，现在是清零（bytecode）或按 `got` 截断（其余六处）。
- `-Wunused-result` 的 `chdir` 三处：`src/main.c:280`、`:454`、`:878`。注意 **`(void)` 强制转换不能消掉 `-Wunused-result`**（实测仍然报），要写成 `if (_chdir(p) != 0) { /* best effort */ }` 让值真的被用掉。
- `_GNU_SOURCE` 重定义：`src/main.c:1` 加了 `#ifndef` 保护。

**当时剩下的 16 条 `-Wformat-truncation`（如实记在明处；下一轮全部修掉，见 §1.22）：**

| 位置 | 警告 |
|---|---|
| `src/platform/http_posix.c:553`、`:555` | `%s` 最多 32767 字节写进 505/512 字节 |
| `src/lint_mod.c:181`（×2 形态）、`:377`、`:425`、`:429` | 最多 319 / 3071 / 5503 字节写进 96–303 字节 |
| `src/mod/verse_dist_mod.c:384`、`:588`、`:590`、`:839`、`:1297` | 最多 1023 / 1199 / 8191 字节写进 1024–1400 字节 |
| `src/mod/replay_mod.c:287` | 最多 287 字节写进 236 字节 |
| `src/main.c:245` | 最多 511 字节写进 0–1023 字节 |
| `src/compiler/compiler.c:922`、`:1952` | 最多 511 / 255 字节写进 256 / 249 字节 |

这 16 条是真的缓冲区尺寸问题，每一条都需要一次判断（扩大缓冲、还是显式接受截断、还是改成拒绝），所以留作独立的一轮，不混在本次改动里。**它们是现在才第一次可见的** —— 之前那句 `warnings: 0` 把它们全部盖住了。它们已在 §1.22 全部修掉。

## §1.22 16 条 `-Wformat-truncation`：三种判断，以及一条「拒绝而不是截断」的新约定

§1.21 把 16 条 `-Wformat-truncation` 如实记在明处，本轮把它们全部修掉。门禁的 `warnings` 从 25 降到 **0**（全量 `--clean-first` 构建，`.verify/v31/warn4.txt`）。

**这些警告是真的。** `-Wformat-truncation` 不是风格问题：它说的是 `snprintf` 的目标缓冲区可能装不下格式化结果，而 `snprintf` 的回应是**静默截断**。对一条路径来说，静默截断意味着文件写到别的地方；对一条诊断信息来说，意味着用户看到半句话。GCC 只在能证明边界时才报，所以这 16 条每一条都需要一次判断。

### 判断一：目标缓冲区本来就该更大 —— 扩大它

| 位置 | 原来 | 现在 | 理由 |
|---|---|---|---|
| `src/compiler/compiler.c:912` | `char fname[256]` | `char fname[512]` | `:919` 的 `nsfull_c` 就是 `char[512]`，`:922` 整个拷进来 |
| `src/compiler/compiler.c:1952` | `char tname[256]` | `char tname[264]` | 固定前缀 `"sprite#"` 7 字节 + `sname[256]` |
| `src/mod/replay_mod.c:286` | `char tail[256]` | `char tail[448]` | 固定文本 40 + `key_esc[288]` + `g_prev_hash[65]` = 最坏 391 |

这三处的共同点：**缓冲区比它要装的东西小，而装的东西的尺寸在编译期就能算出来**。扩大是唯一不丢信息的改法。

### 判断二：截断是可接受的，但要显式写出来 —— 加精度

| 位置 | 改法 |
|---|---|
| `src/lint_mod.c:377` | `"%s"` → `"%.95s"`（源和目标都是 `char[32][96]`） |
| `src/lint_mod.c:425`、`:429` | `%s`（`variant[8]`）→ `%.7s`；`%s`（`type_name[64]`）→ `%.63s`，最坏 165 < 320 |
| `src/mod/verse_dist_mod.c:588`、`:590` | `"http://%s/v/%s"` → `"http://%.500s/v/%.680s"`，最坏 1190/1194 < 1200 |
| `src/platform/http_posix.c:553`、`:555` | `"http://%s/ping"` → `"http://%.499s/ping"`；`"%s/ping"` → `"%.506s/ping"` |

显式精度把「编译器无法证明」变成「编译器能证明」，代价是承认超长输入会被截断。对诊断信息和探活 URL 来说这是可接受的 —— 截断后的 ping URL 会在 hub 上响亮地失败，而不是安静地访问错误的资源。

**注意 `src/platform/http_posix.c:556` 的第一版精度 `%.500s` 仍然报同一条警告**：`"http://"`(7) + 500 + `"/ping"`(5) = 512，正好等于 `sizeof url`，但还要一个 NUL，所以差一字节。改成 `%.499s` 才是 7 + 499 + 5 = 511 + NUL = 512。**`snprintf` 的容量包含结尾 NUL**，这是这类 off-by-one 警告的常见来源。

### 判断三：路径不能截断 —— 改成拒绝

`src/lint_mod.c:181` 的 `lint_add` 往 `LintBuf.lines[64][200]` 写调用方的 `char msg[320]`。这里没有「扩大」这个选项：`LintBuf` 已经是 12.8 KB，而且可能是栈上分配的。改成前缀 `snprintf` + 显式 clamp + `memcpy` 余量 + NUL —— **`msg` 完全不再走 `%s`**，编译器因此能证明边界。截断的是诊断正文，前缀（行号和 tag）始终完整。

路径类的四处（`src/main.c:247`、`src/mod/verse_dist_mod.c:384`、`:839`（两处）、`:1297`）换成 `im_platform_path_join`：

```c
int im_platform_path_join(char *buffer, size_t capacity, const char *base, const char *part);
```

它在 `src/platform/platform.c:122-133`，装不下时返回 **-1**（`return (written < 0 || (size_t)written >= capacity) ? -1 : 0;`），同时会去掉 `base` 结尾和 `part` 开头多余的分隔符，并按平台选 `/` 或 `\`。

**为什么是拒绝而不是扩大缓冲区**：静默截断的路径会把文件写到**错误的位置** —— 那是数据损坏，而且是安静的；被拒绝的 join 只是一个被跳过的条目。调用方现在一律 `if (im_platform_path_join(...) != 0) continue;`。

因为 `im_platform_path_join` 自己就按平台选分隔符，`src/main.c:247` 和 `src/mod/verse_dist_mod.c:384` 里那两段 `#ifdef _WIN32` 的 `/`→`\` 转换循环随之删掉。

**这条约定值得单独记住**：本仓库原来的路径拼接是 `snprintf(dst, sizeof dst, "%s/%s", a, b)` —— 截断、且不告诉任何人。以后新写的路径拼接一律走 `im_platform_path_join`。

### 钉住

`src/platform/platform_probe.c` 是既有测试（CTest `platform_probe`，注册在 `CMakeLists.txt:201`，目标建在 `:32`），本轮把 join 的语义补进去，**没有新增 CTest、`EXP_CTEST` 不变（117）**：

```c
char tiny[8];
if (im_platform_path_join(tiny, sizeof tiny, "/tmp", "inimerse") != -1) return 6;
if (im_platform_path_join(NULL, sizeof tiny, "/tmp", "inimerse") != -1) return 7;
if (im_platform_path_join(tiny, 0, "/tmp", "inimerse") != -1) return 8;
char a[64], b[64];
if (im_platform_path_join(a, sizeof a, "/tmp/", "/inimerse") != 0) return 9;
if (im_platform_path_join(b, sizeof b, "/tmp", "inimerse") != 0) return 10;
if (strcmp(a, b) != 0) return 11;
```

即：装不下必须拒绝、`NULL`/零容量必须拒绝、多余分隔符必须归一化。

### 诚实边界

- **没有为「路径过长时跳过条目」写端到端测试。** 要触发它需要构造一条超过 1024 字节的路径，而 `zip_extract_all` / verse jar 解包都不可从 `.im` 直接调用。能钉住的只有 `im_platform_path_join` 本身的语义（上面），拼接点是否都检查了返回值只由代码审阅保证。
- **`warnings: 0` 现在是真的，但仍然不是断言。** `stage_build` 对警告数返回 0；把它做成断言需要一个与编译器版本绑定的数字。这一轮之所以能说「归零」，是因为全量 `--clean-first` 构建的输出被落盘并数过（`.verify/v31/warn4.txt`），而不是因为门禁说 0。
- **WIN32 目标本机不编**，所以 `src/main.c` / `src/mod/verse_dist_mod.c` 的 Windows 分支只有与 POSIX 逐字一致这一层保证。
- **`-Wformat-truncation` 只是可见警告的一部分。** 这一轮清掉的是 GCC 当前愿意报的那些；换编译器版本可能报出新的。

## §1.23 `+` 里非字符串的左操作数被静默丢掉

**症状。** `+` 的规则从来只有一条：**任一操作数是字符串，整个表达式就是拼接**——`"7" + 1` 一直是 `"71"`。但这条规则只实现了一半：右操作数是字符串时，左操作数会消失。

| 表达式（`x = 5`，`y = "7"`） | 修复前 | 修复后 |
| --- | --- | --- |
| `x + y` | `7` | `57` |
| `x + "a"` | `a` | `5a` |
| `nil + "x"` | `x` | `nilx` |
| `true + "x"` | `x` | `truex` |
| `5.5 + "x"` | `x` | `5.5x` |
| `x + y + "8"` | `78` | `578` |
| `x + y + x` | `75` | `575` |
| `"a" + x` | `a5` ✓ | `a5` ✓ |

退出码全程 0，只有值不对——**静默数据丢失，不是优先级问题**。最后一行是本来就对的那一侧，留作回归护栏。

**机制。** `L_ADD`（`src/vm/vm.c`）原文：

```c
const char *sa = a->sval?a->sval:"", *sb;
char sbuf3[128];
if (b->type == VAL_STRING) sb = b->sval?b->sval:"";
else { value_to_string(vm, b, sbuf3, sizeof sbuf3, 0); sb = sbuf3; }
```

**右边走了 `value_to_string`，左边直接读 `a->sval`。** 只有 `a` 确实是字符串时这才对；对别的 tag，`sval` 是那个 `char *` 成员上一次留下的值（它**不在 union 里**，所以不会被 `ival` 的写入覆盖），刚 load 出来的整数那里是 NULL，于是左边贡献空串。`L_CONCAT` 的一般折叠路径有**逐字相同**的不对称（`const char *sa = acc.sval ? acc.sval : ""`），所以三项以上的链丢的是同一个操作数——这正是为什么 2 项链（走 `OP_ADD`）和 3 项链（走 `OP_CONCAT`）必须用同一条规则。

**修法。** 两处都改成对称选择：类型是 `VAL_STRING` 就取 `sval`，否则 `value_to_string` 进一个新的 `abuf3[128]` / `abuf2[128]`。两处都加了注释说明「2 项链由 `OP_ADD` 折叠、3 项以上由 `OP_CONCAT` 折叠，两条规则必须一致」。

**判据。** 新增 CTest **`concat_left_operand_runtime`（#118）**，钉一行 `concat-ok intstr=57 strlit=5a nil=nilx bool=truex float=5.5x chain3=578 chainix=575 rev=a5`；FAIL 正则 `concat-ok intstr=7 |strlit=a |nil=x |bool=x |float=x |chain3=78 |chainix=75 ` **已双向验证**（`grep -Ec`：修复前 1、修复后 0）。末尾的空格是刻意的：引擎会把字符串池 dump 到 stderr，而 CTest 连 stderr 一起捕获，带尾空格的模式匹配不到池里的任何字面量。`EXP_CTEST` **117 → 118**。

### 诚实边界

- **只有解释器有这条路径。** AOT 的 `nv_add`（`src/compilation/aot_native.c:220-225`）压根不处理字符串（`emit_expr` 只有 `EXPR_NUMBER` 一个 case），wasm 同样拒绝字符串。所以三通道差分模糊测试**测不到它**——这已经是本系列又一个只在解释器里的缺陷。
- **f-string 从没走过坏路径。** `$"sum={name + 1}"` 被明确拒绝（`Error: f-string interpolation only supports plain identifiers inside {}`），而 `parse_fstring` 生成的链第一个操作数永远是字面量字符串，所以坏的那一侧从没被触发。
- **编译器无辜。** `x = 1; y = "7"; z = x + y` 的字节码是正确的 `OP_ADD`（`LOAD_GLOBAL r1 = x`、`LOAD_GLOBAL r2 = y`），缺陷在 VM 的拼接分支里。

## §1.24 `atomic_add`/`atomic_set` 比语言本身窄

**症状。** 整数层从 v3.1 起是 int64（`str(3000000000)` 就是 `3000000000`），但 `atomic_set` / `atomic_add`
中间穿过一个 `int`，槽里落下的于是是另一个数。实测（`.verify/v31/atom.im`）：

| 表达式 | 修复前 | 修复后 |
| --- | --- | --- |
| `atomic_set("k", 3000000000)` 后 `atomic_get("k")` | **-1294967296** | 3000000000 |
| `atomic_add("k", 3000000000)`（槽初值 0） | **-1294967296** | 3000000000 |
| `atomic_add("k", 2147483648)`（槽初值 0） | **-2147483648** | 2147483648 |
| `atomic_add("k", 7)` | 7 | 7（不变） |
| `atomic_add("z", 1)`，`z` = 9223372036854775807 | **0**，槽被写成 -1 | 抛 `numeric_overflow`，槽**不变** |

**退出码全程 0，只改值** —— 与 §1.15 / §1.17 / §1.23 同类的静默数据损坏。`atomic_get` 自身一直是对的，
所以这个缺陷**只在写之后才看得见**：读一个从没写过的槽永远返回 0。

**机制。** POSIX（`src/runtime/runtime_posix.c`）：

```c
int d = (int)val_as_int(&delta);                                    /* 32 位 */
int old = __sync_fetch_and_add(&vm->globals[idx].val.ival, d);      /* 32 位返回 */
push_int(vm, old + d);                                              /* 32 位加法 */
```

`posix_atomic_set` 同样是 `int val = (int)val_as_int(&value);`。槽本身是 `long long`，
三步各窄一次，3000000000 在第一步就变成 -1294967296。WIN32 的三个函数（`src/runtime/runtime.c`）
逐字对应地窄：`builtin_atomic_add` 用 `InterlockedExchangeAdd(…, (LONG)delta)` 配
`push_int(vm, (int)(old + delta))`，`builtin_atomic_get` 用 `(int)InterlockedCompareExchange(…, 0, 0)`，
`builtin_atomic_set` 用 `InterlockedExchange(…, (LONG)val)` 配 `push_int(vm, (int)val)` —— **三个都窄**，
不是只漏了一个。

对照 `posix_atomic_get`（`:744-751`）一直是 `__sync_add_and_fetch(&…ival, 0)`，`long long` 进已经加宽过的
`push_int` —— 这正说明缺陷是「写」那半边独有的。

**为什么不能只是加宽。** 语言对整数溢出有明确的规矩：`+` 抛 `numeric_overflow`
（`x = 9223372036854775807; say x + 1` → `[exception] uncaught: numeric_overflow`，退出码 1）。
裸的 `__sync_fetch_and_add` / `InterlockedExchangeAdd` **看不见自己的溢出** —— 它们先写再返回旧值，
所以「加宽」本身会把「静默地少 2^32」换成「静默地回绕到 INT64_MIN」，只是把缺陷挪大。要能判溢出，
就必须**先算后写**，也就是 CAS 循环。

**修法。** 两处 `atomic_add` 都改成 CAS 循环，POSIX 用 `__builtin_add_overflow` 判、WIN32 因为
MSVC 没有这个内建函数而手写 `LLONG_MAX`/`LLONG_MIN` 边界；溢出时 `vm_throw_kind(vm, "numeric_overflow")`
并**原样留下槽**（不部分生效）。`atomic_set` 只需把 `int` 换成 `long long` / `LONG64`，
并把 WIN32 那两个手写三元表达式换成 `val_as_int`（顺带修掉 `atomic_*(…, true)` 从 0 到 1，与 §1.20 同源）。
`runtime.c` 补 `#include <limits.h>`。三个 `Interlocked*` 调用全部换成 `Interlocked*64` 打在
`(volatile LONG64*)&…ival` 上。

**判据。** 新增 CTest **`atomic_int64_width_runtime`（#119）**，钉一行
`atomic-ok set=3000000000 add=3000000000 get=3000000000 add2p31=2147483648 get2=2147483648 small=7 getsmall=7 ovf1=numeric_overflow keep1=9223372036854775807 half=9223372036854775807 ovf2=numeric_overflow keep2=9223372036854775807`；
FAIL 正则 `set=-1294967296|add=-1294967296|get=-1294967296|add2p31=-2147483648|get2=-2147483648|ovf1=0 |half=-1 |ovf2=0 `
**已双向验证**（`grep -Ec`：修复前 1、修复后 0）。溢出那半边用 `try { … } catch (err) { … }`
（`docs/SYNTAX.md` §4.4 错误处理，写下时 `:339-340`）接住错误，`str(err)` 得到错误种类名 `numeric_overflow` —— 这样测试仍然只断言
**值**，不依赖退出码，也就不需要 `WILL_FAIL` 记账项。`keep1` / `keep2` 两格钉的是「被拒绝的加法不改槽」。

**诚实边界。**

- **WIN32 那三个函数本机不可执行。** 该目标不在 Linux 上构建，所以只能证明它们与 POSIX 逐字同构、
  并且两个文件里再没有残留的 32 位 `InterlockedExchangeAdd(` / `InterlockedExchange(` /
  `InterlockedCompareExchange(` / `(int)val_as_int`（`grep` 为空）——**不是**跑过。
- **两份实现的溢出判定故意不同构。** POSIX 用 `__builtin_add_overflow`，WIN32 用手写的
  `LLONG_MAX`/`LLONG_MIN` 边界，因为 MSVC 没有这个内建函数。语义相同，文本不同。
- **字符串参数仍然等于 0。** `atomic_add("k", "7")` 走 `val_as_int` 的 `default` 分支得 0，与
  `sqrt("9")` 不是 3 一致，但和 `int("7")` = 7 不是一回事。本轮不改。
- **`atomic_*` 在此之前没有任何文档。** `grep -rn atomic docs/*.md` 除 §1.20 自己的表格外没有命中，
  所以这一节是它第一次被写下来；也因此没有历史行为可供对照。


## §1.25 同一段程序在两个平台上意思不同：`sum()` 的非数字元素

**症状。** 容器里出现一个非数字元素时，POSIX 的 `sum()` 答 `nil`，而 WIN32 的 `sum()` 抛
`sum: non-numeric element`。同一个 `.im` 程序因此在两个平台上**意思不同**：

| 程序 | POSIX（修复前） | WIN32（修复前） |
| --- | --- | --- |
| `say sum([1, "a"])` | `nil` | 抛 `sum: non-numeric element` |
| `say sum([true, 2])` | `nil` | 抛 |
| `say sum([1, nil])` | `nil` | **`1`**（nil 被 `val_as_double` 当成 0） |
| `say sum([1, "a"]) + 1` | **`1`** | 抛 |

最后一格是真正的伤害：`nil` 被算术吸收（`nil + 1` == `1`），所以「求和失败」这个信号**不可观测** ——
调用方分不清「和是 1」和「和失败了」。

**这不是一处没注意到的分歧。** §1.15 当初明确写了「保留它自己的错误风格（非数字元素
`vm_throw_msg(...)`，而不是 POSIX 的 nil）」。那是个**保守**决定（各副本保持原样），不是一个
正确性论证。本节把它推翻，理由三条：① **平台相关的程序含义本身就是缺陷** —— 门禁里有
`differential fuzz (interp vs AOT)` 这一阶段，正因为「同一个程序在不同通道上不同」被当作缺陷，
而 POSIX 与 WIN32 之间**没有任何门禁**；② `nil` 作为失败信号在这里不可观测（上表最后一格）；
③ 仓库自己的 §1.14（`docs/AUDIT.md:624`「v3.1 整数位宽」）规定的是**算术**溢出要抛而不是回绕 —— **它只管算术，不是「任何内建遇到不认识的参数类型就抛」的通用教条；本节原先把它升格成了通用教条，更正见 §1.46。**

**顺带修掉的第二处。** WIN32 的元素规则是 `if (t == VAL_STRING || t == VAL_BOOL) { ok = 0; break; }`
—— **`VAL_NIL` 不在拒绝之列**，于是 `val_as_double(nil)` 得 0.0，`sum([1, nil])` 在 Windows 上答 1。
POSIX 的规则是 `t != VAL_INT && t != VAL_FLOAT`，更严。统一到 POSIX 那条。

**修法。** 两份实现现在逐字同构：

```c
    const Value *items = NULL; int n = 0, enumerated = 0, bad_element = 0, all_int = 1;
    ...
    if (enumerated) for (int i = 0; i < n; i++) {
        int t = items[i].type;
        if (t != VAL_INT && t != VAL_FLOAT) { bad_element = 1; break; }
        ...
    }
    pop(vm);
    if (bad_element) { vm_throw_msg(vm, "sum: non-numeric element"); return 1; }
    if (!enumerated) { push_nil(vm); return 1; }
    if (!all_int) { push_float(vm, fsum); return 1; }
    if (oflow) { vm_throw_kind(vm, "numeric_overflow"); return 1; }
    push_int(vm, isum); return 1;
```

关键是把原来那**一个** `ok` 标志拆成 `enumerated` 与 `bad_element` 两个 —— 三种原因必须给三种信号：

| 原因 | 信号 | 为什么 |
| --- | --- | --- |
| 元素不是数字 | **抛** `sum: non-numeric element` | 输入坏了；nil 会被算术吸收，信号消失 |
| 集合无法枚举（无界，如 `Z`） | `nil` | 与 `list(Z)` 一致，是 §1.10 定的「拒绝而不是截断」惯例 |
| 参数根本不是容器 | `nil` | 同 `list(123)` |

POSIX 原来那版 `if (!ok) { push_nil(vm); return 1; }` 把三种原因压成一种；WIN32 那版靠 `pop` 之后
再读 `v->type` 来区分，而 `v` 是指进栈的指针 —— 现在两种情况都在 `pop` **之前**就决定好。

**判据。** 既有 CTest **`sum_components_int64_runtime`（#114）** 就地扩展，不新增 CTest
（`EXP_CTEST` 保持 119）：值那半仍是原来那行，末尾接 `refuse-bool=` / `refuse-str=` / `refuse-nil=`
三格，各用 `try { … } catch (e) { … }` 接住并断言 `str(e)`。FAIL 正则
`comp=0|two=0|single=0|big=9007199254740992|refuse-bool=nil|refuse-str=nil|refuse-nil=nil`
**已双向验证**（`grep -Ec`：修复前 1、修复后 0）。`sum: non-numeric element` **不进字符串池**
（实测 `grep` 计数 0），所以 PASS 正则引用它是安全的。全套 ctest **119 / 119**，`contract_test.im`
**66 passed / all passed**。

**诚实边界。**

- **WIN32 那份本机不可执行。** `CMakeLists.txt:426-436` 的 `if(WIN32)` 分支（`list(APPEND INIMERSE_ENGINE_SOURCES` 那一段）把 `src/runtime/runtime.c` 放在 `:429`
  分支里，Linux 上编译的是 `runtime_posix.c`。对 `runtime.c` 只有两条证据：与 POSIX **逐字同构**，
  以及两个文件里不再有旧的 `ok` / `val_as_double(&items[i])` 写法。**没有执行过** —— 打 Windows 包时
  它是第一次真正被编译。
- **`err_test.im` 是未被门禁覆盖的旧文件**，它早就假设 `sum` 会抛（`try { say sum(sx) } catch`）。
  本轮之后那一段终于走到 `catch`；同一文件里 `round("abc", 2)` 与 `list(123)` 仍答 `nil` 并打印
  `should not print`，那是另一类（`nil` 是 `list`/`round` 的既定拒绝惯例），不在本轮范围。
- **`sum` 只读栈顶一个参数**，`sum(1,2,3,4)` 与 `sum("a","b")` 的行为不由 `argc` 决定。
  本轮没有改动这一点。


## §1.26 没有顺序的一对值，比较却答「0」

**症状。** `val_cmp` 在既不是「两边都是字符串」也不是「两边都是整数」时，落到
`val_as_double(a) < val_as_double(b)`，而 `val_as_double` 的 `default` 是 **0.0** ——
于是**每一个非数字值都按数字 0 参与比较**。实测（修复前）：

| 程序 | 结果 | `==` 的说法 |
| --- | --- | --- |
| `"abc" < 1` | **true** | `"abc" == 0` → false |
| `"abc" > -1` | **true** | 同上 |
| `"abc" <= 0` / `"abc" >= 0` | **true / true** | 同上 |
| `[1, 2] < 1` | **true** | `[1, 2] == 0` → false |
| `(1, 2) < 1`（集合） | **true** | — |
| `nil <= 0` | **true** | `nil == 0` → false |
| `0 <= nil` | **true** | 同上 |
| `nil < 1` | **true** | — |
| `nil > -1` | **true** | — |

两条独立的伤害：

1. **`<=` 与 `==` 自相矛盾。** `nil <= 0` 与 `0 <= nil` 同时为真，而 `nil == 0` 为假 ——
   即 `a <= b && b <= a` 为真却 `a != b`。任何「用 `<=` 建立等价、再用 `==` 判断」的代码
   都会得到两个答案。
2. **比较本身答错了。** `"abc" < 1` 为真没有任何读法成立：字符串不是数字，也不是 0。

**顺带一处：`max()` 把 nil 丢掉。** `s = nil, 5` 时 `max(s)` 答 **5**（nil 被当成 0 比较、输掉），
而两个参数的 `min(nil, 1)` 早就答 **nil**。同一个问题两种拼写两个答案。

**修法。** 顺序只在**两个字符串之间**或**两个数字之间**有定义（数字 = `VAL_INT` /
`VAL_FLOAT` / `VAL_BOOL`）；其它任何一对**拒绝**而不是答 0：

```c
static int val_is_num(const Value *v) {
    return v->type == VAL_INT || v->type == VAL_FLOAT || v->type == VAL_BOOL;
}
static int val_orderable(const Value *a, const Value *b) {
    if (a->type == VAL_STRING && b->type == VAL_STRING) return 1;
    return val_is_num(a) && val_is_num(b);
}
```

`L_LT`/`L_GT`/`L_LE`/`L_GE` 四个操作码先查 `val_orderable`，不成立就
`vm_throw_kind(vm, "type_mismatch")`（2202，`IM_ERROR_DOMAIN_TYPE_VM`，已经在
`src/vm/vm.c` 的 `L_SET_GLOBAL` 路径上用过）。`set_minmax` 的三处 `val_cmp` 也加同一个前置检查，
不成立就沿用它已有的 `have = -1` 拒绝路径。

**这是「拒绝而不是算错」的又一处**（§1.10 的枚举器、§1.14 的整数溢出、§1.25 的 `sum` 同一族）。
`nil` 因此**不可排序**：`nil <= nil` 也拒绝，与 Python 的 `None <= None` 抛 `TypeError` 一致。

**修复后实测**（`vtest/order_requires_orderable_v06.im`）：

```
order-ok strnum=type_mismatch arrnum=type_mismatch nille=type_mismatch zerole=type_mismatch
         nillt=type_mismatch streq=false strlt=true strgt=false numlt=true numle=true
         boollt=true biglt=true minset=nil maxset=nil minscalar=nil
```

后半段是**反方向的判据** —— 它挡住「把 `<` 一律改成抛异常」这种假修复：
`"abc" < "abd"` 仍是 true、`"abc" > "abd"` 仍是 false、`1 < 2` / `2 <= 2` / `true < 2` /
`9007199254740993 < 9007199254740994` 全部不变。

**判据。** 新 CTest **`order_requires_orderable_runtime`**（#120），`EXP_CTEST` **119 → 120**。
FAIL 正则 `strnum=true|arrnum=true|nille=true|zerole=true|nillt=true|maxset=5 `
**已双向验证**（`grep -Ec`：修复前 **1**、修复后 **0**；修复前的整行是
`… strnum=true arrnum=true nille=true zerole=true nillt=true … maxset=5 minscalar=nil`）。
全套 ctest 从 119 升到 **120，0 失败** —— 也就是说**门禁里没有任何用例依赖这个强制转换**，
blast radius 为零（实测，不是推断）。

**诚实边界。**

- 这一处只在解释器里：AOT 的 `emit_expr` 只有 `EXPR_NUMBER` 一个 case，wasm 拒绝字符串，
  所以三通道差分模糊测试**够不到**这一处（同 §1.23）。
- `set_minmax` 对**集合里**的混合类型现在也拒绝（`min(s)` / `max(s)` 都答 nil），
  这与两个参数形态的 `min(nil, 1)` = nil 一致；但集合元素里如果有数组或字典，
  以前是「按 0 比较」，现在是拒绝 —— 两者都不是「对」，只是后者不再声称一个数字。
- **`val_cmp` 本身没有加运行时断言**：它仍是 `static`，前置条件靠四个操作码与 `set_minmax`
  各自检查，加注释说明。如果将来有新调用方忘了查，缺陷会以「又答 0」的形式回来。


## §1.27 仓库自己的 `src/platform/process.h` 挡住了 CRT 的同名头，Windows 构建因此红了 158 个提交

**症状。** Windows 构建（MSYS2 UCRT64）在 `39ddabd`（2026-10-02 `verse: engine-side UPP framing and session state machine`）之后一直红，最后一次绿是 `a2a583d`（v0.4.1），中间 158 个提交全红 —— 也就是说 Windows 运行时**从来没被验证过**。`ninja -k 0` 报出 7 个错误、落在 5 处，全部是「函数没有声明」：

- `src/platform/thread.c:24`、`src/headless_server.c:188`、`src/mod/gui_mod.c:3199`：`implicit declaration of function '_beginthreadex'`
- `src/common/vverse_pack_probe.c:42`：`implicit declaration of function '_getpid'`
- `src/verse/upp_probe.c:884`、`src/verse/crp_probe.c:1210`：`implicit declaration of function 'getline'`
- `src/common/vverse_pack.c:151`/`:156`：`_stat` 填进 `struct stat`，类型不符

**根因（前三类里的五处）。** 仓库自带 `src/platform/process.h`，而 CMake 把 `src/platform` 加进了 include 路径（`CMakeLists.txt:25` 的 `inimerse_platform`，以及各探针的 `target_include_directories`）。于是 `#include <process.h>` 拿到的是**仓库自己的头** —— 它只有 `im_process_*` 那一组声明，没有任何 CRT 函数。证据：`gcc -M` 的依赖列表里出现的是 `src/platform/process.h`，`C:/msys64/mingw64/include/process.h` **根本没被打开**；同一段代码不加 `-Isrc/platform` 就能过。mingw-w64 与仓库**同名**的头还有两个：`dir.h`、`parser.h`（`ls /mnt/c/msys64/mingw64/include/{dir,parser,process}.h` 三个都在）。仓库目前没有 `<dir.h>` / `<parser.h>` 的尖括号引用，所以它们只是同类隐患，尚未发作。

具体是哪些声明丢了：mingw-w64 的 `process.h:69-70` 把 `_getpid` 包在 `#ifdef _CRT_USE_WINAPI_FAMILY_DESKTOP_APP` 里，而 `corecrt.h:461-470` 只在 `WINAPI_FAMILY` 未定义（或分区到桌面）时才定义那个宏；`_beginthreadex` 同理在 CRT 头里。仓库头一挡，两者都消失。

**修法。** 把仓库头改名：`src/platform/process.h` → `src/platform/im_process.h`（用 `git mv`，`git log --follow` 仍追得到），并更新 5 个引用点 —— `src/platform/process.c:1`、`src/platform/process_probe.c:1`（`"process.h"`）、`src/child_proc.h:6`（`"platform/process.h"`）、`src/runtime/runtime_posix.c:588`、`src/mod/server_mod_posix.c:2`（`"../platform/process.h"`），外加三处文档反引号引用（`docs/API.md:293`、`docs/STATUS.md:361` [obs: ab70a71 ≡ main]、`docs/archive/ROADMAP.md:62`）。改名之后那 5 个 `#include <process.h>` 自然解析到 CRT 头。**没有选 `#include_next <process.h>`**：它一行就能解决，但那是 GCC 专有扩展；改名是纯标准 C，而且把「仓库头不该与系统头同名」这条规则真正修好，`dir.h`/`parser.h` 的同类隐患也照此办理。

**第五类（`getline`）与上面无关，是另一件事。** `#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)` 那段守卫**不是**原因：mingw-w64 的 `<stdio.h>` 里 `getline` **一次都没出现**（`grep -c getline` 在 MINGW64 与 UCRT64 两个 sysroot 上都是 **0**），改宏、把宏提前、不定义宏，三个最小复现都报同一个 implicit declaration。因此加本地 shim `src/common/probe_compat.h`：`#if defined(_WIN32)` 下 `probe_getline`（`fgets` + `realloc` 增长循环，遇 `\n` 返回读到的字节数，EOF 无数据返回 -1），随后 `#define getline probe_getline`，语义与 POSIX 一致。**没有选「把这两个探针从 Windows 构建里排除」** —— 那会悄悄删掉 Windows 的 upp/crp 覆盖，而 CTest 正是靠它们。

**第六类（`VV_STAT`）。** `src/common/vverse_pack.c` 的 `_WIN32` 分支把 `VV_STAT` 定义成 `_stat`，而 `_stat` 是宏、展开为 `_stat64i32`（`_mingw_stat64.h:22`），它填的是 `struct _stat64i32`；代码里的变量却是 `struct stat`（同文件 `sys/stat.h:137` 的另一个布局）。加 `VV_STAT_T`（Windows `struct _stat64i32` / POSIX `struct stat`），`path_is_dir`、`path_is_file` 两处改用它。

**验证。** 本机可以直接调用 Windows 侧工具链：`/mnt/c/msys64/mingw64/bin/gcc.exe`（gcc 16.1.0，Rev5）。对**全部** `src/**/*.c` 做 `-fsyntax-only` 扫描、只筛 `implicit declaration`：修复前命中 5 处，修复后只剩 `src/platform/http_probe.c:93` 的 `setenv` —— 而该文件在 `CMakeLists.txt:213` 的 `if(NOT WIN32)` 里（`add_executable(http_probe …)` 在 `:214`），不参与 Windows 构建，因此不是 Windows 缺陷。逐个确认 PASS：`src/verse/upp_probe.c`、`src/verse/crp_probe.c`、`src/common/vverse_pack_probe.c`、`src/common/vverse_pack.c`、`src/platform/thread.c`、`src/headless_server.c`、`src/mod/gui_mod.c`、`src/vm/vm.c`。

**诚实边界。** ① 本机 MSYS2 **没装 cmake.exe**，所以我做的是逐编译单元的 `-fsyntax-only`，不是完整 Windows 构建；完整证据（164/164 干净重建、`inimerse.exe` 链接成功）来自发布会话在**仓库外克隆**上的实测。② Linux 门禁**永远看不见**这一类缺陷：`tools/gate.sh` 跑在 Linux 上，glibc 没有 `<process.h>`，所以这道门禁此前红不了、以后也挡不住同类的 Windows-only 编译错 —— 能挡住它的只有 Windows CI，而 CI 自己红着的时候没人看。③ 编译修好之后 Windows 的 ctest 只有 **54/84**（30 项失败：9 项段错误、9 项超时、4 项 Failed、17 项 Not Run），那些是**运行时**缺陷，与本节无关，本轮**故意不修**（发布会话正在请用户决定是带已知问题发版还是修到全绿）。

## §1.28 `vm_init` 是一张字段清单，清单漏了两个字段

**现象。** Windows 上 18 个 CTest 用例以 `0xC0000005` 段错误退出，栈完全相同：`rip=0x7ff7584c5673`，模块基址 `0x7ff7584a0000` ⇒ RVA `0x25673` ⇒ `prof_record_call+0x73`（函数入口 `0x140025600`）。`objdump -dS` 给出的故障指令是 `140025673: mov %ebx,0x8(%r12)`，对应 `src/compilation/profiler.c:101` 的 `fr->depth = depth;`，其中 `fr = &st->stack[st->stack_count++]`、`st = (ProfState*)vm->prof_state`。

**根因。** `src/vm/vm.c` 的 `vm_init` 原本是一张**逐字段赋值清单**，没有任何 `memset`，而清单里从来没有 `prof_enabled`（`src/vm/vm.h:309`）和 `prof_state`（`src/vm/vm.h:310`）。两个调用点 `src/main.c:949` 与 `src/main.c:979` 都是**栈上**的 `VM vm; vm_init(&vm);`，于是这两个字段是栈上的垃圾值：`vm->prof_enabled` 非 0 时，`src/vm/vm.c:3930` 的 `if (vm->prof_enabled) prof_record_call(...)` 就用一个垃圾 `prof_state` 指针去记录调用。Linux 之所以是绿的，只是因为新栈页恰好是零 —— **这不是「Linux 对」，是「Linux 没被照到」**。

**这不是两个字段的疏漏，是清单这种写法的必然结果。** `vm_init` 自己的注释（原 `src/vm/vm.c:1407`）已经写着「追加字段必须清零」——作者知道规则，清单还是漂了。把 `vm_init` 里的 `vm->X` 赋值集合与 `src/vm/vm.h` 的结构体声明做差，**12 个字段一次赋值都没有**：`hookCount`、`spi_sub_count`、`spi_sub_cap`、`spi_subs`、`user_data`、`argc`、`argv`、`cur_argc`、`prof_enabled`、`prof_state`、`main_thread`、`mod_bcs`；另有整组 `im2d_*`（`im2d_interval_ms`、`im2d_next_frame`、`im2d_ready`、`im2d_dt`、`im2d_scene[64]`、`im2d_last_scene[64]`、`im2d_cb_load/update/render`）同样一次都没有。

**修法：`memset(vm, 0, sizeof(VM))` 放在 `vm_init` 最前面，清单原样保留。** 保留清单不是冗余，而是因为它设置的是**正确值不是 0** 的那些字段（`sp = -1`、`exec_timeout_ms = 120000`、`mod_caps = -1`、`record_save_path = strdup("save.dat")`、`ent_free_head = -1`）——清零保证起点确定，清单保证意图不被清零替代。**为什么 `memset` 不会破坏 `-1` 哨兵**：`im2d_cb_load/update/render` 的 `(-1=未解析, -2=不存在)` 只在 `src/vm/vm.c:2529` 的 `if (!vm->im2d_ready)` 里被读取，而 `:2526` 的 `if (vm->im2d_interval_ms <= 0) return 0;` 先把整条 `vm_frame_callback` 挡在门外；清零后两个守卫都读 0，哨兵在第一次被读之前就被重新建立。

**同一轮删掉的调试残留。** `src/vm/vm.c:1312-1313` 在 Windows 上会往 stderr 打 `[TBP] timeBeginPeriod(1) result=%u (0=OK)` —— 这是发布阻塞项，改为 `(void)timeBeginPeriod(1);`（POSIX 侧 `src/vm/vm.c:157` 本来就有同名的空桩）。

**判据（新增 CTest `vm_init_probe`，#121）。** 新探针 `src/vm/vm_init_probe.c` 先用 `memset(&vm, 0xAA, sizeof vm)` **把结构体填成脏值**，再调 `vm_init`，然后逐项断言上表全部字段为零、以及清单提供的非零默认值仍然正确。**预填 0xAA 才是这条判据的全部意义**：它让「删掉 memset」在任何平台上都红，而不是只在栈恰好是脏的那一台上红。**双向验证**：正常构建 `vm_init_probe: OK`（exit 0）；把 `memset(vm, 0, sizeof(VM));` 换成空注释后重编，探针打印 **25 条 `FAIL`**（`prof_enabled`、`prof_state`、`hookCount`、`spi_subs`、`spi_sub_count`、`spi_sub_cap`、`user_data`、`argc`、`argv`、`cur_argc`、`main_thread`、8×`mod_bcs[i]`、6×`im2d_*`）并以 exit 1 结束；还原后逐字节相同、复绿。

**探针自己的一处修正。** 第一版负控**丢掉了全部 FAIL 行**：检查失败时结构体按定义是未定的，`vm_free(&vm)` 跟着垃圾指针走，进程在 stdout 被 flush 之前就以 `free(): invalid pointer` 中止。现在失败路径**不调 `vm_free`**、先 `fflush(stdout)` 再返回 1 —— 一个「失败时看不到失败原因」的判据等于没有判据。

**诚实边界。** ① 18 个用例是在 Windows 上观察到的，本机（Linux）修复前后都绿，所以「修好了那 18 个」的**最终确认在发布会话的 Windows 机器上**，本节的证据是「字段差集 + 负控探针」。② `memset` 修的是「未初始化」，不是「未定义」：清单与结构体的同步仍然靠人，探针只钉住**这一版**的字段集合；结构体新增字段而清单和探针都没跟上时，探针不会自动发现（它能发现的只有「零值不对」）。③ 探针断言的是 `vm_init` 之后的字段值，不覆盖 `vm_free` 与运行期语义。④ 同一轮**没有**碰 `atomic_*` 的 Windows 位宽缺陷与两个 verse C 探针的失败 —— 它们是独立的三类问题，分开处理。

## §1.29 `atomic_set` 先写 `ival` 再写 `fval`：联合体的后一次写把值清零

**现象。** Windows 上 `atomic_int64_width_runtime`（CTest #119）报
`atomic-ok set=0 add=3000000000 get=3000000000 add2p31=2147483648 get2=2147483648 small=7 getsmall=7 ovf1=NO-THROW keep1=1 half=4611686018427387903 ovf2=NO-THROW keep2=4611686018427387904`，
期望是 `set=3000000000 … ovf1=numeric_overflow keep1=9223372036854775807 half=9223372036854775807 ovf2=numeric_overflow keep2=9223372036854775807`。
12 个数字全部由一条事实推出：**`atomic_set` 写了，但写进去的是 0** —— 注意 `add2p31=2147483648` 而不是 `5147483648`，说明它确实覆盖了旧值，只是覆盖成了 0。

**根因。** `Value`（`src/vm/vm.h:24-26`）是 `{int type; union { long long ival; double fval; }; char *sval; void *ptr;}`，`ival` 与 `fval` **是同一块存储**。`src/runtime/runtime.c` 的 `builtin_atomic_set` 原本是
`InterlockedExchange64((volatile LONG64*)&vm->globals[idx].val.ival, val); vm->globals[idx].val.fval = 0;`
—— 后一句把前一句写进去的位型清零。POSIX 的 `posix_atomic_set`（`src/runtime/runtime_posix.c:781-793`）只写 `ival`、从不碰 `fval`，所以这个缺陷只在 Windows 存在：`src/runtime/runtime.c` 不参与 Linux 构建，Linux 门禁结构上看不见它。

**修法。** 删掉那句 `fval = 0`，并把非联合体成员 `sval = NULL` 挪到写 `ival` **之前**，让 `InterlockedExchange64(…ival, val)` 成为对联合体的最后一次写。

**判据。** ① 全仓扫描「同一个基址在 6 行内既写 `.ival =` 又写 `.fval =`」共 **14 处**，只有这一处有害：另外 12 处是「先 `fval = 0` 再 `ival`」的无效写（后写的 `ival` 才是值），2 处是互斥分支（`src/mod/json_mod.c:291-293`、`src/vm/vm.c:2114-2115`）。② 机制复现 `.verify/v31/union_repro.c` 按 `src/vm/vm.h:24-26` 原样重建布局，Linux `gcc -std=gnu11 -O2` 与 `/mnt/c/msys64/mingw64/bin/gcc.exe` 结论完全一致：`after ival then fval : ival=0 (expected 3000000000) CLOBBERED` / `after ival only : ival=3000000000 OK`。③ 引擎里本来就有正确的构造函数，只是它是 `static`：`src/vm/vm.c:474-482` 的 `value_set` 按 `type` **只写一个**联合体成员。`runtime.c` 调不到它，于是自己手搓构造并写错了纪律。

**诚实边界。** ① 本机不能编译 `src/runtime/runtime.c`（`<windows.h>` / `<winhttp.h>`），证据是「改动区域语法通过 + 与 POSIX 版逐行同构 + 机制探针」，真机确认在发布会话。② Linux 侧**没有**新增 CTest：缺陷在 Linux 不可达，硬造一个静态检查（`tools/check_value_union.py`）被考虑过但没有做。③ 同轮**没有**统一 `value_set` 与各运行时的构造纪律 —— 那是一次更大的重构。

## §1.30 路径锚定：调用者给的路径锚调用者的 cwd，引擎自己产生的路径锚进程 cwd

**现象。** Windows CI 上 `cli_incremental_regression` 报 `error: cannot read script 'D:\a\Inimerse\Inimerse\build-windows-gcc\app.im'`，而进程的 cwd 是 `D:\a\Inimerse\Inimerse` —— 相对脚本名被解析到了**构建目录**而不是脚本目录。

**根因。** 引擎在启动时把 cwd 切到脚本目录（`chdir_to_script_dir`，`src/main.c`），此后所有相对路径都相对脚本目录。但 `make_abs_path` / `make_abs_path_loose` 用 `_fullpath` / `realpath` 做归一化，它们锚的是**进程当时的 cwd**；一旦引擎为了别的原因动过 cwd（或调用发生在 `chdir` 之前），两者就不一致了。修法是把**调用者给的**路径显式锚到「调用者的 cwd」（启动时记下的 `g_caller_cwd`），与进程 cwd 解耦。

**同轮抓到的自伤回归。** 第一版改动把**所有**调用点都锚到调用者 cwd，其中一个是错的：`src/compiler/compiler.h:76` 的 `cur_dir` 在顶层是 `""`（`compiler_new` 对 `Compiler` 做 `memset`），`resolve_import_path("" , rel)` 原样返回 `rel`（`src/compiler/compiler.c:3008`），于是 `comp->dep_paths[i]` 可以是**相对路径**（`:3035`）—— 它是引擎自己产生的，语义基准是脚本目录。修法是新增 `make_abs_path_cwd()`（Windows 走不锚定的 `_fullpath`，POSIX 走 `make_abs_path_loose`），只把那一个调用点换过去。

**判据。** 除该点外，`make_abs_path*` / `chdir_to_script_dir` 的全部调用点拿的都是 `script` / `input` / `argv[2]` / `argv[3]`（完整清单见 `docs/BOARD.md` 第 161 行），锚调用者 cwd 是对的。mingw `-fsyntax-only src/main.c` RC=0；Linux `ctest -R "incremental|dep|compile|selfhost"` 4/4 通过。

**诚实边界。** 打包发行版不靠这两个函数找随包资源：资源用的是**进程 cwd 下的相对字面量**（例如 `src/main.c:1102` 的 `read_file_alloc("mods/debug/main.im", NULL)`），DLL 靠加载器的 exe 目录搜索，`get_self_path()`（`src/main.c:117`）另有 5 个直接调用点。所以 `chdir` 的意图没有被这次改动破坏。

## §1.31 「建目录」有两个生产点，两处都把盘符根交给了 `mkdir`，verse 那份还用 `fopen(dir)` 判存在

**现象。** Windows 上 `verse_layer_probe`（#3）7 条失败、`verse_eventlog_probe` 连带、`verse_closed_loop`（#5）「第一次成功、第二次失败」，以及 `vverse_cross_regression`（#39）/ `vverse_cli_regression`（#74）的 `cannot create laws`。

**根因（两层）。**
① **存在性判据错了。** `src/verse/layer.c` 的 `vl_mkdir_p` 收尾是 `if (vl_mkdir(tmp) != 0) { FILE *probe = fopen(tmp, "r"); if (!probe) return -1; }`。`fopen(目录, "r")` 在 Windows 上**必然失败**（目录不能当文件打开），Linux 上却成功 —— 所以同一个路径第一次 `_mkdir` 成功返回 0，第二次 `_mkdir` 失败后 `fopen` 拿到 NULL，`vl_mkdir_p` 答 -1，`vl_layer_create` 于是返回 `VL_ERR_IO` 而不是 `VL_ERR_CONFLICT`，manifest 也没写下来，后续 `vl_layer_open` 全部失败。正确判据是 `errno == EEXIST`，两个 CRT 对「已经存在的目录」都这么答。
② **盘符根不是目录。** `vl_mkdir_p` 按 `/` 和 `\` 切分前缀，于是任何绝对 Windows 路径的第一个前缀都是 `"C:"`。`_mkdir("C:")` 在「该盘没有当前目录」时返回 `EACCES`（实测 `errno=13`），在「恰好有」时返回 `EEXIST` —— 结果取决于进程从哪里启动。**同一个缺陷在 `src/platform/platform.c` 的 `im_platform_mkdirs` 里一模一样**（`:105-109` 同样按 `'/' || '\'` 切分），而 `src/common/vverse_pack.c:777` 建 `laws/` 走的正是它。

**修法。** 两处各加一个纯字符串谓词 `vl_is_drive_root` / `im_is_drive_root`（`[A-Za-z]:` 且长度为 2），前缀扫描跳过它、收尾把它当成「已存在」直接返回 0；同时把 `vl_mkdir_p` 的存在性判据从 `fopen` 换成 `errno == EEXIST`。**两处都改**是必须的：这是同一份计算的第二个生产点，只改 verse 那份，#39/#74 仍然红。

**判据（全部用真 Windows 工具链 `/mnt/c/msys64/mingw64/bin/gcc.exe` 实测）。**
- `_mkdir("C:")` → `-1 errno=13 (Permission denied)`（直接复现盘符根那一层）。
- `.verify/v31/mkdrive.c` + `src/platform/platform.c`：绝对新路径 `C:\Temp\vinim\mk3\out\laws` → `0`、再次 → `0`、正斜杠版 → `0`、相对路径 → `0`、`"C:"` 本身 → `0`。
- `.verify/v31/layer_abs.c` + `src/verse/layer.c`（根用 `C:\Temp\vinim\vltest`）：`create1 -> 0 (VL_OK)`、`create2 -> -3 (VL_ERR_CONFLICT)`、`open -> non-NULL`、`PASS`。
- **负控**：把 `vl_is_drive_root` 的两处守卫去掉后重编（`.verify/v31/layer_nodrive.c`），同一个程序变成 `create1 -> -2`（`VL_ERR_IO`）、`create2 -> -2`、`open -> NULL`、`FAIL`、exit 1 —— 也就是说**没有这个守卫时连第一次创建都会失败**，与「提交版恰好绕过它（它只按 `'/'` 切分）」相互印证。
- `src/platform/platform_probe.c` 扩了 8 项 `im_platform_mkdirs` 契约断言（已存在即成功、深层新路径、尾部分隔符、`NULL`/空串拒绝、超长路径拒绝而非截断），`platform_probe` 通过；`verse_layer_probe`、`verse_eventlog_probe` 在 Linux 与 Windows 上都是 `all checks passed`。

**诚实边界。** ① 盘符根那一层在 Linux 上不可达（没有盘符），Linux 侧只能钉住「已存在即成功」这条契约；盘符根由发布会话的 5 个 Windows 用例（#2/#3/#5/#39/#74）端到端确认。② `vl_mkdir_p` 与 `im_platform_mkdirs` 的规则**是重复的**，这是有意的：verse 核心刻意不依赖 `src/platform`，所以规则在两处各写一遍，两边都加了同样的注释指向对方。

## §1.32 hub 端口：「实际绑定的端口」有两个生产点，只有一个会报

**现象。** Windows 上 7 个 hub 用例（#36 `crp_session_flow`、#37 `hub_dist`、#40 `node_discovery`、#41 `lease_handoff`、#43 `economy_domain`、#44 `economy_migration`，外加 #35 `http_large_package` 连带）全部倒在 `start_hub_bound_ports` / `start_hub`；#35 拿到 `status=200 len=436 body='<html>…<b>Wrong port!</b>'`。

**根因。** 引擎用 `--port 0 --http-port 0` 让内核分配端口，然后把**实际**端口打在 stderr 上，harness 读这一行（`tools/testports.py:302`：端口非正就当成「服务器报不出自己绑了什么，视为没起来」）。但 `src/headless_server.c` 的 `headless_init()` 只做 `g_hl_port = port;`（`:26`）而后再也不更新，`src/mod/verse_dist_mod.c` 的 `verse_http_start()` 也只做 `g_listen_port = port;`（`:1875`）—— 两处都报的是**被请求**的端口。Linux 之所以绿，是因为 POSIX 的两个孪生实现（`src/headless_server_posix.c:51-53`、`src/platform/http_posix.c:2129`）本来就有 `getsockname` 派生的 getter。**HTTP 服务器有两个生产点**（`src/platform/http_posix.c:2155` 与 `src/mod/verse_dist_mod.c:1862`，后者在 `:1315 #ifdef _WIN32` … `:2007 #else` … `:2063 #endif` 里），只有 POSIX 那个报实际端口。

**修法。** winsock 两侧各加 `getsockname()`：`headless_init` 在 `listen()` 之后把 `g_hl_port` 换成实际端口，并在 `headless_enabled()` 旁加 `int headless_bound_port(void)`（写进 `src/headless_server.h`）；`verse_http_start` 同样在 `listen()` 之后记录实际端口，并加 `int verse_http_bound_port(void)`。`src/main.c` 里两组 `#if defined(_WIN32)` 分支随之删除，两个平台统一走 getter。

**判据。** Linux 上 9 个 hub 相关用例（`crp_session`、`hub_dist`、`node_discovery`、`lease_handoff`、`economy`×2、`http_large`、`verse_closed` 等）`100% tests passed, 0 tests failed out of 9`；mingw `-fsyntax-only` 对 `src/main.c`、`src/mod/verse_dist_mod.c`、`src/headless_server.c`、`src/headless_server.h` 全部 PASS。

**诚实边界。** Windows 侧的真机确认（`headless: 127.0.0.1:<非零>`）在发布会话。同轮顺带记录了 `docs/STATUS.md` §2.9「端口窗口已关闭」这条结论原本只在 Linux 上成立。

## §1.33 eventlog 的回滚在 Windows 上被 `#ifndef` 编译掉了

**现象。** `verse_eventlog_probe`（#2）在 `src/verse/eventlog_probe.c:146` 报 `vl_eventlog_verify(log) == VL_OK` 失败。

**根因。** 故障注入的第 6 步（落盘失败）本应把已经写下的记录回滚掉：`src/verse/eventlog.c:498-505` 原本是 `if (!flush_ok) { #ifndef _WIN32 if (ftruncate(vl_fileno(log->fp), off) != 0) { /* best effort */ } #endif fseek(…); … return VL_ERR_DURABILITY; }` —— **截断在 Windows 上被编译掉了**，于是第 5 步已经写进文件的记录留在盘上，而内存里的 head/count/end_off 已经回滚，下一次 `verify()` 的 `vl_recompute` 对不上，答 `VL_ERR_RECOVERY_REQUIRED`。

**修法。** 在 `src/verse/eventlog.c:10-18` 的宏块里加 `vl_truncate`（Windows `_chsize`，POSIX `ftruncate`），把守卫块换成无条件调用。`<io.h>` 在 Windows 上本来就已包含。

**判据。** Windows `eventlog_probe.exe` → `verse_eventlog_probe: all checks passed`；Linux `./build/verse_eventlog_probe` → 同样。

**诚实边界。** 「回滚要真的落盘」这条在 Linux 上一直成立，所以这个缺陷同样是 Windows-only；证据是同一个探针在两个工具链上的同一句输出。

## §1.34 谓词在 Windows 上答的是数字，不是布尔

**现象。** 同一句 `str(startswith("abc", "a"))`，Linux 答 `true`，Windows 答 `1`。

**根因。** 同一个计算有两个生产点，两边选的值种类不同：

- Windows `src/runtime/runtime.c:465-474`（`builtin_str_startswith`）与 `src/runtime/runtime.c:475-485`（`builtin_str_endswith`）用 `push_int(vm, ok)`；
- POSIX `src/runtime/runtime_posix.c:494-500`（`posix_core_startswith`）与 `src/runtime/runtime_posix.c:502-509`（`posix_core_endswith`）用 `push_bool(vm, ok)`。

**Windows 那一份与它自己也不一致**：同一个文件里的 `builtin_has`（`src/runtime/runtime.c:403`）与 `builtin_remove`（`src/runtime/runtime.c:433`）用的都是 `push_bool`。「答是/否」这一类计算只有一种正确的结果种类，所以异类是 Windows 这一份。

**差分扫描。** 把两个运行时里 59 个同名内建的 `push_bool`/`push_int`/`push_nil`/`push_float`/`push_string` 词汇逐一对比，全部差异只有 5 处、3 类：

| 内建 | Windows | POSIX | 处置 |
|---|---|---|---|
| `startswith` `endswith` | `push_int` | `push_bool` | 改 Windows |
| `spi_mods` | 无 `aidx < 0` 守卫，收尾仍发布 `ival = aidx + 1 = 0` | `push_nil` | 改 Windows |
| `mod_usage` | 无守卫，两次 `vm_dict_set` 静默无效后仍发布 `ival = 0` | `push_nil` | 改 Windows |
| `round`（非数字实参） | `vm_throw_msg` 抛错 | 返回 `nil` | **保留分歧**，见诚实边界 ② |

`spi_mods`/`mod_usage` 只走分配失败路径：`vm_dict_set` 在 `src/vm/vm.c:1028` 有 `aidx < 0 || aidx >= vm->arrayCount` 守卫，所以不会越界写；危害是**把一个指向 −1 号槽的引用包装成 dict/array 发布出去**，违反既有的「宁可拒绝也不给出错误答案」。

**修法。** `src/runtime/runtime.c` 三处向 POSIX 看齐：`push_bool` ×2，两处补 `if (aidx < 0) { push_nil(vm); return 1; }`。

**判据。** 新用例 `vtest/predicate_result_type_v06.im` 打印 `predbool true true false 14`（把 `startswith`/`endswith`/`has` 的结果与 `len` 并排放在一行），注册为 CTest **#122** `predicate_result_type_runtime`，PASS 正则是整行、FAIL 正则 `predbool 1|predbool true 1|predbool true true 1`。`tools/gate.sh` 的 `EXP_CTEST` 那一行 121 → **122**。用例注册在测试列表末尾，既有 `#N` 不动。

**诚实边界。** ① `src/runtime/runtime.c` 在 Linux 上根本不参与编译（`CMakeLists.txt:426` 的 `if(WIN32)` 分支（引擎源清单那一段）），所以 Linux 门禁只能钉住 POSIX 那一半；Windows 那一半靠 ucrt64 与 mingw64 两套 gcc 16.1.0 的 `-fsyntax-only`（均 RC=0、0 error）加上协调者在 Windows 上的实跑。② `round` 的非数字实参分歧**没有动**：Windows 抛错、POSIX 答 `nil`，两者都是既有的拒绝形态（硬拒绝 vs 软拒绝），属于设计选择而非明显缺陷，改 POSIX 会改变 Linux 行为，需要单独一轮裁决，因此只在此记录。③ 本次没有为「值种类」加运行期断言，判据是 `str()` 的输出。

## §1.35 依赖 trailer 在 Windows 上记不下相对路径

**现象。** 同一个最小工程（`lib.im` 加一个 `import "lib.im"` 的 `app.im`），Windows 上 `buildc app.im app.inim` 之后 trailer 里记的是 `../D:\inim-rel\nt2\app.im` 与 `../D:\inim-rel\nt2\lib.im`；第二次 `--incremental` 仍然打印 `compiled:` 而不是 `up to date`。

**根因。** 同一个「把绝对路径变成相对路径」的计算有两个生产点，只有一处认识 Windows 的分隔符：`src/compilation/deps.c:83-93` 的 `deps_bc_dirname` **已经**在 `_WIN32` 下额外找 `\`（`:86-89`），而 `src/compilation/deps.c:97-121` 的 `deps_relative_path` 只按 `/` 切 —— `:104-105` 的尾部剥离与 `:107-108` 的两处 `strtok(..., "/")`。

`src/main.c:390-396` 的 `normalize_path` 在 Windows 上把 `/` 全换成 `\`，`make_abs_path_loose`（`src/main.c:442-448`）会调它，所以 `src/main.c:774` 与 `:781` 传进来的 `abs_main`/`abs` 是反斜杠绝对路径。反斜杠路径里没有 `/`，于是 `ac`/`bc` 各自只剩一个分量，`common` 恒为 0，函数吐出 `".."` 加完整绝对路径。读回时 `deps_entry_abs_path`（`src/compilation/deps.c:123-133`）看到首字符是 `.`，既不认 `/` 也不认盘符，拼成 `dir + "/" + "../D:\...\app.im"` → 文件不存在 → sha256 失败 → 判定 stale → **每次增量都重编译**。

**修法。** `src/compilation/deps.c` 加一个平台相关的分隔符集合：`_WIN32` 下 `#define DEPS_SEPS "\\/"` 与 `deps_is_sep(c)` 认 `/` 和 `\`，POSIX 下退化成只看 `/`；`deps_relative_path` 的尾部剥离与两处 `strtok` 都改用它。这与 `deps_bc_dirname` 的既有写法同源。

**判据。** 用 ucrt64 gcc 16.1.0 编译 `src/compilation/deps.c` 加一个直接调 `deps_relative_path` 的探针，对 HEAD 那份与工作区那份各跑一遍：

| from_dir | abs_target | 修复前 | 修复后 |
|---|---|---|---|
| `C:\Users\x\nt2` | `C:\Users\x\nt2\app.im` | `../C:\Users\x\nt2\app.im` | `app.im` |
| `C:\Users\x\nt2` | `C:\Users\x\nt2\lib.im` | `../C:\Users\x\nt2\lib.im` | `lib.im` |
| `C:\Users\x\nt2\sub` | `C:\Users\x\nt2\app.im` | `../C:\Users\x\nt2\app.im` | `../app.im` |
| `C:\Users\x\nt2\` | `C:\Users\x\nt2\app.im` | `../C:\Users\x\nt2\app.im` | `app.im` |
| `D:\proj` | `C:\other\lib.im` | `../C:\other\lib.im` | `../../C:/other/lib.im` |
| `/tmp/tmp.X` | `/tmp/tmp.X/app.im` | `app.im` | `app.im` |

修复前那一列与协调者在 Windows 上观察到的 trailer 逐字节一致；最后一行证明 POSIX 行为未变。

**诚实边界。** ① 这是 Windows-only 缺陷（Linux 上 `DEPS_SEPS` 就是 `/`），Linux 门禁只能证明「没改坏」：`ctest -R "incremental|dep|compile|selfhost|cli_"` 5/5 通过。② `deps_entry_abs_path` 的绝对路径判定仍然只认前导 `/` 与盘符，不认前导 `\`；修复后 `deps_relative_path` 只会吐出 `/` 连接的相对路径或 `..` 前缀，所以这条分支不会被反斜杠路径命中 —— 但这是一个**没有被测试覆盖**的假设。③ trailer 内部仍然用 `/` 连接（`src/compilation/deps.c:113` 与 `:117`），这是刻意的：trailer 的内容要跨主机可比。

## §1.36 模组加载通知写进了程序输出

**现象。** 在 Windows 上（只有 Windows 引擎链接 `mods/build/build_mod.c`，见 `CMakeLists.txt:434`）解释器比 POSIX 多输出一行 `[build模组] 已加载`，`tools/wasm_backend.test.py` 的逐字节比较因此失败。

**根因。** 同一件事（模组加载完成）有三个生产点，只有一处写 stdout：`mods/build/build_mod.c:742` 的 `printf`，对 `src/mod/infiverse_mod.c:839` 与 `src/mod/verse_dist_mod.c:2688` 两处都写 `fprintf(stderr, ...)`。

**修法。** `mods/build/build_mod.c:742` 改 `fprintf(stderr, ...)`。同文件里其它的 `printf`（`mods/build/build_mod.c:371` 的 `打包完成`、`:373` 的 `已嵌入模组`、`:405` 的参数错误等）是 `build` 这个内建**本身**的输出，属于「命令的答案」，保持 stdout 不动。

**判据。** ucrt64 与 mingw64 下 `-fsyntax-only` 均 RC=0、0 error。

**诚实边界。** 这一行在 Linux 上不参与编译，所以 Linux 门禁看不见它；「通知走 stderr」这条规矩本身没有被测试钉住，只有三处源码的一致性。

## §1.37 inim-client 在 Windows 上是一份桩，于是那个测试注定红

**现象。** Windows ctest `#5 verse_closed_loop` 的第一条 FAIL 是
`recover: the client reports the crash: 4`（`tools/verse_closed_loop.test.py:221`
期望 `proc.returncode == 137`），紧接着 `committed records survived the crash:
{'cells': [], 'head': 'e3b0c442...', 'seq': 0}`（`:228`）。

**根因不在恢复，也不在落盘。** `src/verse/client.c` 在 Windows 上整份就是桩：

```c
#if defined(_WIN32)
int main(void) {
    fprintf(stderr, "inim-client: process spawning is not implemented on this platform\n");
    return 4;
}
#else
```

客户端根本没跑起来，所以一个 `put` 都没发出去，`seq=0` / `cells=[]` /
`head=e3b0c442...`（`e3b0c442` 是**空串**的 sha256）全是同一原因的后效。
`src/verse/client.c:1-14` 的注释自己就写了「Spawning a child needs fork+exec,
so this program is POSIX-only for now; on other platforms it says so instead of
pretending to work」——**它在诚实地拒绝**，问题只在于 `CMakeLists.txt:62`/`:75`
在 Windows 上也注册了这个测试，于是它注定红。

**修法：补齐能力，不是跳过测试。** 把「spawn 一个带两条管道的子进程」抽成四个
操作，一个平台一份：

- `child_spawn(Child *, server, root, verse_id, FILE **to_child, FILE **from_child)`
- `child_kill(Child *)` / `child_wait(Child *, int *exit_code)` / `child_close(Child *)`

参数解析、`slurp`、请求循环、`#crash` 指令、退出码传递**全部共享**，两个平台不可能
在「发什么、印什么」上再次漂移。POSIX 那半是原来的 `pipe` / `fork` / `dup2` /
`execlp`，逐行未改；Windows 那半是 `CreatePipe`（`SECURITY_ATTRIBUTES.bInheritHandle
= TRUE`，而父进程自己那两端要 `SetHandleInformation(..., HANDLE_FLAG_INHERIT, 0)`，
否则子进程握着自己的 stdin 永远看不到 EOF）+ `CreateProcessA(NULL, cmd, ...)` +
`_open_osfhandle` / `_fdopen`（两端都 `_O_BINARY`，让请求字节与 POSIX 逐字节相同）
+ `TerminateProcess`。`src/platform/im_process.h` 那层只有
`im_process_spawn(command, new_console)`，没有管道重定向（`im_process_capture` 是
POSIX PAL），所以这次不动平台层，客户端内部自足。

**实测抓到的两个坑。**

1. **命令行每段之间必须有空格。** 第一版 `cmdline_append` 只写了引号，于是生成
   `"a""b""c"`，`CreateProcess` 把它解析成**一个**参数 `abc`，子进程 `argc` 只剩 1、
   `argv[1]`/`argv[2]` 全空——而**管道是通的、退出码是对的、`#crash` 也正常**，只有
   参数悄悄丢了。这正是「快速通道的门被当成了答案」那一类：管道通了不等于命令行对了。
   判据是让假 server 把收到的 `argv` 印出来，而不是只看客户端自己印了什么。
2. **反斜杠要转义。** `CommandLineToArgvA` 的规则是「引号前的连续反斜杠加倍」，
   否则以 `\` 结尾的路径会吞掉收尾引号。`cmdline_append` 按这条规则写。

**判据（真 Windows，ucrt64 gcc 16.1.0，配一个只回 JSON 行的假 server）：**

| 用例 | 期望 | 实测 |
|---|---|---|
| `#crash` | 两条应答 + `#crash: server killed` + 退出码 **137** | 一致 |
| 子进程 EOF 退出 3 | 客户端原样传出 **3** | 一致 |
| root 以 `\` 结尾 | 子进程收到 `C:\Temp\vinim\root\` **一字不差** | 一致 |
| server 路径带空格 | 能起来并应答 | 一致 |

Linux 侧 `ctest -R verse_closed_loop` 仍 `Passed`，即 POSIX 那半行为未变。

**诚实边界。** ① Linux 门禁只能钉住 POSIX 那半；Windows 那半的证据是 mingw 两套
工具链（ucrt64 与 mingw64）的 `-fsyntax-only` 均 RC=0 且 `-Wall -Wextra` **零警告**，
加假 server 实跑，真 server 的实跑由协调者在 Windows 上做。② 假 server 验的是
**客户端自己的管道与命令行**，不是协议语义。③ Windows 上没有信号，「正常退出」与
「被杀」只能靠退出码区分，所以 POSIX 那半「非 `WIFEXITED` 就不给退出码」的语义在
Windows 上没有对应物，代码里记了这一点。

## §1.38 包名只按 `/` 取，于是 Windows 上整条绝对路径成了包名

**现象。** Windows `#38 verse_pack_regression` 报 `AssertionError: open=0`
（`tools/verse_pack.test.py:189`），stderr 里有 `[VDP] cannot create laws`。

**根因。** `src/mod/verse_dist_mod.c:1082` 只有一句

```c
const char *slash = strrchr(tail, '/');
```

而 `tail` 在 Windows 上是 `C:\Users\...\mypkg.vverse`——**全是反斜杠**，`strrchr`
返回 NULL，于是整条绝对路径被当成包名；`:1087` 剥掉 `.vverse` 之后
`dest = "<home>/universe/C:\Users\...\mypkg"`，逐段 `mkdir` 走到分量 **`C:`**，
冒号在路径分量里非法 → `_mkdir` 失败 → `cannot create laws`（正是 `dirs[0]`）。

**同一个文件里同一个计算已经有三个生产点，另外两个都记得反斜杠：**
`src/mod/verse_dist_mod.c:271-273`（`home_dir`）与 `:387-389` 都写了
`char *bs = strrchr(p, '\\'); if (bs && (!slash || bs > slash)) slash = bs;`。
第三个（`:1082`）忘了。这与 `src/compilation/deps.c` 是同一类：一件事多个生产点，
只有一处记得平台分隔符。

**修法。** 用文件里已有的那句写法，取**靠后**的那个分隔符（不是「平台相关的那个」：
Windows 上 `/` 与 `\` 都合法，只看一个会在混用路径上再次出错）。

**诚实边界。** ① 这一处只在 Windows 上可观测（Linux 的包路径用 `/`），Linux 门禁
钉不住它；判据是协调者在 UCRT64 上复跑 `#38`，预期 `name = mypkg`、
`dest = <home>/universe/mypkg`、`open=1`。② 这次没有新增用例：它和 §1.35 一样属于
「Linux 上不参与编译的 Windows-only 分支」。

## §1.39 DWARF 的行号程序记下了整条绝对路径

**现象。** Windows 上生成的 `<out>.debug_line` 里，源文件名是整个绝对路径
`C:\dir\app.im`，而不是 `app.im`。

**根因。** `src/compilation/debug_info.c` 取 basename 时只看正斜杠：

```c
const char *base = strrchr(source_path, '/');
base = base ? base + 1 : source_path;
```

Windows 的源路径是反斜杠形式，`strrchr` 返回 NULL，于是整条路径被写进 DWARF 的
line program。与 §1.35（`src/compilation/deps.c`）、§1.38
（`src/mod/verse_dist_mod.c`）是同一类：**一件事多个生产点，只有一处记得平台分隔符。**

**修法。** 取靠后的那个分隔符（`src/compilation/debug_info.c:155-157`）：
`{ const char *bs = strrchr(source_path, '\\'); if (bs && (!base || bs > base)) base = bs; }`。
这个文件在 [../CMakeLists.txt](../CMakeLists.txt) 的**公共源列表**（`:421`）里，两个平台
都编，所以它 Linux 上也编译，只是 Linux 的输入全是 `/`，测不出差别。

**诚实边界。** ① 这一条**没有新增用例**：Linux 上 `source_path` 总用 `/`，除非刻意传一个
含反斜杠的文件名，否则两种写法输出相同，所以 Linux 门禁只能证明「没改坏」。判据是
协调者在 Windows 上生成一个 `<out>.debug_line` 并检查其中记的是 basename。② 影响面只有
调试信息，不影响执行语义——它是本轮里唯一一条「纯粹是元数据写错」的缺陷。

**一个查过并关掉的候选（记下来，免得下次再查）。** `src/compiler/bytecode.c:641`
`bytecode_release_mods()` 在 `:676` 硬编码 `snprintf(out_path, ..., "%s\\%s", destDir, rel)`
并把 `/` 全换成 `\\`，而且该文件里 `:676` 之外没有任何平台守卫（最近的条件指令是
`:467 #ifdef _WIN32` / `:511 #else` / `:514 #endif`），看起来像「在 Linux 上把反斜杠当
分隔符写文件名」。**它不是缺陷**：唯一的调用点是 `src/main.c:547`，而那一整段在
`src/main.c:524` `static void load_embedded_mods_impl(VM *vm)` 的
`#ifndef _WIN32`（`:525`，POSIX 直接 `(void)vm; return;`）`#else`（`:527`）分支里，
`#endif` 在 `:552`——**这个函数在 POSIX 上是空的**，所以 `bytecode_release_mods` 只在
Windows 上被调用，硬编码 `\\` 正是它唯一的调用者要的。查它是为了确认「公共源文件里的
反斜杠」不是盲区，结论是这条路径的守卫在**调用方**而不是被调用方。

## §1.40 一个连接超时兜不住名字解析

**现象。** `tools/selfhost_compare.py` 的 `#27 selfhost_codegen_parity` 在 Linux 上是 13.03 s，在 Windows 上是 `***Timeout 300.05 sec`（协调者独立复测 270–300 s）。读数像「Windows 慢 21 倍」，实际不是慢：`selfhost/tests/hw_test.im` 里的 `http_get("http://example.com")` 走真实网络，而这条路径承诺的上界够不到名字解析这一步。把网络拿掉（`unshare -rn`）后同一个调用是 0.12 / 0.18 / 0.14 s。同一个调用的墙钟（各三次）：

| 环境 | 三次实测 | 最坏 |
|---|---|---|
| `unshare -rn`（断网） | 0.12 / 0.18 / 0.14 s | 0.18 s |
| 联网 | 0.71 / 2.79 / 1.62 s | 挂过 ≥300 s 一次 |

`ctest --timeout 900` 对这种形状无效 —— 这是**停顿**，不是**慢**，900 s 只是把红灯推后；真正兜住它的是 300 s 的 per-test TIMEOUT。

**根因。** HEAD 版 `src/platform/socket.c:97` 的 `ImSocket *im_socket_connect_timeout(const char *host, uint16_t port, int timeout_ms)` 在 `:99` 先解析、`:100` 才 clamp 超时：

```c
if (resolve_addr(host, port, &addr, &addr_len, 0) != 0) return NULL;
if (timeout_ms <= 0) timeout_ms = 2000;
```

`timeout_ms` 只在解析**之后**的 connect 等待里被用到（`:119` 的 deadline、`:126`–`:127` 的 `select()`）。而解析这一步拿到的是 `resolve_addr`（`src/platform/socket.c:66`）：

```c
static int resolve_addr(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *out_len, int passive)
```

第五个参数是 `passive`（`hints.ai_flags = passive ? AI_PASSIVE : 0`），**不是超时**；`:70` 的 `getaddrinfo()` 没有任何超时，标准里也没有。于是「连接超时」这个承诺在整条路径**最慢的一步**上是空的：解析器被黑洞时，等的是系统自己的重试表（分钟级），而不是调用方给的毫秒。

**修法。** 解析改走仓库已有的可移植线程面（`src/platform/thread.c` 的 `im_thread_start` / `im_thread_join` / `im_thread_detach`）：`resolve_addr_timed`（`src/platform/socket.c:117`）把解析放进 helper 线程，`im_thread_join(thread, timeout_ms)` 非 0 就 `im_thread_detach` 并返回 NULL —— 超时即拒绝。

- 不用 `getaddrinfo_a()` / `gai`：glibc 独有，Windows 与 musl 都没有，会把可移植层变成 Linux-only。
- 不用 `res_options` / `RES_OPTIONS`：全局（影响同进程所有解析）、不可移植，而且它给的是「系统重试计划的上界」，不是逐调用的上界。
- 选线程是因为 `thread.c` 本来就是这个仓库的线程面，Windows 与 POSIX 两侧都已实现。

**证据（A/B，`LD_PRELOAD` 把 `getaddrinfo` 换成一个会停顿的版本）。**

| 版本 | 输出 |
|---|---|
| HEAD 的 `socket.c` | `host=example.com timeout_ms=5000 connected=yes elapsed=34270 ms` |
| 修复版 | `host=example.com timeout_ms=5000 connected=no elapsed=5000 ms` |

无停顿回归（修复版，不注入）：`example.com` 265 ms、`127.0.0.1` 3 ms、`nonexistent.invalid` 76 ms —— 有界路径没有把普通调用弄慢。

**钉子。** 新增两个 POSIX-only 文件：

- `src/platform/slowdns_preload.c`：`#define _GNU_SOURCE` + `dlsym(RTLD_NEXT, "getaddrinfo")` 拦截 `getaddrinfo`，`sleep(atoi(getenv("SLOWDNS_SECS")))`，停顿值**在调用时读取**，所以同一个探针进程能用两次不同停顿做两个 Phase。
- `src/platform/resolve_timeout_probe.c`：Phase A 注入 10 s 停顿、超时 2000 ms，断言 `elapsed > 3500` 为「上界没兜住」；Phase B 用数字主机 `127.0.0.1`，不依赖网络。

**「钉子是不是空的」不能用秒表判。** 第一版把非真空判定写成时间窗（`elapsed < 1500` 即「停顿没注入」）。独立复核实测把它**证伪**了：`env -u LD_PRELOAD` 连跑 11 次，第 11 次得到 `elapsed_ms=2871 connected=0` 与 `resolve_bound: ok`、rc=0 —— **没有注入却全绿**。根因是新代码下「无注入」的一次调用本来就要花掉真实解析加上一整个 connect 预算（约 2025 ms），真实 connect 稍慢就落进窗口，所以丢掉 preload 只有约 **10/11** 的概率被抓。改法：让垫片自己留可检验的痕迹 —— 垫片拦截时向 `SLOWDNS_LOG` 指向的文件追加 `getaddrinfo node=… stall=…`，探针跑完 Phase A 读该文件，空则报 `FAIL getaddrinfo was not interposed; the pin is vacuous`。文件内容不会「慢」，所以不会像秒表那样误判；改后同样的负控 **12/12** 都红。

CTest **#123** `resolve_timeout_runtime`（`CMakeLists.txt:940`），`tools/gate.sh` 的 `EXP_CTEST` 那一行从 122 改成 **123**。

**反向对照。** 同一个探针分别链到 HEAD 的 `socket.c` 与修复版：HEAD 侧是 `resolve_bound stall: elapsed_ms=10224 connected=1`、`FAIL the bound did not hold`、rc=1；修复版 rc=0。也就是说这个钉子对修复前的代码**是红的**，不是一条永远绿的断言。

**诚实边界。** ① 钉子是 POSIX-only：它靠动态加载器注入，Windows 不跑它，所以 Windows 侧只有代码路径，没有回归用例。② 修的是「解析不再无限等」，不是「解析一定成功」：超时后返回 NULL，调用方只看到连接失败，与 DNS 真失败不可区分。③ 被中断的解析线程仍在后台跑：`src/platform/thread.c` 的 `im_thread_detach` 在 POSIX 上是 `pthread_detach(*thread)` 加 `free(thread)`（句柄释放、线程退出时由 libc 回收），Windows 上是 `CloseHandle`；`ResolveJob` 则**故意泄漏**（`src/platform/socket.c:131` 写着 `job intentionally leaked; the resolver is still using it`），因为 `getaddrinfo()` 不能被取消，释放它就是对仍在写的线程做 use-after-free。④ Phase B 用数字主机，所以它验证的是「有界路径没把普通调用弄慢」，不是「解析在无停顿下正确」。⑤ 非 Linux 的 POSIX 上 `im_thread_join` 没有 timed join（`src/platform/thread.c:80-85` 退化成 `pthread_join`），那个平台上界是否成立没被证明 —— 它退化成原来那份无超时实现，「不比原来更糟」。⑥ 只修了 connect 路径：`im_socket_listen` 仍走无超时的 `resolve_addr`。⑦ `#27` 自己仍然跑真实网络请求（`selfhost/tests/hw_test.im` 未改），这次买到的是「挂住的那一步有界」，不是「测试不再依赖网络」。⑧ **上界是分阶段的，不是一次性的总预算**：`src/platform/socket.c:166` 的解析最多等 `timeout_ms`，`:185` 又给 TCP connect 一个**全新的** `timeout_ms`，所以最坏总耗时接近 `2×timeout_ms`；钉子只覆盖「解析超时」那一支，别把它读成硬性总上界。⑨ 这次改动一开始在 Windows 上引入了一条新警告：`#include "thread.h"`（`thread.h:5` 拉进 `<windows.h>`）排在 `<winsock2.h>` 之前，触发 `winsock2.h:15: #warning Please include winsock2.h before windows.h [-Wcpp]`（HEAD 版零警告，对 HEAD 版加 `-include src/platform/thread.h` 可复现同一条）。已把该 include 移到平台头之后，ucrt64 与 mingw64 两个工具链复测 `-Wall -Wextra -fsyntax-only` 均 rc=0 且零警告。全仓库没有 `-Werror`，`tools/gate.sh` 也没有 mingw 交叉编译步骤，所以它从未让门禁变红 —— 这类警告只有真去编 Windows 才看得见。

## §1.41 CMake 里 `ENVIRONMENT` 是共享属性，最后写者胜

**现象。** 新测试 `resolve_timeout_runtime` 需要 `LD_PRELOAD`，但第一次注册之后 `build/CTestTestfile.cmake` 里记录到的属性是 `ENVIRONMENT "PYTHONIOENCODING=utf-8"` —— `LD_PRELOAD` 干净地消失了，钉子在无人察觉的情况下变成一条「没注入停顿」的测试（Phase A 的 `elapsed < 1500` 会报 `the pin is vacuous`）。

**根因。** `CMakeLists.txt:921` 的全局收尾：

```cmake
get_property(INIMERSE_ALL_TESTS DIRECTORY PROPERTY TESTS)
if(INIMERSE_ALL_TESTS)
  set_tests_properties(${INIMERSE_ALL_TESTS} PROPERTIES ENVIRONMENT "PYTHONIOENCODING=utf-8")
endif()
```

`ENVIRONMENT` 在 CMake 里是**单一属性，不是列表累积**：对同一个测试 `set_tests_properties` 两次，第二次**替换**第一次（要累积必须自己把旧值读出来再拼）。这个循环对**每一个**已注册测试写一遍，所以它上面的测试属性赋值会被它整体覆盖。

**这个症状第一次是怎么出现的。** 最初整块新测试放在 `if(INIMERSE_BUILD_ENGINE)` 内、也就是那个循环**之前**（那个 `endif()` 现在在 `CMakeLists.txt:982` —— 它闭合的是 `:411` 的 `if(INIMERSE_BUILD_ENGINE)`；覆盖 `ENVIRONMENT` 的那一句在 `:990`，它自己的 `endif()` 在 `:991`。**原文这里写的 `:915` 是陈旧号**），于是循环把 `LD_PRELOAD` 覆盖掉，生成文件里只看得见 `PYTHONIOENCODING=utf-8`。

**修法。** 整块移到那个循环**之后**（当时是 `CMakeLists.txt` 的最后一段：`slowdns_preload` 那一段，守卫 `if(NOT WIN32 AND INIMERSE_BUILD_ENGINE)` 在 `:1006`、`ENVIRONMENT` 在 `:1014`。**原文这里写的 `:935` 是陈旧号，而且「最后一段」本身也已不成立** —— 现在 `:1380` 还有一段 `getaddrinfo_log_preload`），记录到的属性变为 `ENVIRONMENT "LD_PRELOAD=/home/sakiko/inimerse/build/libslowdns_preload.so"` —— 生成器表达式 `$<TARGET_FILE:slowdns_preload>` 在 `ENVIRONMENT` 里确实会展开。

**教训（一句话）。** 本仓库里测试的 `ENVIRONMENT` 是共享的、最后写者胜的属性；需要它的测试必须注册在那个全局循环**之后**，否则它的环境变量会被静默清空，而 CTest 不会给任何提示。

**另记。** `slowdns_preload` 没有设 `PREFIX ""`，所以产物是 `libslowdns_preload.so`（保留 `lib` 前缀）；手写 `LD_PRELOAD` 或手动复现时要用这个名字。

## §1.42 一个探针只打印 pid，所以四个失败码等于一个

**现象。** Windows 上 `#19 process_probe` 间歇性红，而日志里能用来定位的只有一行进程号；CI 历史失败表里从来没有出现过它，所以也没有历史输出可查。问题不是它红，是**它红了说不出为什么** —— 四个不同的步骤失败，日志完全一样。

**根因。** HEAD 版 `src/platform/process_probe.c:12` 只在开头打印一次 pid，之后以 2/3/4/5/6/7/8/9 退出：

```c
ImProcess *p = im_process_spawn(cmd, 0);
if (!p) return 2;
printf("process_pid=%llu\n", (unsigned long long)im_process_pid(p));
if (im_process_wait_kill(p, 3000) != 0) return 3;
...
if (im_process_wait_kill(q, 20) != 1) return 6;
...
if (im_process_wait(b, 3000) != 0 || im_process_exit_code(b) != 7) return 9;
```

四个步骤（spawn、kill、短等待、退出码）的日志**逐字节相同**，退出码只有 CI 的 harness 看得到，`ctest` 的失败输出里也不带它。另有一处竞态：HEAD 的 `src/platform/process_probe.c:22` 只给 `im_process_wait_kill(q, 20)` **20 ms**，而 Windows 上那个慢命令（`cmd /c ping 127.0.0.1 -n 4 >nul`）光是 `cmd.exe` 启动就可能超过它 —— 「慢子进程还活着」是**假设**，不是检查。

**修法。** 每个失败点自己报出步骤并打印实测值，一律返回 1：

- `process_probe: FAIL spawn(%s)`
- `process_probe: FAIL wait_kill(%s) rc=%d want 0`
- `process_probe: FAIL still alive after wait_kill(%s)`
- `process_probe: FAIL %s had already finished; cannot time a wait against it`
- `process_probe: FAIL wait_kill(%s, 500) rc=%d want 1`
- `process_probe: FAIL exit_code after kill=%d want >= 0`
- `process_probe: FAIL wait(%s) rc=%d exit_code=%d want rc=0 exit_code=7`

短等待从 **20 ms** 改成 **500 ms**（`src/platform/process_probe.c:39`），并把「慢子进程还活着」从假设改成显式前置检查 `im_process_alive(q)`（`:35`）：慢命令本身跑数秒，500 ms 落在这个窗口里很宽；真的起不来时报告的是「它已经结束了」，而不是一个假的超时。

**判据。** 真实 Windows 上（`/mnt/c/msys64/ucrt64/bin/gcc.exe`）新探针 **12/12 通过**。

**诚实边界。** ① 旧探针**在本机 20/20 也通过**：这个 flake 没有被复现，所以**不能**声称 20 ms 是根因 —— 只能说探针现在可诊断，并且不再含那个 20 ms 竞态。② flake 是否消失要等 CI 长跑；本次交付证明的是「下次红了，日志能说出是哪一步、看到的实测值是多少」。③ Linux 侧行为没变，改的只是 Windows 会走到的那条短等待与日志。

## §1.43 一个逃逸守卫读了没写过的字节，而唯一断言它的探针没有注册

`src/platform/vfs.c:22` 的 `..` 守卫是 `if (w == 0 || !strchr(out, '/')) return -1;`，
但此刻 `out[w]` 还没有写终止符 —— 终止符在 `:29` 才写。`strchr` 于是越过已写入的 `w`
个字节，去读调用方缓冲区里**从未初始化过**的内容，命中与否取决于栈上恰好有什么。
注释写着「A VFS path may never escape its mount prefix」，而这条约束实际被放行：
`im_vfs_normalize("os:/../escape")` 从源码根连跑 40 次，**40/40 返回 0**（成功）。

**为什么一直没人发现。** `vfs_probe` 在 `CMakeLists.txt:34` 就被构建，却从来没有
`add_test` 注册，`tools/gate.sh` 里也没有任何 `vfs` 引用 —— 一个已经断言了正确行为、
并且正在失败的探针，门禁从来没有跑过它。这和第 1.24 节、第 1.40 节是同一类：
**断言存在，执行点不存在。**

**修法。** `strchr(out, '/')` 改成 `memchr(out, '/', w)`，只搜已经写过的 `w` 个字节。
修复后同一条命令 40/40 返回 0（正确拒绝），从 `build/` 跑返回 4（读不到 `README.md`），
这正是必须带 `WORKING_DIRECTORY` 的原因。`src/platform/vfs_probe.c` 另加两条：
`"os:/a/b/.."` 仍须正常归一化到 `"os:/a"`（前缀**内部**的 `..` 不能一并禁掉），以及
**读路径**上的逃逸 `im_vfs_read_file(v, "os:/../README.md", ...)` 必须失败。

**负对照。** 用 `git show HEAD:src/platform/vfs.c` 编出的旧探针 rc=**2**（逃逸检查触发），
证明这个 pin 看得见修复前后的差别。注册为 CTest `#124 vfs_probe`，放在注册序列末尾。

**诚实边界。** ① 这个缺陷**不是 Windows 专有**，两个平台都在读未初始化的栈内存；Linux
上 40/40 都命中，是因为那个缓冲区恰好带着调用方的旧字节。② 我改的是「只搜已写入的
字节」，没有改 `..` 的语义。

## §1.44 `im_platform_write_file` 的成败极性反了，于是 POSIX 的 `server_start` 每次自杀

`src/platform/platform.c:213` 原本是

    int ok = (n == length && fclose(f) == 0) ? 0 : -1; if (ok) return 0; return -1;

成功码是 0、失败码是 -1，而 `if (ok)` 在**成功时**为假。两个方向都反了：写成功返回 -1，
写失败返回 0。唯一调用者是 `src/mod/server_mod_posix.c:89`：

    if (mlen < 0 || im_platform_write_file(path, metadata, (size_t)mlen) != 0) {
        im_process_kill(proc); im_process_close(proc); return (push_int(vm, 0), 1); }

`!= 0` 是「当成失败」，而写成功恰好返回 -1 ⇒ **`server_start` 每次都 kill 掉自己刚
spawn 的子进程，并返回 0 而不是房间号**，在每一个 POSIX 运行上，而进程退出码是 0。

**为什么门禁看不见。** 这个函数在 Windows 上**没有任何调用者**（`im_platform_read_file`
的三个调用点也都在 `server_mod_posix.c`），所以它不是 Windows 缺陷；而 POSIX 侧没有任何
用例断言过 `server_start` 的返回值。这是「同一个计算有多个生产点」的反面：
**一个生产点，两个方向都写反了，而没有断言看着它。**

**修法。** 直接返回那个三元表达式。`src/platform/platform_probe.c` 加一个往返（写必须
成功、读必须看见、写到一个打不开的路径必须拒绝），返回码 20–23。负对照：用 HEAD 版
`platform.c` 编同一个探针 → rc=**20**。

**诚实边界。** 我没有在 POSIX 上真跑一次 `server_start` 的端到端流程（它需要
`server_mod_posix.c` 那一整套房间与子进程环境）；证据是「极性反了」这一读法 + 调用者
的 `!= 0` 判据 + 往返探针。

## §1.45 同一个内建名，两份实现，四个不同的答案（其中一个是崩溃）

`src/runtime/runtime.c`（WIN32）与 `src/runtime/runtime_posix.c`（POSIX）用不同的代码注册
**同一批名字**，所以一个名字有两份实现就是一个名字有两个答案。59 个同名内建逐条对拍出
四条真分歧：

| 输入 | POSIX | WIN32（修前） |
| --- | --- | --- |
| `int(9007199254740993)` | `9007199254740993` | `1` |
| `int(9223372036854775807)` | `9223372036854775807` | `-1` |
| `int("0x10")` | `0`（**修前**）→ `16` | `16`（**本轮裁定两端统一**） |
| `chr("A")` | `""` | `'\x01'`（每次不同） |
| `atomic_get(42)` / `atomic_set(42, 7)` | `0` | **进程崩溃（rc=5）** |

**① `int()` 的宽度。** `src/runtime/runtime.c:40` 用 `int res` 中转
（`res=(int)strtoll(...)`、`(int)v->fval`），而 `ival` 是 64 位整数槽
（`docs/DECFY_DESIGN.md` **§2 (a) 层次划分表 item ⑤**，写下时 `:76`、`src/vm/vm.h:24-26`），所以这是把 64 位值 mod 2³² 后符号扩展。
`src/runtime/runtime_posix.c:75` 一直是 `int64_t n`。

**② `chr()` 不查类型就读 union。** `src/runtime/runtime.c:354` 原本是
`int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival;`。`ival` 与 `sval` 是**同一个 union 的两个
成员**，按 type tag 二选一有效；对一个 `VAL_STRING` 读 `ival` 读的是**指针的一半**，所以
`chr("A")` 每次给一个不同的控制字符。POSIX `:400` 先判 `v.type == VAL_INT`。

**③ `atomic_get`/`atomic_set` 的 NULL 名字 —— 这一条会崩。** `:1631`/`:1650` 把非字符串
实参变成 `nm = NULL`，然后**无条件**把它交给 `strcmp(vm->globals[i].name, nm)`。同族的
`builtin_atomic_add:1585` 一直有 `if (!nm) { push_int(vm, 0); return 1; }`，POSIX 的
`posix_atomic_find` 开头也有同样的检查 —— 所以这是**一个家族里漏掉的一个守卫**，不是有意
的设计分歧。实测 `atomic_get(42)` 在 Windows 上 rc=5，输出 `[crash] rip=... [stack] #0..#15`，
是用户可复现的进程崩溃。

**④ 参数顺序：`say_log` / `say_file`。** 文档与 POSIX 副本都写 `say.log(text, level)`、
`say.file(text, path)`（`src/mod/say_mod_posix.c:11`、`:31-38`），也就是**文本是第一个
实参**；`src/mod/say_mod_windows.c:16`/`:27` 把第一个实参当成了另一个值。于是
`say_log("TEXT", "WARN")` 在 Windows 上打印 `[TEXT] WARN`（文本当成了级别）。同一份文件
还缺 `say_target` 的 `console`/`log`/`json` 三分支，且 `say_ai` 对已经是 JSON 的载荷一律
再加一层引号与转义（POSIX `:42` 的 `is_json` 分支）。

**⑤ 同批修掉的其余项。** `src/mod/net_mod.c:135-136` 的 `net_recv` 单参形式无条件读下标 1
（POSIX `:26-28` 按 `argc` 分支）；`src/mod/server_mod.c:184/187/189` 三条遗留的 `[srvdbg]`
调试输出（POSIX 对应物不打印）；`lan_ip` 在 Windows 上以 flags=0 注册而 POSIX 是
`1|CAP_NET`（`src/mod/server_mod_posix.c:145`），于是 `--safe` 下 POSIX 拒绝、Windows 放行；
`src/mod/io_mod.c:750` 的知识库路径硬编码成作者机器的 `D:\inimerse_stable\_ai_kb.im`；
`src/mod/verse_dist_mod.c:2648` 的 `iv.ival = (int)expires_at` 把 `uint64_t` 的 Unix 秒截成
32 位（2038-01-19 起变负数），以及 `src/runtime/runtime.c:1697` 的 `vv.ival = (int)vs[i]`
把 gc 计数截成 32 位。

**为什么这些活了下来。** 唯一做过 Windows↔POSIX 对拍的用例 `posix_runtime_parity` 在
`CMakeLists.txt:676` 的 `posix_runtime_parity` 当时被 `DISABLED TRUE`（只在 `if(NOT WIN32)` 分支启用）—— **这个 `DISABLED` 分支已由 `da79097` 删掉、两端都注册**（`CMakeLists.txt:677` 与 `:1040` 的注释自己写着），所以
**没有任何用例在 Windows 上比较过这两份运行时**。

**修法与 pin。** 新 pin `vtest/divergent_builtin_contract_v06.im` 注册为 CTest `#125`，
**两端都跑**：Linux 上判 POSIX 副本，Windows 上判 WIN32 副本，打印同一行。

**证据（真 Windows 工具链）。** msys2 里没有 `cmake.exe`，所以用 mingw64 gcc 按
`CMakeLists.txt:427-436` 的 WIN32 源列表（`list(APPEND INIMERSE_ENGINE_SOURCES` 那一段）手工全量编译引擎（`.verify/v31/dvbuild.sh`）。
**修前**引擎在那条 pin 上 rc=**5**；**修后**引擎 rc=**0**，且输出与 Linux **逐字相同**：
`divergent-ok i1=9007199254740993 i2=9223372036854775807 i3=16 i4=16 c1=A c2empty=true a1=0 a2=0 a3=0 a4=0`（`i3` 就是上表那条 `int("0x10")`，已按人的裁定恢复断言）。`say_pair_probe_v06.im` 在两个引擎上同样逐字相同
（`[WARN] TEXT`、`payload:{a:1}` 不加引号、`payload:"plain"` 加引号、`null`、`saypair rc=1`）。

**为什么不是「挑一侧当基线」。** 本轮的判据是**消去同一语义的两个决定点**，不是
「让 Windows 抄 POSIX」。`docs/DECFY_DESIGN.md:8-12` 把这条写死了：「用 `.im` 重写」不是修法，
真正的修法是让所有后端消费**同一个 IR（字节码）**、把语义收敛成**一张表**，使分歧在结构上
不可表达。`docs/STATUS.md` 阶段一的两条 `[ ]` 直接对着这件事（把 Fiber/线程/锁/进程能力拆成
POSIX/Windows 后端、统一走 `src/platform/features.h` 的能力声明）。先例是 `docs/STATUS.md`
§5 P1 第 8 条：`inim-client` 的子进程派生是 POSIX-only，非 POSIX 平台**明说未实现（退出码 4）
而不是假装可用**。所以判据顺序是：① `docs/SYNTAX.md`/`docs/API.md` 明文 → 照它；② 文档
沉默 → **显式拒绝**，**不挑任一侧的值当规范**。这是仓库时代的操作性推断，不是设计期约定：
设计对话（791 条消息）里作者从未讨论过平台基线，只有 `m00181`「初步完成了去Windows依赖」与
`m00185`「Linux…甚至无母系统」，方向是**减少依赖**，不是「POSIX 是基线」。

**判据的最终来源是人，不是任何一侧的代码。** 本节先把 `int("0x10")` 登记为未裁定，随后由协调者连同 `load_params`/`round` 一起提交给用户；**用户裁定两端都答 `16`**。所以这一条现在的状态是 **RESOLVED BY HUMAN**，落法是**两份副本一起改**（`src/runtime/runtime.c:40` 保留 base-16，`src/runtime/runtime_posix.c` 补上同一条分支），而不是「POSIX 抄 Windows」—— 方向由人给，代码跟着裁定走。

**诚实边界。** ① `int("0x10")` **已由人裁定为两端都答 `16`**（本轮），状态从 REGISTERED, NOT RESOLVED 变为 RESOLVED BY HUMAN。它此前的分歧是：`src/runtime/runtime.c:40` 接受 `0x` 前缀、POSIX 副本不接受。我**先把它补进 POSIX**、随后**撤回**了（那是把一个决定点的**特性移植**到另一边，不是消去分歧 —— 修法把 1 个问题变成了 2 个），登记一轮后由人拍板；现在**两份副本一起改**，pin 恢复断言 `i3=16`，`docs/SYNTAX.md` §7.3 **R4** 的「十六进制有两条扫描路径」随之消去一条。② `load_params` /
`save_params` 的**无参**行为仍然分歧（POSIX 注册成 `posix_unsupported` 答 -1，Windows 有真
实现答 0/1），方向未定，**我没有动它** —— `docs/API.md:590` 说这套 `.params` API
「在源码中无实现证据」，即 POSIX 侧才是文档描述的状态。③ `round` 对非数字参数 Windows
抛错、POSIX 返回 nil，是**有意保留**的硬/软拒绝设计分歧，未动。

## §1.46 一条被两处引用、却不在被引处里的「教条」

`docs/AUDIT.md:1135` 与 `docs/STATUS.md:3325`（写作时 `ab70a71` 上；`main` @ `274c233` 上是 `:3346`，**整段 +21**）**[obs: ab70a71 → `main` @ 274c233]** 各自写着

> ③ §1.14 的教条是「宁可抛，不要静默算错」。

两处都把 **§1.14** 当作一条**通用教条**的来源。但 §1.14（`docs/AUDIT.md:624`）的标题是
「v3.1 整数位宽：Value 的整数槽改 int64，模糊测试的分歧归零」—— 它定义的是**算术运算符**
在溢出与整除上的行为（`ib == 0` 抛 `division_by_zero`、64 位总额越界抛 `numeric_overflow`），
不是「任何内建遇到不认识的参数类型就抛」。顺带核一条：`docs/DECFY_DESIGN.md` 里**根本没有
§1.14** —— 它的 §1 只到 §1.4（`:18`/`:35`/`:45`/`:52`），所以「§1.14」这个编号只能指
`docs/AUDIT.md` 的那一节。

**这条差别是有后果的。** 它是我在 §1.45 里**差点据此给 `chr(non-int)` 加硬抛**的依据：如果
仓库真的有一条「宁可抛」的通用教条，那 `chr("A")` 答空串就是违反它。但 §1.14 只覆盖算术，
而 `docs/SYNTAX.md` §7.1 的 D1–D12 说明这个引擎整体是**极度宽容**的（D3「类型标注是纯装饰，
没有任何静态或动态检查」、D4「函数调用参数个数两个方向都不报错」、D5「未声明变量求值为
`nil`」）。**在一处硬抛 = 凭空造一条只此一家的约定**，比两端一致的错误答案更糟。所以 §1.45
的 `chr` 落法是「两端都答 `""`」（把非确定性换成确定性），并应在 `docs/SYNTAX.md` §7.1 按
D1–D12 的格式登记成一条 D 类条目 —— 而不是硬抛。

**另一处引用是对的。** `docs/STATUS.md:3346`（`main` @ `274c233`；写作时 `ab70a71` 上为 `:3325`）**[obs: `main` @ 274c233 ← ab70a71]**前一句「§1.14 让整数溢出抛异常而不是回绕的
同一条理由」**站得住** —— 那正是 §1.14 定义的东西。错的只是把同一节升格成通用教条的那半句。

**为什么值得单列一节。** 这不是抄错行号，而是**引用被当成了答案**：一个标签（「§1.14」）
在**两个生产点**上被赋予了一个它并不承载的语义，而下游拿它当依据做决定。它与 §1.40
（超时兜不住名字解析）、§1.41（`ENVIRONMENT` 最后写者胜）同类：**同一个主张有多个生产点，
而且没有一个点去核对源头。** 处置按仓库惯例：历史小节保留自己的文字，本节给出指针与更正，
不改写它们。

**诚实边界。** ① 我只核了这两处**显式**写「§1.14」的地方；`docs/` 下未显式写编号但转述
同一教条的位置**没有全查**。② 本条第一版保留了两处原文、只在这里更正；协调者随后按用户的裁定**就地改掉了** `docs/AUDIT.md:1135` 与 `docs/STATUS.md:3325`（只改「教条」那半句并各加一句指向本节，两节其余文字不动；后者在 `main` @ `274c233` 上是 `:3346`）**[obs: ab70a71 → `main` @ 274c233]**。③ 「§1.14 覆盖算术、
不覆盖通用参数类型」这个判断的依据是它的标题与正文第 4 条；我没有找到任何把它写成通用
教条的地方。

## §1.47 `round` 对非数字实参：两侧各自的现行拒绝形态，已登记并 pin 住

`round("x", 2)` 在 POSIX 上答 `nil`（`src/runtime/runtime_posix.c:104` 的 `posix_core_round`：
非 `VAL_INT`/`VAL_FLOAT` 时 `pop` 两次、`push_nil`），在 Windows 上抛
`round: expected number`（`src/runtime/runtime.c:41` 的 `builtin_round`：同条件
`vm_throw_msg`）。**本轮不改任何一侧**，但把它从「未记录」变成「已登记、已 pin」——
因为**把未记录当成有意，正是本轮在修的那个病**。

**先例是 §1.25。** `docs/AUDIT.md:1115` 那一节的标题逐字是「同一段程序在两个平台上意思不同：
`sum()` 的非数字元素」：POSIX 答 `nil`、WIN32 抛 `sum: non-numeric element`，处置（见 `:690`）
是**保留两侧风格并记录，没有统一**。`round` 与它同形，所以处置也同形。

**provenance（归因，不是理由）。** Windows 侧存在一条显式的参数校验、POSIX 侧没有；
**作者与时间不可考**（设计记录 `/home/sakiko/i.json` 里 `expected number` 命中 **0** ——
这句是仓库时代产物，不是设计期约定）。「有人特意加的」是推断，「不可考」才是事实，
所以它**不能**当作 rationale 用，只能当作 provenance 记。

**分类。** POSIX 的 `nil` 属 `docs/SYNTAX.md` §7.1 **D5**【危险·静默】；Windows 的抛是
【响的失败】。按 §7 自身的分类，响的优于静默的 —— 但**「更好」不等于「已裁定」**，
所以这一条的状态是 **REGISTERED, NOT RESOLVED**，不是「已经对了」。
若要收敛，方向是**两端都抛**；那是新立一条规范，要人批，本分支不采。

**pin 的形状。** `vtest/round_nonnumber_contract_v06.im` 注册为 CTest `#126`
`round_nonnumber_contract_runtime`，`PASS_REGULAR_EXPRESSION` 在 **configure 期按平台选**：
Windows 断言 `round: expected number`、POSIX 断言 `round-nonnumber=nil`。
这**不是**「挑一侧当基线」—— 它断言的是**双方各自的现行值**，所以任一侧漂移都会立刻变红，
分歧保持可见且可变检测，不需要先裁定谁对。这正是 §1.43 缺的那一环（`vfs_probe` 建了却
从没 `add_test`）。

**诚实边界。** ① 这条只覆盖 `round` 一处，**不是** 59 个同名内建的全量对拍
（全量对拍的形状见 §1.45 与 §10.77 的 pin，尚未做成 59 名一表）。② `round` 的 Windows
一侧**没有在真 Windows 上跑过**本节的断言形状以外的东西：本机是 POSIX 构建，
`src/runtime/runtime.c` 在 Linux 上不参与编译（`CMakeLists.txt` 的 `if(WIN32)` 分支），
Windows 的数值证据来自 ucrt64 手工全量编译的引擎。③ 判据是 `str()` 的输出，
所以它钉的是**值**不是退出码（`docs/SYNTAX.md` §7.2 **M13**：101 个 CTest 里 66 个
只断言退出码）。

## §1.48 一个没被写过的字节，让 `a.b` 变成了 `a?.b`

**症状。** 参数文件里写 `player.max_hp = 42`，脚本里读 `player.max_hp`：有些文件答 `42`，
有些文件答 `nil`。答错的那一批看起来**完全由文件大小决定** —— 400 字节以内对、401 字节
开始错。顺着这条线索找会一路找错地方：`inim_load_text` 的上限是 `1 << 26`，全 `src/`
没有任何 `400` 这个常量。

**真正的判据是一个从未被赋值的字节。** 在 `src/compiler/compiler.c` 的 `case EXPR_MEMBER:`
顶上临时插桩，同一份 `buildc` 下两个文件的差别只有一行：

```
对的文件:  member='max_hp' objtype=3 safe=0
错的文件:  member='max_hp' objtype=3 safe=97
```

`safe` 是 `97`，也就是字符 `'a'` —— 一块分配器还回来的旧数据。

- `src/parser/ast.h:63`：`struct { Expr *object; StringView member; bool safe; } member;`
- `src/parser/parser.c:570`（普通 `.` 路径）：`Expr *mem = malloc(sizeof(Expr));`，随后只写
  `type` / `member.object` / `member.member`，**`safe` 一次也没被写过**；同一段的安全路径
  `src/parser/parser.c:549` 用的是 `calloc` 并显式 `safe = true`。
- `src/compiler/compiler.c:1083`：`if (expr->member.safe)` —— 字节非 0 就成立 ⇒ `a.b` 被当成
  `a?.b` 编成 `OP_INDEX_GET`。点号全局（例如名为 `player.max_hp` 的参数）在对象 `player`
  上当然查不到，于是**静默答 `nil`**。

文件大小、注释长短、字符串字面量长短都只是「让那一个字节碰巧非零」的手段。交叉验证：长注释、
长字符串字面量、12 条短注释都能触发；50 条 `y$i = $i` 填充到 462 字节**反而正常**（分配模式
不同）。所以「400 字节阈值」是**观察的假象**，不是机制。

**这不是编码错误，是同一语义的两个生产点，而判据是未初始化内存。** 它与 §1.40（CMake 的
`ENVIRONMENT` 是共享属性、最后写者胜）、§1.46（引用被当成了答案）同族：一个值在两个地方被
生产，而没有任何一处负责把它定下来。区别在于这里的「两个地方」不是两段代码，而是**同一段
代码的两个分支**，中间隔着一个没人写过的字节。

**修法。** `src/parser/parser.c` 里 AST 分配本来就不一致：22 处 `calloc`，95 处 `malloc`。
按类型统一为零初始化：

| 替换 | 处数 |
|---|---|
| `malloc(sizeof(Expr))` → `calloc(1, sizeof(Expr))` | 40 |
| `malloc(sizeof(Stmt))` → `calloc(1, sizeof(Stmt))` | 55 |
| `malloc(sizeof(Program))` → `calloc(1, sizeof(Program))` | 1 |

于是 `safe` 在 `.` 路径上**按构造**为 false、在 `?.` 路径上显式为 true —— 判据不再依赖分配器
的历史。

**pin（非空验证）。** 新增 `src/parser/parser_member_safe_probe.c`，它**先往堆里灌 `0x01`
再解析**，所以任何没被零初始化的节点都必然带着非 0 的 `safe`，不靠运气：

- 修复前：`5 failure(s)`，rc=1（`x.y`、`x.y.z` 两层、`x?.y.z` 的外层、`x.y?.z` 的内层）
- 修复后：10 项全 `ok`，rc=0
- 同一支探针在 mingw64 上编出并运行，结果与 Linux 相同

CTest `#127 parser_member_safe_probe`（注册在最后，既有 `#N` 不动），`tools/gate.sh` 的
`EXP_CTEST` 126 → 127。

**诚实边界。**
1. 我只证明了 `member.safe` 这一个字段的后果。95 处 `malloc` 意味着**其它节点的每一个未写
   字段**此前同样是未定义值；我修的是这一整类，但只为 `safe` 造了可复现的用例。
2. 「400 字节」这个数字是我最初的误判。写在这里是为了记住假象的形状，它不是机制的一部分。
3. 探针的 `0x01` 灌注依赖分配器复用同尺寸的已释放块（glibc 的 tcache/fastbin LIFO，以及
   mingw 的对应行为）。两个平台都实测复现了修复前的失败，但严格说它依赖这条复用规律。
4. 我只核了 `src/parser/parser.c` 一处；`grep -rn 'malloc(sizeof(Expr))\|malloc(sizeof(Stmt))'
   src/` 在修复后为 0 命中，别的目录本来就没有 AST 分配。

## §1.49 参数文件的名字是相对谁解析的，以及「没读到」为什么不是答案

**症状。** `inimerse --params s.params s.im` 答 `s=42`，把脚本换成子目录里的 `inimerse --params s.params sub/s.im` 就答 `s=nil`，**退出码仍是 0**。一个用户明明命名了的文件被忽略，程序带着错值跑完。

**机制。** `load_and_run()` 打开参数文件的位置在 `src/main.c:1346` 的 `chdir_to_script_dir(script)` **之后**（`:1348 load_and_run(&vm, read_path)`），而 `params_path` 是一个**相对**路径（默认 `"params.params"`，`src/main.c:195`），于是它是相对**脚本所在目录**解析的，不是相对调用者的工作目录。脚本在当前目录时两者恰好重合，所以它一直没被发现。

**三处修改。**

1. `src/main.c` 在任何 chdir 之前就把 `params_path` 固定成绝对路径（`make_abs_path_loose`，锚定 `g_caller_cwd`）。
2. 新增 `params_explicit`：`--params` 命名的文件**不是可选的**，读不到就报错（`load_params_or_report()`）；默认的 `params.params` 仍然可选。这是同一个「静默 nil」形状的另一半。
3. 删掉 `load_and_run_source()` 里的裸调试打印 `[main] compile preregister gc=%d g23.name=%s`（原 `src/main.c:355-356`）。它与 `b1bdc00` 删掉的字符串池 dump **同一个来源**：`git log -S` 追到 `8248e08 Release Infiverse 0.2.0`，也是从 0.2.0 起每版都在吐，而且把 `vm->globals[23].name` 这个内部索引写死在消息里。

**一条旁证。** `vtest/params_precompiled_v06.inim` 是用**修复前**的 `buildc` 编的，于是它把 §1.48 的缺陷**烘进了字节码**：同一份 `.im` 源码经 `.inim` 路径跑出 `player=nil`，重新编译后才是 `player=42`。`.inim` 是一个把编译期结论存起来的生产点，所以修了前端不会自动修好已有的字节码。

**pin 与双向验证。** 新增 CTest `#128 params_relative_path_runtime`、`#129 params_missing_explicit_runtime`、`#130 params_precompiled_runtime`（都注册在最后），`EXP_CTEST` 127 → **130**。第一条的 `--params` 参数**故意写相对路径**：写绝对路径会让 chdir 变得不可见，用例会在缺陷上面绿。`git stash push -- src/main.c` 后重编运行：**三条全部 Failed**（`Required regular expression not found`）；`git stash pop` 重编后 **4/4 Passed**。

**诚实边界。**
1. 我只核了 `--params` 这一个选项。`src/main.c` 里还有其它在 chdir 之后才被打开的路径，我没有逐个查完。
2. 「`--params` 命名的文件读不到就报错」是一个**行为改动**，不是修 bug的必然结果——有人可能依赖「缺失就跳过」。我把它写在这里而不是当成修法的一部分。
3. `params_precompiled_v06.inim` 是一个**二进制产物**。它会随前端变化而陈旧，而且没有任何东西在校对它与源码是否同步——这正是本仓库反复出现的「一个偶然对上的常量被当成了保证」。
4. 三条用例都只断言程序输出，不断言退出码。第二条用 `FAIL_REGULAR_EXPRESSION "params-relative speed="` 补上「脚本不得跑完」这一半，否则一个打了错误信息但退出 0 的引擎会蒙混过去。

## §1.50 能力串有两个生产点：同一句 `spi_meta` 在两侧拿到不同的权限

`spi_meta(id, version, caps)` 的第三个实参是一串能力名，而仓库唯一写过它形状的地方是
`src/vm/vm.h:159` 的注释——`spi_meta(id, version, "io,net")`，即**逗号分隔的整名**。
但**解析它的是两份各自独立的代码**：

- POSIX `posix_spi_meta`（`src/runtime/runtime_posix.c:948-956`）对**整串**连做六次 `strstr`：
  `if (strstr(caps.sval, "io")) mask |= CAP_IO;` …；
- WIN32 `spi_parse_caps`（`src/runtime/runtime.c:1185-1200`）先跳到下一个 `,`，再 `strncmp(p, "io", 2)`。

于是两侧都把**能力名当成一段更长字符里的子串**来匹配，而且**方向相反**。实测（修前，POSIX | WIN32）：

| `caps` 实参 | POSIX | WIN32 | 谁错了 |
| --- | --- | --- | --- |
| `"io,net"` | 768 | 768 | 都对（**唯一**一致的一格，也正是 `vm.h` 写下的形状） |
| `"io net"` | 768 | 256 | WIN32：从 `"io net"` 这个非 token 里**前缀**匹配出 `io`，并**静默丢掉 `net`** |
| `"audio"` | **256** | 0 | POSIX：`strstr("audio","io")` 命中，**静默过授 `CAP_IO`** |
| `"ionet"` | 768 | 256 | 两侧都从非 token 里匹配出 `io`，POSIX 还多授了 `net` |

**修法是把两个生产点收成一个**（`docs/DECFY_DESIGN.md:10-12` 的「让所有后端消费同一个 IR」）：
新增 `int vm_parse_caps(const char *s)`（`src/vm/vm.c`，声明在 `src/vm/vm.h` 紧挨它返回的
`CAP_*` 位），按**整段逗号分隔的 token** 精确匹配，正是 `vm.h:159` 写下的那个形状；两侧
runtime 都调它，`spi_parse_caps` 整个删掉。不在那个形状里的输入（`"audio"`、`"io net"`、
`"io, net"`）**什么都不授**——`vm.h:156` 把这个模型叫 **minimal-permission**，所以对未定义的
输入答**更小**的那个集合；写错声明的模组会在第一次受能力管辖的调用上被内建**拒绝**，而不是
悄悄持有一份它从未写出的授权。修后两侧在**全部 11 个字符串输入**上逐字一致：
`doc=768 space=0 audio=0 ionet=0 all=65280 int=256`。

**已登记、未裁定（第二处）**：`caps` 实参的**类型**仍是两个生产点——`spi_meta(id, ver, true)`
POSIX 答 **0**（`posix_spi_meta` 只认 `VAL_INT` 与 `VAL_STRING`），WIN32 答 **65280 = CAP_MASK**
（`src/runtime/runtime.c` 的 `else if (capsv->type == VAL_BOOL) caps = capsv->ival ? CAP_MASK : 0;`）。
设计记录与 `docs/API.md` 都没有规定这个实参的类型，**不挑一侧当规范**（`docs/DECFY_DESIGN.md:8-12`、
`docs/STATUS.md:30` [obs: ab70a71 ≡ main]）。新 pin `vtest/spi_caps_contract_v06.im` 把前六个字段断言成**一份**（两侧已
一致），把 `bool=` 这一个字段**按平台各断言各自现行值**（`CMakeLists.txt`），任一侧漂移立刻变红
——与 §1.47 的 `round` 同一手法。

**同批更正一条上一轮的结论**：§1.45 之后收到的一份报告说 WIN32 `builtin_spi_mods`
（`src/runtime/runtime.c:1328-1354`）用 `vm_array_push` 六次「冒充 dict」、是错的一侧。**实测证伪**：
`vm_dict_set`（`src/vm/vm.c:1027`）**自己的存储布局就是「交替键值的数组」**（`a->items[a->count++] = key_copy;`
`a->items[a->count++] = val_copy;`），哈希表是**惰性**建的（每个消费者进来先 `dict_hash_ensure`，
再 `if (!h->slots && a->count > 0) dict_hash_build(vm, aidx);`），所以在没有「边写边查」的情况下两者
**可观测地等价**。对 `spi_mods()[0]` 实测 12 个字典操作（`len`/`size`/`keys`/`has`/按键取值/取缺失键/
`str()`/整数下标/写入后重查）**两平台逐字相同**。⇒ 那条是**读码结论，不是实测结论**；登记为
「未被复现」，不作修改。

**诚实边界**：① 我只测了 `spi_meta` 一处，没有全仓搜其它「用 `strstr`/`strncmp` 匹配名字」的地方；
② `"io, net"`（逗号后有空格）修后答 0 是**新行为**，两侧此前都不是 0——这是「不在文档形状里就不授」
这条规则的推论，不是从任一侧继承来的值；③ 我没有核 `spi_has()` 的判定是否与 `mod_caps` 的位语义
完全同构（`posix_spi_has` 与 `builtin_spi_has` 的写法本身也有细微差别，本轮未动）。

## §1.51 原子槽不是整数时，两侧都去读了 union 里没被写过的那个成员

**缺陷类**：一个名字两份实现（`src/runtime/runtime.c` 与 `src/runtime/runtime_posix.c`），**两份都不查槽的 `type` 标记就读写 union**。`Value`（`src/vm/vm.h:24-26`）是 `{int type; union {long long ival; double fval;}; char *sval; void *ptr;}`，`ival` 与 `fval` 是同一块存储，读错成员就是读别人的位型。这与 `docs/SYNTAX.md` §7.1 **D13**（`chr` 不查 `type` 就读 `ival`）是**同一个病**，只是 D13 当时只修了 `chr` 一处。

**实测（POSIX，修前，退出码全部为 0）**：

| 语句 | POSIX 修前 | WIN32 修前 | 修后（两侧） |
| --- | --- | --- | --- |
| `atomic_get("y")`，`y = 1.5` | `4609434218613702656`（`1.5` 的 IEEE754 位型） | `0` | `0` |
| `atomic_get("s")`，`s = "abcdef"` | `1` | `0` | `0` |
| `atomic_add("f", 1)`，`f = 1.5` | `4609434218613702657`，且 `f` 被毁 | `1`，且 `f` 被毁 | `0`，`f` 保持 `1.5` |
| `atomic_add("h", 0)`，`h = 2.5` | `4612811918334230528`，**加零就把值毁掉** | `0`，同样毁掉 | `0`，`h` 保持 `2.5` |
| `atomic_add("t", 1)`，`t = "abcdef"` | `2`，字符串被毁成 `2` | `1`，字符串被毁成 `1` | `0`，`t` 保持 `"abcdef"` |
| `atomic_get("k")` / `atomic_add("k", 5)`，`k = 7` | `7` / `12` | `7` / `12` | `7` / `12`（未变） |

两侧的机制不同但都错：POSIX（`src/runtime/runtime_posix.c` 的 `posix_atomic_get` / `posix_atomic_add`）**完全没有门**，直接 `__sync_add_and_fetch(&vm->globals[idx].val.ival, 0)`；WIN32（`src/runtime/runtime.c` 的 `builtin_atomic_add`）有一句 `if (val.type != VAL_INT) { type = VAL_INT; ival = 0; }`，**把「不是整数」当成了「它是 0」**，于是确定地把浮点值毁掉。前者是不确定的错答，后者是确定的数据毁坏——都是本仓库在修的那个形状。

**修法**：两侧都先问 `type`。一个不是 `VAL_INT` 的槽**不是计数器**，所以答 `0` 并**不碰那个槽**——这正是这个族在名字解不开时已经给的答案（`idx < 0` 的三处都是 `push_int(vm, 0)`），所以**没有新立约定**。`atomic_set` 不动：它是调用方明确要写一个整数进去，把槽变成 `VAL_INT` 是应该的。

**为什么不报错**：与 D13 同一条边界。`docs/SYNTAX.md` §7.1 的 D1–D12 说明引擎整体极度宽容，而**仓库里没有任何一条「参数/槽类型不对时怎么办」的规范**；报 `type_mismatch` 是新立一条规范，需要人批。当前答 `0` 仍属 `docs/SYNTAX.md` §7.1 的**危险·静默**类，已登记为 **REGISTERED, NOT RESOLVED**：要收敛的方向是「两端都报类型错」，但本分支不采。

**双向验证**：`vtest/atomic_slot_type_contract_v06.im`（CTest **#132** `atomic_slot_type_contract_runtime`，`PASS_REGULAR_EXPRESSION` 钉住整行）。`git stash push -- src/runtime/runtime_posix.c` 后重编，同一条命令打出
`atomic-slot-ok g1=4609434218613702656 r1=4609434218613702657 y=4609434218613702657 r3=4609434218613702657 yz=4609434218613702657 g2=1 r2=2 s=2 g3=7 r4=12`、**退出码 0**；恢复后打出
`atomic-slot-ok g1=0 r1=0 y=1.5 r3=0 yz=1.5 g2=0 r2=0 s=abcdef g3=7 r4=12`。两行在两个平台上**同一行**（故意不分平台分支）。Windows 侧（ucrt64，CI 同款工具链；ctest 汇总分母 **122**，**不是 `Total Tests:`** —— 该平台注册数是 **131**（`c71ea00`），见本节 §1.65）另做了同形的 A/B：把 `src/runtime/runtime.c` 的新守卫换回 `if (type != VAL_INT) { type = VAL_INT; ival = 0; }` 后重编，该测试打出
`atomic-slot-ok g1=0 r1=1 y=1 r3=1 yz=1 g2=0 r2=1 s=1 g3=7 r4=12`、**Failed**（`PREFIX_CTEST_RC=8`）；换回后重新 Passed（`FIXED_CTEST_RC=0`）。这一行同时把上表里 WIN32 一列的「修前」值从读码结论升为**实测**。

**诚实边界**：① 只核了 `atomic_get` / `atomic_add` / `atomic_set` 三个，族里其它名字未逐一核；② WIN32 侧的「修前」值已由 ucrt64 上的 A/B **实测确认**（打出 `g1=0 r1=1 y=1 r3=1 yz=1 g2=0 r2=1 s=1`、Failed，与上表逐格相符），不再是读码结论；③ 「应该报错而不是答 0」这一问未裁，本节只记录它；④ `posix_atomic_set` 与 `builtin_atomic_set` 都是无条件写 + 改 `type`，本节认为合理，但没有为它写用例。

## §1.52 一个内建名字有两个生产点，而只有一个被注册

**症状（Windows，退出码 0）。** `random(10)` 返回 **27606**；`random(0)` 返回 **27606** ——
**同一个值**，因为实参根本没被读。同一个程序在 POSIX 上返回 `3`，`random(0)` 返回 `0`。

**两个生产点。**

| 位置 | 实现 | 是否被注册 |
|---|---|---|
| `src/runtime/runtime.c:28` `builtin_random`（**修前**在 `:16`，那里现在是说明注释） | `int max = ...ival; rand() % max` | **否** —— `runtime_register_builtins`（`src/runtime/runtime.c:1771`）里没有这个名字 |
| `src/mod/io_mod.c:143` `builtin_random`（**修前**；那里现在是说明注释） | `push_int(vm, rand())`，**完全忽略实参** | 是（**修前** `src/mod/io_mod.c:322`；该行已删除） |

`src/mod/io_mod.c` 只在 Windows 上编译（`CMakeLists.txt:431`），POSIX 侧由
`src/platform/posix_stubs.c:6` 把 `io_mod_register` 桩掉，所以这个分歧只在 Windows 上出现。
于是 runtime 里那个带 `max > 0` 门、看起来正确的实现是**死代码**，真正回答的是那个不读实参的副本。

**为什么「先注册者胜」不是解释。** `vm_register_builtin`（`src/vm/vm.c`）不查重，
`builtin_insert` 线性探测**只找第一个空槽**，所以同名时先注册的那条在探测序列里先被查到。
`runtime_register_builtins` 与 `register_core_modules` 的调用顺序（`src/main.c:1096` / `:1097`）
本来让 runtime 侧先注册 —— 但修前 runtime 侧**根本没注册这个名字**，顺序无从生效。
修后顺序也不再承重：**只剩一个生产点**。

**修法（三处编辑）。**

1. `src/runtime/runtime.c:28`（**修前** `:16`）的 `builtin_random` 补上 POSIX 已有的门：
   `push_int(vm, max > 0 ? rand() % max : 0);` —— `rand() % 0` 是整数除零。
2. `src/runtime/runtime.c:1774` 在 `runtime_register_builtins` 里注册 `random`。
3. 删掉 `src/mod/io_mod.c:143-147` 的副本与 `:322` 的注册行（两处都是**修前**行号）。

**双向验证（真 ucrt64 引擎，CI 同款工具链）。** 把两个文件都 `git checkout` 回 `HEAD` 重建 ⇒
`random(10)` → `27606 9428 30941`、`random(0)` → `27606 9428`（与前者头两个值**逐字相同**），
两次退出码都是 **0**；换回修法 ⇒ `random(10)` → `1 7 9`、`random(0)` → `0 0`。

**pin。** `vtest/random_bounded_contract_v06.im` / CTest **#133**，`PASS_REGULAR_EXPRESSION`
钉整行，两平台**同一行** —— `posix_random` 本来就有上界与 `n > 0` 门，本轮只是把同一条规则
给了第二个消费者，**没有新立规则**。Windows 上把两个文件退回 `HEAD` ⇒ 打出
`random-bounded-ok one=false zero=false neg=false ten=false big=true`，**退出码仍是 0**。

**诚实边界。**

1. 五个字段里 `big` 在修前也是 `true` —— 裸 `rand()` 本来就小于 1000000。它留在断言里是因为
   它同时守住「修完不许比上界还大」，但**它不是这条缺陷的证据**，只有另外四个字段是。
2. `random("abc")` / `random(1.5)` 这类**非整数实参**两平台同形（都去读 `ival`），
   与 `docs/SYNTAX.md` §7.1 **D13** 同病，**本轮未动，也没有登记为本条的一部分**。
3. 我核过的是 `random` **这一个名字**，不是「所有重名」。注册表层面的集合比对只做了一次（见 §1.53）。
4. `projects/demo/main.im:60` 的 `rand(a, b)` 与本条无关，见 §1.53。

## §1.53 登记：三个「一个名字有两份说法」的实例，本轮不动代码

§1.52 修的是**同一份注册表里的重名**。同一形状还有三处，全部**只登记**，
理由逐条写在下面 —— 它们的共同点是：**改变哪一份都不是我能单方面决定的**。

| 实例 | 两份说法 | 为什么本轮不动 |
|---|---|---|
| `gui_fullscreen` | `src/mod/gui_mod.c:3690` 注册 `builtin_gui_fullscreen`（`:1661`，用 `SetWindowLongA` 去掉 `WS_CAPTION | WS_THICKFRAME`，**要求实参**）；`:3697` 注册 `builtin_fullscreen`（`:1730`，用 `SetWindowLongPtr` + `WS_POPUP | WS_VISIBLE`、保存 `G.restoreStyle`、**支持无参切换**）。两行在同一个 `gui_mod_register` 里相隔 **7** 行。先注册者胜 ⇒ **`builtin_fullscreen` 不可达**。**注意复核方式**：本节写下这条时，`src/mod/gui_mod.c` 含 5 个 NUL 字节，**普通 `grep` 会把该文件当二进制、只列出 NUL 之前那一行（`:1661`）并把 `binary file matches` 打到 stderr、退出码仍为 0** —— 要拿到 `:3690`／`:3697` 必须 `grep -a`。这些字节已由 §1.55 移除，此后普通 `grep` 即可 | 两个体行为不同，选哪个是人的决定；且 `gui_mod.c` 需要窗口，本机没有可跑的 GUI 断言 |
| `rand` | `docs/SYNTAX.md` §5（写下时 `:500`）把它列进「核心高频内建（**有 `vtest` 覆盖的**）」；`projects/demo/main.im:60` 的 `rand_int` 调用它。**POSIX 侧零注册**；**Windows 侧有**（`src/mod/gui_mod.c` 注册 `rand`，自 `8248e08`（2026-08-27）起）—— ⇒ 写下「全 `src/` 零注册」时该命令在当时的树上**就已不成立**（「这个平台上没有」被写成了「任何文件里都没有」；普查见 [builtin-platform-census.md](streams/builtin-platform-census.md) §4） | 加一个 `rand` 内建是**新立一个名字**，不是消除分歧；正确处置是从文档与示例里去掉它，那要改 `projects/`（见下条边界） |
| `docs/SYNTAX.md` §5（写下时 `:500`）的「有 `vtest` 覆盖」 | 该名单里 `random` 当时**零覆盖**（`grep 'random(' vtest/ tools/ mods/ projects/` 只命中 Python 的 `rng.random()`） | **本轮就地改了**：`rand` 从名单移除，`random` 的覆盖由 §1.52 的 pin 补上 |

**`gui_fullscreen` 的那条登记其实早就存在，只是被当成计数问题。** `docs/API.md:234` 逐字写着
「`gui_mod` 计数虚高 | 表列 163，源码唯一名 **162**；原因是表内 `gui_fullscreen` 重复出现两次 |
源码提取 + 集合比对」—— **一条被登记的事实，没有人消费它**：没人注意到「重复出现两次」
意味着其中一条实现不可达。这与 §1.46 的引文、§1.43 的 `vfs_probe` 是同一个病：
**事实被写下来了，但没有变成可检测的断言。**

**一次注册表层面的集合比对（我做的，`python3` 按 `CMakeLists.txt:427-436` 的 WIN32 源列表
与 POSIX 源列表分别抽 `vm_register_builtin(_full)?(vm, "…")`）：** WIN32 **398** 个名字 /
**2** 个重名，POSIX **128** 个名字 / **0** 个重名。第二个重名 `isolate_run`
（`src/isolate_mod.c:227` 与 `:318`）是**误报** —— 两处分别在 `#ifdef _WIN32` 与 `#else`
分支里，扫描器不认预处理条件。

**诚实边界。**

1. 我**没有**在真 Windows 上跑过 `gui_fullscreen`（需要窗口），「`builtin_fullscreen` 不可达」
   是从 `builtin_insert` 的探测语义与注册顺序读出来的，不是实测。
2. `grep 'random('` 是**字面**匹配，它证明的是「这四处目录里没有 `random(` 这个字符串」，
   不等于「没有别的方式覆盖 `random`」。
3. `docs/SYNTAX.md` §5（写下时 `:500`）那张名单我只核了 `random` / `rand` 两个名字，**其余 50 多个没有核**。
4. `projects/demo/main.im` 我**没有改**：它在 POSIX 上跑到第一条 `gui_stage` 就死了
   （`[exception] uncaught: unknown builtin function 'gui_stage'`），`rand` 那条要等 GUI 起来
   才轮得到，所以「示例里这个函数是坏的」我是用 `rand(1, 6)` 单独实测 + 零注册的 grep 得出的，
   不是跑完示例得出的。


## §1.54 数组池唯一的门不能拒绝一个下标，所以它后面八个 `if (!a)` 都是死代码

**症状**：`vm_pool_slot(VM *vm, int idx)`（`src/vm/vm.c:743`）是数组/字典池唯一的入口 —— 全 `src/` **71 个调用点**都从它取槽 —— 而它**不可能返回 NULL**：

```c
ArrayObj *vm_pool_slot(VM *vm, int idx) {
    if (idx >= 0 && idx < 4096) return &vm->arrays[idx];
    return &vm->arrays_big[idx - 4096];
}
```

两个分支都返回地址。于是：

| 下标 | 修前返回 | 含义 |
|---|---|---|
| `0 <= idx < 4096` | `&vm->arrays[idx]` | 正确 |
| `idx == 4096`（`bigCap == 0`） | `NULL` | **偶然**（`NULL + 0`） |
| `idx < 0` | `&vm->arrays_big[idx - 4096]` | 新 VM 上 `arrays_big == NULL` ⇒ **野低地址**；池长大后 ⇒ 一个真的堆地址 |
| `idx >= 4096 + bigCap` | 分配之外的槽 | 越界 |

**实测（修前）**：`src/vm/vm_pool_slot_probe.c` 在一个 `memset` 清零的 VM 上**不打任何解引用**，只打印返回值：

- Linux：`raw -1=0xffffffffffeefef0 -4097=0xffffffffffddfef0 4096=(nil) 100000=0x18e0a00`
- Windows（ucrt64，CI 同款）：`raw -1=FFFFFFFFFFEEFEF0 -4097=FFFFFFFFFFDDFEF0 4096=0000000000000000 100000=00000000018E0A00`

两个平台的野低地址**逐字节相同** —— 它就是 `NULL - 4097 * sizeof(ArrayObj)`，与平台无关。`arrays_big` 非 NULL 时（探针把 `bigCap` 设成 4）`slot(-1)` 在 Linux 答 `0x5a671f67df10`、在 Windows 答 `00000227F33749A0` —— **都是真的堆地址**，也就是说一个负下标在池长起来之后不再是「低地址崩溃」，而是**读到别的对象**。

**八个 `if (!a)` 是死代码**：`python3` 扫描 71 个调用点（把同一行或后两行出现 `!<var>` 算作有守卫），**只有 8 处有 NULL 守卫、63 处没有**。八处集中在 `src/mod/verse_dist_mod.c`（`:613`/`:631`/`:638`/`:689`/`:699`/`:740`/`:745`/`:1115`），形状都是 `ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1); if (!a) return 0;`。

**这个检查在本文件里不是不知道怎么写**：GC 标记 `src/vm/vm.c:2818` 对**同一个句柄**用的是 `if ((gv->type == VAL_ARRAY || gv->type == VAL_DICT) && gv->ival > 0 && gv->ival - 1 < vm->arrayCount)`，`:2832`/`:2841`/`:2853`/`:2862`/`:2869` 是同形的五处。⇒ **检查存在于一个地方，却不在所有人都要过的那道门上。**

**修法**：把边界放回那道门（`src/vm/vm.c:743`），并在注释里引 `:2818` 的既有谓词：

```c
ArrayObj *vm_pool_slot(VM *vm, int idx) {
    if (idx >= 0 && idx < 4096) return &vm->arrays[idx];
    if (idx >= 4096) {
        int big = idx - 4096;
        if (big < vm->bigCap) return &vm->arrays_big[big];
    }
    return NULL;
}
```

`vm_array_new`（`src/vm/vm.c:748`）在把槽交出去之前一定先 `realloc` + `memset` 并把 `bigCap` 提上去，所以任何**池真正拥有**的下标都不会被拒。

**双向验证**：

- Linux：`git stash push -- src/vm/vm.c` 重编 ⇒ CTest `#134 vm_pool_slot_probe` **`***Failed`**、5 条 `FAIL slot(...) answered …, want NULL`；`git stash pop` 重编 ⇒ `Passed`、`vm_pool_slot_probe: ok`。
- Windows（ucrt64）：把函数体换成修前版本重编 ⇒ `FAIL slot(-1) answered FFFFFFFFFFEEFEF0, want NULL` 等 5 条、`PREFIX_PROBE_RC=1`、CTest `***Failed`；换回 ⇒ 四个 `(nil)`、`ok`、`Passed`。

**新 pin**：`src/vm/vm_pool_slot_probe.c` / CTest `#134 vm_pool_slot_probe`（**无 `PASS_REGULAR_EXPRESSION`，退出码即判据**）。它同时断言 `slot(0) == &vm->arrays[0]` 与 `slot(4095) == &vm->arrays[4095]`，所以「一律返回 NULL」蒙不过去；它也不解引用任何东西 —— 越界答案是**一个指针值**，修前那个值就是证据。

**为什么必须是一条 C 探针**：`.im` 脚本无法命名一个池下标，缺陷在 C 函数里，C 层是唯一能观察这条契约的高度。照 `vm_init_probe`（`CMakeLists.txt:938`）直接链 `inimerse_engine`。

**同批发现：探针在 Linux 能编、在 Windows 编不过。** 初版把局部变量写成 `far` 与 `at`；mingw 的系统头把 `far` 定义成**空宏**（遗留 `__far`），于是 `ArrayObj *far = ...` 报 `expected identifier or '(' before '=' token`（共 3 个 error），而 Linux 的 `-fsyntax-only` 完全看不见。已改名为 `deep`/`edge`，双工具链 0 error。**这是「Linux 门禁看不到 Windows 编译」的又一个实例，只是这次挡住的不是引擎，是我自己的探针。**

**诚实边界**：

① 71 个调用点里 63 个没有 NULL 守卫，其中多数自带 `idx >= 0 && idx < vm->arrayCount` 之类的范围检查（例如 `src/vm/vm.c:885`、`:899`）；我只逐处看了 `src/mod/verse_dist_mod.c` 那 8 处与 `src/vm/vm.c` 内的部分。**「63 处都没有守卫」是扫描器的计数，不是逐处定性。**

② `verse_verify`/`verse_unpack` 的两条调用链今天都先查了类型（`src/mod/verse_dist_mod.c:1154` 的 `if (!ok || pkg.type != VAL_DICT)`、`:813` 的 `if (!ok || pv.type != VAL_DICT)`），所以**负下标在今天不是可达的崩溃**；本轮修的是「不变量由每个调用方各自维持、门自己不检查」。

③ 修法把 63 个无守卫调用点从「野指针解引用」变成「NULL 解引用」——**种类没变坏**，但行为变了。经验判据是全量门禁：**134 个测试全绿、0 跳过**，说明没有调用点依赖越界访问。我没有为这条做静态证明。

④ `idx == 4096` 在 `bigCap == 0` 时返回 NULL 是**偶然**，不是设计；我没有去查历史上是否有代码依赖过这个偶然。

⑤ 探针用 `malloc(sizeof(VM))` + `memset` 造 VM，**不调 `vm_init`** —— 它只观察 `vm_pool_slot` 这一个函数的边界算术，不声称覆盖 VM 初始化（那是 `vm_init_probe` 的事）。

## §1.55 受版本控制的文本文件里的 NUL 字节，让 `grep` 答一个更短的问题

**一句话。** 三个受版本控制的 C 源文件在**块注释里**带着 NUL 字节：
`src/mod/gui_mod.c` **5** 个（1,000,721 B）、`src/lexer/lexer.c` **2** 个（15,432 B）、
`src/lexer/lexer.h` **1** 个（2,513 B）。字节对编译器完全无影响（构建一直绿），
但**对读源码的工具不是**。

**机制（实测）。** GNU `grep` 读到 NUL 就判定该文件是二进制，此后**不再列出行**，但它
**已经把 NUL 之前找到的匹配打到了 stdout**，把 `binary file matches` 打到 **stderr**，
**退出码 0**：

```
$ grep -n 'gui_fullscreen' src/mod/gui_mod.c
1661:static int builtin_gui_fullscreen(VM *vm) {
grep: src/mod/gui_mod.c: binary file matches
$ echo $?
0
$ grep -an 'gui_fullscreen' src/mod/gui_mod.c
1661:static int builtin_gui_fullscreen(VM *vm) {
3690:    vm_register_builtin(vm, "gui_fullscreen", builtin_gui_fullscreen);
3697:    vm_register_builtin(vm, "gui_fullscreen", builtin_fullscreen);
```

**所以这不是「文件读不了」，而是「文件回答了一个比被问的更短的问题，而且答案形状一样」。**
只看 stdout、或不读 stderr 的人，拿到一份**看起来完整**的截断清单。这一次被吞掉的正是
`:3690`／`:3697` —— 证明 `gui_fullscreen` 注册两次、`builtin_fullscreen` 不可达（§1.53）
的那两行。**一条登记在 `docs/API.md:234` 的事实，此前没人能靠普通 grep 复核。**

**修法：把字节去掉，而不是教每个读者加 `-a`。** 8 个字节**全部**落在块注释内
（逐个用「反向数 `/*` 与 `*/`」确认），替换成空格，文件字节数不变、编译产物不变。
修后 `grep -n` 直接给出三行。

**为什么值得单独登记。** 它与本仓库反复记的那一族同形，只是下沉到工具层：
**同一件事有两个生产点**（文件内容 vs 工具的读取策略）、**快路径的门被当成了答案**
（「grep 退出 0」被当成「清单完整」）。`docs/SYNTAX.md:35` 已记「14 个受版本控制的文件含
U+FFFD」；NUL 字节是**更硬的一种**：U+FFFD 只是让字符看起来不对，NUL 让**工具静默降级**。

**登记表（全仓库扫描，`git ls-files` 858 个路径）。**

| 类别 | 文件 | NUL 数 | 处置 |
|---|---|---|---|
| C 源码 | `src/mod/gui_mod.c` | 5 | **本轮修**（注释内） |
| C 源码 | `src/lexer/lexer.c` | 2 | **本轮修**（注释内） |
| C 源码 | `src/lexer/lexer.h` | 1 | **本轮修**（注释内） |
| 引擎字节码 | `projects/{set_test,set_comp_test,exc_test,tt4,tt7,tt6,t1}.inim`、`vtest/params_precompiled_v06.inim` | 236–2779 | **不动**：`.inim` 是序列化后的程序，本来就是二进制 |
| 二进制资产 | `icon.ico`、`Infiverse_standard/src-tauri/icons/*`、`examples/assets/monster8.bmp`、`selfhost/tests/*.bmp` | 23–8084 | **不动**：图像格式 |

**闸门。** 新增第 **11** 个阶段 `text-integrity`（`tools/check_text_integrity.py`）：
`git ls-files` 枚举受版本控制的路径，按**扩展名白名单**（`.c .h .md .im .py .sh .json .yml
.cmake .iss .ts .js .css .html .xml` 等）挑出文本文件，逐个断言无 NUL 字节。
白名单而非黑名单：新加的二进制产物**不可能**把闸门判红，也就不需要有人去改名单。
没有 git 时**响亮退出**（照 `check_links.py`／`check_doc_paths.py` 的成例，不留 `os.walk` 回退）。
实测 `770 text file(s), 0 with NUL bytes`（本阶段建立时是 **769**；多出的 1 个就是
`tools/check_text_integrity.py` 自己 —— 它被提交之后才进入 `git ls-files`。**这个数会随每个
新增文本文件变动，所以它不作断言**：本阶段断言的是「0 个含 NUL」，数量只作输出）；
**反向验证**：往 `src/lexer/lexer.h` 塞回一个 NUL ⇒ `1 NUL byte(s) in 1 of 770 tracked text
file(s)`、报出字节偏移与行号、`exit 1`，还原即复绿。

**诚实边界。**

1. 我核的是 **C 源码**这三个文件；`.inim` 与图像里的 NUL 是**格式本身**，我**没有**逐个
   验证它们是否都合规，只是按扩展名排除。
2. 白名单是**我的判断**：一个受版本控制的**新**文本扩展名（比如 `.vue`）若含 NUL，
   本阶段**看不见**它。名单在 `tools/check_text_integrity.py` 的 `TEXT_SUFFIXES` 里，
   加一项是一行的事。
3. 我**没有**证明这 8 个字节是「某次编辑事故」还是「某工具写入的」——`git log -S` 对
   NUL 字节不可用（git 自己把它当二进制），所以**来源未知**。修的是后果，不是原因。
4. 「`grep` 会截断」这条是**本机 GNU grep** 的实测；`ripgrep`、编辑器内搜索、`git grep`
   的行为**没有逐个实测**（`git grep` 在实测里同样只给了 `:1661` 一行）。
5. 这一阶段**不是**在断言「仓库里没有二进制文件」，它只断言「被我们当成文本的那些文件
   里没有 NUL」。

## §1.56 关于实参类型的第一条通用规则：D14 落地，以及 POSIX 侧「两个决定点折成一个返回值」

**形状**：内建函数收到**无法解释为所要求类型**的实参时怎么办？写 D13 的时候，仓库里
**没有任何**这样的规定 —— D13 自己的正文就写着「设计记录里没有任何『参数类型不对时怎么办』的
规定」，于是两侧都只能各自挑一个答案。原子家族挑了「答 0」：名字不是字符串时，Windows 三处
`if (!nm) { push_int(vm, 0); return 1; }`（`src/runtime/runtime.c` 的 `builtin_atomic_add`／
`builtin_atomic_get`／`builtin_atomic_set`），POSIX 三处把 `name.type == VAL_STRING ? name.sval :
NULL` 交给 `posix_atomic_find`，而后者首行是 `if (!name) return -1;`。

**这不是「少一个守卫」，是两个决定点被折成了一个返回值。** `posix_atomic_find` 用同一个 `-1`
回答了两个不同的问题：

- 「你给我的**实参**不是字符串」—— 调用方错误，这次调用**从来就不可能成功**；
- 「这个名字**不存在**」—— 运行期状态，那里本来就没有东西。

调用方拿回 `-1` 之后再也分不开这两件事，于是只能给一个答案（`0`），而这个答案对第一种情形是
**静默失败**：调用方无从区分「成功了」与「什么都没发生」。这与 §1.54 是**同一种病的两种形态** ——
一个是「门不检查」，一个是「两个问题共用一个答案」。

**裁定（2026-10，人类，D14）**：按**这次调用是否操作共享状态**分两支。操作共享状态 ⇒ 抛
`type_mismatch`，不得静默返回；只产出值 ⇒ 按各自已裁定的定义值作答（`chr(non-int)` 答 `""`、
`int("0x10")` 答 `16`）。**运行期状态**的类型不符（槽里存的不是 `VAL_INT`）**明确排除**在外，
仍答 `0` 且**不得改动那份状态**。完整条款见 [`docs/SYNTAX.md`](SYNTAX.md) §7.1 D14。

**改动（六处）**：Windows 三处改为 `vm_throw_kind(vm, "type_mismatch")`；POSIX 三处在
**调用 `find` 之前**加 `if (name.type != VAL_STRING)` 守卫（放在 `find` 之后就晚了 —— 这正是上面
那条「折成一个返回值」的直接后果）。`chr`／`int`／`round` **一个字都没动**。

**实测**（Linux，`./build/inimerse --no-mods`）：

| 调用 | 改后 | 改前 |
|---|---|---|
| `atomic_get(42)` | `[exception] uncaught: type_mismatch`，rc=1 | 答 `0`，rc=0 |
| `atomic_add(42, 1)` | 同上 | 答 `0`，rc=0 |
| `atomic_set(42, 1)` | 同上 | 答 `0`，rc=0 |
| `atomic_get("no_such_slot_anywhere")` | 答 `0`，rc=0 | 答 `0`，rc=0 |
| `y = 1.5; atomic_get("y")` | 答 `0`，`y` 仍是 `1.5` | 答 `0`，`y` 仍是 `1.5` |
| `h = 2.5; atomic_add("h", 0)` | 答 `0`，`h` 仍是 `2.5` | 答 `0`，`h` 仍是 `2.5` |

**pin 断言的是错误种类串，不是退出码。** `vtest/atomic_slot_type_contract_v06.im` 用三段
`try { … } catch (e) { n = str(e) }` 拿到种类串，末行期望

```
… r4=12 n1=type_mismatch n2=type_mismatch n3=type_mismatch n4=0
```

`n4` 是「名字不存在」那一支，**必须仍是 `0`**：只断言「抛了东西」的 pin 会在 `numeric_overflow`、
索引错误或任何将来的种类上照样通过，而且分不出这两支 —— **两条不能同时被区分出来就等于没落地**。
`CMakeLists.txt` 的 PASS 正则已扩、并新增
`FAIL_REGULAR_EXPRESSION "n1=NO-THROW|n2=NO-THROW|n3=NO-THROW|n4=type_mismatch"`，**双向验过**
（修前输出命中、修后不命中）。

**A/B（Linux）**：`git stash push -- src/runtime/runtime_posix.c` 重编 ⇒ fixture 打出
`n1=NO-THROW n2=NO-THROW n3=NO-THROW n4=0` 且**退出码仍是 0** —— 这正是这条缺陷的形状，也说明
一个只看退出码的 pin 会**绿着**放它过去；恢复后打出上表。

**诚实边界**：

- **这是关于参数类型的第一条通用规则，而「操作共享状态 ⇒ 抛／只产出值 ⇒ 答定义值」这个切法本身
  还没有被正式确认。** 人类明确要求**只落窄条款**：只有原子家族的名字参数改了行为。所以本节的
  结论**不能**外推成「引擎开始校验参数类型了」—— D3／D4／D5 说明引擎整体仍不校验类型与 arity。
- **运行期状态仍是答 `0`**，这是本条的**排除项**，不是遗漏。`atomic_set` 的槽类型归一化（写
  `VAL_INT`）也**没动**。
- **`docs/SYNTAX.md` §7.1 D13 的两段散文已同步**：它原来写「设计记录里没有任何这样的规定」与
  「需要先有一条规范」，现在这条规范存在了；`chr` 答 `""` 不变，但「没有规定」这句话不再成立。
- **没有做静态证明**：`type_mismatch` 之外没有别的种类被排除，判据是 134/134 与 fuzz 阶段全绿。
- Windows 侧**只做了编译与代码等价性核对**（三处文本与 POSIX 侧成对改动），本轮**没有**在真
  Windows 上跑 `atomic_get(42)` 的 A/B。

## §1.57 `--only` 打错一个阶段名：跑了个空，却打印绿灯

**形状**：`tools/gate.sh` 的 `run_stage` 在 `--only` 与当前阶段不匹配时把该阶段记为 `SKIP` 并
`return 0`。于是 `--only` 给了**任何**不存在的名字，**每一个**阶段都被跳过，而收尾那行仍然是

```
gate: OK — every stage passed.
```

**退出码 0**。实测：

```
$ tools/gate.sh --only definitely-not-a-stage
build … SKIP --only definitely-not-a-stage
…（11 个阶段全部 SKIP）…
gate: OK — every stage passed.
$ echo $?
0
```

表格里确实每一行都写了 `SKIP`，但**收尾句与一次全绿的门禁逐字相同** —— 只看最后一行（或只看退出
码）的人拿到的是一次「什么都没跑」的绿。这是本仓库已经写下来的规则「**一个 skip 不是一次 pass**」
（`tools/gate.sh` 顶部注释、`stage_ctest` 单独数 `***Skipped`）的下一层：跳过**全部**反而是绿的。
在 CI 里把阶段名写错（例如在新阶段尚未合入时先写上去）会**静默**什么都不跑并且绿。

**修法**（`tools/gate.sh`）：

- `run_stage` 把每个选择器记进新数组 `STAGE_WANTED`（不论是否被 `--only` 选中）；
- 所有阶段注册完之后校验 `--only`：没有匹配 ⇒ 报错、**exit 2**，并列出合法值；
- 合法值**从 `STAGE_WANTED` 派生**，不是第二份手写清单 —— 手写清单没有消费者，它只约束写它的
  那一刻（`EXP_CTEST` 的 130、M13 的 101、`API_BUILTIN_TABLE.md` 的 59，加上这条）。
- 收尾句不再在部分运行上读起来像整场门禁：

```
gate: OK — the selected stage passed (--only links).
gate: this was NOT the full gate: 11 stages are registered and only this one ran.
```

  全量运行则报「11/11 stages ran」。

**实测**：`--only definitely-not-a-stage` ⇒ exit **2** + 三行 stderr（含合法值列表）；
`--only links` ⇒ 跑 1 个阶段、收尾句自报「这不是完整门禁」。

**为什么与 §1.54、§1.56 登记在一起**：三条是同一个缺陷类在三个层面上的样子 ——
**一道不能拒绝的门**（数组池 `vm_pool_slot`）、**两个问题被折成一个返回值**
（`posix_atomic_find` 的 `-1`）、**一句描述了一次它并没有执行的运行的总结**。

**诚实边界**：① 这份发现来自 `ivory-ember`（它在自己的 `stream/ci-gate-static` 分支上也修了同一处），
我独立复现了「打错名字 ⇒ 绿 + exit 0」这一条；② 没有穷举 `--only` 的其他畸形输入（空串、
大小写、前后空格）；③ 阶段名列表是**派生**的，但派生自 `run_stage` 的**调用点**，如果有人绕过
`run_stage` 直接跑一个函数，那个函数不会出现在列表里。

## §1.58 一个内建名字有两个注册点，而注册表自己不会说

`vm_register_builtin`（`src/vm/vm.c:1675`）把名字追加进 `vm->builtins[]` 后调用
`builtin_insert`（`:1667-1673`）：线性探测，**取第一个空槽，从不检查这个名字是否已经在表里**。
查找侧 `builtin_lookup`（`:1657-1666`）从 `builtin_hash_fn(name)` 出发探测，
**返回探测链上第一个名字匹配的槽**。

两条合起来的意思是：**同一个名字注册两次时，先注册的那个占住探测链上更早的槽，
于是永远胜出；后注册的那个拿到自己的槽，但 `builtin_lookup` 永远走不到它。**
它既不会被拒绝、也不会被报告 —— `builtinCount` 照样把它算进去，
所以「表里有几个内建」和「能调用几个内建」是两个数，而**没有任何东西比较过这两个数**。

### 普查

对 `src/` 全部 `vm_register_builtin(_full)?(vm, "…")` 提取：**588 个注册点、462 个唯一名字**。
跨文件的重名有 126 个，但绝大多数是**平台分叉**（`CMakeLists.txt:427-443`：
`src/runtime/runtime.c` 在 `if(WIN32)` 里、`src/runtime/runtime_posix.c` 在 `else()` 里，
二者互斥）—— 同一个二进制里只看得见其中一个，所以不是重名。

**同一个文件内的重名只有两处**：

| 位置 | 名字 | 判定 |
|---|---|---|
| `src/isolate_mod.c:227` 与 `:318` | `isolate_run` | **误报**：两处分别在 `#ifdef _WIN32` 与 `#else` 分支里 |
| `src/mod/gui_mod.c:3690` 与 `:3697` | `gui_fullscreen` | **真重名**：两行在同一个 `gui_mod_register` 里相隔 7 行，**中间没有预处理条件** |

⇒ 今天仓库里**唯一一个**「一个名字、两个注册点」的实例就是 §1.53 登记的那一个。
（`gui_mod.c` 只在 `if(WIN32)` 里参与编译 —— `CMakeLists.txt:431` —— 所以这条**只在 Windows 上可达**，
本机 Linux 引擎根本不含这个文件。）

### 修法：让损失出声，而不是让注册表保持沉默

`src/vm/vm.c` 的两个注册函数各加一段守卫（`vm_register_builtin:1684`、`vm_register_builtin_full:1708`）：

```c
if (builtin_lookup(vm, name) >= 0) {
    fprintf(stderr, "[vm] builtin '%s' is already registered; the first one stays\n", name);
    return;
}
```

**这不改变任何分派行为**：`builtin_lookup` 本来就返回先注册的那个，所以第二个注册点
**今天已经是不可达的**；守卫做的事只是「不再给它造一个槽」并且**把这件事说出来**。
区别在于：从前它是**一条读起来像活代码的死代码**，现在它是**一行 stderr**。

### A/B：守卫有牙

把 `src/runtime/runtime_posix.c:1117` 的 `vm_register_builtin(vm, "random", posix_random);`
**故意复制成两行**、重编、跑任意脚本：

```
[vm] builtin 'random' is already registered; the first one stays
```

恢复后该行不再出现。**Linux 侧零假阳性**：当前 POSIX 源列表里 `0` 个重名
（与 §1.53 那次集合比对一致），所以守卫在 Linux 上是**无操作** —— 这一点是实测的，
不是推的。

### 闸门断言：`stage_ctest` 里的 `is already registered`

守卫把损失变成一行 stderr，但**没有任何测试会因为它失败**。
`tools/gate.sh` 的 `stage_ctest`（该函数在 `4ca013d` 上占 `:148-236`；这条断言就是 `:168` 的 `tools/check_test_ports.py` 调用）新增一条断言：
ctest 的输出里出现 `is already registered` ⇒ **阶段红**，并把那一行原样打出来。

**为什么断言放在这里**：它是**这一阶段刚跑的那些套件**的性质，而**没有任何单个测试文件看得见它** ——
重名发生在 VM 初始化期，早于任何 `.im` 断言的第一行。
这条与 `stage_ctest` 已有的另外两条断言同族：`0 tests failed out of $EXP_CTEST`（数量）
与 `***Skipped`（跳过）。三条都在回答同一个问题：**「跑了的那些」与「本该跑的那些」是不是同一批。**

### §1.53 那个实例的处置：删掉重名，**不**替人做选择

§1.53 把 `gui_fullscreen` 登记为**只登记、不动代码**，理由逐字是
「两个体行为不同，选哪个是人的决定」。本轮**删掉的是不可达的那一行注册**，
**没有删掉另一个实现体**：

- `src/mod/gui_mod.c:3690` 的注册保持不动（它本来就是胜出的那个）；
- `:3697` 那一行**删除**；
- `builtin_fullscreen` 的**函数体保留**（`src/mod/gui_mod.c:1682`），
  并在注册处用一行 `(void)builtin_fullscreen;` 引用它，使它在 `-Wunused-function` 下仍然干净；
- 紧邻的注释写明：**两个体不一样**（保留下来的那个要求实参、用 `SetWindowLongA`；
  保留但不注册的那个缺省是切换、用 `SetWindowLongPtr` 加保存的 restore style），
  **「该注册哪一个」不在这里决定**，`docs/AUDIT.md` §1.53 记着它是一个人的决定。

⇒ **行为逐位不变**（被删的那行今天已经不可达），**人的选择仍然摆在同一处、只差一行注册**。

### 诚实边界

1. **`src/mod/gui_mod.c` 的改动本机无法编译验证**：该文件只在 `if(WIN32)` 里参与编译
   （`CMakeLists.txt:431`），Linux 引擎不含它。`(void)builtin_fullscreen;` 的合法性与
   `-Wunused-function` 的干净性**必须在 Windows 上核**。
2. **守卫只在「第二个注册点存在」时出声**。它不检查 `builtinCount` 与「可调用名字数」是否一致，
   也不扫描源文件 —— 那需要一个解析预处理条件的提取器，而 §1.53 那次比对已经证明
   不带条件的文本扫描会产生误报（`isolate_run`）。
3. **`builtin_insert` 仍然不检查重名**。守卫在调用方，不在表里；
   直接调 `builtin_insert` 的路径（今天只有这两个注册函数）绕过它就绕过了检查。
4. **`gui_fullscreen` 的两个体行为不同这件事我没有实测过**（需要窗口），
   是从两份实现逐行读出来的，与 §1.53 的读法一致。
5. 普查的 588/462 是**文本提取**，不含运行时构造的名字（若有）。

## §1.59 一份表格声称有 CTest 覆盖，而那两个 fixture 从来没有注册过

**症状。** `docs/API.md` 第 2.1 节的证据列标题是「证据（CTest）」，而 `:114` 逐字写着：

| `case value { _: ... }` 默认分支 | 已实现 | `lint_case_missing_default_v04.im` 相关 CTest |

`:115` 把 `lint_case_exhaustive_v04.im` 与**四个真实的 CTest 名字**并列在同一个「证据（CTest）」格子里。两处都不成立：

| 声称 | 实测 |
| --- | --- |
| `lint_case_missing_default_v04.im` 有「相关 CTest」 | 全仓库对该文件名的引用**只有这一行文档**；`ctest -N` 的 134 个名字里没有它，也没有任何名字含 `missing_default` |
| `lint_case_exhaustive_v04.im` 是一个 CTest | 它是**文件名**，不是测试名；全仓库引用同样只有这一行；四个并列的名字（`lint_case_enum_runtime`／`lint_case_membership_runtime`／`lint_case_try_members_runtime`／`lint_case_try_alias_runtime`）**都是真的** |
| `docs/REQUIREMENTS_ANALYSIS.md:177` 的「`migrate_report.py` + 对应 CTest」 | `tools/` 下**没有** `migrate_report.test.py`；`ctest -N` 里没有任何名字含 `migrate` |
| `docs/STATUS.md:286` [obs: ab70a71 ≡ main] 把 `tools/migrate_report.py` 与两个 CTest 并列 | `bindgen_regression` 跑的是 `tools/bindgen.test.py`，`scan_tools_regression` 跑的是 `tools/scan_tools.test.py`（其 docstring 只提 `cpp_scan` 与 `python_scan`）—— 两个都不碰 `migrate_report.py` |

**能力是真的，覆盖不是。** 两个 fixture 今天都跑得对：

```
$ ./build/inimerse --lint vtest/lint_case_missing_default_v04.im
[lint] line 1 [WARN] case has no wildcard '_'/'else' branch; exhaustive coverage cannot be proven for open or infinite sets
$ ./build/inimerse --lint vtest/lint_case_exhaustive_v04.im
[lint] line 3 [WARN] case branch is unreachable: wildcard '_'/'else' appears before this branch
```

两者 rc 均为 1。`tools/migrate_report.py` 也跑得对（`--help` rc=0；对 `tools/cpp_scan.py` 生成真报告、rc=0）。所以这不是「文档描述了不存在的东西」，而是**「文档描述了一个从未接上的东西」** —— fixture 写好了、诊断是对的、表格把它当证据引用了，**而注册那一行从来没有被加过**。

**为什么没有人发现。** `CMakeLists.txt:756-766` 的五个 `lint_case_*` 测试是**手写列举**的（`:767` 的注释自己数着「The five lint_case_* tests above」），于是第六个和第七个 fixture 落地时没有任何东西会要求把它们加进去。仓库里**没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**。

**普查。** 67 个 `vtest/*.im` 中 **5 个**在 `CMakeLists.txt` 里一次都没被提到：

| fixture | 判定 |
| --- | --- |
| `lint_case_missing_default_v04.im` | **真缺口** —— 文档声称有覆盖 |
| `lint_case_exhaustive_v04.im` | **真缺口** —— 文档声称有覆盖 |
| `params_precompiled_v06.im` | 合法：它是 `vtest/params_precompiled_v06.inim` 的**源**，而 `params_precompiled_runtime`（`CMakeLists.txt:1153`）跑的是那份序列化产物 |
| `eidos_object_probe_v04.im` | 合法：`docs/API.md:201` 引用的**样例脚本**；功能由 `tools/eidos_runtime.test.py` 自带的内联脚本断言 |
| `say_pair_probe_v06.im` | 合法：一次性探针，输出逐字记在 `docs/AUDIT.md:1811-1812`（原文这里写的 `:1807` 是陈旧号），本来就不是回归输入 |

另外两组也查了，**今天都是 0 孤儿**：30 个 `tools/*.test.py` 全部被 `CMakeLists.txt` 提到；13 个 `tools/*.test.js` 全部被 `CMakeLists.txt` 或 `tools/node_suites/run_all.js` 提到。

**修法分两半 —— 把声称变成真的，以及让下一个缺口自己出现。**

**(1) 注册那两个 fixture**（`CMakeLists.txt` 末尾，**注册在最后以免任何既有的 `#N` 位移**）：`lint_case_missing_default_runtime`（**#135**）与 `lint_case_exhaustive_runtime`（**#136**），形状与既有的五个 `lint_case_*` 一致，各带 `PASS_REGULAR_EXPRESSION`。**并且各带一条 `FAIL_REGULAR_EXPRESSION` 断言对方那条警告不出现**：

```
lint_case_missing_default_runtime: PASS "case has no wildcard"       FAIL "case branch is unreachable"
lint_case_exhaustive_runtime:      PASS "case branch is unreachable: wildcard"  FAIL "case has no wildcard"
```

理由是这两个 fixture 只差一行、且都在同一个 `--lint` 通道上：**只断言自己那条发现，分不开「诊断因正确的理由触发」与「诊断对每个 case 都触发」。** 实测两个方向都成立（各自输出里对方那条计数为 0）。

**(2) 新增 `tools/check_orphan_fixtures.py` 与门禁第 12 阶段** `orphan-fixtures`。它比较「测试输入集合」与「真正会跑的集合」，三组都查；不在 `CMakeLists.txt` 里的必须出现在脚本的 `ALLOWED` 里，**且每条都要写出「那跑的是什么」**（上面表格里三条合法项的理由逐字在内）。一条只列文件名的白名单，就是同一个缺陷上升一层。

```
$ python3 tools/check_orphan_fixtures.py
check_orphan_fixtures: 110 input(s) checked (67 vtest fixtures, 30 python harnesses, 13 node harnesses); 64 fixtures registered, 3 allowed with a stated reason.
```

**A/B（脚本有牙）。** 把 `CMakeLists.txt` 退回 `HEAD`（即没有那两条注册）：

```
check_orphan_fixtures: 2 orphaned input(s) out of 110 checked:
  vtest/lint_case_exhaustive_v04.im: not named in CMakeLists.txt and not in ALLOWED. ...
  vtest/lint_case_missing_default_v04.im: not named in CMakeLists.txt and not in ALLOWED. ...
check_orphan_fixtures: an input nothing runs cannot fail, and cannot pass either -- it is not evidence.
RC=1
```

恢复后 rc=0。**它点名的正好是文档声称有覆盖的那两个。**

**A/B 抓出了脚本自己的缺陷（值得单独记）。** 第一次跑这个 A/B 时脚本**崩了**：

```
NameError: name 'CMAKE_SOURCE_DIR' is not defined
```

因为提示串是 f-string，`${CMAKE_SOURCE_DIR}` 里的花括号被当成替换字段。⇒ **一个「能发现孤儿」的检查在真发现孤儿时会抛异常而不是报告** —— 它存在、它退出非零、它答的是另一个问题。这与本仓库一直在治的形状逐字同形，只不过这次在检查器自己身上；已改成 `${{CMAKE_SOURCE_DIR}}`。**这条是 A/B 唯一的产出，没有 A/B 就不会有人发现。**

**诚实边界。**
1. **「在 `CMakeLists.txt` 里被提到」是子串测试，不是解析。** 一个只在注释里被提到的 fixture 会通过。这是刻意的：替代方案是写一个 CMake 解析器，而一条提到 fixture 的注释至少是一个能被找到的读者。
2. **这一阶段不判断被注册的测试是否断言了任何东西。** 一条既无 `PASS_REGULAR_EXPRESSION` 也无 `FAIL_REGULAR_EXPRESSION` 的注册可以靠任何退出码通过 —— 见 `docs/SYNTAX.md` §7.2（M13）。
3. **只查 `vtest/*.im`**，不查 `vtest/*.params` / `*.inim` / `*.txt`。那些是被注册的测试的输入而不是测试，且 `params_precompiled_v06.inim` 由跑它的那条注册点名。
4. **`tools/migrate_report.py` 当时仍然没有 CTest** —— 本轮只更正了那两处文档声称（并把它记成「部分」），**没有**为它写测试，因为 `tools/*.test.py` 属于对等方的改动域。它是本阶段唯一「已知且被记录」的覆盖缺口：`check_orphan_fixtures.py` 查的是 `vtest/*.im` 与 `*.test.py`，**一个既非 fixture 又无 `X.test.py` 的工具落在两组之外**（刻意的窄口，写在脚本的 docstring 里）。**该缺口已补**：`tools/migrate_report.test.py` + CTest `migrate_report_runtime`，见 §1.60 末节。
5. **本轮计数 134 → 136**（新增两条 CTest），`tools/gate.sh` 的 `EXP_CTEST`、`docs/BOARD.md` §3、`docs/STATUS.md` §2/§2.1 四处同步。


## §1.60 同一个修复贴着两份拷贝：一份修了、一份没修，而第三份差点被造出来

**症状（流程缺陷，不是引擎缺陷）。** `.github/workflows/` 里有两份逐字相同的 CTest 循环 ——
`release.yml` 与 `linux-build.yml` 各一份。`release.yml` 那份被修过：抽取模式从
`s/^[[:space:]]*Test #[0-9][0-9]*:/` 改成 `Test[[:space:]]*#`，并加上了
`collected == Total Tests` 的计数断言。**`linux-build.yml` 那份没有**，而两份都绿。

**机制（比「写错的 sed」有用）。** `ctest -N` **右对齐**测试编号：

```
Test   #9: ed25519_probe
Test  #64: range_meta_runtime
Test #100: function_thread_lifetime_runtime
```

所以旧模式**只在 `Total Tests <= 99` 时成立**，在**第 100 个测试被加进来那一刻静默降级** ——
而仓库里没有任何东西记录过那个阈值。实测（本树 137 个测试）：
旧模式收 **38** 个（`#100`–`#137`），新模式收 **137** 个。空格普查（134 个测试时）：
`Test #` 35 个、`Test  #` 90 个、`Test   #` 9 个，合计 134。

**两个后果。** ①`linux-build.yml:51` 的 `if [ "$test_name" = "range_meta_runtime" ]` 诊断块
**从未执行** —— 那个名字（两位数编号）从来不在名单里。②`:43-44` 的注释
`http_probe is covered by local CTest; hosted runners intermittently fail its threaded loopback
scheduling and must not block packages.` 是又一次「**本地 CTest 覆盖了它，而本地 CTest 不在 CI 里**」——
实测 `http_probe` **是**一个已注册的 CTest（`ctest -N | grep -c http_probe` = 1），所以它确实会被那个
循环跑到；注释描述的意图是对的，而**执行它的循环当时只跑了 38/137**。

**修法（`vivid-anchor`，`12ff42a`）。** 新增 `tools/ctest_enumerate.sh`：`ctest -N` → 新模式抽取 →
读回 `Total Tests:` → `collected != registered` 即 `exit 1`，`ctest -N` 失败或零个名字也 `exit 1`，
否则逐行打印名字。两份工作流都改成**先落文件再 `mapfile`**：

```sh
bash tools/ctest_enumerate.sh build > build/ctest-names.txt
mapfile -t TESTS < build/ctest-names.txt
```

**这个 `mapfile` 写法本身是一个缺陷**：`mapfile -t TESTS < <(cmd)` 是进程替换，
**`mapfile` 的退出码是它自己的，不是 `cmd` 的** —— 枚举脚本的 `exit 1` 会被丢掉，
循环照旧在一个空名单上跑完并报绿。先落文件再判 `rc` 是必要的，不是风格。
`release.yml` 里原来那段内联 `REGISTERED` 换成同一个调用；`linux-build.yml` 拿到了它从来没有的断言。
A/B：本树 137 个名字 `rc=0`；把模式退回旧式 ⇒ `collected 38 of 137 registered tests from build`、`rc=1`。

**第三份拷贝差点被造出来（本轮的真实教训）。** 我在**不知道** `12ff42a` 存在的情况下，独立写了
`tools/ci_ctest.sh`（抽取 + 计数断言 + `***Skipped` 断言 + 逐测试循环）并把两份工作流改成调用它，
建在一个本地 `git merge origin/main`（merge-base `6f0f7bd`，未含 `12ff42a`）之上。
收到 `vivid-anchor` 的来信后核对远端，发现同一个修复已经落地 —— 于是**放弃本地 merge、删掉
`tools/ci_ctest.sh`、把两份工作流恢复成它那一版**，只保留真正独有的部分（本节的 `migrate_report`
harness）。**这不是一次「重复劳动」的遗憾，是这个缺陷在治它的时候又发作了一次**：
两条分支各自独立修同一个 bug，正是本节要记录的形状本身。
判据是**可复核的**：`git merge-base --is-ancestor 12ff42a HEAD` 与
`git log --oneline origin/main | head` 在看远端之前都答不出「谁已经在修」。
⇒ 这条与 §1.59 的 `orphan-fixtures` 是**同一个动作**：**在动手之前先比较两个集合**
（这里是「我打算改的文件」与「远端已经改过的文件」），而不是事后。

**CMake 自己会拒绝重名（实测，这是好消息）。** 三行隔离工程里两个 `add_test(NAME same_name …)` ⇒
`CONFIGURE_RC=1`、`Configuring incomplete, errors occurred!`、`ctest -N` 报 `Total Tests: 0`。
**所以同一个测试名不会静默进入任何一棵树** —— 它需要的是**一个决定**，不是一个守卫。
合并时两侧都注册了指向同一个 fixture 的用例（`main` 的 `lint_case_wildcard_unreachable_runtime`
与本分支的 `lint_case_exhaustive_runtime`），收口**保留了本分支那套按 fixture 命名的名字**，
并保留 `main` 更严的 `PASS` 串与双向 `FAIL` 正则；最终 **137 个 `add_test(`、0 重名**。

**最后补上的实例：`tools/migrate_report.py`。** 该工具是 §1.59 里「既非 fixture 又无
`X.test.py`」那个刻意的窄口，也是文档声称有 CTest 覆盖而实际没有的第四处。
新增 `tools/migrate_report.test.py`（CTest `migrate_report_runtime`）断言的是**分母**：

- 两个 fixture 覆盖 `PYTHON_RULES` / `C_RULES` 里的**每一条规则**（`goto`/`setjmp`/`alloca`/
  `threads`/`func-ptr` 与 `dynamic-attr`/`metaclass`/`yield`/`async`/`varargs`/`decorator`/
  `global-stmt`/`exec-eval`/`lambda`）；
- 表头的 `Scanned:` / `Manual adaptation points:` / `Dependencies:` **等于表体实际行数** ——
  把 `f"- Manual adaptation points: {len(all_findings)}"` 改成常量 `0` ⇒ **红**
  （`says 0 but the table has 15 row(s)`），这正是「一个没有分母的结论」那一族；
- **负对照**：干净输入必须报 `0` 且表体为空 —— 没有它，一个「把什么都报成发现」的工具会通过上面两条；
- 找不到源文件必须**非零退出并点名原因**（`error: no source files found`），而不是打一份看起来干净的报表。

**两个方向都实测有牙**：删掉 `lambda` 规则 ⇒ `rule 'lambda' did not fire`、rc=1；把表头计数写成 `0`
⇒ 两条 FAIL、rc=1；恢复 ⇒ `migrate_report tests: ok`、rc=0。

**计数。** `grep -c 'add_test('` **134 → 136（本分支）→ 137（合并 `main` 后）→ 138**；
`tools/gate.sh` 的 `EXP_CTEST` 那一行、`docs/BOARD.md` §3、`docs/STATUS.md` §2/§2.1 四处同步。
`check_orphan_fixtures` 的输入数 **110 → 112**（`tools/desugar.test.py` + `tools/migrate_report.test.py`；
`tools/desugar_probe.sh` 被删除）。`tools/README.md` 的门禁表原先写「Seven stages」、只列 7 个、
`ctest` 期望 `93 / 93` —— 已由 `vivid-anchor` 改成十二阶段与当前数字；**这是同一形状的第五次**
（一份手写列举，没有任何东西比较它与 `STAGE_WANTED`）。

**诚实边界。** ①本轮**没有**在 Windows 上复核任何东西，上面全部是 Linux 实测；
②`12ff42a` 的 `tools/ctest_enumerate.sh` 与 `741191b` 的合并**是别人的工作**，本节只记录合并后的状态，
不复述它们的完整证据；③`ctest_enumerate.sh` 的断言只证明「抽取数与 `ctest -N` 一致」，
**不证明抽对了测试**（名字含正则元字符时 `-R "^${name}$"` 仍会误配；本树 137 个名字元字符计数为 0）；
④`linux-build.yml` 仍然**没有 `***Skipped` 断言**（`ivory-ember` 在 `stream/ci-gate-static` 上加了，
未合入）—— 「跳过不是通过」这条在 CI 上仍然只有本地 `tools/gate.sh` 守着；
⑤**「两条分支各自绿」没有守卫**：CMake 只拒绝**同一棵树内**的重名，跨分支的同一处修复
仍然只能靠人先比较再动手，本轮是靠一封来信才看见的。

## §1.61 两个 fixture 描述的程序，从来没有被解析过

### 症状

`vtest/lint_case_enum_v04.im:2` 与 `vtest/lint_case_membership_v04.im:2` 都写着：

```
dir be Direction = "N"
```

`be` 语句的初始化分隔符是 **`:`**，不是 `=`。实测直接跑：

```
$ ./build/inimerse --no-mods vtest/lint_case_enum_v04.im
Error: expected 'expression', but got '=' (type 83)      # rc=1
```

而这两个 fixture 各自的 CTest（`lint_case_enum_runtime` / `lint_case_membership_runtime`）**一直是绿的**。

### 机制：两条语法，两个分隔符

不是「文档与实现不一致」，是**两个不同语句的两个不同分隔符**：

| 语句 | 解析点 | 初始化分隔符 |
| --- | --- | --- |
| `name be <集合或表达式> [: init]` | `src/parser/parser.c:1383-1391` | `if (match(p, TOK_COLON)) stmt->beStmt.init = parse_expr(p);` —— **只有 `:`** |
| `type X = <集合或表达式>` | `src/parser/parser.c:1218-1227`（`parse_type_stmt`） | `consume(p, TOK_EQ, "'='")` —— **要求 `=`** |

`be` 的 RHS 与 `type` 的 RHS 都走 `looks_like_set_start(p) ? parse_set_literal(p) : parse_expr(p)`，**形状相同、分隔符不同**，所以写错一个字符会得到一条看起来像「表达式写错了」的消息。实测 `dir be Direction : "N"` ⇒ rc=0、打印 `N`。

### 为什么没有任何东西发现

这两个 fixture **只被 `--lint` 消费**，而 **lint 路径根本不打印解析错误**：

```
$ ./build/inimerse --lint vtest/lint_case_enum_v04.im
[lint] line 3 [WARN] finite case type 'Direction' is missing members: E, W
$ echo $?      # 1
```

同一条命令的合并输出里 **`expected '` 出现 0 次**（实测 `parseerr=0`）。CTest 的 `PASS_REGULAR_EXPRESSION "missing members: E, W"` 匹的是那条 warning，`FAIL_REGULAR_EXPRESSION "line 11"` 也成立 ⇒ **绿着，而 fixture 描述的那个程序从来没有被解析过**。这是本族里「断言了一个没有分母的结论」的又一例：分母是「这个 fixture 能不能被解析」，而没有任何地方问过。

### 普查

`vtest/*.im` 共 **67** 个，其中 **2 个**吐解析错误 —— 就是这两个。其余 65 个干净。全仓受跟踪的 `.im` 里 `be` 只有 10 处，其中**恰好 2 处**用了 `=`（即这两个 fixture），另外 8 处都是 `:` 且 rc=0。

### 修法与 A/B

把两行的 `=` 改成 `:`（各一个字符）：

| | 修前 | 修后 |
| --- | --- | --- |
| 直接跑 | `Error: expected 'expression', but got '=' (type 83)`、rc=1 | rc=0，无错误 |
| `--lint` 输出 | `line 3 [WARN] finite case type 'Direction' is missing members: E, W` | **逐字相同** |
| `--lint` 输出（membership） | `line 6` / `line 11` 两条 unreachable | **逐字相同** |

⇒ `PASS_REGULAR_EXPRESSION` 与 `FAIL_REGULAR_EXPRESSION` **一条都不需要改**，而 fixture 从「不可解析」变成「可解析」。

### 守卫：`tools/fixture_parse.test.py` + CTest `#139 fixture_parse_runtime`

扫描 `vtest/*.im`，断言**吐解析错误的 fixture 数 == 0**，并**同时打印两个数**：

```
fixture_parse: 67 fixture(s) scanned, 0 emitted a parse error
fixture_parse tests: ok
```

两层防「空扫描读作干净」：`MIN_FIXTURES = 50`（找到的 fixture 少于这个数直接失败），以及 `ALLOWED` 里每条必须写出**它是什么的证据**（今天为空；将来出现合法的负例 fixture 时，一条只列文件名的白名单就是同一个缺陷上升一层）。

**A/B（守卫有牙）**：把两行改回 `=` ⇒

```
fixture_parse: 67 fixture(s) scanned, 2 emitted a parse error
  lint_case_enum_v04.im: Error: expected 'expression', but got '=' (type 83)
  lint_case_membership_v04.im: Error: expected 'expression', but got '=' (type 83)
a fixture that cannot be parsed cannot be evidence for anything the engine does with it
```

rc=1；恢复 ⇒ rc=0。注册在 `migrate_report_runtime` 之后（末尾追加，既有 `#N` 不移），`EXP_CTEST` 138 → **139**。

### 同节附带修掉的一处：计数的第六个点

`docs/BOARD.md:54` 写的是 **`137 / 137`** 与 `0 tests failed out of 137`，而 `tools/gate.sh` 的 `EXP_CTEST` 那一行当时是 **138**。这一行恰恰是**引用 `stage_ctest` 断言文本**的那一行（「`stage_ctest` 会检查输出里确有 `0 tests failed out of N`」）⇒ 一份声称「数量是断言」的表格，自己引的数**落后一轮**。上一轮我报「四处计数已同步」时把这一处算进去了，**它实际没被改**。本轮连同 139 一起改成六处：`tools/gate.sh` 的 `EXP_CTEST` 那一行、`docs/STATUS.md` §2 的四处（`:38`/`:44`/`:51`/`:60`）、`docs/BOARD.md:54`。

### 诚实边界

1. **标记表是字面前缀**，取自 `src/parser/parser.c` 与 `src/lexer/lexer.c` 里全部解析期错误串（`expected '` 覆盖 `parse_error_expected` 的两种形状，另加 f-string / case-action / `++`·`--` / 未终止字符串四条）。**新增一种错误消息形状不会被这里认出来** —— 这是检查的边界，不是它的能力。
2. **刻意排除** `Error at line %d: task/thread definitions inside a loop are silently ineffective`（`src/parser/parser.c:1557`）：它报告的是**被接受然后被忽略**的构造，不是解析失败。
3. **超时算作没解析失败**（每个 fixture 20 s）。一个真的挂死的 fixture 不会被这里抓到。
4. **只扫 `vtest/*.im`**。仓库根目录的 `*.im`、`selfhost/**`、`examples/**`、`projects/**` 都不在范围内 —— 那 10 处 `be` 里有 8 处在这些位置，它们今天恰好都是 `:`，但**这个检查不保证它们**。
5. 「`--lint` 不打印解析错误」是**一条命令的实测**，我没有读 `--lint` 的实现去解释它为什么这样。

## §1.62 `substr` 的钳位整型溢出：`start + len` 溢出后守卫恒假

**症状。** 任何脚本都能让引擎段错误：

```im
s = "abcdefghij"
say substr(s, 5, 2147483647)
```
```
$ ./build/inimerse --no-mods /tmp/substr_ovf.im
Segmentation fault      rc=139
```

**机制。** `start` 与 `len` 都是 `int`，而 `start` 在前几行已经钳进 `[0, sl]`：

- `src/runtime/runtime_posix.c:476`：`if (len < 0) len = 0; if (start + len > sl) len = sl - start;`
- `src/runtime/runtime.c:560`：`if (start + len > (int)sl) len = (int)sl - start;`

`len` 接近 `INT_MAX` 时 `start + len` **有符号溢出**（UB），结果变负 ⇒ `> sl` 恒假 ⇒ `len` 保持 ~2^31 ⇒ `malloc((size_t)len + 1)` 约 2 GB ⇒ `memcpy(out, s + start, (size_t)len)` 从一个长度 `sl` 的堆字符串**越界读约 2 GB**。

**阈值实测（`s = "abcdefghij"`，`sl = 10`）：**

| start | len | `start + len` | 结果 |
| ---: | ---: | ---: | --- |
| 5 | 2147483640 | 2147483645 | rc=0，`fghij` |
| 5 | 2147483641 | 2147483646 | rc=0，`fghij` |
| 5 | 2147483642 | **2147483647 = INT_MAX** | rc=0，`fghij` |
| 5 | **2147483643** | **2147483648 溢出** | **rc=139 SIGSEGV** |
| 5 | 2147483647 | 溢出 | rc=139 |
| 0 | 2147483647 | 2147483647 = INT_MAX | rc=0，`abcdefghij` |

崩溃边界**恰好**是溢出边界（`2147483642` 不崩、`2147483643` 崩、`start=0` 永不崩），所以这是机制证明而不是相关性。

**修法（语义不变的钳位）。** `start` 已钳进 `[0, sl]` ⇒ `sl - start ∈ [0, sl]` **不可能溢出**，把比较换到减法那一侧：

```c
if (len < 0) len = 0; if (len > sl - start) len = sl - start;
```

**两处都换**（POSIX `src/runtime/runtime_posix.c:476`、WIN32 `src/runtime/runtime.c:560`），对**所有不溢出输入逐字相同**。另补 **WIN32 缺的 NULL 检查**：POSIX 早有 `if (!out) { free(s); push_string(vm, ""); return 1; }`，WIN32 是 `malloc` 后直接 `memcpy`，两份拷贝在 OOM 上给出两个答案。

**A/B。** CTest **`#140 substr_boundary_runtime`**（`vtest/substr_boundary_v06.im`）把三个长度放在边界两侧：`2147483642`（无溢出）与 `2147483643` / `2147483647`（溢出）。实测：修复版 **100% tests passed**；把 POSIX 那一行换回 `start + len > sl` 重建 ⇒ **`0% tests passed, 1 tests failed`**，手工跑同一条 ⇒ **rc=139**，`cmp` 恢复后复绿。

**为什么活到今天。** `docs/SYNTAX.md` §5（写下时 `:500`）把 `substr` 列在「核心高频内建（**有 vtest 覆盖的**）」里，而实际覆盖是**一条 happy path**：`vtest/posix_core_api_v04.im:14` 的 `substr(s, 2, 5) == "Hello"`（另一个提到 `substr` 的 `vtest/spi_caps_contract_v06.im:9` 是注释里的「substring」一词）。`len > INT_MAX - start` 没有任何用例靠近过 —— **分母从来没被问过**。

**诚实边界。** ①阈值表是 Linux/POSIX 实测；**WIN32 侧只有读码**（`src/runtime/runtime.c` 只在 `CMakeLists.txt:426-429` 的 `if(WIN32)` 分支被编译，Linux 上编不到），那里的溢出相同、且多一个缺 NULL 检查。②探针脚本在 `/tmp/substr_ovf.im`、`/tmp/t.im`，未入库。③修的是**溢出**，不是「超大 `len` 应当被拒绝」—— 后者是语义决定，本轮按「行为对不溢出输入逐字不变」的最小改动做。

## §1.63 数值字面量也走全套解析器：Windows 上 `socket_probe` 撞 CTest 上限

**症状（Windows/ucrt64 实测）。** `socket_probe`（`#20`）在 Windows 全量 ctest 里 `***Timeout 11.31 sec`（ctest 默认 10 s）。不是断言错、不是 flake：20 次单跑全部 rc=0，但 wall time 从 **151 ms 到 8564 ms** 长尾。逐调用计时驱动的 5 次运行（毫秒）：

```
run1  init 1.5  listen 3662.1  port_available 1478.6  connect 1542.1  port_open 2116.2  close 882.7
run2  init 1.7  listen  895.8  port_available  782.2  connect  837.4  port_open 1780.8  close 1.2
run3  init 1.5  listen  902.8  port_available    3.0  connect    7.8  port_open    4.7  close 2.6
run4  init 3.7  listen 1029.0  port_available    1.3  connect    6.2  port_open    4.1  close 1029.1
run5  init 2.1  listen 2265.2  port_available    8.3  connect    6.9  port_open   13.3  close 3.5
```

`im_socket_init` 恒为毫秒级；**慢的全是走名字解析的那几个调用**，`listen` 最重。探针一轮做 4 次解析（`im_socket_listen`、`im_socket_port_available`→`listen`、`im_socket_connect`、`im_socket_port_open`），长尾就是这 4 次的和。

**机制。** `getaddrinfo()` **接受** `"127.0.0.1"` 与 `"::1"` 并原样返回，但它是**穿过完整解析路径**才做到的，而那条路径正是慢的那条（`src/platform/socket.c:82-98` 的注释已记：解析器不可达时它等的是系统自己的重试表，单位是分钟）。

**修法。** `src/platform/socket.c` 新增 `addr_from_literal()`：`inet_pton`（WIN32 `InetPtonA`，同一个契约：1 成功 / 0 非法 / -1 出错，经 `IM_INET_PTON` 宏择一）先试 `AF_INET`、再试 `AF_INET6`，成功就直接填 `sockaddr_storage`。**两个解析入口都问同一个助手**：`resolve_addr`（`im_socket_listen` 走它）与 `resolve_addr_timed`（`im_socket_connect_timeout` 走它，且**在线程派生之前**）—— 否则「快路径」会变成 `listen` 一条规则、`connect` 另一条。空主机 / 主机名 / `AI_PASSIVE` 一律回落 `getaddrinfo`，行为不变。

**A/B（不吃时间的判定）。** 新增 LD_PRELOAD 垫片 `src/platform/getaddrinfo_log_preload.c`（拦截 `getaddrinfo`，把每次调用的 node 追加进 `$GA_LOG`）+ 探针 `src/platform/literal_resolve_probe.c`，CTest **`#141 literal_resolve_runtime`**。断言的是**调用计数而不是毫秒**，因为两态返回**同一个地址**、只有代价不同，而代价是平台相关的（Linux 上文字量两种实现都是微秒级）。实测：

- 修复版：`literal-resolve literal=0 name=1` ⇒ 文字量 **0 次** `getaddrinfo`，`localhost` **1 次**；`literal_resolve tests: ok`。
- 把 `addr_from_literal` 改成恒返回 -1 重建 ⇒ `***Failed  Required regular expression not found`，stderr `a literal host reached getaddrinfo 1 time(s); the fast path is gone`，日志里是 `getaddrinfo node=127.0.0.1`。

探针的**第二半不是装饰**：「文字量不再进解析器」这个断言，被「干脆永远不调解析器」同样满足，而那是个严重得多的缺陷；要求主机名仍然到达 `getaddrinfo` 才是「回落还在」的证据。

**诚实边界。** ①**本机 Linux 上改前/改后逐调用数字无法区分**（修复版 `listen` 2.6–6.2 ms，屏蔽快路径后 2.6–5.1 ms，其余都是 1–2 ms）—— Linux 对文字量本就几乎不花钱，**收益是 Windows 特有的**，`#141` 钉的是**机制**不是收益。②因此 `#141` 是 **POSIX-only**（同 `slowdns_preload` 的 `LD_PRELOAD` 手法），**Windows 侧对这条仍无覆盖**。③`#141` 断言的是「没有多余的解析调用」，**不证明** Windows 上那 4 次解析的耗时下降了多少 —— 那要 peer 的 Windows 驱动。④`InetPtonA` 的行为是读 Microsoft 文档的判断，**本机没有实测**。

## §1.64 POSIX 的 `match` 静默截断模式：跑的不是被写下的那个模式

### 症状

`match` 的模式超过约 2031 字节时被**静默截断**，而**跑的是截断后的模式**。被截掉的那一段如果承载了整条约束，`match` 就会对一个根本不可能匹配的模式回答 `true` —— **与正确答案同形，没有诊断**。

最小复现（对照只差长度，模式 = `"^" + N 个 a + "$zzz"`，主语 = N 个 a）：

| N | 模式字节 | 应当 | 实测 |
|---|---|---|---|
| 2028 | 2033 | `false` | `false` ✓ |
| **2030** | **2035** | `false` | **`true`** ✗ |
| 3000 | 3005 | `false` | `true` ✗ |
| 50（对照） | 55 | `false` | `false` ✓ |

`$zzz` 要求串尾之后**还要**有字面 `zzz`，所以这个模式永远不可能匹配。翻转点 N=2030 与下面读码推出的 2031 字节上限**逐字吻合** ⇒ 不是「长模式都不准」，是「恰好越界就换个答案」。

### 机制

`src/runtime/runtime_posix.c:131`（修前）：

```c
char translated[2048]; size_t j = 0; int in_class = 0;
for (size_t i = 0; pattern[i] && j + 16 < sizeof translated; i++) {
```

复制循环的上界是 `j + 16 < sizeof translated`，即**最多抄 2031 字节**；超出部分**无声丢弃**，然后拿截断后的串去 `regcomp`。被丢掉的正好是 `$zzz` —— 整条尾部约束 —— 剩下的 `^aaa…` 匹配前缀。

**这个缺陷的形状**：不是崩溃、不是报错，是**给出一个与正确答案同形的答案**。读代码的人看到那层「把 `\d` 翻译成 `[[:digit:]]`」的循环，会以为它只是翻译；而它在**输入足够长时换了一个模式**。这与 §1.62 是同族：**检查在算一个已经不等于它要防的那个量** —— 那里是钳位算 `start + len`，这里是翻译算一个装不下的缓冲区。

### 平台与基线

`char translated[2048]` **只出现在 `runtime_posix.c`**。Windows 那份 `regex_match`（`src/runtime/runtime.c:986`）用的是自己写的匹配器，**没有定长模式缓冲**（它的 `builtin_match` 只有一个 `char sbuf[512]` 装主语）⇒

- **缺陷是 POSIX-only，Windows 侧本来就是对的**；
- 按本仓既有的 `int`（§1.45，人类裁定 16 进制前缀两侧统一 16）与 `float`（§1.20，`val_as_double()` 取代裸读 union）处置惯例，**基线是没有 bug 的那一份**。

这条与 DECFY 的「并集归哪一层」是**同一类判断**：不是「选一个实现」，是「**定谁代表契约**」。

### 覆盖为什么是零，以及原因要说对

`match` 全仓只被 **1 个** vtest 文件用到（`vtest/posix_core_api_v04.im`），而 `vtest/` 里最长的模式是 **59 字节**（`"^[\\w.+-]+@[\\w-]+\\.[\\w.-]+$"`）—— **低于阈值 34 倍**。

⇒ 措辞必须是「**阈值远在任何正常用法之外，而越界时它不说话**」，**不是**「用户会踩」。缺陷是**沉默**，不是**概率**；把判据写成「长模式不准」会把这条降级成一个模糊的印象。

### 修法

按 `strlen(pattern)` **动态定尺寸**，不再有定长世界：

```c
size_t tcap = 16 * strlen(pattern) + 1;
char *translated = (char *)malloc(tcap);
if (!translated) { pop(vm); pop(vm); push_bool(vm, 0); return 1; }
size_t j = 0; int in_class = 0;
for (size_t i = 0; pattern[i] && j + 16 < tcap; i++) {
```

每处替换最多 13 字节（`\w` → `[[:alnum:]_]`），所以 16 倍是上界，`+1` 是终止符。`translated` 在 `regfree` 之后 `free`。

**分配失败 ⇒ `push_bool(vm, 0)`，不截断** —— 本仓「拒绝优先于截断」的教条：`false` 对能匹配的模式是错的，但它**从不静默地错**，而走到这一支意味着连 16 倍模式都装不下。

### 守卫与双向 A/B

`vtest/match_long_pattern_v06.im` + CTest **`#142 match_long_pattern_runtime`**。

**跨平台注册，不是 POSIX-only**：Windows 那份本来就对，只在 POSIX 上注册会让「**Windows 是对的**」这件事没有被钉住，而正是这一点让它算**缺陷**而不是**移植**。

**每个长度成对断言**：不可能模式必须 `false`，**同时**同长度的可能模式必须 `true` —— 否则一个「一律回答 false」的假修法也能通过。

- 修好 ⇒ `100% tests passed, 0 tests failed out of 1`、`Passed 0.07 sec`。
- **A/B**：把 `tcap` 还原成 `2048`（即旧的定长世界）重建 ⇒ `***Failed Required regular expression not found`，`impossible=true`（2030 与 3000 两档），**两档 `possible` 与短对照仍全对** ⇒ 翻转点与长度绑定，不是整个函数坏掉。
- 修前手测：`N=2028 false / N=2030 true / N=3000 true`；修后四档全 `impossible=false, possible=true`。

### 五条诚实边界

1. **`N=2030` 这个翻转点是我这一侧实测的**；`noble-zephyr` 已声明它只核了代码形状（`:131`、`:541`）、未独立复跑。发版复核若要引用该数字，应在合并后的树上重跑一次 `vtest/match_long_pattern_v06.im`。
2. **`match` 的两个实现（手写回溯器 vs POSIX `regcomp`）是「两种实现」而不是「有测量到的行为差」**。本轮我一度声称 Windows 缺 `\d\w\s\D\W\S` —— **错**：`re_class_char`（`src/runtime/runtime.c:846`）六个全实现，`re_match_elem` 见到 `\` 就派发给它。**那是从体量差（444 vs 1336 字符）推断的，没读码。**
3. **建议的分配上限是估计不是测量**：13 倍来自 `\w` → `[[:alnum:]_]` 那一条最长的替换；16 倍留了余量，但没有穷举全部替换。
4. **有意的行为改变**：超过 16 倍模式的输入从「静默截断得答案」变成「返回 `false`」。极端构造下 `false` 是错的答案，只是不再静默。
5. **`docs/SYNTAX.md` 对 `match` 的长度/字节/截断一个字都没有**，所以这条不是「违反已写下的契约」，是**补上一个从未写下的边界**；本档写完后建议在 `docs/SYNTAX.md` 对应处加一句「模式长度不设上限，超长不再截断」。

## §1.65 `Total Tests` 有三个数三种含义，而 `131` 曾被推导出来、却写成实测读数

**症状。** `Windows Total Tests: 131` 曾在 `release/051-final` 一侧的发版说明（该分支里的发行说明文件，第 14 行；本树无此文件）作为**实测读数**写下：`Windows \`Total Tests: 131\`，运行 122，0 失败`。它是本轮 D5 裁定的宾语，也是 `142 − 11 = 131` 这个推导的来源。

**取证。** 全库逐条数过（`grep -n 'Total Tests' docs/*.md`，17 处），结论与最初的判断相反，所以两种说法都要写下来：

- `131` 作为 **Windows 读数**的出处是 `docs/RELEASE_0.5.1.md:14`（`Windows \`Total Tests: 131\`，运行 122，**0 失败**，2 按设计跳过，9 按裁定 DISABLED`）。**本条不写「几处」**：本节原来那句「全库零命中」是「`131` 没有出处」的**代理**，而代理会被文档编辑扰动 —— 引用它的每一行都是它的一次出现，**写下这条结论这个动作本身就把「零命中」变成了假**。要枚举只能枚举**产出它的命令**（`grep -rn 'Total Tests: 131' docs/ tools/`），不枚举它的输出。
- **更正（合并进 `main` 时才发现）**：本节原写「该分支里的发行说明文件，第 14 行；**本树无此文件**」—— 那是 `ab70a71` 上的事实。`docs/RELEASE_0.5.1.md` 随语言迁移落地进了 `main`（`57ece55`），**本树现在有这个文件**，而 `:14` 正是把 `131` 当 Windows 读数写下的那一行。
- `131` 在 `EXP_CTEST` 记账里的出处是 `docs/STATUS.md:3663`（`main` @ `57ece55`；本节写这条时它在 `ab70a71` 的 `:3642`）**[obs: ab70a71 → `main` @ 57ece55]**：`计数 **131 → 132**（\`tools/gate.sh\` 的 \`EXP_CTEST\`）`。它紧接同节 `:3645`（`ab70a71` 的 `:3624`）的 `新 pin CTest **#131** \`spi_caps_contract_runtime\`` —— **同一节里两个不同含义的 `131` 挨在一起，零标记。** 前者是 `EXP_CTEST` 增量（Linux 侧记账），后者是 `ctest` 的测试序号；**两个都不是平台注册数。**
- 同一行（`docs/STATUS.md:3663`，`main` @ `57ece55`；本节写作时在 `ab70a71` 的 `:3642`**[obs: ab70a71 → `main` @ 57ece55]**）括号里的 `Windows 侧（ucrt64，CI 同款工具链，\`Total Tests: 122\`）` **才是** Windows 的实测留痕。`docs/AUDIT.md:2065` 有同句。

⇒ **`131` 不是被测量出来的，它是两个含义里更没有出处的那一个被读成了另一个含义。** 这与本档 §1.50（`spi_meta` 的能力串：唯一被写下的形状恰好是唯一两侧一致的输入）同形：**不是算错了，是一个名字承载了两个答案，而缺失的那一半正是判据。** **更正（`main @ 32771cf`）：上面那句对「推导」成立、对「数字」不成立。** `131` **是被测量出来的** —— `vivid-anchor` 在 Windows 上跑了四件套（`ctest.exe --test-dir '<导出树>' -N`；导出树由 `ab70a71` 的 `git archive` 得来、用 `grep -c 'add_test('` = **142** 钉住身份，另一棵是 `c71ea00`），**两棵树尾部逐字相同**：`  Test #131: match_long_pattern_runtime` / 空行 / `Total Tests: 131` / `--- Test# lines ---` / `131` / `--- Disabled ---` / `9`。⇒ 准确的说法是：**`131` 曾被「推导出来」却写成「实测读数」，而今天它被独立实测、并且与推导同值** —— 不成立的只是「没有出处」那半边（出处就是本节上面写的 `docs/RELEASE_0.5.1.md:14`）。**三个数现在各有一次独立读数**：**注册数 `131`**（`ctest -N` 印的 `Total Tests:` —— 注意它**不检查可执行文件是否存在**：Ninja 多配置下那行路径逐字是 `D:/inim-rel/absrc/build-n/Development/inimerse.exe.exe`，照样列全 131 条）；**汇总分母 `122`**（`out of 122`）；**实跑 `120`**（`122 − 2 skipped`）。⇒ 开头那句「`142 − 11 = 131` 这个推导的来源」应读作：**推导与实测同值，但它们是两件事** —— 一条算术与一次读数偶然一致，不使那条算术成为一次读数。

**修法。** 不做「删掉 131」，也不写任何推导数：

- `docs/STATUS.md:44` 与 `docs/BOARD.md:54` 改为**按平台并列**：`Linux Total Tests: 142` / `Windows Total Tests: 122`（`dc0624f`）。**[原文，已在 `main` 上被改写] [obs: ab70a71]**：`ab70a71` 上这两处写 `Linux Total Tests: 142`，`main` 上已改成 `148` 并各带自己的 ref ⇒ 引的是**当时的原文**，锚要按被引处**现在**的特征串取（`按平台并列`）。
- `docs/STATUS.md:51` 写明 `142` 的**身份是注册上限，不是任一平台的实跑数**，并逐条给出三个裁剪注册的**环境**条件：`INIMERSE_NODE`（`CMakeLists.txt:233`）、`INIMERSE_CLANG`（`:367`，条件为 `:368 if(INIMERSE_CLANG AND NOT WIN32)`）、`INIMERSE_PYTHON`（`:66`）。三者都不是平台条件 ⇒ **平台间的差无法只由 `CMakeLists.txt` 推出**。**[原文，已在 `main` 上被改写] [obs: ab70a71]**：这条引的 `142` 在 `main` 上已是 `148`（锚按现在取：`是注册上限`）。
- `docs/STATUS.md:38` 标题补「表内数字除注明外均为 Linux 实测」。 [obs: ab70a71 ≡ main]（锚：`表内数字除注明外均为 Linux 实测`，两棵树上都在）

**为什么这是缺陷而不是书写问题。** 那四行 `add_test`（`:381 :382 :386 :399`）的条件栈是**嵌套**的：它们同时在 `if(INIMERSE_NODE)`（`:380`）**和** `if(INIMERSE_CLANG AND NOT WIN32)`（`:368`）之下。本节的两轮取证各自只认了其中一层——一方把它们计入「Windows 必然不注册」（当平台条件），另一方把它们完全剔除（当工具条件）——**两个方向是同一个错：都把嵌套读成了单层。** 只要注册条件里混着环境与平台两类，就不能靠数 `add_test(` 推出任何一个平台会跑多少个。

**判据（今后）。** 凡写 `Total Tests: N`，**必须同时写明是注册上限还是某平台实跑数**；只写数字的用法，本档视为未完成的记账。

**口径（合并后补）。** 本节所有行号都是在 `ab70a71` 上量的；合并进 `main`（`57ece55`）时 `docs/STATUS.md` 与 `docs/AUDIT.md` 都被插入了内容，**这些行号已经漂了** —— 正文里的旧行号一律写成 `:旧 → :新` 的映射：`:3642 → :3663`、`:3624 → :3645`、`:1562 → :1583`（三处已就地更正并带上 ref）。**规矩是「凡带前缀的引用都要带上它的观测点」，不是「不许出现旧值」** —— 旧行号可以留（§1.46 那三处就留着 `docs/STATUS.md:3325` 并各带观测点），但**每一处所在行都要含一个可 `grep` 的记号 `[obs: <ref>]`**（在哪棵树上量的就写哪棵；同文于两棵树时写 `[obs: <ref> ≡ main]`）——**「段落」与「行」不是同一个量**：下面那条红绿命令是**逐行**过滤的，所以规矩也必须说「同一行」；若记号写在同段的下一行，**规矩满足而判据报 1**（假红），下一个人看不出为什么（由 ivory-ember 复核发现）——**「紧挨着」不是可判定的距离，记号才是**；**裸引用不许有**。**这条规矩有红绿命令**（本条改完后的实测就是期望值）：`grep -n 'docs/STATUS\.md:[0-9]' docs/AUDIT.md | grep -v '\[obs:' | wc -l` → 期望 **0**。（初稿只写了「每一处都要**紧挨着**写明」，**没有距离、也没有命令** —— 一条不能判红绿的规矩不是规矩，是愿望。）但**下面这条我原本写成的「可执行判据」是错的，而且绿在唯一该红的地方**：我原写「`grep -o 'docs/STATUS\.md:[0-9]*' docs/AUDIT.md | sort -u`，输出里不应出现旧值」，而 `docs/STATUS.md` 的 `:3325`（上面 §1.46 引的那一处）当时**就是**陈旧的带前缀引用，它不在我手写的旧值清单里 ⇒ 判据放它过去。**成因**：这条判据对「旧」的定义**就是那张映射表的左列** ⇒ **它只能复核我已写下的例外，不能发现漂移**；它是 §9 第 31 条 (c)（「量的是它自己」，这次量的是它自己维护的清单）的第三个投影，也是第 30 条自指形状的第三面（由 ivory-ember 的独立复核证伪并附活反例）。**判据不能建立在行号上，只能建立在内容上** —— 一条引用一条断言：`sed -n '<N>p' docs/STATUS.md | grep -q '<被引句的特征串>'`（期望 rc=0）。**这条判据还有第三个情形，而本节自己就踩了两次**：引用一段**已被就地改写的原文**时，被引句现在不在被引处 —— 本节修法里引的 `docs/STATUS.md:44` 与 `:51` 就是（`ab70a71` 上那两行写 `142`，`main` 上已改成 `148`），读者拿到 rc=1 **仍然分不清「漂了」还是「原句被改过」**，也就是又回到本段在治的那个歧义。**补法**：锚换成**被引处现在的特征串**，并显式标成 `[原文，已被 <谁> 改掉]`。**并且两种东西的分工要写清**：**观测点是诊断用的**（这条是在哪棵树上量的 ⇒ 能区分「漂了」和「本来就不对」），**内容锚是判据用的**（它对不对），**两者不能互相替代** —— 只有内容锚、没有观测点，漂移会被误判成错误（ivory-ember 本轮实际就这么错过一次，并据此提出了一个把错行号钉死的修正）；只有观测点、没有内容锚，只知道它从哪来、不知道它对不对。**本段原写「这三处已就地更正」，而实测另有两处漏改与一处指向不存在的行**（见本节正文第 4 条与下面的诚实边界 1）—— 由 ivory-ember 的独立复核发现，在本笔更正。**处方写在前言、病灶在正文，这是「处方不是执行」的又一个实例。****范围也只覆盖了本文档。** 带前缀的 `docs/STATUS.md:<N>` 引用还散在 `docs/DECFY_DESIGN.md`、`docs/BOARD.md`、`docs/STATUS.md` 自身、`docs/streams/`、`docs/HANDOFF_INFIVERSE.md`、`docs/HYGIENE.md` 里，**全部裸写、全部在本条视野之外**；重取命令 `grep -rno 'docs/STATUS\.md:[0-9]\+' docs/ tools/`。**漂移区是 `N > 488`**：`ab70a71` → `main` 在 `docs/STATUS.md:488` 之后插入了 21 行（`diff` 的 hunk 头 `488a489,509`），此后**整段 +21**，落在里面的引用都要逐条按内容重取。**我只逐条证伪了 `:3325`，其余是「未验证、且无任何东西在检查」**（这一句照引 ivory-ember 的原话）。**下面几条诚实边界里其余的 `docs/STATUS.md:NNNN` 同理，复核时请用命令重取、不要照抄行号**：`grep -n '<那句原文的特征串>' docs/STATUS.md`。这正是 H4.1 与 `docs/DECFY_DESIGN.md` 的 `[口径]` 要治的形状。**规矩本身的例外已清**：本条原写「凡带前缀的引用都要带上它的观测点」，而实测本文档有 **7 处**带前缀引用没有观测点（`:1287`→`:361`、`:2024`→`:30`、`:2527`→`:286`、`:2955`→`:44`、`:2956`→`:51`、`:2957`→`:38`、`:3235`→`:327`）⇒ **这句在它自己的文档里是假的**；已逐处补 `[obs: …]`。**分两半说才准**：「凡带前缀的引用」原句为假（7 处反例），但「**凡落在漂移区的带前缀引用**」为真（漂移区内 6 处全带观测点）。

### 三条诚实边界

1. **我最初把这条立成了「历史留痕仍在混淆」，复核后自己推翻了这个前提。** 逐条读下来，`docs/STATUS.md` 那五处 —— `main` @ `57ece55` 上是 `:1583`/`:2737`/`:2814`/`:2864`/`:2920`，本节写作时（`ab70a71`）是 `:1562`/`:2716`/`:2793`/`:2843`/`:2899`，**整段 +21**，重取命令 `grep -n 'out of 11[0-3]' docs/STATUS.md` —— **都先写注册数、再单独写 `out of N` 的实跑数**，两个概念是**分开**写的，没有混。`docs/AUDIT.md:2605`/`:2628` 是 shell 变量与 `ctest -N` 解析语境（这两处在 `main` @ `57ece55` 上仍逐字相同）。**原写的第三个指针 —— `docs/AUDIT.md` 的 `:3851` —— 不存在** —— `docs/AUDIT.md` 只有 **3365** 行（`ab70a71` 上 3362）、`docs/STATUS.md` 的 `:3851` 也是空行，而那个并列串省略了文件名 ⇒ **连它本来想指哪个文件都无法判定**。**指向不存在的行比漂移更坏：漂移指到别的东西上，它指不到任何东西** —— 而它和两个验得上的引用写在同一个并列串里，读者会以为三个都验过（由 ivory-ember 的独立复核发现，在本笔更正）。**真正「一个数两个平台」的只有一处（`docs/STATUS.md:3663`，`main` @ `57ece55`；`ab70a71` 上为 `:3642`）**[obs: ab70a71 → `main` @ 57ece55]**，而它已由 `dc0624f` 修掉。** 所以本节的成果是**判据**，不是一份待修的清单——若按原前提立账，本节自身就是一个「立了一个前提为假的条目」。
2. **那行发版说明我没有改。** 写这一条时 `docs/RELEASE_0.5.1.md` 只存在于 `release/051-final` 一侧（`ab70a71` 上 `git ls-files` 查不到该路径），所以「在该流禁触清单内」这个**豁免只在那棵树上成立**。文件已随语言迁移落进 `main`（`57ece55`）⇒ **豁免连同它的适用范围一起失效**：本树上有这个文件，`:14` 那行是否要改是一个**仍待裁定**的事项，不是已被排除的事项。
3. **Windows 的 `122` 我也没有独立复跑。** 本机是 Linux 且 `node` 在 `/home/sakiko/.nvm/…/bin/node` 存在、`clang`/`python3` 均可发现，所以本树**测不到** Windows 的注册条件组合。`122` 是从 `docs/STATUS.md:3663`（`main` @ `57ece55`；`ab70a71` 上为 `:3642`）**[obs: ab70a71 → `main` @ 57ece55]**与 `docs/AUDIT.md:2065` 两处历史留痕**读回**的，不是本轮测量。真正重算 Windows 注册数需要在 Windows 上跑一次 `ctest -N`，本节没有做。

## §1.66 取并集的解冲突策略，对「两侧都改了同一行」会写出两遍，而没有检查器看得见

**症状。** `release/051-final` 吸收 `stream/lang-migration` 时，`docs/BOARD.md` 与 `docs/STATUS.md` 的合并冲突按**取并集**解决。实测结果是两处文档里共有 **5 行被写了两遍**：`docs/BOARD.md` 的 ctest 行；`docs/STATUS.md` 的 §2 标题、全量测试行、测试注册行、示例命令行各一次。归一数字之后，那 5 行就是**同一句话各出现了两遍**。

**机制：并集对「新增」正确，对「改动」错误。** 冲突的两种形态在并集下行为不同：

- **两侧各自新增不同的行** ⇒ 并集给出的正是想要的（两条都要）。
- **两侧各自修改了同一行** ⇒ 并集把**两个版本都收进来**，于是同一个条目各进来一次。若两侧改的是同一个数字（本例正是：测试计数），两个版本归一之后字面相同，**看起来像「同一行写了两遍」而不是「两个互斥的版本」**。

⇒ 缺陷不在被改的那一行，**在解冲突的算子对两种形态给出了同一个动作**。这与 §1.10（集合字面量是并集：三处「只走一半」的枚举）同族但方向相反：§1.10 是**并集的成员少收了**，本条是**并集的成员多收了**，而两者都源于「把并集当成一个对任何输入都正确的动作」。

**为什么门禁无感（本条的要害）。** 触发它的人是**读回文件**发现的，不是检查发现的。`tools/check_links.py` 与 `tools/check_doc_paths.py` **各自都只检查「有没有指向不存在的东西」**：

```
$ grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py
（零命中）
```

markdown 对「一句话出现两遍」没有任何意见，两个检查器因此全绿。⇒ **它是一个集合被枚举了，而成员是否重复从未被比较过** —— 与 §1.65（一个名字两个含义）同族，并同属 §1.67 立的那条判据（本仓库的检查器多数是「指向性」的）：**不是算错了，是没有任何东西在看这件事。**（§1.59 曾是同族实例，现已由 `tools/check_orphan_fixtures.py` 补上 —— 见 §1.67。）

**与 D4 的边界。** D4 裁的是「缺失覆盖由门禁**可计算地**发现」。本条是「**可计算但未被计算**」——同文件内的近邻重复段落是一条写得出来的检查，只是现在没有。**故本条立账、不现在实现检查。**

**判据（今后）。** 解文档冲突时，**「两侧新增」与「两侧改动同一行」必须用不同的动作**：前者取并集，后者必须选一个版本。合并后若某行的两侧版本归一后相同，**该行的出现次数仍是判据**（应为 1）。⇒ 凡合并 `docs/**`，交付前必须**读回被改的文件**——这条动作的**依据在 §1.67**（本仓库的检查器多数是「指向性」的，内容层面的不一致不靠人读回就没人看），此处不重复论证。

### 三条诚实边界

1. **这次是人工读回文件发现的，不是工具发现的。** 也就是说，本条能立账本身就是「门禁在这一层是空的」的证据 —— 与 §1.65 同形（那条也是靠人逐条读 `grep` 命中才发现前提为假）。**不要把本条读成「已有一条检查在盯这件事」**：没有。
2. **「形状相同、数字不同」不是本条要抓的东西。** 触发它的人另外核过 **7 处「相邻近似行」**，判定它们各自**数值不同**、是表格行而不是重复。⇒ 「近邻重复」的判定在实现时必须先把这个区分定下来：**归一之后相等才算重复，形状相同而数值不同不算**；否则任何密集表格都会红。**本节的判据只覆盖「归一后相同」，不覆盖「看起来像」。**

## §1.67 本仓库的检查器几乎都是「指向性」的：它们问东西在不在，不问东西是不是唯一的

**判据本身。** 现有检查器绝大多数是 **reference-shaped（指向性）** —— 只问「有没有指向不存在的东西」，从不问「被指向的东西是否唯一、是否一致、是否只出现一次」。**这条要立成判据，是因为写下一个新检查器时，先问它属于哪一类，比先问它检查什么更有用。**

**实测：不是「全部」，而是「多数」。** 逐个量过 `tools/check_*.py`：

```
$ for f in tools/check_*.py; do n=$(grep -c 'dup\|repeat\|seen\|count(' "$f"); echo "$f  $n"; done
tools/check_async_commands.py        0
tools/check_doc_paths.py             0
tools/check_ignored_credentials.py   1
tools/check_links.py                 0
tools/check_orphan_fixtures.py       0
tools/check_test_ports.py            1
tools/check_text_integrity.py        2
```

**⇒ 其中有一条不是指向性的，而它恰好是本族最近一次修复的产物：**

`tools/check_orphan_fixtures.py` **就是 content-shaped** —— 它把 `vtest/` 目录这个集合与 `CMakeLists.txt` 的注册集合**真的做了比对**：

```
$ python3 tools/check_orphan_fixtures.py
check_orphan_fixtures: 115 input(s) checked (69 vtest fixtures, 33 python harnesses, 13 node harnesses); 66 fixtures registered, 3 allowed with a stated reason.
```

它的 docstring 第一行写着 `Why this exists (docs/AUDIT.md §1.59):`，且在门禁里真的跑（`tools/gate.sh` 里 `stage_orphan_fixtures` 的 `tools/check_orphan_fixtures.py` 调用；`4ca013d` 上在 `:474`）。**⇒ §1.59 描述的那个缺口已经被这条检查补上了。** 它自己第 23-24 行那句「a set is named, and its membership is never checked against the set that actually runs」是在描述**它要修的缺陷**，不是在描述它自己 —— **读起来极易反过来理解。**

同类还有 `tools/check_text_integrity.py`（为 §1.55 而写，检查文本文件不得含 NUL 字节 —— 这是**内容**性质的，不是指向性的）。

**为什么这仍然是缺陷。** 判据的结论不因「有一条例外」而改变，但**它的表述必须从「全部是」改成「多数是」**：一个集合被枚举了，而**成员之间**是否重复/是否一致从未被比较过。本族的四处实例：

- **§1.66** —— 并集解冲突产出重复行，而**没有任何检查器看得见重复**（`grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py` 零命中）。**这是本判据唯一的「实测过的空位」。**
- **§1.65** —— `Total Tests` 一个名字两个含义（注册上限 vs 某平台实跑数），而没有任何检查器比对这两个口径。
- **§1.59** —— **曾经**是实例（`vtest/` 集合与注册集合从不比对），**现已由 `tools/check_orphan_fixtures.py` 补上**。列在这里是为了记录「本族的缺口一旦被发现就可以被补」，而不是把它当作现存缺口。
- **§1.55** —— 曾被 NUL 字节污染源文件；**现已由 `tools/check_text_integrity.py` 补上**，且那条检查本身就是 content-shaped 的。

**实用判据（最重要的一条）。** **写新检查器时，先问它是 reference-shaped 还是 content-shaped。** 若一个仓库的检查**只有** reference-shaped，那就意味着「**所有内容层面的不一致都必须靠人读回文件**」—— 所以：

> **凡是合并 `docs/**`、或改动计数类的量，交付前必须读回被改的文件。**（与 §1.66 的判据同一句，见该节「判据（今后）」；两节互相引用，不各写一份。）

### 三条诚实边界

1. **本条只描述现状的形状，不主张把所有检查都改成 content-shaped。** 有些检查本就该是指向性的 —— `check_links.py` 问「这个链接指向的文件在不在」就是它的全部职责，让它去查重复会把两件事混成一件。**判据是「先分类」，不是「都应该变成后者」。**
2. **实例是枚举出来的，不是穷举。** 我只逐个量了 `tools/check_*.py` 这 7 个文件（用上面那条 `grep` 命令，判据是「是否含 `dup`/`repeat`/`seen`/`count(`」），**没有**审计 `tools/` 下其余脚本、`.github/workflows/**`、`tools/gate.sh` 里各阶段的检查逻辑，也没有审计 CTest 侧。⇒ 若别处存在 content-shaped 检查，本条的第一句应当再收窄；**这条命令是重跑的入口，不是已经穷尽的结果。**
3. **本节的实例清单同时含「已修」与「现存」两类，而两者的判据不同** —— 这是两种不同的定性，不能混读：**「已修」的判据是「存在一条检查器、且它真的跑在门禁里」**（`§1.59` → `tools/check_orphan_fixtures.py`，`tools/gate.sh` 的 `stage_orphan_fixtures`；`§1.55` → `tools/check_text_integrity.py`，`tools/gate.sh` 的 `stage_text_integrity`），**「现存」的判据是「没有任何检查器覆盖这件事」**（`§1.66` 的 `grep -n 'dup\|repeat\|seen' tools/check_links.py tools/check_doc_paths.py` 零命中；`§1.65` 的两个口径无任何比对）。⇒ **同一个列表里两类条目若不分判据，读者会把「已修」读成「仍有缺口」或反过来** —— 而这两种误读都恰好是本族在治的形状。**本节因此把两类并列写出，而不是只留现存缺口。**

## §1.68 同一个量在两棵树上有两个值，而两个都是对的

**本节是 §1.67 判据的一个具体形态，也是 H4.1（「给数字不等于给出可信度」）在跨树引用上的落地。** 一条崩溃调用链在 `c71ea00` 上取到，而 `main` 已在 `5868940`；**同一个 `pos[4096]`，在两棵树上有两个行号，而两个都是对的。** 读者若只拿到一个数字，就一定有一个是错的。

**不是「这棵树加 28 行」，是「两棵树的对应关系没有单一的 delta」。** 实测（`git show <ref>:src/runtime/runtime.c`）：

| 位置 | `c71ea00`（发布点 / 链的取证树） | `main`（`5868940`） |
| --- | --- | --- |
| `const char *pos[4096];` | `966` | `994` |
| `if (count < 4096) pos[count] = nt;` | `971` | `999` |
| `const char *cont = re_seq(elnext, t);` | `977` | `1005` |
| `t = pos[count - 1];` | `981` | `1009` |
| `re_seq(pattern + 1, s)`（顶层入口） | `999` | `1027` |
| `static const char *re_seq(...)`（定义） | `920` | `948` |
| `re_seq(re + 1, s)`（组） | `912` | `940` |
| `re[0] == '$'`（`$` 尾巴） | `924` | `952` |
| `re_seq(left, s)`（分支左） | `938` | `966` |
| `re_seq(re + alt + 1, s)`（分支右） | `941` | `969` |

**上表十行逐项量过，偏移在 `24` 与 `28` 之间不恒定** ⇒ 「加 28 行」这种心算在两棵树之间不成立。命令：`git show c71ea00:src/runtime/runtime.c | sed -n '966p;971p;977p;981p;999p'`。

**引用行号时必须同时标明是哪棵树。** 本节的崩溃链取自 **`c71ea00`** —— 崩溃帧、带符号构建 `build-sym`、`w38-ctest.log` 全是那棵树上的字节；而 `c71ea00` **现在是「已发布点」，不是正本**（`main` 已在 `5868940`）。**不写这一句，本节自己就成了一条漂移值** —— 而这正是本节在治的形状。

**给读者一条命令之前，先在命令所指的那棵树上跑一遍。** 本节初稿曾按 `main` 上核过的行号（`968`/`972`/`990`）写进面向 `c71ea00` 的段落 —— 若照此落地，**这一节在落地当天就是一条漂移值**。拦截它的动作不是更仔细，而是**「拒在不核过的树上给读者命令」**：先把 `git show c71ea00:...` 跑一遍，再写数字。

**`build-windows-gcc` = DO NOT DELETE / DO NOT REBUILD。** 它是 Windows `122`（注册数）那份证据对应的构建目录，而 **`/mnt/d/inim-rel/src/build.ps1` 会 `Remove-Item -Recurse -Force` 掉它** —— 一次手快的「重跑一遍」就会销毁证据。带符号的取证另起 `build-sym`（`RelWithDebInfo`，未碰 `build-windows-gcc`）。

**`inimerse_crash.log` 是追加写入、且按 CWD 落盘**（从 `/mnt/d/inim-rel/src` 跑会落在 `vtest/inimerse_crash.log`）⇒ **必须先清空再读、且只读一条条目**；否则会串到旧条目上（本轮一度把一条陈旧的 `0xC0000005` 读成栈溢出链）。

**`122` 是注册数、`121` 是实跑数**（`1 failed out of 122`）—— 两个概念，不混读。

**三条探针，都是「不靠门禁」，但触发时机不同**：**列数**（本轮有人在 Markdown 单元格里嵌换行、把 6 格的行拆成 3 格，门禁全绿）、**行是否重复**（另一条线上 `docs/BOARD.md`/`docs/STATUS.md` 一行出现两遍，`check_links`/`check_doc_paths` 全绿）、**我在哪棵树上**（本节作者的 `142` 误读）。**前两条在写完之后读回文件，第三条在读之前** —— 同族而触发条件不同，三条都要留。

**活体标本（同一路径、两个分支、两个值、门禁全绿）**：`stream/builtin-contract-rulings @ ab70a71` 的 `tools/gate.sh` 的 `EXP_CTEST` 那一行是 `EXP_CTEST="${EXP_CTEST:-142}"`，而 `main` 上是 `148`（**原文写的是「根工作树的 …… 至今是」，那个「至今」在根工作树切到 `main` 之后就假了** —— 又一次「观测点没跟着被观测的对象走」）。**本节作者读它时正好中招**，一度把 `142` 判成对方的数字错。

## §1.69 测量点、发布物、以及「我说的那棵树」怎么变成可判定的

**本节是 §1.67（「东西在不在」有人问，「两个值是不是同一个」没人问）与 §1.68（同一个量在两棵树上有两个值）在发布动作上的落地。** 一节写证据的文档，若只说「量过」，读者无法判断量的是不是发布的那棵树 —— **本节把每个断言换成一条可重跑的命令。**

### A. 测量点 ≠ 发布树

**`122` / `121` 是在 `c71ea00` 上量的，而发布树是 `4e444dd`。**

```
$ git rev-list --count c71ea00..5868940        →  44      （量过；不是 112）
$ git merge-base --is-ancestor c71ea00 5868940 →  是祖先（RC=0）
```

**完整、不得压缩成「122/121」一句话**：

> **在 `main` 的祖先 `c71ea00` 上量到 122 注册 / 121 实跑；`main` 已于 `5868940`；两者之间 44 个提交；未在 `5868940` 上重量。**

**`44` 是量的；`112` 是 `v0.5.0..5868940`，与本节无关。** 两个数出自两个区间 —— 一个数写错出处，下一个读者就会把它当成另一个区间。**命令：`git rev-list --count <refA>..<refB>`，用哪个区间就写哪个区间。**

### B. 发布树是哪一棵

```
$ git rev-parse --short v0.5.1              →  ceda774   （annotated）
$ git rev-parse --short v0.5.1^{commit}     →  4e444dd
$ git diff --stat 5868940 4e444dd           →  docs/STATUS.md | 2 +-   (1 insertion, 1 deletion)
$ git rev-parse --short main                →  5868940   （未动）
```

**`4e444dd` 的父是 `5868940`，tag 指的 commit 与 `main` 差一个文档提交。** 不写这句，读者会把「发布树」当成上面 A 里那个测量点 `c71ea00`。

**「Lands the language migration on `release/051-final`」这句是可判定的，不是措辞**：

```
$ git merge-base --is-ancestor release/051-final 5868940   →  RC=0（是祖先）
$ git rev-parse --short release/051-final                  →  c71ea00
```

⇒ **祖先关系成立**，同时它说明 **`release/051-final` 与 A 里的测量点是同一个 commit** —— 两句话指同一个 ref，**读者必须能从命令看出来，而不是从措辞猜。**

### C. `142` / `148`：同一量、两棵树、两个值、两个都对

```
$ git cat-file -p c71ea00:tools/gate.sh | sed -n '54p'
EXP_CTEST="${EXP_CTEST:-142}"
$ git cat-file -p c71ea00:CMakeLists.txt | grep -c 'add_test('
142
$ git cat-file -p 4e444dd:tools/gate.sh | sed -n '54p'
EXP_CTEST="${EXP_CTEST:-148}"   # recounted off the merged tree by the commit below, not inherited from either side
$ git cat-file -p 4e444dd:CMakeLists.txt | grep -c 'add_test('
148
```

**`148` 的留痕形态不是一份日志，是一个提交加一条可重跑的命令**：`4e5b3f8`（`gate: the number is 148`）、`9d1ec6f`（`gate: EXP_CTEST is recounted off the merged tree, not inherited`）。**⇒ 数字在树上一致，且它是怎么来的写在树里。** **「几条测试」与「EXP_CTEST 是多少」在同一棵树上互相印证**，`grep -c 'add_test('` 一条命令即可复算。

**⚠️ 若不追问，tag 正文里的 `148` 会看起来与本项目的 `142` 冲突。** 事实是：**合并后的树重数了一遍，`142` 与 `148` 各自为真、各自有出处。** 这正是 §1.68 的形状，**而这次的「两棵树」是发布前后的两棵树。**

### D. 两个目录：留着是为了能重跑，不是因为它们重要

**`build-windows-gcc` = DO NOT DELETE / DO NOT REBUILD。** 它是 Windows `122`（注册数）证据对应的构建目录，而 `/mnt/d/inim-rel/src/build.ps1` 会 `Remove-Item -Recurse -Force` 掉它 —— **一次手快的「重跑一遍」就会销毁证据**。带符号的取证另起 `build-sym`（`RelWithDebInfo`，未碰它）。

**`inimerse_crash.log` 是追加写入、且按 CWD 落盘**（从 `/mnt/d/inim-rel/src` 跑会落在 `vtest/inimerse_crash.log`）⇒ **必须先清空再读、且只读一条条目**；否则会串到旧条目上（本轮一度把一条陈旧的 `0xC0000005` 读成栈溢出链）。

**`vtest/_bisect_*.im` 探针留着。** 它们是二分出「N=25 起崩」的工具，而「25 起崩不是 `pos[4096]` 越界」这条结论**靠它们才站得住**：**留下的是结论、扔掉的是能重跑结论的东西。** 它们不在 git 内，不构成发布树上的字节。

### E. 本节自己遵守的纪律

**本节所有数字都在写之前用上面的命令跑过一遍。** 一条命令若只覆盖表的一部分、或只在另一棵树上跑过，**就必须在正文里写清覆盖范围与适用树** —— 否则这一节在落地当天就是一条漂移值。

## §1.70 Windows 上 match 长模式的崩溃：26 帧 × 32896 字节，和一次把尾调用写成递归

**测量点。** 崩溃读数取自 `main` 的祖先 `c71ea00`（也就是发布线 `release/051-final`）的 Windows 构建；修复与反向验证在 `stream/win-match-fix`（base `main` @ `57ece55`）上做。两棵树都点名，因为同一个量在两棵树上有两个值。

### A. 现象与分类

`match_long_pattern_runtime` 在 Windows 上 `***Exception: SegFault`（`w38-ctest.log:262-263`）。分类键是日志里的 `code`：

```
[crash] code=0xC00000FD
```

`0xC00000FD` 是 `STATUS_STACK_OVERFLOW`，不是 `0xC0000005`（访问违例）。同一台机器上另有一处 `code=0xC0000005` 的崩溃（`crash-g.log`，15 帧），两者可区分。

`[stack] #7` 与 `[crash] rip` 逐字节相同 ⇒ 回溯停在崩溃帧。原因是 `RtlCaptureStackBackTrace` 走 `.pdata`，而 `___chkstk_ms` 没有 `.pdata` 条目。**这不是 `-g` 或 `-Wl,-Map` 能补的**，是结构上取不到，所以下面不用回溯。

### B. 机制（实测）

方法：直接读原始栈内存，数落在 `re_seq` 运行区间内的返回地址。运行区间由 `rip - 0x974c6`（`___chkstk_ms` 的 RVA）加 `nm` 给出的偏移算出 —— 不能用 PE 静态 `ImageBase`，ASLR 下镜像加载基址是随机的。

```
RE_HITS=51
DISTINCT_RET_OFFSETS={+0x16: 26, +0x25a: 25}
STRIDES={0x48: 25, 0x8038: 25}
FRAME_SLOTS=26
RSP=0x407af0
```

`+0x16` 是 `call ___chkstk_ms` 之后那条指令，`+0x25a` 是唯一那次递归 `call re_seq` 之后那条（`14005333a: mov %rax,%rbx`）。`re_seq` 的序言是

```
push %r15
mov $0x8038,%eax
push %r14,r13,r12,rbp,rdi,rsi,rbx
call ___chkstk_ms
sub %rax,%rsp
```

帧大小在两个配置上各量了一次，**逐项相同**：发布配置 `-O3 -DNDEBUG`（`build-windows-gcc/inimerse.exe`，`re_seq` @ `0x140063440`）与 `RelWithDebInfo` `-O2 -g`（`build-sym`）的序言都是 `push %r15 / mov $0x8038,%eax / push ×6 / call ___chkstk_ms / sub %rax,%rsp`，帧 = 8×8 + 0x8038 + 8 = **32896 B = 0x8080**，与测到的步长逐项相同。发布配置上另有两处独立印证：`___chkstk_ms` 在 `0x1400c0850`，而 `w38-ctest.log` 里 `rip` 的 RVA `0xc0866` = 它 `+0x16`（崩溃帧正落在 `___chkstk_ms+0x16`）；同一条日志里的 `[stack] #8 0000000000008038` 就是这个常量。**这条推翻了本节初稿的边界**：帧大小原先只是 `RelWithDebInfo` 上的读数、对发布配置是外推；现在它是发布证据那个二进制本身的读数，`0x8080` 不是 `-O2` 的产物。**但这三条读数（两个配置的序言逐项相同、`___chkstk_ms` @ `0x1400c0850`、`rip` 的 RVA `0xc0866` = 它 `+0x16`）在本树上没有可重算的输入** —— `build-windows-gcc`、`build-sym`、`w38-ctest.log` 都不在 Linux 工作树里（`ls -d build*` 只有 `build`）**[obs: agent3 的 Windows 树]**，读者只能接受转述；**并且本树门禁全绿对这条判据不提供任何证据** —— 这正是 §1.71 的结论，用在 §1.70 自己身上。

⇒ **崩溃瞬间有 26 个活的 `re_seq` 帧，仅它们就占 26 × 32896 = 855296 B。**

预算：`objdump -p` 给出 `SizeOfStackReserve 0x200000`（2 MiB），两个 exe 一致。`vm_execute_thread`（`src/vm/vm.c:2931`）不是 OS 线程，是主线程在 `src/vm/vm.c:4950` 调的 ⇒ 预算就是主线程的 2 MiB。

算术：崩溃时 `rsp=0x407af0`，可读顶 `0x601000` ⇒ 已耗 `0x1f9510` = 2069776 B = **98.70%**（2 MiB 预留 `0x200000` 的 98.70%）。剩余 `rsp − 0x401000` = `0x6af0` = **27376 B**，小于进入下一帧需要的 `0x8080`(32896 B) ⇒ **差 5520 B**。这就是 `___chkstk_ms` 报 `STATUS_STACK_OVERFLOW` 的算术。

**更正（写下这一节时才发现）。** 本行初稿写「剩余 `0x60f0`(24816 B) ⇒ 差 8080 B」——**`0x407af0 − 0x401000` 是 `0x6af0` = 27376，不是 `0x60f0` = 24816**；`8080` 是这一步的连带值。独立核对（不经过上面任何一步）：已耗 2069776 + 进一帧 32896 = **2102672**，超出预留 2097152 **恰好 5520**。**三个数各量不同的东西，不得互相推导**：26 帧自身 855296 B 是 `re_seq` 帧的和（占预留 **40.78%**），98.70% 是整条栈的已耗，5520 B 是差多少进不去。这一处与本节其余读数无关（帧数、帧大小、步长、两个输入读数相同都不受影响）。

**最有力的一步是两个输入给出逐项相同的读数**：2030 个字符的输入与 25 个字符的输入，`RSP` 都是 `0x407af0`，都是同样的 26 帧 ⇒ **深度由栈预算封顶，与输入长度无关**。

独立复核：在 `re_seq` 上打断点计数，`contract_test.im` 3 次、`vtest/posix_core_api_v04.im` 6 次、`vtest/match_long_pattern_v06.im` 26 次后 SIGSEGV。两个互不相干的方法都落在 26。

**边界（保留）。** 不声称这 26 个里哪一个是崩溃帧。两个计数彼此自洽，但「出错的那一帧」与「走完序言的那些帧」之间的一格之差，这两次测量都没有分辨。

**边界（保留）。** `CONSUMED` 与 `STACK_READABLE_TOP` 来自读探针，Windows `ReadProcessMemory` 对 reserved-but-uncommitted 页会成功并返回零 ⇒ 可读顶可能高于真正的 `StackBase`。26 帧与 `0x8080` 步长不受影响，因为那是栈上的真实数据。

### C. 读码（与实测分开写）

以下行号都在**修复前**的字节 `2245ca1` 上。`re_seq`（`src/runtime/runtime.c:948`）是手写回溯器。模式里没有 `*`/`+`/`?` 时 `elnext = re + elen`（`:990`），而续接写成了递归：

```
:1005    const char *cont = re_seq(elnext, t);
```

于是**每个模式元素花一帧**。2030 个字符的字面模式要 2030 帧 × 32896 B ≈ 66.8 MB。

### D. 修法

三处纯尾调用改写成既有 `for (;;)`（`:949`）的迭代：

| 位置 | 改前（`2245ca1`） | 改后（`24f6814`） |
|---|---|---|
| `$` 分支 | `:952 return (*s == '\0') ? re_seq(re + 1, s) : NULL;` | `:953-961 if (*s != '\0') return NULL; re++; continue;` |
| 交替分支 | `:969 return re_seq(re + alt + 1, s);` | `:981-982 re = re + alt + 1; continue;` |
| 无量化续接 | `:1005 const char *cont = re_seq(elnext, t);` | `:1017-1028 if (!quant) { re = elnext; s = t; continue; }` |

无量化那条是尾调用，理由是**构造上的**：没有量化符 ⇒ `qmin == qmax == 1` ⇒ 下面的回溯循环只能返回续接或返回 NULL，没有第二条路。新增的 `quant` 标志（`:998`，三个量化分支置 1）就是用来分辨这两条路的。

有量化那条仍然递归，但每次 `re_seq(elnext, t)` 返回之后才进下一次，深度由嵌套决定而非模式长度（早先一个探针实测深度 2）。不会死循环：`elnext = re + elen`，`elen >= 1`，`re` 严格前进。

`src/runtime/runtime.c` 是这次唯一改动的 `src/` 文件：`git diff --name-only 57ece55 -- src/` 只有它。

### E. 判据

| 判据 | 结果 |
|---|---|
| ① 那条 CTest 在 Windows 上由 SEGFAULT 变通过 | 通过（`1/1 Test #137 ... Passed 1.01 sec`） |
| ② 反向验证：在**带修复的同一棵树**上换回修复前的字节，增量重建，必须重新红 | 通过（`***Exception: SegFault 1.08 sec`） |
| ③′ 同树 Windows 全量 ctest 不新增失败 | 通过（见下表） |
| ③ 同树 Linux 门禁 | **作废，空绿** |

同树 Windows 全量 ctest（`INIMERSE_BIN` 必须指到构建目录，否则测试工具找不到引擎）：

| 状态 | 结果 |
|---|---|
| 修复前 `237c547ef2cb5f8c7f29702bafd77062f41feff4` | `98% tests passed, 3 tests failed out of 128` |
| 修复后 `e0b3d93c204901ee475f8524e9a8b7f23e08045a` | `98% tests passed, 2 tests failed out of 128` |

修复前那三条是 `process_probe`（本环境下的既有抖动，在 `w38-ctest.log:38-39` 的发布证据里是 Passed）、`count_builtin_runtime`（见 F.4）、`match_long_pattern_runtime`（本次目标）。

**③ 为什么作废。** `src/runtime/runtime.c` 在 POSIX 上根本不参与编译：`CMakeLists.txt:429` 在 WIN32 分支里选它，`:438` 的 `else()` 分支选 `src/runtime/runtime_posix.c`（后者没有 `re_seq`，用 libc `regcomp`）。所以 Linux 门禁**不会编译被改的那个文件**。它在这次修复上是空绿 —— 可以跑，但只能当卫生检查，不能当证据。**这不是本节的性质，是门禁的性质**，见 §1.71。

### F. 登记但未修

1. **`pos[4096]` 的无守卫读。** `:1007 const char *pos[4096];`，写入有守卫（`:1012 if (count < 4096) pos[count] = nt;`），回溯读没有（`:1035 t = pos[count - 1];`）。这是并存的独立缺陷，**未被本次崩溃触发** —— 由「25 就崩」证伪：4096 项的数组在 25 不可能溢出。只登记，不改。
2. **被证伪的假设留在产物里。** 崩溃起点被钉在 N=25（20…24 正常，25 首次崩），`pos[4096]` 溢出假设与任何页/大小阈值假设一并被排除。留下它，是因为下一个读到「25 就崩」的人会先去想 4096，这一段能省他一轮。
3. **`re_seq` 仍有 32896 字节的帧。** 尾调用改掉之后，它仍然封顶**嵌套量化元素或嵌套分组**的可用深度。触发类已写出，**未测**。
4. **同一类崩溃的第二个实例，不在本次修复范围。** `count_builtin_runtime` 在 Windows 上也是 `code=0xC00000FD`，但 `[stack] #8` 是 `0000000000006ed8`（≠ `re_seq` 的 `0x8038`），`re_seq` 断点在它的 fixture 上计数为 **0**，原始栈扫描把它定位到 `compile_expr` 自递归（32 帧，偏移 `+0x16` 与 `+0x15eb`）。该测试在发布证据 `w38-ctest.log` 里不存在（是 `c71ea00` 之后新增的），而且在 `c71ea00` 的 `build-sym` 上同样崩 ⇒ **既有缺陷，被新测试暴露**，不是本次修复引入的。

### G. 空绿

「一条构造上不可能变红的判据，不是判据。」这次撞到三处：

- 本节的 ③：Linux 门禁不编译被改的文件。
- 测试工具里的 `find_engine()`：候选目录写死为 `build` / `build-local` / `build-windows-gcc` / `build-py`。构建目录叫别的名字时，六个工具测试会以 `inimerse engine not found; set INIMERSE_BIN` 失败 —— 那是**找不到引擎**，不是产品红。用 `INIMERSE_BIN` 指过去之后它们全过。
- `[stack] #8 0000000000008038`：这不是哨兵，是 `re_seq` 序言里的帧大小常量 `$0x8038` 被压在栈上。帧大小由它独立印证了一次。

## §1.71 Linux 门禁对 12 个 `src/**/*.c` 结构性失明：它们只在 `if(WIN32)` 分支里

**症状。** §1.70 的判据 ③ 被它自己的作者判成**空绿**：`src/runtime/runtime.c` 在 POSIX 上根本不参与编译，所以「同树 Linux 门禁」在那次修复上只能当卫生检查，不能当证据。**这不是那一节的性质，是门禁的性质** —— 同一个形状覆盖 12 个文件。

**实测（`main` @ `c76273f`，构建目录 `build/`）。** 逐文件比对「被 git 跟踪的 `src/**/*.c`」与「Linux 构建实际产出 `.o` 的」：

```
tracked src/**/*.c = 114   Linux 构建出 .o 的 = 102   没有 .o 的 = 12
  src/child_proc.c        src/headless_server.c      src/mod/ai_mod.c
  src/mod/gui_mod.c       src/mod/identity_mod.c     src/mod/io_mod.c
  src/mod/mod.c           src/mod/net_mod.c          src/mod/say_mod_windows.c
  src/mod/server_mod.c    src/mod/social_mod.c       src/runtime/runtime.c
```

方法（**这一条是判据**）：`find build -name '*.o'` 取 basename 去掉 `.o` 后缀，与 `git ls-files 'src/**/*.c'` 求差 —— 它是**内容性**的（两个集合真比过）。**但它自己的前提没写进初稿**：方法把 **basename 当唯一标识**，而**没有任何东西在检查 basename 的唯一性**。实测本树 `src/` 下重名 basename = **0** ⇒ 方法**现在**成立；**换一棵树、或加一个重名文件，它会静默给出错答案** —— 这正是 §1.67 的形状（「在不在」问过了、「是不是唯一」没人问），出现在一个自称比 §1.67 更强一层的条目里（由 ivory-ember 独立复核发现，已在下条更正）。**边界**：这是对**现有增量构建目录**的测量，不是对 CMake 目标图的解析 —— 若某文件本该被编译却因增量状态缺 `.o`，也会落进这 12 个里。**独立交叉核对（不是判据）**：这 12 个在 `CMakeLists.txt` 里**全部只出现在 `:429-436` 的 `if(WIN32)` 块内**（`grep -n '<file>' CMakeLists.txt` 的**首个命中**全落在这一段）—— **首命中是指向性的**，只能当交叉核对、不能当判据；POSIX 侧 `:438-444` 的清单里一个都没有 ⇒ 两条互不相干的方法给出同一组文件。**另一个独立读数**：`nm build/inimerse | grep -c 're_seq'` = **0**。

**为什么这是缺陷而不是设计。** 「Windows 运行时只在 Windows 上编译」本身合理；**不合理的是门禁把「没编译」报成「PASS」**。Linux 十二阶段里 `build` 与 `ctest` 对这 12 个文件无声，而 `grep` 类检查器只看文档与 fixture 名（§1.67）—— 于是**改动这 12 个中的任何一个，在 Linux 上拿到的是一个全绿的门禁**，而绿的原因与改动无关。§1.70 G 节把这一类叫「一条构造上不可能变红的判据，不是判据」。

**与 §1.67 的关系：正交，不是同一轴的强弱**（本条初稿写「更强一层」，**错了**，由 ivory-ember 的独立复核更正）。§1.67 量的是**判据的形状**（指向性还是内容性），本条量的是**判据的覆盖面**（被测集合里有没有这个东西）。**本条自己引的证据就是反证**：`tools/check_orphan_fixtures.py` 正是 §1.67 点名的**非**指向性那一例（它比的是集合），而它 `grep -c 'src/'` = **0** ⇒ **一条内容性判据同样完全看不见这 12 个文件**。⇒ 修好 §1.67 那一类**不会**顺带修好本条，反过来也一样。本条不重复 §1.67 的判据，只登记这一组文件与它的量化。

**登记，未修。** 修法候选（**未裁定**）：①在 `build` 阶段增加一条显式的「这些文件在本平台上不参与编译」清单与理由 —— 照 §1.60 的规矩，白名单也要写出「那跑的是什么」；②或把源文件也纳入某个既有的孤儿检查（agent2 已实测 `tools/check_orphan_fixtures.py` 的 `grep -c 'src/'` = **0**，即它结构上不可能发现 `src/` 孤儿）。**本条只登记**：改门禁在 `tools/**`（agent2 的写域），且这 12 个里哪些是**真** Windows-only、哪些是**漏进** WIN32 分支的，需要逐个裁定。

**§1.71 补（同一笔之后加的读数）。** 把上面这 12 个按「POSIX 侧有没有替代物」分成三组 —— 两组是设计，一组不是：

| 组 | 文件 | POSIX 侧 | 性质 |
|---|---|---|---|
| ① 有对等实现 | `src/runtime/runtime.c`→`runtime_posix.c`、`src/headless_server.c`→`headless_server_posix.c`、`src/mod/mod.c`→`mod_posix.c`、`src/mod/net_mod.c`→`net_mod_posix.c`、`src/mod/server_mod.c`→`server_mod_posix.c`、`src/mod/say_mod_windows.c`→`say_mod_posix.c` | `CMakeLists.txt:438-443` 各有一个 | 平台对等，合理 |
| ② 被空桩顶掉 | `src/mod/gui_mod.c`、`src/mod/io_mod.c`、`src/mod/identity_mod.c`、`src/mod/social_mod.c`、`src/mod/ai_mod.c` | `src/platform/posix_stubs.c:6-8` 的 `STUB_REG(...)` | 已登记的平台边界，但**登记得不全**（见下） |
| ③ 两组都不是 | `src/child_proc.c` | 无对等实现、也无桩 | 无引用者，故不参与链接；**未裁定** |

**②这一组的登记不全，是本条真正的增量。** `docs/STATUS.md` §10.44/§10.47、`docs/AUDIT.md:453` 与 `:594`、`docs/API.md:323` 说的都是**三个**桩（`io_mod`/`gui_mod`/`build_mod`），而 `src/platform/posix_stubs.c:6-8` 逐字桩的是**六个**：

    STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)
    STUB_REG(identity_mod_register)
    STUB_REG(social_mod_register) STUB_REG(ai_mod_register)

⇒ `identity_mod`、`social_mod`、`ai_mod` 三个**不在任何一份已登记的清单里**。规模也没登记过：这六个模块在 Windows 上 `vm_register_builtin` 的次数（**严格计法 `vm_register_builtin(_full)?\(`**）是 `io_mod` **43**、`gui_mod` **161**、`identity_mod` 8、`social_mod` 5、`ai_mod` 6（`build_mod` 在 `mods/` 下，不计），合计 **223** 个内建名在 POSIX 上不存在。**本处初稿写 `io_mod` 44 / `gui_mod` 162 / 合计 225，是错的**（由 ivory-ember 独立复核发现）：`io_mod` 在 `c76273f`/`8e67a1e`/`d766add`/`809dc73` 四个 ref 上都是 **43**；`gui_mod` 松散的 `grep -c 'vm_register_builtin'` 会多数一行 —— 那一行是 `src/mod/gui_mod.c:3695` 的**注释**（逐字 `vm_register_builtin now refuses a duplicate name and says so on stderr.`），**把一行注释算成了一个内建名**。复算命令：`for f in src/mod/{gui,io,identity,social,ai}_mod.c; do git show <ref>:$f | grep -cE 'vm_register_builtin(_full)?\('; done`（宽松计法给 224，含那行注释）。**结论不变**（两百多个内建名在 POSIX 上不存在），但数得出来才算数。

**并且那个「告诉你它不可用」的函数，六个桩里只有一个用。** 同文件 `:4` 的 `unsupported()` 会打印 `inimerse: capability '%s' is not available on this POSIX build yet`，而它**只被 `build_project_impl`（`:9`）调用**；六个 `STUB_REG` 展开成 `void name(VM *vm) { (void)vm; }`，**一个字都不打**。实测（`main @ 8e67a1e`，`./build/inimerse --no-mods`）：`file_exists("CMakeLists.txt")` → `[exception] uncaught: unknown builtin function 'file_exists'`；`ai_list()` → `unknown builtin function 'ai_list'` —— **读者看到的是「这个名字不存在」，而不是「这个平台不支持它」**，而后者才是作者写在同一个文件里的答案。`docs/STATUS.md` §10.47 记的正是这次「从静默返回垃圾变成抛异常」的可诊断性收益；本条是它的**下一步**：异常说了「没有」，没说「为什么没有」。

**顺带一条未核对的观察。** `docs/SYNTAX.md:510` 把 `file_exists` `mkdir` `io_list_dir` `http_get` `clipboard_set` `timer_ms` `exec_async` `proc_list` 列进「**核心高频内建**（有 `vtest` 覆盖的）」；**该行没有平台标注**（全节未逐处核对），而实测在 POSIX 上这些名字全部不存在。**未裁定**：是给那张表加平台列，还是在平台边界那条登记里指过去。

**边界。** 223 是**注册调用点的计数**（严格计法），不是去重后的名字数（同一名字可能多处注册），也不是「Linux 用户实际会调到的名字数」；要精确须取注册名集合。**本条只登记，不改 `src/`、不改门禁。**

**这 12 个文件的逐文件裁定（Windows 侧生产点 / POSIX 对应物 / 归属 / 注册测试可达性）记在 `docs/streams/win-source-attribution.md`（`agent3` 量于 `d27413a`：74 条以 `inimerse.exe` 为 COMMAND 的测试 × 14 个入口断点）。** 两处要点先放在这里：**「仅 Windows」有两义** —— **空桩**（gui/io/identity/social/ai 五个，POSIX 有 `STUB_REG`，功能确实只在 Windows）与**构建表所致**（`child_proc.c`：**文件里一行 `_WIN32` 都没有**，头文件还专门为非 Windows 给了 `DWORD` typedef，是 `CMakeLists.txt` 只在 `if(WIN32)` 里列它；**是否能编进 POSIX 分支 —— 未测**）。而第三列解释了本节的「结构性失明」具体长什么样：**8 个 `*_mod_register` 由 `src/main.c` 里的 `register_core_modules(&vm)` 无条件调用，`--no-mods` 挡不住**（73/74 命中），`mod_load_all` 57/74，`child_proc_*` 与 `headless_*` **0/74** —— 而那是**「没测到」，不是「不存在」**（`headless_probe` 在 Windows 根本没注册，gdb 也不跟子进程）。**这是一行指针，表本身在那边；它是记录，不是断言。**

**那两处「未测」现在测了（agent3，`main @ b68d6d6`，`-fsyntax-only` 跑在真实的 POSIX 旗标下）：`src/child_proc.c` 通过、`src/headless_server.c` 与 `src/runtime/runtime.c` 不过。** 宏与包含路径取自本树 Linux 构建的 `flags.make`，并**回到源码**核过（`if(NOT WIN32)` 那段给的是 `_GNU_SOURCE` 与 `_stricmp=strcasecmp`；`_WIN32` 在 POSIX 旗标里出现 **0** 次）。不过的两个都死在**第一个 Windows 头或类型**上：`headless_server.c` 第 6 行的 `#include <winsock2.h>`；`runtime.c` 里的 `HINTERNET`。

⇒ **`child_proc.c` 的「仅 Windows」不是文件的性质，是一条清单的性质。** 它 17 行、7 个 `#include`、**没有任何预处理条件**（这正是上文那句「一行 `_WIN32` 都没有」），头文件还给非 Windows 补了 `DWORD`；`-c` 产出的目标文件里所有未定义符号（`im_process_spawn`/`im_process_pid`/`im_process_alive`/`im_process_kill`/`im_process_close`、`im_mutex_*`、`im_platform_now_ms`）**在 POSIX 侧的目标文件里都有定义**（`src/platform/process.c` 同时列在两个分支、自带 `#ifdef _WIN32`/`#else`）。而 `nm build/inimerse` 里 `child_proc_spawn` **0 命中**、`build/` 里**没有**它的目标文件 ⇒ 它在 Linux 引擎里缺席，**只因为构建列表没列它**。

**但这仍不是一次链接**：没跑 `ld`、没跑、没被任何测试碰到、对行为什么都没说。**而它该不该被列进去，是一个决定，不是一个测量** —— 下一步要问的是「POSIX 侧有谁调它」：若无，编进去只改变二进制的内容，不改变任何行为。

**那个问题现在也答了（`main @ 2734b57`，`git grep -n 'child_proc_' -- src/ tools/ vtest/`）：调用者只有三个文件，全在 `src/mod/` 下** —— `src/mod/io_mod.c` 六处、`src/mod/server_mod.c` 四处、`src/mod/verse_dist_mod.c` 三处；**而这三个文件都只在 `CMakeLists.txt` 的 `if(WIN32)` 源码分支里列出**（`verse_dist_mod.c` 那处还顺手调了 `Sleep(1500)`）⇒ **POSIX 构建里没有任何被列进去的东西调它。** 把 `child_proc.c` 加进 POSIX 列表**只会改变二进制的内容，不改变任何行为**。⇒ **清单是对的；文件开头那句 `/* Cross-platform child process registry. */` 是一个意愿，不是一条现状。** 这一处**只登记、不改**。


## §1.72 一次推送前必须跑哪些阶段，由 `git diff --name-only` 决定，不由感觉决定

**症状（由 ivory-ember 复核指出，发生在本文档自己的推送流程里）。** `ce457df` 只删了一个 `。`，我在**文档三阶段 rc=0 之后就推了**，全量门禁（**当时是十二阶段**）**在推之后**才跑完。**这次是对的，但「对」需要写出理由才算对** —— 否则「全量门禁在跑」这句话会被下一个读的人理解成「推的时候还没验证」。

**规矩。** 推之前必须跑的，是**这笔 diff 能影响的阶段**；这件事的**输入**是 `git diff --name-only <base> <head>`。

**通则 A（按扩展名**或**文件名，不按目录）。** `tools/check_text_integrity.py` 有**两条**入口，缺一条这条通则就只写了一半：

- **`:55` 起 `TEXT_SUFFIXES`** = `.c .h .cc .cpp .hpp .rs .py .sh .bash .md .txt .im .json .jsonc .yml .yaml .toml .ini .cfg .cmake .iss .ts .tsx .js .mjs .cjs .css .html .xml .csv .gitignore .gitattributes .editorconfig`，作用域是 `git ls-files` 的**全部受管文本**；
- **`:93` `TEXT_NAMES = frozenset({"CMakeLists.txt", "LICENSE", "Makefile", "Dockerfile"})`**，由 **`:125` `if path.name in TEXT_NAMES: return True`** 生效；`:52-54` 逐字写着理由：「\`CMakeLists.txt\`, \`LICENSE\` and \`Makefile\` are matched by name below because they have no informative suffix or none at all.」

⇒ **凡改动落在这些扩展名、或这些文件名上，`text-integrity` 就必须跑，与文件在哪个目录无关。** 这条单独写，因为**逐行补必漏**（初稿只在 row 1/row 2 写了它，于是 row 3/row 4/row 5 全漏）；且 **`text-integrity` 不是「读 markdown 的阶段」** —— markdown 只是它拥有的三十几个后缀之一。 **通则是按机制写的，那就要把机制写全，不能只写机制里好看的那一半。**

**`TEXT_NAMES` 那一半是第二轮之后才补的，补之前它是个真洞**：`CMakeLists.txt` 被「全部阶段」那一行覆盖了，但 **`LICENSE`、`Makefile`、`Dockerfile` 落在每一行之外** —— 改它们的人照这张表跑，**一个阶段都不用跑，而 `text-integrity` 会读它们**。本树里 `LICENSE` 与 `Makefile` 都真实存在（`Dockerfile` 没有），而**在 `a64c4fd` 上** `docs/AUDIT.md` 里 `LICENSE`/`Makefile` 各出现 **0** 次（`git show a64c4fd:docs/AUDIT.md | grep -c 'LICENSE'` ⇒ 0）。

**这句必须带观测点，而初稿没带。** 它在 `a64c4fd` 上为真，在写下它的 `4b7b37c` 之后**为假** —— **因为这句话自己把这两个词各写了两遍**（`git show 4b7b37c:docs/AUDIT.md | grep -c 'LICENSE'` ⇒ 2 —— **注意这个命令必须带 ref**）。这是 §9 第 30 条的又一投影，**而且它出现在「补上机制的另一半」那一段里**：要保住的事实是「**补之前**它是洞」，不是「这个词永远不出现」。

**这条命令的初稿不带 ref，于是它自己就是这条规矩的又一次落空。** 初稿写的是不带 ref 的 `grep -c`（**关于当前树的断言 ⇒ 下一笔就失效**）：**这一笔又加了一行含该词的行**（`git show 588a302:docs/AUDIT.md | grep -c 'LICENSE'` ⇒ 3；而**基数是 2 不是 1**，因为早就在的 `:3318` 那行逐字引了 `TEXT_NAMES`）。**而正确形式就在它前一句**：前一句带 ref、永远为真，后一句不带 ⇒ **同一段里两种形式并存，一个成立一个不成立。**（**这里原来写的是「隔四个词」，那是一个没量过的说法** —— 错因正是 H4.1：**给了一个会随 ref 变化的量，却没给产出它的命令**。初稿后来补的那两个距离数也删了：**它们量的是本文档两点之间的距离，而其中一个端点就是这句被改掉的话 —— 改它，必然移动它。**）

**★ 由此得到一条比上面几条都硬的处方：当被数的东西就是这句话自己写的字时，不要报绝对数，报增量。** 证据就在这一段里：**每加一段「解释这个数为什么错」的话，就又多出几行命中，于是那个数又变了** —— `4b7b37c` 2 → `588a302` 3 → `601aac1` 4，**这是一个发散过程，不是收敛过程**；所以它不是「这条规矩的又一个实例」，而是**这条规矩会自己生产新实例**。修法**不是**再写一段自觉的文字（那会变成 5），而是写成**不随自己变化的形状**：「**这一笔又加了一行含该词的行**」是**增量、永远为真、不需要 ref**；**绝对数必须带 ref**；**被测量的是本文档自身的量（词频、行距、字符距）时，绝对现值一律非法** —— 因为改这句话就是移动被测量的东西，合法的只有两种：**带 ref 的历史读数**，或**增量**。**这一段到此为止，不再加自觉的散文 —— 再长，它的数字只会更错。**

**这条规矩的落空，每一次都发生在一句正在解释这条规矩的话里**（H4.1 的判定、§1.70 的观测点、这一句，都只是已经能点名的几个） ⇒ 所以它不是新缺陷，是**这条规矩的又一个必要实例**：**不带 ref 的观测点会随下一笔失效，而失效方向恰好是「越解释越假」。**

| 改动落在 | 推前必须跑 | 为什么（不是「感觉不可能」） |
|---|---|---|
| `docs/**` | `links` / `doc-paths` + **通则 A** | 只有 `links` 与 `doc-paths` 把 markdown **当 markdown** 读 |
| `src/**` | `build` / `ctest` / `fuzz` / `economy` / `plugin` + **通则 A** | `tools/check_text_integrity.py:5-8` 逐字写着它**为什么存在**：「Three git-tracked C sources carried NUL bytes inside block comments -- `src/mod/gui_mod.c` (5), `src/lexer/lexer.c` (2), `src/lexer/lexer.h` (1)」；`tools/economy_migration.test.py:70-75` 找 `build/inimerse`、`:142` 跑它；`tools/dsh-inimerse/verify.mjs:137` 走 `inim_run`，而 `tools/gate.sh` 的 `stage_plugin` 自述（`4ca013d` 上 `:356-357`）是「a live round trip through the real **inim-server / inim-client binaries**」 |
| `vtest/**` | `ctest` / `orphan-fixtures` + **通则 A** | 输入是 fixture；不读 C 源码、不读 markdown |
| **`tools/` 下任何被 `tools/gate.sh` 调用的文件**（含 `Infiverse_standard/oauth_loop/**`） | **它被调用时所在的那个阶段** + **通则 A** | **判据本身变了**。**这一行不许按名字手写 glob，必须从 `tools/gate.sh` 的真实调用点派生** —— 映射见下表 |
| `tools/gate.sh`、`CMakeLists.txt` | **全部阶段** | **判据本身变了，且没有任何阶段可免** |
| `.github/**` | **通则 A**（`.yml` 在列）；其余无 | 实测**无人读 `.github/`**：全仓只有 `tools/ctest_enumerate.sh:12` 的一句注释提到它 ⇒ 「没有阶段读它」成立，**但「不受影响」不成立**（`.yml` 是 `text-integrity` 的输入） |

**`tools/gate.sh` 的真实调用点（逐个读出，不是按名字猜；**下表全部在 `4ca013d` 上量**）。**

| 阶段 | 它调用的判据 |
|---|---|
| `ctest` | `tools/check_test_ports.py`（`:168`，**在 `stage_ctest` = `:148-236` 内部**） |
| `fuzz` | `tools/im_diff_fuzz.py`（`:241`） |
| `economy` | `tools/economy_migration.test.py`（`:303`） |
| `node` | `tools/node_suites/run_all.js`（`:326`） |
| `plugin` | `tools/dsh-inimerse/verify.mjs`（`:361`；它自己的自述在 `:356-357`） |
| `oauth-loop` | `Infiverse_standard/oauth_loop`（`:375`）、`tools/check_async_commands.py`（`:424`，**在 `stage_oauth_loop` = `:364-430` 内部**） |
| `ignored-credentials` | `tools/check_ignored_credentials.py`（`:439`） |
| `links` | `tools/check_links.py`（`:443`） |
| `doc-paths` | `tools/check_doc_paths.py`（`:453`） |
| `text-integrity` | `tools/check_text_integrity.py`（`:463`） |
| `orphan-fixtures` | `tools/check_orphan_fixtures.py`（`:474`） |
| `orphan-targets` | `tools/check_orphan_targets.py`（`:488`） |
**为什么是十二行。** 四个数**各自有出处**，不用减法串起来（全部在 `4ca013d` 上量）：`$REPO_ROOT/` 在 `tools/gate.sh` 里出现 **15** 处（`grep -c '\$REPO_ROOT/' tools/gate.sh`）；其中 `:54` 的 `BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"` 是**构建输出目录**、`:625` 的 `--required-for` 读取器是**读 `TEXT_SUFFIXES` 的工具本身** ⇒ **13 处判据调用点**；`STAGE_SPECS` 里注册 **13 个阶段**（`awk '/^STAGE_SPECS=\(/{f=1;next} f&&/^\)/{exit} f' tools/gate.sh | grep -c '^ *"'`），除 `build` 外**每个阶段调用一个判据** ⇒ **12 个阶段**；而 **12 行对应 13 处引用**，因为 `oauth-loop` 那一行含**两个**文件（`:375` 与 `:424`）。
**这四个数在合入 `4ca013d` 时全部变了一次**（`$REPO_ROOT/` 13 → **15**、阶段 12 → **13**、引用 12 → **13**、行 11 → **12**），而**两个非调用点的 `$REPO_ROOT/` 里，有一个正是修这条规矩的那个机制自己加的**（`:625` 的 `--required-for` 读取器）—— **修「集合变了、数字没变」的那一笔，自己又往那个集合里加了一项。这也是「集合变了、数字没变」的一例，而且是唯一一次成因是「修这个病的东西本身就是新元素」的。**

**这一句的初稿是本节第三个数字问题，而且是性质最坏的一个。** 初稿写「引用共**十二**处……十二阶段减 `build` 得十一」—— 实测总数是 **13**（`git show 4b7b37c:tools/gate.sh | grep -c '\$REPO_ROOT/'`），于是那个减法在它自己的量上不成立（13 − 1 = 12 ≠ 11）；**它把「判据引用数」和「阶段数」当成了同一个量，在一个句子里换了两次口径**。**三者是三种形状**：「八个」→十一是**过期的数**（集合变了、数字没变）；`TEXT_NAMES` 的洞**不是数字问题、也没有「变了」** —— 那是**机制只写了一半**（一条从未在文档里出现过的通道，不是过期的数）；而这一个是**新写的错数**。**三种形状比「两个同类 + 一个更坏」更能支持「性质最坏」这个判断**：前两者是漏改，第三个是新增。

**★ 这张表本身的成立条件（本条初稿在这上面错了四处，全部由 ivory-ember 复核指出，五条我逐条独立复核成立）。**
1. **初稿的 `src/**` 行漏了三个阶段，而其中一个是「为 `src/` 而建」的**：`text-integrity` 的存在理由**就是** `src/` 里的 NUL 字节。**照初稿那张表跑的人，不会跑那个专门为 `src/` 而建的阶段** —— 一张用来**免跑**的表把该跑的免掉了，这比没有表更坏。
2. **初稿把第三列写成「不受影响」的名单，而同一个表头下有两种意思**：`vtest/**` 那一行填 `——`，读作「没有阶段是不可能受影响的」，**而那是假的**（`vtest/**` 显然不影响 `links`/`doc-paths`）。**一个表头两种语义，就是本轮一直在治的形状。** 现在第三列改成「**为什么**」——它要的是**理由**，不是**名单**。
3. **初稿缺了「判据本身变了」的第二个入口**：十三阶段里有**十二个**调用了 `tools/`（或 `Infiverse_standard/`）下的文件（见下表；**十三减 `build` 得十二** —— `build` 阶段的 `$REPO_ROOT/build` 是**输出目录**，不是判据）。**改 `check_doc_paths.py` 显然影响 `doc-paths` 阶段**，而初稿只把 `tools/gate.sh`/`CMakeLists.txt` 当成判据变更入口。**这个数字在第二轮补进四个之后没跟着改（同一节、隔一张表），是「集合变了、数字没变」的一例；而在合入 `4ca013d` 之后它又变了一次（十二 → 十三，`orphan-targets` 是第十三个阶段），当时它正被本节引用 —— 同一个量、同一节，又变了一次。**
4. **标题比实际强一级 —— 这一处在 `4ca013d` 上已修。** `git diff --name-only` 给的是**文件清单**，清单→阶段的映射原先只存在于上面那张 markdown 表里（散文）、**没有一条命令返回 0/1**。**现在有了**：`tools/gate.sh --required-for <base>..<head>`（用法、取参、以及它读 `TEXT_SUFFIXES`/`TEXT_NAMES` 那处，**号见上面那张带 `4ca013d` 的映射表；本节不裸写 `tools/gate.sh` 的行号**，理由见 ★10），它**从 `STAGE_SPECS` 与真实调用点派生**，先例就在同一个文件里（逐字：「The legal `--only` values are derived from this, never from a second handwritten list: **a list nothing consumes only constrains the moment it was written**」），并带四条红控：注册表指向不存在的函数、规则写成不存在的阶段、派生器读不到任何调用点 —— 这三条各返回 rc=2；**第四条要先把 `REQUIRED_SCOPES` 里两条 `|all` 规则去掉才会红**（`tools/gate.sh` 里那条 `if [ "$sels" = "all" ]; then reach=1; break; fi` 让 `reach` 对每个已注册阶段都是 1 —— **本节原写 `tools/gate.sh:768`，那个号从来不对**：`:768` 是它下面 `for s in $sels` 里那一行；**改用文本锚，理由见 ★10**）。**有 `all` 时每个已注册阶段确实可达，所以 rc=0 是对的 —— 错的是把它写成「各返回 rc=2」。****（补：它并非看不见未提交改动 —— 单参形式 `--required-for HEAD` 是「rev vs 工作区」，实测对 ` M .gitignore` 答 `text-integrity`；两参形式 `<base>..<head>` 只看已提交。⇒ 正确的处置不是「要求参数含 `..`」（那会禁掉唯一能看见工作区的形式），而是**让它自报它回答的是哪个问题**。）**仍未可派生的一半**：`REQUIRED_SCOPES` 里「哪个阶段的主题覆盖哪棵子树」仍手写，因为 `stage_links` 跑 `tools/check_links.py`、**没有任何调用点说它读 `docs/`**（引擎是被编译器读的，也没有 reader），每次运行与注册表核对。⇒ **判据可执行了；表里「覆盖哪棵子树」那一列仍是声明。**
5. **第四行的 glob 是按「名字形状」选的，漏了四个「判据本身」（第二轮打回）**：初稿写 glob `tools/check_*.py`、`tools/*.test.py`，于是**名字不像 checker 的四个全漏了** —— `tools/im_diff_fuzz.py`（→`fuzz`）、`tools/node_suites/run_all.js`（→`node`）、`tools/dsh-inimerse/verify.mjs`（→`plugin`）、`Infiverse_standard/oauth_loop/**`（→`oauth-loop`）。**这四个恰恰是「改了就影响某个阶段」的判据本身。** 而**修法不是再加一个 glob，是换来源**：从 `tools/gate.sh` 的真实调用点派生。**我这次是靠手工枚举才把四个找出来的，而这个枚举本身就该由脚本做。**
6. **表内自相矛盾**：row `src/**` 的「为什么」**逐字引用了 `tools/dsh-inimerse/verify.mjs:137`** 当作必须跑 `plugin` 的理由，而同一文件在第四行**不是**「判据本身变了」的入口 —— **同一张表对同一个文件给了两种身份。**
7. **`text-integrity` 漏在后三行，成因是它按扩展名扫全部受管文本**：`tools/check_text_integrity.py:55` 起 `TEXT_SUFFIXES` 含 `.md .im .py .sh .yml .js .json .rs .ts .txt .toml` 等，作用域是 `git ls-files` 的**全部**受管文本 ⇒ row 3（`.im`/`.py`）漏、row 4（`.py`）漏、**row 5 说 `.github/**` 「无」是错的**（`.yml` 在列）；顺带 row 1 的理由也不准（它写「只有这三个阶段读 markdown」，而 `text-integrity` **并不把 markdown 当 markdown 读**）。**干净修法是表外加一条通则，不是逐行补**（见通则 A）。
8. **row 3 与 row 4 的 glob 相交且答案不同**：初稿 row 3 的 `tools/*.test.*` **包含** row 4 的 `tools/*.test.py`；对 `tools/economy_migration.test.py`，row 3 说跑 `ctest`/`orphan-fixtures`（**错的** —— 它不是 CTest 的输入），row 4 说跑 `economy`（对的）。**一个路径类落在两行、两个答案。** 现在 row 3 只留 `vtest/**`。
9. **两个「在内部被别的阶段调用」的脚本必须点名它属于哪个阶段**，否则「它背书的那个阶段」是一句空话：`tools/check_test_ports.py` 在 `tools/gate.sh:168`、属 `stage_ctest`（`:148-236`）⇒ 背书 `ctest`；`tools/check_async_commands.py` 在 `:424`、属 `stage_oauth_loop`（`:364-430`）⇒ 背书 **`oauth-loop`**（**按名字完全猜不到**）。

10. **合入 `4ca013d` 让本节自己失效了一次（「集合变了」的又一例）。** 那条分支在 `tools/gate.sh` 顶部加了 35 行，于是**本文档里 13 处 `tools/gate.sh:<N>` 引用当场全部失效**（实测映射：`EXP_CTEST` 那一行 `:54` → **`:92`**、`check_test_ports.py` `:131` → `:168`、`stage_ctest` `:111-200` → `:148-236`、`stage_oauth_loop` `:327-394` → `:364-430`、`check_orphan_fixtures.py` `:437` → `:474`、`check_text_integrity.py` `:426` → `:463`）。**注意这条链是文档的，不是文件的。** 文件本身在这期间还经过 **`:60`**（`84ab67e`，顶部 +6；而 `84ab67e` **不是 `4ca013d^1` 的祖先** —— 它在被 `4ca013d` 合进来的那条支线上），于是**同一棵树上可以同时挂着文档里的两个号与文件里的一个号**；拿这条链去算「文件挪了多少」会得 +4/+38 而**漏掉那 +6**。**同一个目标在本文档里已经有过三个号码**（`EXP_CTEST` 那一行：`:50` → `:54` → `:92`），**而每一次写下时都是对的**。⇒ 处置**不是把 13 个号码换成新号码**（下一笔又会漂），而是**在 `tools/gate.sh` 上不再写裸行号、改写成锚**（写「`EXP_CTEST` 那一行」、写「`stage_orphan_fixtures` 的 `check_orphan_fixtures.py` 调用」）；**只有上面那张映射表保留行号，因为那张表的主题就是行号，且整表带 `4ca013d`。**
11. **本文档指向 `CMakeLists.txt` 的行号没有任何东西在查，而且已经漂过。** 这次合并对 `CMakeLists.txt` 的改动是 `@@ -1031,8 +1031,21 @@`（净 +13），于是 §1.73 里那处 `:1370`（38 个可执行目标的第一个源文件那一段的端点）**当场变成 `:1383`**；而同一节里另外七处（`:25`/`:377`/`:451`/`:578`/`:588`/`:614`/`:1007`）全在 1031 之前，**不受影响**。**普查另找到两处不是这次合并造成的**：`:383`/`:386`/`:395`（`CMakeLists.txt` 上根本不是那三样东西）与 `:1113`（`params_precompiled_runtime` 实际注册在 `:1153`）—— **两处都已按内容重取**。⇒ `tools/gate.sh` 的行号有上面那张表与 `--required-for` 兜着，**`CMakeLists.txt` 的行号一处兜的都没有**。（以上号在 `c9a218d` 上量；补充读数在 `4fe8850` 上量。）**补：这条普查的筛选条件是错的** —— 我按「号 > 1031」筛，等于假设陈旧只能是这次合并造成的；**正确的条件是「引的是 `CMakeLists.txt` 的行号」**。按内容重取后另找到四处明确陈旧（`:453` 的 `:383-393`/`:394-400`、`:1184` 的 `:381-395`、`:1413` 的 `:395`、`:1442` 的 `:400`）与四处区间端点偏了（`:1809`/`:2134` 的 `:412-436`、`:2430` 的 `:428-442`、`:2830` 的 `:427-429`）。**而每一次漂移的偏移量都不同**：`:383 → :426` 是 **+43**、`:400 → :434` 是 **+34**、`:412 → :427` 是 **+15**、`:387 → :421` 是 **+34**、`:733 → :756` 是 **+23**、`:662 → :676` 是 **+14**、`:190 → :201` 是 **+11**、`:202 → :213` 是 **+11**、`:915 → :982` 是 **+67** ⇒ **没有统一偏移量，「按合并的 +13 换算」与「按某个固定偏移量换算」都不成立，只能按内容重取。****而这条普查的输入集也不对**：按前缀形式 `grep -o 'CMakeLists\.txt:[0-9-]*' docs/AUDIT.md` 收 **35 处 / 34 行**，而本文档提到 `CMakeLists.txt` 的行有 **68 行**、其中带行内号的 **46 行** —— **裸 `` `:NNN` `` 形式的真引用有 2 处**（`:1569` 的 `:421`、`:1659` 的 `:935`），**前缀式与区间式两种筛选都收不到它们**（`:1569` 的 `CMakeLists.txt` 只出现在 markdown 链接目标里；`:1659` 的号挂在「守卫」那句上）；而 46 行里绝大多数是**同一行提到别的文件**（`:692` 的 `:241`/`:1042` 属 `src/runtime/runtime_posix.c`、`:3318` 的 `:93` 属 `tools/check_text_integrity.py`）。⇒ 检查器的输入集必须定义成「**同一行出现 `CMakeLists.txt` 之后还有行内号**」（前缀 + markdown 链接 + 裸号三种），**且逐处要内容锚才能判定那个号属于谁**。**同类还有三个分母，而它们的起点都必须是「那个值」，不是「说这句话的文件」**：`git grep -n 'docs/AUDIT\.md:1807' -- .` 给 **5 份**拷贝（`docs/AUDIT.md:2550`、`docs/BOARD.md:272`、`docs/BOARD.md:278`、`docs/STATUS.md:3808`、`tools/check_orphan_fixtures.py:82`），而我从「写这句话的那个文件」出发只找到 **2** 处；`git grep -n -E 'docs/AUDIT\.md:[0-9]' -- . ':(exclude)docs/AUDIT.md'` 给 **12 行**；把这几轮修过的 `CMakeLists.txt` 陈旧值逐个 `git grep` 一遍，**`docs/AUDIT.md` 之外还有 17 处引用、落在 17 行上**（`docs/BOARD.md` 7 行、`docs/STATUS.md` 7 行、`docs/DECFY_DESIGN.md` 3 行）—— **两个数必须出自同一条命令、而输入集必须写出来**：`git grep -nE 'CMakeLists\.txt:(190|202|383|386|395|400|662|662-663|733-743|744|915|935|1113|1370|383-393|394-400|381-395|412-436|428-442|427-429)\b' -- . | grep -v '^docs/AUDIT.md'`。**这一句我先写成「19 处 / 17 行」，而 19 与 17 不是从同一份输出派生的**（ivory-ember 指出）：19 是**逐个值各跑一次再相加**的和，而 `\b` 让 `:383` 也匹配 `:383-393` ⇒ 同一行被数两次（`docs/BOARD.md:145`、`docs/STATUS.md:2648` [obs: main @ 58f1a44]；**前者的号在 `stream/required-for-closure` 上已漂成 147** —— 那一支在阶段表第 61 行之后插了两行，`docs/BOARD.md` 的行号从那里起整体 +2），17 才是上面那条命令的行数。**这正是我自己裁过的「判据两侧必须从同一份输出派生」，出现在写这条裁定的那一节里。**⇒ **但这些不能一律改成当前值**：其中一部分是**指针**（「去那里看」⇒ 必须指向现在），一部分是**记录**（「当时量到的是什么」⇒ 必须保留原值，并写明是哪棵树）。**区分二者是判断，不是搜索** ⇒ 处置是：① 指针改成当前值**并就地标出原文写的是什么**；② 记录保留原值；③ **在检查器存在之前，这 17 行是已知未处理项** —— 每处引用必须有内容锚，锚不成立就报出来，由作者声明它是哪一种。**这一条原来写成「其余交给检查器」，ivory-ember 判它是本段唯一的免票**：那个检查器**今天不存在**，而写法让读者读成「已交给一个机制」⇒ **把未处理写成未处理，不是把它写成流程。**（`docs/BOARD.md:272` 与 `:278` 是**同一个 slug 的两行**，`:278` 少一列、计数停在 110、且写着「`migrate_report.py` 的测试本轮不写」，而 `:272` 记着它后来补上了 ⇒ **本表把两行都留了**：`docs/BOARD.md` 的行号被全仓库引用，删一行会移动其后所有行号。）

12. **三次「按点名的行去改」，而缺陷就在紧邻处（ivory-ember 逐次实测）。** `:713` 修了 `:383`，而同一节的 `:453` 还留着 `:383`；`:3330` 去掉了序数，而同一段的 `:3326` 还留着序数；`:1659` 修了 `:935`，而**它上面两行、同一段**的 `:1657` 还留着 `:915`（`:915` 是注释，闭合 `:411` 的 `if(INIMERSE_BUILD_ENGINE)` 的 `endif()` 在 `:982` —— **第九个偏移量，+67**）。⇒ **三次都不是没做，是做完之后没在紧邻处再看一眼。改动的单位是「段落」，不是「被点名的那一行」；检查器也一样 —— 它的作用域必须是「行」，而不是「被点名的行」，否则它会和它的使用者犯同一个错。**

13. **把「列表长度」与「去重后的长度」当成同一个量。** **这句话先是 ivory-ember 写的，我把它复制进了产物**（它要求写明这一层：那是事实、不是客气）—— 所以这条错的形状不是「数错了」，而是**「抄对了」**：**一个数可以靠复制传播，而复制与计算在文本上无法区分，除非判据跟着数一起走**（这解释了为什么同一句错话在同一轮里出现在两个地方，而「数错了」解释不了）。我向它报 ★11 的九个偏移量时写「**第九个不同的值**」，而九项只有 **七个**不同值（`+34` 与 `+11` 各出现两次）—— **这个数从来没有被算过，只被数了条目**；而**产物是干净的**（★11 写的是「第九个偏移量」＝列表里的第九项，且结论「没有统一偏移量」只要 ≥2 个不同值就成立）。⇒ **判据**：说「不同值」之前，`len(list)` 与 `len(set(list))` 各跑一次，**相等才允许**。这是 H4.1 那一族的又一形态：**数字必须有产出它的命令** —— 而这里连「这是个集合的基数」这件事都没被检查过。

**边界（本条初稿的反例放错了行，由 ivory-ember 实测更正）。** 初稿写「一个改 `docs/` 却被 `text-integrity` 之外的东西读到的文件会**漏**」。实测**那一侧是严的**：没有任何测试或检查器 `open` 一个 `docs/` 文件（`tools/check_orphan_fixtures.py` 只读 `CMakeLists.txt:104` 与 node runner `:109`），`add_test` 行里引用 `docs/` 的 = **0**。⇒ **`docs/**` 那一行是整张表里唯一严的一行；漏在 `src/**` 那一行**，已补完。这条规矩整体仍是**充分不必要**的保守下界：**照它跑不会漏，但它不声称「跑完就够」**。
## §1.73 一个建出来、编译过、却没有任何东西跑它的目标，是唯一一种连「失败」都拿不到的证据

**本节是 §1.67（检查器问「东西在不在」，不问「两个值是不是同一个」）与 §1.59（没人跑的测试输入）在 CMake 可执行目标上的落地。** §1.59 比的是测试**输入**集合，本节比的是可执行**目标**集合 —— 同一个问题在两个层级上。

### A. 分母

```
$ grep -c 'add_test(' CMakeLists.txt            →  148
$ python3 tools/check_orphan_targets.py
check_orphan_targets: 38 add_executable( ) target(s) checked against 148 add_test( ) registration(s); 37 run by at least one CTest, 1 allowed with a stated reason.
```

**判据**：一个目标算「被跑」，当且仅当它的名字作为某个 `add_test( )` 里 `COMMAND` 之后的第一个 token、或作为 `$<TARGET_FILE:…>` 的实参出现。**匹配的是目标，不是测试名**（见 D）。

### B. 那个 1 是谁

**`websocket_probe`。** 它被建（`CMakeLists.txt:222`）、被链接（`:223`）、被 include（`:224`），源列表里有 `src/platform/websocket_probe.c`，而 `tools/` 与 `.github/` 里没有任何东西引用它。

**它不是新发现，也不需要修**：`docs/API.md:481` 已经记着「`CMakeLists.txt` 里没有该 CTest」，`docs/API.md:622` 的通道表把 WebSocket 行标为「⬜ **预留** … 协议帧未实现；**无 `websocket_probe` CTest**」。**登记为「已知未注册」，保留。** 判据不是「它有没有用」，而是「**它是不是唯一剩下的那个**」—— 38 个里 37 个被至少一个 CTest 跑。

### C. 为什么这个集合从来没有被比过

`tools/check_orphan_fixtures.py` 的分母是三类输入文件：`vtest/*.im`、`tools/*.test.py`、`tools/*.js`（读码 `:113-146`；`grep -c 'src/' tools/check_orphan_fixtures.py` = **0**）。而 38 个可执行目标的**第一个源文件全部在 `src/` 下**（`CMakeLists.txt:32 src/platform/platform_probe.c` … `:1383 src/platform/socket.c`）。

⇒ **该检查器在结构上不可能看见这一族：它的分母里根本没有「目标」这个概念。** 这不是它写错了，是它被写成了回答另一个问题 —— 与 §1.66、§1.67 同形状。

这条形状付过一次真实的代价：`src/platform/vfs.c` 的 `..` 守卫搜了一个从未写入的终止符（读未初始化内存），`im_vfs_normalize("os:/../escape")` **40/40 被接受**，而唯一断言正确行为的 `src/platform/vfs_probe.c` **建了很久却从没被跑过** —— 它是被人读码看见的，不是被抓住的。它现在已注册（`CMakeLists.txt:34`、`:1027`，CTest `#129`）。

### D. 那把尺子，以及它第一次给出的三个假孤儿

第一次手查用的是「测试名是否等于目标名」（`add_test(NAME <目标名> …)`），报出三个：`literal_resolve_probe`、`resolve_timeout_probe`、`websocket_probe`。**前两个是假的**：测试名由写注册的人起，不必像它跑的目标 —— 它们是 `literal_resolve_runtime` 与 `resolve_timeout_runtime`，两个目标都在跑。换成「目标是否出现在 `add_test( )` 的 `COMMAND` 或 `$<TARGET_FILE:…>` 里」之后，**三个变一个**。

**按测试名匹配目标名是错的尺子。** 这句话写在 `tools/check_orphan_targets.py` 的头注释里，而不是留在本节当一条轶事 —— 下一个量这个问题的人会先读那个文件。

### E. 一条什么都不豁免的豁免，是一句没有任何运行能证伪的话

检查器里有一张 `ALLOWED` 表，唯一一项是 `websocket_probe` 及其理由。两次反向对照：

```
$ # A：把 ALLOWED 的键改名，使查找落空
check_orphan_targets: 2 problem(s) over 38 add_executable( ) target(s) checked (37 run by at least one CTest, 1 run by none):
  websocket_probe: built, but no add_test( ) runs it (it is named 3 other time(s) in CMakeLists.txt). …
  websocket_probe_renamed: listed in ALLOWED, but it is not an orphan (there is no such add_executable( ) target), so the allowance excuses nothing. …
rc=1

$ # B：把一个「已经被测试跑」的目标塞进 ALLOWED
check_orphan_targets: 1 problem(s) over 38 add_executable( ) target(s) checked (37 run by at least one CTest, 1 run by none):
  inimerse: listed in ALLOWED, but it is not an orphan (a CTest runs it), so the allowance excuses nothing. …
rc=1
```

**对照 B 当场抓出了检查器自己的一个缺陷。** 成功行原来印的 `37 run by at least one CTest` 是 `len(targets) - len(ALLOWED)` **算出来的**，而不是数出来的 —— 只在每个豁免恰好都豁免真孤儿时才等于实数。把 `inimerse` 塞进 `ALLOWED`，它就印 `36`，而树上有 37 个。**一个从表里算出来的数，是一个没有人能复核的数**（§1.68 说的是同一个病：一个量在两处有两个值）。修法两条：两个数都改成**数出来的**；`ALLOWED` 里不豁免孤儿的那条**自己也被报出来** —— 一条什么都不豁免的豁免，就是一句没有任何运行能证伪的话，而且它会掩盖「这个目标某天不再被跑」的那一天。

### F. 诚实边界

- **`COMMAND` 的首 token 是文本读法。** 经 CMake 变量间接引用的目标（`COMMAND ${SOME_TARGET}`）认不出；今天没有这样的注册，这一句是「今天没有」，不是「永远不会有」。
- **「被跑」不等于「被断言」。** 没有 PASS 也没有 FAIL 正则的注册，在任意退出码上都算过（§7.2 M13）。本检查器回答的是「有没有东西跑它」，不是「跑它能不能证明什么」。
- **被 install 规则或 custom command 引用的目标仍算孤儿**：被复制不是被跑。
- **本节只量了 `add_executable( )`。** 本仓库另有 6 个 `add_library( )`（`CMakeLists.txt:25`、`:451`、`:578`、`:588`、`:1007`、`:1381`）与 2 个 `add_custom_target( )`（`:377`、`:614`），**都没有量**。对库来说，「被跑」的类比是「被测试加载」，而那是另一个问题 —— 一个当 `LD_PRELOAD` 用的 `.so` 是写在测试的环境里，不是写在 `COMMAND` 里，本节那把尺子量不到它。**这是没查，不是查过没有。**

## §1.74 命令能复现的，只有与运行状态无关的量

**本节记的是 H4.1 判定① 的失效，以及本节自己写下来时踩的那几次。** 到 §1.76 为止共九种，**它们不在同一个轴上**：A（运行状态）、B（筛选条件）、C（自指）、E（被别人移动）、F（从未正确）、G（输入是渲染）说的是**这个数怎么错的**；H（无人站岗）说的是**它错的时候谁会说话**；I（按形状定的规矩用按文件的豁免去执行）说的是**检查器自己**；D 说的是一条命令**答的是哪个问题**。**第十到第十五种、以及六个分类学事实，见本文件末尾的「§1.74 增补」**——它被放在末尾而不是这里，是为了不移动本文档里那些被别的文档按行号引用的位置（见那节第十种）。

H4.1（`docs/DECFY_DESIGN.md`）的两条判定是：① 数字旁边写着产出它的**命令**，或 ② 它被写成从同一份输出**派生**的断言。下面两处各证明 ① 的一种失效，而它们**方向相反**。

### A. 命令给了，也复现不出来：`822` vs `823`

`tools/check_text_integrity.py` 收尾打印的分母（「N text file(s)」）**不是任何 ref 的属性**。决定性实验（用隔离索引，不碰共享索引）：

```
$ export GIT_INDEX_FILE=/tmp/probe_idx_$$ ; git read-tree HEAD
$ GIT_INDEX_FILE=$GIT_INDEX_FILE python3 tools/check_text_integrity.py | tail -1
check_text_integrity: 823 text file(s), 0 with NUL bytes.
$ printf 'x\n' > .scratch_index_probe.md
$ GIT_INDEX_FILE=$GIT_INDEX_FILE git add .scratch_index_probe.md
$ GIT_INDEX_FILE=$GIT_INDEX_FILE python3 tools/check_text_integrity.py | tail -1
check_text_integrity: 824 text file(s), 0 with NUL bytes.      ← 同一 HEAD、同一工作区、只多一个索引条目
```

成因是读码事实：脚本第 79 行是 `["git", "ls-files", "-z"]`、`cwd=REPO_ROOT` ⇒ **读的是索引，不读任何 ref**。所以 `822` 是 `7fbc190`（911 paths）那棵树的值，`c9a218d` 与 `e3da33c` 各自量都是 **823 text / 912 paths**；差的那一个路径是 `tools/check_orphan_targets.py`（`edbc788` 加入、`4ca013d` 才进主线）。

**两个人都没读错，读的是两棵树** —— 数字被挂到了「它谈论的那个 ref」而不是「它读到的那棵树上」。

> **补充情形一。** 一个数如果是从**工作区 / 索引**读出来的（`git ls-files`、`git status`、文件系统枚举），那么「给出命令」不足以复现它；必须再给出**命令被运行时的状态**（哪个 ref 检出、索引是否干净），或者把它改写成**从 ref 派生**的量（`git ls-tree -r <ref>`）。判据：**同一命令在两个 ref 上跑，若得数不同，这个数就必须带 ref。**

同一处还有一条**脚本自身的不对称**：它的 docstring 逐字写着「`git ls-files` defines repository content … an untracked scratch file must not be able to turn this gate red」—— **对红绿成立，而它印出来的分母带着它刚刚声明要排除的那种依赖** ⇒ **同一个脚本里，「什么算内容」这条规则对判定严格，对报数宽松。** 最小改动一行：收尾那行自报出处（`(git ls-files: the index, not a commit)`）。

**本节里就有这条规矩的阳性对照，相隔 16 行**：**本节两处**（一处是「为什么是十二行」那一段，一处是 ★10）对 `tools/gate.sh:54` 给两个答案 —— 一处说它是 `BUILD_DIR=…`（**在 `4ca013d` 上**），另一处说它是 `EXP_CTEST`（**在 `4ca013d^1` 上**）—— **同一个号、两棵树、两个答案、两个都对**，因为每句话都带着自己那棵树。⇒ **A 段主张的「带 ref 能治过期」，在它自己的节里已经有一个活的例子**；而讲「F 是第五种」时也不该让读者以为带 ref 就够了 —— **带 ref 治过期，治不了从未正确，也治不了把展示值当测量值。**（**而这三种不在同一个轴上**：过期与从未正确是**坐标轴**，把展示值当测量值是**推导轴** —— 见下面 G 段。）（**这一句自己就是这么错的**：原稿给的是两个行号，转移进产物时两个都掉了、换成一个位置词「★ 列表里」，而那个位置词对其中一个是错的 —— **坐标在转手时会丢，丢了之后会被一个看起来对的说法补上**；这与「第九个不同的值」被复制是同一族。）

**A 的第三个入口：ref 没变，只换索引状态，得数也不同（agent3 本轮）。** 一份新文档写完后**先 commit 再跑 gate** 是必需的 —— 三个文档检查器都用 `git ls-files` 定义「仓库内容」（`check_text_integrity.py` 的 docstring 逐字写着 an untracked scratch file must not be able to turn this gate red），**未提交的文件不在扫描集里** ⇒ 直接跑会得到一次**空绿**。⇒ A 段说的是「换个 ref 得数不同」，这一条说的是「**ref 没变、只换索引状态，得数也不同**」；两条的共同判据是同一条：**这个数派生自哪份状态，就把那份状态写下来。**

### B. 命令精确复现，而数是错的：`106` vs `114`

```
git ls-files 'src/**/*.c'            106   ← 报告里写的
git ls-files ':(glob)src/**/*.c'     114   ← 真数
git ls-files 'src/*.c'               114
git ls-files '*.c'                   118   ← 全仓受管 .c
```

差集逐字是 `src/` 顶层那 8 个：`child_proc.c`、`desugar_mod.c`、`headless_server.c`、`headless_server_posix.c`、`headless_server_probe.c`、`isolate_mod.c`、`lint_mod.c`、`main.c`。**成因：默认 pathspec 里 `**` 被当作 `*`，而 `*` 能跨 `/`** ⇒ `src/**/*.c` 的两个字面斜杠仍然要求两个路径分隔符，直接躺在 `src/` 下的文件斜杠不够 ⇒ 落选；**`:(glob)` 下 `**/` 才有「零或多个目录」的语义**。⇒ **不是「`**` 漏了」，是「不写 `:(glob)` 时 `**` 根本不是 `**`」。**

⇒ `114` 与 `118` 都真，**`106` 是唯一那个「说的和量的不是同一个集合」的**；而「C 层有多大」**不是良定义的问题**，该写的是「**哪个集合**」（`src/` 之外还有 4 个：`hl_bridge.c`、`mods/build/build_mod.c`、`mods/debug/debug_mod.c`、`tools/wasm_probe.c`）。

> **补充情形二（比 A 更根本）。** 一个计数断言合格，**当且仅当**旁边写着产出它的命令，**且该命令的筛选条件本身是可复核的**。可执行检验：**把筛选条件换一种等价写法再量一次；若得数不同，那么「这个数是多少」这个问题在被问出来之前就还没定义好。**

**判定① 保证的是「可复现」，不是「正确」。** 而这条被记录下来的坑，**在同一天、同一棵树、同一条命令上又被踩了一次**（它早已写在 agent2 的 12 文件裁定表里）⇒ **一个已经被记录下来的坑，不阻止下一个人再踩一次；阻止它的只有把它写成判据。**

### C. 本节写下来时踩的第三次：一个数不能写在它自己数的那个提交里

`docs/RELEASE_0.5.2.md` 的基线表初稿写「相对 `v0.5.1`：该 tag 之后的 **74** 个提交（`git rev-list --count v0.5.1..v0.5.2`）」。**`74` 是在 `88e9338` 上量的**（那是合并 agent4 那条分支之后的 HEAD），而文档随 `751b2a0` 发布 —— 同一个命令在 `751b2a0` 上给 **76**。**数与它的 ref 错位，与 A 是同一形状。**

而这一处**比 A 更硬**：就算当时量对了，**这个字面量也永远不可能对** —— 写下一个数**就多一个提交**，而那个数正是要数的提交数。⇒ 落地的写法**不写这个字面量**：给出命令，把字面量挂在**一个被点名的 ref** 上。

> **§1.65 的形状在这里第二次出现，而这次它落在发布说明上。** 与 §1.65 的区别只是：那里写下结论的动作往仓库里加了一个**字符串命中**，这里加了一个**提交**。

**判据说准一点，就不是「不许写字面量」。** 一个字面量合法，**当且仅当写下它不改变它所量的那个东西**：

- `docs/RELEASE_0.5.2.md` 里写 `76` —— 那一笔**就在被数的提交里**，写下它就把计数 +1 ⇒ **非法**（改成给命令，并把字面量挂在一个被点名的 ref 上）。
- `docs/STATUS.md` 里写 `v0.5.2` → `0ebd68d` —— 那一笔**在 tag 之后**，写下它**不移动 `v0.5.2^{commit}`** ⇒ **合法**。

两条都可判定：`git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'` 与 `git rev-parse --short 'v0.5.2^{commit}'`。**这就是「写下这句话会不会改变它量的那个数」** —— §1.72 对 `tools/gate.sh` 的 `$REPO_ROOT/` 处数用过同一条判据（写下它不动它 ⇒ 合法），而 §1.65 那两处「零命中」是它最干净的反例。

### D. `--required-for` 的两种形式各回答什么

`tools/gate.sh --required-for` 有两种形式，而**命令行里没有任何东西告诉读者答的是哪一个**：

- **单参 `--required-for <ref>`** = **工作区相对那个 ref 变了什么**。读码事实：`tools/gate.sh:675` 是 `git -C "$REPO_ROOT" diff --name-only "$range"` ⇒ 含已暂存与未暂存、**与提交历史无关**。⇒ 干净树上必然是 `required: 0 stage(s)` + rc=0（agent4 在那棵树上看到的），脏树上就是那些脏文件（本轮在 `main` 上看到三个阶段）。
- **两参 `--required-for <base>..<head>`** = **那个提交区间变了什么**。

**两个答案都对，错的是「成功那一侧不自报问题」。** 「拒绝回答」那一侧是有牙的（`:676-679` 对不可解析的范围打 `gate: refusing to answer a question about a range that does not exist.` + `exit 2`）；缺的只是成功时说明它答的是哪一个 —— 而**「零个阶段」在一个敲错的调用里仍然是最危险的答案**。
### E. 第四种失效：写下时对，之后被别人移动了被量的东西

前三段（A 运行状态、B 筛选条件、C 自指）都是**写下的时候就错了**，所以都能靠「写得更仔细」避免。第四种不能。

`docs/STATUS.md` 的「已发布点」那一格曾逐字写：

```
**已发布点**：`v0.5.2` → `0ebd68d`（`git rev-parse --short 'v0.5.2^{commit}'`）… `git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'` → **76**
```

**写下它的时候，两处都是真的** —— `0ebd68d` 时 tag 就指向 `0ebd68d`，`v0.5.1..0ebd68d` 也确实是 77（而 76 是 `751b2a0` 上的值，即发布提交上的值）。C 段的论证也逐字成立：*写下它不移动 `v0.5.2^{commit}`* ⇒ 按 §1.74 C 的判据，它「合法」。

**然后 tag 被移动了两次**（第一次治发布说明里那个自指的字面量，第二次治 §1.70 那处算错的算术）。实测在冻结的 HEAD 上：

```
git rev-parse --short 'v0.5.2^{commit}'                     = 322c085   ← 文档写 0ebd68d
git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'    = 80        ← 文档写 76
git rev-list --count 'v0.5.1^{commit}'..751b2a0              = 76        ← 76 是这里量的
```

⇒ **同一行两个假数，而它们是「对过之后才变假的」。**

**结论一：每一条判据只保证它自己那一根轴。** 判定① 保证的是**可复现**，不是正确（B 段）。§1.74 C 保证的是**不自指**，不是**不会过期**（本段）。**C 段那条论证在自己的轴上完全正确，它只是不保证值会保持为真。**

**结论二：这条线有一个无限回退。** 修它要开一笔提交 ⇒ 为了让「tag 落在说对了的那棵树上」tag 又要移动 ⇒ **那一移又让新写的数过期**。只要「已发布点」写成「tag 当前指向 X」，它就永远追不上自己。

**处方（唯一能终止回退的那一种）：把现值换成带 ref 的历史读数。**
- 「已发布点」写**发版说明落笔点** `751b2a0`（那一笔上有版本号与发版说明），**不写 tag 当前指向哪**；要 tag 就 `git rev-parse`，并且**不从它派生任何数**。（**这条处方落笔时叫它「发版提交」，那个名字已退休** —— 理由见本节 E 的变体：那一笔上四个版本生产点只有 1 个是对的。）
- 派生的计数**一律带 ref**：「在 `751b2a0` 上量是 76」。
- 这不是新规矩：`docs/RELEASE_0.5.2.md` 的基线表**本来就长这样**（「在 `751b2a0` 上量是 **76**」，逐字写着「本行不写这个字面量」）。**同一个版本里两条相反的做法，活下来的是不写现值的那条** —— 而它给的理由（自指）**恰好不是它活下来的原因**，真正的原因是**它带了 ref**。**理由对了一半，产物就是对的；理由全对而写法是现值，产物照样过期。**

**结论三：这一条能进 `tools/`，而 A/B/C 那三种都不能。** 判据是一条命令：`git rev-parse --short 'v0.5.2^{commit}'` 与文档里记的那个 sha 相等；**不等 ⇒ 文档里所有从 `v0.5.2` 派生的数一次性全部作废**。手工普查做了一整轮，这一条一条命令查完 —— **它也是这一族里第一个「能自动变红」的**（A/B/C 的判据都要求人去跑一次原始读数）。

**E 的一个变体：换上的新锚与旧锚覆盖面不同。** 删掉「tag 当前指向哪」之后，「发版提交」这个词接过了「这一版是什么」的语义 —— **而它没有接过全部产物**，接过的正好是那个**没跟上**的生产点：`751b2a0` 上四个版本生产点（`CMakeLists.txt:8`、`installer.iss:7`、`:13`、`build_installer.ps1:25`）里只有 `CMakeLists.txt:8` 说 `0.5.2`。⇒ **换锚不只是换一个坐标，是换一个集合**；新锚能回答的问题必须**逐个点名**，不能假定它接过了旧锚的全部语义。**这一条也有一条命令可查**（四个生产点互查，任一与 `CMakeLists.txt:8` 不等 ⇒ 那个提交不是发版点），与上面那条（文档里的 sha 与 tag 是否一致）**是两个问题、两副牙**，不要并成一个检查器。

**一条支撑事实（本段自己量的）**：**移动 tag 在本地不留痕迹** —— `git reflog show v0.5.2` 返回空（tag 默认不写 reflog）⇒ 除了「文档里的 sha ≠ 实际的 sha」，没有任何东西会告诉你它动过。**这就是为什么这一类必须由检查器看，不能由人看。**

**E 的变体二：换动作者。** E 段里「被量的东西被别人移动了」的动作者一直是人。本轮出现了一个**动作者不是人**的形态（agent2 判、我复核成立）：`docs/BOARD.md` 那行引 `stream/range-semantics` @ `30ee4d0`，而一条在我要求到达之前跑下的 `git merge --ff-only main`（在 `.worktrees/range-semantics` 里）**宾语是「我的检出」，副作用却落在被引用的那条 ref 上**（`30ee4d0` → `58f1a44`，reflog `{1} merge main: Fast-forward`；已由 `git branch -f` 复原）。

⇒ **被移动的是一条 ref，动作者是一条以别的东西为宾语的维护命令。** 而**文档没有变假** —— 那一行点名的是**提交**（`30ee4d0`），ref 移动不改变那句话的真值（`git merge-base --is-ancestor 30ee4d0 HEAD` 仍 rc=0）⇒ **E 的 near-miss，而且是纪律起了作用的 near-miss**：那一行活着，正因为写的是提交而不是分支 tip。

> **一般式：`git merge --ff-only` 在一支已合并的分支上，不是「同步我的检出」，是「移动那条分支的 ref」。** 判据：一条维护命令的**宾语**与它**会改到的 ref** 是不是同一个东西；不是，就得在跑之前点名那个副作用。


### F. 第五种：坐标从来没对过（不是过期，是从未正确）

A/B/C/E 四种**都是「坐标过期」** —— 写下时对、后来变了，或写下时就不自洽。**F 不是**：上面 ★ 列表第 4 条引 `tools/gate.sh:768` 当作 `if [ "$sels" = "all" ]` 那一行，而实测 `:768` 是它下面 `for s in $sels` 里那一行、`:766` 才是。**`tools/gate.sh` 自 `4ca013d` 起一个字节没动**（`git diff --name-only 4ca013d <head> -- tools/gate.sh` 为空）⇒ **那个号写下的那一刻就是错的，从来没有对过。**

**它不满足 H4.1 判定①②的任何一条**：旁边没有产出它的命令，也不是从同一份输出派生的 —— **旁边只有「`4ca013d` 上」这个 ref，而 ref 保证的是「当时是这棵树」，不保证「这棵树上那个号对」。** ⇒ **带 ref 能治「过期」，治不了「从未正确」。** 两者的判据不同：过期靠「同一个号在两棵树上是否一致」发现；**从未正确只能靠把号对着内容再量一次**。

**修法不是把 `768` 改成 `766`** —— 下一笔又会漂。同一节 ★10 逐字已经写了处置：「在 `tools/gate.sh` 上不再写裸行号、改写成锚……只有上面那张映射表保留行号，因为那张表的主题就是行号，且整表带 `4ca013d`。」**而第 4 条是列表正文、不是那张映射表，它裸写了四个 `tools/gate.sh:<N>`** ⇒ 处置 = **换成文本锚**，并把「原写的那个号从来不对」就地记下来（改对它等于抹掉这个事实）。

**覆盖面那一课（复核方自领）**：这一处由 `4fe8850` 引入，而那一轮独立复核报的是「映射表 14 个行号逐个量过全中」—— **这一处不在映射表里，在第 4 条正文里。** ⇒ **「逐个核过」的覆盖面又比说出口的窄了一次**，与它上一轮 `CMakeLists.txt` 前缀式普查**同一形状**：**核的是「我以为的那份清单」，不是「所有该核的」。**

**F 的全量普查（复核方做，在 `182879e` 上）：零处新增** —— 除已修的 `:768`，`tools/gate.sh` 上的行号引用一处不差（映射表左右两侧、`:3359` 的三个、以及 ★ 列表各处，逐号对内容量过）。

**而它同时证伪了我提的输入集，错的方向正是这个陷阱的镜像。** 我提议的输入集是「所有裸写的 `tools/gate.sh:<N>`」= **4 行**（其中两行是记录）；**它会漏掉 `docs/AUDIT.md:3347` 与 `:3352` 这两行** —— 它们引了五个 `tools/gate.sh` 行号，**而整行不出现 `gate.sh` 三个字**（是映射表的行，合法：表前一句写着「下表全部在 `4ca013d` 上量」）。⇒ **一个按文件名建的清单，连该量的对象都点不出来** —— 而 F 的判据恰恰是「从未正确只能靠把号对着内容再量一次」。
⇒ **正确的输入集是按目标建、不按文件名建**：把 `tools/gate.sh` 里的东西列出来（`EXP_CTEST`、`stage_ctest`、`stage_oauth_loop`、`check_test_ports.py` …），找每一处挨着它们的 `:NNN`。**这与本节前面那条 `docs/AUDIT.md:1807` 的第五例同族，只是镜像：「谁引了这个目标」，不是「谁提到这个文件」。**

### G. 第六种：输入是渲染（从显示值反推出来的数）

**这是这一族里唯一一种「长得很像测量值」的失效。** 实例：`98.7%` 是**渲染**（三位有效数字），从它反推精确字节数得 `5633`，而真值是 `5520`：

```
2097152 × 0.987 = 2069889   ⇒ 余 27263        （真 27376，差 113 B）
printf '%d %d\n' $((0x601000-0x407af0)) $((0x200000-0x1f9510))   →  27376 5520
```

⇒ **从显示值反推出来的数，精度上限是显示值的精度，不是树的精度。**

**它不可由读文本发现**：`5633` 长得跟一个测量值一模一样（四位数、量纲对）。⇒ 判据不能是「别这么干」，必须是**产出它的那个形式**：

> **涉及余量的数，给出两端，不给百分比。** —— **百分比是渲染，余量是测量；渲染是单向的。**

**A 段收口句把三种并列时，它们不在同一个轴上。** ①（过期）②（从未正确）是**坐标轴**（这个号指着哪棵树 / 它有没有指对过）；**B 段（`106`）与本节（③）是推导轴** —— B 是**筛选条件错**（命令对、口径错），③ 是**输入是渲染**（口径对、输入不是原物）。⇒ **带 ref 治的是坐标轴那一半；推导轴那一半，ref 一点用都没有。** 两轴必须分开说，否则「带 ref」会被读成一个通解。

### H. 第七种：现值 + 无人站岗

**A–G 说的是「这个数是怎么错的」，H 说的是「这个数错的时候谁会说话」** —— 它比 A–G 更接近根，因为它解释了**为什么同一份文档里有的数活了、有的数烂了**。

实例（agent4 找到、我复核成立）：`docs/DECFY_DESIGN.md` 有一句普查同时写下四个数（`vtest/*.im` 的总数、其中未被 `CMakeLists.txt` 提到的数、`tools/*.test.py` 的数、`tools/*.test.js` 的数），紧接着说「今天都是 0 孤儿」。实测在 `main @ f307b23` 上：孤儿检查器 rc=0、四个数里**烂了三个**（总数、未被提到的数、`*.test.py` 的数），只有 `*.test.js` 那个没变。

**烂的恰好是没有东西站岗的那三个，而没烂的那一半有门禁站岗**：`tools/gate.sh` 里 `orphan-fixtures` 那一行的标签逐字是 `test inputs that no CTest runs (expect 0)` ⇒ **它过期时会变红，不会静默变假。**

> **判据：现值 + 站岗 = 安全（它自己会喊）；现值 + 无人站岗 = 病（它会静默变假）。**

⇒ 这条把 E 段那个无限回退**收进了一个可判定的问题**：不是「能不能写现值」，而是**「这个现值变假时，谁说话」**。站岗者是 `tools/` 里的检查器、是 `--required-for` 的派生、是门禁的 `expect` 标签。**A/B/C 三种之所以不能自动变红（E 段结论三），正是它们没有站岗者；而 E 段那条能进 `tools/`，正因为有。**

**推论（可执行）：给一个现值配一条命令还不够，得配一个会在它变假时变红的东西；配不出来的，就在它旁边写明「这个数会烂，烂的时候没人说话」。** §1.76 末尾那句就是这么写的。
**H 的伴生形态：一个测试可以因为错误的原因而通过，而它打印的是对的东西。** ivory-ember 本轮在自己的 T2 里踩到：值栈约定是 1 个实参 `< 0`、2 个 `< 1`、3 个 `< 2`，它的守卫写成 `< 1`/`< 2` ⇒ 守卫提前 `return 0`、**不压结果** ⇒ 调用方读**陈旧寄存器** ⇒ **得到一个形状正确的错答案**。而第一版 fixture **看不见它** —— 那时值栈已经够深，`.declared` 恰好打印出正确答案。只有把它放在**声明之后的第一个语句**才暴露。⇒ **H 说的是「没人反驳」；这一条说的是「反驳看起来通过了」。** 两者都不需要作者更小心，需要的是**让那个值的位置不依赖偶然的栈深**。


### I. 第八种：按形状定的规矩，用一份按文件列的豁免去执行

**这一种不在「数」上，在「检查器」上** —— 而它由 agent2 新写的 `release-tags` 检查器**自己抓出来**（它的第一个真捕获，**判词却是假的**）。

原样判词：`check_release_tags: docs/STATUS.md:42 records \`v0.5.2\` -> \`0ebd68d\`, but \`git rev-parse --short 'v0.5.2^{commit}'\` is \`322c085\` today -- the tag moved and the document did not`。而那一行**逐字是记录**：「本格初稿写过 `v0.5.2` → `0ebd68d` ……：前者在 tag 被移动后失效」，**同一行**还写着「**记录一个曾经的绑定，不能写成绑定的形状**」⇒ **文档跟上了移动，检查器说它没跟上。**

**根因：豁免是按文件认的，而规矩是按形状定的。** 豁免表里列的是两个文件，而家规说的是「**一条曾经的绑定不能写成绑定的形状**」—— **一条按文件列的豁免表永远追不上一条按形状定的规则**：任何别处新写一条记录形状的绑定，都会被当成现值。

⇒ **判据：一条按形状定的规矩，它的豁免键也必须是形状**（行内自述「记录 / 初稿写过 / 曾经的 / `[obs:`」），**不能是文件名清单**；并且**豁免必须要求同一份文档里出现它的证伪者**（这正是 C 的实例条件）。**可红负向对照**：一条**裸的、不带记录标记**的绑定形状引用**必须红**；一条**带记录标记**的必须绿 —— 否则「豁免」与「闭嘴」不可区分。

### 「更正的作用域」是一个新形状，不是 C 的实例

**agent2 的判定、我复核成立。** 同一句断言在四处出现（`docs/AUDIT.md`、`docs/RELEASE_0.5.1.md`、`docs/STATUS.md`、`docs/BOARD.md`），而撤回那一笔只动了前三处，**`docs/BOARD.md` 那第四份拷贝原封不动活到很久以后**。

⇒ **C 的实例要求同一份文档同时含着断言和它的证伪者**；这里证伪者在**别的文档**里、撤回的触及范围是**逐文档**的 ⇒ **「更正的作用域」是一条新形状：一个断言的每一份拷贝都是一个独立的目标，撤回其中一份不会让其余三份变成「已经撤回」—— 它们只是继续假着。** 处置：撤回时按**断言的拷贝**枚举，不按**文档**枚举。

### 第六个实例（本节作者本轮犯的）：读的是历史版本，评的是当前文档

我要求 agent4 修 `docs/DECFY_DESIGN.md` 里「`8d28c60` 在 `stream/sayout-ignore` 上、尚未合进 `main`」这半句，理由是我在 `main` 上写下了它。**它不在 `main` 上** —— 那是 `7daf145` 的版本，agent4 在 `9826a5c`（正是我当时要求的那一轮）已经换成了命令形态；`git show origin/main:docs/DECFY_DESIGN.md | grep -c '尚未合进'` = **0**。

⇒ **判据：引用一句要修的话之前，先确认那句话还在被引的那棵树上（一条 `grep`）。** 这与「引的是指针的当前值」、`docs/BOARD.md` 那两行同 slug 的拷贝同形，入口不同：**那两处是「读错了树」，这一处是「读对了树，但读的是它的过去」。**

**agent4 的另一半才是真目标**：那半句里「在分支 `stream/sayout-ignore` 上」**也是现值**（该支 tip 已从 `8d28c60` 前进到 `458fa29`）。它的修法值得抄：**两个问题各问一条命令**（①条目在不在：`git show <你读的那棵树>:.gitignore | grep -cxF '/vtest/sayout.txt'`，0 = 尚未收录 / 1 = 已落地；②那一笔进没进：`git merge-base --is-ancestor 8d28c60 <你读的那棵树>`，rc=0 即在 / rc=1 即不在），**分支位置降级为记录**，两个观测点都实测（`58f1a44` → rc=1 / 0；`d27413a` → rc=0 / 1）。**它没有把它改成「已合进 `main`」** —— 那是同一个病换方向（下一次 revert 或丢弃分支，照样静默变假）。

### 第七个实例（agent4 找到，在 `docs/DECFY_DESIGN.md` 里）：一个关于会生长的文件的「记录」，写成现在时就会被文件长大变成断言

§9 里有一条一般式，用来解释「`docs/AUDIT.md:3851` 指向一个不存在的行」的框架句**在写下时成立**（当时 `docs/AUDIT.md` 是 **3849** 行，`:3851` 越尾）；而 §1.76 把该档插到 **3887** 行之后，**`:3851` 存在了** —— 而且它指向一个**空行**（观测点 `main @ ce403cb`）。

⇒ **反例的前提自己过期了，反例随之不再是反例。** 引文本身必须逐字留（那是那条一般式的唯一例外：**引用且标明自己在引用**）；**过期的是框架，不是引文** ⇒ 修法是给框架加观测点，不是改引文。

**而它过期的方式比「号越尾」更坏**：`:3851` 现在**解析得到**（落到一个空行）⇒ **一条「这个行号存不存在」的检查会放它过去**。⇒ 与 A 段同族：**判据的射程（行存在）小于断言的内容（那一行说的是什么）**。 **★ 而这句更正自己也烂了 —— 第三代（`exact-otter` 自首，`main @ a7ce370`）**：上面「落到一个空行」那个值是在 `main @ ce403cb`（**3887** 行）上读的；到 `main @ a7ce370`（**4000** 行）同一句里的 `:3851` 落到的是 `| `c++` | 23.1 – 23.2 MB |` —— **§9 那张内存表的中间**。⇒ **一条更正也是一条记录，所以它带着与它更正的东西同一个失效模式。** 「越尾 → 空行 → 表中间」三次都是真的读数，而三次都被写成了**关于一个会生长的文件的现值**。⇒ **修法不是写「它现在落在哪一行」（那是第四个现值），而是给命令**：`git show <你读的那棵树>:docs/AUDIT.md | sed -n '3851p'` —— 读到的任何一行由读者自己判定；并把三种可能的读数各自映射到它的含义（**越尾 = 该号不存在；空行 = 该号存在但不指任何东西；有内容 = 该号存在且指着一个东西，而它是不是你要的那个要另判**）。 **这一条比前面所有实例都硬的地方**：它证明**「加观测点」不足以救这类句子** —— 观测点救的是「我知道它当时是什么」，救不了「读者现在读到的是另一个值」。**能救的只有「把值换成命令」。**

**★ 而这一代还教了半步（`noble-zephyr` 抓到，我认）**：上面那个「`| `aot` | 23.0 – 23.1 MB |`」是**错的** —— 它是 `:3850`，`:3851` 是 `| `c++` | 23.1 – 23.2 MB |`（三棵树 `32771cf`/`a7ce370`/`6c93c5b` 逐字相同）。根因：我用 `sed -n '3849,3853p'` 读了**五行窗口**，然后**从窗口顶端数起**，把第二行当成了 `:3851`。⇒ **前三次是「漂移」（文件长大、同一个号落到别处，读数一直是对的），这一次是「量错」（读数当场就不对）。** ⇒ **那条一般式要再走半步**：

> **一条更正既可能因为「时间」而烂（漂移），也可能因为「它自己就是一次测量」而烂（量错）—— 而后者不会随时间变好，它一开始就是错的。**

**而抓它的工具本来就在同一段里，只是没绑 ref**：我原先给的 `sed -n '3851p' docs/AUDIT.md` **读的是工作区**，所以它答的是「你这棵树的那一行」，不是「那棵树的那一行」—— **这正是 §1.74 A 段自己立的规矩，我在同一段里违反了它。** 绑上 ref 之后（`git show <ref>:docs/AUDIT.md | sed -n '3851p'`），**这一次的量错当场就会被这条命令抓到** —— 它自己就是判据。⇒ **「把值换成命令」还不够，命令本身也必须绑在它要回答的那棵树上。**

**与第六个实例的关系**：第六个管**读者**（读的是历史版本、评的是当前文档）；这一个管**时间**（文件长大，把一条关于它的记录变成一条断言）。两条应当挨着写。

### 证明自己在扫描集里，靠分母动了，不靠结果是绿的（agent3 本轮）

一份新文档落地后跑那三个文档阶段：`links` 的 markdown 文件数从 **101 变 102**、`text-integrity` 的文本文件数从 **824 变 825** —— **两个差值都是 1，正好是那一个文件** ⇒ 它确实落在两个扫描集里，那两行绿是它自己的绿。而 **`doc-paths` 扫不到它**：`tools/check_doc_paths.py` 的模块 docstring 明写 `skipped  docs/streams/`（理由逐字：internal per-stream work orders, not delivered docs; they quote broken paths *on purpose*）⇒ 那一行的 `0 broken` 对这份文档是**空绿**（与 §1.75 的 G 段同族）。

⇒ **判据：一条绿色的读数只对「它的扫描集」成立；要证明你在集里，靠的是分母随你动了一格，不是靠那行印着 `0`。** 这也把「空绿」从一种**事后才发现的事故**变成一种**事前就能排除的状态**：落地一件新东西时，先看它把哪个分母推动了；推不动任何一个，你就还没被任何东西看过。

**分子 / 分母：这条判据对「新文件」与「已在集内的文件」是两个不同的读数（agent3 自己推的，我确认）。** 新文件落地时动的是**分母**（markdown 文件数 101 → 102、文本文件数 824 → 825）；而**已在扫描集内**的文件里新增引用时动的是**分子**（`links` 的链接数 586 → 588、`doc-paths` 的反引号引用 932 → 936），**分母一格不动**。⇒ 完整判据：**看那个读数里属于你的那一格有没有动 —— 新文件看分母，新引用看分子。**

**它的副产品更值，而且它是一条能纠正上一句的句子**：**同一份交付里可以一个文件真绿、一个文件空绿。** 同一笔里 `docs/BOARD.md` 落在 `doc-paths` 的扫描集内（`README.md, docs/*.md, future/*.md`）⇒ 那一行 `936 refs / 0 broken` **覆盖了**它新加的那四个引用；而 `docs/streams/win-source-attribution.md` 不在集内 ⇒ 对它仍是空绿。⇒ **「这份交付绿了」不是一个能对整份交付说出口的句子，只能对文件说。** 这与 C 段（同一份文档里断言与它的证伪者）同族：**都是「一个结论的作用域被说得比它实际覆盖的范围大」。**


### 第八个实例（ivory-ember 本轮）：一个既有缺陷，被新特性搬到了一条新路上

`src/compiler/compiler.c:1209` 的 `return -1;` 是一个**既有**缺陷：对象不是 `EXPR_IDENT` 且成员名不在元属性表里时，`compile_expr` 返回寄存器号 **-1**，而**调用方不检查** ⇒ 旧树上 `arr[0].bogus`、`id(x).bogus` 就是段错误（RC=139）。

**而 T2 让这个缺陷在新路上可达**：`arr[0].declared` 在旧树上**写不出来**（`.declared` 是新加的元属性），在 T2 之后是一个**语法合法的程序**，它照样 RC=139。⇒ **没有任何一条检查是因为这个新名字而重新跑过的** —— 既有缺陷在旧路上不可达，在新路上可达，而「新路上有什么」这件事**不在任何判据的射程里**。

⇒ **判据：加一个新语法/新名字时，要问的不是「它自己工作吗」，而是「它把哪些既有路径变成可达的」。** 处置（本轮）：**新路上不得带崩溃**（T2 里修调用点，并给一条修前 RC=139、修后干净报错的可红对照）；**旧路上的那个口子只登记、不顺手修** —— 它需要自己的红色对照，顺手修会让两条改动的证据混在一起。

### 本节自己用的方法：按形状搜邻域，不按值搜

**修一处同族病时，把那一处的邻域按同一种形状再搜一遍** —— 理由不是玄学：**同一个作者在同一天、用同一种方式、写了很多句。** agent4 把它跑成一条不看值的命令（按 `[0-9]+ ?(citations|处|行|个|条|次)` 这种形状 grep），当场又抓出第五个实例（H 段那个）。**按值搜得先知道那个值，而那个值正是这族病让你看不见下一处的原因。**

**而形状搜索是过滤器，不是证明**：它证明「找到了 N 处」，**不证明「只有 N 处」** —— 会漏并列计数、漏非计数（sha、日期）、漏中文数字与单词数。⇒ **它的输出是候选集，逐处仍要按「站岗」（H）判定。**

**两条来自 Windows 侧的读数纪律（同族、入口不同）：**

1. **一个 ref 在被读的那一侧不存在时，不要「换一棵树去读」，要先把那棵树的字节导出来再读 —— 导出之后必须用一个与该 ref 绑定的量重新钉住身份**（agent3 用 `git archive` 导出 `ab70a71` 再读，并用导出后的 `add_test(` = 142 钉身份，因为导出树没有 `.git`）。它与「不要给读者一条没在目标树上验过的命令」互补：**那条防读错树，这条防读不到时随手换树。**
2. **`ctest -N` 印的 `Total Tests:` 是注册表的读数，与「这些测试能不能跑」无关** —— 它在**不检查可执行文件是否存在**的情况下照样列全并印出总数（Ninja 多配置把路径推成 `…/Development/inimerse.exe.exe`，它照样印）。⇒ **「注册上限」与「实跑数」是两个量，任何把它们写成同一个数的句子都要拆开。**
3. **「工具说没有」与「确实没有」之间，隔着一次「我给它的路径形式它读不读得懂」的检查**（agent3 本轮两处）：`nm.exe`/`objdump.exe` 是 Windows 程序，**给 `/mnt/d/...` 形式会得到 `No such file`，而 `nm --defined-only` 只输出一行 —— 看上去像「符号不存在」**；该导出树的对象文件后缀是 **`.obj` 不是 `.o`**，用 `.o` 去找会得出「12 个文件全缺失」的假结论。⇒ 与 A 段同族、入口不同：**A 防「换个 ref 得数不同」，这一条防「换个路径形式得『没有』」。一次筛选取不到东西，先怀疑筛选条件，再怀疑世界。**


### 封闭性：只有 git 跟踪的文件能 holding 一个号

**agent2 的读数（已写进 `tools/README.md`）**：行号引用的「持有者」集合**只有 git 跟踪的文件**。两个观测（同一份清单、两棵树）：带 `build/` 的树上 **520 held / 515 unanchored**；它的 depth-1 clone 上 **514 / 521** ⇒ **六个号被 clone 里根本不存在的产物撑着**。⇒ **一个号能不能被某个东西撑住，取决于那棵树里有没有那个东西** —— 与 A 段同族（同一命令在两棵树上跑得数不同，这个数就必须带 ref），只是这里差的是**工作区里有没有未跟踪的产物**。

**并且 pin 也会被「没人碰过那行」的改动改掉**：一次 `git merge main` 把强形态引用数从 **694 变 720**，而那行没人碰过。⇒ **「这行没人碰过」不是「这个数没变」的理由** —— 这个数派生自整棵树的内容。
**它的自指版本（agent2 本轮给出）**：`tools/check_line_refs.py` 的 pin 是在 `56bf4c5` 上取的（explicit 720 / unanchored 521），而那**之后**该支还有五笔提交 —— 也就是说**移动这个 pin 的是写这个 pin 的人自己的后续提交**，没有任何外人 merge main。⇒ **一个 pin 的作者与一个 pin 的破坏者可以是同一个人，而中间隔着的只是「我后来又改了那个文件」。**

**而且它有一个可复现的机制，值得单独记**：agent2 那支给门禁加了两个阶段，`tools/gate.sh` 因此整体位移约 250 行 ⇒ **`docs/` 里所有 `tools/gate.sh:<N>` 形式的引用同时失去内容锚**。隔离 worktree 实测（同一脚本、同一台机器、只有树不同）：合之前 `explicit 730 / unanchored 506`，合之后 `explicit 734 / unanchored 530`。⇒ **一次合并可以把一个 pin 打红，而它红的原因不是「有人写错了数」，是「有人移动了那个数所指的文件」。** 这正是 `line-refs` 这条 pin 存在的理由 —— **没有它，那 24 处失去锚的引用不会有任何东西说话。**



## §1.75 `text-integrity` 的 allow-list 里有三个不可达条目，而 89 个受管文件在它视野之外

**症状。** `tools/check_text_integrity.py` 用**允许清单**（allow-list）决定哪些文件归它管（`:55` 的 `TEXT_SUFFIXES` 加 `:93` 的 `TEXT_NAMES`）。清单里写着 `.gitignore`、`.gitattributes`、`.editorconfig` —— **这三个条目永远不会匹配任何文件**：

```
$ python3 -c "import os; print(os.path.splitext('.gitignore'))"
('.gitignore', '')
```

`os.path.splitext` 对一个**以点开头、且点后再无点**的名字返回**空后缀**（那是隐藏文件，不是扩展名）。判定在 `:125-127`：先查 `path.name in TEXT_NAMES`，再查 `path.suffix.lower() in TEXT_SUFFIXES` —— 两条都不成立。⇒ 本仓库里**真的叫这个名字的 5 个文件**（`.gitattributes`、`.gitignore`、`Infiverse_standard/oauth_loop/.gitignore`、`Infiverse_standard/src-tauri/.gitignore`、`tools/dsh-inimerse/.gitignore`）**一个都没被检查过**。要匹配它们得走 `TEXT_NAMES`，或者把判定改成「先比全名、再比后缀」。

**规模。** 用 `git ls-files -z`（**必须带 `-z`**，见下）：

```
受管文件 913 ；在 text-integrity 之外 89
  .ps1      19   ai_build.ps1, build.ps1, build_asan.ps1, …        ← 全是构建脚本，是文本
  .png      14   Infiverse_standard/src-tauri/icons/128x128.png …  ← 二进制，应当在外
  .inim      9   nst2.inim, projects/exc_test.inim …               ← 文本
  无后缀     8   .gitattributes, .gitignore, …（其中 5 个就是上面那批）
  .params    8   bad.params, game.params, params.params            ← 文本
  .bmp       5   examples/assets/monster8.bmp …                    ← 二进制，应当在外
  .tpl       5   templates/README.tpl, templates/main.tpl …        ← 文本
  .lock 2 / .ico 2 / .def 2 / .st 2 / .vverse 2 / .icns 1 / .gradle 1 / .bat 1 / .java 1 / .svg 1 / .manifest 1 / .dev 1 / .dev28 1 / .dev29 1 / .hold5 1 / .orig 1
```

⇒ **二进制那几类（`.png` `.bmp` `.ico` `.icns`）在外是对的**；**`.ps1` `.inim` `.params` `.tpl` `.st` `.def` `.bat` `.java` `.manifest` `.gradle` `.vverse` `.lock` 在外是缺口**（合计 50 个文本文件），加上三个不可达条目覆盖的 5 个，**这份清单今天看不见 89 个受管文件里的 55 个文本文件**。

**这一条是被 `--required-for` 自己暴露的**，不是被读码发现的：

```
$ tools/gate.sh --required-for 751b2a0..2cf072e
gate: --required-for: no rule matches 'build_installer.ps1'; requiring every stage.
… required: 13 stage(s)
```

**这里必须用区间形式。** 单参 `--required-for HEAD` 是「rev vs 工作区」，它只在工作区里恰好有未提交改动时才复现上面那个数 —— **在干净树上它答 `required: 0 stage(s)`，还带 rc=0**（最危险的那个答案）。**本节最初写的就是单参形式，而那正是 §1.74 A 段的形态（文档给了命令、命令复现不出文档里的数）—— 它出现在隔一节的地方，被一份独立复核（ivory-ember 对 `0155baf`）抓到。** 区间形式只从 ref 派生，干净树上也复现；`2cf072e` 改的 5 个文件里就有 `build_installer.ps1`，所以命中的正是同一件事。

**它的保守回退是对的** —— 没人定过规则的改动就当它什么都可能影响。但根因是 allow-list 少了一个扩展名，所以**每一个 `.ps1` 的改动都会把十三阶段全拉一遍**，而**没有一个阶段真的读那个文件**。

**同一个引号陷阱，这是第三次（本节自己踩了一次）。** 同一个 `git ls-files`：

```
$ git ls-files      | wc -l   →  915
$ git ls-files -z   | tr '\0' '\n' | grep -c .   →  913
```

差的 12 个是**带空格或非 ASCII 的路径被 C 式引号转义**后的产物：`docs/archive/工作台使用教程.md` 在输出里带上引号，`os.path.splitext` 于是看到后缀 `.md"`，落出清单。**同一棵树、同一个命令、两个分母，差不是文件变了，是读法变了。**（§1.74 A 段记的是同一个陷阱在 `git ls-tree -r --name-only` 上的形态：那边给 813、加 `-z` 给 823。§1.72 的通则 A 说的是「按扩展名、不按目录」—— **这里要补一句：按扩展名，就得先保证扩展名是那么读出来的**。）

**判据（可重跑）。** ① `python3 -c "import os; print(os.path.splitext('.gitignore'))"` 给空后缀；② `git ls-files -z | tr '\0' '\n' | grep -c '\.ps1$'` 给 19，而 `text-integrity` 印出的分母里不含它们（`:57` 的清单里没有 `.ps1`）；③ 往一个 `.ps1` 里插一个 NUL 字节，`text-integrity` **不变红**（而往 `.md` 里插会红）—— **第三条是这条断言真正的牙**：前两条只证明清单里没有它，第三条证明后果。

**只登记、未修。** 修它在 `tools/**`（归 agent2 的写域）：三处最小改动是「判定先比全名」「`.ps1` 进清单」「清单里每个条目至少被一个受管文件命中（否则它要么是死条目、要么是缺文件）」。**第三条本身就该是一个检查器**，而它比前两条更根本 —— 一个永远不匹配的 allow-list 条目与一个没人跑的 `add_executable` 是同一件事（§1.73），只是它在**清单里**而不是在构建里。

**更正（`vivid-anchor` 实测，`main @ b68d6d6`；本节上面的数字作为**记录**保留 —— 它们各自带着写下时的那棵树）。** 判据 ③ **成立，而且带正对照**：往受管的 `build_installer.ps1` 的 offset 100 插一个 NUL（`git diff --numstat` 给 `-	-	build_installer.ps1`、`git diff` 印 `Binary files a/build_installer.ps1 and b/build_installer.ps1 differ` ⇒ **变异确实落进了文件**），`bash tools/gate.sh --only text-integrity` ⇒ **rc=0**、`check_text_integrity: 825 text file(s), 0 with NUL bytes.`、阶段行 `✔ tracked text files carry no NUL byte (expect 0)  PASS`；把**同一个字节**插到受管的 `AI_LAYOUT.md` 的同一处 ⇒ **rc=1**、`check_text_integrity: 1 NUL byte(s) in 1 of 825 tracked text file(s).` / `  AI_LAYOUT.md  (1 NUL)` / `    byte 100 (line 2)` / `✘ tracked text files carry no NUL byte (expect 0) (exit 1)`。**同树、同命令、同字节，`.ps1` 绿、`.md` 红** —— 差别不在文件里，在清单里。**一处口径**：`--only` 那次的**末行**逐字是 `gate: this was NOT the full gate: 13 stages are registered and only this one ran.`（`gate: OK — the selected stage passed` 在阶段表**上方**），引用时别把两行合成一行。

**而本节另外四个数今天不成立**（同一次实测，全部可重跑）：

- **「三个不可达条目」应为 15 个，且必须分成两类。** `TEXT_SUFFIXES` 实际 **33** 条（本节写 35）、`TEXT_NAMES` **4** 条。**(A) 结构性不可达 —— 3 条**：`.gitignore`、`.gitattributes`、`.editorconfig`。`Path('.gitignore').suffix == ''`，而本仓库真的叫这名字的 **5 个文件全是点文件**（逐个验过 `tracked=True`、`suffix=''`）。**但「永远不会匹配任何文件」这话太强**：一个叫 `foo.gitignore` 的文件是能命中的（`suffix='.gitignore'`）⇒ 准确说法是「**永远匹配不上点文件，而本仓库这 5 个全是点文件**」。**(B) 今天没有受管文件命中的死条目 —— 12 条**：`.bash .cc .cfg .cjs .cmake .csv .hpp .ini .jsonc .ts .tsx` 十一个后缀，加 **`Dockerfile`**。它们**原理上可达**（加一个该扩展名的文件就活了），只是今天没人用 —— **这 12 条是本节的分类没数的**。可达的是后缀 **19** 条、名字 **3** 条（`CMakeLists.txt`、`LICENSE`、`Makefile`）；19 + 14 = 33 ✓。**大小写形态**：受管文件里后缀含大写字母的 **0** 个、`TEXT_NAMES` 没有变体 ⇒ `.lower()` 与大小写敏感给出**同一份**死清单。
- **`:3709` 的受管文件总数 913 今天 914**（`git ls-files -z` 与 plain 一致；树长了一个文件）。
- **`:3737-3738` 的 `915` vs `913`、「差的 12 个」今天复现不出来**：`git ls-files | wc -l`、`git ls-files -z | tr '\0' '\n' | grep -c .`、`git -c core.quotePath=false ls-files | wc -l` —— **三个读法全是 914，差 0**。而且**它自己的算术也不自洽**（915 − 913 = **2**，不是 12）；**机制上也不成立**：非 ASCII 路径 **10** 个、含换行的路径 **0** 个，而 C 式引号是**转义**（`\345` 这类）、**不产生新行** ⇒ 两种读法的**行数本来就该相等**。**「同一棵树、同一个命令、两个分母」这句话本身对，但它举的这个实例今天复现不出来** —— 最可能是把 `git ls-tree -r --name-only` 那个形态（§1.74 A 段：813 vs 823）串了行。**不替它圆：今天就是 914 / 914 / 0。**
- **`:3720` 的算术与分类**：它列的 12 类**实际相加 53**（本节写 50），53 + 5 = **58**（本节写 55）；**`.inim` 被误分类** —— 9 个里 **1 个文本、8 个二进制**，而 `tools/check_text_integrity.py` 的 docstring 逐字写着 `.inim` 「is a serialised program, not source」、**out of scope by construction** ⇒ 它本来就不该在缺口名单里。用一条显式判据（**无 NUL 字节且能按 UTF-8 解码**）把 89 个逐个分类 ⇒ **视野外 text = 59、binary = 30**；二进制 30 = `.png` 14 + `.bmp` 5 + `.ico` 2 + `.icns` 1 + `.inim` 8 ✓ 恰好对上。⇒ **`89` 这个数是对的，`55` 这个数不对，真值是 59**；而且**构成**也不一样 —— 本节漏了 `mods/debug/mod.st.{dev,dev28,dev29,hold5,orig}` 这 5 个与 `.svg`，多算了 `.inim` 的 8 个二进制。

**可重跑判据（本节那条的加强版）**：用 `git ls-files -z`，并**直接 `import` 那份清单本身**（`sys.path.insert(0,'tools'); import check_text_integrity as cti`）—— 清单改了、判据跟着改，不会脱节；再把每个视野外文件按「**无 NUL 且 UTF-8 可解码**」分类。⇒ **这一节的病与 §1.74 H 同形：`55`、`50`、`913`、`915`、`三个` 都写在正文里，而站岗的只有 `0 NUL` 那一条。**

## §1.76 那张「核心高频内建」名单缺平台标注：四个名字、三种归属、零覆盖

**症状。** `docs/SYNTAX.md` 的「**核心高频内建**（有 `vtest` 覆盖的）」那一行把 Windows 独有、POSIX 独有与两平台共有的内建名并列，读者无从判断一个名字在哪个平台上存在。本节把那一行里**四个**名字逐一定案。（同族的规模登记见 §1.71：`src/mod/io_mod.c` 只在 `CMakeLists.txt` 的 `if(WIN32)` 源码分支里被编译，POSIX 侧给它的是 `src/runtime/runtime_posix.c`。）

**实测（`main @ d27413a`，Linux，`build/inimerse --no-mods`，每个名字一条独立脚本，逐字）。**

```
file_exists("/tmp")       → rc=1  [exception] uncaught: unknown builtin function 'file_exists'
timer_ms()                → rc=1  [exception] uncaught: unknown builtin function 'timer_ms'
mkdir("/tmp/probe9/xx")   → rc=0  true
list_dir("/tmp")          → rc=0  probe9
```

**生产点（`git grep -n '"<name>"' -- src/`，逐字）。**

| 名字 | Windows（`src/mod/io_mod.c`） | POSIX（`src/runtime/runtime_posix.c`） | 归属 |
| --- | --- | --- | --- |
| `file_exists` | `:318` | — | **仅 Windows** |
| `timer_ms` | `:337` | — | **仅 Windows** |
| `mkdir` | `:319` | `:1183` | **两平台，两个生产点** |
| `list_dir` | — | `:1184` | **仅 POSIX**；Windows 上注册的是**另一个名字** `io_list_dir`（`src/mod/io_mod.c:323`） |

⇒ **`list_dir` 不是「同一个名字两个生产点」** —— 它是 POSIX 独有，Windows 那个是另一个名字。**`io_mod.c` 也不是「POSIX 上是空桩」** —— 它有三个真实实现（`builtin_file_exists`、`builtin_io_list_dir`、`builtin_timer_ms`），只是**在 POSIX 上根本没有被编译**。这两句是 `docs/RELEASE_0.5.2.md` 登记这一行时的原话，已在 `main` 上就地更正（更正后**只有那句结论不变**：缺平台标注）；冻结的 tag 那棵树保留原文。

**「有 `vtest` 覆盖」对这四个名字全不成立。** `git grep -c '<name>' -- vtest/` 对 `file_exists` / `timer_ms` / `mkdir` / `list_dir`（以及 `io_list_dir`）**全 0 命中** ⇒ 那一行开头的括号对这四个名字是假的，而它同时是「有覆盖」这个断言唯一的出处。`docs/SYNTAX.md` 那一行已就地加括注（**行数中性**：加行会移动 `docs/TYPESET_V06.md` 引的 `docs/SYNTAX.md:596` 与 `:805`）。

**顺带更正一处引用（同族、另一种形状）。** §1.71 末尾那段「顺带一条未核对的观察」把「`file_exists` `mkdir` `io_list_dir` `http_get` `clipboard_set` `timer_ms` `exec_async` `proc_list`」八个名字记成是 `docs/SYNTAX.md` 那一行列出的，并断言「实测在 POSIX 上这些名字**全部不存在**」。**两句都错，而且错的方向不同：**

- **名单不对**：八个名字里只有三个（`file_exists`、`mkdir`、`timer_ms`）真的在那一行上；`io_list_dir`、`http_get`、`clipboard_set`、`exec_async`、`proc_list` 在 `docs/SYNTAX.md` **全篇零命中**（按名字逐个 grep 反引号形式，五个都为空）。⇒ **引用被当成了答案**：一个名字列表被挂到一行上，而那一行没写这些名字。这与 §1.46 那条「引用被当成了答案」同形，只是这里的「答案」是一份名单。
- **「全部不存在」不对**：同一批名字里 `mkdir`（POSIX `src/runtime/runtime_posix.c:1183`）与 `http_get`（`:1185`）在 POSIX 上**都有注册**。实测：`mkdir("/tmp/probe9/xx")` = `true`（rc=0）、`http_get("http://127.0.0.1:9/")` = rc=0。另四个的实测逐字是 `[exception] uncaught: unknown builtin function 'io_list_dir'` / `'clipboard_set'` / `'exec_async'` / `'proc_list'`（各 rc=1，尾行 `at ip=N frames=0`）。⇒ **那六个确实在 POSIX 上不存在**（`file_exists`、`timer_ms`、`io_list_dir`、`clipboard_set`、`exec_async`、`proc_list`），**但那两个存在** —— 「全部」这个词把六个的结论借给了两个反例。那段自称「未核对的观察」，而它错的恰好是**核对一下就会翻的那两句**。

**未裁定（留给下一次）。** 那一行还有 50 多个名字没逐个核平台与覆盖（§1.52 只核过 `random`/`rand`，§1.53 只核过 `substr`）。本节**不动名单本身**，只给那四个名字加括注、并在行尾指向本节。

**判据（可重跑）。** ①四个名字各跑一条单行脚本，rc 与首行输出与上表逐字一致；②`git grep -n '"<name>"' -- src/` 给出上表的生产点，且 `list_dir` 的 Windows 侧为空、`io_list_dir` 的 POSIX 侧为空；③`git grep -c '<name>' -- vtest/` 对五个名字全 0；④按名字在 `docs/SYNTAX.md` 里 grep 反引号形式，`io_list_dir` 为空（证明那段登记里的名单不是那一行的抄录）。

**这一节自己什么时候会变假。** 四个名字的**归属**由 `src/` 的注册点与 `CMakeLists.txt` 的源码分支决定，不随文档编辑改变；而**「零覆盖」那一半会变** —— 任何人补一条 vtest 就翻，而它**没有站岗者**。所以它按「现值 + 无人站岗」登记：**数在这里、命令在这里、它什么时候会烂也写在这里**（§1.74 E 那条判据的实例）。


## §2 执行通道效率比较

### 2.1 五条通道

| 通道 | 命令 | 说明 |
| --- | --- | --- |
| `interpreter` | `inimerse <wl>.im <N>` | 字节码 VM |
| `aot` | `aot-native translate --extern N` → C → `cc -O2` | 原生后端（转译器，不是 JIT） |
| `wasm` | `inimerse compile --abi-target wasm` → `node tools/wasm_run.js` | WebAssembly 后端（本轮新接入） |
| `c++` | `g++ -O2` | 静态类型基线 |
| `rust` | `rustc -O` | 静态类型基线 |

**AOT 通道必须用 `--extern N`**。不加时宿主 C 编译器看到的是常量工作负载，会把整个循环在编译期算完 —— `src/compilation/aot_native_tool.c` 的 usage 原文：*Without this the host compiler sees every input as a constant, evaluates a constant workload at compile time, and any speedup measured against the binary is fiction.* 工具另加断言：生成的 C 里必须出现 `extern NV g_N;` 与 `nv_program_main`。

**wasm 通道不能传 argv，所以它把 `N` 烧进模块里。** `tools/wasm_run.js:103` 调的是 `inst.exports.inimerse_run(0)`（不传参数），而 wasm MVP 子集**没有 `args()`** —— 模板首行 `N = int(args()[0])` 会让五个工作负载全部报 `error: wasm MVP subset: function 'int' not found (builtins are not in the wasm MVP subset) (line 1)`。因此 wasm 通道把绑定行**替换成字面量**再重新编译。这带来一个结构后果：通道不能再被当作「固定 argv」，`tools/perf_channels.py` 里每条通道都是一个 `rebuild[chan](m) -> (argv, err)`，解释器/原生通道只是换 argv，**wasm 通道重新编译**。缩放闸门也随之从 `argv[:-1] + [str(n2)]` 改为调用 `rebuild`。

### 2.2 结果（五通道，`--reps 5`，中位数）

| 工作负载 | N | interpreter | aot | **wasm** | c++ | rust |
| --- | --- | --- | --- | --- | --- | --- |
| `arith` | 20,000,000 | 0.845s (1.00x) | 0.005s (**155.07x**) | 0.310s (2.72x) | 0.006s (151.68x) | *被缩放闸门拦下* |
| `branch` | 10,000,000 | 0.966s (1.00x) | 0.078s (12.42x) | 0.450s (2.15x) | 0.033s (29.34x) | 0.034s (28.56x) |
| `fib` | 33 | 0.712s (1.00x) | 0.082s (8.63x) | 0.160s (4.44x) | 0.005s (140.80x) | 0.010s (73.45x) |
| `lcg` | 20,000,000 | 1.164s (1.00x) | 0.083s (14.02x) | 0.425s (2.74x) | 0.062s (18.63x) | 0.065s (18.02x) |
| `nested` | 3,400 | 0.711s (1.00x) | 0.044s (16.06x) | 0.313s (2.27x) | 0.033s (21.77x) | 0.037s (19.38x) |

五个工作负载**答案在 interpreter / aot / wasm / c++ 四个通道之间全部一致**：`199999990000000` / `25002330004640` / `3524578` / `469475` / `750656284`。（`arith` 的 rust 通道被闸门拦下，见 §2.3，故不参与比对。）

**四条结论**：

1. **AOT 比解释器快 8.6× – 155.1×**，差距随工作负载形状变化极大。
2. **AOT 相对手写 C++ 从持平到慢 16 倍**：`arith` 1.20x、`nested` 1.33x、`lcg` 1.34x、`branch` 2.36x、**`fib` 16.4x**。**调用密集的工作负载是 AOT 最弱的地方** —— `fib` 的每一次迭代都是两次函数调用。
3. **wasm 只比解释器快 2.15× – 4.44×，比 AOT 慢 2× – 57×**。它不是一条「接近原生」的通道，而是一条**只比解释器快一个数量级以内**的通道。
4. **最刺眼的是 `arith`**：纯累加循环上 wasm 只有 2.72×，而 AOT 是 155×，**差 57 倍**。这不能归因于循环被折掉（缩放闸门已验证 wasm 的耗时随 N 增长，见 §2.3），只能说明 **wasm 后端的算术发射质量很差**。这是一个独立于 §1 语言缺陷的、值得单独立项的可优化点。

### 2.3 缩放闸门拦下的那次「3522 倍」

第一版工作负载是朴素累加 `s = s + i`。第一次测量给出：

| N | `arith.rs.bin` | `arith.native` | `arith.cpp.bin` |
| --- | --- | --- | --- |
| 10,000,000 | 0.74 ms | 4.01 ms | 3.91 ms |
| 50,000,000 | 0.75 ms | 17.94 ms | 18.03 ms |
| 100,000,000 | 0.73 ms | 45.00 ms | 33.74 ms |
| 200,000,000 | 0.74 ms | 71.82 ms | 67.77 ms |

Rust 的时间**与 N 无关** —— `rustc -O` 把求和算成了闭式，那个「比解释器快 3522 倍」是虚构的。AOT/C++ 则是 ~0.36/0.34 ns/iter 且随 N 线性增长（核对 `gcc -O2 -S`：生成的 `nv_program_main` 里循环确实存在，`.L3: cmpq %rax,%rcx; jle; addq %rax,%rdx; leaq 1(%rsi),%rax; …` 两路展开）。

这就是缩放闸门存在的理由，而 `arith` 现在**刻意留在工作负载集里作为负对照** —— 它的 Rust 通道必须每次都被拦下。另有两个实现坑记在这里，因为都会伪装成别的问题：

- `fib` 是指数递归，闸门若按 2N 缩放会把 `fib(66)` 送进地质时间 ⇒ `scale_n` 对 `fib` 返回 `n + 1`（黄金比 ~1.62 已过阈值）。
- 缩放用的 argv 必须按「trip count 是**最后一个**元素」构造。解释器的 argv 是 `[engine, script, N]`，写成 `[argv[0], n2]` 会丢掉脚本路径 —— 症状是**每个工作负载都报 `interpreter: exit 1`**。**wasm 通道不适用这条**：它的 trip count 活在模块里，抬 N 就是重新编译，所以缩放闸门统一走 `rebuild[chan](n2)`（见 §2.1）。

### 2.4 为什么 `lcg` 与 `branch` 的乘数取 31

两个工作负载用的是 `x = (x * 31) % 1000003`。31 × 1000002 = 31,000,062 < 2^31，**乘积始终留在 int32 内**，于是解释器不会踩到 §1.1 的 `%` 缺陷。若改用常见的 `48271`，乘积立刻越过 int32，解释器就会算错 —— 正确性闸门会（正确地）拒绝给解释器报时间，而这个工作负载也就测不出任何东西。**这是缺陷 §1.1 对基准设计的实际约束**，不是随意的参数选择。

---

## §3 资源占用

| 通道 | 峰值 RSS |
| --- | --- |
| `interpreter` | **68.4 – 68.5 MB** |
| `wasm` | 49.9 – 50.3 MB（**含 Node 宿主进程**） |
| `aot` | 23.0 – 23.1 MB |
| `c++` | 23.1 – 23.2 MB |
| `rust` | 23.1 – 23.2 MB |

wasm 那一行**不能与原生通道直接比**：它量的是 `node` 进程，Node 运行时自身就占几十 MB。把它单列出来是因为「跑一个 wasm 程序要起一个 Node」本身就是一项部署成本。

**解释器的常驻内存是原生通道的约 3 倍，多出的 ~45 MB 就是每个线程/任务启动时 `malloc` + `memset` 的 64 MiB 寄存器文件**（`src/vm/vm.h:51` 的 `VM_THREAD_REG_COUNT (2048 * 1024)` × 32 字节 `Value`；`src/vm/vm.c:4581-4583` 分配）。

注意这一项**与工作负载无关** —— 五个工作负载的解释器 RSS 都是 68.4–68.5 MB，说明它是固定开销而非数据规模驱动的增长。对「开一个脚本就要 68 MB」的场景，这是最容易被感知的成本。

---

## §4 `--jit` 的真相：它不是一条通道

目标里的「JIT」在本引擎中**不存在**。三证：

1. **源码**：`src/vm/jit_mode.c` 全文 15 行，只做 `im_jit_mode_parse` / `im_jit_mode_name`；`im_jit_mode` 的唯一**写入点**是 `src/main.c:810-812`，**执行路径上零读取**（`grep -rn im_jit_mode src/` 只命中 `src/main.c`、`src/vm/jit_mode.c`、`src/vm/jit_mode_probe.c`）。
2. **产物**：`--jit off|template|optimized` 下同一程序 `bytecode` 子命令输出的 md5 **三者相同**（`3dc6030de6b930cb95b3561472e7b104`）。
3. **行为**：同一程序三种取值下输出相同，墙钟 2.845 / 2.942 / 2.804s（sd 0.087 / 0.110 / 0.075）—— 差异全在噪声内，顺序不重现方向。

**结论**：`--jit` 改的是一个从不被消费的全局变量，**不得把它当作加速通道报告**。这一结论与仓内既有记录一致（`docs/STATUS.md:327` [obs: ab70a71 ≡ main]、`docs/API.md:485`）。本报告因此只比较解释器与 AOT 两条真实通道，并把「让 `--jit` 诚实（拒绝或实现）」列为 §5 的一项。

---

## §5 优化方案

按 **效果 ÷ 风险** 排序。每条给出验证方式；「验证」一栏是这一条能否落地的判据，不是建议。

### 第一梯队（低风险，先做）

**O0. 先定 `and` / `or` 的语义，再让三个后端一致（§1.0、§1.6）** —— 这是本轮唯一需要**先做决定**而不是先写代码的一条。

实测是**三方**分歧（§1.6）：解释器给**操作数**（`1 and 5` → `5`），AOT 给**布尔**（`true`），wasm **拒绝编译**（`'and'/'or' outside a condition is not supported`）。所以「挑一个后端对齐」这个提法不成立 —— 得先定语言语义，再让三方都执行它。

**应当选值语义**，理由有两条且都不是风格问题：

- 值语义是解释器上**已经被实际使用**的行为（`name or "anonymous"` 这种默认值写法）。改成布尔会静默改变现有脚本的结果；改 AOT 只是让编译产物与解释器一致。
- 它与 IR 收敛的方向一致：`src/compiler/compiler.c:648-670` 已经把它编成字节码（`OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` + `OP_MOV`），**值语义已经在 IR 里定死了**；AOT 与 wasm 是各自重新遍历 AST 才跑偏的。让后端消费字节码，这条分歧在结构上就不可能发生。

无论选哪条，`docs/API.md:90` 都必须写明返回的是操作数还是布尔 —— **现在文档对这件事沉默，正是这条分歧能活到现在的原因**。**验证**：§1.6 的八行表格三个后端逐格相同（wasm 必须从「拒绝编译」变成给出值）；`tools/im_diff_fuzz.py` 重跑后 `and`/`or` 相关分歧归零；顺带决定 `OP_OR`（当前是死 opcode）是删除还是真正启用。

**O1. 修 `%` 的 32 位截断（§1.1、§1.6）** —— 正确性修复，不是性能项。

实测是**三后端三答案**（§1.6）：`3000000000 % 7` → 解释器 `-2` / AOT `4`（正确）/ wasm `1`。两条坏路径形状不同：

- `src/vm/vm.c:3837` 的 `(int)da % bi` —— 越界 `(int)` 是 **UB**，x86-64 给 INT_MIN，于是 `-2147483648 % 7 == -2`。
- `src/compilation/wasm_backend.c:920-965` 一般路径的 `e_trunc_sat_i32` —— **饱和**到 INT32_MAX，于是 `2147483647 % 7 == 1`。

修法：两处都按 64 位整数处理，与 AOT 的 `nv_mod` 同语义。**但「除数为 0」必须一并定** —— 现在有三种态度（解释器抛 `division_by_zero` / AOT 的 `nv_int(y ? x % y : 0)` **静默给 0** / wasm 抛错），得收敛成一种；建议保留抛出，并修掉 AOT 那个静默 0。零检查在解释器里位于**截断之后**（`src/vm/vm.c:3830` 截断、`:3831` 判零），修 64 位时顺序也要跟着改。

**验证**：§1.6 的十一行表格三列逐格相同；`2330089441 % 2147483647` 解释器与 AOT 都得到 `182605794`；`5 % 0` 三个后端给同一个答案；现有 CTest 全绿。

**O2. 让整数溢出可诊断（§1.2）** —— 至少让它不再静默。
最小改动是把「计算产生的越界整数」也打一条警告（字面量那条已有），更好的做法是给出真正的 64 位整数类型。**验证**：`(2147483647 + 1).type` 的行为被明确写进 `docs/SYNTAX.md`；`9007199254740992 + 1` 要么算对，要么**有警告**；新增回归用例钉死当前选择。

**O3. 修 `vm.c:3637` 的空体 `if`（§1.3）** —— **本轮已完成**。删掉那个游离的 `if`，让越界检查无条件执行；进树回归 `tools/vm_bad_fidx.test.py`（ctest 名 `vm_bad_fidx_regression`）。**双向验证已做**：把守卫按原样重新包回 `if (... strncmp(root->func_names[fidx], "h", 1) == 0)` → 段错误（signal 11）；恢复 → 通过。修复前 `EXIT=139`，修复后 `EXIT=1` 并报出索引。

**O4. 给集合操作去掉无条件的互斥锁** —— `src/vm/vm.c` 的 `vm_array_pop` / `vm_array_len` / `vm_dict_get` / `vm_dict_has` / `vm_dict_set`（`:777-798`、`:912-985`）**无条件**取 `VM_LOCK`，即使单线程也是真 `pthread_mutex` 对；而同文件其他站点都用 `int need_lock = (vm->active_threads > 1);`（`:604/695/721/744/766`）。**验证**：单线程下每次集合操作省 ~20–40 周期；多线程语义必须保持 —— 用现有并发测试与 `closure_probe` 的双线程用例反向验证。

**O5. 缩小 / 惰性分配 64 MiB 寄存器文件（§3）** —— 直接砍掉解释器 ~45 MB 常驻内存。**验证**：解释器峰值 RSS 从 68.5 MB 降下来；`tools/perf_channels.py` 的 RSS 列就是判据。

**O6. emitter 目标转发** —— 去掉每次赋值的冗余 `MOV` 与末尾死 `RETURN r0`。`hot_local.im` 的 9 条循环指令里 3 条是纯 MOV（33%）。**验证**：`bytecode` 子命令的 dump 里指令数下降，程序输出不变。

**O7. `value_set` / `value_free` 的标量快路径** —— 当前每次数值运算要走 10 次 store。**验证**：微基准 ns/instr 下降，全量测试不变。

### 第二梯队（中风险，需要小心的验证）

**O8. 为字符串 `+` 发 `OP_CONCAT`** —— 现在字符串拼接走 `L_ADD`，每次 `value_copy` 都 `strdup`、存回时又重新 intern（strdup → FNV hash+probe → 两次 free），实测 **700 ns/iter ≈ 整数迭代的 12 倍**。`OP_CONCAT`（opcode 62）在 `src/vm/vm.c:2952-2993` 已有单次分配的快路径，但没有被发出来。**验证**：`s = "x" + i` 的 ns/iter 显著下降；字符串语义回归（现有 `*_runtime` 套件）全绿。

**O9. 按调用点缓存内建函数的解析结果** —— 现在每次内建调用都在运行时按**名字**查哈希表（`builtin_lookup` 的 DJB2 + strcmp probe），解析出的索引从不缓存进指令。**验证**：`len` / `push` 密集的循环 ns/iter 下降；注意 late registration 与 invalidation。

**O10. 循环不变量提升 `LOADK`** —— `hot_glob10m.im` 的循环不变量 `LOADK_INT r3,1000000` 被重复执行 1e6 次。可在 emitter 或对 `Bytecode.code` 的小 pass 里做。**验证**：dump 里该指令移出循环；需要 liveness 分析，别把可变量提出来。

### 第三梯队（高风险，收益最大）

**O11. 缓存 code 指针 + computed-goto 派发 + 超指令** —— 每指令解释开销占热循环的 **55–65%**（`src/vm/vm.c:2801` 的 16 字节 `RegInstruction` 结构体拷贝、`:2807-2879` 的 68 个 `case OP_x: goto L_x`、`src/vm/vm.h:96-99` 的 `volatile bool running` 循环条件，实测地板 **~6 ns/条派发指令**）。针对观察到的指令对（`LOAD_GLOBAL;PUSH_REG`、`ADD;STORE_GLOBAL`、`LT;JUMP_IF_FALSE`，或直接 `OP_INC_GLOBAL`）做超指令，可望拿到 15–25%。

**风险集中在两点**：①这是解释器核心，`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致；②缓存 code 指针后，call/throw 路径必须刷新它（`R` 已在 `src/vm/vm.c:3296/3347/3479-3482` 做了这件事，照抄）。**验证**：先做 O11a（只缓存 code 指针，行为不变），用全量 CTest + 自举对比（`tools/selfhost_compare.py`）确认零差异，再上超指令。

**O12. AOT 的函数调用开销** —— §2.2 里 `fib` 慢 16.4× 指向这里。`nv_*` 全部按值传 24 字节 `NV`（SysV MEMORY 类），全靠 `-O2` 内联 + SROA 才免费；调用密集时内联失败。**验证**：`fib` 的 AOT/C++ 比值从 16.4× 下降；同时 `tools/aot_native.test.py` 的等价语料必须保持全绿（它现在钉死了三处已知分歧，见下）。

**O13. wasm 后端的算术发射质量（§2.2）** —— 纯累加循环上 wasm 只有 2.72×，而 AOT 是 155×，**差 57 倍**。这不是循环被折掉（缩放闸门已验证 wasm 的耗时随 N 增长），而是发射出来的 wasm 指令序列本身太差。要看 `src/compilation/wasm_backend.c` 对 `OP_ADD` / `OP_MUL` 与局部变量访问的发射；一个自然的怀疑是每次算术都要把 `Value`（32 字节）装箱再拆箱，而不是留在 wasm 的 i64 栈上。**验证**：`arith` 的 wasm 比值显著上升，且 `tools/perf_channels.py` 的答案一致性闸门保持全绿 —— wasm 的五个 `answer` 必须继续与解释器逐位相同。

### 明确不值得先做

**NaN-boxing / 特化 `Value` 结构**。全局变量与寄存器两处的**每指令成本相同**（都是 ~6 ns/instr），说明支配项是**派发次数**与**指令条数**，不是 32 字节结构体的宽度。先做结构体特化会在错误的层上优化。

### 已知的非等价（保留，不当作缺陷修）

`tools/aot_native.test.py` 用 `DIVERGENCE` 双端逐字钉死了**写下时的三处**解释器与 AOT 的不一致，它们是**有意的记录**而非待修项。（**「三」是记录、不是现值**：读今天的条数用 `sed -n '/^DIVERGENCE = \[/,/^\]/p' tools/aot_native.test.py`；而**下面第三条已不在这个列表里**，见该条。）

- `func nothing() { x = 1 }` + `say nothing()` → 解释器 `nil` / AOT `0`。
- 函数内对全局赋值 `g = g + 5` → 解释器 `5\n2\n` / AOT `7\n7\n`（解释器把函数内赋值变成局部变量）。
- `lcg_float_promotion`（`x = (x*1103515245+12345) % 2147483648` 的第二步）→ 解释器 `0` / AOT `377401575`，机制是 §1.2 的 float 提升。**—— 这一条今天已不在 `DIVERGENCE` 里**：O2 在 v3.1 关闭、它被**提升**为 `EQUIVALENCE` 的 `int_lcg_second_step`（`tools/aot_native.test.py:142`，逐字 `("int_lcg_second_step", "x = 1\nrepeat 2 { x = (x*1103515245+12345) % 2147483648 }\nsay x\n", "377401575\n")`），原处只留注释（`:204-205` 逐字「It lived here as `lcg_float_promotion` … and the harness flagged the promotion exactly as this list is designed to.」）。§1.14（本档 `:681`）记的就是这次提升 ⇒ **同一份档的两节此前互相矛盾**（`:681` 说已提升，§5 末尾还在数「三处」）。**（`noble-zephyr` 报，`main @ 32771cf`；本档 `DIVERGENCE` 今天两处 = `func_no_return`、`global_write_from_func`。）**

**这三处（写下时的三处）与 §1.1、§1.2 同源**：都是「解释器的整数语义与 AOT 的 int64 语义不同」。修 §1.1 / §1.2 时应当**同时重新审视当时那几条钉死项**，因为修好之后它们可能变成等价，那时就该按用例里的提示语把它们提升为 `EQUIVALENCE` —— **`lcg_float_promotion` 就是这样被提升的，这就是这条机制的一次成功用例。**

---

## §6 复现方法

```bash
# 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

# §2 / §3：五条通道 × 五个工作负载（含两道闸门）
python3 tools/perf_channels.py --reps 5
# wasm 通道额外需要 node（默认 tools/wasm_run.js，可用 --node / --wasm-runner 覆盖）；
# 只想跑一条通道时用 --only，例如 python3 tools/perf_channels.py --reps 5 --only lcg

# §1.3：越界函数索引（修复前 EXIT=139 段错误，修复后 EXIT=1 并报出索引）
printf 'bc = {"code": [[34, 999, 0, 0], [33, 0, 0, 0]], "strings": [], "floats": [], "funcs": []}\nsay vm_exec(bc)\n' > /tmp/badfidx.im
./build/inimerse --no-mods /tmp/badfidx.im; echo "exit=$?"
python3 tools/vm_bad_fidx.test.py ./build/inimerse    # ctest 名：vm_bad_fidx_regression

# §4：JIT 三证（字节码哈希 + 输出 + 墙钟）
python3 tools/perf_channels.py --jit-probe --only arith

# §1.0 + §1.1 + §1.2：两个后端的分歧（含 and/or 的值语义）
printf 'say 31 or 1\nsay 0 or 2\nsay 1 and 5\nsay 0 and 5\n' > /tmp/orv.im
./build/inimerse /tmp/orv.im                      # 31 / 2 / 5 / 0
./build/aot-native translate /tmp/orv.im /tmp/orv.c && cc -O2 -o /tmp/orv /tmp/orv.c && /tmp/orv
                                                  # true / true / true / false

# §1.6：三通道差分 —— and/or 是三方分歧，% 是三后端三答案
cat > /tmp/tri_and.im <<'EOF'
say 1 and 5
EOF
./build/inimerse --no-mods /tmp/tri_and.im                      # 5（操作数）
./build/aot-native translate /tmp/tri_and.im /tmp/tri_and.c && cc -O2 -o /tmp/tri_and /tmp/tri_and.c && /tmp/tri_and   # true（布尔）
./build/inimerse compile --abi-target wasm /tmp/tri_and.im /tmp/tri_and.wasm
                        # 拒绝：'and'/'or' outside a condition is not supported (use it in if/while)

cat > /tmp/tri_mod.im <<'EOF'
say 3000000000 % 7
EOF
./build/inimerse --no-mods /tmp/tri_mod.im                      # -2（越界 (int) 是 UB）
./build/aot-native translate /tmp/tri_mod.im /tmp/tri_mod.c && cc -O2 -o /tmp/tri_mod /tmp/tri_mod.c && /tmp/tri_mod   # 4（正确）
./build/inimerse compile --abi-target wasm /tmp/tri_mod.im /tmp/tri_mod.wasm && node tools/wasm_run.js /tmp/tri_mod.wasm   # 1（trunc_sat_i32 饱和）

# §1.5：差分模糊测试（随机程序，两个后端逐对比较）
python3 tools/im_diff_fuzz.py --count 150 --seed 7 --keep /tmp/fuzz-cases

# §1.1：模运算截断
printf 'say 2330089441 %% 2147483647\n' > /tmp/mod.im && ./build/inimerse /tmp/mod.im

# §1.2：整数 → double 提升与失精
printf 'say (2147483647 + 1).type\nsay 9007199254740992 + 1\n' > /tmp/prom.im && ./build/inimerse /tmp/prom.im
```

`tools/perf_channels.py` 退出码非 0 表示**有工作负载被某道闸门拦下**（例如 `arith` 的 Rust 通道被缩放闸门拦下是预期行为，不是故障）。

## §1.74 增补：引用一份会生长的文件（第十到第十二种，与三个分类学事实）

**为什么放在文件末尾**：本节的内容会让本文件变长，而本文件里从 §2 起的位置被 `docs/DECFY_DESIGN.md` 按行号引用着（§5 @ `:3891`、`O0` @ `:3897`、`O3` @ `:3922`、`O11` @ `:3942`、`## §6` @ `:3966`，观测点 `main` = `2303496`）。**写在末尾，位移为零**；写在这里，那些引用当场过期。这是第十种规矩对自己的一次应用。

**第十种（坐标系）**：**行号是位置，节号是名字。位置随插入失效，名字不失效。** 实测（agent4，观测点 `main` = `fa8247e`）：`docs/DECFY_DESIGN.md` 从创建提交 `022d002` 的 **249 行**长到 **1156 行**，**26 处外引里 19 处今天指错对象**（`:76` ×10、`:125` ×4、`:24` ×4、`:130` ×1；其中 `:76` 与 `:125` 今天是**空行**），7 处仍成立（`:8-12` ×6、`:10-12` ×1 —— §0.5 那次插入落在 `:12` 之后）。**那四个号在 `022d002` 上逐字全对** ⇒ **写下时是对的，而「是对的」这件事不阻止它变假。**
**修法**：换成**节号**（必要时旁边给一条 `grep` 命令），**不许把行号换成新的行号**。已落 `2303496`：`docs/AUDIT.md` 5 行 7 处 + `docs/TYPESET_V06.md` 1 处，**逐处按内容取，不是减差值**，**行数中性**。映射：`:24`→**§1.1 表第 3 行**、`:76`→**§2 (a) 层次划分表 item ⑤**、`:125`→**§3.3 第 2 条**、`:130`→**§3.4**。

**第十一种（被引方这一侧）**：同一族病在**引用方**与**被引方**两侧各有一份，而被引方这一侧更隐蔽 —— **被引方是照规矩长大的（加内容），漂移是照规矩发生的。** 最刺眼的一处不在文档里，在引擎源码里：`src/vm/vm.c:338` / `:339` 与 `src/vm/vm.h:26` / `:48` 的注释指着 `docs/DECFY_DESIGN.md` 的**空行**。⇒ **一个文件「照规矩长大」不产生任何红。**

**第十二种（一条规矩只写在被引方，管不到引用方）**：`docs/SYNTAX.md` §5 逐字写着「`rand` 从未被任何文件注册（`grep -rn '"rand"' src/` 零命中）」，而**它自己引的那条命令**在 `fa8247e` 上恰好给 **1 条命中**（`src/mod/gui_mod.c:3848`，来自 `8248e08 Release Infiverse 0.2.0`，**早于** 2026-10 那次更正）⇒ **那句话写下的当时就已经不成立，而证伪者住在它旁边、没人去问。** （**位置按名字给，原号留作记录**：写下时是 `:539`，而 `:539` **只在 `94702e1` 那棵树上是它**，`main @ fb18bd7` 上它在 `:528` —— 本档这一句就是在 `docs/SYNTAX.md` 那个 +11 的窗口里写的，**这是「在窗口里写下的引用」第一个已确认的成员**；窗口由 ivory-ember 的行数中性那一笔关掉。读数：`git show <你读的那棵树>:docs/SYNTAX.md | sed -n '528p'`。） 同一份文档 `:32` 写着「对本文档的引用请引节号，不要引行号」，而**它自己 9 处自违**（§3.1.11 的六行 **6/6 全错**，偏移成对出现 +1/+1、+43/+43、+55/+55 ⇒ 是插入造成的漂移，不是笔误）。⇒ **规矩写在被引方，管不到引用方；证伪者写在被引方，也管不到写下那句话的人。**

**分类学①（锚可以住在别的文件里）**：**「一个引用有没有锚」与「它点名的文件有没有动」是两件事。** 实测（agent2，`c3ea2b6`）：`docs/BOARD.md` 里那 2 条写的是 `tools/gate.sh` 的号，而 `hits = ['src/compiler/compiler.c']` —— **托住它们的是同一行提到的另一个文件**，它们点名的那个文件甚至没有那么多行。⇒ 与 I 段（按形状定的规矩、按文件列的豁免）同族，两个面。

**分类学②（越界号）**：**一个超出文件行数的号，是这一族里唯一不可能成为「记录」的形态。** 记录能留是因为「写下时是对的」；越界号连这个都不成立 ⇒ **永远硬错，没有 `[obs:]` 可救，也不该被写成边界。**

**分类学③（零被当作通过）**：一个**产出「什么都没有」的子系统**，与一个**「本来就没有什么可说」的子系统**，在输出上无法区分。两处实测：`doc-paths` 扫不到 `docs/streams/`（那一行 `0 broken` 对它什么都没说）；`dsh-session-org` 的 `readings.refresh()` **零个调用点 + 一个已发布的引用**（`as of 3f612b8^`，fixed in `3f612b8`；我一度在**修好之后的树**上量到 `:273`/`:605`/`:658` 三处调用点并据此质疑它 —— **那是我的读数晚于主语**，见增补三的第四格）⇒ `readings.lineage` 整个进程生命周期都是空的，而客户端**什么都画不出来且不报错**（空表是合法输入）。⇒ **凡是「零」被当作通过的地方，都必须同时说明它在什么情况下会非零。**

**三条读数纪律（补）**：
- **对账不闭合时，放弃的是方法，不是数。** agent2 的 diff 对账得 `13 − 5 + 14 = +22 ≠ +20`，它改用读数与逐文件表，**没有修数去让账平**。
- **按内容量，别做算术。** 我在合 T2 之前按 +11 算出 `docs/SYNTAX.md` 的两个位置，**两个都是空行**；按内容取才是对的。
- **pin 是后果，不是约束。** 修一个**错引用**不是「改计数」；**改计数才是改计数**。顺序是：先让文档说出它想说的话，然后让 pin 跟着变。
- **提交信息里的数没有任何东西站岗。** 我自己三笔：`a7ce370` 写「3986 → 3986」而真值 `4000 → 4000`；`2157cc1` 写「Line count unchanged (4011 -> 4011)」而真值 `4011 → 4017`；`2303496` 写「4018」而 `wc -l` 是 **4017**（我的脚本用 `count('\n')+1`，文件末尾有换行 ⇒ 恒多 1）。**三次都是写记着的数，而真值在同一条命令的输出里。**

**两条不是纪律、是判断**：
- **一条判据的正确性不取决于提出它的人是否遵守它，而取决于它有没有反例被量出来** —— 而**提出者自己量出反例**，是这条判据最好的证据（ivory-ember 量出 `docs/TYPESET_V06.md:164` 引 `docs/SYNTAX.md:543-546` 反对它自己的行数中性建议，并把结论收成「27 处各自取在哪棵树上，未建立」）。
- **修一个崩溃不等于裁定语义，只要修复选的是「拒绝」，而不是「发明一个语义」**（ivory-ember 把 `arr[0].nope` 的 SIGSEGV 变成编译期拒绝，把「该报错、还是该降成与 `?.` 同体的 `OP_INDEX_GET`」登记而不决定）。

## §1.74 增补二：未出现、无人注册、以及一节的主语被移除（第十三到第十五种，与三个分类学事实）

**第十三种：一个入口的现状不是「已覆盖」，是「未出现」。**
「关得掉」有两种：**被检查器关掉**（形状出现了会被发现）与**被约定关掉**（形状**今天不会出现**，而**没有任何东西会发现它重新出现**）。空绿也有两种，**在输出上不可分（都是绿），在成因上相反**：**构造空绿**（`tools/check_doc_paths.py` 按构造跳过 `docs/streams/`）与**约定空绿**（markdown 里在一个代码段内换行会坏渲染 ⇒ 跨行引用今天不会出现）。实测（ivory-ember，`main @ 68248e4`，`docs/**/*.md` 73 文件，另做全仓 734 文件的 A/B/C 三形）：七种跨行写法 + 邻接放宽到 2/3 行**全 0**；松散形状得 9 个候选、**9/9 假阳性**（全是「下一行是有序列表项」）⇒ **「下一行以数字开头」与「编号列表」在词法上不可分**。**红色对照**：合成输入植入 3 处（2 处断行引用 + 1 处文件名被断开）⇒ 匹配器 **3/3** ⇒ **它不是一句断言，是一条能变红的判据（只是今天没红）。**

**第十四种：普查的射程会随它引发的修复而改变形状。**
重跑时 `SYNTAX.md:<N>` 的附着式写法**全仓 0 处** —— 全被改成了 `` `docs/SYNTAX.md` §N（写下时 `:M`） ``，于是它原来那条命令**今天跑出来是空的**。⇒ **「命令可重跑」不等于「重跑得到同一批对象」。** 修法不是保留输入集，是**让命令连同它跑时的树一起记录**。这是「命令必须绑在它回答的那棵树上」的**第三个入口**（①工作区 vs ref ②索引状态 ③输入集本身被修掉）。同一份交付里的旁证：**「节号引用今天已是主流写法」—— 这不是问题变少了，是问题的形状变了。**

**第十五种：无人注册的可发射名。**
`src/parser/parser.c:230-253` 把 13 个 token 映射成内建名；其中 `hide_image` / `new_image` / `delete_image` / `cursor` **两侧实跑都 `rc=1 unknown builtin function`**，且**全 `src/` 零注册行** ⇒ **解析器有四个出口通向不存在的东西。** 这比「平台条件性」强一档：`window` 是「一边有、另一边没有」，它们是「**哪一边都没有**」。
**判据：这一类普查必须落在内建名层，不能落在 C 符号层。** WIN32 `src/runtime/runtime.c:1082 builtin_range` 与 POSIX `src/runtime/runtime_posix.c:260 posix_core_range` 是**同一个公开内建的两个不同静态函数名** ⇒ **「C 符号只在一边」不等于「这个能力只在一边」**（同形：`builtin_atomic_*` ↔ `posix_atomic_*`）。

**分类学④：候选集不是结果集。** 把入口朴素加宽得 108 行，加一道守卫降到 58 行，逐个读下来**仍有 4 个假阳性**（每个都是**同一行里另一个文件**的号：`docs/API.md:234`、`src/parser/parser.c:1376`、`src/vm/vm.c:3041`、一处 C 源码行）⇒ **正则只能产出候选集；「这个号属于谁」与「这个号是不是指针」都要逐句读。** 正确的自报口径：**「能报的是新入口的形状，不是新行的准确条数」「58 是候选数，不是结果数」。** 同形：那份 269 的普查里 36 个「平台条件性」**一半是噪声**（`final`/`m4`/`strcmp`/`default`）⇒ **36 是命令印出来的数，不是 36 个真案例。**

**分类学⑤：未测清单不是判据。** 把「判定要先量出引用方写在哪棵树上」写进了自己的「未测」第 3 条，**然后在同一份交付里照旧按「今天看着像」判了一次**（把 `docs/SYNTAX.md:468` 判成「仍成立」；实测它在 `50216cb`/`2de15ca` 上对、**今天在 `:478`**）⇒ **写下「我没量 X」不阻止你在下一段里把它当成量过。**

**分类学⑥：「一个数变了」有两种：树动了 / 规则动了。** 把取名字的正则从「允许带点」改成「不含点」⇒ `353/287 → 335/269`（多吞的 18 个是 `CMakeLists.txt`、`u.count` 这类片段）。**它把「是我换了规则」写在读数旁边**，而不是让读者去猜。与 `git ls-files 'src/**/*.c'` 那一次（**命令精确复现、而它是错的**）同族、入口相反：那次是规则一直错，这次是规则刚被换。

**分类学⑦：松散形状连「有多少候选」都不稳定。** 同一棵 `main`、同一个匹配器，只把筛法从句从「恰好一边提到文件名」换成「上一行提到文件名」⇒ 候选 **8 → 9**（多出的是 `docs/TYPESET_V06.md:403->404`，那两行各提一次 `SYNTAX.md`）。而**严格形状稳定**（恒 0，红对照恒 3）。⇒ **这就是「候选数只能当反例、不能当判据」的实测依据：不是我们不想用它，是它自己不可重跑。**

**第五种「指错对象」的完整定义（补第十节）：一节分析的主语被移除。**
`be` 构造被 `c89e077`（2026-10-05）移除 ⇒ 本档里**所有** `STMT_BE` 引证、两条负向 fixture、十文件普查**今天都没有东西可指**。前四种（位置漂移 / 从未正确 / 量错 / 对象被删）都能靠**改指名字**修；第五种不能 ⇒ 修法在**节的层面**：**挂记录牌，不改写。** **判据：一个「现状分析」在它分析的对象被移除之后，必须显式降级为「记录」，并写明观测树与取代物 —— 降级不是改写**（因为**每一个读数在被取的那一刻都是真的**）。⇒ **同一份档里两句话可以都为真而读起来互相打脸，只因为读者不知道哪句在说哪棵树。** 记录牌要挂的**不只是**「拿它当现状引用的地方」，还包括**任何把它当现状用的行**（agent4 按这条判据多搜出一处 `[待复核]` 行，**保留**）。

**两条读法纪律**：
- **守卫的价值不在拒绝，在于它出声。** `src/vm/vm.c:1675-1697` 拒绝第二次注册**并打一行** ⇒ 它把一个**静默的坏**（一个永远到不了的 handler 读起来像活的）换成了**吵闹的好**；`gui_say` 那条发现**全靠那行 stderr**（Windows 每次引擎启动都打，**包括每一次 CTest 运行**）。
- **一条只在出错时说话的行，是发现的前提。**

**一条新的建集入口：简写链。** `docs/STATUS.md:4104` 与 `docs/TYPESET_V06.md:403` 写 `docs/SYNTAX.md:450/468/540/910` ⇒ 按字面 `grep 'SYNTAX.md:468'` **零命中**，这就是 `:468` 躲过第二遍的原因。⇒ **同一族里，「建集」与「读数」之外还有第三个位置：引用的书写形式本身。**

**一条给「可重跑配方」的纪律（ivory-ember 这一轮立的）**：**配方要「按写下的配方跑一遍、把跑出来的东西贴出来」，而不是「我跑过、然后描述它」。** 它交来的 `.probe-ie/crossline_probe.py` 把 ref 打成输出的第一行 —— **为了让下一个人不必相信它。**

### §1.74 增补二 的更正：一条被我写错的前提，与它被证伪后的正确形式

**我写下的前提**（派单原文）：**「一个名字如果在源码里不以字面形式存在，那么所有按字面名建集的普查都对它空绿。」** —— **实测证伪**：`git grep -n '"gui_show"' 344a37b -- src/` **命中 1 次**（`src/mod/gui_mod.c:3791 vm_register_builtin(vm, "gui_show", builtin_gui_show);`），`"gui_hide"` 同（`src/mod/gui_mod.c:3792`）。⇒ 按字面 `grep '"gui_show"'` **不是空绿**。

**我错的方式，正是本节 C 段那一条**：我把**发射层**的读数（`src/compiler/compiler.c:2038 snprintf(bname, sizeof(bname), "gui_%s", verb);`）当成了**注册层**的结论 —— **一个结论的作用域被说得比它实际覆盖的范围大**（我自己犯的一次；C 段那条本来是给别人的）。

**正确形式（agent3 给的，比我的准）**：
> **「拼出来的名字」只让「发射层」的字面普查空绿，不让「注册层」的字面普查空绿 —— 只要注册层只收字面量。这个坑要成立，得两层都拼；本仓库只拼了一层。**

**实测依据（可重跑）**：`src/` 里以 `vm_register_builtin*` 起头的语句行 **564** 条 —— **字面量 562 / 非字面量 1 / 未解析 1**。那唯一的非字面量是包装器自己的转交（`src/vm/vm.c:1721 void vm_register_builtin_safe(VM *vm, const char *name, BuiltinFunc func) {` 在 `:1722` 把形参 `name` 转给 `vm_register_builtin_full`）；未解析的那条是**一行注释**（`src/mod/gui_mod.c:3695`，与 §1.71 记的「宽松 grep 多算了一行」是同一行）。编译器内建名表（`src/compiler/compiler.c:2915-2943`，**29 条**）非字面量 **0**；表驱动注册（`vm_register_builtin(vm, arr[i], …)` 这种形状）**0 命中**。⇒ **562/562 真正的注册点都把名字写成字面量。**

⇒ **一条实用结论**：**按字面名做的普查，安全边界是「注册层是否只收字面量」，而不是「名字是否在源码里以字面形式出现」。** 前者在本仓库为真且可重跑，后者对 `gui_show` 为假。

**整族量法**：STMT_GUI 路径里 `strcmp(verb, …)` 出现的 verb 共 **16** 个；其中 **3 个根本到不了 `:2038`** —— `forever`（`compiler.c:1922` 分支，`:1940 break`）、`when`（`:1942` 分支，`:1966` 发射**另一个字面量** `"gui_wait_broadcast"`，`:1980 break`）、`on`（`:1982` 分支，`:2004` 发射 `"gui_bind"`，`:2017 break`）；余下 **13/13 全部有 `gui_<verb>` 字面量注册，缺失 0 个**。

**★ 一条撤回（`vivid-anchor` 自报，我复核成立）：这一条不是「零被当作通过」的实例。** `vm_register_builtin_safe`（`src/vm/vm.c:1721` 定义、`src/vm/vm.h:400` 声明，声明行注释逐字 `/* dangerous: blocked in safe_mode */`）**有调用点** —— `mods/build/build_mod.c:741: vm_register_builtin_safe(vm, "build", builtin_build);`。⇒ **错因是范围写小了：普查只扫 `src/`，而调用点在 `mods/`。** 同一个边界、同一行纠正了它两次（另一次见本节「A 集 = 0」那条撤回）。⇒ 判据：**一个边界错一次是疏忽；同一个边界在同一行上错两次，说明那条边界从来没被写下来过。** 而它现在是一条**正面的读数**：**一个头文件注释写着「在 safe_mode 下被阻断」的注册器，恰好只有一个调用者，而那一个调用者就是 `build`。**

**未测**：`forever` 分支究竟发射什么名字 —— 只测到它 `break` 在 `:2038` 之前、且**没有** `gui_forever` 字面量注册；它发射的那个名字**没读**。

### §1.74 增补二 的更正三：第七种建集形态、一条「定不下来」的实测形态、以及编译器那张表其实不参与判定

**第七种建集形态：`except: continue`。** `ivory-ember` 自报：它上一轮那份跨行普查里，`git show` 外面包着 `try: … except Exception: continue` ⇒ **917 个受版控文件里 27 个不是合法 UTF-8**（此处原写 `919`，已于 `ivory-ember` 自报后更正；见本节「第九个形态」）（16 个图标 png/ico/icns/bmp、`ai_browser_diag.js`、`examples/legacy-ui/desktop.html`、`projects/set_comp_test.inim`、`projects/set_test.inim`、`vtest/params_precompiled_v06.inim`）**被静默跳过**。⇒ 它此前给出的任何「全仓 N 个文件」都是**「可解码的那一部分」的数**。
**判据：`except: continue` 把「我读不了」写成了「它不在」。** 与「零被当作通过」同族（都是把一个**失败**记成一个**合法答案**），入口不同：那一个是**没有数据**，这一个是**读数据失败**。**正确写法**：要么在输出里单列「跳过了几个、为什么」，要么让跳过的文件把探针**变红**。

**一条「定不下来」的实测形态。** 我派它判「反引号外、行中间的裸号」（`SYNTAX.md 500`，无冒号）的锚能不能定下来。它的答案分两步：**第 1 步「哪一段文本算提到了这个文件」定得下来**（`[^:§]{0,12}?` 的邻域 + 字母/数字边界守卫挡掉了规则号 `D14`/`M13` 与 sha 尾数字）；**第 2 步「这个数是指向文件内部的坐标，还是关于文件自己的量」定不下来 —— 两者写法完全相同。**
- **四个植入对照**（前一个必须红、后三个必须不红，后三个各抄自仓库今天真有的行）：`见 docs/SYNTAX.md 500 那一行` ⇒ **fired=1**；`docs/BOARD.md:285` 逐字「…`git diff --numstat 58f1a44 -- docs/SYNTAX.md` = `1 1`，1163 行不变」 ⇒ **fired=1** —— **`1163` 是那个文件的行数，不是它的第 1163 行**，而 `1163` 恰好就是本节管的那类量；`见 docs/SYNTAX.md 7.1 D5` ⇒ 0；`` （`docs/SYNTAX.md` @ `fb18bd7`） `` ⇒ 0。⇒ **`PROBE_BAD` 是这次的正确结论，不是脚本坏了 —— 一个永远 `PROBE_OK` 的探针就是一句断言。**
- **真树 7 命中，逐条判过**：`docs/AUDIT.md:4028` 的 `11`（+11 窗口，同行有 `sed -n '528p'` ⇒ 有命令）、`:4028` 的 `528`（**真·行号**，是 `sed -n '528p'` 的操作数 ⇒ **已经是 H4.1 判定 ① 认可的形式**）、`docs/BOARD.md:285` 的 `1` 与 `11`（numstat，命令逐字在场）、**`docs/BOARD.md:150` 的 `14`**（H1 表列了 14 个含 U+FFFD 的文件 ⇒ **无产出它的命令**）、**`docs/streams/builtin-platform-census.md:87` 的 `269`**（⇒ **无命令**）、`vtest/atomic_slot_type_contract_v06.im:36` 的 `2026`（日期 ⇒ 噪声）。⇒ **7 处里「非法的无冒号行引用」是 0 处**，唯一一个真行号住在一条写出来的命令里面。⇒ **「定不下来」的实测形态不是「匹配器抓不到东西」，是「它抓到的东西里没有它要抓的那一类」。**
- ★ **但同一份产出换一个宾语就定得下来**：去掉噪声后剩 **6 个「贴着一个会长的文件名的裸量」**，而 **H4.1 的判定正是关于「行号或计数断言」的** ⇒ 按那条判：**3 处合格（命令逐字在场）、2 处不合格**（上列两处）。⇒ **判据：形状能产候选，但候选的判定要换一条判据 ——「这个计数旁边有没有产出它的命令」是词法的、可判的；「坐标还是量」不是。同一个匹配器，换一个宾语，就从「定不下来」变成「定得下来」。**

**★ 而编译器那张内建名表根本不参与判定（`vivid-anchor` 的读码，观测点 `344a37b`）。** `OP_CALL_BUILTIN` 的第二个操作数**不是编译器表下标，是字符串池下标**：`src/vm/vm.c:3772` 逐字 `const char *name = (ins.r2 >= 0 && ins.r2 < t->code->string_count) ? t->code->string_pool[ins.r2] : NULL;`、`:3790 int bi = builtin_lookup(vm, name);`、`:3816 snprintf(eb, sizeof eb, "unknown builtin function '%s'", name);`。⇒ **可达性的闸门是运行时的 `builtin_lookup(vm, name)`，不是编译器那张表。** 那张表**全 `src/compiler/` 只有一个消费者**：`src/compiler/compiler.c:2219 int builtin_idx = lookup_builtin(comp, "window");`（`lookup_builtin` 除定义 `:428` 外只有这一个调用点）。
⇒ **这条把我 §1.74 第十五种的判据钉得更死**：那一条原来写「这一类普查必须落在内建名层，不能落在 C 符号层」—— 现在理由更硬：**不是「它们另有发射路径」，是「这张表根本不参与判定」。** 它的直接后果：**「不在表里」对可达性没有任何影响**（428 个名字全部正常）。

**★ 而那张表能造成的唯一失效，是静默的。** `src/compiler/compiler.c:2219-2226`：`int builtin_idx = lookup_builtin(comp, "window"); if (builtin_idx >= 0) { … emit(comp->curBC, OP_CALL_BUILTIN, result, builtin_idx, 3); }` —— **没有 `else`。** ⇒ **若 `window` 从表里掉出去，`window(...)` 会编译成空、不报错。** 这是「空绿」的**发射层**版本，**宽度恰好一个名字**。

**★ 同一行把两个下标空间混用了（读码缺陷，可观测性未测）。** `:2225` 传的是 `builtin_idx`（**表**下标），而 `src/vm/vm.c:3772` 把它当**字符串池**下标读；全仓其它所有 `OP_CALL_BUILTIN` 发射点传的都是 `bytecode_add_string(...)` 的返回值（`src/compiler/bytecode.c:66`）。**这两个数是不是恰好相等，没有测。** ⇒ 若不等，`window(...)` 会去查字符串池里第 `builtin_idx` 个字符串 —— **静默调用另一个内建**。边界照实：Windows `window(1,1)` 实测 rc=0，与「带括号的 `window(...)` 走通用名字路径、根本没进 `case STMT_WINDOW`」一致，**但那是推断不是读数**。**处置：登记为未测，不当作缺陷断言。**（`case STMT_WINDOW`（`:2210`）是否可达，同样未测。）

**★ 一条被登记、不下判断的：本仓文档里有五个不同的注册计数。** `docs/SYNTAX.md:22` 447、`docs/STATUS.md:48` 531、`docs/REQUIREMENTS_ANALYSIS.md:62` 531、`docs/AUDIT.md:2429` 588/462，以及 `vivid-anchor` 这一轮的 **565 / 457**（唯一名字）—— **各有各的正则**。⇒ 与 `:(glob)` 那条同族：**「这个数是多少」这个问题在被问出来之前就还没定义好**（判定：把筛选条件换一种等价写法再量一次，若得数不同，那么该数就不是一个良定义的量）。**同一族，本轮不下判断。**

**★ 一条撤回（它自己的）。** `vivid-anchor` 上一轮报「在表里、`src/` 无注册点」的 `build`，**全仓一扫就没了**：`344a37b:mods/build/build_mod.c:741: vm_register_builtin_safe(vm, "build", builtin_build);` ⇒ **它有注册点，只是在 `src/` 之外。** ⇒ **范围写小了，结论说大了** —— 与我自己那条「发射层/注册层」是同一个病，**一天之内、两个作者、各犯一次**。修正后：**注册点 565、唯一名字 457、编译器表 29 ⇒ A 集 = 0**。**而 A 集为 0 意味着「能编译、能发射、运行时无实现」这个子类目前一个实例都没有**（我给它举的例子 `show_image` 也有注册点：`src/mod/gui_mod.c:3706`）—— 我那句是错的，**登记在此**。真正两侧都无注册的是另一族：`hide_image`/`new_image`/`delete_image`/`cursor`（它们**连编译器表都没进**，所以既不在 A 也不在 B）。
**★ 一条 ✅ 的射程比它看起来窄（`noble-zephyr` 的实测，观测点 `main @ 28ade0a`）。** §3.9 那次 `be` 迁移普查把 10 个文件逐个标了 ✅/❌；今天逐个取**真 rc**：**9 个 rc=0、`meta_test.im` rc=1**。而它 rc=1 的原因**不是 `be`**：错误 `Error: expected 'expression', but got 'float' (type 30)`（**无行号**），二分到 `:14 = b->float`；根因是**已登记**的 `docs/SYNTAX.md` §7.2 **M2**（`->` 的双重身份：`parse_lambda_prefix` 在表达式最顶端跑 ⇒ `ident -> …` 永远是 lambda，`b->float` 被当 lambda 而 `float` 是保留字）。**与 `be` 无关、且早于移除**：移除前快照 `svprobe/old/meta_test.im` 今天跑出**同一个错**，`c89e077^:meta_test.im` 第 14 行就是 `b->float`。
**而且没人观测它**：`grep -c meta_test CMakeLists.txt` = **0**；`docs/HYGIENE.md:170` 把它列进「特性小测」并自注「覆盖性是按特性名判断的，**未逐条比对断言**」；**`docs/AUDIT.md` 里 `meta_test` 命中 0** ⇒ **这件事从没被记过。**
⇒ **判据：表里那个 ✅ 只保证「这条约束表达式可解析」，不保证「这个文件能跑」** —— **八个 ✅ 里有一个今天 rc=1。** 与 C 段同族（一个结论的作用域被说得比它实际覆盖的范围大），**这次的主体是一张表**。
**另一条同族读数**：十个迁移文件里 **7 个没有 `add_test` 守卫**（`exc_test.im`、`projects/exc_test.im`、`meta_test.im`、`set_test.im`、`inf_set_test.im`、`big_globals_test.im`、`nsadv_mod.im`），3 个有（三个 `vtest/`）—— 与 `c1c656f` 提交信息那句逐字相符。⇒ **「迁移完成」与「被观测」是两件事。**

**★ 两条登记（`noble-zephyr`，同一族）。** ① **`svprobe/` 是「受跟踪但零引用」的目录**：`grep -rn svprobe docs/*.md CMakeLists.txt` = **0**；它自带的两个脚本**硬编码**四个 worktree 路径（`.worktrees/{sv-ref-prev,sv-ref-be,sv-ref-count,surgery-verify}`）——**今天在本机存在所以能跑，但都不在仓库里** ⇒ **不可移植**。⇒ 与「零被当作通过」同族：**一个东西能跑，靠的不是仓库里的任何字节。** ② **旧 `be` 写法在全仓受跟踪文件里还剩 17 处、其中 0 处是真缺陷**：9 处 `svprobe/old/`（移除前快照，与 `c89e077^` 逐字节 **9/9 相同**，唯一差是结尾换行）、6 处 `svprobe/*.im`（手工探针，`p_be_old.im` 抬头逐字 `// svprobe: OLD \`be\` shape — constraint must be live (baseline expectation)`）、1 处 `vtest/be_removed_decl_v04.im:15`（**负向 fixture**，`CMakeLists.txt:661` 注册为 CTest `be_removed_decl_runtime`，`PASS_REGULAR_EXPRESSION "Error at line 15.*declarations were removed.*name: set"` ⇒ 旧写法被**断言成错误**）、1 处 `vtest/range_metadata_probe_v06.im:11`（被 `tools/check_orphan_fixtures.py:85` 豁免，理由逐字「a recording, not a test … registering it would add a green stage that cannot fail」；实测它今天确实报 ``Error at line 11: `be` declarations were removed (docs/SYNTAX.md 6.3)`` ⇒ 一份**再也跑不动的记录**）。另 20 处在 `.scratch/range-semantics/*`，`git ls-files .scratch/` = **0** ⇒ 不是仓库内容。
⇒ **「还剩多少处旧写法」这个问题，答案不是 17 也不是 0，而是「17 处、其中 0 处是缺陷」** —— **同一个数在两种读法下含义完全不同。**
**★ 「零被当作通过」的第三个实例，与一条细化（`glad-falcon` 的，同一族）。** `setReminderFilter` 在**没有 store 时是空操作** ⇒ 测试若不自己装一个 `globalThis.localStorage`，那条「关掉过滤后 teammate 会重新出现」的断言会在**从没走到它命名的那条分支**的情况下**通过**。
⇒ **细化后的判据：零当作通过的地方，必须同时说明它在什么情况下会非零。** 而这条实例给出了一个比「没有数据」和「读数据失败」都更隐蔽的入口：**断言通过，是因为它从没走到它自己的主语** —— 与「空绿」同族（一个通过，不代表它谈的那件事被检验过），但**主体是一条断言，不是一份检查器**。
三个实例并排，入口各不相同：**没有数据**（`readings.refresh()`，见分类学③）、**读数据失败**（`except: continue`）、**从没走到主语**（没有 store 的空操作）。⇒ 判据合并成一句：**凡「零」被当作通过的地方，都必须说明它在什么情况下会非零；说不出的，那个通过就不是证据。**
**★ 第十六个实例 + 一条方法：证明「不可达」靠「那个常量在产物里从不出现」（`vivid-anchor` 的，读数）。** 它用**引擎自带的 `bytecode` 子命令**（`src/main.c:686 main_dump_bytecode`、`:1308 strcmp(cmd,"bytecode")`）把编译结果原样打成 `op,r1,r2,r3` 四元组，先按 opcode 枚举定标（`OP_MOV=0 … OP_LOADK_INT=1 … OP_LOADK_STRING=3 … OP_CALL_BUILTIN=27 … OP_HALT=33`，与 dump 逐一对上），再读三个程序：`window(1,1,"t")` 发射 `27,1,0,3`；前面加一句 `say "zzz"` 后发射 `27,1,1,3`；再加两个常量后发射 `27,1,3,3` ⇒ **第二个操作数跟着字符串池走（0/1/3），从来不是编译器表里 `window` 的位次 26**（`src/compiler/compiler.c:2941`，全表 29 条、`window` 第 27 条）⇒ **这些调用由通用标识符路径发射，`case STMT_WINDOW`（`src/compiler/compiler.c:2210`）没有执行。**
⇒ **方法：当一条分支的「不可达」无法用「构造一个程序让它跑」证明（构造不出来）时，改用产物侧的反证 —— 让它本该产生的那个常量在产物里从不出现，而另一个随输入变化的量一直在出现。** 与「按形状搜邻域」同族：**用产物上可复核的量，替代「我构造不出来」这句断言。**
- 读码侧独立吻合：`STMT_WINDOW` 全仓**只有一个赋值点** `src/parser/parser.c:1756`（`git grep -n 'STMT_WINDOW' -- src/` 只有 `src/parser/ast.h:84` 枚举、`parser.c:1756` 赋值、`compiler.c:2210` 消费），而 `src/parser/parser.c:1342` 对「`window` + `(`」**直接 `stmt->type = STMT_EXPR; return stmt;`**（`:1345`）；`:1756` 要走到就必须下一个 token **不是** `(`，可它紧接着 `consume(p, TOK_LPAREN, "'('")`（不匹配即 `parse_fatal()`，`src/parser/parser.c:130-141`）⇒ **任何解析成功的程序都构造不出 `STMT_WINDOW` 节点。**
- ★ **(b) 因此不可观测 —— 但把它「如果执行」会发生什么测出来了，比「调到另一个内建」更糟**：`src/vm/vm.c:3772`/`:3773` 取 `string_pool[ins.r2]`，`:3776 if (name) {` **没有 `else`**，`:3819` 关掉它，`:3825 if (t->sp >= 0) { value_move(&R[ins.r1], &t->stack[t->sp]); t->sp--; }` ⇒ 小程序的池里没有第 27 个字符串 ⇒ `ins.r2 = 26` **越界** ⇒ `name = NULL` ⇒ **整块被跳过、落到 `:3825` 把栈顶弹进结果寄存器** ⇒ **调用静默返回「上一次压进去的东西」，不报错**。而 `src/vm/vm.c:3808-3814` 的注释逐字写着它修掉了这类（`a missing name is a fact worth reporting, not a value to invent`）—— **那修的是「名字查不到」那条分支；「操作数越界」这条今天仍然静默穿过。** ⇒ **同一条路径上有两个没有 `else` 的口子，一个在编译器侧（`src/compiler/compiler.c:2219`）、一个在 VM 侧（`src/vm/vm.c:3776`），而它们各自的「静默」形状不同：前者编译成空、后者返回陈旧值。** ★ **VM 侧那一个已于 `1a2c14e` 修掉**（用户裁定「只修 VM 侧」）：`src/vm/vm.c:3774` 现在在 `if (!name)` 时挂上 `unknown builtin function (string index %d is out of range)` 并走**既有的** `vm_throw_msg` 路径 —— **没有新发明错误形状，也没有加 `else`**（守卫放在没人引用的两行上，`docs/AUDIT.md` 一处号都不用重取；加 `else` 那版实测让 `unanchored` 561 → 562，当场退掉）。可红对照：同一份 `r2=26` 的构造物，改前 `rc=0`、stdout=`abcdef`、stderr 空（**`len("abcdef")` 返回了它自己的实参**），改后 `rc=1` 且 stderr 有那个索引；合法程序 `rc=0`、`150/150`、pins held。**下面这段「整块被跳过、落到 `:3825` 把栈顶弹进结果寄存器」由此成为历史读数**（结论过期，行号与引文都还成立）。
- 边界（它自己标的）：第三条是**读码**不是运行读数（构造不出能到达 `case STMT_WINDOW` 的程序）。未测：`case STMT_WINDOW` 的**删除是否安全**；`src/vm/vm.c` 那条越界静默路径**是否还有别的调用者能触发**。
**★ 第十七个实例 + 五条判据（`ivory-ember` 的，全部是读数）。** 它给四个检查器**各注入一个非 UTF-8 文件到它自己的输入集里**（用它们自己的 `main()`、`REPO_ROOT`/`ROOT` 指到 `/tmp/nonutf8/inject`，仓库一个字节没动）：
```
check_links           rc=1  names the file: True
   BROKEN  bad.md  ->  <unreadable>  ('utf-8' codec can't decode byte 0xff in position 10: invalid start byte)
check_doc_paths       rc=1  names the file: True
   BROKEN  bad.md  ->  <unreadable: 'utf-8' codec ...>  (no such file)
check_text_integrity  rc=0  (bad.js IS in its input set, and is invalid UTF-8)
   output: 'check_text_integrity: 1 text file(s), 0 with NUL bytes.'
check_orphan_fixtures rc=1   names vtest/orphan.im: True   names vtest/orphan.inim: False
```
⇒ **①「在扫描集里、读失败、而输出仍绿」这个状态，在这四份里一处都不存在** —— 每一条失败路径都是响的（进 `broken` 或 `sys.exit`）。⇒ **可分性不是靠输出做到的，是靠「没有那个状态」做到的。**
⇒ **② 但「不在扫描集里」与「在扫描集里」在输出上确实不可分**：`check_text_integrity` 只印一个总数（`N text file(s), 0 with NUL bytes`）、**从不印每文件名单** ⇒ 读者无法从那行判断某个文件是被白名单排除、还是被算进了那个 N。**要判只能去读它的白名单。**
⇒ **③ 而那一行 rc=0 是本轮最扎人的读数**：`bad.js` **在输入集里、被读了、通过了** —— 因为**它的判据是「有没有 NUL 字节」，不是「是不是文本」**。
**★ `errors='replace'` 判死（读数，不是论断）**：把那 27 个非 UTF-8 文件用 `errors='replace'` 解一遍 ⇒ **27 files, U+FFFD total = 724661**（最大三个：`selfhost/tests/bg.bmp` 480004、`Infiverse_standard/src-tauri/icons/icon.icns` 113254、`icon.ico` 25974），**全部 27 个的替换字符数都超过全仓最坏的 mojibake（619）**；replace 解出的 `icon.ico` 前 90 字符逐字 `'\x00\x00\x01\x00\x01\x00\ufffd\ufffd\x00\x00\x01\x00 \x00(\x08\x01\x00...'` ⇒ **它是一个合法的 `str`** ⇒ 下游每一条「拿字符串做判断」的检查都会照常跑完、给出一个完整形状的答案。
⇒ **★ 更硬的一条：这个修法对那 27 个文件一点用都没有** —— 它们**不是解码失败被跳过的，是按白名单就不在输入集里**：**四个检查器的输入集里，27 个总共只命中 2 个**（`ai_browser_diag.js`、`examples/legacy-ui/desktop.html`，都只落在 `check_text_integrity`，而它**根本不解码**）⇒ **`errors='replace'` 能影响的文件数 = 0；它会新造的 mojibake = 724661 个替换字符。**
- **④ 三分法的第四个格子**：`check_text_integrity` 把 **`ai_browser_diag.js`**（2556 字节、**0 个 NUL**）与 **`examples/legacy-ui/desktop.html`**（4880 字节、**0 个 NUL**）**算进「text file(s)」并放行** ⇒ **那个阶段的「text file(s)」是文件名的属性（`.js`/`.html` 在白名单里），不是字节的属性；而它的判据（NUL）与这个集合的名字正交。** 不是①解不开、不是②解出替换字符、不是③有 NUL，而是 **④扩展名说是文本、字节说不是、而判据两样都不问**。
- **⑤ 一个不带 reason 的报告格式，会自己编一个**：`check_doc_paths` 的红对照逐字 `BROKEN  bad.md  ->  <unreadable: 'utf-8' codec ...>  (no such file)` —— 它的 `broken` 类型是 `(source, path)` **没有 reason 字段**，印的时候**硬拼了一句 `(no such file)`** ⇒ **一个存在但读不了的文件，被报成不存在的文件。** 对比 `check_links`：那边 `broken` 是三元组 `(source, target, why)`，印出来是 `<unreadable>  (utf-8 codec …)` —— **同一个形状，一个带 reason 一个不带，不带的那个就自己编了一个。**
- **⑥ 知识在仓库里，不在输入集里**：`check_orphan_fixtures` 的 `*.im` glob **看不见 `.inim`**（红对照 `names vtest/orphan.inim : False`）；今天那唯一一个 `vtest/params_precompiled_v06.inim` 没被漏掉，**是因为有人把它的说明写进了旁边那条 `ALLOWED` 条目的正文里** ⇒ **下一个 `.inim` 不会有这种运气。** 仓库里今天共 **9 个 `.inim`**（`projects/` 7、`vtest/` 1、根 `nst2.inim` 1），**没有一个在任何检查器的输入集里。**
- **⑦ 三种解码行为，各自的位置**：`strict`（`check_links.py:152`、`check_doc_paths.py:188` ⇒ 抛、进 `broken`、响）；`replace`（`check_test_ports.py:58`、`migrate_report.py:47`、`check_ignored_credentials.py:57` 及一批 `*.test.py` ⇒ 不抛、制造替换字符、**把失败伪装成数据**）；`surrogateescape`（`check_orphan_fixtures.py:104`/`:109`、`check_orphan_targets.py:142`、以及三个检查器**路径名**的解码 ⇒ 不抛也不替换、坏字节原样留在 `str` 里，**可逆**（`encode('utf-8','surrogateescape')` 能还原），但**下游任何 `print`/`json.dumps` 都会在写出时炸**）。
- **⑧ 一条拒绝测量的理由（要进产物）**：它**没量** `src/compiler/compiler.c:2211` 那条被糊掉的注释与 `:2219` 无 `else` 分支的相邻性 —— 理由逐字：**「一个距离分布要有一个零模型」**（那个文件里有 619 个 U+FFFD、分散在很多行），**没有零模型，量出来的是「它们在同一屏里」，那不是判据。** ⇒ **一条「我没量」若写出理由，它的价值等于一次测量；写不出理由的「我没量」只是缺口。**
- 它**独立复核了我的 14 个**（同一命令、逐文件计数**逐个相同**）⇒ 现在这个数有**三份独立读数**（我的、`docs/BOARD.md:150` 的、它的）。
**★ 第十八个实例 + 四条判据（`vivid-anchor` 的，读数）。** 它普查了全仓 `OP_CALL_BUILTIN` 的发射点：`git grep -n 'OP_CALL_BUILTIN' 344a37b -- . ':!docs/' ':!future/'` 共 32 处，**非发射点 6 处**（`src/compiler/bytecode.h:19` 枚举、`src/vm/vm.c:3099` 分派、`src/vm/vm.c:5244` 反汇编器、`mods/debug/debug_mod.c:156` 反汇编器、`selfhost/compiler.im:35` 常量 `OP_CALL_BUILTIN = 27`）⇒ **发射点 26 个**：`src/compiler/compiler.c` **24 个** + `selfhost/compiler.im` **2 个**（`:377`、`:506`）。
- **25/26 可追溯到字符串池下标**：`compiler.c` 里 **13 个直接是 `bytecode_add_string(...)`**（含**跨行的两个** `:1612`/`:1620`，操作数写在下一行）、**10 个是变量且赋值就在紧邻的上一两行**（`:464`/`:576`/`:588`/`:945`/`:964`/`:1010`/`:1168`/`:1590`/`:1762`/`:2053`，其中 `:2053 bidx <- :2039 int bidx = bytecode_add_string(comp->curBC, bname);` **就是 `gui_%s` 那行**）；`selfhost/compiler.im` 两个走 `add_str`（定义 `:139-143`，返回自举编译器的池下标）。
- **唯一不可追溯的 1 个 = `src/compiler/compiler.c:2225`**，操作数 `builtin_idx` 来自 `:2219 int builtin_idx = lookup_builtin(comp, "window");` ⇒ **编译器内建名表的下标**（表 `:2915-2943` 共 29 条，`"window"` 固定第 **26** 位 `:2941`）。
- ★ **「今天不执行」与「今天恰好不越界」不是一回事 —— 而这里连「恰好不越界」都不成立**：`src/vm/vm.c:3772` 要求 `ins.r2 < t->code->string_count`，而操作数是**常量 26**；用 `bytecode` 子命令读池大小，`w3.im` 的池 = `["window","t"]`（**2 个**）、`w_shift3.im` 的池 = `["a","b","c","window","t"]`（**5 个**）⇒ **26 越界 24 个位次，一执行就必然越界。** ⇒ **判据：现状与性质要分开写；「它今天不执行」不能顶替「它执行时会怎样」。**
- ★ **越界时 `src/vm/vm.c:3776` 那个没有 `else` 的块会把它变成什么 —— 三种结局，没有一种是对的**：**池 ≤ 26**（实测的 2 与 5 都在这一档）⇒ `name = NULL` ⇒ 整块跳过 ⇒ 落到 `:3825` **把栈顶弹进结果寄存器**（静默返回上一次压进去的值）；**池 ≥ 27** ⇒ `name = string_pool[26]`，一个毫不相干的串 ⇒ 走到 `:3790 builtin_lookup(vm, name)`：那个串若恰好是内建名就**静默调到那个内建**，否则抛 `:3816 unknown builtin function '<那个不相干的串>'` —— **一条指向错误对象的报错。**
- ★ **「不伪造字节码」这条边界要进产物**：它**没有**伪造一个越界字节码去喂 VM，理由逐字「**那是造数据，不是测量**」。⇒ 与「构造不出触发程序就标明是读码」是同一纪律的两半：**读码要说自己是读码，而补上读数的那一步不能靠编造输入。**
- **一条同族形态（它自己的）**：它第一遍的正则**漏了两个跨行的发射点**（`:1612`/`:1620`，操作数写在下一行），人工核过才发现 ⇒ 与 `ivory-ember` 的跨行普查同形：**「操作数在这一行」与「操作数在下一行」在按行的正则里是两个不同的集合。**
- **一条只登记的差异**：`selfhost/compiler.im` 的 `add_str`（`:139-143`）**不查重、只追加**，而 C 侧的 `bytecode_add_string`（`src/compiler/bytecode.c:66-69`）**先扫一遍等值串再决定是否追加** ⇒ **同一个名字在两套编译器里可能落在不同的池下标上。** 两边各自自洽，但差异本身值得记一笔。
**★ 第十九个实例 + 五条判据（`noble-zephyr` 的，读数）。** 它普查了 `projects/**`、`svprobe/**`、`examples/**`、`mods/**` 四个目录里 **70 个受跟踪 `.im`**（`projects/` 16、`svprobe/` 28 含 `old/` 9、`examples/` 24、`mods/` 2），观测点 `main @ c68894b`（`git diff --stat c68894b d94622b -- <四目录> CMakeLists.txt tools/` **空** ⇒ 观测点移动不影响这批数）。
- **注册为 CTest 的：0/70。** 全部 `add_test` 里 COMMAND 带 `${CMAKE_SOURCE_DIR}/….im` 的目标，**目录直方图 = `vtest/` 74、`selfhost/compiler.im` 1、仓库根 `contract_test.im` 1** —— 四个目录里一个都没有。
- ★ **被执行（≠被断言）的只有 2 个，而其中一个每一轮都在跑**：**`mods/utils/main.im`** 被引擎**每次运行都自动加载**（它 `say "[mod] utils loaded"`、`say "[mod] self-check init=" + str(mod_init_time)`）⇒ **每一次 CTest 都在执行它**；**但没有任何断言** —— `grep -rn 'self-check init' --include='*.py' --include='*.js' --include='*.sh' CMakeLists.txt` = **0** ⇒ **把它输出的那个数从 42 改成 43，没有人会红。** ⇒ **「能跑」与「被观测」是两件事；这是「能跑但没人看」最纯的样本。** 另一个 `mods/debug/main.im` 只由 `inimerse debug <script.im>` 拼接（`src/main.c:1150-1157`），门禁里从不执行。
- **有「别的观测者」的：0 个。** 其余 66 个的引用**全在 `docs/**` 的散文里**（`docs/HYGIENE.md` 的清单、`docs/streams/*`、STATUS/BOARD）—— **没有一行是可执行的东西**；`svprobe/sv_ctest.sh`/`sv_build_refs.sh` 受跟踪但**一个 `.im` 都不跑**（外部 worktree 的 `ctest`/`cmake` 驱动，且硬编码 `.worktrees/{sv-ref-prev,sv-ref-be,sv-ref-count,surgery-verify}`），`git grep 'sv_ctest\|sv_build_refs'`（排除 `svprobe/` 自身）= **空** ⇒ 没人调用。⇒ **这 70 个文件唯一的执行者，是人在终端里敲 `./build/inimerse <file>`。**
- **真 rc（`timeout 15 ./build/inimerse <file>`，二进制 = 2026-10-06 16:03）**：**rc=0 = 38、rc=1 = 31、rc=124 = 1**；跑完 `git status --porcelain` 空。三栏分法：**真红 6** / **能跑但没人管 38** / **有意的红 26**（`be` 负向记录 17 + `meta_test` 的 `->` 1 + 平台门 7 + 超时 1）。⇒ **「38 个 rc=0」不是好消息**：它们没有任何断言，改坏了也没人知道。
- **平台门 7 个是文档里已写明的**：`canvas_demo`/`projects/demo/main`/`es_200`/`es_400`/`es_800`/`es_1600` 报 `unknown builtin function 'gui_stage'`、`im2d_test` 报 `'im2d_init'`；两个内建都注册在 `src/mod/gui_mod.c`（`im2d_init` `:3851`），而 `gui_mod.c` **只在 `if(WIN32)` 里编译**；**`CMakeLists.txt:872-874` 自己逐字写着**「io_mod/gui_mod register str2int/noise2d/gui_*, and the **POSIX build stubs those modules out**, so calling them now throws "unknown builtin function"」⇒ **不是坏，是平台，而且这条已经被写下来了。**
- ★ **一个新发现（独立于本轮主题）：引擎的中文错误信息是「双重编码后的 UTF-8 文本」，不是 GBK 字节。**（本节末尾有更正块；这句话的第一版把机制写反了。） `projects/host/main.im` 的 rc=1 报 `错误: 无法读取文件 'sync.im'`，**而它在终端里显示成 `閿欒: 鏃犳硶璇诲彇鏂囦欢 'sync.im'`** ⇒ 源文件里就是那串乱码字符本身，引擎原样打印（逐字节相同，无转换）——**终端是无辜的**。⇒ 与本档已登记的 **14 个含 U+FFFD 的文件**、**27 个非 UTF-8 文件**、**`src/compiler/compiler.c:2211` 被糊掉的注释**是同一层的三个入口：**这个仓库的编码不是一处坏了，是一条链上有多个地方各自坏过。**
- **六个真红**（rc=1、不是记录、不是平台、不是超时）：`projects/host/main.im`（`错误: 无法读取文件 'sync.im'`，缺同级依赖）、`projects/set_comp_test.im`（`[exception] uncaught: comprehension: source set is not enumerable`）、`projects/tt1.im`（`[exception] uncaught: x`）、`examples/scripts/verse_biome_demo.im`（`Error: '+' is not defined for arrays/dicts (arr + [x] silently loses data; use push(arr, x…`）、`svprobe/label_old.im`（`error: unknown label 'SKIP'`）、`svprobe/p_colon_enforce.im`（`[exception] uncaught: type_mismatch`）。**它只判了前四个，后两个不下结论**（名字暗示有意，但没有 `be` 那批的抬头自述）—— **这是正确的留白。**
- ★ **一条它自己标出的射程洞**：观测者只搜了**字面路径/文件名**（`git grep -F`）⇒ **靠拼接、变量、glob 到达这些文件的观测者搜不到**；它只额外手查了 `CMakeLists.txt` 的 `.im` 目标目录直方图来补这个洞，**没有**穷举 `file(GLOB)` 与 `.sh`。⇒ **与「按形状搜邻域」同族：一个按字面名建的输入集，它的射程就是「按字面名能到达的那些」。**
- **另两条边界**：`rc=124` 只说明「15 秒内没跑完」，**没有区分「慢」与「死循环」**（它判 `cpu1.im` 是慢，依据是源码与同族 `cpu8.im` rc=0，**不是**跑到底）；第 3 类引的 `CMakeLists.txt:872-874` 讲的是 `contract_test.im` 那套 probe 的处理方式，**不是它实测过那 7 个文件在 Windows 上的行为**。
**★ 第九个形态：切分 —— `str.split()` 是一个会改变集合大小的操作，而它长得像「把输出读进来」（`ivory-ember` 自报）。** 它写的是 `git ls-tree -r --name-only <ref>` 的 `stdout.split()`，而 **`str.split()` 按空白切** ⇒ 两个**含空格的受跟踪文件名**（`future/archive/Inim OS总纲.md`、`future/archive/Inim OS特性.md`）各被切成两个 token ⇒ **917 + 2 = 919**：
```
ls-tree -z (NUL-separated) = 917
ls-tree    (newline)       = 917
ls-tree    .split()        = 919        <- 它用的就是这个
wc -l                      = 917
names containing space/tab/newline = 2
```
**正确切分是 `-z` 的 NUL（`git ls-files -z` / `git ls-tree -z`）或 `splitlines()`。** ⇒ **分子是对的、而且很稳**：**27** 在 `2d0ecd8`/`439df94`/`f575fd6`/`4af0409`/`cfda74c` **五个 ref 上逐个量过、全是 27**。**错的只有分母。**
⇒ ★ **一般式（要进产物）：分母的错误不会让分子看起来可疑** —— 27 一直是对的，所以那个 919 从来没被怀疑过。⇒ **一个数被怀疑，靠的不是「它旁边有别的数」，是「它旁边有一条产出它的命令」。**
⇒ 它已落在产物里：本档上一段与那笔提交信息都写着 `919` ⇒ **本档那处已更正为 917（分子不动）；提交信息改不了 ⇒ 登记为记录，不假装它没发生。**
**★ 第八个形态：同一个通配符在两种工具里不是同一个集合（它自报，且它差点把它报成仓库缺陷）。** 它先看到 `git ls-files -- 'vtest/*.im'` = **79**、`glob.glob("vtest/*.im")` = **78**，形状是「一个受跟踪的 fixture 从工作区里消失了」；**实测那个文件在盘上（`vtest/params_sub/relative_script.im`，42 字节）** —— 差的是**两个不同的通配符**：**`git` 的 `*` 跨 `/`**（匹配子目录），**`glob` 的 `*` 不跨 `/`** ⇒ **79 与 78 是同一个集合被两种通配符量了两次。**
⇒ **准确说法**：检查器印的 `124` 是「**glob 看得见的**」，那个前缀下受跟踪的是 `125` ⇒ **差的那一个不是未跟踪的，是子目录里的、glob 看不见。今天它的输入集比仓库小 1，不是大。** 两个方向都存在，今天是「小」。
**★ `surrogateescape` 会不会被写出去：会，三条路径；而写出去时会炸 —— 实测。** 逐站点追用途：`check_orphan_fixtures.py:104`（`cmake` 只喂 `name in cmake`）、`:109`（`node_text` 只喂 `name in node_text`）、`check_orphan_targets.py:142`（`cmake` 只喂解析）⇒ **一个字都不进输出**；而 **三个检查器的路径名**（`check_text_integrity.py:120` ⇒ `:182` `print` **stdout** / `:170` `sys.exit` **stderr**；`check_links.py:103` ⇒ `:187`/`:189` `print` 与 `:176` `json.dumps(ensure_ascii=False)` **stdout**；`check_doc_paths.py:121` ⇒ `:211` `print` **stdout**，**原写 `:124`**）⇒ **会**。
```
stdout encoding='utf-8' errors='strict'   stderr.errors='backslashreplace'   locale zh_CN.UTF-8
  print(s)                        -> UnicodeEncodeError: 'utf-8' codec can't encode character '\udcff' in position 3: surrogates not allowed
  sys.stdout.write(s)             -> UnicodeEncodeError: ...
  json.dumps(ensure_ascii=False)  -> UnicodeEncodeError: ...
  sys.stderr.write(s)             -> ok
  sys.exit(s)                     -> SystemExit (wrote to stderr)
```
⇒ **stdout 是 strict、stderr 是 `backslashreplace`** ⇒ **红报告里走 stdout 的那几行会以一个 `UnicodeEncodeError` 结束，而不是以那条 BROKEN 行结束。** 红对照（注入、仓库一字节没动）：干净跑 rc=0、surrogate 出现在输出里 **False**；offender 跑 rc=1、印出 `['  /tmp/nonutf8/surr/bad\udcff.js  (1 NUL)']`、surrogate 出现 **True**。
⇒ ★ **三条路径都只在「已经红了」的时候才走到**（offender / broken / `--verbose` / `--json`；干净跑只印计数）—— **而「已经红了」正是读者最需要看清文件名的时刻** ⇒ **它在最该说话的时刻失败。** 与「一个不带 reason 的报告格式会自己编一个」同族：**报告格式在它最该说话的时刻失败。**
⇒ **写这句话时那个状态不存在，也是量出来的**（观测点 `main @ cfda74c`）：`git ls-files -z` ⇒ **917 个受跟踪路径，名字不是合法 UTF-8 的 = 0** —— **这个分母随每一笔加文件的提交变**（`9aa8442` 上已是 **919**，多出的两个是 `tools/check_line_refs.py` 与 `tools/check_release_tags.py`；分子今天仍是 **0**）⇒ 与跨行那条同形：**不是「已覆盖」，是「未出现」** —— 只是这一次连「未出现」都是读数。
**★ 它自己的第二个假绿（要进产物）：把输出重定向进 `StringIO`，于是根本不会发生编码。** 它第一版测 surrogate 时重定向进 `StringIO` ⇒ 读到的「`print` 没炸」是**假绿**；第二次用**真 stdout** 才得到上表。⇒ **与「构造空绿 / 约定空绿」同形：我测的是「我重定向到的那个东西」，不是「真正会写出东西的那个东西」。**
**★ 配方三段（已修正为 `-z`，不会再产出 919）**：脚本用 `subprocess.run(["git","ls-tree","-r","-z","--name-only",REF]).stdout.split(b"\0")` 取名字、用 `importlib` 加载 `tools/check_text_integrity.py` **并直接调用它自己的 `is_text()`**、其余三份各按其自身谓词（`links` = 后缀 `.md`/`.markdown`；`doc_paths` = `README.md` 或 `docs/`|`future/` 下的 `.md`；`orphan_fixtures` = `vtest/*.im` 或 `tools/*.test.{py,js}`）；期望输出（`main @ cfda74c` 实跑）：`ref = main` / `tracked files = 917, not valid UTF-8 = 27` / **`text-integrity 2 of 27`、`links 0 of 27`、`doc-paths 0 of 27`、`orphan-fixtures 0 of 27`**。
**★ 第二十个实例（本轮最重要的一条）：一条「只有编译器能到达」的路径，其实是「数据可达」的 —— 全部实测。**
- **有加载路径，且是一等入口**：`src/main.c:308 if (plen > 5 && strcmp(path + plen - 5, ".inim") == 0)` → `bytecode_read_file`（声明 `src/compiler/bytecode.h:145`，格式注释 `src/compiler/bytecode.c:333`「8-byte magic "INIMBC"+version + bytecode_write stream」）；CLI 上就是 `inimerse run <script.inim>`（`src/main.c:180`），`compile`/`buildc` 反向产出（`:181`/`:182`）。受跟踪的 `.inim` 共 **8** 个（`projects/{exc_test,set_comp_test,set_test,t1,tt4,tt6,tt7}.inim`、`nst2.inim`、`vtest/params_precompiled_v06.inim`）。
- **加载器对 `r2` 零校验**：`src/compiler/bytecode.c:268 if (fread(bc->code, sizeof(RegInstruction), count, f) != (size_t)count) goto fail;` —— 整个指令数组**按裸块读入**，逐条没有任何检查；读入器里所有 `for` 循环只覆盖字符串池、浮点池、func、thread、try、global_names，**没有一个碰 `bc->code` 的操作数**。`RegInstruction` = `{OpCode op; int r1; int r2; int r3;}`（`src/compiler/bytecode.h:69-74`），16 字节定长，`r2` 原样收下。
- **三种结局全部实测**（构造物在 `/tmp/oob/`，仓库一字节没动）：原程序 `x = len("abcdef")` / `say str(x)` 输出 `6`（对照）；`inimerse compile` 得 532 B 的 `.inim`，按格式解析（8 字节头 `INIMBC\x03\x00` + count + 字符串池 + 浮点池 + `count×16` 字节指令）后把每条 `OP_CALL_BUILTIN` 的 `r2` 改成 **26** 再落盘：
  - **结局一（池仍 3 个串、`r2=26` 越界）⇒ rc=0、stdout=`abcdef`、stderr 空** —— `len("abcdef")` **返回了它自己的实参**（`LOADK_STRING r1=1,1` → `PUSH_REG r1=1` → `CALL_BUILTIN r1=1,r2=26,r3=1`；`name=NULL` 使 `src/vm/vm.c:3776` 整块跳过，落到 `:3825` 把刚压进去的栈顶弹进 `r1`）⇒ **静默返回栈顶的直接证据。**
  - **结局二（池补到 27、`pool[26]="upper"` 真内建）⇒ rc=0、stdout=`ABCDEF`** —— 且它与结局一**被判别开了**：若静默返回栈顶应得 `abcdef`，实测 `ABCDEF` ⇒ **确实调用了 `upper` 而不是 `len`** ⇒ **静默调到另一个内建的直接证据。**
  - **结局三（池补到 27、`pool[26]="zzz_not_a_builtin"`）⇒ rc=1、`[exception] uncaught: unknown builtin function 'zzz_not_a_builtin'` at `ip=3`** —— **那个名字在用户程序里根本不存在** ⇒ **报错指向错误对象的直接证据。**
- ⇒ **一句话：这条路径由数据可达，三种结局都不需要编译器参与；而 `src/vm/vm.c` 里那三个口子与「谁产生了那个 `r2`」无关。** ⇒ **判据：一条「今天不执行」的分支，它的可观测性不能只问「谁能构造出触发它的程序」—— 还要问「谁能构造出触发它的数据」。**
- ★ **另一条判据：「差异」的第三种分类。** `selfhost/compiler.im` 的 `add_str` 不查重那条差异，答案既不是「理论差异」也不是「今天没被观测的差异」，而是 **「被工具明确声明为不比较的差异」**：`selfhost_codegen_parity`（`CMakeLists.txt:261-263`、`tools/selfhost_compare.py --check --engine <inimerse>`）记录 `bytecode_match`（`:182`）与 `ops_match`（`:183`），而 `ops_only`（`:88-98`）的 docstring 逐字「**Normalized bytecode: the opcode column only. Register numbers and constant-pool indices are allocation decisions the two compilers are free to make differently; the sequence of operations is what they have to agree on.**」，工具 docstring（`:19-22`）逐字「…**agree on behaviour but not on register allocation, so their bytecode streams legitimately differ. What must not differ is what the program prints.**」，`--check` **断言的只有运行输出**（`output_match`）⇒ **没人漏写测试；是那条测试自己划了界。** ⇒ **判据：说一条差异「没被观测」之前，先查有没有一条观测者明确声明不观测它 —— 那是两个不同的句子。**

**★ 更正块：上一条的机制写反了，而「写反」这件事本身给出了第二十条判据。**
- **更正**：我上一轮写「输出的是 GBK 字节、被当 UTF-8 读」——**错**。逐字节比对：`src/compiler/compiler.c:3072` 的 `fprintf(stderr, "閿欒: 鏃犳硶璇诲彇鏂囦欢 '%s'\n", full);` 与引擎实际吐出的字节**到 `'%s'` 为止逐字节相同** ⇒ **中间没有任何转换，引擎把字面量原样打印**；终端也无辜。**真正的机制是双重编码**：`printf '鏃犳硶璇诲彇鏂囦欢' | iconv -f UTF-8 -t GBK` ⇒ **`无法读取文件`** ⇒ 原文的 UTF-8 字节曾被当成 GBK 解码、结果再存成 UTF-8。`git grep '无法读取文件' -- src/` = **0 命中**，**不是因为它不在，是因为它被糊过了**。
- ★ **由此得到一条判据：一个按「是不是合法 UTF-8」建的输入集，对「被双重编码过的中文」全盲。** 乱码**本身是合法 UTF-8** —— 所以「全 `src/` 都是合法 UTF-8」与「这里有一处中文被糊过」两句话**同时为真**。这与本档已登记的**第十四/十五种**同族：**判据的名字听起来在量内容，实际在量编码。** 同一层的三个入口是：**27 个非 UTF-8 文件**（解不开）、**182 行双重编码**（解得开、是乱的）、**14 个含 U+FFFD 的文件**（解得开、有替换字符）—— **门禁只看第三种。**
- **射程（`src/**/*.{c,h}`，python 与 `git grep -P` 两种数法交叉核对）**：含 **PUA（U+E000–U+F8FF）的行 = 95**、含 **U+FFFD 的行 = 104**、**并集 = 182 行 / 11 个文件**（`src/compiler/compiler.c` 61、`src/compiler/bytecode.c` 36、`src/vm/vm.c` 28、`src/vm/vm.h` 17、`src/main.c` 14、`src/parser/parser.c` 14、`src/compiler/compiler.h` 7、`src/lexer/lexer.h` 2，另三个各 1）。**`src/` 里全部 14 条含中文的 `fprintf(stderr, …)` 行，14 条全是乱码，0 条干净。** 全仓含 U+FFFD 的受跟踪文件 14 个，其中 9 个在 `src/`。
- ★ **可还原的是少数，而「PUA 是洞」这句话本身是错的**（`noble-zephyr` 自报后更正，见本节末尾「可还原性普查」）：逆变换必须**用 GB18030、不是 GBK** —— `iconv -t GBK` 在这里失败（GBK 表没有 PUA 那一段），**GB18030 有**，`printf '閿欒' | iconv -f UTF-8 -t GB18030` 精确吐回 `e99499e8 afaf` ⇒ **`错误` 也能还原**；**真正的洞是 U+FFFD，PUA 只是 GB18030 的用户定义区**。按 `line.encode('gb18030').decode('utf-8')` 逐行判：**(a) 能精确还原 25 行 / (b) 有损 157 行 / (c) 判不出来源 0 行**（原写「至少 16 行」是把 GBK 当成 GB18030 之后的低估）。例：`src/compiler/compiler.c:246` → `/* ---------- 线程与互斥锁 ---------- */`、`:661` → `/* 结果复用左操作数（临时） */`、`:983` → `/* 参数已压栈，临时寄存器可回收 */`）。
- **它不是本仓引入的，而且它还在长**：`src/compiler/compiler.c` 最早出现在 `8248e08 Release Infiverse 0.2.0`，那时**已带 62 行** PUA/FFFD；今天 61。`src/vm/vm.c` **9 → 28**、`src/main.c` 11 → 14。⇒ **继承来的，但没停止增长 —— 这不是考古题。**
- ★ **案发现场是一份规则文件**：`tools/ENCODING_RULES.txt` 逐字写着双轨制「文档/脚本(md/im/tpl): **GBK(936)**」「新内容源文件: **UTF-8**」，外加「禁止 Node 对 GBK 文件做 latin1+utf8 混合读写」「禁止 write_file 直接覆盖 GBK 中文文件」—— **这份文件自己预告了这次事故**，而它里面的工具路径还写着 `D:\inimerse_stable\…`、`C:\Users\Lenovo\Infiverse\…`。⇒ **判据：一份规则文件若只禁止「混合读写」而不指定「哪一侧是权威编码」，它就只是把事故描述了一遍。**
- **`sync.im` 从来没有存在过（不是回归）**：`git log --diff-filter=D -- '**/sync.im'` = **空**；`git log --all -- 'sync.im' 'projects/host/sync.im' 'projects/sync.im'` = 只有 `8248e08`；`git ls-files projects/host/` = **只有 `main.im`**；而 `projects/host/main.im:1` 就是 `import "sync.im"`，import 按**脚本目录**解析（`src/compiler/compiler.c:139` 那条糊掉的注释还原出来正是「import 相对路径解析…：绝对/带盘符原样，否则 base_dir + rel」）⇒ **一个从 Infiverse 搬来、同级依赖没跟着搬、因此从没跑通过的样例。** 六条真红里 `projects/host/main.im` 那一条至此结案。

**★ 第二十一个实例（`vivid-anchor` 的，读数）：受跟踪的 `.inim` 语料与「今天能跑的输入」不是一回事；能把字节码喂进 VM 的入口有四个，四个全是数据可达的。**
- **9 个受跟踪 `.inim`，越界 `r2` 一个都没有**：逐文件解析（8 字节头 + `count` + 字符串池 + 浮点池 + `count×16` 指令），**共查 25 条 `OP_CALL_BUILTIN`，越界 0 条**。**`nst2.inim` 是 0 字节的空 blob**（`e69de29b…`），根本不是字节码 —— 这就是第一遍解析它失败的原因。
- ★ **7 个被版本闸门挡掉**（此处原写「6 个」；名单里就是 7 个名字，`exact-lumen` 逐条跑出 **8 个 rc=1** = 7 个版本闸门 + `nst2.inim` 那个 0 字节空 blob）：`projects/set_test.inim`/`t1.inim` 是 **ver=1**、`exc_test`/`set_comp_test`/`tt4`/`tt6`/`tt7` 是 **ver=2**，而今天的 `INIM_BYTECODE_VERSION` = **3**（`src/compiler/bytecode.h:65`），`bytecode_read_file`（`src/compiler/bytecode.c:346-356`）要求 `magic[6] == 3` ⇒ 实测报 `cannot load bytecode … (old format? recompile with buildc)`。**8 个可解析的里只有 `vtest/params_precompiled_v06.inim`（ver=3）是活输入。** ⇒ **不是缺陷（历史留档），但「语料」这个词会把两者说成一件事。**
- **四个入口，四个全是数据可达**：①`src/main.c:309 bytecode_read_file(path)` ← `load_and_run`（`src/main.c:296`，`.inim` CLI 参数 `:308`）——**用户命名的任意磁盘文件**；②`src/main.c:1072 bytecode_load_from_exe(self_path)` ——**正在运行的 EXE 自己**的尾部 trailer（`TAIL_MAGIC 0x1BC0FFEE`、`BC_MAGIC 0x1BC0FFDB`，`src/compiler/bytecode.c:391-430`），而 **EXE 是一个文件、尾部是普通数据**；③`src/main.c:302-304 zip_extract_all(path, jarCache)` → `runPath = "main.inim"` ——**用户命名的任意 ZIP**；④`mods/build/build_mod.c:337 bytecode_read_file(input)` ——**build 命令收到的任意 `.inim`**。
- ★ **同一条数据路径上，四个入口的闸门不是同一个**：入口 1/3/4 过 `bytecode_read_file`（查 8 字节头的版本），而**入口 2 走 `bytecode_load_from_exe` → 直接 `bytecode_read(f)`**（`src/compiler/bytecode.c:413`/`:428`），**只查尾部 `TAIL_MAGIC`、不查字节码版本**。⇒ **判据：说「这条路有闸门」之前，先问「是不是每一个入口都过同一个闸门」。**
- ★ **`bytecode_read_file_compat`（`src/compiler/bytecode.c:716`，声明 `src/compiler/bytecode.h:147`）调用点为零** ⇒ **「零被当作通过」的一个实例**（编号与前两个的名单见增补三的实例台账；此处原写的第二个已被撤回）。**一个名字里带 compat 的读入器，读起来像一条兼容通路，实际是零。**
- **它自己划的那条线值得记下来**：入口 2 的实际触发**它没测** —— 理由是「在 `/tmp` 里改一个 EXE 的尾部去喂入口 2，我判断它越过了『接受字节码文件的程序喂字节码文件』那条线 —— 那是在改一个不是为字节码设计的容器。**所以我不做，照实写没测到。**」⇒ **这正是边界纪律的正确形态：不是「我不敢」，是「我说得出为什么这一条越线」。**
- **只登记**：`src/compiler/bytecode_capture_probe.c:7 #define BC_MAGIC 0x1BC0FFDB`（同认那个尾部魔数）是否被测试注册，未查；五个注册计数照旧不下判断。

**★ 第二十二条判据（来自一次合并）：一个会被「换掉被指向的那个文件」整体重置的天花板，量的是那一次合并，不是它想压住的那批引用。**
- `stream/pin-shift` 合进 `main` 之后 pin 从 **764/539** 变成 **768/561**，**两个成因、没有一个作者**：①**+3 explicit / −13 unanchored** = `main` 自己的文档长出了带锚的引用（§1.74 的几次追加）；②**+4 explicit / +22 unanchored** = 那一支重写了 `tools/gate.sh`（`227 ins / 32 del`）⇒ **`docs/` 里每一条 `tools/gate.sh:<N>` 同时失去内容锚**。⇒ **天花板是「树」的性质，而换掉那些号共同指向的那个文件，会把它一次性重置。** 已按「pin 在散文之后取」重取，**并把这段成因写进常量的注释里、不写在信里**（当时重取为计数形态 `EXP_LINE_REFS = 768`、`EXP_LINE_REFS_UNANCHORED_MAX = 561`）。★ **这两个常量名今天已经不存在** —— 同一支车道随后把计数形态整体换成了**相对一棵具名基树的 delta**（`EXP_LINE_REFS_BASE` / `EXP_LINE_REFS_DELTA` / `EXP_LINE_REFS_UNANCHORED_DELTA_MAX`，`e18d6d1` + `e6dc449`，我合并在 `32a9418` 之上、并把基树移到 main 尖端）。**这一段留作计数形态的历史读数，不是可跟的指针。**
- 顺带修掉它自己的一处同族病：docstring 里解释天花板的那个句子**把被解释的值写进了句子本身**（「552 is a known quantity today, and 558 tomorrow is an unannounced regression」）⇒ 改成不点值：「one more than the reading below is an unannounced regression」。

**★ 一条判据（来自后缀清单）：一条按后缀定的规则，必须对「这个后缀」成立，不能只对「今天这几个实例」成立。**
- 加完十个后缀之后（`.params .tpl .st .def .bat .java .manifest .gradle .vverse .lock`，`tools/check_text_integrity.py` `10 10` 行数中性），受管文本文件 **854 → 879**（+25 恰好是十个后缀的受管文件数：`.params` 8、`.tpl` 5、`.st` 2、`.def` 2、`.vverse` 2、`.lock` 2、`.bat` 1、`.java` 1、`.manifest` 1、`.gradle` 1），**死条目仍是 13**（十个新条目每一个都命中受管文件）。十条后缀**各配一条可红对照**（取该后缀第一个受管文件、offset 100 插 NUL、先验 `git diff --numstat` 落盘再跑、十条全部 `rc=1` 并点名该文件），之后同一条命令回到 `879 … 0 with NUL bytes … 13 of them match nothing today (stated).` `rc=0`。症状对照：`--required-for` 对碰过 `templates/README.tpl` 的那一笔，**改前 `required: 15 stage(s)`（含 `no rule matches 'templates/README.tpl'; requiring every stage.`）、改后 `required: 1 stage(s)`**。
- ★ **`.svg` 够格、备份后缀不够格**：`.svg` 按定义是 XML 文本 ⇒ 加。而 `.dev`/`.dev28`/`.dev29`/`.hold5`/`.orig`（今天各 1 个，全是 `mods/debug/mod.st.*`）**不加** —— **它们是备份命名，一个备份的内容继承自它的来源：`foo.png.orig` 会是二进制。** 它们今天恰好都是 `.st` 的备份，**那是这棵树的性质，不是后缀的性质** ⇒ 与「那个集合的名字是文件名的属性、不是字节的属性」同一条。

**★ 一条判据（`ivory-ember` 的修复 ①）：一个报告格式若没有 reason 字段，它就会自己编一个。**
- `tools/check_doc_paths.py` 原来的 `broken` 是 `(source, path)`、打印时硬拼 `(no such file)` ⇒ 一个 UTF-8 解码失败的文件也被说成「文件不存在」。修法是**让 reason 随条目走**（三元组 `(rel, path, why)`），**不是按分支拼句子** —— 理由是 `tools/check_links.py:146` 本来就是三元组 `(source, target, why)`：**它从来没有这个毛病；差别在字段，不在打印。** ⇒ **这一件不是「补一个特例」，是把两个检查器的报告格式对齐到那个本来就对的一边。**
- 红对照（真 stdout、真 `main()`、注入输入集）：(a) 只有解不开的文件 ⇒ `BROKEN  unreadable.md  ->  <unreadable>  ('utf-8' codec can't decode byte 0xff in position 4: invalid start byte)`，**输出里不出现 `(no such file)`**；(b) 只有真的不存在的目标 ⇒ `(no such file)` 保留；(c) 两者并存 ⇒ 两条各自的 reason 都在，`--json` 的 `why` 字段两条都带上。A/B（同一棵树）：**改前改后都是 `21 markdown files, 991 backtick refs, 0 broken` rc=0** ⇒ **绿树上一个读数都没变，它只改「红了的时候那句话的形状」。** 行数中性：`6 6`，**六处编辑每一处都是换一行**（该文件被 `docs/AUDIT.md` 按行号引着）。
- ★ **它自报的一处，形状与本档第 ⓪ 条同族**：它把「`git` 的 `*` 跨 `/`、`glob` 的 `*` 不跨」当成新发现报上来，而**它早就写在 `tools/check_doc_paths.py:82-83` 的 docstring 里**（`63c79c3` 加的，逐字 `` `docs/*.md` matches `docs/archive/*.md` too (`*` crosses `/` in a pathspec; measured: 51 matches, 6 of them non-recursive). ``）—— **它读那个文件是为了看它的输入集，不是为了看它知道什么。** ⇒ **「把『我没找到』当成『它不在』」的宾语可以是仓库里的知识，不只是文件。**

## §1.75 增补：三条最小改动落地之后的读数，以及一处从未正确的坐标

**这一节是重取，不是改写。** §1.75 正文里的 `913` / `915` / 「三个不可达条目」/「`:57` 的清单里没有 `.ps1`」是**记录**（各自带着写下时的那棵树）；本节更正块里的「今天 914」是**当时的读数**。三条最小改动落地之后（`c40cc83` + `a1b8822`，合并为 `9aa8442`），今天的读数是：

- `TEXT_SUFFIXES = 44`、`TEXT_NAMES = 4`（原 33 / 4）
- **(A) 结构性不可达 = 0 条**（原 3 条：`.gitignore` / `.gitattributes` / `.editorconfig`）
- **(B) 今天没有受管文件命中的死条目 = 13 条**，逐字 `.bash .cc .cfg .cjs .cmake .csv .editorconfig .hpp .ini .jsonc .ts .tsx Dockerfile`（我的 12 + `.editorconfig` —— **我原把它归在 (A)，错了**：(A) 说的是「匹配规则够不着」（点文件），而它是「一个受管文件都没有」）
- 受管文本文件 **879**（原 830，更早 825）
- `--required-for <碰到 .ps1 的提交>` ⇒ **`required: 5 stage(s)`**（原逐字 `no rule matches 'build_installer.ps1'; requiring every stage.` ⇒ 15）
- `.ps1` 现在在清单的 **`:65`** 那一行（原「`:57` 的清单里没有 `.ps1`」已不成立）

**★ 但更正块本身也会过期，而且它以同样的方式过期。** 更正块里写「今天 914」，今天这个数是 **919** —— 因为它旁边没有观测点，而**它的读数不经过任何门禁**：没有任何阶段会因为「受跟踪文件数变了」而红。⇒ **判据：一个「今天」若旁边没有观测点，它会随下一笔提交变假，而变假时没有人会说话。** 这与 H（现值 + 无人站岗）同形，只是这次的现值住在一段**更正**里。

**两个分母与它们的差**：`9aa8442` 上 `git ls-tree -r -z --name-only` = **919**；`e2501a3` / `aba5ed4` / `2d0ecd8` / `f575fd6` 上都是 **917**，`439df94` 上 **916**。差的两个是 `tools/check_line_refs.py` 与 `tools/check_release_tags.py`（`git diff --name-status aba5ed4 9aa8442 | grep '^A'`）—— **不是「数错了」，是「树长了」**；而 `str.split()` 那个 919 是另一回事（见本节「第九个形态：切分」）。

### 一处从未正确的坐标（F 类的新实例，`ivory-ember` 发现，我独立复核）

本文档上一段那句「`check_doc_paths.py:124` ⇒ `:211` `print`」**错**：解码点在 **`:121`**，`:124` 是一个空行。已就地改成 `:121` 并标出原文。

- `tools/check_doc_paths.py:121` = `for name in proc.stdout.decode("utf-8", "surrogateescape").split("\0")`；`:124` = 空行。
- **历史**：只有 `63c79c3` 与 `509d5d2` 碰过这个文件，`63c79c3` 上解码点就在 `:121` ⇒ **没有任何一笔提交把解码点放在 `:124`** ⇒ 这是 **F（坐标从来没对过）**，不是漂移。同一句里 `check_text_integrity.py:120` ✓、`check_links.py:103` ✓ 都对 —— **三个里错一个**。

★ **它是怎么被发现的，比这一处本身值钱**：`ivory-ember` 找移位的判据是「改前 vs 改后这个号上的内容一样吗」，而它报 `SAME` —— **因为两边都是空行**。⇒ **判据：一个「有没有动」的判据，对「从来没对过」是瞎的 —— 错号只要继续错，就永远比对相等。** 与 H4.1 同源：**两棵树对比只能发现漂移，永远发现不了「从未正确」；后者只能把号对着内容再量一次。**

### 可还原性普查（`noble-zephyr`，并更正它自己上一轮的机制）

**它上一轮说「`错误` 的末两字节 `af af` 变成了 PUA 洞」是错的 —— PUA 不是洞，正确的逆变换是 GB18030，不是 GBK。** `iconv -t GBK` 在这里失败（GBK 表没有 PUA 那一段），`iconv -t GB18030` 有：`printf '閿欒' | iconv -f UTF-8 -t GB18030` 精确吐回 `e99499e8 afaf`。⇒ **真正的洞是 U+FFFD；PUA 只是 GB18030 的用户定义区。**

判据 `line.encode('gb18030').decode('utf-8')`，182 行逐行：

| | (a) 能精确还原 | (b) 有损 | (c) 判不出来源 |
|---|---|---|---|
| 行数 | **25** | **157** | **0** |

(a) 的分布极不均匀：`src/compiler/compiler.c` 22、`src/compiler/compiler.h` 2、`src/vm/vm.h` 1，**其余 8 个文件 0 行**。

**而 (b) 这 157 行不是「坏过一次」，是「至少坏过两次」**，两种分开报：

- **(b-1) 文件里已经有一个 U+FFFD 字面量：104 行** —— 已经烙进去的替换字符，任何逆变换都救不回来。
- **(b-2) 只有 PUA、没有 U+FFFD：53 行** —— 它们逆到一半**字节流本身不是合法 UTF-8**（例 `src/compiler/compiler.c:18` 重建出 `e8b083 efbf 3f 2a2f`，其中 `ef bf 3f` 是一个被截断的三字节序列加 `?`）⇒ **在双重编码之前就已经坏了。** 那一层是什么工具做的，**没测**。

洞的码位直方图（(b) 全部 157 行）：`U+FFFD` 104 行，其余全是 GB18030 用户定义区的码位（`U+E044` 8、`U+E046` 8、`U+E7D2` 7、`U+E100` 7、`U+E0FF` 6、`U+E21A` 6 …）。

⇒ **判据：说一个「不可逆」的规模之前，必须先说清它坏在哪一层 —— 一次双重编码可逆，二次不可逆。**

### `tools/ENCODING_RULES.txt` 的射程：**7 条里，有门禁在守的 = 0 条**

- **`git grep -n 'ENCODING_RULES'` 全仓 1 命中**，就是本文档上一段（在引它）⇒ **功能引用 0 个。**
- 逐条：①「文档/脚本(md/im/tpl) 走 GBK(936)」**没有门禁，而且被仓库自己否定**（`.md` 93、`.im` 386、`.tpl` 5、`.txt` 8 = **492/492 全是合法 UTF-8，0 个 GBK**；三个文档检查器都 `open(..., encoding="utf-8")`）⇒ **门禁不是没守它，是站在它的反面**；②「新内容源文件走 UTF-8」**没有门禁，但它被遵守了，同时 182 行是乱的** ⇒ **「守住了」与「事故」同时为真**；③「唯一允许的读写方式是 PowerShell + .NET（禁止 Node readFileSync/writeFileSync）」**没有门禁，而且这条规则描述的工作方式已经不在了**（实际工具链是 Python + Node）；④「校验：`Test-GbkClean` / `Test-GbkText`」**没有门禁**（`grep -ciE 'gbk|cp936|iconv' tools/gate.sh` = **0**；16 个阶段没有一个碰编码）；⑤「禁止 `write_file` 直接覆盖 GBK 中文文件」**没有**；⑥「禁止 PowerShell 命令内联中文字面量」**没有**；⑦ 两个 Windows 工具路径：`D:\inimerse_stable\tools\enc_utils.ps1` **不存在**、`C:\Users\Lenovo\Infiverse\tools\enc_utils.ps1` **在这台机器上存在**，而 `tools/enc_utils.ps1` 连同 `check_enc.ps1` / `conv_tpl.ps1` / `gbk2utf8.ps1` / `gbktool.ps1` / `patch_gbk.ps1` / `read_gbk.ps1` / `utf82gbk.ps1` **在仓库里**。
- ⇒ **7 条里 5 条连「它描述的那个世界」都已经不在了。** 判据照旧：**一条只写在规则文件里、没有任何门禁在守的规则，与一条不存在的规则在行为上无法区分。**

### `TARGETS` 是一份白名单，而白名单之外的世界不是「没问题」，是「没人问」

`tools/check_line_refs.py` 的 `TARGETS = {"CMakeLists.txt", "tools/gate.sh"}` ⇒ **只有这两个文件的行号引用有站岗的。** 指向 `tools/*.py` 的号**没有任何东西在看** —— 上面那处错了多久、还有多少处，今天没人知道。**这一节自己就是证据**：这句话的第一版把那个号写成裸号，于是它按「本行最近的文件名」继承了行内另一个目标文件，`line-refs` 当场把它算成一条无锚引用 —— **我写「白名单之外没人问」的那一行，正好落进白名单之内，并且被抓到了。**已派一次普查：`docs/**` 里指向 `tools/*.py` 的行号引用逐条对着今天的内容量一次，报「指对 / 指错 / 号不存在」三栏。


## §1.74 增补三：实例台账重排、五种形态、以及六条新判据

**这一节把「零被当作通过」这一族从三个实例重排成五个，并把今天四条车道上量出来的判据并到一起。**

### A. 实例台账（重排后）

| # | 形态 | 实例 | 谁在什么情况下会非零 |
|---|---|---|---|
| 1 | **定义了，从没被调用** | `dsh-session-org` 的 `ensureSessionScope`（零调用点） | 补上调用点即可 —— **但补上之后门还是死的，见 #2** |
| 2 | **注册了，但注册在不在链上的键** | guard 挂在 `createScope(ctx, {sessionId})` 上，而 `tools.guardReason(exec)` 走 `chainLayers(exec.agent)`，agent 的 scope key 是 **agent 对象本身** | 没有任何东西会非零：**注册成功、不报错、永远不被咨询** |
| 3 | **两个部件对同一个键各写各的** | `assignRole` 存角色 id **字符串**，`charterFor` 读**定义** ⇒ 章程 `undefined`、section 名 `session-org:charter:undefined` | 没有任何东西会非零，**也不报错** |
| 4 | **自动路径零调用点，但有一个已发布的引用** | `readings.refresh()`（**零个调用点 + 一个已发布的引用**，`as of 3f612b8^`，fixed in `3f612b8`） | 只有**手动**调用才会非零；线钟（`3f612b8` 加的）之前，`readings.lineage` 整个进程生命周期为空 |
| 5 | **写好了 + 有文档 + 有测试 + 已发布 + 两个方向都没有消费者** | `dsh-session-org` 的 `renderHandoff`（`src/handoff.js:78` 生产路径零调用点；`HandoffLedger` 在 `apply` 里被 new 出来、发布到 `sessionOrg` 服务面，**没有东西消费它，也不在 Remote 面上**） | 两个方向都断 —— 前四个至少有一个方向是通的 |

⇒ **判据：这一族不是一种病，是五种；「零被当作通过」只是它们共同的症状名，而它们的修法各不相同。** 编号口径：**第三个实例是 `setReminderFilter`（没有 store 时是空操作）**，`bytecode_read_file_compat` 与 `renderHandoff` 依次在其后；**本档旧文里「第二个实例 = `vm_register_builtin_safe`」已撤回**（它有调用点，在 `mods/build/build_mod.c:741`）。

### B. ★ 第四格：读数需要一个绑定时刻

**审计实例是关于「某个提交区间」的陈述，不是关于「某个文件」的陈述。在一个已经修好的树上量一个实例，永远是假的。**
- 实测：我在 `dsh-session-org` 的 **HEAD** 上量到 `src/index.js:273`/`:605`/`:658` 三处调用 `readings.refresh()`，据此质疑那个实例；而 `git show 3f612b8^:src/index.js | grep -n` 给的是 **`:466 refreshLineage: () => readings.refresh(),` 一处已发布的引用 + `:505 setInterval(sweep, …)`（那是标题修复那条线，不是血缘）**。⇒ **那三处正是修它时加的。**
- 判据逐字（`glad-falcon` 的）：**「你量到的那个零和我报的那个零，不是同一个零 —— 你的读数晚于主语。」** 我那条判据（「零被当作通过」要说明「非零的东西真的在被读的地方」）由此长出第四格：**一个读数即使非零、即使真的在被读的地方，它仍然可能是假的 —— 如果它不是在主语那个时刻读的。**
- 它与两条已登记的判据同族：**血缘读数需要一个时钟**（`readings` 那处）、用户原话**「按隐藏有延迟反应」**（交付那处）。⇒ **同一个族，第三次出现在三个不同的层上（时钟 / 交付 / 审计）。**
- 落档形态：**实例必须带区间**（`as of <sha>` / `fixed in <sha>`），否则它会在被修好之后继续为真地说话。

### C. ★ 光断言「它存在」不够，要断言「它可达」

- 实例（`glad-falcon`）：`test/arming.test.mjs:338` 钉住宿主半承诺的**工具名集合**恰好是 `escalate,handoff,overseer_review`；把 handoff 的注册删掉，它立刻失败并打印 `escalate,overseer_review` ⇒ **那行输出就是「交接不存在」与「交接可达」的差别。**
- ⇒ **「存在」是一条关于源码的陈述，「可达」是一条关于调用图的陈述，而它们今天在五个地方分开了**（上表五格）。
- 同族（本仓，`vivid-anchor` 的普查）：**一个只扫 `src/` 的「零调用点」普查，会把 4 个活符号报成零** —— `vm_register_builtin_safe`（调用点 `mods/build/build_mod.c:741`）、`compiler_get_using_mods`（`mods/build/build_mod.c:351`）、`vm_debug_threads`（`mods/debug/debug_mod.c:604`）、`vm_debug_jump`（`mods/debug/debug_mod.c:593`/`:599`）。⇒ **`mods/` 是每一个 `src/` 作用域普查的盲区。**
- ★ **第二种盲区不会因为扩大目录范围而消失：跨语言同名。** 排除掉的候选里有两个假阳性来自它 —— `compile_index_set_chain` 命中的是 `selfhost/compiler.im:393` 的**自举编译器同名函数**，`test_drain`/`test_undo` 命中的是 `tools/verse_closed_loop.test.py:134` 的 **Python 测试函数**。⇒ 判据：**一个按名字匹配的普查，假阳性有两种来源 —— 名字在别的目录里、以及名字在别的语言里。**
- ★ **出现次数 ≥ 3 不证明可达。** `bytecode_check_compatible` 有 3 次出现（定义 + 声明 + 一处调用），而它**唯一的调用者在 `bytecode_read_file_compat` 体内** —— 那是一对**互相支撑、但没有入口**的符号。⇒ **一个按出现次数判定零的普查，会把这个死对报成活的；要读调用者。**
- ★ **`bytecode_read_file_compat` / `bytecode_check_compatible` 属于「从来没被调用过」，不是回归。** `git log --all -S` 对两个名字各只返回一笔（`7be36c6`），在那之前 0 命中，在那一笔里两个都存在且**从诞生那一刻起就只有那一条内部调用**；`docs/archive/CHANGELOG_0.5.0.md:142` 逐字 `- \`bytecode_read_file_compat\`/\`bytecode_check_compatible\` matched a non-existent header layout; rewritten against the real INIMBC container`（**这处坐标原报作 `docs/` 直接下属的 CHANGELOG\_0.5.0.md 的 `:129`，两处都错：那个路径不存在，文件在 `docs/archive/` 下，引文在 `:142` —— `doc-paths` 当场红了，它印的是一行 `BROKEN` 加那个不存在的路径。**⇒ 又一个实例：**一条被引的坐标，要么能被某个检查器证明，要么它只是在等下一个读它的人。**）⇒ 判据：**「零」有两种 —— 从来没被调用过（设计遗留）/ 曾经被调用过、后来断了（回归），而它们在今天的样子完全一样。** 边界：`7be36c6` 是一笔**压平的发布提交**（59 文件、+26740/−167）⇒ **一次 squash 会把「它诞生时有没有调用者」变成不可观测的**；那条 changelog 只能证明它被重写过，不能证明重写前后有没有调用者。

### D. ★ 等号该用在哪里（对 §1.74 第二十二条的裁定）

- 我裁 **agent2 的 (b)**：把 `EXP_LINE_REFS` 的等号/上限换成**相对一棵具名基树的 delta**。理由：**(a) 补 22 条内容锚只解决这一次** —— 下一次有人重写被引文件，同样的位移会再来，而这一点今天已经被证明了两次（`9969e5b` 那 850 行、`stream/pin-shift` 那一支的 `tools/gate.sh`）。
- 两件今天没有的东西定下来：**基树 = 一个 HEAD 的祖先 sha**（检查器断言它是祖先，**不可解析 ⇒ exit 2**，用 `--required-for` 那条已有的牙）；**基树过期就让它自报** —— 报告行必须带 **`base <sha> is N commit(s) behind HEAD`**。⇒ 判据：**一个 delta 的权威性来自「基树不动」；正因为它不动，delta 会随主线前进而变大 —— 所以 delta 必须与「它离基树多远」一起印出来。**
- ★ **而 `glad-falcon` 给出了区分这两种等号的判据**（它为自己的工具名集合**保留等号**）：**等号该用在「这个集合的成员身份是有意义的」上；用在「这个集合的成员身份会自己变」上，它测的是噪声。** 工具名不会因为无关编辑漂移，行号会 ⇒ **同样是等号，一个断言的是一条有意义的边界，另一个断言的是一条会自己漂的坐标。** 它的代价它自己认，并把它从「代价」改成「契约」：**让失败信息自己说出这个契约**，于是一次正当的增长是**一行、故意的动作**，而不是一道谜题。

### E. ★ 行数中性是手段，「被引的号没有位移」才是目的

- 两条独立到达同一句：`ivory-ember` 改两个检查器时总行数 **199 → 223 / 220 → 243**，而**被引的号一个都没动**（`:39`/`:80`/`:103`/`:152`/`:99`/`:188` 改前改后逐字节相同）；`exact-lumen` 修 `src/vm/vm.c:3776` 那口子时**不加 `else`**，把守卫放在没人引用的两行上，因为**要保护的不是文件长度，是被引用的行号**（它实测加 `else` 那版让 `unanchored` 561 → 562 并当场退掉）。
- ⇒ 判据：**两者冲突时，报出总行数变了，并证明被引的号逐字节相同。**

### F. ★ 规则文件的射程（`noble-zephyr` 的普查）

**8 个文件在写规则，规则条数合计 ≥ 26，有门禁在守的 = 0 条**（`tools/ENCODING_RULES.txt` 7 条 0 守、`docs/BOARD.md` §「三条硬规则」3 条 0 守、`docs/README.md` §「三条硬规则」3 条 0 守、`tools/README.md` ~8 条 0 守、根 `README.md` 2 条 0 守、根 `imai_sys.txt` 2 条 0 守、`docs/HANDOFF_INFIVERSE.md` §4.6+§5 1 元规则+3 建议 0 守、`docs/SYNTAX.md` §7.4 H4 1 元规则 0 守）。

三条最刺眼的形状：
1. **★ 「三条硬规则」是一个名字两个答案。** `docs/BOARD.md:9` 与 `docs/README.md:20` 各有一节，标题**逐字都是「三条硬规则」**，装的是**六条不同的规则**（3+3，零交集，没有任何交叉链接或消歧）。⇒ **引擎里那条判据（同名注册两次、`builtin_lookup` 只认第一个）的原文，出现在文档层** —— 而它更隐蔽：**引擎里那次至少有一个探测链会出声，文档层没有任何东西出声，读者靠标题找规则。** ⇒ **这两节哪一条是家，是一个决定，不是一个测量。**
2. **★ 同一个量 5 个抄本、4 个错、唯一对的在代码里**：根 `README.md:26` 与 `docs/README.md:32` 写 `out of 97`、`tools/README.md:147` 写 `148 / 148`、`docs/SYNTAX.md:1030` 写 `130`、`tools/gate.sh:149` 写 `150`（唯一权威）。而 `docs/STATUS.md:51` 自己写着「必须与 `tools/gate.sh` 的 `EXP_CTEST` 同步」—— **它指的那个行号也已经不对了**（那一行今天是 `EXP_CTEST` 的默认值定义行，默认值 150，位于 `tools/gate.sh` 的 `stage_ctest` 之前）。⇒ **它不是「数烂了」，是「维持数不烂的那个机制自己烂了」。** 而 `tools/README.md` 自己那张表下面逐字写着 `When one of those numbers changes, update this table *and* the baseline row in docs/STATUS.md §1 — otherwise the next session gates against a stale expectation.` ⇒ **规则写在文件里，文件自己违反了它，没有任何东西检查。**
3. **★ 一条说「规则需要家」的规则，当年没有家；后来有了家，而它举的第一个例子同时过期两次。** `docs/HANDOFF_INFIVERSE.md` §4.6 逐字「**规则本身还没有家**：一次观测 ≠ 一个性质」+「本文件的作者没有权限写那两处，所以它被留在这里」；后来家是 `docs/SYNTAX.md:1021` §7.4 H4；而 H4 同族清单第一条逐字 `` - `EXP_CTEST = 130`（`docs/STATUS.md`、`docs/BOARD.md`）—— 写下来那一刻就没有权威来源 `` —— **今天它是 150，而权威来源存在** ⇒ **数变了，且「没有权威来源」这句已经不成立。** ⇒ 判据：**一条元规则也会成为它自己的实例，而它过期的方式是「它举的例子先过期」。**

另两处同族：`tools/README.md` 抬头给自己发免责声明（「实现状态可能已变化；权威总览以 `docs/API.md` 为准」）⇒ **一份规则文件给自己发免责声明，等于承认「这份文件里的规则没有门禁守」**；它的 Windows 路径那一族（`D:\inimerse_stable\tools\ai_run.ps1` 与 `mcp_server.js` 都不存在，而 `tools/ai_run.ps1`/`tools/mcp_server.js` 就在仓库里）—— **`ENCODING_RULES` 那一族的第二个文件，而这次是活文档（5 个文件引用它）。**
**唯一对的一格也记一笔**：`tools/README.md` 写 Fifteen stages，而 `tools/gate.sh` 里**恰好 15 个 `stage_*` 函数**，`--only` 的合法值也是这 15 个 ⇒ **只报坏消息的普查会让人以为整份文件都烂了，而它有一格是对的。**

### G. ★ 一条「有没有动」的判据，对「从来没对过」是瞎的

- 实例（`ivory-ember` 撞出、我复核成立）：`docs/AUDIT.md:4183` 原写 `check_doc_paths.py:124` ⇒ 解码点其实在 `:121`，而 `:124` 是一个空行；扫该文件每一个历史提交，**`:124` 从不是解码点**（`NO commit has the decode site at :124`）⇒ 这是 **F（坐标从来没对过）**，不是漂移。同一句里另外两个号都对 —— **三个里错一个**。
- 机制：它找移位的判据是「改前 vs 改后这个号上的内容一样吗」，而它报 `SAME` —— **因为两边都是空行**。⇒ 判据：**两棵树对比只能发现漂移，永远发现不了「从未正确」**（与 H4.1 同源）。

### H. ★ 两条来自「错误形状」的判据

- **「不可逆」的规模必须先说清坏在哪一层**（`noble-zephyr` 的逐行普查）：按 `line.encode('gb18030').decode('utf-8')` 判，**(a) 能精确还原 25 行 / (b) 有损 157 行 / (c) 判不出来源 0 行**；而 **(b) 要分两种**：**(b-1) 文件里已经有一个 U+FFFD 字面量 —— 104 行**（已经烙进去的替换字符，任何逆变换都救不回来）、**(b-2) 只有 PUA、没有 U+FFFD —— 53 行**（逆到一半**字节流本身不是合法 UTF-8**，例 `src/compiler/compiler.c:18` 重建出 `e8b083 efbf 3f 2a2f`）⇒ **在双重编码之前就已经坏了**。⇒ 准确说法不是「有损一次」，是**至少坏过两次**：(a) 那 25 行是一次双重编码、可逆；(b) 这 157 行**在双重编码之前就已经坏了**（那一层是什么工具做的，没测）。
- **一条测试可以在「从没走到它命名的那个主语」的情况下通过**（`glad-falcon` 自报，它当场发现并改掉）：同步的 `test()` helper 配 async body ⇒ helper **不 await body** ⇒ `.then()` 里的断言在**汇总行之后**才跑 ⇒ **测试报绿而一条都没检查**。⇒ 与「凡『零』被当作通过的地方……」是同一个东西，**只是主语换成了测试自己**。**而它是在一次「绿」里被发现的，不是在一次「红」里。**

### I. ★ 未出现 ≠ 已覆盖 ≠ 不可达（一处待判）

`exact-lumen` 初读发现：`tools/check_text_integrity.py` 的**所有红报告都走 `file=sys.stderr`**（唯一的 stdout 是那句**不含任何路径**的通过行），而 `sys.stderr` 默认 `backslashreplace`、`sys.stdout` 才是 `strict` ⇒ 同一个炸弹在这个文件上**可能根本走不到**。而 `ivory-ember` 在 `tools/check_links.py` 与 `tools/check_doc_paths.py` 上**把同一个炸弹做红了**（那两个文件的报告走裸 `print()`，即 stdout）⇒ **同一族形状，一个可达一个不可达，差别就是 `file=` 这一个参数。** ⇒ 待它把「可达 / 不可达」逐字量出来后落档；**两种产出都要写清是哪一种**：「它可达，修它」或「它今天不可达，而不可达的原因是这个参数 —— 记下来，别改」。

---

## §1.74 增补四：两笔我自己的欠账、第六种形态、以及五条从「一次绿」里长出来的判据

### A. 两笔欠账（都是我自己的）

**A1. `git add -A` 在一笔合并提交里加的不是「合并」。** 合 `stream/pin-shift` 时我用了 `git add -A`，**把另一个会话（`website`）当时未提交的 14 个文件（`website/**`）一并扫进了那笔合并提交**。它不具破坏性（文件本来就在工作树里，只是被保存了），但它**未经复核就进了 `main`，而且混进了一笔合并提交** —— 事后从 `git show --stat` 里能看见 14 行 `website/...`，而当时没人看。
⇒ **判据：`git add -A` 在一笔合并提交里加的不是「合并」，是「合并 + 任何人恰好留在工作树里的东西」。** 一笔合并提交的语义是「把这些分支合起来」，不是「把此刻的工作树快照下来」；用前者的话写后者的事，会让**别人的半成品获得你的签名**。处置：文件保留（那是真实工作产物）、逐份复核、并记账。

**A2. 运行时版本与 tag 之间没有任何东西在比对（报而不改的缺口）。** `exact-lumen` 报：没有任何门禁把**运行时自报的版本**与**最新 tag** 比（`version_runtime` 只守自洽）。**我的裁定：不加，也不现在加。** 理由：开发期里「树比最新 tag 新」是**常态**，而**一条断言若它的例外状态是常态，它断言的就不是它命名的那个东西**。真要加，加在**发版流程**里 —— **一条只在那一刻有意义的关系，就该只在那一刻被检查。** 记为缺口，不记为待办。
**而它顺手给出的判据更值钱**：**一个「A 必须等于 B」的断言，只有当 A 与 B 由同一件事决定时才是约束；由两件事决定而今天恰好相等，那是一次巧合，而巧合会自己过期。** 它是在 `docs/STATUS.md` 的「`add_test(` 个数必须与 `EXP_CTEST` 同步」上得出这条的 —— `EXP_CTEST` 量的是**跑起来的**条数，`add_test(` 量的是**注册的**条数，注册数还受三个环境条件裁剪 ⇒ **今天两者都等于 150 是巧合，不是约束**。

### B. 第六种形态：一半的线不存在，而另一半正常

`glad-falcon` 的 `09c148f`（跨半写入）：`setLeader` 以前**只写 localStorage**，而 `REMOTE_METHODS` 里只有读方向，宿主 `parentOf` 只认真实血缘 ⇒ **声明的组织图宿主从来不知道**。
⇒ **它落在五种形态之外，是第六种**：不是「定义了没被调用」（#1），不是「注册了但键不在链上」（#2），而是 **一条线只有一半** —— 写方向存在、读方向存在、**而它们不在同一个进程里**。
★ **它危险的地方在于从有 UI 的那一侧看，它与「功能正常」一模一样**：徽章对、缩进对、读回来的东西也对 —— **因为它读的正是它自己刚写进去的那份。** 判据句（它的）：**一条只有读方向的线，会让「读得到」看起来像「同步过」。**
⇒ 台账补第六行；编号口径不变：第三 = `setReminderFilter`、第四 = `bytecode_read_file_compat`、第五 = `ensureSessionScope`、**第六 = 单向断**。

### C. 五条从「一次绿」里长出来的判据

**C1. ★ 边界不赦免结论。** `vivid-anchor` 在 `docs/streams/eidos-state.md` 里写下两句**正确**的边界 ——「`future/archive/` 里的 Eidos 材料我没有逐份读……只看了它的命中计数，没有读内容」与「**我没读，所以我不说它没有**」—— **而这两句上面的那句结论是错的**（它据半份输出断言「没有一条在讲 Eidos 的开放与封闭」）。
⇒ **判据：写下了边界，并不能追认它上面的那句话。** 一条边界的作用是**限制同一份文档里其它句子的射程**；它不会、也不能，让已经说过的话变对。**一份诚实边界的文档，可以同时是一份结论错误的文档** —— 而这正是它最难被自己发现的原因：**边界让人以为已经谨慎过了。**

**C2. ★ 一个用了 `head`/`tail`/`cut` 的读数，射程就不再是命令的射程，而是那个数字的射程。** 同一处：`git grep -n '开放\|封闭' -- docs/ future/ | cut -c1-170 | head -30` —— 命中总数 **62**，`git grep` 按路径排序、`docs/` 全排在 `future/` 前面，**`head -30` 只放出了前一半，`future/` 整段被砍掉**。而输出上**看不出来它被截断了**。
⇒ **它比一次普通的「我漏了」严重一档，因为那条命令看起来是完整的。** 纪律：**用了 `head`/`tail`/`cut` 的读数，必须在读数旁边写出「这是前 N 条，总数 M」；`M` 不写出来，就不许下全称结论。先写射程，再写结论。**
★ **而它与 `exact-lumen` 的过滤器是同一个形状，一天之内两个作者各撞一次**：后者用 `grep -v 'for \|case \|_ in '` 得出「`in` 没有测试」，**过滤器吃掉了 `300 in x.range` 里的 `0 in `**。⇒ **筛法要能被反过来问一次：「我要找的东西，能不能被这条筛子筛掉？」问不出来的筛法不是筛法，是结论的伪装。**

**C3. ★ 一个被移除的拼写，不等于一个被移除的语义。** `be` 于 `c89e077`（2026-10-05）被移除（`docs/SYNTAX.md` §6.3 逐字 `name be <集合或表达式> [: init]  // 已移除：报错，不静默`），**而约束机制活着**：`x: 0,1,2,3 = 2` → `say x` = `2`；`x = 9` → `[exception] uncaught: type_mismatch`（RC=1）。
⇒ **一份「已实现」清单，如果按**拼写**读，会在语言换拼写的那一天整体过期，而它描述的**机制**一条都没变。** 判据：**说一件东西「已实现」之前，先分清你指的是它的名字、还是它的语义** —— 换名字不算回归，换语义才算。
**同族的第二处**（`docs/SYNTAX.md` §6.3 自己的例子）：`age: [0, 120] = 18` **不产生约束**（`age = 200` 被接受、RC=0），而 `x: 0,1,2,3 = 2` 产生约束 ⇒ **括号形式与裸逗号形式行为不同，而文档给的是不生效的那个。** 待判：两种写法在语法树里是不是同一个节点（同一个节点 ⇒ 解析器缺陷；两个节点 ⇒ 未收敛，进 §3.1 的决定点清单）。

**C4. ★ 值 + 无守卫：「有测试覆盖」的强度上限。** 三个计数 —— 模语义 **16**、逻辑语义 **64**、AOT **104** —— **都没有门禁盯着**：`CMakeLists.txt` 里对应的三条 `add_test` 都**没有** `PASS_REGULAR_EXPRESSION`，`tools/gate.sh` 只钉 fuzz 的三个 `EXP_FUZZ_*` ⇒ **删掉 30 条 `logic_semantics` 用例，它仍会打 `ok (34 cases…)` 并且门禁全绿**；而「16」唯一的家是 `tools/mod_semantics.test.py` 的一句注释（该文件只 `print len(TABLE)`，无断言）。
⇒ **「有 CTest 覆盖」今天的意思是「有人跑过它」，不是「它掉下来会有人知道」。** 判据（`noble-zephyr` 的形状，我上一轮已用过一次）：**值 + 守卫 = 安全；值 + 无守卫 = 病。** 一个数在文档里被转写三次（`docs/STATUS.md` → `docs/DECFY_DESIGN.md` → `docs/PLAN_V06.md`），而它掉下来时没有一处会说话。

**C5. 把一条已裁的事写成未裁的，与把一条未裁的事写成已裁的，是同一种病。** `docs/PLAN_V06.md` §3.3 第一版把 Eidos 写成「进引擎，**或者**明确裁定永远外部」，而 §2 已经裁了「进引擎」；§2 的表格又把 Eidos 的权威文档写成「无（连设计文档都没有）」，而 `future/archive/面对对象.md` 的 `## 四、开放与封闭` 就在那里。
⇒ 两处都是**同一件事的两个方向**：一处把已裁的写成未裁，一处把已存在的写成不存在。**它们的共同点是「写的时候没有回去问那句话的射程」。**
**而正确的写法是两层，不能合成一句**：**概念上回答过**（那份归档规范比任何现行文档都细：六类封闭手段各有语法、边界与失败后果，30 处全是决定，默认一律「开放」）；**以现行设计权威的身份，没有**（归档 README 逐字「已被取代、或被更高层文档覆盖」「不代表当前状态，也不代表已实现」）。**一份「被归档的设计」既不是「不存在」，也不是「现行权威」** —— 合成一句话，两个意思都会丢。

### D. 一条通信纪律（不是判据，是处置）

**一条发错会话的指令，正确的处置是原样退回，不是照着执行。** 我把本该发给 `glad-falcon` 的一条长信投给了 `video`（`glad-badger`）；它**没有执行任何一项**，原样退回并说清理由（内容与它的写域毫无关系）。
⇒ **一个把「不属于我的活」照单做掉的下属，比一个把它退回来的下属危险得多** —— 前者会以「已完成」的样子污染一个它无权碰的写域，而错投这件事本身**在结果上看不出来**。
**同族的一条**（`glad-falcon` 的处置，我认）：**一个未裁定的行为，在测试里应该是一条被写下来的边界，而不是一个被断言成正确的值。** 它把「声明上下级是否授予角色」写成边界而非断言 —— 于是那条测试**不声称一个没人裁过的答案**，同时**把这个问题留在读者眼前**。（该问题现已裁定：组织图 = **描述**，不授予角色。）

## §1.74 增补五 —— 「零」的第七、八种形态；引文带观测点

### A. 第七种形态：隔离机制本身在掩盖失败

前六种都是「线不在」或「线接错了」；这一个是**线接对了，而失败被关进一个不报错的房间**。

`dsh-session-org` 的客户端半加了一个取 `uiWorkspace` 的启动步骤之后，`test/client.e2e.mjs` 的三个 ctx 替身都没有 `inject`，于是它抛 `ctx.inject is not a function`；而 `apply()` **按设计把自己的失败隔离掉**（画一个 chip、只写 console）⇒ **整套 e2e 43 条断言，全绿地从一次三行内就死掉的启动上开了过去。**

★ **判据句**：**「`apply()` 从不抛异常」是设计，所以「apply 返回了」不是「apply 成功了」的证据 —— 凡是靠『不抛』来证明通过的地方，都必须另有一个数能非零。**

它比「零被当作通过」更狠的地方在于：后者至少还有一个数可以看，而这里**连那个数都没有**。

处置：启动步骤对没有 `inject` 的 ctx 返回 `undefined` 而不是抛；导出 `reportedFailures()`/`clearFailures()`，e2e 每次 `apply()` 之后断言**报告数为零**。**第一条只是不再抛，第二条才让「没抛」变成一个能被读到的数。**

### B. 第八种形态：「跑了」「抛了」「真的没有」长得一模一样

一次 lineage sweep 可以以三种方式结束，而从外面看**完全一样**：

| 情况 | `parentOf(id)` | 老代码能说出的 |
| --- | --- | --- |
| 从来没跑过 | `undefined` | 什么都不说 |
| 跑了，抛异常（保留旧读数） | `undefined` | 什么都不说 |
| 跑了，真的没有会话 | `undefined` | 什么都不说 |

三种情况下 `sessionIds()` 都是空表、`parentOf` 全是 `undefined`，而**空表是合法输入** ⇒ 没有任何下游能分辨。原代码就是一句 `catch { /* keeps the previous readings */ }`。

★ **它与第七种、与更早那次「函数没有调用点」的关系是「同一个形状，深了一层」**：上一次的修复是**加时钟**，解决的命题是「**它有没有在跑**」；而「**它跑了**」从来不是一个能读到的数。⇒ **上一轮的修复消掉了第一个零，而它下游还压着一个同样不可归因的零。**

★ **判据句**：**一个「零」需要被归因。「它有没有在跑」与「跑出来的那个零是哪种零」是两个问题，而它们在树上的样子完全一样，处置相反。** 更一般地：**「修好了」与「修好了上一层」之间的差别，只有在有人问「那个零是哪种零」的时候才会显出来。**

处置：把 sweep 主体抽成导出的、会记账的函数，并经 `lineageHealth()` 问出来 —— `sweeps`（成功失败都算，**它必须能非零**）、`lastSweepAt`、`lastCount`（最后一次**成功** sweep 看到的数）、`lastError`（失败原文，成功后清回 `null`）。**失败仍然保留旧读数**（不该把好数据抹掉），**但现在它会说自己失败了**。测试里有一条专门断言「**成功但为空**」与「**从来没跑过**」`lastCount` 相同、`sweeps` 不同 —— **它钉的不是值，是那两种零的可分辨性。**

### C. 判据句可以是扫描器，不只是分类器

第八种形态是**扫出来的，不是撞上的** —— 提出它的人拿上一条判据句当扫描器，去翻自己的旧代码。

★ **这是这一族里第一次出现「用判据句当扫描器」的动作**，此前所有实例都是事后归类。⇒ **一条判据的价值不在它解释了多少旧案例，而在它替你找到了多少新案例。** 经验只能等你再撞一次；可执行的判据句能去你没想到的地方翻。

### D. 引文也带观测点（与「读数带观测点」并列，不是并进它）

行号那件事早就立过规矩（位置形式的引用必须能回到产生它的那棵树），**而引文一直不在那条规矩的射程里** —— 那条只管「文件名:数字」。

★ **判据句**：**一条引文也是一条读数，而它需要自己的观测点。带着走的引文没有观测点，所以它过期时没有任何东西会红。** ⇒ **上一轮读过的原文，这一轮要么重取，要么标上「读于 `<sha>`」。**

它发生的条件比「两棵树不同」宽得多：**只要「上一轮读过、这一轮接着用」就会发生，而它恰好是写报告时最自然的动作。** 同一份报告里，**数据是重取的、引文是带过来的** —— 这个不对称本身就是病征。

**实测实例（两跳，第二跳更值）**：

1. 一份独立复核引用了 `docs/PLAN_V06.md` §1 纪律 2 的原文，**引用时逐字正确**；而**把它变旧的那一笔，正是把那次复核的结论落进那份文件的那一笔**。⇒ **它不是读了一棵旧树，是「把一条引文从旧读数里带进了新报告」。**
2. 紧接着，提出这条判据的人在**更正上一条引文错的同一条消息里**，**又写下了一句时态错的引文** —— 它写「它自己已经宣布作废」，而查实：**在它读的那一刻，那一行上没有任何作废通知**；通知是**随更正一起到达的**。⇒ ★ **作废通知是随更正一起到的 —— 在读数被取走的那一刻，树上没有任何东西标记它是即将作废的。所以「带着走的引文」不是一种疏忽，是一种没有对照物的状态：它只能在事后被发现。**

★ **第二跳决定了这条纪律是「可执行的规矩」还是「道德劝告」**：如果读者本可以看见通知而没看，结论只能是「下次仔细点」；如果读者不可能看见，结论只能是机制 —— **要么重取，要么盖上「读于 `<sha>`」。只有后者能防下一次。**
★ **而它的复发间隔可以短到一条消息**：一个已经警觉的人，在刚刚立下这条纪律之后一条消息，就又犯了一次。

### E. 要否定一条记录，得在它记录的那个条件下复现它

一条记录带着它的条件（一个 ref、一段前置声明）。有人拿一个**没有那句声明**的读数，去否定一条逐字写着「真基线 `<sha>` 上」的记录 ⇒ **同一个名字，声明在场与不在场是两个不同的程序。** 他否定的不是那条记录，是他自己搭的那一支。

★ 与之成对的一件：**两个人各改一半、都没有为了凑一个故事而让两个答案一致** —— 一个人给出错的定性，另一个人复跑后才发现自己那条是假警报，两边都自己撤了。

### F. 照抄不核会把结论写反

有人把 `be` 的落点从另一份文档的引文里抄过来，核后：那个行号是**空行**，真实位置在别处，**而且它不是接受、是拒绝** ⇒ **照抄的引文与它的对象差一个字，而那个字正好是「活」与「死」的分界。**

★ 配套一条：**「跑过了」与「在里面」是两个问题。** 先跑门禁、看着三个读数像「已经进去了」，而成员资格的判据是 `git ls-files`，**它根本不看未跟踪文件**（检查器自己逐字写着「There is deliberately no `os.walk` fallback」）⇒ 提交后重跑才看见分母真的动了，而且**立刻报 1 broken** —— 那条坏链是自己引入的。

### G. 三条来自交付面的

- **「如果存在就检查」这种写法本身就是假绿 —— 它比没有断言更坏，因为它计入分母。** 没有断言的地方至少你知道自己没测；写成条件断言的地方，你以为测了，而分母替你作证。
- **空 numstat 是唯一的警报。** 一次红对照第一次是绿的，原因是**变异根本没落地**（替换目标写错、replace 静默没匹配）—— **一个「变异没落地」的绿，与「代码是对的」的绿长得一模一样，破绽只有「那个本该变的东西没有变」。**
- **恒真断言只撑大分母**（一条 `typeof === 'string'` 的封面检查）；**一个数字如果没有出处，它就不是占位符，是一个断言。**

### H2. 第九种形态：一个把两种事实压成一件事的读数（**主语是安全机制**）

与第八种的区别：第八种是「**一个不可归因的零**」，这一种是「**一个把两种事实压成一件事的读数**」。

**观测**：`getMode(id) === 'hard'` 与「会话 `id` 上真的有一个活着的 guard」曾经是**同一个观测**。武装逻辑里有一句「当时不是活的就没什么可武装的」，于是那条路径**直接返回、一个 guard 都不注册**，而下一次走到提交路径的工具调用**照常进行**。两种情况 `getMode` 都回 `'hard'`：

```
getMode('s1')   // 'hard'   ← 它说自己是硬封锁
// 而 state.scopes 里没有 s1，一个 guard 都没有
```

⇒ **「没装」被读成了「装好了而且放行了」。**

★ **判据句**：**`getMode` 报的是意图，不是安装状态，而它长得像后者。** 更一般地：**一个读数如果同时声称了两件本来不同的事实，那么当其中一件不成立时，它不会变红 —— 它会继续声称两件都成立。**

★ **它在安全机制上的分量**：**一个报「我在挡」而实际上什么都没装的读数，比一个报「我不知道」的读数更危险 —— 因为它长得像在工作。** 「我不知道」会让人去查；「我在挡」让人不去查。最坏的情况不是「没挡」，而是**「没挡」被读成了「挡了并且放行了」**。

**处置**：武装逻辑记账，并经 `gateHealth()` 问出来 —— `declared` / `armed` / `unarmed`（**点名而不是计数**：`why` 三值 `'armed'` / `'not-live'` / `'error'`，因为「还没活」与「注册就抛了」是两个不同的问题、修法也不同）/ `lastArmAt`。`declared` 的候选集来自三个声明处，不是从一个地方反推。

★ **最有价值的是它那三条断言，因为它断的不是值**：
```
ok('but nothing at all is armed', health.armed === 0);
ok('while the mode still reads as a hard block', getMode('s1') === HARD);
ok('and no guard was ever registered', calls.guards.length === 0);
```
⇒ **它断言的是「这两个事实不再是同一个观测」。** 一条断言能这样写，说明那个被压扁的读数**已经被拆开了** —— 这是把一次修复钉住的最强形式：**不是钉住修好之后的值，是钉住「它们从此可以被分开问」。**

★ **它在修一个「把两种事实压成一件事」的病时，没有顺手造出下一个同形状的东西**（`unarmed` 是点名不是计数）。这是这一族里第一次出现「修复者主动避开同形状」。

★ **它给出的第二条扫描器收获**：**「用判据句当扫描器，扫到的往往不是别人的 bug，是自己那句『这里应该没问题』。」** —— 第一次扫出的是自己写的代码，第二次扫出的是**自己写的那句「应该没问题」**。

### H. 一条结果段的成功判据

一次重录脚本报了 `exit=0` 与一个字节数，**看起来完全成功**，而编码器在非 tty 下拿不到覆盖回答就**立刻退出、录制根本没开始**，脚本把**旧文件**报成了成功 —— **唯一破绽是字节数没变。**

★ **判据句：一个结果段的成功判据，必须包含「产物本身变了」这件事，而不只是「命令退出码是 0」。**

★ 配套一条被真实事件验过的：**「记录是指针还是记录」不再是一条声明。** 交付记录里的 `ref` 停在录制那一刻，而仓库 HEAD 已经往前走了六个提交 —— **两处 `ref` 都没跟着动。**

### H3. 第十种形态：成功的射程被高估（**主语是「一个成功的射程」**）

**实例（`glad-falcon`，两笔）**：`568fa66` 接上了客户端那一次 `$mount`，用户随即报出浏览器里的第一条错：

```
Error: typert: dsh-session-org#sessionOrgReviews/decideReview parameter id strict codec has no create() factory
```

★ **它的栈里出现了 `Proxy.mountContribution`（`dsh-api-gateway` 客户端半）** ⇒ **`$mount` 成功了，然后被拒绝。** 所以这一族**不是一层，是两层**：第一层是「没有线」（已修），第二层是「**线接上了，但接头被校验器剪掉**」。

**第二层的规则在另一个包里，而且是两道关**：`requireStrictCodec`（`dsh-api-gateway`）只要求 `mode:"strict"`；随后 `validateCodec`（`dsh-typert-registry`）要求 `typeSymbol` 非空**且 `typeof codec.create === "function"`**。★ **上一轮只对了第一道，而第一道通过时什么都没说。**

★ **形状名不是「接头被剪掉」，是「成功的射程被高估」。** 「被剪掉」听起来像有人做了多余的事；实际发生的是**两件成功的事被读成了第三件**：`$mount` 不抛、`mountContribution` 不抛，**这两个成功都被读成了「线通了」** —— 而它们只覆盖到「contribution 被提交」，**不覆盖「被接受」**。

★ **一般形式**：**「A 调用 B 成功」只覆盖到 B 的第一层；B 内部还有几层，必须去 B 里数，而不是从 A 的成功推断。** 主语是**一个成功的射程**，不是**一条线**。

★ **判据句**：**「它成功了」与「它成功了，而且下游也成功了」是两个陈述，而它们在前一个的返回值里长得一样。**

★ **与第九种形态的分别**：第九种（H2）的主语是**一个同时声称两件事实的读数**（`getMode` 报意图、长得像安装状态）；这一个的主语是**一个说得太宽的陈述**（陈述有了，射程宽了）。★ **而这一族我自己犯过一次** —— 增补二里那个被我证伪的前提（把**发射层**的读数当成**注册层**的结论），正是「一个结论的射程写得比它覆盖的宽」。**两条并排放，是因为它们同族、入口相反：那一次是作者的结论越界，这一次是被调用者的成功越界。**

**★ 修法为什么不是宽松占位（这一条是整份报告里最重的）**：`codec.create()` 在**整棵树里唯一的调用点是宿主自己的 `decode()`**，而它读的是**宿主自己的** descriptor —— 那里「没有声明类型的 parameter」被建成 `{mode:"src-json"}`，**永远不会是 strict codec**；宿主同时是校验方。⇒ **客户端那条 codec 是一条它无法履行的声明，只被检查存在性、从不被调用。** 所以修法是让 `undeclaredCodec(typeSymbol)` 返回一个 **`parse` 会抛**的对象，而不是宽松占位。

⇒ ★ **判据句**：**一个宽松的占位会是一个「什么都放过」的校验器 —— 一个看起来像答案的读数。**

**★ 它把「这个测试会红」也断言了**：两条断言跑的是**真的校验器**（`dsh-typert-registry` 的 host face 与 client face 是同一实现 ⇒ `new Context()` + `new TypertRegistry(ctx)` + `ctx.typert.remotes.register(...)` 让生产报错**逐字重现**），其中一条**把 `create` 剥掉、断言它必须以逐字相同的文案被拒**；包名不在本插件依赖里、`exports` 不含那个路径，所以测试从 `process.execPath` 推 DSH 安装根再 `require.resolve`，**解析不到大声失败，不静默跳过**。

⇒ ★ **两条方法论**：**① 它没有去复刻那个校验器的规则，它把真的校验器请进来跑**（「两条断言跑的是真的校验器，不是它规则的副本」）；**② 把一次修复钉住的最强形式，不是钉住修好之后的值，是钉住「它还能红」。**

**★ 编号口径（本次更正）**：本档此前把这一族记作「第十个实例」，而**「形态」与「实例」是两条不同的轴** —— 增补五的 A/B/H2 记的是**形态**（第七/第八/第九种），增补三 A 记的是**实例台账**（重排成五个，口径见那一节）。**这一条是「第十种形态」。** ★ 而我在给它的回信里先写成「第十一个」，**那句话是错的，以本节为准。**
