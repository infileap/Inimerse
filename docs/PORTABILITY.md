# 跨平台边界

Inimerse 将核心 VM 与操作系统能力分离。`src/platform/` 提供稳定的 C 接口，模块通过能力声明使用它们；模块不应散落 `_WIN32` 判断。

## 已统一的 PAL

- 时间与休眠：`im_platform_now_ms`、`im_platform_sleep_ms`
- 路径与文件系统：`im_platform_mkdirs`、`im_platform_path_join`、`im_platform_executable_path`、`im_dir_*`
- 同步与线程：`ImMutex`、`im_thread_start/join/detach`
- Fiber：`im_fiber_*`（Windows Fiber；POSIX `ucontext`）
- 进程：`im_process_spawn/pid/alive/wait/wait_kill/kill/exit_code`
- TCP：`ImSocket` 的监听、连接、超时连接、accept、收发、非阻塞和端口查询

Windows 使用 Win32/WinSock；Linux、macOS 和其他 POSIX 主机使用 BSD sockets、pthread、fork/exec。socket 层处理 `EINTR`，发送使用 `MSG_NOSIGNAL`（若可用），避免信号导致 VM 退出。

## 模块状态

POSIX 已提供：核心文件/目录、线程/Fiber/锁、进程、TCP `net_mod`、房间 `server_mod`、HTTP/串口运行时、headless 帧服务、WebSocket 基础握手。GUI、键鼠、原生 DLL、PE 嵌入和 WinHTTP 仍是宿主专用能力；不可用能力返回明确错误并可通过 `has_capability` 查询。

`server_mod` 的房间目录、项目目录和引擎路径支持环境变量覆盖：

```text
INIMERSE_ROOMS_DIR
INIMERSE_PROJECTS_DIR
INIMERSE_ENGINE
INIMERSE_BRIDGE
```

路径创建使用 PAL 递归创建，禁止依赖开发机固定盘符。

## 能力与构建

```text
inimerse capabilities
cmake -S . -B build -DINIMERSE_BUILD_ENGINE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`capabilities` 输出是运行时事实；CI 应在 Linux、Windows 和 WASI 探针上执行门禁。WASI 当前覆盖 ABI/宿主导入边界探针，完整 WASI VM 不属于本阶段已交付能力。

## 验证探针

Makefile 门禁包含：

```text
platform_probe  fiber_probe  process_probe  socket_probe
thread_probe    dir_probe    headless_probe
```

这些探针覆盖路径、目录类型、线程超时/分离、Fiber 返回、进程回收、TCP 收发和 headless JSONL 输入/帧广播。新增 PAL 时必须提供对应探针，并同时接入 Makefile 与 CTest。
