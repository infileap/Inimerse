# Infiverse 0.2.1 — Cross-platform baseline

## 本版本

- WSL2/Ubuntu POSIX 核心可构建并运行。
- PAL 提供跨平台时钟、休眠、路径、目录、环境变量、线程和 Fiber 基础接口。
- POSIX 支持脚本模块扫描与加载、文件 IO、目录操作和能力查询。
- 新增 Linux CI、POSIX 冒烟测试和 WASM 工具链检查入口。
- 清理生成缓存、测试二进制和源码备份文件。

## 已验证

```text
Windows: powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
WSL:     ./tools/posix_smoke.sh
协议:    node tools/regression.js
```

## 已知边界

POSIX 0.2.1 暂未提供 Windows 专用 GUI、WinHTTP、键鼠、原生 DLL 和完整 headless/Verse 网络后端；这些能力会通过 `has_capability` 和明确错误反馈，并在 0.3.0 的 PAL 后端任务中继续实现。

