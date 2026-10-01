# docs/ — Inimerse / Infiverse 文档索引

本目录只保留**当前有效**的指导文档。历史版本说明、阶段性路线图与已被取代的参考文档全部移入 [archive/](archive/README.md)。

## 当前文档

| 文档 | 作用 | 什么时候读它 |
| --- | --- | --- |
| [STATUS.md](STATUS.md) | **状态、路线图与版本裁定的唯一权威** | 想知道"现在到底做到哪了""v0.6 那些声明算不算数" |
| [API.md](API.md) | **语言、API 与平台事实的唯一权威** | 想写脚本、查内建函数、查语法糖、查平台与编译目标支持 |
| [REQUIREMENTS_ANALYSIS.md](REQUIREMENTS_ANALYSIS.md) | 需求分析与治理裁决：33 项需求↔现状差距矩阵、9 处口径冲突、P0–P3 优先级 | 想知道"文档承诺 vs 仓库现状"的完整对照 |

> 研究性愿景与设计草案见 [../future/README.md](../future/README.md)。

## 三条硬规则

1. **`docs/archive/` 里的内容不代表当前状态。** 归档文件中含已废止声明，清单见 [archive/README.md](archive/README.md) 第一节。
2. **设计 ≠ 实现。** 任何「已完成」「支持全部平台」「完全兼容」式的表述都必须附明确维度与证据（口径见 [STATUS.md](STATUS.md) §1）。
3. **性能只以 [archive/SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) 为准**，由 `python3 tools/selfhost_bench.py --runs 5 --write-docs` 生成；相对上一份报告任一套例运行中位数劣化超过 20% 时不得宣称发布。

## 快速上手

```bash
# 构建与全量测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4     # 期望：100% tests passed, 0 failed out of 85

# 运行与编译脚本
build/inimerse run script.im
build/inimerse compile script.im out.inim

# P1 最小 Layer 闭环：权威端与客户端是两个独立进程，只通过 stdin/stdout 通信
build/inim-server root main            # 从 stdin 读 canonical-JSON 请求行，向 stdout 写响应行
build/inim-client root main scenario  # fork 一个 inim-server 子进程并按剧本驱动它
```

JS 侧协议测试不在 CTest 内，需单独运行：

```bash
node tools/upp_reference.test.js
node tools/crp_reference.test.js
node tools/vverse_validate.test.js
```

## 归档

- [archive/README.md](archive/README.md) —— `docs/` 归档说明与**已废止声明清单**（读任何归档文件前先看这里）
- [../future/archive/README.md](../future/archive/README.md) —— `future/` 归档说明
