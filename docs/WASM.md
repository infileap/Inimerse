# WebAssembly 构建边界

Inimerse 的 VM、词法/语法分析器、编译器和纯语言 runtime 可作为 WASM 核心；文件系统、线程、网络、GUI、进程和原生插件由宿主通过 PAL/导入函数提供。

当前仓库已完成能力分层，但尚未提交正式 WASI/浏览器构建产物。实现 WASM 目标时应保持：

运行 `make wasm`（内部调用 `tools/wasm_check.js`）可检查本机是否安装 Emscripten 或 WASI clang，并输出 JSON 状态；工具链缺失时命令以状态码 2 退出，不会伪造构建成功。

- 核心字节码和集合语义不依赖操作系统。
- 缺失能力通过 `has_capability` 返回 `false`，调用时给出可操作错误。
- 不在 WASM 模块中直接使用 Win32、POSIX 文件描述符或动态库 API。
- 使用固定 ABI 导入表传递时间、随机数、文件和网络能力。

验收目标：`wasm32-wasi` 可构建 VM 核心，浏览器宿主可运行 `.im` 脚本并通过 JS 流提供 IO。
