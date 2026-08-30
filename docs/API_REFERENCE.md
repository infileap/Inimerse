# Inimerse API 参考

> API 状态总览请先看 [API 大全](API_CATALOG.md)。本页保留运行时与语言细节。

> 本文档描述当前源码中可调用的接口（2026-08-26）。未来设计以 `future/` 为准，
> 尚未实现的 Eidos、VFS 或 Inim OS 能力不属于现有 API。

## 1. 运行引擎

```text
inimerse <script.im> [参数...]       编译并运行脚本
inimerse <script.inim>               加载预编译字节码
inimerse --version | -V              输出版本
inimerse where                       输出当前引擎路径
inimerse changelog                   显示最近变更
inimerse --lint <script.im>          静态检查
inimerse --desugar <in> [out]        输出脱糖脚本
```

常用选项：`--safe`、`--low-config`、`--time-limit N`、`--limit-mem MB`、
`--limit-vram MB`、`--limit-time SEC`、`--err-json`、`--headless`、`--no-gui`、
`--no-mods`。预编译 `.inim` 当前不保存参数表。

## 2. 语言基础

支持 `if/elif/else`、`while`、`for`、`repeat`、`break/continue`、`func`、
`return`、`import`、`task`、`thread`、`yield`、`try/catch/throw/final`、
`lock/unlock`、`send/recv` 和 `wait`。语句由换行或分号分隔。

```im
func clamp(x, lo, hi) { return max(lo, min(hi, x)) }
for i in 0..10 { say i }
unless ready { say "not ready" }
```

语法糖分为两层：内糖由核心前端直接处理（区间、分号容忍、`++/--`、尾逗号、f-string、`|>`、Result `?`、集合推导）；外糖由脱糖层处理（`fn`、`print`、`&&/||`、`//`、`unless`、`say@target`）。V0.4 已确定但仍待 parser/compiler 接入的函数式内糖包括 lambda 和 `>>`，详见 [SYNTAX_SUGAR.md](SYNTAX_SUGAR.md)。
数组/字典不可用 `+` 拼接，应使用 `push` 或显式构造。

## 3. 核心内置函数

基础：`say`、`len`、`type`、`str`、`int`、`float`、`bool`、`min`、`max`、`time_ms`、
`timer`、`range`、`push`、`pop`、`chars`、`ord`、`index`、`replace`、`startswith`、
`endswith`。

实体/精灵：`entity_spawn`、`entity_set/get`、`entity_kill`、`entity_count`、
`entity_clear`、`entity_neighbors`、`entity_at`、`entity_render`、`find_sprite`。
完整清单见 [API_BUILTIN_TABLE.md](API_BUILTIN_TABLE.md)。

### 集合、`in`、`be` 与 `case`

集合支持有限字面量（`1, 2, 3`）、集合推导式（`{x in source | condition}`）、
内置数集（`N`、`Z`、`Z+`、`Z-`、`Float1`–`Float9`）和区间（`[a,b)`、`(a~b]`）。
`in` 对标量执行成员判断，对数组/集合执行子集判断；`be` 将全局变量绑定到集合，
每次写入都会验证约束。`case` 支持等值、比较、`in`、`match` 和 `else` 分支，
按从上到下的首个命中分支执行。

## 4. 并发与资源

- `task` 是 Fiber 协作任务；`thread` 是操作系统线程。
- `start/join/stop/pause/resume/restart/kill` 管理生命周期。
- `send target value` / `recv name` 提供任务消息队列。
- `atomic_set/get/add` 提供原子全局访问。
- `mod_limit(mem_mb, vram_mb, time_s)`、`mod_usage()` 提供模组配额。

## 5. 模组接口

`isolate_run(script, timeout_ms)` 返回 `{exit, out, timedout}`；`spi_on`、`spi_emit`、
`spi_ok`、`spi_meta` 提供事件和能力声明。

Windows 服务器接口：`server_ports`、`port_check`、`port_pid`、`port_kill`、`lan_ip`、
`server_start`、`server_join`、`server_status`、`server_stop`、`server_rooms`。路径默认
从引擎目录推导，可用 `INIMERSE_ROOMS_DIR`、`INIMERSE_PROJECTS_DIR`、`INIMERSE_ENGINE`、
`INIMERSE_BRIDGE` 覆盖；项目名会进行路径遍历校验。

AI 接口：`ai_config`、`ai_register`、`ai_list`、`ai_chat`、`ai_params`、`ai_code_check`。
身份接口：`me`、`profile_get`、`profile_set` 及 OAuth 关联函数。

## 6. 能力与平台抽象

危险能力由 `CAP_*` 和 `--safe` 控制。宿主代码应使用 `src/platform/` 的
`im_platform_now_ms`、`im_platform_sleep_ms`、`im_platform_mkdirs`、
`im_platform_executable_path`、`ImMutex`、`ImThread`、`ImProcess`、`ImSocket`、`ImDir`。
不可用能力必须返回失败值，不能伪装成功。

## 7. 当前限制

1. `.inim` 暂不携带命令行参数元数据。
2. `mod_limit` 尚未完整统计 VRAM、指令数及字符串拼接。
3. Fiber 调度器在主线程长时间休眠时可能暂停，建议用 `join` 驱动。
4. Eidos、VFS、Shell、热修改、跨 Verse 变换和多目标 `say` 仍是 `future/` 设计，尚未冻结。

## 8. 相关文档

[API_CATALOG.md](API_CATALOG.md) · [API_BUILTIN_TABLE.md](API_BUILTIN_TABLE.md) ·
[PORTABILITY.md](PORTABILITY.md) · `future/` 下的五份设计文档。
