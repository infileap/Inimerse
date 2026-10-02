# 作业单：`agent-bridge` —— 多智能体协调桥接层的复核、更新与接流程

**状态**：进行中（**第一轮：只出设计，不写代码**；**第二轮：三条决断 + 一条非决断，见 §6**）
**冲突域（别人别碰）**：`future/multi-agent-coordination-bridge.md`、`future/README.md`、`docs/streams/agent-bridge.md`、`docs/BOARD.md`（**仅 §5 一行**）、`docs/STATUS.md`（**仅 §9 新增一节**）
**禁碰**：`src/**`、`tools/**`、`CMakeLists.txt`、`.gitattributes`、`future/archive/**`、`docs/BOARD.md` §1–§4

---

## 0. 这条流在补什么

`future/multi-agent-coordination-bridge.md`（624 行，设计草案）**已经写好了，质量很高**，但按本仓库的流程它此前**等于不存在**：

- 它由 `88a2109`（subject：`docs: fix the red doc-paths gate I introduced on the board row`）提交进 `main`，而**那条 commit 的正文一个字都没提这 624 行**；`git log --all -- future/multi-agent-coordination-bridge.md` 只有这一条。
- 除自身外全仓库零引用：`grep -c "multi-agent-coordination-bridge" future/README.md` → `0`，`grep -rn "multi-agent-coordination-bridge" --include=*.md .` 排除自身后**零输出**，BOARD / STATUS / 作业单都没有它。
- `future/README.md:11` 的标题写「保留的 **3** 份指导文件」，而 `future/` 下有 **4** 份内容文件。

所以本轮不是「新开一个课题」，是**把它补成一条真实流**：数字重跑、§7.7 改写、登记进 `future/README.md` 与 `docs/STATUS.md`、开流 + 作业单 + BOARD §5 一行。**零代码改动。**

---

## 1. 开工前四个问题的答案（动笔前必须先答，答不出来就别写）

### Q1 「智能体」在这套栈里是什么？

三个候选，权限模型完全不同，必须先摆事实（全部本机复核）：

| 候选 | 它今天是什么 | 它不是 |
|---|---|---|
| **CRP peer** | `(verse, peer)` 是**唯一出现在「被认证的产物」里的身份** —— 能力令牌的 scope 就是这一对 | 令牌**不证明「你是谁」**：它证明持有者知道 hub 级共享 secret（§7.7），而 `peer` 字符串由调用方自选 |
| **CAP_AI 持有者** | `CAP_AI` 是**mod 级能力位**，由 mod 自己在 `spi_meta(id, version, caps)` 里声明（`src/runtime/runtime.c:1275` 的 `spi_parse_caps()` 解析 `"…,ai,…"`） | **进程内自声明，线上不可验证**；`say.ai`（`src/mod/say_mod_posix.c:82`）同理 |
| **Layer 作者** | `vl_layer_put(l, key, actor, role, cell, value, steps)` 里的 `actor`/`role` | **调用方提供的自由字符串，Layer 不校验任何一方**（`src/verse/layer.h:50-53`） |

最近的一条既有先例是 `ai_boundary`（`src/mod/say_stream.c:70` 与 `:98`）：`target == "ai"` 的帧必须自带 `"source":"ai"`，否则拒收，错误码 `ai_boundary`。它说明本仓库**已经有「AI 必须自我标识」的取向**（对应 [STATUS.md](../STATUS.md) §4 阶段四的「强制 AI 标识与权限边界」），但**判定方式是子串匹配 meta 字符串，不是凭据校验** —— 它防的是"冒充 AI 的帧"，不是"AI 冒充人类"的可验证证明。

**答案（结论 + 它强加的规则）**：今天**没有任何单一概念是「可验证的智能体身份」**。「智能体」只能被定义为**一个复合体**：*一个持有作用域为 `(verse, peer)` 的 `signal` 令牌、跑在 UPP 监督的宿主里、并以该 peer 名写 Layer 记录的 CRP peer*。其中只有**令牌 scope** 是被认证的，其余三层都是自报。

这条答案直接强加一条设计规则（本轮已写进文档 §7.9）：

> **桥接层不得采信调用方提供的 `actor`/`role`；承诺的 debtor 必须由令牌 scope 派生。** 而「谁是这个 peer」今天没有答案 —— 要不要把 peer 名变成真实主体，是一次需要先改参考实现的决断（见 Q3）。

### Q2 「协调」的原语是什么？我同意文档的答案吗？

文档的答案是「G²CP 的 performative 叠在 CRP 的 `PORTAL` / `SIGNAL` 之上，`UPDATE` = `PORTAL` + `SIGNAL` 两步」。**我同意它的形状，不同意它可以被当成一条已经不成立的映射**。三点修正：

1. **更准确的一句概括**：协调原语 = **「一条带 performative 的 `SIGNAL` 帧」+「一个承诺 cell 的数值迁移」+「一个幂等键」**，三者都在既有机制里；`PORTAL` 只在这条链需要**写入权**时才出现。把它说成「performative 叠在 PORTAL/SIGNAL 上」容易让人以为 `PORTAL` 表达了具体写权限 —— 它**不表达**：`DEFAULT_CAPS[]` 只有 `"signal"`（`src/verse/crp.c:355`/`:363`），令牌的 `capabilities` 里也只有 `["signal"]`。
2. **`UPDATE` = `PORTAL` + `SIGNAL` 今天只是形式上的**（§7.7）：`PORTAL` 的签发只做**调用方认证**，没有 per-`(verse, peer)` 授权。所以在补上 per-pair 授权之前，这条映射**不成立**，不能写成"已具备"。
3. **`SIGNAL.event` 是自由字符串，引擎不校验任何 performative**（`crp_type_is_valid()` 只校验帧类型，`src/verse/crp.c:491`）—— 这是既有的 §7.3 `unbounded_scope`，本轮不改。

### REJECT 怎么补？（文档说它「在 CRP 里没有对应物」，我同意，且补法有四条硬约束）

错误响应**不进事件日志**，所以拒绝要可审计只能**显式发一条 `SIGNAL`**。四条底线：

1. **不新增帧类型**（§6 反模式 5）—— 三个帧够用，`event` 已经是自由字符串。`REJECT` 是一条 `SIGNAL` 的 `event`。
2. **必须把「传输层失败」与「语义拒绝」分开**：`403 invalid capability token` / `409 session sequence out of order` / `404 verse not found` 是**传输层结果**，它们**不进** Layer；这不是缺口，是**必须写进文档的边界**（否则运维会以为拒绝已经被审计）。
3. **审计侧只能记录发送方声称的拒绝**：文档 §3.2 的 actor/role 方案里，`CANCELLED`（`value=4`）由 debtor 或 creditor 写，**而 Layer 不校验 actor** —— 所以「拒绝被记录」≠「拒绝真的发生过」。这与 §3.4 「`commit.head` 给的是抗改写，不是真实性」是同一条边界，必须复用同一句话。
4. **同一个 cell 的两种语义必须可区分**：建议**不复用** `commitment:<id>` 的 `value=4` 同时表达"被拒绝"与"被撤销"。这是阶段 3 动手前要定的一处（本轮只登记，不改编码）。

### Q3 谁是裁判？

**JS 参考实现是判据方，引擎是被告**（`crp-portal-auth` 立的规矩：`40f6094` 走的就是「先改 `tools/crp_relay.js` 的 `enrollProof()`，引擎跟随」）。本轮的推论有三条：

1. **本轮零代码改动，所以不产生 crosscheck 分歧** —— 不碰 `src/**`、不碰 `tools/**`。
2. **阶段 1（SLIP 词法门禁）的判据方今天还不存在**，必须先造：先写一个**独立的离线校验器**（`tools/` 下的 Python/Node），让它**自己**成为将来「引擎侧 SLIP 校验」的判据方。顺序是「先有校验器 + 语料，再有引擎实现」，不能反过来。
3. **任何「引擎应该怎样」的先决条件都是一次参考实现改动** —— 包括 Q1 的「peer 名要不要变成真实主体」与 Q2 的 per-pair 授权。这两条都属于**先决断、后动引擎**，本轮不决断。

### Q4 冲突域

见本文件抬头的「冲突域 / 禁碰」。本轮只写 5 个文件，其中 BOARD 只加 §5 一行、STATUS 只加 §9 一节。`src/common/**` 硬禁碰；`CMakeLists.txt` 是热点（本波由 `xlang-bridge` 独占），**本流不需要它**。

---

## 2. 本轮交付（三件，全部只动文档）

### 2.1 §9 的证据数字重跑到当前 `main`（`e6e4936`）

| 位置 | 文档旧值 | 实测新值（2026-10-02，`e6e4936`） |
|---|---|---|
| `ctest -R "crp\|upp\|json_min\|verse_"` | `0 tests failed out of 15` | **`out of 16`** |
| `./build/verse_crp_probe` | `crp_probe: 173 checks` | **`185 checks, 0 failures`** |
| `node tools/crp_engine_crosscheck.js` | `107 records` | **`115 records, text-identical`** |
| `crp_closed_loop.test.py` | `crp_peer: 27` / `hub: …exchanges:26` / `crp_closed_loop: 17` | **`31 exchanges` / `exchanges:30` / `19 checks`** |
| `tools/gate.sh` 的 `EXP_CTEST` | `93` | **`95`**（`grep -c "add_test(" CMakeLists.txt` 同为 **95**）。本轮复核时 `EXP_CTEST` 在第 **43** 行；`xlang-bridge` 合入后它是第 **49** 行、值为 **97** |
| 事件环淘汰 | `crp.c:1102-1105` | **`crp.c:1176-1182`**（`memmove` 在 `:1179`，`nevents++` 在 `:1191`） |
| 令牌检查的字面量 `"signal"` | `crp.c:1082` / `:1146` | **`crp.c:1155-1156` / `:1220`**（`1082`/`1146` 今天都是注释） |
| §6 第 6 条的交叉校验语料 | `107 条` | **`115 条`** |

**未变、不需要改的**：`upp crosscheck: ok (111 ops, …)`、`crp.c:697`/`:703`/`:491`、`crp.h:13`/`:43-46`/`:52`/`:59`、`layer.h:49`/`:50-53`/`:64-66`、`grep -rn "commitment" src/` 仍然零命中（rc=1）、阶段 3 的「`verse_closed_loop` 今天 67 项」（实测 `67 checks, 0 failures`）。

**原 §9.1 的第二条前提被今天的行为推翻**：它写「`{"op":"portal", …}` → `200` 且**未做任何认证**就返回令牌」。实测现在是 `403 invalid enrollment proof`；未注册的 verse / 缺 `peer` → `404 verse not found`。已按实测重写（并保留"这句话今天会失败"的提示，防止有人照旧版引用）。

### 2.2 §7.7 改写（前提今天变了一半 → 拆成「认证」与「授权」两句）

新 §7.7 的两句是：**(a) 认证** —— `/portal` 现在要求 enrollment 证明 `base64url(HMAC-SHA256(CRP_ENROLL_SECRET, String(verse) + "\0" + String(peer)))`，判定点**只有一处** `crp_enroll_check()`（`src/verse/crp.c:1057`），`crp_registry_portal` 与 HTTP 监听器**共用它**，未配 secret 时 **fail-closed**（`:1077`），证明错时 `:1079`；**(b) 授权** —— 它**不是**针对特定 `(verse, peer)` 的授权：证明的唯一输入就是这两个字符串（`tools/crp_relay.js:29`），没有第三个因子，所以知道 hub 级共享 secret 的人**仍可为任意 `(verse, peer)` 现算证明并取得令牌**。

**这个区分是承重的**：§2.2 的 `UPDATE` 论证直接依赖它，所以 §2.2 的 ⚠️ 段落与判定标注表同步改了，不能只改 §7.7 留一处矛盾。另外新增 **§7.9**（Q1 的结论：智能体身份没有已验证载体，债务类型 `missing_owner`）。

### 2.3 接进流程

1. `future/README.md`：标题 `3 份` → **`4 份`**，表格加一行（本文件 + 一句性质说明 + 状态 `设计未实现`/E1）。
2. `docs/STATUS.md`：新增 **§9.3 多智能体协调桥接层（`设计未实现`）**，含路线图挂靠点（§4 阶段四「强制 AI 标识与权限边界」）与**可重复的验收命令** —— 这是 [future/README.md](../../future/README.md) 使用约定第 3 条的硬要求。
3. `docs/BOARD.md` §5 加一行（`agent-bridge`，状态 `进行中`，交付后改 `待验收`）。
4. 本作业单。

---

## 3. 判据（验收条件）

1. **门禁全绿**：`tools/gate.sh`（**全量**，七阶段）打印 `gate: OK — every stage passed.` 且 exit 0。本轮不新增 CTest、不改 `CMakeLists.txt`，故 `EXP_CTEST` **必须与 main 一致**（本轮复核时 main 是 95；`xlang-bridge` 合入后 main 是 **97**，故合并后读作 97）。
2. **新增文档先 `git add`**：两个检查器只枚举 `git ls-files`，否则会跳过新文档却照样打印 `0 broken`。
3. **每条数字可在本 worktree 复现**：§5 的命令可原样跑出本节表格里的值。
4. **本轮 `git diff --stat` 只含文档**：`src/**`、`tools/**`、`CMakeLists.txt` 零改动（可用 `git diff --name-only main...HEAD` 复核）。
5. **旧数字只出现在注解与历史记录里**：`1102-1105`、`EXP_CTEST:-93`、`107 records`、`crp_probe: 173`、`27 exchanges`、`exchanges:26`、`0 tests failed out of 15`、`107 条`、`93/93` 不得再被本设计**当作事实引用**。允许的落点只有三类：**(a)** 本文件 §2.1 的旧值→新值对照表；**(b)** 设计文档 §9.2 的「行号漂移提示」与 §9.1 的「那句话今天会失败」注解（这两处是**故意**保留旧数字的反例说明）；**(c)** `docs/STATUS.md` 与 `docs/BOARD.md` 里**别的流的既有历史记录**（如 json-min 时代的「语料 101 → 107 条」「ctest 93/93」）—— 那些不属于本流，**不得顺手改**。复核命令：
   ```bash
   grep -n "1102-1105\|EXP_CTEST:-93\|107 records\|crp_probe: 173\|27 exchanges\|exchanges:26\|0 tests failed out of 15\|107 条\|93/93" \
     future/multi-agent-coordination-bridge.md docs/streams/agent-bridge.md
   # 预期：设计文档 1 处（§9.2 漂移提示）+ 本文件 §2.1 对照表 6 处 + 本条判据自身的 2 行；
   # 两处正文（设计文档 §0–§8 与 §9.1 实测块）零命中
   ```
6. **`git diff --numstat e6e4936..HEAD` 与 §4 的「没做什么」一致**：`src/**`、`tools/**`、`CMakeLists.txt`、`.gitattributes` 全部零行改动。

## 4. 边界与没做什么

- **没有实现任何东西**：SLIP 词法门禁、承诺落 Layer、UPP↔承诺绑定、持久协调日志，全部仍是 `planned` / `unresolved`。
- **没有改参考实现，也没有改引擎**：本轮不产生 crosscheck 分歧，也没有推进 Q3 的三条先决条件。
- **§7.1（64 事件环不落盘）仍是本设计最大的 `unresolved`**，本轮只更新了行号，没有给答案。
- **§7 其余各条（7.2–7.6、7.8）未动**；`SIGNAL.event` 的自由字符串问题（§7.3）仍未决。
- **§9.3 的第三方原始字节没有进仓库**（两份不同许可证的第三方材料，且已跟踪的第三方 README 会永久污染 `links` 阶段）。
- **留给下一轮**：~~Q1 的 peer 身份决断、Q2 的 per-pair 授权、REJECT 的 cell 语义（§1 REJECT 第 4 条）。~~ → **已在第二轮给出决断，见下方 §6。**

## 6. 第二轮（`agent-bridge-round2`，2026-10-02，base `5f23e95`）—— 三条决断 + 一条非决断

**状态**：**只出决断，引擎侧零改动**（`grep -rniE "\bagent" src/` 与 `grep -rni "commitment" src/` 本轮复测均为**零命中**）。
**写域**：仅 `future/multi-agent-coordination-bridge.md`（新增 §7.10）与 `docs/streams/agent-bridge.md`（本 §6）。
**禁碰本轮不变**：`src/**`、`tools/**`、`CMakeLists.txt`、`docs/BOARD.md`、`docs/STATUS.md`。

| # | 问题 | 决断 | 一句话理由 | 落点 |
|---|---|---|---|---|
| ① | peer 身份是否升级为真实主体 | **不升级**。`(verse, peer)` 保持 scope 名，不成为 principal；桥接层只认「债务 `debtor` 必须由令牌 scope 派生」，**禁止采信调用方自报的 `actor`/`role`** | 升级需要同时改判据方载体 + 给 `CMakeLists.txt:96-115` 三个 hub 目标补链 `src/common/ed25519.c` —— 那是新机制，且本轮不写代码；今天 scope 名已足够当稳定记账键 | 设计文档 §7.10.1 |
| ② | per-`(verse, peer)` 授权缺口 | **收，最小形态 = 把 `/portal` 证明输入从「两个自选字符串」扩为「hub 持有的 per-pair 注册项」**（per-pair key + per-pair `capabilities`），**未登记 pair 与证明错必须不可区分（都 403）**，否则 404 变成枚举 oracle | 是在既有机制内部加一个因子，不引入账号体系、不新增帧、不改令牌格式；但必须判据方与引擎**同一次改动落地** ⇒ 本轮不动 | 设计文档 §7.10.2 |
| ③ | REJECT 的 cell 语义 | **不复用 `commitment:<id>` 的 `value=4`**（4 已被 §3.2 定义为 `CANCELLED` = 自愿撤销）。REJECT 落**独立 cell `reject:<id>`**，语义 = *发送方声称接收方违约*（与 G²CP `C(sender, receiver, violated(op, constraint))` 同构，**debtor = 指控者**） | 撤销与拒绝责任方不同，塞进同一个值会让二者不可区分；且 cell 记的是**声称**，「被记录 ≠ 发生过」必须随定义出现 | 设计文档 §7.10.3 |
| §7.1 | 64 事件环不落盘 | **显式非决断**（不是留白）：不决断「协调事件要不要进持久审计」 | 缺的是连接件不是概念（平台层 `im_crp_session_resume_plan` 已算出 `needs_snapshot`，只是没接到 CRP hub 的 `/session/resume`）；它与 ② 是同类改动应合并进一条新流；且今天协调事件**根本不存在**，损失为零 —— 该论证随阶段 2/3 实现到期 | 设计文档 §7.10.4 |

**与「错误响应不进事件日志」的共存方式（③ 的关键）**：**不试图把 HTTP 拒绝搬进 Layer**。协议层拒绝（`tools/crp_relay.js` 的 400/403/404/409）继续只存在于线路上、**不进审计**（判据方原文事实，桥接层不推翻）；要可审计的拒绝必须由某一方**显式发一条 `SIGNAL`** 主动表达。**两条路径永不混为一谈。**

**第二轮没做**：未改 `src/**` 任何一行；未改判据方 `tools/crp_relay.js`（`tools/crp_engine_crosscheck.js` 仍 `115 records, text-identical`）；未开新流（② 与 §7.1 的落地需要新流、含 `CMakeLists.txt` 与判据方改动，按规矩先报协调者）；未把 `future/` 任何内容表述为已实现。

## 5. 复现命令（本 worktree 内原样可跑）

```bash
cd .worktrees/agent-bridge
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

ctest --test-dir build -R "crp|upp|json_min|verse_" --output-on-failure   # out of 16
./build/verse_crp_probe                                                    # 185 checks
node tools/crp_engine_crosscheck.js ./build/verse_crp_probe                 # 115 records
node tools/upp_engine_crosscheck.js ./build/verse_upp_probe                 # 111 ops
INIM_CRP_HUB_BIN=./build/crp-hub python3 tools/crp_closed_loop.test.py ./build/crp-peer
./build/verse_layer_probe                                                  # all checks passed
./build/verse_protocol_probe                                               # 49 checks
PATH="$PWD/build:$PATH" python3 tools/verse_closed_loop.test.py             # 67 checks

sed -n '49p' tools/gate.sh ; grep -c "add_test(" CMakeLists.txt             # 97 / 97
grep -n "CrpEvent\|CRP_EVENT_WINDOW" src/verse/crp.c src/verse/crp.h
grep -rn "commitment" src/ --include=*.c --include=*.h -i                  # 无输出，rc=1
```

`tools/gate.sh --fast` 会**跳过 configure 但仍会 build**，只能跟在已有 `build/` 的目录里用。
