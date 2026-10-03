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

**计数**：`grep -c 'add_test(' CMakeLists.txt` **108 → 110**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **110**。

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

**修法**：套件开头加两个探针（`io_ok` 试 `str2int`；`gui_ok` 试 `gui_vram_used()`，无副作用），第 9/15/16 段按探针整段跳过；末尾打印探针取值，并加一道**下限断言** `if pass < 60 { throw … }`（全套 70 个 `check`，POSIX 跳过 7 个，实测 63）—— 跳过是可闻的，整段没跑不会静默变绿。

**判据与双向验证**：CTest 名 `contract_suite_runtime`，在 `CMakeLists.txt` 里注册，`PASS_REGULAR_EXPRESSION "contract: [0-9]+ passed"`，`TIMEOUT 30`，标签 `vm;language;regression;contract`；**故意不设 `FAIL_REGULAR_EXPRESSION`** —— 抛出文本 `CONTRACT FAIL` 是套件里的字面量，会命中 §1.11 记的那条池转储坑，绿跑也会被误判；异常让进程 exit 1，靠退出码就够。`tools/gate.sh` 的 `EXP_CTEST` 111 → 112。**双向验证**：注册后 `ctest -R contract_suite_runtime` 绿；`git checkout HEAD -- contract_test.im`（未加探针的旧版）+ 重建 ⇒ 红（`***Failed  Required regular expression not found. Regex=[contract: [0-9]+ passed`，旧版在第一处 `str2int` 就抛、退出码 1）；`cp` 回 + `cmp` 逐字节相同 ⇒ 复绿。这条注册**真的在跑引擎**，不是「注册了一个永远绿的壳」。

**诚实边界**：本平台 io/gui 双缺，所以 63/70；Windows 上两段会真的跑（70/70），下限 60 对两个平台都成立。但 POSIX 门禁**确实没有验证 io/gui 那 7 条契约** —— 这是平台能力边界（模块不在 POSIX 构建里），不是套件偷懒；`src/platform/posix_stubs.c` 是空实现这一现状记录在 `docs/STATUS.md` §10.47。

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
