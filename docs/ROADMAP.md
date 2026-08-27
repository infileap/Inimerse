# Infiverse / Inimerse 路线图

> 更新：2026-08-27
>
> 本路线图根据 `docs/愿景.md` 重构，描述可验证的工程交付物，而不是抽象宣传目标。API 以 `docs/API_REFERENCE.md` 为准。

0.3.0 版本待实现内容详见 [ROADMAP_0.3.0.md](ROADMAP_0.3.0.md)。
v0.4–v0.6 分层规划详见 [ROADMAP_0.4-0.6.md](ROADMAP_0.4-0.6.md)。
集合化类型系统延期至 v3.1，详见 [ROADMAP_3.1.md](ROADMAP_3.1.md)。

## 愿景映射

| 愿景层 | 工程主线 | 当前状态 |
| --- | --- | --- |
| 本底宇宙 | Inimerse VM、稳定 ABI、模组与安全沙箱 | 基础能力已完成，进入兼容性治理 |
| 创世神域 | Verse Forge、VDP、世界蓝图与规则配置 | 单机脚本基础已有，打包/分享待完成 |
| 涌现与连接 | UPP、CRP、中继、好友图谱与跨 Verse | 当前最高优先级 |
| AI 居民 | AI 对话、行为边界、训练沙盒 | 已有 Ollama/AI 编程接口，居民系统待实现 |
| 数字主权 | 本我之核、资产溯源、可迁移身份 | 本地身份已完成，链上与跨服资产待研究 |

### 状态口径与升级原则

- **已完成**：代码已合入当前参考实现，并有可重复的本地验收命令或测试覆盖。
- **进行中**：已有可运行的骨架，但协议、错误处理或跨平台行为尚未冻结，不承诺兼容性。
- **待实现**：愿景中已明确、但尚无可交付实现；完成后必须补充测试和文档。
- 路线图只跟踪可交付的协议、工具和 UI，不把宣传性目标当作里程碑。
- 每个阶段完成时必须同时更新 API 参考、迁移说明和验收基线；协议变更优先保持向后兼容。

## 阶段一：本底宇宙与单机参考实现

### 已完成

- [x] 词法、语法、字节码编译器、寄存器 VM、GC 与模组加载器。
- [x] 动态全局表、命名空间、分片锁、原子操作、task/Fiber 调度。
- [x] 实体系统、空间索引、精灵索引、SPI 事件总线。
- [x] `isolate_run` 未签名模组隔离与 per-mod 资源记账。
- [x] 字符串所有权审计、函数帧恢复、selfhost 大文件回归。
- [x] `--lint`、AI 代码检查和基础 Ollama 接口。
- [x] `verse_dist`、`record`、`sync` 等协议基础模块。

### 稳定性治理

- [ ] 发布稳定 ABI v1，并为模组声明接口版本。
- [ ] 建立弃用周期、迁移工具和 LTS 分支策略。
- [x] 将所有固定盘符、调试路径和个人数据从发布构建中移除。
- [ ] 完成 Windows、Linux、WebAssembly 构建矩阵。
- [x] 将 VM 时钟、休眠、目录和可执行文件定位迁移到 `src/platform/` 抽象层。
- [ ] 将 Fiber、线程、锁和进程能力拆分为 POSIX/Windows 后端，并为不可用能力提供明确错误。
- [ ] 将 `net_mod` / `server_mod` 的 TCP、HTTP 和 WebSocket 后端迁移到 `ImSocket`。
- [ ] 将 `net_mod` / `server_mod` 的平台专用注册逻辑改为能力检测并提供 POSIX 实现。
- [x] 在 `desugar_mod` 增加 `unless` 条件语法糖的规范化转换。
- [ ] 将 `isolate_mod` 的输出捕获、超时和资源限制迁移到 `ImProcess` 管道后端。
- [ ] 统一使用 `src/platform/features.h` 的能力声明，禁止模块直接散落平台宏判断。

> 进展：平台时钟/休眠已接入 VM 兼容层；跨平台互斥锁接口已建立，VM 现有
> `CRITICAL_SECTION` 调用仍待逐步替换；原生 Verse 线程和调度器线程的启动、
> join/close 已接入 `src/platform/thread.h`，取消与超时强制终止语义仍待迁移。
> Fiber 调用已接入 `src/platform/fiber.h`（Windows Fiber/POSIX `ucontext`）。
> 主程序自身路径解析已迁移到 `im_platform_executable_path`。
> VM 全局/分片/消息/脚本锁已迁移到 `ImMutex` 平台接口，Windows 构建回归通过。
> `child_proc` 注册表锁和计时已迁移；进程创建/终止后端仍待 POSIX 实现。
> `src/platform/process.h` 已提供跨平台进程 API，并有 `process_probe` 验证程序。
> `child_proc` 已迁移到 `ImProcess`，下一步处理 `isolate_mod` 的输出捕获与超时。
> `server_mod` 已移除固定盘符，路径默认随可执行文件目录推导并支持环境变量覆盖；
> 房间目录创建/删除已脱离 Win32 文件调用，目录枚举仍待平台目录迭代器。

**阶段出口条件**：ABI v1 有版本协商和兼容性测试；至少一个旧版模组可在新运行时加载；三平台构建在干净环境可复现。

## 阶段二：多人在线与跨服务器互联（当前阶段）

### UPP：宇宙进程协议

- [x] 定义宿主、Verse 子进程、客户端之间的握手和能力协商（`tools/upp_reference.js`）。
- [x] 实现心跳、启动、停止、日志、崩溃和版本不兼容消息（`tools/upp_reference.js`）。
- [x] 为每个 Verse 生成 manifest、入口脚本、接口需求和文件 SHA-256 摘要（`node tools/upp_reference.js --generate <dir>`）。

### CRP：宇宙中继协议

- [x] 阶段 2A：实现 `FIND`、`PORTAL`、`SIGNAL` 消息的本地参考协议（`tools/crp_reference.js`）。
- [x] 阶段 2B：实现 HTTP 中继参考节点、Verse 注册/发现/连接/信号和 SHA-256 内容寻址接口（`tools/crp_relay.js`）；WebSocket 待后续接入。
- [x] 阶段 2C：提供客户端指数退避、取消和断线重连基础（`tools/crp_client.js`）；好友图谱和 NAT 穿透待后续接入。
- [x] 阶段 2D：客户端多源下载、包 SHA-256 校验和 HMAC 能力令牌（`CrpClient.fetchContent`、`crp_relay`）。

### 客户端验收

- [ ] 单机、热联机、冷联机三种模式均可从桌面 UI 启动。
- [ ] `verse://hub/<id>` 可发现、下载、校验并启动 Verse。
- [ ] 节点状态、延迟、版本和错误原因在客户端可见。

**阶段出口条件**：UPP/CRP 本地回环通过；签名篡改、版本不兼容和断线重连均有自动化测试；桌面端可从 UI 完成一次端到端 Verse 启动。

## 阶段三：Verse Forge 与宇宙分发

- [ ] Verse 配置模型：拓扑、坐标、时间流速、物理常数和传送协议。
- [ ] Avatar、生态、社会权限、视觉叙事和 NPC 配置面板。
- [ ] 蓝图导入/导出：地形、结构、资源、生态和传送门种子。
- [ ] 生成 `.vverse` 包：`manifest.json`、`laws/`、`blueprint.json`、`assets/`、`mods/`、`signatures/`。
- [ ] 打包签名、哈希、版本兼容检查和只读预览。
- [ ] Hub 清单、上传、下载、分叉与本地缓存。
- [ ] 工作台支持创建、打开、运行、分享和下载 Verse。

**阶段出口条件**：同一 `.vverse` 包在 Windows 与 Linux 解包结果一致；只读预览无需执行脚本；签名、哈希和依赖缺失均能给出可操作错误。

## 阶段四：AI 居民与训练沙盒

- [ ] AI 居民 API：人格、记忆、行为树、日程、对话和声誉。
- [ ] 强制 AI 标识与权限边界，避免将 AI 伪装成人类玩家。
- [ ] AI 资源配额：时间、指令、网络、记忆和模型调用预算。
- [ ] 隔离训练 Verse：快进、回放、评估、重置和迁移。
- [ ] AI 镜像玩家的行为审核、可解释日志和一键停止。
- [ ] 将 `ai_code`、`ai_code_check` 与 Verse Forge 配置流程打通。

**阶段出口条件**：AI 居民始终带有可见标识；资源配额可强制执行；训练 Verse 可暂停、回放、重置并安全迁移，且审计日志可导出。

## 阶段五：经济、资产与数字主权

- [ ] 本我之核：跨 Verse 成就、特异点、声誉和可迁移身份声明。
- [ ] 资产溯源：发行、转移、兑换、版本和冲突记录。
- [ ] `store:server` / `store:both` 的 CAS 冲突解决和审计接口。
- [ ] 研究 `store:chain` 的可插拔账本接口，不绑定单一链或货币。
- [ ] 公开信用证明查询，不提供中心化价值评级。

**阶段出口条件**：身份和资产声明可导出、校验和迁移；冲突解决过程可审计；实现不绑定单一链、货币或中心化评分机构。

## 桌面应用交付线

- [x] Tauri 八模块壳、现代化 UI、相对路径和发布安装包。
- [x] 主页资料编辑、二维码、成就概览和本地身份。
- [x] 工作台编辑、保存、运行、参数面板、新建项目和 AI 建议。
- [x] 引擎发现、多版本选择、更新通道、组件状态、包管理和 Repair 自检基础。
- [x] 插件搜索、安装、启停和移除。
- [ ] GitHub/Bilibili `code → token → profile → oauth_bind` 完整链路。
- [ ] 内置 IDE 语法高亮、运行停止；
- [x] 内置 IDE 查找替换和错误行定位。
- [ ] Verse Forge 桌面入口和 `.vverse` 管理器。

## 多样化 `say` 输出与流路由

目标：让脚本根据场景选择文本的接收对象，而不是把所有文本都写入标准输出。
语法保持 `say` 兼容，输出目标通过可选通道参数或上下文默认值决定。

### 输出目标

- [ ] `say.console(text)`：终端/REPL，保留当前默认行为。
- [ ] `say.log(text, level)`：结构化日志流（debug/info/warn/error），支持时间、模块和 Verse ID。
- [ ] `say.chat(text, channel)`：聊天/群组/AI 对话频道，支持权限和消息回执。
- [ ] `say.ui(text, region)`：桌面 UI 或 Web UI 指定区域，支持富文本和生命周期。
- [ ] `say.world(text, scope)`：Verse 内广播、区域广播、玩家私聊和事件流。
- [ ] `say.character(text, character, audience)`：游戏角色/NPC 对白，携带角色身份、情绪、动作和受众范围。
- [ ] `say.dialogue(character, text, options)`：可分支对白，支持选项、意图、上下文和回执。
- [ ] `say.system(text, severity)`：系统提示、任务提示和不可伪装的安全告警。
- [ ] `say.network(text, peer)`：UPP/CRP 节点消息，默认受能力令牌和速率限制保护。
- [ ] `say.file(text, path)`：显式文件流，使用沙箱路径和轮转策略。
- [ ] `say.json(value, stream)`：机器可读 JSONL，供 IDE、AI 和自动化工具消费。

### AI 分析输出

- [ ] `say.ai(event, context)`：向 AI 分析流发送结构化事件，不默认显示给玩家。
- [ ] `say.ai_observe(scene, actors, state)`：提交场景快照，供 AI 分析角色关系、风险和下一步行动。
- [ ] `say.ai_trace(action, result, cost)`：记录 AI 行为、结果、资源消耗和因果链。
- [ ] `say.ai_feedback(label, reward, reason)`：向训练沙盒提交反馈，支持强化学习评估。
- [ ] AI 输出必须标记 `source=ai`，并携带模型、会话、Verse、时间戳和置信度元数据。
- [ ] AI 分析流与玩家可见流分离；只有经过权限过滤的摘要才能路由到 `say.chat` 或 `say.ui`。
- [ ] 禁止将隐藏思维链直接写入玩家流；仅保留可审计的结论、证据引用和安全事件。

### 统一流模型

- [ ] 定义 `OutputStream` 抽象：目标、格式、优先级、缓冲、背压、取消和错误策略。
- [ ] 支持同步输出与异步事件：`say_start` / `say_chunk` / `say_done` / `say_error`。
- [ ] 流路由表可由宿主、Verse 或客户端覆盖，但安全模式禁止越权目标。
- [ ] 断线时可配置丢弃、缓存或回放；网络流不得阻塞 VM 主线程。
- [ ] 所有输出记录来源（模块、线程、Verse、身份）并可按权限脱敏。

### 语法糖与去糖

- [ ] `say "hello" -> say.console("hello")`（保持旧代码兼容）。
- [ ] `say@chat "hello" -> say.chat("hello", "current")`。
- [ ] `say@character("guard", "请止步", "nearby") -> say.character("请止步", "guard", "nearby")`。
- [ ] `say@ai(event) -> say.ai(event, "current")`。
- [ ] `say@ui("status", text) -> say.ui(text, "status")`。
- [ ] `say@log.warn text -> say.log(text, "warn")`。
- [ ] 所有新语法只在 `desugar_mod` 实现，规范 AST/字节码不增加平台耦合。

### 验收标准

- [ ] 同一脚本可在 CLI、桌面 UI、WebSocket 客户端和 headless 服务端选择不同输出目标。
- [ ] 慢网络客户端不会阻塞 VM；超过配额时产生可观察的 `say_error`。
- [ ] `--safe` 下文件、网络和跨 Verse 输出必须显式声明能力。
- [ ] 输出流测试覆盖编码、顺序、取消、断线重连、背压和敏感信息脱敏。
- [ ] 角色对白测试覆盖身份伪造、受众范围、对白分支、字幕/语音同步和 AI 标识。
- [ ] AI 分析测试覆盖结构化事件、权限隔离、脱敏、可解释摘要和训练反馈回放。

## 验收基线

- 引擎：`contract_test.im`、`ai_syntax_test.im`、`syntax_simple_test.im`、`node_task_state_test.im`、`string_ownership_test.im`。
- selfhost：`compiler.im --dump lexer.im`、`eval.im`、`parser.im`。
- 协议：UPP/CRP 本地回环、签名篡改、断线重连和版本不兼容测试。
- 桌面：`node --check src/ui/app.js`、`cargo check --offline`、`cargo build --release --offline`。

## 下一步优先级

1. UPP 本地参考协议与 Verse manifest。
2. CRP `FIND` / `PORTAL` 回环和签名校验。
3. `.vverse` 打包、预览、下载与启动。
4. GitHub/Bilibili OAuth token 交换和资料绑定。
5. Verse Forge 第一批时空/物理/蓝图面板。
