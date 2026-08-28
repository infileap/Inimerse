# Inimerse v0.3 实现接口

本文记录当前可用、已在代码中实现的 v0.3 接口。

## 桌面工作台（Tauri）

- `workbench_run(file)`：在工作区内运行 `.im` 文件。
- `workbench_stop()`：终止当前运行任务。
- `verse_local_packages()`：递归发现 `projects` 下的 `.vverse` 包。
- `verse_package_preview(file)`：只读查看包路径和大小。
- `verse_install_package(file)` / `verse_remove_package(id)`：安装和删除本地包。
- `verse_run_uri(uri)`：安全启动 `verse://local/<package>.vverse`。

所有文件操作均限制在应用工作区内，并执行 canonical 路径检查。

## CRP WebSocket

POSIX HTTP 服务提供 `/ws`。设置 `CRP_REQUIRE_AUTH=1` 后，连接必须携带 portal token；无效、过期或撤销 token 返回 `401`。Node 客户端具备消息队列、指数退避和并发安全重连。

## `.vverse` 包

`tools/vverse_pack.js` 支持：

```text
pack <directory> [output]
unpack <package> <directory>
preview <package>
```

验证器检查 `manifest.json`、`blueprint.json`、目录结构、SHA-256 签名和依赖声明格式。

## 多目标 say

Windows 与 POSIX 均支持 `console`、`log`、`chat`、`ui`、`world`、`character`、`dialogue`、`system`、`file`、`json`、`network`、`ai` 目标，以及 `say_target(target, text)`。AI 目标输出结构化 JSON 事件。

## 验收命令

```text
node tools/regression.js
make check
cargo check --manifest-path Infiverse_standard/src-tauri/Cargo.toml
```
