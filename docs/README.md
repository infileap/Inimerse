# docs/ — Inimerse / Infiverse 文档索引

本目录只保留**当前有效**的指导文档。历史版本说明、阶段性路线图与已被取代的参考文档全部移入 [archive/](archive/README.md)。

## 当前文档

| 文档 | 作用 | 什么时候读它 |
| --- | --- | --- |
| [STATUS.md](STATUS.md) | **状态、路线图与版本裁定的唯一权威** | 想知道"现在到底做到哪了""v0.6 那些声明算不算数" |
| [BOARD.md](BOARD.md) | **多会话协调板**：任务认领、冲突域、门禁、交接格式 | 准备动手改代码之前——先确认没人已经认领了同一件事 |
| [API.md](API.md) | **语言、API 与平台事实的唯一权威** | 想写脚本、查内建函数、查语法糖、查平台与编译目标支持 |
| [REQUIREMENTS_ANALYSIS.md](REQUIREMENTS_ANALYSIS.md) | 需求分析与治理裁决：33 项需求↔现状差距矩阵、9 处口径冲突、P0–P3 优先级 | 想知道"文档承诺 vs 仓库现状"的完整对照 |
| [WASM.md](WASM.md) | **Wasm 后端的唯一权威**：线性内存堆与所有权、数组语义、PAL/ABI、错误码、v128 实测表、wasm-GC 降级 | 改 `src/compilation/wasm_backend.{c,h}`、写 `.wasm` 宿主、或想知道「SIMD/GC 到底做到哪了」 |
| [AUDIT.md](AUDIT.md) | **语言缺陷审计与执行通道效率的唯一权威**：三条已复现缺陷（`%` 的 32 位截断、整数静默退化成 double、`vm.c:3637` 的死守卫）、四条通道 × 五个工作负载的实测表、12 条优化方案 | 想知道「引擎哪里会静默算错」「解释器 vs AOT vs C++/Rust 各快多少」、或准备做性能优化之前 |
| [DECFY_DESIGN.md](DECFY_DESIGN.md) | **底层去C化设计的唯一权威**：同一条语义的多个独立决定点（`and`/`or` 六个、`%` 三个，**横切 C 与 `.im` 两种语言**）、五类暂时搬不动的 C 及逐条理由、IR 收敛接口草图、引导与双构建等价判据、迁移顺序与可证伪判据 | 想知道「哪些引擎层能搬进 `.im`、哪些搬不动、按什么顺序搬、怎么证明搬对了」，或要动 `src/compiler/compiler.c` / `src/compilation/aot_native.c` / `src/compilation/wasm_backend.c` 的语义之前 |

> 研究性愿景与设计草案见 [../future/README.md](../future/README.md)。
> STATUS.md 记录「**已经**做到哪了」（事实与证据），BOARD.md 记录「**正在**做什么、谁在做」（在途与认领）；两者不重叠。

## 三条硬规则

1. **`docs/archive/` 里的内容不代表当前状态。** 归档文件中含已废止声明，清单见 [archive/README.md](archive/README.md) 第一节。
2. **设计 ≠ 实现。** 任何「已完成」「支持全部平台」「完全兼容」式的表述都必须附明确维度与证据（口径见 [STATUS.md](STATUS.md) §1）。
3. **性能只以 [archive/SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) 为准**，由 `python3 tools/selfhost_bench.py --runs 5 --write-docs` 生成；相对上一份报告任一套例运行中位数劣化超过 20% 时不得宣称发布。

## 快速上手

```bash
# 构建与全量测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4     # 期望：100% tests passed, 0 failed out of 97

# 运行与编译脚本
build/inimerse run script.im
build/inimerse compile script.im out.inim

# P1 最小 Layer 闭环：权威端与客户端是两个独立进程，只通过 stdin/stdout 通信
build/inim-server root main            # 从 stdin 读 canonical-JSON 请求行，向 stdout 写响应行
build/inim-client root main scenario  # fork 一个 inim-server 子进程并按剧本驱动它
```

JS 侧协议测试不在 CTest 内，由门禁的 `node` 阶段统一跑（11 个套件）：

```bash
node tools/node_suites/run_all.js    # 期望：node protocol suites: 11/11 passed
```

## 多会话工作

多路会话并行开发时**不要共用工作区**。每个会话开自己的工作树，完成后跑门禁：

```bash
tools/stream.sh new <slug>     # 建 .worktrees/<slug> 与分支 stream/<slug>
tools/gate.sh                  # 九个阶段：build + ctest + economy + node + plugin + oauth-loop + ignored-credentials + links + doc-paths
```

每个会话开工前先读自己那条流的**作业单**：`docs/streams/<slug>.md`（已结项的作业单在文件头
标了状态，判断做没做以 [STATUS.md](STATUS.md) §10 与 [BOARD.md](BOARD.md) §5 为准）。
门禁**串行跑**，不要两个一起跑——会撞端口，制造 §2.9 记的那种假失败。

规则、任务板与交接格式见 [BOARD.md](BOARD.md)。

## 归档

- [archive/README.md](archive/README.md) —— `docs/` 归档说明与**已废止声明清单**（读任何归档文件前先看这里）
- [../future/archive/README.md](../future/archive/README.md) —— `future/` 归档说明
