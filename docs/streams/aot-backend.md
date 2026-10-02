# 作业单：`aot-backend` —— 把「AOT ≥2x」从宣传口径变成一个测出来的数字

> **本条流第一波不碰 `CMakeLists.txt`、不碰 `src/main.c`。** 这一波 `CMakeLists.txt` 由
> `xlang-bridge` 独占。你的第一交付物是**测量 + 判断**，不是新目标；需要新目标时**停下报告**，
> 协调者会在 `xlang-bridge` 交回后把 `CMakeLists.txt` 交给你。

---

## 1. 现状（这是本仓库被反复点名的最大一处宣传与事实不符）

`docs/STATUS.md` §3.1 的「声称但未交付」表第一条：

| `docs/archive/RELEASE_0.5.0.md` 声称 | 核实结果 |
| --- | --- |
| 「AOT compilation … outperform the interpreter by **at least 2x**」（第 43 行） | 实测 **1.09x**。AOT 通道是**打包**通道（引擎副本 + 嵌入规范化字节码），**复用同一个 C 解释器**，不自生成原生代码。原文 `SELFHOST_BENCHMARK.md` 已自述「不满足 ≥2x 目标」 |

`docs/archive/SELFHOST_BENCHMARK.md:39` 自己写着：

> ≥2x 目标属于优化型 AOT 后端（原生代码生成），为 v0.5→v0.6 后续迭代。

而 `docs/archive/RELEASE_0.5.0.md:172` 又把「v0.5.0 lays the groundwork for …」链到
`ROADMAP_0.5-0.6.md`。**口径是散在至少三份文档里的，且互相不一致** —— 这本身就是一条要修的缺陷。

已知的测量工具：`tools/selfhost_bench.py`、`tools/perf_compare.py`。

---

## 2. 目标：**先测量，再动手；数字说话**

### 2.1 第一步（必须做）：把 1.09x 这个数字的来源钉死

- 跑 `tools/selfhost_bench.py`（或它调用的底层），**原样复现 1.09x**，把命令与完整输出贴出来。
- 说清楚 **1.09x 到底在比什么**：是「打包产物 vs 解释器」还是「AOT 编译时间 vs 解释执行时间」？
  分母分子各是什么？这条不写清楚，后面所有优化都没有基准。
- 说清楚当前 `--aot` **实际产出什么**（读 `src/main.c` 的 `--aot` 分支与相关源码；
  **只读不改** —— 本条流第一波不碰 `src/main.c`）。

### 2.2 第二步（二选一，都要有实测数字）

**路线 A：做一个真的数值子集 AOT**

- 取当前 wasm 后端已经覆盖的那个**数值子集**（int/float/bool、算术、比较、`if`/`while`/`repeat`、
  用户函数、全局变量 —— 见 `src/compilation/wasm_backend.h` 的文件头，它是现成的子集定义），
  生成 **C 源码**并用宿主 `cc` 编译成可执行文件，然后与解释器比。
- 目标：给出一个**测出来的**加速比。**它可能仍然不到 2x —— 那就如实写。**
- 这条路的产出包括新的 `src/compilation/aot*` 源文件，所以**需要 `CMakeLists.txt`**：
  先把设计、接口与测量方法写清楚并报告，等协调者把 `CMakeLists.txt` 交给你再落代码。

**路线 B：证据充分的降级结论**

- 如果评估后认定「≥2x 原生代码生成」在本架构下不属于 v0.5（例如：解释器本身就是 C 实现的
  寄存器 VM，且 `--aot` 的设计目标是分发而非加速），那就把结论**写死**：
  - 在 `docs/` 里明确 `RELEASE_0.5.0.md` 那条断言的正确表述；
  - 明确 2x 归到哪个版本、以什么为验收；
  - 把散在三份文档里的口径统一到**一处**。

---

## 3. 冲突域

**你的写域**

- `src/compilation/aot*`（新建，路线 A 用）
- `tools/selfhost_bench.py`、`tools/perf_compare.py`、`tools/aot*`
- `docs/archive/SELFHOST_BENCHMARK.md`

**不要碰**

- `CMakeLists.txt`（**第一波禁碰**，见抬头）
- `src/main.c`（**第一波禁碰**；读它是允许且必要的）
- `src/compilation/wasm_backend.*` —— `wasm-simd-gc` 的写域。
  `checksum.*`/`debug_info.*`/`deps.*`/`profiler.*` 两边都别动。
- `src/common/**`（硬禁碰）
- `tools/gate.sh`、`docs/STATUS.md`、`docs/BOARD.md`

---

## 4. 判据（缺一项就不收）

1. **1.09x 被原样复现**，且分子分母被明确写出来（命令 + 完整输出）。
2. 当前 `--aot` 的真实行为有源码级说明（引 `src/main.c` 的具体行号）。
3. 路线 A 或路线 B 的产出，且**加速比是测出来的**。
4. 口径统一：散落的 ≥2x 表述被收敛到一处，并给出「哪份文档现在该怎么写」的建议文本。
5. `bash tools/gate.sh`（全量）七阶段全 PASS，且 **ctest 计数未变**（第一波没加 CTest）。
6. `git diff --stat <base>..HEAD` + **没做什么 / 已知没解决什么**。

---

## 5. 诚实条款（本条流的全部意义就在这里）

- **禁止**把「打包」与「加速」混为一谈。当前 `--aot` 是**分发手段**，这一点
  `SELFHOST_BENCHMARK.md` 自己已经承认了；不要在交接说明里把它说回成优化。
- **禁止**在没有实测的情况下写任何倍数。
- 如果你做出来的东西**不到 2x**，那是一个**有效结论**，不是失败 —— 如实报。
  本仓库的判据从来是「有可重复验收命令」，不是「数字好看」。
- **不要为了让一个数字好看而换基准**。换基准必须明说是换基准，并给新旧两个数字。
- 本条流最大的风险是「写了一堆优化，但没人能复现那个数字」。所以：**先有测量命令，再有代码。**
