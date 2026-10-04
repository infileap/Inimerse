# Inimerse / Infiverse 需求分析：需求来源、实测现状与差距

- 分析对象：`docs/`（36 份已交付文档）+ `future/infiverse-inim-os-summary.md`（§1–§101，21 503 行）
- 工作区：`/home/sakiko/inimerse`
- 方法：全量读 `future/infiverse-inim-os-summary.md`（分段摘要）+ 全量读 `docs/`；对仓库做**可执行证据核查**（干净构建、全量 CTest、代码规模统计、关键词反证、git 状态）
- 证据等级沿用白皮书 §64.2：E0 概念 / E1 文字设计 / E2 静态样例 / E3 可运行原型 / E4 自动化验证 / E5 多环境验证 / E6 长期运行证据

> **文档收敛说明（2026-10-01，分析之后执行；2026-10 修订引用）**
> `docs/` 已收敛为 `README.md`（索引）+ `STATUS.md`（状态与路线图权威）+ `API.md`（语言与 API 权威）+ `BOARD.md`（多会话协调板）+ 本文件；分析时引用的其余 **39 份**文档（含 `docs/archive/ROADMAP_0.5-0.6.md`、`docs/archive/RELEASE_0.5.0.md`、`docs/archive/SELFHOST_BENCHMARK.md`、`docs/archive/ROADMAP.md`、`docs/archive/API_REFERENCE.md` 等）已移入 **`docs/archive/`**。
> `future/` 同样收敛：保留 `infiverse-inim-os-summary.md`、`愿景.md`、`优化路线pro.md`，其余 **7 份**移入 **`future/archive/`**。
> **本文正文里的 `docs/<name>.md` 引用已全部改写为实际位置**：归档件写成 `docs/archive/<name>.md`；`AI_LAYOUT.md` 是例外，它移到了仓库根。每条引用都用 `test -e` 逐条核对过，并由 `tools/check_doc_paths.py` 作为门禁复核（`tools/check_links.py` **看不见**反引号里的这类引用，因为它必须先剥离行内代码，见 `docs/streams/docs-audit.md` §3.2）。改动只涉及**指向归档件的引用**，未改归档内容，也未改分析当时的结论与数字；已废止声明的清单见 [archive/README.md](archive/README.md)。

> **口径提示：本文用的是分析期词汇，交付口径以 [STATUS.md](STATUS.md) §1 为准。**
> §3 的判定列用「**符合** / **部分** / **缺失** / **虚假**」四个词（定义在该节开头），那是分析当时为回答「文档声称 vs 仓库实测」而立的对照表，**与 [STATUS.md](STATUS.md) §1.1 的交付四标记「已验证 / 部分实现 / 设计未实现 / 已废止」不是同一套**，也不构成交付承诺。两者的近似对应关系是：符合 ≈ 有可执行证据（可支撑「已验证」）、部分 ≈「部分实现」、缺失 ≈「设计未实现」、虚假 ≈「已废止」的**候选**。正式裁定一律以 [STATUS.md](STATUS.md) §3.2 的裁定表为准，本文不自行升级任何条目的状态。
> 本文中出现「已完成」字样处，一律是在**转述被分析文档的原话**（多数是被证伪的声称），不是本文对现状的判定。

---

## 0. 摘要（先看这五条）

1. **存在两条互不相交的"事实线"。** `docs/` + `src/` 描述的是一个**扎实的脚本运行时**（36 836 行 C、531 个 builtin、79 个 CTest 全绿、自举编译器真实存在）；`future/infiverse-inim-os-summary.md` §28–§101 描述的是一个**操作系统 + 分布式内容平台**（Inim OS、Infiverse 内核、CRP 三平面、经济结算、证明后端、裸机路线）。两者之间没有中间层：**没有任何一份"从 A 到 B"的迁移计划，只有目标态**。

2. **`docs/archive/ROADMAP_0.5-0.6.md` 的 v0.6 章节存在系统性虚假"已完成"标记。** 它把 Inim OS Daemon（§1.1）、VFS（§1.2）、统一 PAL（§1.3）、包与服务生命周期（§1.4）、`infiverse.protocol.v1/` RFC（§2.1）全部标注 **已完成**，但仓库内：无 `src/inim_os`、无 `inim` CLI、无 daemon 字样、无 RFC 目录、无 `tilemap`/`scene::Node`/`camera::Camera`/`collision::AABB`（`grep` 命中 0 个文件）。同样地 `docs/archive/RELEASE_0.5.0.md` 声称 AOT "至少快 2x"，被同仓库的 `docs/archive/SELFHOST_BENCHMARK.md` 实测（AOT 打包 1.09x、Wasm MVP 1.51x，原文自述"不满足 ≥2x 目标"）直接否证。

3. **需求实际上是三层，权重完全不同。** (a) **硬承诺层**——`docs/RELEASE_*.md`、`CHANGELOG_0.5.0.md` 的 `[Unreleased]` 段、`API_CATALOG.md`：这些有实现证据，是**当前真实交付边界**；(b) **工程路线图层**——`docs/archive/ROADMAP.md`（206 行，2026-08-27）：唯一有状态口径定义（已完成=有可重复验收命令）和阶段出口条件的路线图，**应作为唯一权威**；(c) **研究愿景层**——白皮书 §28–§101 + `docs/archive/ROADMAP_3.1.md`/`ROADMAP_FRONTIER.md`：按 `docs/README.md` 的约定属"研究性愿景"，**尚不构成交付承诺**。

4. **白皮书自己给出了正确的下一步，而且与我实测的结论一致。** §77 明确写："**不再增加概念数量**，先选「一个最小 Layer、一个临时副本、一个服务器插件、一个客户端模组、一个训练沙盒」，分别验证创建·进入·同步·排空·恢复·撤销·回放"；§64.5 自述现状分栏为 `current_artifacts = inimerse_vm / .im_interpreter / selected_runtime_modules`、`verified_workflows = local_script_execution / basic_gui_or_io_demo`。**白皮书对现状的描述比 `ROADMAP_0.5-0.6.md` 诚实得多**——问题出在中间那层文档，不是愿景本身。

5. **最高优先级不是写更多代码，而是"先把话说准 + 把现状基线钉死"。** 具体三项：修 `ROADMAP_0.5-0.6.md` 与 `RELEASE_0.5.0.md` 的状态口径（违反白皮书 §64.7、§80.8 与 `docs/README.md` conventions）；清理仓库卫生（`build/` 是从 `/home/sakiko/inimerse_stable` 复制来的构建树，直接 `ctest` 100% 失败；`Infiverse_standard/src-tauri/target` 独占 4.0 GB）；给 `http_posix.c` 的 §43.5 经济域迁移在制品收口。

---

## 1. 方法与可执行证据

### 1.1 读了什么

| 范围 | 内容 |
|---|---|
| `docs/` | 36 份文档全量（release 0.2.0/0.2.1/0.4.0/0.4.1/0.5.0、changelog、roadmap 4 份、API_REFERENCE/CATALOG/BUILTIN_TABLE、PORTABILITY、WASM/WASM_ABI、PARAM_FORMAT、SYNTAX_SUGAR、protocol_v1、OAUTH、compile_guide、SELFHOST_BENCHMARK、COLLECTION_PERF_AUDIT、CI/发布教训 3 份、发布 playbook） |
| `future/infiverse-inim-os-summary.md` | §1–§101 全量（分 4 段，含 §39 的 210 项未冻结清单） |
| `future/` 其余 | `优化路线.md`（性能画像与 PAL 平台矩阵）、`ROADMAP_3.1`/`ROADMAP_FRONTIER` 对应的 `集合化.md`/`前沿.md` 大纲 —— 均已移入 `future/archive/`（`优化路线pro.md` 除外，它被保留） |
| 源码 | `src/` 目录结构、`src/mod/` 模块清单、`CMakeLists.txt`、`src/main.c` 模块注册、`selfhost/` |
| 文档外基础设施 | `tools/`（约 60 项）、`Infiverse_standard/`、`mods/`、`examples/`、`universe/`、`packaging/` |

### 1.2 跑了什么（命令与结果，可复现）

```bash
# 1) 干净构建（工作区原有的 build/ 不可用，见 §5.4）
cmake -S . -B build_verify -DCMAKE_BUILD_TYPE=Release     # exit=0
cmake --build build_verify -j$(nproc)                      # exit=0，仅 1 条 -Wformat-truncation
ctest --test-dir build_verify --output-on-failure -j4      # 100% tests passed, 0 failed out of 79, 11.92 sec

# 2) 不在 CTest 内的 Node 套件
node tools/*.test.js    # 11/12 通过；wasm_host.test.js => "skipped (build wasm first)"

# 3) 版本与规模
./inimerse --version        # inimerse 0.4.0   ← 根目录旧二进制
./build/inimerse --version  # inimerse 0.5.0
grep -n PACKAGE_VERSION CMakeLists.txt   # set(INIMERSE_PACKAGE_VERSION "0.5.0")
grep -c add_test CMakeLists.txt          # 79
find src -name '*.c' | xargs wc -l       # 81 个 .c + 41 个 .h，C 代码 36 836 行
grep -rc 'vm_register_builtin' src/      # 531 处注册调用

# 4) 反证"已完成"声明
grep -rl 'tilemap\|scene::Node\|collision::AABB\|camera::Camera' src/ examples/   # 0 个文件
ls src/inim_os src/os 2>/dev/null                                                 # 不存在
find . -name '*.rfc' -o -type d -name 'infiverse.protocol.v1'                     # 无

# 5) 仓库状态
git log --oneline -15    # 最新：290febd M4 fourth block: economy domains...（M1–M4 全在 main）
git status --short       #  M src/platform/http_posix.c（+218 行，§43.5 经济域迁移在制品）+ 5 个未跟踪脚手架文件
```

### 1.3 实测现状基线（这张表是全篇的地基）

| 维度 | 实测值 | 证据等级 |
|---|---|---|
| C 引擎代码量 | 81 个 `.c` + 41 个 `.h`，**36 836 行** | E4 |
| builtin 注册 | `vm_register_builtin*` **531 处**；模块注册 15 个（`src/main.c:35-49`） | E4 |
| 自动化测试 | **79/79 CTest 全过**（干净构建，11.92 s）；标签 27 类 | E4 |
| Node 协议套件 | 11/12 通过（`wasm_host` 需先 `make wasm`） | E4 |
| 自举编译器 | `selfhost/{compiler,parser,lexer,eval,main}.im` 共 2 316 行，`selfhost_benchmark` CTest 存在 | E4 |
| 脚本体量 | 全仓 **321 个 `.im`**（根目录 170 个为回归测试） | E4 |
| 引擎版本 | `CMakeLists.txt` 与 `build/inimerse` = **0.5.0**；根目录 `./inimerse` = **0.4.0（陈旧）** | E4 |
| 桌面端 | `Infiverse_standard/`（Tauri）自述 **"B0 骨架完成"**，最后提交 `f90d355 Release v0.3.0`；`src-tauri/target` 占 **4.0 GB** | E3 |
| 性能事实 | 解释器 88 ms = 1.00x；Wasm MVP 58 ms = **1.51x**；AOT 打包与解释器**等同**（分布式测量中位 **0.98x**，`docs/archive/SELFHOST_BENCHMARK.md`） | E4 |
| 未提交工作 | `src/platform/http_posix.c` +218 行（`ImImportedLedger`、`econ_balances_digest()`，白皮书 §43.5 经济域迁移导入/导出） | E3 |

---

## 2. 需求来源分层：谁在提什么要求

### 2.1 硬承诺层（有实现证据，是当前真实边界）

来源：`docs/archive/RELEASE_0.2.0.md`、`docs/archive/RELEASE_0.2.1.md`、`docs/archive/RELEASE_0.4.0.md`、`docs/archive/RELEASE_0.4.1.md`、`docs/archive/RELEASE_0.5.0.md`、`docs/archive/CHANGELOG_0.5.0.md`、`docs/archive/API_REFERENCE.md`、`docs/archive/API_CATALOG.md`、`docs/archive/API_BUILTIN_TABLE.md`。

**这一层的口径是可信的**，因为它自带验收命令与门禁：

- **`CHANGELOG_0.5.0.md` 的 `[Unreleased]` 段才是真正的当前实现清单**（`[0.5.0] - 2026-09-25` 段基本复述了宣传口径）。已实现：CLI `compile/buildc/run/profile/symbols` + `--abi-version/--abi-target/--aot/--incremental/--force/--reproducible/--debug-info/--symbols`；`.inim` 依赖尾块（主源 + 全部 import 路径 + SHA-256）；`--debug-info` 产 STABS 风格符号 + DWARF 5 行号程序；`tools/bindgen.py`（`.def` → C/C++/Java/Python）、`tools/cpp_scan.py`、`tools/python_scan.py`、`tools/migrate_report.py`、`tools/prof2flame.py`、`tools/perf_compare.py`、`tools/selfhost_bench.py`。
- **POSIX 线已经真实**：`infiverse`/`record`/`verse_dist` 不再是 stub；`.vverse` pack/sign/verify/update + hub registry + HTTP fetch；POSIX 内嵌 hub 真 TCP+UDP 共享端口 + `GET /v/<id>`。
- **CRP M1–M4 已在 `main`**（git log 可查）：M1 确定性回放闭环（JSONL 事件日志 + sha256 链 + `replay_state_hash` + `replay_verify`）；M2 会话收敛（握手/能力协商/租约/序列 accept·duplicate·gap + `replay`/`snapshot` 恢复决策）；M3 包签名跨平台回归（修 POSIX `home_dir()` 只按反斜杠拆分的缺陷，14+ 处路径连接归一）；M4 节点发现（`node_id` = ed25519 公钥、拒绝伪造/过期声明）、会话 authority 与健康降级、节点交接（`/node/handoff` 校验 checkpoint + 事件尾哈希，失败不产生脑裂）、经济域（`currency_id` = 规范化定义哈希、跨域转账显式拒绝、幂等键防重复结算、hash-chained 审计账本、bridge 显式 `not_implemented`）。
- **发布工程已经很成熟**：`tools/release_verify.py` 四类检查（`--require-wasm`、`--require-deb-python3`、Unix 可执行位、Winget 无占位摘要）；集合审计门槛"三模式结果哈希一致 + 峰值 RSS ≤ 100 000 KiB"；自举门槛"运行耗时 ≤ 宿主 1.20 倍、峰值内存 ≤ 1.30 倍"。

**这一层暴露的真实缺口**（不是"未实现"，而是"实现了但没冻住"）：

- `--jit=template|optimized` **始终是解释器安全回退**，文档明确"不得把回退解释器的波动解释成 JIT 加速"。
- 有限集合枚举/TypeSet 已成体系，但 `case try` 的**开放错误域穷尽性证明**仍不完备。
- Eidos **只有外糖可执行子集**（脱糖器明确拒绝 mixin），无可见性/sealed/frozen/invariant/热修改。
- `docs/archive/PARAM_FORMAT.md` 的 `inim bundle resolve/graph/verify/gc` 与 `.param` 清单属**提案，无实现证据**。
- `docs/archive/NUMERIC_MODEL_V04.md` 的数值塔 `Number = Z ∪ Q ∪ D ∪ F`**尚未实现**（当前 `strtod` + IEEE-754 double）。

### 2.2 工程路线图层（`docs/archive/ROADMAP.md`，应作为唯一权威）

这份 206 行的文档是全仓**唯一定义了状态口径**的路线图：

> **已完成** = 代码已合入并有可重复的本地验收命令或测试覆盖；**进行中** = 有骨架但协议/错误处理/跨平台行为未冻结，不承诺兼容性；**待实现** = 愿景已明确但无可交付实现。

其阶段划分与实测的对应关系：

| 阶段 | 内容 | 实测对照 |
|---|---|---|
| 阶段一 本底宇宙与单机参考实现 | 词法/语法/编译器/寄存器 VM/GC/模组加载器、Fiber 调度、实体系统、`isolate_run` 隔离 | **基本吻合**；稳定性治理 8 项未完成（稳定 ABI v1 版本协商、弃用周期、三平台构建矩阵、`net_mod`/`server_mod` 迁 `ImSocket`、`isolate_mod` 迁 `ImProcess`、统一 `features.h` 等） |
| 阶段二 涌现与连接（**当前最高优先级**） | UPP 握手/心跳/manifest；CRP 2A–2D（FIND·PORTAL·SIGNAL、HTTP 中继、指数退避重连、多源下载 + HMAC 令牌） | **协议层 `[x]` 全勾且实测通过**；**客户端验收 0/3 勾**：三模式从桌面 UI 启动、`verse://hub/<id>` 可发现/下载/校验/启动、节点状态在客户端可见 |
| 阶段三 Verse Forge | Verse 配置模型、蓝图导入导出、`.vverse` 生成与签名、Hub 清单、工作台创建/运行/分享 | **全部未勾**；`tools/vverse_pack.js` 已验证器与 pack/unpack/preview 基础 |
| 阶段四 AI 居民 | 居民 API（人格/记忆/行为树/日程/对话/声誉）、AI 标识与权限边界、资源配额、隔离训练 Verse | **全部未勾**；已有 Ollama `ai_ask/ai_vision/ai_code` 接口与 仓库根 `AI_LAYOUT.md` 的视觉排版助手原型 |
| 阶段五 经济/资产/数字主权 | 本我之核、资产溯源、`store:server`/`store:both` CAS 冲突、`store:chain` 可插拔账本 | **全部未勾**；但 M4 已落地经济域原型（币种定义哈希 / 签名发行 / 幂等结算 / 审计链） |

**结论**：`ROADMAP.md` 的诚实度与实测高度一致。它明确写了"当前最高优先级 = 阶段二 UPP/CRP"，且**客户端验收全未勾**——这正是 `ROADMAP_0.5-0.6.md` 声称"已完成"的那些内容的反面。

### 2.3 研究愿景层（白皮书 §28–§101 + ROADMAP_3.1 / ROADMAP_FRONTIER）

`docs/README.md` 的 conventions 定义了这一层的地位：

> `future/` 保存研究性愿景和设计草案，**已进入交付承诺的内容必须同步到对应版本路线图并补充可重复构建/测试命令**。

白皮书 §28–§101 绝大部分**尚未同步到任何版本路线图**，因此按此约定属研究性。它体量极大（约 21 500 行），可归为 8 个需求域：

| 章节 | 需求域 | 关键规范产出 |
|---|---|---|
| §28–§39 | 语言/Eidos/`.inim` 容器/VR·PDP/性能/待验证清单 | §39 的 **210 项"非冻结标准"** |
| §40–§52 | 运行时/虚拟设备/经济/record/VDP/RFC/`Event`/`Operation`/版本检查 | §46.8 **M0–M5**、§52 **P0–P5** 阶段序列 |
| §53 | 目标导向对象、进化池、可验证协作 | `interface CellLife`、13 条不变量、细胞凋亡状态机 |
| §54 | 编译器可信链、跨语言迁移、兼容性证明 | Core IR、7 级兼容、证据链 |
| §55 | CRP 三平面、协议发现、版本演进 | 会话状态机、6 类版本分别协商 |
| §56–§57 | 现实设备（R0–R3）、内容治理与未成年人保护 | 外部效果流程、标签体系 |
| §58–§62 | 规范治理/可访问性/创作者生态/数据模型/资源感知运行时 | `canonical_json/1`、四态区分、能源预算 |
| §63–§68 | 可观测性/能力状态与证据等级/OS 生命周期/许可与 AI 数据/需求追踪/虚拟设备 | **§64 六态+E0–E6**、§67 需求追踪格式、§65 安装/升级/恢复 |
| §69–§77 | 空间契约/证明后端/去中心化协作/规则运行时/版本阶段/架构层级/游戏兼容 | 六层空间、`ProofArtifact`、C0–C6 兼容等级 |
| §78–§101 | **MVP 设计、线协议、存储与验收、Verse Forge、运维、AI 沙盒、CLI、互操作认证、CRP 节点、资产凭证、桌面端、治理、数据语义、容量、存储后端、权威执行、身份、经济结算、SDK/ABI、现实设备、大厅搜索、Inim VFS、测试平台、实施阶段** | 可逐条验收的工程规范 |

**这一层自带三条治理规则，必须用来约束其他文档**（这是全篇最有价值的部分）：

- **§64.7 / §80.8**：「白皮书不得把讨论中方案写成已具备功能」「**无代码、无可验证记录的内容只能标 `designed`**」「禁用『支持所有平台/完全兼容』」——需按六态 + 证据等级**按维度分别标记**（示例：`syntax implemented / compiler experimental / linux_backend specified / wasm_backend planned`）。
- **§67.2–§67.3**：`coverage_status`（covered/partial/principle_only/conflicted/unresolved）与 `implementation_status`、`evidence_level` **三者并列**；`covered ≠ implemented ≠ validated`；升 `covered` 前必须回答八问（对象是什么/谁负责/如何调用/如何失败/如何观察/如何迁移/如何验证/边界是什么）。
- **§77**：下一阶段**不再增加概念数量**，先做最小闭环（一个 Layer、一个临时副本、一个服务器插件、一个客户端模组、一个训练沙盒），验证"创建·进入·同步·排空·恢复·撤销·回放"八件事。

**§78 MVP 已经很克制，可直接作为实施基线**：`inim-client` / `inim-server` 两个逻辑进程；`ServerOrArchive → VerseGroup → Layer → Block → Cell`；14 种事件类型；10 条命令 + 23 个错误码；`state_hash = SHA-256(canonical_json(...))`；提交顺序 10 步（第 6 步 flush 失败**不得**执行 7–10）；CI 11 个门；测试向量 V-001…V-012。

---

## 3. 需求 ↔ 现状差距矩阵

判定口径：**符合** = 有可执行证据；**部分** = 有原型/子集；**缺失** = 无实现；**虚假** = 文档声称已完成但仓库无对应实现。

| # | 需求域 | 权威出处 | 文档声称 | 实测证据 | 判定 |
|---|---|---|---|---|---|
| 1 | 语言前端（词法/语法/AST/语义） | ROADMAP §1、RELEASE_0.5.0 | 已完成 | `src/lexer`、`src/parser`、`src/compiler`；42 个 language 标签 CTest | **符合** |
| 2 | 优化器（循环不变式外提、DCE、常量折叠） | RELEASE_0.5.0 | 已完成 | `src/compilation`；未单独验证各 pass | **部分** |
| 3 | 字节码格式 `infiverse.mv1` + ABI `1.0` | ROADMAP_0.5-0.6 §1 | 已完成 | `bytecode_capture_probe` 等 CTest 通过；`--abi-version` 存在于 CHANGELOG | **部分**（未验证跨版本失败路径） |
| 4 | 三阶段自举 + 可复现构建 | ROADMAP_0.5-0.6 §1 | 阶段 0 完成 | `selfhost/*.im` 2 316 行 + `selfhost_benchmark` CTest + `SELFHOST_BENCHMARK.md`（4 套例哈希） | **符合**（stage2 阈值未固定） |
| 5 | Native ABI（`native` 模块 + 能力沙箱） | ROADMAP_0.5-0.6 §2 | 已完成 | `examples/cpp_native.im`、`interface.def`、`bindgen_regression`/`scan_tools_regression` 通过 | **部分** |
| 6 | 跨语言 Python/Java 绑定 | RELEASE_0.5.0 声称有 `.whl`/`.jar` | 已完成 | 只有 `tools/bindgen.py` + `examples/{python,java}_bridge.im`；**未见 wheel/jar 产物** | **部分/待复核** |
| 7 | Wasm 后端 | ROADMAP_0.5-0.6 §2.3 | MVP 完成，SIMD/GC 未完成 | `wasm_backend_regression`/`wasm_host`/`wasm_probe` CTest 通过；`tools/wasm_backend.test.py` 13 例 | **符合 MVP** |
| 8 | AOT ≥2x 解释器 | RELEASE_0.5.0 | 已完成 | `SELFHOST_BENCHMARK.md` 实测 **1.09x**，原文自述"打包是分发手段，不是优化" | **虚假** |
| 9 | 优化型 JIT | 多处 | 草案 | `--jit=template\|optimized` 安全回退解释器 | **缺失** |
| 10 | 增量/可复现构建 + debug-info | CHANGELOG `[Unreleased]` | 已实现 | `cli_incremental_regression` CTest 通过；`<out>.build.json`/`.dbg`/`.debug_line` 见于 changelog | **符合** |
| 11 | 统一 IDL / bindgen / 迁移报告 | ROADMAP_0.5-0.6 §3.4 | 已完成 | `tools/bindgen.py`（CTest `bindgen_regression`）、`cpp_scan.py` / `python_scan.py`（CTest `scan_tools_regression`）；**`migrate_report.py` 无任何 CTest**（`tools/` 下没有 `migrate_report.test.py`）⇒ 该工具今天**无人回归** | **部分** |
| 12 | UPP 协议（握手/心跳/manifest） | ROADMAP 阶段二 | `[x]` 协议层完成 | `tools/upp_reference.js`、`upp_session.js`，Node 测试全过 | **符合** |
| 13 | CRP 2A–2D | ROADMAP 阶段二 | `[x]` | `crp_reference/relay/client/ws_client` + `crp_session_flow_regression` CTest | **符合** |
| 14 | CRP 客户端 UI 闭环 | ROADMAP 阶段二出口条件 | **0/3 勾** | 桌面端仍是 "B0 骨架" | **缺失** |
| 15 | 确定性回放（M1） | CHANGELOG `[Unreleased]` | 已实现 | `replay_mod.c` + `replay_closure_regression` CTest | **符合** |
| 16 | 会话收敛/租约/authority 迁移（M2/M4） | CHANGELOG `[Unreleased]` | 已实现 | `crp_session_flow_regression`、`reconnect_generation_regression`、`lease_handoff_regression` 通过 | **符合** |
| 17 | 节点发现（签名 + 过期） | CHANGELOG `[Unreleased]` | 已实现 | `node_discovery_regression` 通过 | **符合** |
| 18 | 经济域原型 | CHANGELOG `[Unreleased]` | 已实现 | `economy_domain_regression` 通过；`http_posix.c` 有 +218 行未提交的 §43.5 迁移扩展 | **符合（在制品）** |
| 19 | Inim OS Daemon / Task/Process/Thread / 能力模型 | ROADMAP_0.5-0.6 §1.1 | **已完成** | 无 `src/inim_os`、无 daemon、无 `inim` CLI | **虚假** |
| 20 | Inim OS VFS（`/system`、`/verse/<id>`、`vfile`） | ROADMAP_0.5-0.6 §1.2 | **已完成** | 无对应实现；仅有基础上层文件 API | **虚假** |
| 21 | 统一 PAL（device/window/audio/network） | ROADMAP_0.5-0.6 §1.3 | **已完成** | `src/platform/` 有线程/Fiber/进程/socket/时钟；**无 `device::Keyboard`/`window::Window`/`audio::Audio` 抽象**（GUI/键鼠仍为 Windows 宿主专用） | **虚假** |
| 22 | 包与服务生命周期（`inim package install`） | ROADMAP_0.5-0.6 §1.4 | **已完成** | 无 `inim` CLI；`inim` 包管理器脚本存在（`tools/inim.py`，离线/本地/HTTP registry） | **虚假** |
| 23 | `infiverse.protocol.v1/` 版本化 RFC | ROADMAP_0.5-0.6 §2.1 | **已完成** | 无 RFC 目录、无 `.rfc` 文件 | **虚假** |
| 24 | Verse/Layer/Block/Cell 参考实现 + 非欧几何 | ROADMAP_0.5-0.6 §2.2 | **已完成** | `infiverse_mod.c`（839 行）真实提供 biome/block/portal/entity/law/link/nearby/snapshot/use 共 24 个 builtin（16 worlds）；**无 `layer_open`/`chunk_sleep`/`get_portal_layer`**；非欧几何 `BlockNode{neighbors}` 属白皮书 §69.3 设计 | **部分** |
| 25 | Hub / 多节点服务器 / `inim verse pull·push·verify` | ROADMAP_0.5-0.6 §2.3 | **已完成** | POSIX 内嵌 hub（TCP+UDP）+ `tools/hub_client.js` + `hub_dist_regression` 通过；**无 `inim verse` CLI** | **部分/虚假** |
| 26 | 2D 游戏引擎（scene/sprite/tilemap/collision/camera/anim） | ROADMAP_0.5-0.6 §3 | **已完成**（总结段） | `grep tilemap\|scene::Node\|collision::AABB\|camera::Camera` → **0 个文件**；`gui_mod.c`（3 996 行、119 处 sprite）是 Scratch 风格 GUI | **虚假** |
| 27 | `.vverse` 容器 + Ed25519 签名 + 依赖约束 + 缓存 | ROADMAP 阶段三 | 部分 | `tools/vverse_pack.js`/`vverse_validate.js` + `verse_pack_regression` CTest | **符合（基础）** |
| 28 | 标准库集合/Result/case/闭包 | ROADMAP_0.5-0.6 §4.1 | **已完成** | TypeSet/枚举/Result/`case`/闭包各自有 CTest 与源码位置 | **符合** |
| 29 | 异步 IO / `net::Server` / `async::Await`/`Promise` | ROADMAP_0.5-0.6 §4.2–4.3 | **已完成** | 无 async 运行时；`task`=Fiber、`thread`=OS 线程；`docs/archive/API_REFERENCE.md` 明确 Fiber 调度器在主线程长眠时可能暂停 | **虚假** |
| 30 | AI 居民 / 训练沙盒 | ROADMAP 阶段四 | 全部未勾 | 仅 Ollama `ai_*` 接口 | **缺失** |
| 31 | 本我之核 / 资产溯源 / `store:chain` | ROADMAP 阶段五 | 全部未勾 | 仅 `identity_mod`（3 个 builtin）+ OAuth 绑定 | **缺失** |
| 32 | 多目标 `say` / `OutputStream` | ROADMAP 大段 | 全部未勾 | 已有 `say_target()` 与 11 个目标名 + `say_stream.c/.h`；**背压/取消/`say_error`/流路由表未实现** | **部分** |
| 33 | 集合化类型系统 / BigInt / Decimal | `ROADMAP_3.1.md` | 明确**不属 v0.3–v0.6** | TypeSet/枚举是前身；数值塔未实现 | **按计划延期** |

---

## 4. 需求实测的额外发现（文档没写的）

1. **`build/` 是跨机器复制来的构建树，直接 `ctest` 100% 失败。**
   `build/CMakeCache.txt` 里 `CMAKE_HOME_DIRECTORY:INTERNAL=/home/sakiko/inimerse_stable`，`build/CTestTestfile.cmake` 的 `WORKING_DIRECTORY` 全部指向 `/home/sakiko/inimerse_stable`（含 3 个 sandbox 子目录）。实际跑 `ctest` 得到 `Failed to change working directory to "/home/sakiko/inimerse_stable"`。
   → **任何"CTest 通过"的结论都必须在新建构建目录里复现**，否则是环境假象。

2. **版本号三处不一致。** 根目录 `./inimerse` = `0.4.0`（且 `--version` 会打印 `[TBP] timeBeginPeriod(1)` 等 Windows 遗留噪声；`--help` 被当成脚本路径报 `cannot read script '--help'`）；`build/inimerse` = `0.5.0`；`CMakeLists.txt` = `0.5.0`。根 `README.md` 同时写"当前发布基线 0.5.0"又链到 `docs/archive/RELEASE_0.4.1.md`。
   → 直接违反 `docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md` 自己的第一条教训"版本号必须单一化"。

3. **`Infiverse_standard/src-tauri/target` 独占 4.0 GB**，且该目录最后提交是 `f90d355 Release v0.3.0`——桌面端已落后引擎两个版本。

4. **有未提交的在制品。** `src/platform/http_posix.c` 有 +218 行未提交改动，实现白皮书 **§43.5 经济域迁移**（`ImImportedLedger`、`IM_IMPORTS 16`、`g_imports[]`，注释明确"导入的历史不得伪装成本地已提交"，以及 `econ_balances_digest()` 按账户排序、与顺序无关的余额快照哈希）。这是一处**已经写了但没归档的进度**，且恰好对应白皮书 §95 的经济结算要求。

5. **`docs/archive/SYNTAX_SUGAR.md` 与 `docs/archive/API_CATALOG.md` 对 `?.`/`??` 状态不一致**（前者列为"设计项"，后者标"已实现基础语义"）。

6. **`docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md` 文末"版本间自举产物对比"的 0.5.0 记录仍是空模板**（4 个未勾选项：二进制大小、测试通过率、运行时版本一致性、性能对比）。

7. **白皮书 §64.5 比仓库更保守**：它把 `Linux_host` 列为 `known_gaps`，但仓库实测 POSIX 线相当完整（79 个 CTest 里含 `posix_runtime_parity`、`posix_core_api_runtime` 等多个 POSIX 专属测试）。说明白皮书 §64 写于更早的时点，**它的"现状分栏"也需要刷新**。

---

## 5. 需求优先级建议

### P0 —— 收口与诚实化（不求新功能，1–3 天）

| 编号 | 需求 | 依据 | 验收 |
|---|---|---|---|
| P0-1 | 重写 `docs/archive/ROADMAP_0.5-0.6.md` 的 v0.6 章节状态标记，改为白皮书 §64 六态**按维度**标注 | §64.1、§64.7、§80.8、`docs/README.md` | 每个小节有 `status`/`evidence_level`/`known_gaps`；无"已完成"字样落在无代码条目上 |
| P0-2 | 把 `docs/archive/RELEASE_0.5.0.md` 的 "≥2x" 断言改为引用 `SELFHOST_BENCHMARK.md` 实测（1.09x/1.51x）并标 `planned` | §80.8 | 与 benchmark 文档数值一致 |
| P0-3 | 清除跨机器复制的 `build/`；在 `.gitignore` 确认 `build*/` 与 `Infiverse_standard/src-tauri/target/` 被忽略 | §3 实测 | 干净 clone 后新建 `build_verify` 跑 79/79 |
| P0-4 | 统一版本号：构建/替换根目录 `./inimerse`，修 `README.md` 的"基线 0.5.0 + 链到 RELEASE_0.4.1"矛盾 | `BUILD_RELEASE_LESSONS_0.4.0.md` | `./inimerse --version`、`build/inimerse --version`、`CMakeLists.txt` 三者一致 |
| P0-5 | 为 `http_posix.c` 的 §43.5 经济域迁移补测试并提交（或明确回退） | 实测 `git status` | `economy_domain_regression` 覆盖 import/export + `econ_balances_digest` 顺序无关性 |
| P0-6 | 消除 `docs/archive/SYNTAX_SUGAR.md` ↔ `API_CATALOG.md` 的 `?.`/`??` 口径冲突 | §67.6（两章规则冲突不得靠"以后统一"解决） | 指定权威章节并记录冲突 ID |
| P0-7 | 填写 `BUILD_RELEASE_LESSONS_0.4.0.md` 文末的 0.5.0 自举对比 4 项 | §1 自举门禁 | 四项有数值 |

### P1 —— 白皮书 §77 的最小闭环（本周期的唯一正确工程目标）

**"不再增加概念，先把一个 Layer 跑通。"**

| 编号 | 需求 | 依据 | 验收 |
|---|---|---|---|
| P1-1 | 选定**一个最小 Layer + 一个临时副本**，打通创建·进入·同步·排空·恢复·撤销·回放 | §77 | 8 步各有自动化测试 + 证据工件 |
| P1-2 | 落地 `inim-server` / `inim-client` **进程边界**：客户端只提意图 + `request_id`，不得提交最终位置/资产余额/事件序号 | §78 进程边界 | 结构性不可能（非靠约定） |
| P1-3 | 实现 14 事件类型 + `sequence` 单调 + `request_id` 幂等（重复返回首次结果）+ 超时 → `unknown` 只查询不重发 | §78、§79 | V-001…V-012 向量通过 |
| P1-4 | 实现 `state_hash = SHA-256(canonical_json(...))` 与回放输出 `match/mismatch/incomplete` | §79 | 与 §90 `canonical_json/1` 规则一致（键按 UTF-8 字节序、无负零、拒重复键） |
| P1-5 | 提交顺序 10 步：第 6 步 flush 失败**不得**执行 7–10；第 7 步失败进 `RECOVERY_REQUIRED` 且**不得返回 `committed`** | §80 | 故障注入测试（含 `fault inject server-crash --after transfer.prepared`） |
| P1-6 | 把 `docs/archive/ROADMAP.md` 阶段二的**客户端验收 0/3** 变成可执行项（三模式从桌面 UI 启动） | ROADMAP 阶段二出口条件 | 端到端演示脚本 |
| P1-7 | 建立 §67 格式的需求追踪表（`requirement_id` / `source_reference` / `coverage_status` / `implementation_status` / `evidence_level` / `next_gate`） | §67.1–§67.3 | 本文 §3 的矩阵可直接迁入 |

### P2 —— 与 §78–§101 MVP 对齐（1–3 个月）

- `infiverse` CLI（§84）：8 条原则、9 个退出码（`unknown` **不得**退出 0）、`--json` 契约为 `cli.result/1`。
- 互操作认证（§85）：9 个剖面 + T0–T10 分层 + `conformance.json`。
- 存储后端（§92）：`EventLog` 的 `append(stream_id, expected_sequence, events, durability)` 条件追加；MVP 必需 `atomic_append`/`conditional_append`/`durable_flush`/`snapshot_visibility`/`read_after_write`。
- 权威执行（§93）：Tick T0–T10，**T8（算 hash 并持久化）未完成不得标 `committed`**。
- 测试平台（§100）：故障边界 14 个 + 证据目录 14 项（**导出失败不得标 pass**）+ CI 层 L0–L10。
- 存储目录 `data/verses/<verse_id>/` 分段布局 + 段按连续 sequence 编号 + 单行单事件。

### P3 —— 远期（已明确延期的，不要再投入）

- `ROADMAP_3.1.md` 集合化类型系统（白皮书 §28–§39/§49 与之呼应）：**明确不纳入 v0.3–v0.6**。
- `ROADMAP_FRONTIER.md` 的概率编程 / 可逆事务 / 证明携带代码 / 量子启发叠加：**要求至少经过一个稳定版本再冻结语法**。
- 微内核 / 裸机（`优化路线.md` §2.3 阶段 4–5、白皮书 §65、§74）：白皮书自身写"**没有独立启动·驱动·存储·更新·恢复证据的系统不应称为已完成的独立 Inim OS**"。

---

## 6. 开放问题与需要裁决的口径冲突

白皮书内部存在多处并行体系，**必须先指定权威章节**，否则下游无法实现：

| # | 冲突 | 涉及章节 | 建议裁决 |
|---|---|---|---|
| C-1 | 兼容性等级**三套并存**：7 项（含 `behavioral`）/ 6 项（缺 `behavioral`）/ 5 级（wire·semantic·storage·replay·authority） | §54.1 / §58.5 / §61.8 | 保留 §54.1 七项为主轴，其余降为"轴视图"并显式声明映射 |
| C-2 | 版本协商结果集合不一致：含 `forward` / 无 `forward` / 增 `read_only`+`observe_only` | §55.7 / §73.8 / §76.6 | 以最全集为准，写一张并集表 |
| C-3 | "可运行/兼容"分级两套编号：`parse/load/execute/interact/authoritative/replay/migrate` vs `C0–C6` vs `T0–T10` | §64.4 / §75.8 / §85 | 指定 §64.4 为运行时维度、§85 为互操作剖面，二者**不得互译** |
| C-4 | `MulticellObject` 是否进 Core IM：§34.6 建议"不入" vs §49.9/§53.2 当作可选对象类型 | §34.6 / §49.9 / §53.2 | 定级为 Eidos/Research 模块，不进 Core IM |
| C-5 | 实验扩展的可声明范围：§46.4 要求"可在 manifest 声明但不得污染最小核心"，而 §34/§41 对其做了完整设计 | §34/§41/§46.4 | 以 §46.4 为准，§34/§41 标 `designed` |
| C-6 | 微型运行时开销表述被修正：§68.9 把"绝对零开销"改为"未使用时没有不可接受的常驻成本" | §42 → §68.9 | 以 §68.9 为准 |
| C-7 | 空间模型：§69 明确"在 §3/§20/§41/§51 基础上收敛" | §3/§20/§41/§51/§69 | 以 §69 为准 |
| C-8 | §39 的 **210 项"非冻结标准"** 覆盖了其他章节以确定语气给出的设计（如拓扑是否冻结 `BlockNode + neighbors`、传送门寻址位宽 32/64 vs 内容地址+短码、Layer 生命周期统一状态机） | §39 vs 全篇 | **以 §39"待验证"为准**，凡是 §39 覆盖的条目一律降为 `specified` |
| C-9 | 阶段序列并存：§46.8 M0–M5 与 §52 P0–P5 与 §101 P0–P5（semantic-freeze → single-layer-authority → local-closed-loop → extensibility-sandbox → staging-and-operations → multi-node-interoperability） | §46.8/§52/§101 | 以 §101 为准，另两者标为历史版本 |

**需要人工决策而非技术决策的开放问题**：

- 本次分析的**交付边界**是什么？是"写一份分析"（本文已完成），还是"按 P0 动手修文档"，还是"按 P1 开工做最小 Layer"？
- `Infiverse_standard` 桌面端是继续维护（追引擎 0.5.0）还是冻结在 v0.3.0？
- 白皮书 §28–§101 这 21 500 行是否要正式拆成 `docs/` 下的版本化 RFC（§2.1 要求的 `infiverse.protocol.v1/`），还是保持 `future/` 研究态？

---

## 7. 一句话结论

**这个项目不缺愿景，也不缺扎实的底层——它缺的是一张诚实的"当前在哪"，和一条从"仅 Windows 用户态 VM + 基础 GUI/IO"（白皮书 §64.5 自述）通往"最小可验证 Layer"的路。**
先把 `ROADMAP_0.5-0.6.md` 的虚假"已完成"改掉，把 `build/` 与版本号清理干净，然后严格按白皮书 §77 做那一个 Layer 的八件事闭环——这比再写 200 页设计更有价值。

---

## 附录 A：证据索引（可复现路径）

- 构建与测试：`cmake -S . -B build_verify -DCMAKE_BUILD_TYPE=Release` → `cmake --build build_verify -j` → `ctest --test-dir build_verify --output-on-failure -j4`
- 跨机器构建树证据：`build/CMakeCache.txt`（`CMAKE_HOME_DIRECTORY:INTERNAL=/home/sakiko/inimerse_stable`）、`build/CTestTestfile.cmake`（`WORKING_DIRECTORY`）
- 自举：`selfhost/compiler.im`（22 357 B）、`selfhost/parser.im`（13 248 B）、`docs/archive/SELFHOST_BENCHMARK.md`、CTest `selfhost_benchmark`
- 性能：`docs/archive/SELFHOST_BENCHMARK.md`（解释器 88 ms / Wasm MVP 58 ms / AOT 打包 81 ms）、`docs/archive/COLLECTION_PERF_AUDIT.md`（门槛 100 000 KiB）
- 反证：`grep -rl 'tilemap\|scene::Node\|collision::AABB\|camera::Camera' src/ examples/` → 空；`ls src/inim_os` → 不存在
- 在制品：`git diff src/platform/http_posix.c`（`ImImportedLedger`、`econ_balances_digest()`）
- 模块注册：`src/main.c:35-49`；模块规模 `src/mod/infiverse_mod.c` 839 行 / `verse_dist_mod.c` 2 719 行 / `gui_mod.c` 3 996 行
- 测试名单与标签：`ctest --test-dir build_verify -N`；`grep -c add_test CMakeLists.txt` → 79

## 附录 B：被分析文档的状态裁定

| 文档 | 裁定 |
|---|---|
| `docs/archive/ROADMAP.md` | **权威**（唯一状态口径 + 阶段出口条件） |
| `docs/archive/CHANGELOG_0.5.0.md` 的 `[Unreleased]` | **权威**（当前真实实现清单） |
| `docs/archive/RELEASE_0.2.0.md`–`0.4.1.md`、`docs/archive/V04_STATUS.md`、`docs/archive/API_REFERENCE.md`、`docs/archive/API_CATALOG.md`、`docs/archive/PORTABILITY.md`、`docs/archive/SELFHOST_BENCHMARK.md`、`docs/archive/COLLECTION_PERF_AUDIT.md`、CI/发布教训 3 份、`docs/archive/GITHUB_RELEASE_PLAYBOOK.md` | **可信**（自带验收命令与门禁） |
| `docs/archive/RELEASE_0.5.0.md` | **前瞻/宣传**（"≥2x"被同仓库 benchmark 否证） |
| `docs/archive/ROADMAP_0.5-0.6.md` 的 v0.5 段 | **大体可信** |
| `docs/archive/ROADMAP_0.5-0.6.md` 的 v0.6 段 | **不可信**（系统性虚假"已完成"；且第 163–302 行与 239–302 行是两套重复小节） |
| `docs/archive/PARAM_FORMAT.md` 的 `inim bundle *` | **提案**（无实现证据） |
| `docs/archive/NUMERIC_MODEL_V04.md` | **提案**（数值塔未实现） |
| `docs/archive/protocol_v1.md`、`docs/archive/工作台使用教程.md` | **设计文档**（自带"权威总览以 API_REFERENCE.md 为准"提示） |
| `docs/archive/ROADMAP_3.1.md`、`docs/archive/ROADMAP_FRONTIER.md` | **明确延期**（不属 v0.3–v0.6） |
| `future/infiverse-inim-os-summary.md` §28–§101 | **研究愿景**（按 §80.8 未实现部分只能标 `designed`） |
| `future/优化路线pro.md`/`愿景.md`（保留）、`future/archive/` 下的 `优化路线.md`/`集合化.md`/`前沿.md`/`面对对象.md`/`函数式和错误处理.md`/`Inim OS总纲.md`/`Inim OS特性.md` | **研究愿景**（约定见 `docs/README.md` 与 `future/README.md`） |
