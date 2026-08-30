# V0.4 状态矩阵

更新时间：2026-08-30

## 已进入主线并有回归证据

| 能力 | 当前状态 | 证据 |
|---|---|---|
| 参数格式 v2 | 已实现 | `vtest/params_v2.params`、loader 回归 |
| 虚拟文件系统 | 基础能力已实现 | `vfs_probe` |
| `inim` 离线包闭环 | 已实现 | `inim_regression` |
| Result 原语 | 已实现 | `result_runtime` |
| Result `?` | 已实现 unwrap postfix 语义 | `result_question_runtime` |
| 集合推导 | 已实现筛选式 | `collection_comprehension_runtime` |
| `|>` 管道 | 已实现 | `pipeline_runtime` |
| 浮点字符串精度保护 | 已实现 17 位往返输出（`str` 与 VM 通用格式化） | `float_precision_runtime` |
| JIT 开关 | 已实现，后端回退解释器 | `jit_mode_probe` |
| HTTP 探针稳定性 | 已加入启动/端口重试 | 连续 5 次 `http_probe` 通过 |

## V0.4 仍需开发

- lambda、闭包和函数值表示；
- `>>` 函数组合（依赖函数值）；
- `?` 的函数级 Err 自动返回与栈展开；
- Eidos class/instance/method 对象模型；
- 真正模板/类型特化 JIT；
- 远程 registry、签名验证、Winget/Linux 官方提交。

## 发布门禁

功能冻结后必须重新执行：

1. 全量 Bug 排查：构建、CTest、语言/协议/包回归和跨平台 CI，失败项必须有修复记录；
2. 集合变换性能审计：固定规模和构建配置，比较 JIT 模式耗时、峰值内存、结果哈希并设回归阈值。

当前最近一次全量预审计为 20/20 通过；`http_probe` 已修复启动竞态并连续重跑通过，仍需在最终冻结后再跑完整门禁。
