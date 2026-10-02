# 作业单：`wasm-simd-gc` —— 把 `SIMD/GC/heaps are future work` 变成事实或明确的降级

> ✅ **本条流已交付并合入 `main`（`3fadc8b`，merge `860cc62`）。**
> 交付结论见 [STATUS.md](../STATUS.md) §10.16 与 [WASM.md](../WASM.md)。**不要按本作业单重新开工。**
> 摘要：heaps **已做**（确定性回收 + 显式 `heap_exhausted`）、v128 SIMD **已实现已测量但生成器不选路**、wasm-GC **明确未实现**；
> 顺带修掉 `nil` 被静默丢弃与数组末尾多写 16 字节两个缺陷。**未做的**：字符串（会改宿主 PAL 契约）。

> **本条流不碰 `CMakeLists.txt`。** 这一波 `CMakeLists.txt` 由 `xlang-bridge` 独占。
> 你只能**扩展现有的** CTest（`wasm_backend_regression` / `wasm_host` / `wasm_probe`）与
> 现有测试脚本，**不能新增 CTest、不能新增编译目标**。如果你确实需要新目标，**停下报告**。

---

## 1. 现状

`docs/STATUS.md` §3.1 的「声称但未交付」表第三条：

| `docs/archive/RELEASE_0.5.0.md` 声称 | 核实结果 |
| --- | --- |
| 「WebAssembly output … supporting SIMD optimizations and WebAssembly GC」（第 38 行） | `src/compilation/wasm_backend.h:10` 原文：`SIMD/GC/heaps are future work.` 当前为数值子集 MVP |

`src/compilation/wasm_backend.h` 的文件头自述了当前子集的**准确边界**（开工前逐字读一遍）：

> Translates a numeric subset of the AST (int/float/bool, arithmetic, comparisons,
> if/while/repeat, user functions, globals) into a standalone WebAssembly MVP binary
> with **boxed values in linear memory**. Output IO goes through the fixed import table
> (`env.im_print_int(i64)` / `env.im_print_float(f64)` / `env.im_print_bool(i32)` /
> `env.im_print_nil()` / `env.im_error(i32)`). The host runner is `tools/wasm_run.js`;
> equivalence against the interpreter is asserted by `tools/wasm_backend.test.py`.
> **SIMD/GC/heaps are future work.**

关键事实：
- 三个 CTest 已存在：`wasm_backend_regression` / `wasm_host` / `wasm_probe`。
- `tools/wasm_backend.test.py` 有 13 例，机制是**等价性断言**：同一段 `.im` 分别跑解释器与
  wasm host，逐例比对输出。**这是本条流的判据底座，不要换成「能编译出来就算过」。**
- 当前是 **MVP**（`wasm32`，无 SIMD、无 GC、无堆；值全部 boxed 在线性内存里）。

---

## 2. 目标：二选一，但**必须给出证据**

「`SIMD/GC/heaps are future work`」这句话要么变成「已完成」，要么变成一条**有证据的降级结论**。
两者都比留着这句含糊的话好。**不要只写代码不改这句话**。

### 路线 A（优先）：真做出来

1. **线性内存堆 + 分配器**，使数值子集之外的**字符串 / 数组**能在 wasm 里存在与回收
   （「heaps」那一项）。至少要有：分配、越界拒绝、以及一个确定性的回收或耗尽行为
   （**耗尽必须显式 trap 或返回错误，不能静默继续** —— 这是本仓库反复出现的失败形状）。
2. **SIMD（`v128`）** 用于数值循环（「SIMD optimizations」那一项）。
   **必须有实测数字**：同一段循环，标量版 vs SIMD 版在 wasm host 里的耗时。
   「理论上更快」不是证据。
3. **wasm-GC proposal 是另一件事**：它需要 `--enable-gc` 与不同的类型系统
   （`struct`/`array` 引用类型），与上面的「线性内存堆」**不是同一个东西**。
   如果你做的是线性内存堆，**必须在文档与代码注释里说清你做的不是 wasm-GC** ——
   把两者混为一谈正是 `RELEASE_0.5.0.md` 当初犯的错。

### 路线 B：证据充分的降级

如果评估后认定某项在本架构下不可行或不属 v0.5，就**明确写出来**：
- 改 `src/compilation/wasm_backend.h` 的 `future work` 那句，改成具体状态
  （哪一项做了什么、哪一项为什么不做、归到哪个版本）；
- 给出评估依据（例如：wasm-GC 需要目标工具链支持 X，而 `tools/wasm_run.js` 的宿主是 Y）。

---

## 3. 冲突域

**你的写域**

- `src/compilation/wasm_backend.c`、`src/compilation/wasm_backend.h`
- `tools/wasm_backend.test.py`、`tools/wasm_run.js`
- `docs/WASM.md`、`docs/archive/WASM*.md`（如存在）

**不要碰**

- `CMakeLists.txt`（`xlang-bridge` 独占 —— 见本单抬头）
- `src/compilation/` 下**除 `wasm_backend.*` 以外**的文件 —— `aot-backend` 的写域
  （它要新建 `aot*` 文件；`checksum.*`/`debug_info.*`/`deps.*`/`profiler.*` 两边都别动）
- `src/common/**`（硬禁碰）
- `tools/gate.sh`、`docs/STATUS.md`、`docs/BOARD.md`

---

## 4. 判据（缺一项就不收）

1. `tools/wasm_backend.test.py` 的**等价性断言**在新增特性上仍全过
   （解释器 vs wasm host 逐例一致）—— 贴命令与输出。
2. 新特性有**新例子**进 `tools/wasm_backend.test.py`，且这些例子**在 main 上失败**
   （因为 main 没有该特性）—— 贴两边的命令与输出。
3. SIMD 有实测数字（路线 A）或明确降级（路线 B）。
4. `src/compilation/wasm_backend.h:10` 的那句 `future work` **已被改写**，
   与你的实际结论一致。
5. `bash tools/gate.sh`（全量）七阶段全 PASS，且 **ctest 计数未变**（你没加 CTest）。
6. `git diff --stat <base>..HEAD` + **没做什么 / 已知没解决什么**。

---

## 5. 诚实条款

- **禁止**把「编译通过了」当作「等价性成立」。本仓库的 wasm 判据从一开始就是**双跑比对**，
  这条不能松。
- **禁止**在没有实测的情况下写任何加速比。
- 堆耗尽、越界、未支持构造 —— 一律**显式报错**，不许静默。这条在 `wasm_backend.h` 的文件头里
  已经写了（`rejection is explicit, never silent`），请守住它。
- 如果你发现「线性内存堆」与「wasm-GC」在当前宿主下**只能做前者**，那就只做前者并写清楚
  —— 这比声称两项都完成要正确。
