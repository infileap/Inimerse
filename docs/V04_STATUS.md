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
| `case try` 基础 Result 分支 | 部分实现 | `case_try_runtime`；支持 `ok`/`err` 判别、载荷绑定与 `e in E` 集合守卫，结构模式与穷尽检查待完善 |
| `case` 类型/集合模式 | 部分实现 | `case_collection_patterns_runtime`；支持 `in` 命名类型、区间和集合，结构模式待完善 |
| `case` 通配模式 | 已实现 | `case_collection_patterns_runtime`；`_` 在前序分支未命中时兜底 |
| case 覆盖/不可达诊断 | 部分实现 | `--lint` 可报告 wildcard/else 后的分支及缺少兜底；有限集合穷尽性待完善 |
| 集合推导 | 已实现筛选式 | `collection_comprehension_runtime` |
| TypeSet 集合类型内核 | 已实现 | `typeset_probe`；独立 C API，尚未接入用户 `type` 声明 |
| 命名 TypeSet 注册表 | 已实现 | `type_registry_probe`；基础 parser/compiler 接入已完成 |
| `type Name = 集合表达式` 基础语法 | 已实现 | `type_collection_runtime`；当前编译为命名集合全局值，复杂类型元数据仍待完善 |
| 预设业务/VM 错误集合目录 | 部分实现 | `error_types_probe`；核心 VM 除零/越界/约束失败已迁移，其他模块错误待完成 |
| `|>` 管道 | 已实现 | `pipeline_runtime` |
| 非捕获 lambda 与函数值调用 | 已实现 | `lambda_runtime`；闭包捕获仍未实现 |
| 浮点字符串精度保护 | 已实现 17 位往返输出（`str` 与 VM 通用格式化） | `float_precision_runtime` |
| JIT 开关 | 已实现，后端回退解释器 | `jit_mode_probe` |
| HTTP 探针稳定性 | 已加入启动/端口重试 | 连续 5 次 `http_probe` 通过 |

## V0.4 仍需开发

- 闭包捕获和完整函数值生命周期管理；
- `>>` 函数组合（依赖函数值）；
- `?` 的函数级 Err 自动返回与栈展开；
- `case try` Result 分支语法与 `finally`；
- Eidos class/instance/method 对象模型；
- 真正模板/类型特化 JIT；
- 远程 registry、签名验证、Winget/Linux 官方提交。

## 下一轮实施顺序

1. 先引入可调用的函数值/闭包对象（必要的 GC 所有权和 `OP_CALL_VALUE`），再实现 lambda；
2. 在函数值之上实现 `>>`，并用组合等价性回归锁定求值顺序；
3. 将 `?` 从当前 `unwrap` 降低扩展为函数帧级 Err 返回，补充嵌套调用和 `finally` 场景；
4. 再推进 Eidos 对象布局与 JIT 特化，避免在运行时表示尚未稳定时重复返工；
5. 最后冻结数值 ABI、包签名和发行清单，执行发布门禁。

## 发布门禁

功能冻结后必须重新执行：

1. 全量 Bug 排查：构建、CTest、语言/协议/包回归和跨平台 CI，失败项必须有修复记录；
2. 集合变换性能审计：固定规模和构建配置，比较 JIT 模式耗时、峰值内存、结果哈希并设回归阈值。

当前最近一次全量预审计为 21/21 通过（包含 `release_verify_regression`）；`http_probe` 已修复启动竞态并连续重跑通过，仍需在最终冻结后再跑完整门禁。
