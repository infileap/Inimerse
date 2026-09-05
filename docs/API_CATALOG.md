# Inimerse API 大全（V0.4）

更新时间：2026-08-30。本文是公开 API 的状态索引；具体签名以源码和回归测试为准。

状态含义：

- **已实现**：当前主线可调用，并有源码注册或回归测试证据。
- **部分实现**：入口存在，但语义、平台后端或边界仍有限制。
- **设计中**：仅有文档/语法草案，不应在生产脚本中使用。
- **弃用**：仍可能被识别，但应迁移到列出的替代 API。

## 命令行接口

| API | 状态 | 说明 |
|---|---|---|
| `inimerse <script.im> [args...]` | 已实现 | 编译并运行脚本 |
| `inimerse <script.inim>` | 已实现 | 加载预编译字节码（当前不携带参数表） |
| `--version`, `where`, `changelog` | 已实现 | 引擎信息 |
| `--lint`, `--desugar` | 已实现 | 静态检查与外糖转换 |
| `--jit=off\|template\|optimized` | 部分实现 | 后两者目前安全回退解释器 |
| `--safe`, `--headless`, `--no-gui`, `--no-mods` | 已实现 | 运行策略开关 |

## 语言核心与内/外糖

| 特性 | 状态 | 分类/备注 |
|---|---|---|
| 变量、数组、字典、有限集合、算术/比较/逻辑、`if`/`while`/`for`/`case` | 已实现 | 核心语法 |
| 区间、`++`/`--`、f-string、尾逗号 | 已实现 | 内糖 |
| 集合筛选推导 `{x in S \| p}` | 已实现 | 内糖；一般映射式仍在扩展 |
| `|>` 管道 | 已实现 | 内糖 |
| Result：`ok`、`err`、`is_ok`、`unwrap`、`unwrap_or` | 已实现 | 核心运行时 |
| `TypeSet` 集合类型内核 | 已实现 | `src/types/typeset.*`；支持枚举、整数区间、并/交/差/补集、成员/子集/相交查询 |
| TypeSet 基数查询 | 已实现 | `im_typeset_cardinality`；有限枚举/闭区间及可精确物化的并/交/差集返回成员数，无限或未知返回 `SIZE_MAX` |
| 有限集合枚举描述符 | 已实现 | `src/types/enum.*`；自动选择 8/16 位宽度，名称↔编码双向查询、未知成员拒绝、重复成员拒绝；可作为错误/线程/游戏状态等通用有限集合表示 |
| 预设错误枚举描述符 | 已实现 | `im_error_domain_enum`；将 File/Parse/VM 错误域以统一 `ImEnum` 提供名称↔编码映射 |
| TypeSet 有限集物化 | 已实现 | `im_typeset_materialize_enum` / `im_enum_from_finite_set`；支持混合标量有限集及字符串/整数/浮点/布尔/nil，非字符串使用类型前缀避免冲突 |
| 枚举 case 穷尽检查 | 已实现 | `im_enum_is_exhaustive` / `im_enum_missing`；可返回缺失成员名并支持重复分支 |
| 枚举描述符指纹 | 已实现 | `im_enum_fingerprint`；为类型名与有序成员生成稳定 FNV-1a 64 位指纹，用于序列化和 ABI 校验 |
| 枚举版本兼容性 | 已实现 | `im_enum_compatible_append`；仅允许末尾追加成员，确保旧编码稳定 |
| 枚举规范标识 | 已实现 | `im_enum_qualified_member` / `im_enum_parse_qualified`；在 `Type.Member` 和数字编码之间双向转换 |
| 闭包环境基础 | 部分实现 | `src/vm/closure.*`；环境与函数对象支持引用计数，VM 已接入捕获指令；统一 Value/GC 生命周期仍待完成 |
| 闭包环境槽复制 | 部分实现 | `im_closure_env_copy_slot`；安全复制单槽并对字符串执行深复制 |
| 闭包环境并发约束 | 部分实现 | retain/release 为原子操作；槽读写需由 VM 调度器或外部锁保护 |
| 闭包环境引用计数查询 | 部分实现 | `im_closure_env_refs`；原子读取环境当前持有者数 |
| 命名类型枚举查询 | 已实现 | `im_type_registry_enum`；从注册的有限 TypeSet 生成枚举描述符 |
| 命名 TypeSet 注册表 | 已实现 | `src/types/registry.*`；支持定义、覆盖、查询和生命周期管理；基础 `type` 语法已接入 |
| `type Name = 集合表达式` | 已实现（基础） | 编译为命名集合全局值；支持 `x be Name` 复用现有 `OP_BE` 校验，复杂谓词类型仍待完善 |
| 预设错误类型目录 | 部分实现 | `src/types/error_types.*`；File/Parse/ArithmeticVM/MemoryVM/TypeVM/RuntimeVM 集合已注册，核心 VM 除零/越界/约束失败已使用 canonical kind |
| `expr?` | 已实现基础传播 | 内糖；顶层使用 `unwrap`，函数体内 Err 直接返回，`result_propagation_runtime` 覆盖 Ok/Err |
| `try { ... } catch (...) { ... } finally { ... }` | 已实现基础语义 | finally 在正常及已捕获异常路径执行；`try_finally_runtime` 覆盖 |
| `case try expr { ok(v): ...; err(e) | e in E: ... }` | 部分实现 | 原生 Result 分支、载荷绑定、`ok/err` 字面量载荷匹配和错误集合成员守卫已接入；结构模式与穷尽检查待完善 |
| `case value { n | predicate: ... }` | 部分实现 | 基础标识符守卫已接入；结构模式与复合集合守卫待完善 |
| `case value { in TypeOrSet: ... }` | 部分实现 | 基础类型/区间/集合成员模式已接入；结构类型模式与穷尽检查待完善 |
| `case value { {"field": pattern}: ... }` | 部分实现 | 字典字段字面量匹配、一层绑定、严格字段存在性和一层嵌套已接入；更深层模式待完善 |
| `dict_has(dict, key)` | 已实现 | 结构模式使用的字段存在性查询 |
| `case value { _: ... }` | 已实现 | 原生通配模式，作为前序分支均未命中时的兜底 |
| `--lint` case 覆盖诊断 | 部分实现 | 检测 `_`/`else` 后不可达分支，并提示缺少兜底分支；完整有限集合穷尽性分析待完善 |
| `x -> expr`、`(a,b) -> expr` | 已实现 | 内糖；当前支持非捕获 lambda 与函数值调用 |
| `>>`（简单函数名形式） | 已实现 | 核心 lexer/parser 将 `f >> g` 生成可调用组合闭包，`composition_runtime` 回归通过 |
| 闭包捕获、部分应用 | 部分实现 | 外层参数捕获与调用已实现；完整 GC 生命周期及通用部分应用仍待完善 |
| `fn`、`print`、`&&`/`||`、`//`、`unless`、`eidos`/`ed` | 部分实现 | 外糖，由脱糖器转换；不是 VM 原生语义 |
| `?.`、`??`、链式比较、后缀 `if/unless` | 设计中 | 尚无核心 parser/compiler 支持 |

## 内置函数模组

下表给出模组级状态；函数名称的完整清单见 [API_BUILTIN_TABLE.md](API_BUILTIN_TABLE.md)。

| 模组 | 状态 | 范围 |
|---|---|---|
| `runtime` | 已实现 | 数值、字符串、集合、GC、原子操作、实体与 SPI |
| `vm` | 已实现 | 调试器 API（`dbg_*`） |
| `io_mod` | 部分实现 | 文件、目录、进程、HTTP、串口、AI；平台能力不足时返回错误 |
| `gui_mod` | 部分实现 | 窗口、控件、绘图；无 GUI/安全模式下不可用 |
| `infiverse_mod` | 部分实现 | Verse 区块、门户、生物群系、实体与快照 |
| `verse_dist_mod` | 部分实现 | Verse 打包、Hub、身份签名与分发 |
| `net_mod` | 部分实现 | TCP/UDP；POSIX/Windows 后端能力不同 |
| `server_mod` | 部分实现 | 房间与端口管理（Windows 主路径） |
| `record_mod` | 已实现 | record 持久化、标签、快照 |
| `social_mod` | 部分实现 | 本地好友与聊天存根 |
| `identity_mod` | 已实现 | `me`、profile 与 OAuth 关联 |
| `json_mod` | 已实现 | `json_parse`、`json_serialize` |
| `isolate_mod` | 已实现 | 隔离脚本执行与超时结果 |
| `lint_mod` | 已实现 | `lint_check` |

## 平台 C API

`src/platform/` 中的 PAL 接口（时钟、休眠、路径、目录、环境变量、线程/Fiber、进程、套接字和互斥量）是宿主扩展使用的稳定边界。POSIX 对部分图形/系统能力提供明确的“不支持”错误；扩展不得伪造成功返回。

## 包与文件系统

| API | 状态 | 说明 |
|---|---|---|
| `inim` 离线包管理器 | 已实现 | 安装、卸载、列表、校验回归闭环 |
| VFS 基础接口 | 部分实现 | `src/platform/vfs.*`；尚未承诺完整挂载/权限模型 |
| 远程 registry、签名验证、官方 Winget/Linux 提交 | 设计中 | 不属于当前发行版保证范围 |

## 明确弃用项

| 旧 API/语法 | 替代 | 状态 |
|---|---|---|
| `x.int` | `int(x)` | 弃用（编译器发出警告） |
| `x.toint` | `int(x)` | 弃用（编译器发出警告） |
| `FloatN`/`floatN` | `Q`、`Dec` 或显式 `float()` | 设计弃用；Q/Dec 尚未实现 |
| `repeat n { ... }` | `for i in range(n) { ... }` | 语法草案中的弃用建议，当前不承诺 |

## 尚未提供的设计 API

Eidos class/instance/method、所有权/借用、Actor/`parallel`、任意精度 `BigInt`/`Q`/`Dec`/`BigFloat`、真正模板/类型特化 JIT、跨 Verse 可逆计算与传送门，均只在 `future/` 或路线图中描述，当前版本不可调用。

相关文档：[API_REFERENCE.md](API_REFERENCE.md)、[SYNTAX_SUGAR.md](SYNTAX_SUGAR.md)、[V04_STATUS.md](V04_STATUS.md)、[NUMERIC_MODEL_V04.md](NUMERIC_MODEL_V04.md)。
