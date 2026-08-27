# v0.3 交付状态

更新时间：2026-08-27

## 已完成

- POSIX/Linux VM 构建，Makefile 与 CMake 双入口。
- Windows/Linux/WASI 探针和统一 CTest/回归门禁。
- PAL：时间、路径、目录、线程、Fiber、进程、socket。
- POSIX `isolate_run`：子进程、输出捕获、超时终止和安全参数转义。
- POSIX TCP `net_mod` 基础 API，包含非消费式连接状态检查。
- POSIX 原生模组 `dlopen/dlsym` 加载。
- CPack TGZ、ZIP、DEB Linux 包及 GitHub Actions 发布上传。
- UPP/CRP、`.vverse` 校验/打包和多目标 say 的参考实现与回归测试。

## 进行中

- `server_mod`、HTTP 和 WebSocket 完整迁移到 `ImSocket`。
- CRP 完整认证、断线恢复、好友发现和 NAT 中继。
- 桌面工作台 Verse 创建/运行/分享/下载闭环。
- C VM 中的 `OutputStream` 与 `say@...` 去糖接入。

## 不属于 v0.3 收口

- 完整 `wasm32-wasi` VM 和浏览器宿主导入表。
- AI 居民、训练沙盒、资产/身份系统。
- 集合化类型系统、BigInt/枚举窄化和 `type` 声明语法（v3.1）。
- 全息数据、可逆计算、概率编程等前沿能力（见 `ROADMAP_FRONTIER.md`）。

## 发布门禁

```text
node tools/regression.js
make check
cmake -S . -B build -DINIMERSE_BUILD_ENGINE=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target package
```
