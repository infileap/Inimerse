# Inimerse / Infiverse 版本路线图（v0.5–v0.6）

本文细化 `future/infiverse-inim-os-summary.md` 中的愿景，将其拆分为可交付、可测试的版本目标。v0.4 提供的运行时能力、Eidos 对象模型、包管理器和 VFS 是 v0.5 的前置条件。

## 版本定位

- **v0.5**：编译化与底层接口，支持多语言迁移，从“脚本引擎”走向“可扩展运行时平台”。
- **v0.6**：Inim OS 与 Infiverse 规范，把运行时能力提升为操作系统级服务和统一内容平台。

## v0.5：编译化与底层接口

目标：为 Inimerse 提供编译器前端、稳定 ABI、自举能力与跨语言迁移工具链，使脚本语言能以高性能、低开销方式嵌入到 C/C++/Java/Python 等系统中。当前状态：v0.5 已发布（2026-09-25），但部分规划目标尚未实现。

### 实施状态（当前）

**已完成部分：**
- 自举编译器阶段 0：宿主编译器→stage1→stage2 循环已完成基础框架，宿主版为 C++，stage1 由 Inimerse 编译器生成，stage2 由 stage1 重新生成。规范化字节码哈希一致性验证已启用，但跨平台回归阈值和完整基准套件尚未固定。
- 稳定 Native ABI：`native` 模块声明、C ABI 调用、动态库加载策略已实现，Windows/POSIX 差异隔离部分完成。
- 跨语言 API：C/C++、Java、Python 三条绑定线均存在原型，但缺少统一 IDL/绑定生成器、错误转换测试和自动迁移报告工具。
- JIT 模板后端和 AOT 实验链路存在草案，但未进入稳定通道。
- 编译器前端基础设施：词法分析、语法解析、AST 生成、语义分析。
- 优化器：循环不变式外提、死代码消除、常量折叠。
- 语法糖支持：`?`、`|>`、`case try`、外糖可执行子集、`eidos`/`ed` 对象语法。
- AOT 打包通道：`inimerse compile --aot <script.im> [out.exe]` 产出原生可执行文件（引擎副本 + 嵌入规范化字节码，`bytecode_load_from_exe` 自执行，跨平台）。
- Wasm MVP 输出后端：`inimerse compile --abi-target wasm <script.im> [out.wasm]` 直接生成 WebAssembly MVP 二进制（数值子集：int/float/bool、算术/比较、if/while/repeat、用户函数递归、全局变量；线性内存槽 `tag+payload` 表示，IO 走固定 `env.*` 导入表）。**解释器等价性已验证**：13 个用例（含递归 fib、循环、浮点、int32 溢出→float、除零、mod）输出逐字节一致（`tools/wasm_backend.test.py`，CTest 标签 `wasm`），导出 `inimerse_probe/abi_version/capabilities` 符合 WASM ABI 探针约定，宿主运行器 `tools/wasm_run.js`（Node）。字符串/集合/线程等构造在编译期明确拒绝。SIMD/GC/完整语言面为后续迭代。
- 测试套件：`simple.im`、`syntax_simple_test.im`、`t_*.im` 测试全部通过。
- 命令行工具：`inimerse compile/buildc/run/profile/symbols` 已实现（AOT/Wasm 目标未进入稳定通道，选择时明确报错退出）。
- 增量编译：`buildc --incremental` 基于 `.inim` 依赖尾块（主源文件 + 全部已解析 import 的相对路径与 SHA-256，见 `src/compilation/deps.c`）做重编译判定；任一依赖变化即重编译，`--force` 强制重建。
- 可复现构建：`buildc --reproducible` 输出 `<out>.build.json`（引擎版本、字节码格式、ABI、目标、依赖哈希、产物 SHA-256）；依赖路径相对化后，相同项目布局跨目录/跨时间字节一致（`tools/cli_incremental.test.py` 断言）。
- 函数级剖析器 + flamegraph：`inimerse profile <script.im> [out.prof]` 输出每函数调用/耗时报告，并附带 `.prof.folded` 折叠调用栈（调用计数 + 时间加权两种视图）；`tools/prof2flame.py` 转换为 flamegraph.pl 折叠输入或 speedscope JSON。
- 统一 IDL/绑定生成器：`tools/bindgen.py <iface.def> --language c|c++|java|python`，单一接口定义驱动三语言绑定（标量/字符串/数组/error，UTF-8 C 字符串+长度约定），类型错误与重复函数在生成期拒绝（`tools/bindgen.test.py`）。
- C/C++ 头文件扫描：`tools/cpp_scan.py`（roadmap §3.1）识别 `extern "C"` 函数、struct、enum、宏；标量函数可 `--emit-idl` 生成 bindgen 接口，函数指针/指针参数/C++ ABI 输出为手动适配点而非静默包装。
- Python 模块扫描：`tools/python_scan.py`（roadmap §3.3）识别函数、类、方法、装饰器；类型注解齐全的标量函数可 `--emit-idl`，无注解/async/装饰器/`*args` 报告为手动适配点。
- 示例与构建集成：`examples/cpp_native.im`、`examples/java_bridge.im`、`examples/python_bridge.im`、`examples/interface.def` 展示三条绑定管线；`examples/build.gradle` / `examples/pom.xml` 提供 Gradle/Maven 依赖集成（§3.2 验收）。
- 错误转换测试 + 迁移报告：`bindgen.test.py` 断言三种语言的错误转换约定（C int 0=成功 / Java InimerseException / Python InimerseError）；`tools/migrate_report.py` 扫描 C/Python 源码生成不可转换语法（file:line）、依赖与运行时假设清单。
- 调试信息：`buildc --debug-info` 输出 `<out>.dbg`（文本行号表 main/func/thread 分块 + STABS 风格符号）与 `<out>.debug_line`（DWARF 5 行号程序，已用独立解码器验证）。完整 ELF 容器与调试器断点需等待 AOT 后端进入稳定通道。
- 包签名/依赖/版本检查的跨平台回归（M3 收尾）：修复 POSIX 数据目录缺陷（`home_dir()` 只按 `\` 分割可执行路径 → 整个 exe 路径被当目录，身份/包/cache 全部写不进去；14+ 处路径拼接反斜杠统一为 `/`，Windows 兼容不变）；新增 `INIMERSE_HOME` 覆盖（部署剖面/测试/只读部署）；身份首次使用自动建目录。**确定性签名测试向量**：`vtest_signed.vverse` 以固定种子（sha256("inimerse ed25519 test vector v1")）重新生成——同 seed 同源打包字节一致（可复现，衔接 §24.4/§54.9），并被测试断言可再生成。`tools/verse_pack.test.py`（CTest `verse_pack_regression`）覆盖：身份生成/签名往返/篡改拒签、`verse_pack` 自动签名（publisher+signature）、自签包 `verse_open` 通过、内容篡改 sha256 拒绝、签名篡改拒绝、min_version 依赖拒绝、向量可复现性。
- POSIX 内嵌 hub 服务器（M2 收尾）：`verse_listen(port)` 在 POSIX 启动真实分发服务（委托 `http_posix` 的 TCP hub），同端口提供 **UDP 分发**（`GET /v/<id>`）；`verse_udp_fetch` 用原生 UDP 实现 3 秒截止的客户端传输；新增 `GET /v/<id>` 路由（与 Windows hub 和 `verse_fetch` 的方言一致，此前 POSIX hub 只有 `/package/<id>`，两端路径不通）。实测：TCP 与 UDP 返回包文件字节一致，`verse_open("verse://host/id")` 与 `verse_open("verse://udp://host/id")` 均成功（`tools/hub_dist.test.py`，CTest `hub_dist_regression`）。
- POSIX 运行时对等（M0/M2 基础）：`infiverse_mod`（Verse/Layer/Block/Portal 模型）、`record_mod`（声明式存档）、`verse_dist_mod`（`.vverse` 打包/ed25519 签名/验签/依赖检查/更新、hub 列表与 HTTP 获取）在 POSIX 上从 stub 变为真实实现（WinHttp→`im_http_request`、Win32 目录遍历→dirent、时间源→`im_platform_now_ms`）；Windows 专属部分（内嵌 HTTP/UDP hub 服务器、child_proc 启动）显式降级并提示。
- CRP 会话收敛（M2，白皮书 §55.3/§55.5/§55.6）：`crp_session` 扩展握手与能力协商（版本不匹配显式拒绝，§24.6）、租约（begin/touch/expiry）、消息序列（accept/duplicate/gap）、恢复决策（replay 窗口 vs 快照）；WebSocket 会话接入：`?ver=&caps=` 协商、重复消息丢弃、缺口回 `resume_required`+`last_applied`、租约过期断开。端到端测试 `tools/crp_session.test.py`（headless hub + Node 最小 WS 客户端 `tools/mini_ws.js`）。
- M1×M2 衔接（事件日志补发，§51.9/§46.8）：`/session/resume`（replay 模式）从持久化回放窗口补发缺失事件并标注 `complete:true`；请求点超出保留窗口时**显式返回 `snapshot_required`+`oldest_seq/latest_seq`**（不再静默返回不完整重放，§24.6）；WebSocket 事件按 (verse,peer) 写入同一回放窗口（`session_note_event`）。**修复服务端并发缺陷**：WebSocket 客户端改为每连接独立线程（此前单线程 accept 循环被长连接阻塞，第二个客户端/health 无法连接——局域网多客户端的基本前提）。测试扩展：`tools/crp_session.test.py` 覆盖补发、超窗快照、多连接并发。
- 确定性回放闭环（M1，roadmap §52 协议核心 3+4 / §24.2 事件信封 / §24.5 错误分类 / §48.4 命名随机流）：`src/mod/replay_mod.c` 提供 `replay_seed/replay_rand`（命名随机流）、`replay_tick/replay_time`（逻辑时钟）、`replay_log_begin/log/log_end`（JSONL 事件日志，信封含 event_id/idempotency_key/链式 sha256）、`replay_state_hash`（规范化状态哈希）、`replay_load/replay_verify`（链校验，篡改报 IntegrityError）。错误按 §24.5 分类（NotFound/ParseError/IntegrityError/ResourceLimit/TypeError/ProtocolError）以 dict 返回。跨平台（POSIX 不再是 stub）。测试 `tools/replay.test.py`（确定性 + 篡改检测）。
- 通道性能对比：`tools/perf_compare.py`（§2.3 验收以测量数据为准，结果见 SELFHOST_BENCHMARK.md——Wasm MVP 实测 ~1.8x 解释器，AOT 打包通道等同解释器，≥2x 目标属优化型 AOT 后端）。
- 回归测试：`tools/cli_incremental.test.py`、`tools/selfhost_bench.py`（`benchmark;regression`）、`tools/bindgen.test.py`、`tools/wasm_backend.test.py`（`wasm`）、`tools/scan_tools.test.py`、`tools/replay.test.py`（`runtime;regression`）、`tools/crp_session.test.py`（`protocol;regression`）（均注册 CTest）。

**未实现部分：**
- 优化型 AOT 后端（原生代码生成，性能目标 ≥2x 解释器）；当前 `run --aot` 明确报错，`compile --aot` 走打包通道。
- Wasm 完整语言面：SIMD 优化、WebAssembly GC、字符串/集合/线程的 wasm 侧运行时（当前为数值子集 MVP，等价性已验证）。
- DWARF/STABS 完整集成：ELF 容器封装与 gdb/lldb 断点（依赖 AOT 原生代码生成）。

### 1. 自举编译器与可复现构建

#### 1.1 字节码与 ABI 版本化

- 字节码格式 `infiverse.mv1`，包含 opcode、常量池、符号表、指令编号和版本号。
- ABI 版本 `infiverse.mv1/abi/1.0`，定义 `i32/i64/i16/i8/u32/u64/u16/u8/f32/f64/bool/string/array/struct/error/option/result` 的内存布局与传递规则。
- 版本不兼容时构建期明确失败，提供 `--abi-version` 选项控制目标版本。

#### 1.2 完整增量编译与可复现构建

- 源文件依赖图、SHA-256 校验和、增量编译标志。**已完成**：`buildc --incremental`（显式开关）+ `.inim` 依赖尾块，验证命令 `ctest -R cli_incremental_regression`。
- 可复现构建开关：固定编译器版本、固定链接器、禁用随机种子、固定路径。**已完成**：`buildc --reproducible` + `<out>.build.json` 构建记录（引擎版本/ABI/依赖哈希/产物 SHA-256）；依赖路径相对化 + 编译期固定到脚本目录实现路径无关；编译器无随机源。**链接器固定**随 AOT 稳定通道交付。
- 验收：同一项目、相同源码和编译选项，在不同日期和不同宿主上生成相同的规范化字节码和结果哈希。**已验证**：`cli_incremental.test.py` 在两棵独立目录树构建并断言字节级一致。

#### 1.3 三阶段自举流程

执行顺序：
1. **bootstrap**：宿主 C++ 编译器（当前版本）生成初始 Inimerse 字节码。
2. **stage1**：Inimerse 编译器编译宿主字节码，生成第一个自举产物。
3. **stage2**：stage1 编译自身，生成最终自举产物。

每轮构建固定源码、编译参数、依赖锁文件，并记录：
- 编译耗时、启动耗时、峰值内存、生成物大小。
- 规范化字节码哈希、运行结果哈希、诊断输出差异。
- 代表性基准套件（集合变换、`case try`、VFS、编译器自身）的中位数/P95。
- 相对宿主编译版本的比值、回归阈值和超阈值原因。

报告写入 `docs/SELFHOST_BENCHMARK.md`，作为发布门禁；stage2 未达到阈值时，不宣称“已完成自举”。**已建立**：运行器 `tools/selfhost_bench.py`（集合变换/`case try`/VFS/编译器自身四套例，中位数+P95，`ctest -R selfhost_benchmark`），回归阈值 20%，初始报告见 `docs/SELFHOST_BENCHMARK.md`。

### 2. 编译器前端与底层接口

#### 2.1 编译器前端

- 词法分析、语法解析、AST 生成、语义分析。
- 类型推导、循环不变式外提、死代码消除、常量折叠。
- 语法糖脱糖：`?`、`|>`、`case try`、外糖可执行子集、`eidos`/`ed` 对象语法。

验收：现有 `simple.im`、`syntax_simple_test.im`、`t_*.im` 测试全部通过；新增语法糖不改变语义；错误信息包含源文件、行列号和可操作建议。

#### 2.2 稳定 Native ABI

- `native` 模块声明、能力沙箱（禁用 `unsafe`）、动态库加载策略（Windows: `LoadLibrary` / POSIX: `dlopen`）。
- 函数签名：`void fn(i32 a, i64 b, string c)` → C ABI 一致。
- 参数/返回值布局：整数传 `int`，浮点传 `double`，结构体传引用/指针，字符串传 UTF-8 C 字符串+长度。
- RAII 适配模板：`native::File`、`native::Memory`、`native::Thread`。
- 错误转换：Inimerse `error` → C `int`（0=success，非0=错误码）或 C `errno`。

验收：C/C++ 项目可调用 Inimerse 函数并正确处理错误；内存泄漏检测通过 Valgrind/ASan；ABI 版本不兼容时构建期失败。

#### 2.3 AOT 与 Wasm 后端实验

- AOT 输出原生可执行文件（`inimerse.aot`）或共享库（`libinimerse.so`）。
- Wasm 输出 `.wasm`，包含 SIMD 优化和 WebAssembly GC 支持。
- 两种后端共享相同的字�码格式和 ABI，确保运行结果一致。

验收：同一 `.im` 文件可生成解释执行、AOT 和 Wasm 三种产物；性能测试显示 AOT/Wasm 优于解释器至少 2 倍；Wasm 模块可在浏览器/Node.js 中加载。

### 3. 跨语言 API 与迁移工具

#### 3.1 C/C++ 绑定

- 头文件扫描工具：`python tools/cpp_scan.py`，识别 `extern "C"` 函数、结构体、枚举、宏。
- C ABI 包装：`inimerse::wrap()` 模板，自动生成 `i32/int`, `i64/long`, `f64/double`, `string/char*`, `array` 转换。
- CMake 项目导入：`inimerse_add_module()`，自动处理链接、包含路径、符号导出。

验收：示例项目（`examples/cpp_native.im`）可编译并调用 C++ 函数；迁移报告列出不兼容语法和手动适配点。

#### 3.2 Java 绑定

- JVM 调用桥：`InimerseBridge.java`，加载动态库、调用原生函数、转换 Java 异常到 Inimerse `error`。
- 类/方法映射：`InimerseClass`、`InimerseMethod` 包装器，支持构造、字段访问、方法调用。
- Gradle/Maven 依赖导入：`inimerse-plugin.gradle`、`pom.xml` 示例。
- 异常转换：Java `CheckedException` → Inimerse `error`，Inimerse `error` → Java runtime exception。

验收：Java 项目（`examples/java_bridge.im`）可调用 Inimerse 函数并正确处理错误；异常边界测试通过；Gradle 构建成功。

#### 3.3 Python 绑定

- 模块扫描：`python tools/python_scan.py`，识别 Python 函数、类、方法、装饰器。
- CPython/ABI3 扩展桥：`inimerse_extension.c`，实现 `PyInit_inimerse()`。
- 异步调用适配：`async_inimerse`，将 Python `async/await` 映射到 Inimerse 异步 Result 传播。
- 迁移报告：自动识别不可转换语法（如 Python 垃圾回收、动态类型、元类）、依赖和运行时假设。

验收：Python 项目（`examples/python_bridge.im`）可调用 Inimerse 函数并正确处理错误；迁移报告列出手动适配点；异步边界测试通过。

#### 3.4 统一 IDL/绑定生成器

- 接口定义语言：`interface inimerse_native { fn foo(i32 a); }`。
- 生成器：`python tools/bindgen.py interface.def --language c/c++/java/python`。
- 支持重载、默认参数、异常、回调函数、异步接口。
- 输出包含类型安全检查和文档注释。

验收：三种语言可从同一接口定义生成绑定；类型错误在编译期被检测；绑定生成的代码可编译并运行。

### 4. 编译器与运行时互操作

- 命令行工具：`inimerse compile <file>`、`inimerse run --abi=host|wasm|aot <file>`。**部分完成**：`compile`/`run` 已实现（host 字节码与解释执行）；wasm/aot 通道未稳定，选择时明确报错。
- 调试信息：DWARF/STABS 生成，支持 `gdb`/`lldb` 调试。**部分完成**：`buildc --debug-info` 生成行号表 sidecar（文本 + DWARF 5 行号程序 + STABS 风格符号）；调试器断点需 AOT 原生代码。
- 性能剖析：`inimerse profile --sample --output=perf.data`，生成 flamegraph。**已完成（函数级）**：`inimerse profile <file> [out.prof]` 输出调用/耗时 + `.prof.folded` 折叠栈；`tools/prof2flame.py` 转 flamegraph.pl/speedscope。采样式剖析待 AOT。
- 符号表导出：`inimerse symbols --output=symbols.txt`。**已完成**：`inimerse symbols <input.im> [output.symbols]`，或 `compile --symbols`。

验收：调试器可附加并断点设置；性能数据可可视化；符号表与源码一致。

## v0.6：Inim OS、Infiverse 规范与 2D 引擎

目标：把运行时能力提升为操作系统级服务和统一内容平台，为 Verse、模组和跨节点互操作提供标准化基础设施。

### 1. Inim OS for Windows/Linux

#### 1.1 用户态服务、任务/进程模型、权限与沙箱

- Inim OS Daemon：常驻进程，管理 Verse、包和系统资源。
- 任务与进程：`Task`、`Process`、`Thread`，支持优先级、超时、取消和任务图。
- 权限与沙箱：能力模型，基于 POSIX capabilities 或 Windows ACL，支持"最小权限"原则。

验收：同一个 Inim OS 服务包可在 Windows 和 Linux 启动；权限越界、路径逃逸和资源耗尽均有测试与可观测错误。
**已完成**

#### 1.2 虚拟文件系统（VFS）

- VFS 层级：`/system/`、`/verse/<verse_id>/`、`/packages/`、`/cache/`、`/userdata/`。
- 虚拟文件：`vfile`，支持可读写、可缓存、可签名、可回放。
- 资源导入管线：`inim os import <path>`，自动处理路径、压缩、签名和缓存。

验收：VFS 路径访问权限正确；虚拟文件读写行为一致；资源导入管线支持增量更新。
**已完成**

#### 1.3 统一设备/输入/窗口/音频/网络 PAL

- 设备抽象：`device::Keyboard`、`device::Mouse`、`device::Gamepad`。
- 窗口抽象：`window::Window`，支持多窗口、全屏、窗口级状态保存/恢复。
- 音频抽象：`audio::Audio`，支持 2D 空间音效、混音、流式加载。
- 网络抽象：`network::Socket`，支持 TCP/UDP、代理、加密传输。

验收：同一 Inim OS 服务包可在 Windows 和 Linux 行为一致；跨平台 API parity 验证通过。
**已完成**

#### 1.4 包、服务和 Verse 生命周期管理

- 包管理器集成：`inim package install --os=windows/linux --auto-update`。
- Verse 部署：`inim verse deploy <verse_id>`，支持签名验证、依赖检查和回滚。
- 日志系统：`log::log(level, message)`，支持按级别、按应用、按时间过滤。
- 更新机制：热更新、版本回滚、崩溃恢复。
- 崩溃报告：`log::crash_report()`，生成 crash dump 和堆栈跟踪。

验收：包和 Verse 可自动更新；更新失败时回滚到上一版本；崩溃报告完整且可分析。
**已完成**

### 2. Infiverse 内核及规范

#### 2.1 规范固化与 RFC 发布

- 固化 UPP、CRP、VDP、ABI、能力模型和内容寻址规范。
- 发布版本化 RFC：`infiverse.protocol.v1/`，包含接口定义、数据格式、错误码、测试向量。
- 规范合规标识：每个实现（Inimerse、Infiverse Standard、外部工具）标注符合版本和兼容性矩阵。

验收：规范文档可独立于实现审查；外部实现可通过互操作测试；规范扩展保持向后兼容。
**已完成**

#### 2.2 Verse/Layer/Block/Cell 空间模型

- Verse/Layer/Block/Cell 空间模型参考实现：`verse::Layer`、`block::Block`、`cell::Cell`。
- 传送门寻址：蜡烛二进制寻址、坐标哈希映射、传送门映射和路由。
- 事件时序：`event::Event`、`event::Queue`、`event::Stream`，支持确定性排序和事件重放。
- 非欧几何：支持平坦、球面、双曲空间，以及手工邻接表构成的折叠空间。

验收：至少两种独立实现通过互操作测试；传送门寻址与事件时序与规范一致；非欧几何视图计算正确。
**已完成**

#### 2.3 Hub、客户端、服务器和工具的互操作

- Hub：多节点协同中心，支持节点发现、节点加入/离开、节点状态同步。
- 客户端：工作台（`Infiverse_standard`）支持连接到 Hub，加载 Verse 和管理用户数据。
- 服务器：多节点服务器，支持 Verse 部署、权限管理和流量控制。
- 工具：`inim verse pull/push/verify`，支持跨节点传输和校验。

验收：同一 Verse 可在多个节点运行并保持状态一致；节点间传送门和事件同步正确；工具命令在跨平台环境下行为一致。
**已完成**

- Inim OS：用户态服务、任务/进程模型、权限与沙箱框架已完成。内核态服务、文件系统、网络、显示引擎、渲染管线、物理引擎、音频引擎、字体引擎、模板引擎、数据库、队列、事件、定时器、IPC、安全/沙箱、进程间通信、内存管理、文件 I/O、网络 I/O、显示管理、渲染、物理、音频、字体、模板、数据库、队列、事件、定时器、安全/沙箱等子系统已完成原型和基本测试。
- Infiverse 规范（UPP、CRP、VDP、ABI、能力模型）在 `Infiverse_standard/` 中已完成草案和部分集成。标准库已建立基础架构，核心集合与类型已实现，跨平台 I/O、网络和系统调用已完成。
- 2D 游戏引擎已实现瓦片地图、精灵、输入、场景树、动画系统、音频抽象和编辑器集成。包含物理引擎、渲染管线和游戏循环支持。

### 1. Inim OS for Windows/Linux

#### 1.1 用户态服务与进程模型

- Inim OS Daemon：常驻进程，管理 Verse、包和系统资源。
- 任务与进程：`Task`、`Process`、`Thread`，支持优先级、超时、取消和任务图。
- 权限与沙箱：能力模型，基于 POSIX capabilities 或 Windows ACL，支持“最小权限”原则。

验收：同一个 Inim OS 服务包可在 Windows 和 Linux 启动；权限越界、路径逃逸和资源耗尽均有测试与可观测错误。

#### 1.2 虚拟文件系统（VFS）

- VFS 层级：`/system/`、`/verse/<verse_id>/`、`/packages/`、`/cache/`、`/userdata/`。
- 虚拟文件：`vfile`，支持可读写、可缓存、可签名、可回放。
- 资源导入管线：`inim os import <path>`，自动处理路径、压缩、签名和缓存。

验收：VFS 路径访问权限正确；虚拟文件读写行为一致；资源导入管线支持增量更新。

#### 1.3 统一设备/输入/窗口/音频/网络 PAL

- 设备抽象：`device::Keyboard`、`device::Mouse`、`device::Gamepad`。
- 窗口抽象：`window::Window`，支持多窗口、全屏、窗口级状态保存/恢复。
- 音频抽象：`audio::Audio`，支持 2D 空间音效、混音、流式加载。
- 网络抽象：`network::Socket`，支持 TCP/UDP、代理、加密传输。

验收：同一 Inim OS 服务包可在 Windows 和 Linux 行为一致；跨平台 API parity 验证通过。

#### 1.4 包、服务和 Verse 生命周期管理

- 包管理器集成：`inim package install --os=windows/linux --auto-update`。
- Verse 部署：`inim verse deploy <verse_id>`，支持签名验证、依赖检查和回滚。
- 日志系统：`log::log(level, message)`，支持按级别、按应用、按时间过滤。
- 更新机制：热更新、版本回滚、崩溃恢复。
- 崩溃报告：`log::crash_report()`，生成 crash dump 和堆栈跟踪。

验收：包和 Verse 可自动更新；更新失败时回滚到上一版本；崩溃报告完整且可分析。

### 2. Infiverse 内核及规范

#### 2.1 规范固化与 RFC 发布

- 固化 UPP、CRP、VDP、ABI、能力模型和内容寻址规范。
- 发布版本化 RFC：`infiverse.protocol.v1/`，包含接口定义、数据格式、错误码、测试向量。
- 规范合规标识：每个实现（Inimerse、Infiverse Standard、外部工具）标注符合版本和兼容性矩阵。

验收：规范文档可独立于实现审查；外部实现可通过互操作测试；规范扩展保持向后兼容。

#### 2.2 Verse/Layer/Block/Cell 空间模型

- Verse/Layer/Block/Cell 空间模型参考实现：`verse::Layer`、`block::Block`、`cell::Cell`。
- 传送门寻址：蜡烛二进制寻址、坐标哈希映射、传送门映射和路由。
- 事件时序：`event::Event`、`event::Queue`、`event::Stream`，支持确定性排序和事件重放。
- 非欧几何：支持平坦、球面、双曲空间，以及手工邻接表构成的折叠空间。

验收：至少两种独立实现通过互操作测试；传送门寻址与事件时序与规范一致；非欧几何视图计算正确。

#### 2.3 Hub、客户端、服务器和工具的互操作

- Hub：多节点协同中心，支持节点发现、节点加入/离开、节点状态同步。
- 客户端：工作台（`Infiverse_standard`）支持连接到 Hub，加载 Verse 和管理用户数据。
- 服务器：多节点服务器，支持 Verse 部署、权限管理和流量控制。
- 工具：`inim verse pull/push/verify`，支持跨节点传输和校验。

验收：同一 Verse 可在多个节点运行并保持状态一致；节点间传送门和事件同步正确；工具命令在跨平台环境下行为一致。

### 3. Infiverse 2D 游戏引擎

#### 3.1 场景、精灵、瓦片地图

- 场景树：`scene::Scene`、`scene::Node`，支持父子关系、局部变换和世界坐标转换。
- 精灵：`sprite::Sprite`，支持缩放、旋转、翻转、纹理和动画帧。
- 瓦片地图：`tilemap::Tilemap`，支持可变地图大小、瓦片类型和群系生成。
- 碰撞：`collision::Collider`、`collision::AABB`、`collision::Raycast`，支持碰撞检测和响应。

验收：可创建、运行、保存并发布一个完整 2D 示例游戏；场景树和瓦片地图支持动态更新。

#### 3.2 相机、动画、音频和输入抽象

- 相机：`camera::Camera`，支持跟随目标、缩放、裁剪和摄像机特效。
- 动画：`anim::Animation`，支持关键帧、插值、曲线和动画混合。
- 音频：`audio::Audio`，支持 2D 空间音效、混音、流式加载和音频事件。
- 输入：`input::Input`，支持键盘、鼠标、游戏手柄和触摸屏。

验收：相机可平滑跟随目标；动画可跨场景混合；音频支持 2D 空间定位；输入支持跨平台映射。

#### 3.3 确定性更新循环、存档/回放、脚本热重载

- 确定性更新：`engine::update()`，支持确定性排序和随机数种子。
- 存档/回放：`save::Save`、`save::Load`，支持事件重放和状态恢复。
- 脚本热重载：`script::Reload`，支持运行时编译和模块替换，不中断游戏循环。

验收：确定性更新可复现；存档/回放与原始状态一致；脚本热重载不导致内存泄漏或状态不一致。

#### 3.4 `.vverse` 作为项目/分发格式

- `.vverse` 作为 Verse、模组、资源和配置的 ZIP 容器，包含 `manifest.json`、`assets/`、`scripts/`。
- 签名验证：`inim verse verify --require-signature`，支持 Ed25519 签名。
- 依赖管理：`manifest.json` 包含依赖列表，支持版本约束和自动更新。
- 增量资源缓存：`vverse cache`，支持懒加载和缓存管理。

验收：`.vverse` 可在多平台上解压和运行；签名验证可拒绝篡改包；依赖解析和自动更新成功。

#### 3.5 编辑器工作台

- 场景树：可视化编辑场景节点和层级。
- 属性面板：编辑精灵、瓦片地图、相机、动画属性。
- 脚本编辑：内置脚本编辑器，支持语法高亮和自动补全。
- 运行/停止：一键运行和停止游戏，支持调试和性能剖析。
- 诊断工具：内存使用、帧时间、资源加载、性能瓶颈分析。
- 打包发布：一键生成 `.vverse` 和 Windows/Linux 安装包。

验收：可创建、运行、保存并发布一个完整 2D 示例游戏；Windows/Linux 行为一致；诊断工具输出准确。

### 4. 标准库搭建

#### 4.1 核心集合与类型

- 集合：`set`、`map`、`array`、`list`、`dict`，支持泛型和迭代。
- 类型：`optional`、`result`、`either`、`enum`，支持模式匹配和穷尽性检查。

验收：核心集合和类型与 v0.4 兼容；泛型支持完整；模式匹配和穷尽性检查正确。

#### 4.2 跨平台 I/O、网络和系统调用

- 文件 I/O：`io::File`、`io::Reader`、`io::Writer`，支持异步读写。
- 网络：`net::Socket`、`net::Server`、`net::Client`，支持 TCP/UDP、代理、加密传输。
- 系统调用：`sys::Process`、`sys::Environment`、`sys::Clock`，支持进程管理、环境变量和时间获取。

验收：跨平台 I/O 行为一致；网络支持 TCP/UDP；系统调用跨平台可用。

#### 4.3 并发与异步

- 线程：`thread::spawn`、`thread::join`、`thread::yield`，支持线程池。
- 异步：`async::Await`、`async::Promise`，支持异步流和协程。

验收：并发测试通过；异步边界正确；线程安全保证。

## 跨版本原则

1. **协议和 ABI 优先于实现**：新增字段必须向后兼容，每个版本都必须提供迁移指南和可重复测试夹具。
2. **实验特性通过显式开关启用**：不能破坏默认解释器路径；失败时有明确降级和错误信息。
3. **性能目标以基准数据为依据**：不以未经验证的理论倍数作为承诺；每个版本都必须提供性能基准数据。
4. **跨平台运行时 API parity**：将核心内置与 Windows/POSIX 平台后端分层，确保同一脚本的通用函数在两端注册、返回值和错误类型一致。
5. **规范先于实现**：规范文档与测试向量同步发布，外部实现可通过互操作测试。

## 版本依赖

- v0.5 依赖 v0.4 的运行时能力、Eidos 对象模型、包管理器和 VFS。
- v0.6 依赖 v0.5 的稳定 ABI、编译链、自举能力和跨语言 API。
- v0.6 的 Inim OS 依赖 v0.5 的 Native ABI 和跨平台 I/O。
- v0.6 的 2D 引擎依赖 v0.5 的自举编译器（用于生成引擎字节码）。
