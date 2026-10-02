# 流简报：`docs-audit`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/docs-audit`，冲突域 `docs/`、`README.md`、`future/`。

## 1. 要交付什么

**文档里的每个数字和每个路径都能兑现。** 不是重写文档，是抓不诚实的表述和失效的引用。

## 2. 口径（先读这个，别自己发明词汇）

[STATUS.md](../STATUS.md) §1 定义了唯一权威口径：

| 标记 | 含义 | 最低证据 |
| --- | --- | --- |
| **已验证** | 代码已合入，且有可重复的自动化验收 | CTest 用例名或 `tools/*.test.*` |
| **部分实现** | 有可运行代码，但覆盖面/错误处理/跨平台未冻结，**不承诺兼容性** | 源文件路径 |
| **设计未实现** | 只有设计文档，无可交付实现 | 设计文档章节 |
| **已废止** | 曾被文档声称存在，经核实为不成立 | §3.2 裁定表 |

证据等级 E0–E6；**只有 E4 及以上才能叫「已验证」**。硬规则：不得出现「支持所有平台」「完全兼容」「已完成」这类无维度、无证据的表述；**设计文档中的方案不等于已具备功能**。

## 3. 已经抓到的三个真问题（从这里开工）

### 3.1 `REQUIREMENTS_ANALYSIS.md` 里有 22 条失效路径

实测：

```
$ grep -oE 'docs/[A-Za-z0-9_.-]+\.md' docs/REQUIREMENTS_ANALYSIS.md | sort -u
```

23 个路径里 **22 个已经不存在**——它们在文档收敛时被移进了 `docs/archive/`（40 个条目）。映射是 1:1 的：

| 失效引用 | 实际位置 |
| --- | --- |
| `docs/ROADMAP.md` | `docs/archive/ROADMAP.md` |
| `docs/ROADMAP_0.5-0.6.md` | `docs/archive/ROADMAP_0.5-0.6.md` |
| `docs/API_CATALOG.md` | `docs/archive/API_CATALOG.md` |
| `docs/RELEASE_0.5.0.md` | `docs/archive/RELEASE_0.5.0.md` |
| `docs/SELFHOST_BENCHMARK.md` | `docs/archive/SELFHOST_BENCHMARK.md` |
| …（共 22 条，逐条核对） | |

例外：`docs/AI_LAYOUT.md` 不在 `archive/`，它被移到了**仓库根** `AI_LAYOUT.md`。

### 3.2 `tools/check_links.py` **看不见**这一类失效

这是关键教训：检查器报 `0 broken`，但这些路径全断了。原因是它必须剥离**行内代码**（否则 `` `object["name"](...)` `` 会产生 15 个假阳性），而这些路径恰好写在反引号里。

⇒ **`check_links.py` 全绿不等于路径都对。** 你需要一个补充检查：对每个 `.md`，抽出反引号里的 `docs/…`/`future/…` 路径，逐个 `test -e`。

### 3.3 两套词汇并存

`docs/REQUIREMENTS_ANALYSIS.md` 用的是另一套词：**已完成 / 符合 / 部分 / 缺失 / 虚假**（见 §112、§159 的判定口径表），跟 STATUS §1 的四标记不是一回事。至少要在文件顶部加一段说明「本文用分析期词汇，交付口径以 STATUS §1 为准」，或者统一。

## 4. 还要查什么

1. **基线数字**：`README.md:25` 写 `0 failed out of 85`。核对 `ctest --test-dir build -j4` 的真实输出，以及 JS 套件的 `11/11`（`node tools/node_suites/run_all.js`）。每个数字都要有出处
2. **`future/` 的收敛是否彻底**：`future/` 现在只剩 `README.md`、`infiverse-inim-os-summary.md`（21503 行）、`优化路线pro.md`、`愿景.md`、`archive/`（8 条）。抽查 `archive/README.md` 的索引是否覆盖全部条目
3. **`docs/archive/README.md`**（40 条）同样抽查索引完整性
4. **废弃横幅**：被归档的文件顶部应有效力声明。抽查若干，确认不是空话
5. **四标记覆盖率**：`docs/API.md`（676 行）、`docs/STATUS.md`（639 行）里的每条声明是否都带标记

## 5. 判据

1. `python3 tools/check_links.py` 0 broken
2. **新增的反引号路径检查** 0 失效
3. `README.md` 的每个数字都能用一条命令复现
4. `docs/REQUIREMENTS_ANALYSIS.md` 的 22 条失效路径全部改正（改引用，**不是**改归档位置）
5. `tools/gate.sh` 全绿

## 6. 已知的坑

- **不要动 `docs/archive/` 里的内容**：那是历史记录，归档的意义就是「原样保留」。只改**指向它的引用**
- **不要改 `STATUS.md` §1 的四标记定义**：那是权威口径，改它等于改法律
- **`check_links.py` 的剥代码规则不能删**：删了会冒出 15 个假阳性。要补的是**独立**的反引号路径检查
- **`future/infiverse-inim-os-summary.md` 有 21503 行**：别整个读进来。用 `grep` 定位章节（§64.1 六态、§64.7、§67.3、§77 是关键锚点）
- **不要碰别人的冲突域**：仓库根归 `repo-hygiene` 流（但 `README.md` 归你），`src/verse/` 归 `upp-in-engine` 流
- **改完必须跑门禁**：删/改文档可能打断 `docs/README.md` 的索引

## 7. 交回时给我

按 [BOARD.md](../BOARD.md) §4 的五项，第 4 项「没做什么 / 已知没解决什么」不能漏。
