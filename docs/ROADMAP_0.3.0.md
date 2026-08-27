# Infiverse 0.3.0 待实现内容

> 目标：在 0.2.0 的 UPP/CRP 基础上，完成可用的多人连接、Verse 分发和桌面工作流。
> 本文只列工程上可验收的内容；Eidos、Inim OS 和高级 JIT 仍属于长期规划。

## P0：协议与稳定性

> WSL2/Ubuntu 已纳入验收环境。2026-08-27 首次 `make` 检查发现 `src/main.c` 仍含 Windows 编码转换、路径、临时目录、DPI 和控制台 API；需先完成源码 UTF-8 统一，再逐项迁移到 PAL。

- [x] 发布稳定 ABI v1：manifest 支持 `abi` 声明并在宿主协商时拒绝不兼容版本。
- [x] 建立 ABI 兼容性测试；旧版（缺省 `abi`，按 v1 处理）模组保持兼容。
- [x] 支持 manifest `abiRange: "N..M"` 范围协商，并在 welcome 返回选定 ABI。
- [ ] 将 `net_mod` / `server_mod` 的 TCP、HTTP、WebSocket 后端统一迁移到 PAL。
- [ ] 将 runtime 的 HTTP、串口、键鼠和系统调用内置函数迁移到 PAL（当前 Linux 使用明确的空实现基线）。
- [x] POSIX 核心提供文件读写、输入、单调时钟和休眠基础函数。
- [x] POSIX 核心提供目录创建、目录枚举和平台能力查询函数。
- [x] 增加 `capabilities` CLI 与 CI 冒烟检查，平台缺失能力可审计。
- [x] Linux 核心 VM 可独立链接并运行（`make`、`inimerse --version`、`inimerse where`）；平台专用模块暂以能力错误桩隔离。
- [ ] 为 Fiber、线程、锁和进程补齐 POSIX 后端及不可用能力错误码。
- [ ] 完成 Windows、Linux、WebAssembly 构建矩阵。
- [ ] WASM 核心目标：先完成 `wasm32-wasi` 编译，再接入浏览器宿主导入表（边界说明见 `docs/WASM.md`）。
- [ ] 为 `isolate_mod` 增加跨平台输出捕获、超时和资源限制。

## P1：UPP/CRP 多人连接

- [ ] UPP 主机、Verse、客户端状态机：启动、心跳、停止、崩溃和版本不兼容可恢复。
- [ ] CRP WebSocket 中继，支持连接认证、心跳和断线重连。
- [ ] 好友图谱发现、无公网 IP 中继和会话恢复。
- [ ] 内容寻址包的权限令牌、过期和撤销接口。
- [ ] 自动化覆盖签名篡改、版本不兼容、超时、断线和重复消息。

## P1：Verse 分发与工作台

- [ ] `.vverse` 标准包：`manifest.json`、`laws/`、`blueprint.json`、`assets/`、`mods/`、`signatures/`。
- [x] `.vverse` 包结构校验与文件 SHA-256 摘要（`tools/vverse_validate.js`）。
- [ ] 打包签名、哈希校验、依赖检查和只读预览。
- [ ] Hub 清单上传、下载、分叉与本地缓存。
- [ ] 桌面工作台支持创建、打开、运行、分享、下载和启动 Verse。
- [ ] 对 `verse://hub/<id>` 完成发现、下载、校验和启动闭环。

## P2：桌面 IDE

- [ ] Inimerse 语法高亮和行号显示。
- [ ] 运行中的任务支持停止，错误信息可定位到文件与行列。
- [ ] 参数面板、输出面板和诊断信息统一状态模型。
- [ ] 插件/引擎安装失败时提供可恢复提示和 Repair 操作。

## P2：多目标 say

- [ ] `say.console/log/chat/ui/world/character/dialogue/system/file/json` 基础 API。
- [ ] `OutputStream` 抽象：格式、优先级、背压、取消和错误策略。
- [ ] `say@chat`、`say@character`、`say@ai` 等语法糖全部在 `desugar_mod` 去糖。
- [ ] 角色对白身份、受众、情绪和字幕/语音同步元数据。
- [ ] AI 分析流与玩家可见流隔离，并禁止隐藏思维链进入玩家流。

## 0.3.0 验收标准

### 本轮新增

- UPP 会话状态机：启动、心跳序号、停止、崩溃和 ABI 不兼容状态均可恢复/审计（`tools/upp_session.js`）。
- 多目标 `say`/`OutputStream` 参考实现与 `say@target` 去糖映射（`tools/say_reference.js`）。
- WASI 工具链实际产物探针：`make wasm` 编译并验证 `wasm32-wasi` 模块（完整 VM/WASI 导入表仍待后续）。
- CI 构建矩阵新增独立 WASI 探针任务，native 与 `wasm32-wasi` 均有自动验收。
- `make check` 纳入平台、Fiber、进程超时和 TCP 回环探针，跨平台 PAL 回归自动执行。
- Linux CI 已改为执行完整 `make check`，native 构建与全部 PAL 探针统一门禁。
- CRP Hub 清单支持关键词过滤，客户端缓存提供查询、统计、清理和容量上限行为。

### 已完成增量（2026-08-27）

- `.vverse` 目录结构与 manifest/blueprint 必填字段校验。
- 递归 SHA-256 摘要生成；可选 `signatures/sha256.json` 完整性校验。
- CLI 支持 `--require-signature` 与 `--require-complete-signature`，安装前可拒绝篡改或未签名文件。

1. Windows 与 Linux 均可完成干净构建，平台能力缺失时返回明确错误。
2. 本地 UPP/CRP 回环、WebSocket 连接、断线重连和签名校验测试全部通过。
3. 桌面端可从 UI 下载并启动一个已签名 `.vverse` 包。
4. 工作台可编辑、查找替换、运行、停止并定位错误行。
5. `say` 至少支持终端、结构化日志、UI 和 AI 分析四类输出目标。
