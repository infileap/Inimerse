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

### 1.0 `and` / `or` 在两个后端返回不同的东西 —— 本轮最严重的一条

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

### 1.1 `%` 在浮点路径把两个操作数截成 32 位 `int` —— 静默算错，且与 AOT 分歧

**位置**：`src/vm/vm.c:3815`（`L_MOD`）。

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

**O0. 先定 `and` / `or` 的语义，再让两个后端一致（§1.0）** —— 这是本轮唯一需要**先做决定**而不是先写代码的一条。

两条路：①按解释器现状（值语义）修 AOT 的 emitter，让它也短路并返回操作数；②按 AOT 现状（布尔）修 `src/compiler/compiler.c:648-670`，改成发 `OP_AND` / `OP_OR`。**应当选 ①**：值语义已经是解释器上被实际使用的行为（`name or "anonymous"`），改它会静默改变现有脚本的结果；而改 AOT 只是让编译产物与解释器一致。

无论选哪条，`docs/API.md:90` 都必须写明返回的是操作数还是布尔 —— **现在文档对这件事沉默，正是这条分歧能活到现在的原因**。**验证**：§1.0 的四行表格两个后端逐格相同；`tools/im_diff_fuzz.py` 重跑后 `and`/`or` 相关分歧归零；顺带决定 `OP_OR`（当前是死 opcode）是删除还是真正启用。

**O1. 修 `%` 的 32 位截断（§1.1）** —— 正确性修复，不是性能项。
把 `L_MOD` 改成与 AOT 的 `nv_mod` 同语义：两个操作数按 64 位整数处理，除数为 0 时保持现有的 `division_by_zero` 抛出。**验证**：§1.1 表格里六个表达式逐个变成正确值；`2330089441 % 2147483647` 解释器与 AOT 都得到 `182605794`；现有 CTest 全绿。

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

# §1.5：差分模糊测试（随机程序，两个后端逐对比较）
python3 tools/im_diff_fuzz.py --count 150 --seed 7 --keep /tmp/fuzz-cases

# §1.1：模运算截断
printf 'say 2330089441 %% 2147483647\n' > /tmp/mod.im && ./build/inimerse /tmp/mod.im

# §1.2：整数 → double 提升与失精
printf 'say (2147483647 + 1).type\nsay 9007199254740992 + 1\n' > /tmp/prom.im && ./build/inimerse /tmp/prom.im
```

`tools/perf_channels.py` 退出码非 0 表示**有工作负载被某道闸门拦下**（例如 `arith` 的 Rust 通道被缩放闸门拦下是预期行为，不是故障）。
