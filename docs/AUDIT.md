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

**`contract_test.im` 剩下的 4 条失败是结构性的，不是缺陷。** 用一遍自动跳过的脚本（跑套件 → 读 stderr 的 `CONTRACT FAIL: <desc>` → 注释掉该行 → 重跑）穷举出**恰好 4 条**：`str2int`、`str2int invalid -> 0`（`:120`/`:121`）、`noise range`（`:170`）、`vram accounting`（`:174`）；跳过这 4 条后该套件 `rc=0`。`str2int` 由 `io_mod_register` 注册（`src/mod/io_mod.c:315`，`vm_register_builtin(vm, "str2int", builtin_str2int);` 在 `:334`），`noise2d`/`gui_canvas`/`gui_px`/`gui_vram_used` 属 gui_mod —— 而 **POSIX 构建根本不编这两个 mod**：`CMakeLists.txt:383-393` 是 `if(WIN32)` 分支（`src/mod/gui_mod.c src/mod/io_mod.c …` 在那里），POSIX 的 `else()`（`:394-400`）挂的是 `src/platform/posix_stubs.c`，其正文 `#define STUB_REG(name) void name(VM *vm) { (void)vm; }` 后跟着 `STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)` —— **空实现**。`contract_test.im:2` 自己也写着 `# usage: inimerse.exe --time-limit 60 contract_test.im`（Windows）。

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

**计数**：①–③ 的两次修复共 `grep -c 'add_test(' CMakeLists.txt` **108 → 110**（`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **110**）；④ 这条回归再 **112 → 113**（111 是 §1.11，112 是 §1.12）。

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

## §1.14 v3.1 整数位宽：Value 的整数槽改 int64，模糊测试的分歧归零

**症状。** §1.13 把差分模糊测试接进门禁时，默认配置 `--count 120 --seed 1` 实测 `agreed 111 / DIVERGE 5 / THREW 4`，两份计数被**钉**在 `5` / `4`。那 5 条 DIVERGE 里解释器把 `9007199254740993` 算成 `9007199254740992`、把整数除零算成 `inf`；4 条 THREW 是 AOT 抛 `division_by_zero` 而解释器根本不认为那是错。本节记录把这 9 条逐条修掉的过程 —— 以及一个**不是算术缺陷**的发现。

**根因是一个，不是九个。** `src/vm/vm.h:24` 的 `Value` 是 `{int type; int ival; double fval; char *sval; void *ptr;}`，**32 字节、整数槽 32 位**。于是 `src/compiler/compiler.c` 在字面量超过 int32 时把它降级成浮点（`OP_LOADK_FLOAT` 加一条 warning），整条表达式从此走 double：2^53 以上的低位没了，而且**所有整数守卫都不再被命中** —— `2147483647 / 0` 抛 `division_by_zero`，`2147483648 / 0` 却算出 `inf`，因为操作数已经不是整数了。守卫本身是对的，只是永远够不着。

**为什么不能直接把 `int ival` 改成 `long long`。** `docs/DECFY_DESIGN.md:76` 把 `Value` 声明为**永久宽度冻结**：它同时是 VM 寄存器、AOT 生成 C 的 `NV`、wasm 线性内存槽位的共同形状。加宽会让 `sizeof(Value)` 从 32 变成 40，是真正的 ABI 破坏。**改用匿名 union 保住了 32 字节**：

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

**为什么两份必须同步。** `CMakeLists.txt:383` 是 `if(WIN32)`：WIN32 编 `src/runtime/runtime.c`（`:386`），其它平台编 `src/runtime/runtime_posix.c`（`:395`），**一个可执行文件里只会有一份**。`src/runtime/runtime.c:1716-1717` 把 `len`/`size` 注册到 `builtin_len`/`builtin_size`，`src/runtime/runtime_posix.c:1057-1058` 注册到 `posix_core_len`/`posix_core_size`。只读其中一份会预测错行为 —— §1.15 已经真实踩过一次。

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

**设计意图。** `docs/DECFY_DESIGN.md:125` 要求「`OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，**真值产生点唯一**」；`:130` 要求一张 `OpCode → { VM 行为, AOT 行为, wasm 行为 }` 表作为唯一真值源，「三列不一致即构建失败」。`:24` 记下的规则是那条三目链的尾句 `… : (va.type == VAL_NIL) ? 0 : 1`，即**非 nil 且非零为真，空串也算真**。

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

**为什么统一到「空串为真」而不是统一到 `bool()` 的「空串为假」。** ① 前者是 `docs/DECFY_DESIGN.md:24` 写下的规则，且六处里有三处本来就是这样；② 前者**完全不动 `if` 的控制流**，对既有 `.im` 程序的爆炸半径为零 —— 反过来统一到「空串为假」会让每个 `if s` 在 `s` 为空串时静默换分支。代价是 `bool("")` 从 `false` 变成 `true`，这是本次唯一面向用户的语义变化，单独写在明处。

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

v3.1 把 `Value` 的整数槽改成 64 位时，为了不破坏 32 字节宽度契约（`docs/DECFY_DESIGN.md:76`）用了匿名 union：`int type; union { long long ival; double fval; }; char *sval; void *ptr;`。`ival` 与 `fval` **共享存储**，于是所有「只有两种类型」的取值写法都成了读错成员。

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

`src/platform/platform_probe.c` 是既有测试（CTest `platform_probe`，`CMakeLists.txt:190`），本轮把 join 的语义补进去，**没有新增 CTest、`EXP_CTEST` 不变（117）**：

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
（`docs/SYNTAX.md:339-340`）接住错误，`str(err)` 得到错误种类名 `numeric_overflow` —— 这样测试仍然只断言
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
③ 仓库自己的 §1.14 教条是「宁可抛，不要静默算错」。

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

- **WIN32 那份本机不可执行。** `CMakeLists.txt:381-395` 把 `src/runtime/runtime.c` 放在 `if(WIN32)`
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

**修法。** 把仓库头改名：`src/platform/process.h` → `src/platform/im_process.h`（用 `git mv`，`git log --follow` 仍追得到），并更新 5 个引用点 —— `src/platform/process.c:1`、`src/platform/process_probe.c:1`（`"process.h"`）、`src/child_proc.h:6`（`"platform/process.h"`）、`src/runtime/runtime_posix.c:588`、`src/mod/server_mod_posix.c:2`（`"../platform/process.h"`），外加三处文档反引号引用（`docs/API.md:293`、`docs/STATUS.md:361`、`docs/archive/ROADMAP.md:62`）。改名之后那 5 个 `#include <process.h>` 自然解析到 CRT 头。**没有选 `#include_next <process.h>`**：它一行就能解决，但那是 GCC 专有扩展；改名是纯标准 C，而且把「仓库头不该与系统头同名」这条规则真正修好，`dir.h`/`parser.h` 的同类隐患也照此办理。

**第五类（`getline`）与上面无关，是另一件事。** `#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)` 那段守卫**不是**原因：mingw-w64 的 `<stdio.h>` 里 `getline` **一次都没出现**（`grep -c getline` 在 MINGW64 与 UCRT64 两个 sysroot 上都是 **0**），改宏、把宏提前、不定义宏，三个最小复现都报同一个 implicit declaration。因此加本地 shim `src/common/probe_compat.h`：`#if defined(_WIN32)` 下 `probe_getline`（`fgets` + `realloc` 增长循环，遇 `\n` 返回读到的字节数，EOF 无数据返回 -1），随后 `#define getline probe_getline`，语义与 POSIX 一致。**没有选「把这两个探针从 Windows 构建里排除」** —— 那会悄悄删掉 Windows 的 upp/crp 覆盖，而 CTest 正是靠它们。

**第六类（`VV_STAT`）。** `src/common/vverse_pack.c` 的 `_WIN32` 分支把 `VV_STAT` 定义成 `_stat`，而 `_stat` 是宏、展开为 `_stat64i32`（`_mingw_stat64.h:22`），它填的是 `struct _stat64i32`；代码里的变量却是 `struct stat`（同文件 `sys/stat.h:137` 的另一个布局）。加 `VV_STAT_T`（Windows `struct _stat64i32` / POSIX `struct stat`），`path_is_dir`、`path_is_file` 两处改用它。

**验证。** 本机可以直接调用 Windows 侧工具链：`/mnt/c/msys64/mingw64/bin/gcc.exe`（gcc 16.1.0，Rev5）。对**全部** `src/**/*.c` 做 `-fsyntax-only` 扫描、只筛 `implicit declaration`：修复前命中 5 处，修复后只剩 `src/platform/http_probe.c:93` 的 `setenv` —— 而该文件在 `CMakeLists.txt:202` 的 `if(NOT WIN32)` 里，不参与 Windows 构建，因此不是 Windows 缺陷。逐个确认 PASS：`src/verse/upp_probe.c`、`src/verse/crp_probe.c`、`src/common/vverse_pack_probe.c`、`src/common/vverse_pack.c`、`src/platform/thread.c`、`src/headless_server.c`、`src/mod/gui_mod.c`、`src/vm/vm.c`。

**诚实边界。** ① 本机 MSYS2 **没装 cmake.exe**，所以我做的是逐编译单元的 `-fsyntax-only`，不是完整 Windows 构建；完整证据（164/164 干净重建、`inimerse.exe` 链接成功）来自发布会话在**仓库外克隆**上的实测。② Linux 门禁**永远看不见**这一类缺陷：`tools/gate.sh` 跑在 Linux 上，glibc 没有 `<process.h>`，所以这道门禁此前红不了、以后也挡不住同类的 Windows-only 编译错 —— 能挡住它的只有 Windows CI，而 CI 自己红着的时候没人看。③ 编译修好之后 Windows 的 ctest 只有 **54/84**（30 项失败：9 项段错误、9 项超时、4 项 Failed、17 项 Not Run），那些是**运行时**缺陷，与本节无关，本轮**故意不修**（发布会话正在请用户决定是带已知问题发版还是修到全绿）。

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

**结论**：`--jit` 改的是一个从不被消费的全局变量，**不得把它当作加速通道报告**。这一结论与仓内既有记录一致（`docs/STATUS.md:327`、`docs/API.md:485`）。本报告因此只比较解释器与 AOT 两条真实通道，并把「让 `--jit` 诚实（拒绝或实现）」列为 §5 的一项。

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

`tools/aot_native.test.py` 用 `DIVERGENCE` 双端逐字钉死了三处解释器与 AOT 的不一致，它们是**有意的记录**而非待修项：

- `func nothing() { x = 1 }` + `say nothing()` → 解释器 `nil` / AOT `0`。
- 函数内对全局赋值 `g = g + 5` → 解释器 `5\n2\n` / AOT `7\n7\n`（解释器把函数内赋值变成局部变量）。
- `lcg_float_promotion`（`x = (x*1103515245+12345) % 2147483648` 的第二步）→ 解释器 `0` / AOT `377401575`，机制是 §1.2 的 float 提升。

**这三处与 §1.1、§1.2 同源**：都是「解释器的整数语义与 AOT 的 int64 语义不同」。修 §1.1 / §1.2 时应当**同时重新审视这三条钉死项**，因为修好之后它们可能变成等价，那时就该按用例里的提示语把它们提升为 `EQUIVALENCE`。

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
