# docs/archive — 历史文档归档

本目录保存**历史版本说明、阶段性路线图、已被取代的参考文档与过程记录**。

> **这些文档不代表当前状态。**
> 当前状态、路线图与版本裁定以 [../STATUS.md](../STATUS.md) 为唯一权威；
> 语言、API 与平台事实见 [../API.md](../API.md)；
> 需求分析与治理裁决见 [../REQUIREMENTS_ANALYSIS.md](../REQUIREMENTS_ANALYSIS.md)。

归档原则：**保留原文以便追溯，不改写内容**。唯一例外是对含已废止声明的文件在顶部加一行废止横幅。

---

## 一、先读：已废止的声明

以下声明在归档时经实测否证。**任何地方都不得再引用它们作为现状。**

| 文件 | 已废止的声明 | 实测结果 |
| --- | --- | --- |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | 「AOT compilation … outperform the interpreter by **at least 2x**」 | AOT 是**打包**通道，复用同一个 C 解释器，实测 **1.09x**。见 [SELFHOST_BENCHMARK.md](SELFHOST_BENCHMARK.md) |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | 「WebAssembly output … supporting **SIMD optimizations and WebAssembly GC**」 | `src/compilation/wasm_backend.h:10` 原文：`SIMD/GC/heaps are future work.` 当前仅数值子集 MVP |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | Python 扩展桥 `inimerse_extension.c` / `PyInit_inimerse()` | 仓库中不存在该文件，全仓无 `PyInit_inimerse` 实现 |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | Java 桥 `InimerseBridge.java` | 仓库中不存在该文件 |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | 发布产物 `.whl` / `.jar` / `inimerse-aot` / `libinimerse.so` | 仓库中不存在任何此类产物 |
| [ROADMAP_0.5-0.6.md](ROADMAP_0.5-0.6.md) | v0.6 §1（Inim OS）、§2（Infiverse 内核）、§3（2D 引擎）、§4（标准库）的**全部「已完成」标记** | 逐条核实为**设计未实现**或**部分实现**，共 11 行裁定见 [../STATUS.md](../STATUS.md) §3.2 |
| [ROADMAP.md](ROADMAP.md) | 作为路线图权威来源 | 内容已完整并入 [../STATUS.md](../STATUS.md) §4，并修正了断引用（原文第 5 行引用的 `docs/愿景.md` 实际位于 `future/愿景.md`） |
| [SYNTAX_SUGAR.md](SYNTAX_SUGAR.md) | 第 18 行把 `?.` / `??` 列为「当前仍是设计项」 | **已实现**：`vtest/optional_member_v04.im`、`vtest/null_coalesce_v04.im`，CTest `optional_member_runtime` / `null_coalesce_runtime` |
| [NUMERIC_MODEL_V04.md](NUMERIC_MODEL_V04.md) | 数值塔 `Number = Z ∪ Q ∪ D ∪ F` | 设计文档，**未实现** |
| [PARAM_FORMAT.md](PARAM_FORMAT.md) | `inim bundle resolve/graph/verify/gc` 与 `.param` 格式 | 提案，**无实现证据** |
| [protocol_v1.md](protocol_v1.md) | 作为已发布协议 | 设计文档（WebSocket / UDP / IPFS 部分为预留） |
| [V04_STATUS.md](V04_STATUS.md) | 「66/66 通过」（2026-09-13） | 当前基线为 **79/79**（2026-10-01），见 [../STATUS.md](../STATUS.md) §2 |

---

## 二、版本说明与变更日志

历史版本的交付说明。**当前版本的裁定不在这里**，见 [../STATUS.md](../STATUS.md) §3。

- [RELEASE_0.2.0.md](RELEASE_0.2.0.md)
- [RELEASE_0.2.1.md](RELEASE_0.2.1.md)
- [RELEASE_0.4.0.md](RELEASE_0.4.0.md)
- [RELEASE_0.4.1.md](RELEASE_0.4.1.md)
- [RELEASE_0.5.0.md](RELEASE_0.5.0.md) —— **含已废止声明，见 §一**
- [CHANGELOG_0.5.0.md](CHANGELOG_0.5.0.md) —— 其中 `[Unreleased]` 段落是当前线的一部分依据

## 三、发布与 CI 流程

- [RELEASE_VERIFY.md](RELEASE_VERIFY.md) —— `tools/release_verify.py` 的用法（该工具仍在用）
- [GITHUB_RELEASE_PLAYBOOK.md](GITHUB_RELEASE_PLAYBOOK.md) —— `infileap/inimerse` 发布手册
- [BUILD_RELEASE_LESSONS_0.4.0.md](BUILD_RELEASE_LESSONS_0.4.0.md) —— 0.4.0 构建/发布的工程约束
- [RELEASE_CI_LESSONS_0.4.0.md](RELEASE_CI_LESSONS_0.4.0.md) —— 0.4.0 CI 故障根因
- [RELEASE_CI_LESSONS_0.4.1.md](RELEASE_CI_LESSONS_0.4.1.md) —— 0.4.1 CI / Windows 故障根因

## 四、阶段性路线图

- [ROADMAP.md](ROADMAP.md) —— v0.2 期主线路线图（内容已并入 [../STATUS.md](../STATUS.md) §4）
- [ROADMAP_0.3_AUDIT.md](ROADMAP_0.3_AUDIT.md) —— v0.3 路线图审计
- [ROADMAP_0.3_STATUS.md](ROADMAP_0.3_STATUS.md) —— v0.3 交付状态
- [ROADMAP_0.4-0.6.md](ROADMAP_0.4-0.6.md) —— v0.4–v0.6 路线图
- [ROADMAP_0.5-0.6.md](ROADMAP_0.5-0.6.md) —— **含已废止声明，见 §一**
- [ROADMAP_CASE_TYPES_V04.md](ROADMAP_CASE_TYPES_V04.md) —— V0.4 集合类型与模式匹配路线图
- [ROADMAP_3.1.md](ROADMAP_3.1.md) —— v3.1 长期方向（集合化类型系统）
- [ROADMAP_FRONTIER.md](ROADMAP_FRONTIER.md) —— 前沿方向（概率编程、证明携带代码等）

## 五、阶段状态与设计草案

- [V0_3_IMPLEMENTATION.md](V0_3_IMPLEMENTATION.md) —— v0.3 已实现接口清单
- [V04_CLOSURE_DESIGN.md](V04_CLOSURE_DESIGN.md) —— V0.4 闭包环境设计
- [V04_STATUS.md](V04_STATUS.md) —— V0.4 状态矩阵（**基线已过期，见 §一**）

## 六、语言 / API / 平台参考（已被 [../API.md](../API.md) 取代）

这些文件的**准确内容已合并进 [../API.md](../API.md)**，并逐条标注了实现状态。保留原文以备追溯。

- [API_REFERENCE.md](API_REFERENCE.md) —— 旧「权威总览」（多处文档曾指向它）
- [API_CATALOG.md](API_CATALOG.md) —— 语法糖与内建目录
- [API_BUILTIN_TABLE.md](API_BUILTIN_TABLE.md) —— 内建函数表
- [SYNTAX_SUGAR.md](SYNTAX_SUGAR.md) —— 语法糖说明
- [syntax_sugar.json](syntax_sugar.json) —— 语法糖机器可读清单
- [PORTABILITY.md](PORTABILITY.md) —— 平台可移植性
- [WASM.md](WASM.md) —— Wasm 后端说明
- [WASM_ABI.md](WASM_ABI.md) —— Wasm ABI
- [NUMERIC_MODEL_V04.md](NUMERIC_MODEL_V04.md) —— 数值模型设计
- [PARAM_FORMAT.md](PARAM_FORMAT.md) —— 参数/包格式提案
- [inimerse_compile_guide.md](inimerse_compile_guide.md) —— 编译命令指南
- [protocol_v1.md](protocol_v1.md) —— 协议设计草案

## 七、性能与审计

- [SELFHOST_BENCHMARK.md](SELFHOST_BENCHMARK.md) —— **唯一的性能事实来源**，由 `python3 tools/selfhost_bench.py --runs 5 --write-docs` 生成。§3.1 的性能裁定即引用此文件
- [COLLECTION_PERF_AUDIT.md](COLLECTION_PERF_AUDIT.md) —— 集合变换性能审计（`tools/collection_perf_audit.py`）

## 八、工具与界面文档

- [OAUTH.md](OAUTH.md) —— GitHub / Bilibili 账号关联 builtin 说明（仍有效，见 [../API.md](../API.md)）
- [AI_LAYOUT.md](AI_LAYOUT.md) —— AI 排版助手使用说明（设计文档）
- [工作台使用教程.md](工作台使用教程.md) —— 工作台使用教程（针对 `workbench.im v3`）

---

## 九、相关归档

`future/` 下的研究性文档也做了收敛，见 [../../future/archive/](../../future/archive/)。
