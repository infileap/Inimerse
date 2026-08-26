# 平台化边界

Inimerse 的移植目标是“核心可移植、系统能力模块化”，而不是让每个模块都
拥有同一套系统实现。

## 第一阶段边界

`src/platform/` 只提供内核需要的最小宿主能力：

- 单调时钟：`im_platform_now_ms`
- 休眠：`im_platform_sleep_ms`
- 递归创建目录：`im_platform_mkdirs`
- 当前可执行文件路径：`im_platform_executable_path`
- 目录迭代：`im_dir_open` / `im_dir_next` / `im_dir_close`
- 互斥锁：`im_mutex_new` / `im_mutex_lock` / `im_mutex_unlock`

Windows 和 POSIX 实现在同一组 C 接口下提供实现。内核后续应逐步改为依赖
这些接口，而不是直接包含 `windows.h`、调用 `GetTickCount64` 或 `_mkdir`。
同步接口位于 `src/platform/sync.h`，不向 VM 暴露 pthread 或 Win32 具体类型。
线程启动接口位于 `src/platform/thread.h`，统一封装 `_beginthreadex` 与
`pthread_create`。
Fiber 接口位于 `src/platform/fiber.h`：Windows 使用 Win32 Fiber，POSIX 使用
`ucontext`。VM 的 Fiber 调用已通过兼容宏收口到该接口。

Fiber 后端可单独回归：

```text
make fiber-probe
./fiber_probe
```

VM 入口已开始移除 `<direct.h>` 的硬依赖，并为 POSIX 路径提供 `chdir`/`mkdir`
映射；崩溃处理器也限定在 Windows 构建中。Fiber、原生线程和网络模块仍在后续
迁移范围内。

主程序的自身路径解析现统一调用 `im_platform_executable_path`，不再在入口处
分别维护 `GetModuleFileName` 与 `/proc/self/exe` 两套逻辑。

`changelog` 命令也已提供 POSIX 文件遍历实现：按 `stat` 修改时间排序并显示最新
三个 `CHANGES*.txt` 文件；Windows 继续使用原生文件时间 API。

## 模块策略

GUI、串口、系统输入、OAuth 浏览器、WinHTTP、PE/EXE 嵌入和 Inno Setup 均属于
平台模块或发布工具，不进入核心 ABI。Linux/macOS 可以提供对应模块，也可以
明确报告“当前平台不可用”。

`src/platform/features.h` 是模块能力的唯一编译时入口。模块不应直接用
`_WIN32` 判断功能，而应使用 `IM_HAS_GUI`、`IM_HAS_SERIAL`、
`IM_HAS_NATIVE_INPUT`、`IM_HAS_PE_EMBED` 和 `IM_HAS_WINHTTP`。

## 构建策略

```text
cmake -S . -B build -DINIMERSE_BUILD_ENGINE=OFF   # 任意平台：平台探针
cmake -S . -B build -DINIMERSE_BUILD_ENGINE=ON    # Windows：完整引擎
```

POSIX 构建显式关闭完整引擎，避免把 GUI、WinHTTP、PE 嵌入等能力伪装成可用。
待 VM 的 POSIX 线程/Fiber 后端和网络模块完成后，再开启该选项。

`isolate_mod` 当前明确限定为 Windows；POSIX 构建不会注册不完整的假沙箱，待
基于 `ImProcess` 与管道 API 的输出捕获、超时和资源限制实现完成后再开放。

跨平台 TCP 接口已加入 `src/platform/socket.h`，覆盖初始化、监听、连接、接受、
收发和关闭；Windows 使用 WinSock，POSIX 使用 BSD sockets。`socket_probe` 已
通过 Windows 回归。

当前 `net_mod` 与 `server_mod` 的完整功能仍限定 Windows；POSIX 构建不会注册
依赖 WinSock 或 Win32 进程控制的实现。`server_mod` 已移除开发机固定盘符：房间
目录、项目目录和引擎/桥接程序默认从当前可执行文件目录推导，也可分别通过
`INIMERSE_ROOMS_DIR`、`INIMERSE_PROJECTS_DIR`、`INIMERSE_ENGINE` 和
`INIMERSE_BRIDGE` 覆盖；目录创建使用 `im_platform_mkdirs`，房间删除使用标准
`remove`。房间枚举现通过 `src/platform/dir.h` 的目录迭代器完成（Windows
内部仍使用原生句柄，POSIX 使用 `opendir`），模块本身不再直接依赖
`FindFirstFileA`；后续迁移目标是让 TCP 基础调用使用 `ImSocket`。

完整 Windows 引擎产物已重新生成并部署到 `%USERPROFILE%\Infiverse\inimerse.exe`；
构建脚本调用超出外层等待时间，但产物时间戳与仓库 `inimerse.exe` 一致。

VM 的全局锁、分片锁、消息锁和脚本锁现已全部通过 `ImMutex` 平台接口分配、
加锁、解锁和释放；Windows 构建已回归通过，未再依赖 VM 内部的
`CRITICAL_SECTION` 对象布局。

子进程注册表（`child_proc`）的锁和计时也已迁移到 `ImMutex` 与
`im_platform_now_ms`；进程创建/终止本身仍保留在 Windows 专用实现中，待
POSIX `fork/exec` 后端接入后再开放跨平台沙箱。

跨平台进程接口已建立于 `src/platform/process.h`，提供启动、PID、存活检查、
等待、终止和释放；Windows 使用 `CreateProcess`，POSIX 使用 `fork/exec`。
`process_probe` 已验证 Windows 后端，下一步将把 `child_proc` 和 `isolate_mod`
迁移到该接口。`child_proc` 现已完成迁移，注册表不再保存 Win32 进程句柄，
而是保存不透明的 `ImProcess*`。

## 本地验证

在 GCC/Clang 环境中可单独编译平台边界探针：

```text
cc -std=c11 -Isrc src/platform/platform.c src/platform/platform_probe.c -o platform_probe
```

探针通过后，再逐步把 VM 的时钟、休眠和路径调用迁移到该接口。
