# 多智能体协调桥接层设计：Slipstream / G²CP ↔ CRP · Verse Layer · UPP

> **性质：设计草案（design only）。** 本文件位于 `future/`，按 [`future/README.md:25`](README.md) 与白皮书 §64.7 的约定，**不得**把本文件中的任何方案表述为已实现功能。
> **口径**：每条结论按白皮书 §67.2 标注三个互相独立的维度 —— `coverage`（covered / partial / principle_only / conflicted / unresolved）、`implementation`（implemented / partial / specified / planned / retired）、`evidence`（E0–E6，见 §64.2）。`covered ≠ implemented ≠ validated`。
> **范围**：只出设计。**本文件不包含任何引擎 C 代码改动**；§8 给的是逐阶段验收门，动手前先过评审。
> **权威**：实现状态以 [`docs/STATUS.md`](../docs/STATUS.md) 为唯一权威；语言与 API 事实以 [`docs/API.md`](../docs/API.md) 为准。
> **本轮修订（`agent-bridge` 流，2026-10-02，main `e6e4936`）**：§9 的实测数字整体重跑到当前 `main`（此前几乎每个数字都过期）；§7.7 改写（前提今天变了一半 —— 理由拆成「认证」与「授权」两句），§2.2 的对应缺口同步；本文件登记进 [`future/README.md`](README.md)「保留的 4 份指导文件」与 [`docs/STATUS.md`](../docs/STATUS.md) §9.3。**本轮只改文档，不含任何代码改动**；作业单见 [`docs/streams/agent-bridge.md`](../docs/streams/agent-bridge.md)。

---

## 0. 结论摘要

1. **CRP 的帧类型与 G²CP 的 performative 不在同一层，逐条一一对应是错的。** FIND / PORTAL / SIGNAL 是*路由 + 权威平面*（§55.1 的控制面/数据面划分），performative 是*言语行为信封*。正确的结合点是把 performative 放进 `SIGNAL` 的 `event` 字段，把权威交给 `PORTAL` 签发的能力令牌。→ §2.3
2. **G²CP 没有线上格式，这正是 Slipstream 要补的位置。** G²CP 的 `G2CPMessage.serialize()` 是给人读的多行文本，而且**有损**：边集被折叠成 `EDGES: [N edges]`、路径被 `self.result.paths[:3]` 截断。因此 `parse(serialize(m)) != m`，与 Slipstream MUST 规则 9 的 `parse(format(msg)) == msg` 直接冲突。**两者是互补的，不是竞争的：G²CP 给语义，SLIP 给词法，CRP 给权威，Layer 给历史。** → §1、§4
3. **Slipstream 的 MUST NOT #2 不是外来约束**，它和引擎既有不变式同向：CRP 帧里本来就没有自然语言，Layer 的 cell 值本来就只能是整数。SLIP 做的是把这条既有方向写成**可校验的词法**。→ §4.5
4. **社会承诺落到 Layer 的最小表达不需要新概念**（§77）：`commitment:<id>` 这个 cell 的值即承诺状态，状态迁移就是既有的 `put`，幂等键就是消息 id。承诺正文必须离开 wire，进内容面/带外存储，由 fallback ref 指回。→ §3.2
5. **`commit.head` 给的是「抗改写」，不是「真实性」。** 一个 agent 完全可以提交一条虚假的 `FULFILLED`，链和锚点都会如实记录它。把「可审计」说成「可验证为真」是过度声明。→ §3.4
6. **UPP 与 CRP 的编码层复用已经是既成事实，不是建议**：`src/verse/crp.h:59` 的 `typedef UppBuf CrpBuf;` 与 `src/verse/crp.h:43-46` 的注释说明了共享的转义实现。要新建的只有一条绑定：**宿主崩溃/心跳超时 → 承诺违反的持久记录**，今天这条线不存在。→ §5
7. **今天「协调事件被持久审计」不成立**：CRP 的 64 事件环是会话结构体里的内存数组（`src/verse/crp.c:703` 的 `CrpEvent events[CRP_EVENT_WINDOW]`，淘汰逻辑 `src/verse/crp.c:1176-1182`），不落盘。这是本设计中最大的 `unresolved`，见 §7.1。→ §3.5

---

## 1. 五个系统各自负责什么

先分清职责，再谈映射。把它们混为一谈是这类方案最常见的失败起点。

| 系统 | 它是什么 | 它明确**不是**什么 |
|---|---|---|
| **CRP**（`src/verse/crp.{h,c}`、`crp_hub.c`、`crp_peer.c`，会话态在 `src/platform/crp_session.{h,c}`） | 跨节点/跨 Verse 的**路由与权威**协议：三种帧、HMAC 能力令牌、单调 seq、64 事件环、撤销集合、租约与恢复 | 不是推理表示，不是知识表示，不校验语义 |
| **Verse Layer**（`src/verse/layer.h`） | **权威状态 + 可验证历史**：规范化 JSON 事件日志、sha256 哈希链、外部锚点 `commit.head` | 不是通用 KV，**不是文本存储**（`vl_layer_put` 的 `value` 是 `long long`） |
| **UPP**（`src/verse/upp.h`） | **进程/插件宿主的生命周期监督**：hello/welcome ABI 协商、start/stop/crash/heartbeat、四态机 | 不是跨节点寻址，不是能力授予，不是协调语义 |
| **G²CP** | **言语行为与社会承诺的语义**：7 个 performative、`C(debtor, creditor, condition)`、图操作 | **不是可用的线上编码**（`serialize()` 有损且非规范，见 §4.2） |
| **Slipstream** | **线上词法**：`SLIP v3 <src> <dst> <Force> <Object> [payload...]`、12 值封闭 Force、严格 token 约束、roundtrip 规范 | 不是权限模型，不是传输层，不带会话状态 |

**桥接层的职责**，一句话：把 G²CP 的语义字段**投影**到 CRP 的帧 + 令牌 + Layer 的 cell，并用 SLIP 作为线上词法；不新增第五套机制。

判定标注：

| 结论 | coverage | implementation | evidence |
|---|---|---|---|
| 桥接层职责定义 | covered | specified | E1（本文件） |
| CRP 三种帧 + 令牌 + 撤销 + 恢复 | covered | implemented | E4（§9 命令与输出） |
| Verse Layer 事件日志 + 哈希链 + 外部锚点 | covered | implemented | E4（`verse_layer_probe`、`verse_closed_loop`） |
| UPP 帧编解码 + 四态机 | covered | partial（无传输接线，见下） | E4 库层 / E1 接线层 |
| G²CP 语义（performative + 承诺） | covered | **retired（不移植）**，仅作语义参照 | E2（按 SHA 固定读到的一手源码，见 §9.3） |
| SLIP 作为线上词法 | partial | planned | E1（本文件）+ E2（外部规范原文） |

---

## 2. 映射表：G²CP performative ↔ CRP 帧与能力令牌

### 2.1 为什么不能逐条一一对应

CRP 的帧类型只有三个，且由 `crp_type_is_valid()`（`src/verse/crp.c:491`）硬校验：

```c
return type && (strcmp(type, "FIND") == 0 || strcmp(type, "PORTAL") == 0 ||
                strcmp(type, "SIGNAL") == 0);
```

这三个值是**路由分类**（问注册表 / 取能力令牌 / 送数据面事件），落在不同的平面上。而 G²CP 的 `Performative` 是**言语行为分类**（7 个封闭成员：REQUEST、INFORM、QUERY、PROPOSE、CONFIRM、REJECT、UPDATE）。两者是正交的两根轴：

- **轴 A（路由/权威）**：FIND / PORTAL / SIGNAL + 令牌的 `capabilities`。由 CRP 决定。
- **轴 B（言语行为）**：performative（G²CP，7 值）或 Force（SLIP，12 值）。由协调层决定。

一个 performative 不"是"一个帧；它是**放在帧里的一个字段**。因此下表用的是「performative → 用哪个帧送 + 需要什么 capability」的投影关系，而不是同构关系。

### 2.2 逐条映射（7 个 performative）

| G²CP performative | 承诺（源码原文） | CRP 路由 | 需要的 capability | SLIP Force | 对应质量 |
|---|---|---|---|---|---|
| **REQUEST** | `C(receiver, sender, execute_and_return(op))` | `SIGNAL`（数据面，定向 peer） | `signal` | `Request` | **可对应（有缺口）** |
| **QUERY** | `C(receiver, sender, truthful_response(op))` | `SIGNAL`；若问「谁有能力做」则退化为 `FIND` | `signal` / FIND 无鉴权 | `Ask` | **部分对应** |
| **INFORM** | `C(sender, receiver, grounded(subgraph, G))` | `SIGNAL` | `signal` | `Inform` | **可对应（不加校验）** |
| **PROPOSE** | `C(receiver, sender, evaluate_and_respond(op))` | `SIGNAL` | `signal` | `Propose` | **可对应（有缺口）** |
| **UPDATE** | `C(receiver, sender, apply_if_valid(ΔG))` | **`PORTAL` + `SIGNAL` 两步** | `portal`（签发）+ `signal`（提交） | `Commit` | **CRP 里没有单一对应物** |
| **CONFIRM** | `C(sender, receiver, verified(result))` | `SIGNAL`（用帧的 `id` 指回） | `signal` | `Accept` | **可对应，最干净的一条** |
| **REJECT** | `C(sender, receiver, violated(op, constraint))` | **不是帧类型，是结果码** | — | `Reject` / `Error` | **CRP 里没有对应物** |

**逐条说明（哪些能一一对应、哪些不能）：**

- **CONFIRM ← 这是唯一接近一一对应的条目。** CRP 帧的 `id` 字段（`crp.h` 的 `frame_tail()`，键序冻结为 `crp/type/payload/id`）天然就是关联键；`crp_frame_*(..., id)` 三个构造函数都接受它。G²CP 的 `CommitmentStore._fulfill_matching(debtor, creditor, condition_prefix)` 用 (债务人, 债权人, 条件前缀) 三元组去兑现，在 CRP 里换成 (peer, peer, 关联 `id`) 即可，不需要新字段。

- **UPDATE —— 不能硬编成一对一，而且这里的差异是实质性的。** G²CP 把义务放在**接收方**：`UPDATE` 的 debtor 是 `msg.receiver`，也就是「你（接收方）必须去检查并应用这个 delta」。CRP 的模型**方向相反**：写入权由 `PORTAL` **预先授予**（令牌 `capabilities` 里写明），接收方无权自行决定要不要应用。所以 UPDATE 的两半必须分别落在 `PORTAL`（**谁能改**）与 `SIGNAL`（**改什么**）上。
  > ⚠️ **当前实现的真实缺口**：CRP 的令牌机制本身是 `implemented` 且 E4，但**签发侧只有调用方认证、没有 per-`(verse, peer)` 授权**（`crp-portal-auth` `40f6094` 已合入 `main`；判定点 `crp_enroll_check()`，`src/verse/crp.c:1057`，两处拒绝在 `:1077`/`:1079`）。这两句必须分开说：**（a）认证** —— `/portal` 现在要求 enrollment 证明，未配 `CRP_ENROLL_SECRET` 时 fail-closed；**（b）授权** —— 但该证明的唯一输入是 `verse` 与 `peer` 两个字符串（`tools/crp_relay.js:29`），没有第三个因子，因此知道 hub 级共享 secret 的人**仍可为任意 `(verse, peer)` 现算证明并取得 `signal` 令牌**。也就是说"能力令牌"到目前为止是*能力凭证*（证明你是这个 hub 的成员）而非*访问控制*（证明你被授权访问这一对）。把 UPDATE 映射到 PORTAL 的前提是补上 per-pair 授权，否则这条映射仍只是形式上的。完整论证见 §7.7。

- **REJECT —— 最大的语义缺口。** 三种帧类型里没有"拒绝"。CRP 的拒绝表现为 HTTP 状态码加错误字符串：`403 invalid capability token`、`409`（seq 乱序且无 replay）、`400`、`404`。G²CP 把拒绝当作**可审计的言语行为**（`REJECT` 产生一条 `ACTIVE` 的承诺记录），CRP 把它当作**传输层结果**。后果很具体：**错误响应不进事件日志**。所以要让 REJECT 可审计，发送方必须**显式**发一条 `SIGNAL`（`event: "REJECT"`），不能指望 CRP 的错误响应 —— 那条响应在 Layer 里是隐形的。

- **REQUEST / PROPOSE —— 能对应，但 CRP 不保证"执行"。** `crp_registry_signal()` 返回的是 `202 {accepted, verse, event, seq}`：**接受**（进序列）不等于**执行**。G²CP 的 `execute_and_return(op)` / `evaluate_and_respond(op)` 承诺了执行语义，CRP 只能承诺送达与排序。这个差别必须写在设计里，否则"发出去就等于做到了"会成为默认错误假设。

- **QUERY —— 需要一个前提才能对应。** CRP 的注册表查询是 `FIND`（控制面，只读，无鉴权，无 seq）——那问的是"有没有这个 Verse/节点"，不是"你对这个知识问题会怎么答"。要对齐 G²CP 的 `truthful_response(op)`，前提是**目标 peer 已知**，然后走 `SIGNAL`。目标未知时 QUERY 在 CRP 里没有承载，只能退化成 `FIND`，语义不同。

- **INFORM —— 最自然的对应，但"grounded"无法在 CRP 里校验。** `SIGNAL` 的 `event` + `data` 就是"我告诉你一件事"。但 CRP 没有 G（知识图），也无法判断 `grounded(subgraph, G)`。grounding 只能由桥接层的外部校验器承担，而校验器自己的结论也只是一条记录（§3.4）。

### 2.3 CRP 有而 G²CP 没有（这些是 CRP 已经 `implemented` 的资产，不要重造）

| CRP 机制 | 证据 | G²CP 的对应物 |
|---|---|---|
| HMAC 能力令牌 `base64url(body).base64url(HMAC-SHA256(secret,body))`，body = `{verse,peer,capabilities,exp}` | `src/verse/crp.h:13`、`crp.c:285-375` | **无任何对应物** |
| 撤销集合（上限 10000，`CRP_DEFAULT_MAX_REVOKED`） | `crp.h`、`crp.c` 的 `crp_registry_revoke()` | 承诺只能 `CANCELLED`，且是进程内内存态 |
| 会话单调 seq + 乱序无 replay → 409 | 闭环记录里的 `409`/`resume` 分支 | 无 |
| 64 事件环 + `resume(replay)` 补发 | `crp.c:1176-1196`、`CRP_EVENT_WINDOW` = `crp.h:52` | 无（Neo4j 事务由数据库代管，协议层没有） |
| 会话租约与节点交接（11 态会话层） | `src/platform/crp_session.{h,c}` | 无 |
| 持久化哈希链 + **外部**锚点 | `src/verse/layer.h:66-78`、`eventlog.h` | 无（`CommitmentStore` 是 `list`，进程结束即消失） |
| 传输无关分帧（1 MiB 上限 `CRP_MAX_FRAME_BYTES`） | `crp.h` | 无 |

### 2.4 G²CP 有而 CRP 没有（这些是桥接层要"提供语义但不提供实现"的部分）

| G²CP 概念 | 源码位置 | 在 CRP 里 |
|---|---|---|
| 封闭的 7 值 performative 枚举 | `g2cp/protocol/messages.py:20-29` | `SIGNAL` 的 `event` 是**自由字符串**，引擎不校验（`crp_type_is_valid()` 只校验帧类型） |
| 承诺对象与 5 态生命周期（created/active/fulfilled/violated/cancelled） | `g2cp/protocol/commitments.py:17-24` | 无 |
| debtor / creditor 的方向性（谁欠谁） | 同上，`create_from_message()` | 无 |
| 图操作作为消息内容（`TraversalOperation` / `UpdateOperation` / `GraphDelta`） | `g2cp/protocol/messages.py` | 无；只能降级为指针（§6） |
| 前置/后置条件（`apply_if_valid`、`violated(op,constraint)`） | 同上 | 无规则引擎 |

### 2.5 两者都没有的：**agent 死了，它的承诺怎么办**

- G²CP 靠本地超时：`check_violations(timeout_seconds: float = 30.0)`，基于 `time.time()`，把超时的 `ACTIVE` 承诺标成 `VIOLATED`。
- UPP 有 `crash`（带 error/exit code）与 `timeoutMs=15000` 的心跳判定（严格 `>`）。
- **两条时钟今天互不相干，也都不落盘。** 桥接层要新增的唯一语义就是这条绑定（§5.3）。这是 `unresolved`。

### 2.6 SLIP 的 12 个 Force 在 CRP 里的承载

Slipstream 的 Force 是**封闭 12 值**（规范原文与 README、ABNF 三处一致）：`Observe`, `Inform`, `Ask`, `Request`, `Propose`, `Commit`, `Eval`, `Meta`, `Accept`, `Reject`, `Error`, `Fallback`。

| Force | CRP 承载 |
|---|---|
| `Observe` | `FIND`（无副作用读）或只读 `SIGNAL` |
| `Inform` | `SIGNAL` |
| `Ask` | 目标已知 → `SIGNAL`；目标未知 → `FIND` |
| `Request` | `SIGNAL` + `signal` capability |
| `Propose` | `SIGNAL` |
| `Commit` | `PORTAL`（授权）+ `SIGNAL`（提交） |
| `Eval` | `SIGNAL`（只在语义上有意义，CRP 无执行语义） |
| `Meta` | `SIGNAL` |
| `Accept` | `SIGNAL` + 关联 `id` |
| `Reject` | `SIGNAL`（**必须显式发**，见 §2.2） |
| `Error` | `SIGNAL` |
| `Fallback` | `SIGNAL` + 带外 ref（§4.4） |

**12 与 7 不是子集关系，两个枚举都不能替另一个**：SLIP 有 `Observe`/`Eval`/`Meta`/`Error`/`Fallback` 而 G²CP 没有；G²CP 有 `QUERY` 而 SLIP 把它归进 `Ask`。反过来 SLIP 用 `Propose`/`Commit` 覆盖了 G²CP 的 `UPDATE` 意图，但**丢失了"义务在接收方"这个方向性**。结论：SLIP 是**语法**，G²CP 的 `C(debtor, creditor, condition)` 是**语义**，两者不能互相替代；桥接层需要两者，且必须显式保存 debtor/creditor（见 §3.2 的 actor/role）。

判定标注：

| 结论 | coverage | implementation | evidence |
|---|---|---|---|
| 7 条 performative 的帧投影 | covered | specified | E1 + E2（见 §9.3） |
| CONFIRM ↔ 帧 `id` 关联 | covered | specified | E1 + E4（`id` 字段已 implemented） |
| REJECT 在 CRP 无对应物 | **conflicted** | specified | E2（两侧源码都读到） |
| UPDATE 需 PORTAL + SIGNAL 两步 | covered | specified | E1；且**签发侧只有调用方认证、没有 per-pair 授权**这一前提缺口 = E4 实测（§9.1，完整论证见 §7.7） |
| 令牌/撤销/seq/事件环/锚点 | covered | implemented | E4 |
| SLIP 12 Force 的承载位置 | partial | planned | E2（外部规范） |

---

## 3. 审计链：社会承诺与「可审计推理链」落到 Verse 哈希链

### 3.1 G²CP 承诺模型的事实（按 SHA 读到，未运行）

```python
class CommitmentState(str, Enum):
    CREATED = "created"; ACTIVE = "active"; FULFILLED = "fulfilled"
    VIOLATED = "violated"; CANCELLED = "cancelled"

@dataclass
class SocialCommitment:
    debtor: str; creditor: str; condition: str
    state: CommitmentState = CommitmentState.CREATED
    source_message_id: str = ""
    created_at: float; fulfilled_at: Optional[float] = None
```

- 承诺由消息创建（`create_from_message()`），`CONFIRM` 一进来就同时建一条 `FULFILLED` 的承诺并 `_fulfill_matching()` 兑现掉对应的 `REQUEST` 承诺。
- `CommitmentStore` 是一个进程内的 `list[SocialCommitment]`，注释自称「Commitments are publicly observable through the audit log」——但**它没有任何持久化、没有哈希链、没有签名**。所谓 "audit log" 在这份源码里没有对应实现。
- `check_violations()` 用本地时钟判超时。

### 3.2 承诺在 Verse Layer 上的最小表达（不新增概念）

Layer 的写入口是（`src/verse/layer.h:50-53`）：

```c
VlStatus vl_layer_put(VlLayer *l, const char *idempotency_key,
                      const char *actor, const char *role,
                      const char *cell, long long value,
                      int steps[VL_COMMIT_STEPS]);
```

`seq` / `rev` 由 Layer 自己分配，**调用方不能提供**（`layer.h:49` 的原话："seq/rev are assigned here, not by the caller"），这正好满足白皮书 §55.13 不变量 7「断线重连和超时重试必须依靠序列号、查询和幂等键，不能重复结算」。

于是承诺的三个时刻各是一条既有记录，**没有任何新类型、新帧、新名词**：

| G²CP 时刻 | Layer 记录 | 说明 |
|---|---|---|
| 生成 | `vl_layer_put(l, key=msg.id, actor=debtor, role="debtor", cell="commitment:<id>", value=1)` | 幂等键 = G²CP 的 `source_message_id`；重复投递不产生第二条 |
| 兑现 | `vl_layer_put(..., cell="commitment:<id>", value=2)`（`role="creditor"`） | 只能是 `put`，因为 Layer 只认 put/undo |
| 违反 | `vl_layer_put(..., cell="commitment:<id>", value=3)` | 触发源见 §3.5 |
| 取消 | `value=4` | |

**数值编码（5 态 → 整数）**：`1=ACTIVE, 2=FULFILLED, 3=VIOLATED, 4=CANCELLED`（`CREATED` 不入 Layer —— 它在产生记录之前不存在，硬造一个 0 态只会让回放多一条无信息的记录）。

**为什么必须是整数**：`vl_layer_put` 的 `value` 是 `long long`，且引擎的 JSON 读取器 `src/verse/json_min.c` 明确拒绝浮点与 `\u0000`。这不是可绕过的实现细节，是硬边界 —— 也正因为如此，**承诺的正文（`condition` 字符串）不可能存在 Layer 里**。

**debtor/creditor 放哪**：`put` 已经有 `actor` 与 `role` 两个字符串字段，直接复用：`actor` = debtor，`role` ∈ {`debtor`, `creditor`}。G²CP 的方向性因此不需要新字段就能保住方向语义。

### 3.3 承诺正文与证据：必须离开 wire，进带外存储

Layer 只存数值，正文（`condition`，例如 `apply_if_valid(delta_G)`、`verified(result)`）和证据（子图、操作、校验器结论）必须另存。**这不是妥协，这正是 Slipstream MUST NOT #2 要求的拓扑**（引原文）：

> 2. **MUST NOT transmit raw natural language on wire**: The wire format carries semantic pointers, not human text. Natural language belongs in out-of-band storage accessed via fallback refs.

在 Inimerse 里，"out-of-band storage" 已经存在，不需要新建：

1. **内容面**：内容寻址对象（`.vverse` 容器、`verse/<id>/` 下的资源文件、`<target>/.inim-cache/archives/<sha256小写>.inim`，见 `docs/API.md` §11.1）。
2. **Layer 的事件日志本身**（`verse/<verse_id>/events.log`）—— 但只能存数值语义，不能存散文。
3. 不采用：把正文塞进 CRP 帧的 `data`。那会让 wire 承载散文，直接违反 MUST NOT #2，也把 1 MiB 帧上限变成了内容通道。

**具体分工**：正文与证据 → 内容面（哈希 + 签名 + 许可 + 过期，§55.13 #4）；索引（ref → 内容地址 + 状态）→ Layer 的 cell。

### 3.4 `commit.head` 在其中扮演什么角色

`commit.head` 是 Layer 的**外部**持久提交指针。它的作用与边界必须讲清楚，否则"有哈希链 = 可审计为真"会变成默认误解：

| 问题 | 哈希链 | `commit.head` |
|---|---|---|
| 某条记录被就地改写了吗？ | **能发现**（链会断） | — |
| 一条记录被改写**且整条链被重算**了吗？ | **不能发现**（重算后的链依然自洽） | **能发现**（外部指针不匹配 → `VL_ERR_RECOVERY_REQUIRED`） |
| 承诺是真的吗？ | **不能** | **不能** |

第二条正是 `docs/API.md` §10.6 记录的既有理由，原文：**「日志的哈希链是自洽的，就地改写一条记录后重算的链依然自洽，唯一能识别篡改的就是这个外部指针」**。不一致时的行为是明确的：`hello` 返回 `recovery_required` 且不设置 `negotiated`（不授予会话），`status` 同样返回 `recovery_required`，`drain` 返回 `drained:false`；修复只能由显式的 `vl_layer_repair()` 操作推进，不会静默修好。

第三条是**设计必须写下的边界**：一个 agent 完全可以提交一条 `value=2`（FULFILLED）而实际没做任何事。链与锚点会**如实记录**这条虚假承诺。所以：

- 粒度不同：`commit.head` 的粒度是**整条日志**，不是单条承诺。「某一条承诺被违反」必须由 `commitment:<id>` 的当前值 + 该条记录所在 seq 一起读，不能只靠 head。
- 因此"可审计"要拆成两句：**provenance 可审计**（谁在什么时候把状态改成了什么，抗改写）是 `covered/implemented/E4`；**正确性可验证**（这条承诺是否真的被兑现）**不是 Layer 提供的**，需要外部校验器，而**校验器的结论本身也只是一条记录**（递归到此为止，不能再往下声称）。

### 3.5 违反检测的两个触发源（今天都不存在）

| 触发 | 现状 | 桥接层需要 |
|---|---|---|
| 本地超时 | G²CP 有 `check_violations(timeout=30.0)`（进程内、不落盘）；UPP 有 `timeoutMs=15000` 的心跳（严格 `>`） | 选一个时钟作为唯一权威（建议 UPP 心跳，因为它是宿主存活的事实来源），超时即 `put(value=3)` |
| 宿主崩溃 | UPP 有 `crash`（带 error/exit code），但**没有任何东西把它变成 Layer 记录** | UPP `crash` → 该宿主所有 `ACTIVE` 承诺 → `put(value=3)`，用 UPP 帧的 `id` 做关联键 |

### 3.6 "可审计推理链"这句话的诚实处理

G²CP 摘要自述「produces fully auditable reasoning chains」。这句话**不能被当作已验证结论**（自报，本仓库未复现，见 §10）。Verse 实际能提供的是：

- **能**：每条记录前向绑定（`eventlog.h` 的 sha256 链）；整条日志对外部锚点绑定（`commit.head`）；从 `events.log` 独立重放并比对状态哈希（`vl_layer_replay()`，`layer.h:64-68`）。
- **不能**：推理的**正确性**、`grounded(subgraph, G)` 的成立性、`apply_if_valid(ΔG)` 的判定结果。这些必须在桥接层的外部校验器里，且校验器是**新的信任主体**，必须显式记账（§67.6 的 `missing_owner`）。

判定标注：

| 结论 | coverage | implementation | evidence |
|---|---|---|---|
| 承诺五态 ↔ Layer cell 数值编码 | covered | specified | E1 |
| 幂等键 = 消息 id，seq/rev 服务端权威 | covered | implemented | E4（`verse_protocol_probe`、`verse_closed_loop`） |
| 哈希链 + 外部锚点的抗改写 | covered | implemented | E4（§9） |
| 锚点不能证明承诺为真 | covered | **不适用（这是边界声明）** | E1 + E4（API.md §10.6 的实测理由） |
| 违反检测的两个触发源 | **unresolved** | planned | E1 |
| "推理链正确性可验证" | **unresolved / 不声称** | retired | — |

---

## 4. SLIP 作为线上语法

### 4.1 谁编码、谁解码

| 角色 | 谁 | 为什么是它 |
|---|---|---|
| **编码** | **发送方的 agent 适配器**（把意图变成一条 SLIP 行） | 只有它知道意图 |
| **解码 + 词法校验** | **接收方的宿主**（引擎侧 CRP 客户端），在帧进入 Layer 或 agent **之前** | 不能信任对方的编码器遵守词法；也不允许 agent 把散文塞上 wire。校验失败**拒绝**，不截断、不修正（与引擎既有的「拒绝，绝不静默降级」一致：`capability_refused`、`malformed`） |

这条分工有一个直接推论：**解码侧的拒绝路径必须是可区分的**（§55.13 #8「新增字段、能力和规则不能被旧端静默解释为成功」）。词法错误与权限错误必须是不同错误码，否则运维无法判断是"消息写得不对"还是"没权限"。

### 4.2 为什么不能直接用 G²CP 的文本格式当 wire

G²CP 的 `G2CPMessage.serialize()` 产出的是**多行、缩进、带 `{...}` 集合记号和 `->` 箭头的人读文本**，而且**有损**：

```python
if self.result.edges:
    lines.append(f"  EDGES: [{len(self.result.edges)} edges]")   # 边集被折叠成一个计数
if self.result.paths:
    for p in self.result.paths[:3]:                              # 路径被截断到 3 条
        lines.append(f"  PATH: {' -> '.join(p)}")
```

后果是 `parse(serialize(m)) != m`（边集不可恢复、超过 3 条的路径丢失）。这与 Slipstream MUST 规则 9 的 `parse(format(msg)) == msg`（"The wire format is canonical"）**直接冲突**。所以：

> **G²CP 缺的正是线上语法，Slipstream 补的正是这个位置。** 两者的关系是互补，不是二选一。

### 4.3 SLIP 行放在哪里

放进 CRP `SIGNAL` 帧的 `payload.data` 里的**一个字符串字段**，不需要新增帧类型、不需要新增 key 顺序。形状（key 顺序沿用 `crp_encode` 冻结的 `crp/type/payload/id`）：

```json
{"crp":1,"type":"SIGNAL","payload":{"verse":"demo","event":"INFORM","data":{"slip":"SLIP v3 Alice Bob Inform Subgraph n1 n2","ref":"Ab12Cd"},"timestamp":1767225600000},"id":"m-0001"}
```

为什么是"嵌进 `data`"而不是另起一个分帧通道：

1. `data` 已经是任意 JSON 对象，**引擎侧零改动**即可承载（`crp_registry_signal()` 原样保存与回放 `data`）。
2. 另起通道需要第二套转义/分帧实现，而 `src/verse/crp.h:59` 的 `typedef UppBuf CrpBuf;` 注释已经把这件事定性为必须避免的漂移（"without a second escaping implementation that could drift"）。
3. §55.4 的取向是"消息头应尽量统一"，不是一个语义一套分帧。
4. `json_min.c` 能处理任意 JSON 字符串；SLIP 行本身是 `[A-Za-z0-9 ]+`，转义代价接近零。

### 4.4 带外存储与 fallback ref 的尺寸冲突（必须正视）

SLIP 的 `Fallback` 要求带一个 **1–16 位字母数字**的 ref（MUST 规则 6 + ABNF `fallback-ref = 1*16( ALPHA / DIGIT )`）。而 Inimerse 的内容地址是 sha256 十六进制（64 字符）。**ref 装不下内容地址，所以 ref 不能是内容地址本身。**

设计结论：**ref 是每会话的短别名**，经一张索引表映到 `(sha256, 签名, 长度, 许可, 过期)`。带外存储的三件东西：

| 层 | 内容 | 既有落点 |
|---|---|---|
| 正文/证据 | 真正的自然语言、代码、子图片段 | 内容面（`.vverse` / `.inim-cache/archives/<sha256>.inim` / `verse/<id>/` 资源文件） |
| 索引 | ref → 内容地址 + 元数据的映射 | Layer 的 cell（数值）或会话级索引文件 |
| 引用 | `ref` 出现在 wire 上 | `payload.data.ref`（或 SLIP 行的第 7 个 token） |

> ⚠️ **设计债务（按 §67.6 记账）**：`missing_owner` + `hidden_dependency` —— 这张索引表的**所有者、生命周期、清理策略**都还没有定义。如果索引表活得比引用它的帧短，审计就会在 ref 处断链，而且**哈希链不会报警**（链只保证记录没被改，不保证 ref 可解析）。这是本设计里最容易漏掉的一环。

### 4.5 限制：只能承载协调意图，不能承载代码/散文/工具输出

四条独立理由，任何一条单独成立就足够：

1. **规范要求**（Slipstream MUST NOT #2 原文，§3.3 已引）：wire 上不得出现自然语言。MUST 规则 6 的伴随句同样明确：*"Raw natural language text MUST NOT appear on the wire."*
2. **词法硬限**：全部 token 必须匹配 `[A-Za-z0-9]+`；payload ≤ 20 个 token，每个 ≤ 30 字符；agent id 1–20 字符。代码、JSON、日志在字符集和长度上直接被排除（30 字符连一行栈帧都装不下）。
3. **引擎侧的正常位置在别处**：CRP 帧上限 1 MiB 但它是控制/数据面，不是内容通道（§55.1：内容面才做完整性、版本、许可和去重，而且"必须校验哈希和签名"）。
4. **可拒绝性**：违反以上约束的输入必须**拒绝**并返回可区分错误，不得截断或"尽力而为"地发送 —— 静默截断会让审计链记录一条语义已被改变的协调消息。

**边界（同一条的重要性）**：SLIP 是**词法**，不是**权限**。一条词法完全合法的 SLIP 行不携带任何权威；权威**完全**来自 CRP 的令牌（`crp_token_check_str(secret, tok, verse, peer, "signal", now)`）。**严禁**把 Force 当权限用 —— 例如把 `Commit` 解读成写许可，或把 `Reject` 解读成"撤回了他人的提交权"。`src/verse/crp.c:1155-1156` 与 `:1220` 的检查（`crp_token_check(r->secret, tok, verse, …, "signal", now)` / `crp_token_check_str(r->secret, tok, vstr, pstr, "signal", now)`）只认字面量 `"signal"`，与 `event` 字段无关。

判定标注：

| 结论 | coverage | implementation | evidence |
|---|---|---|---|
| SLIP 行嵌入 `data` 的载体形状 | covered | specified | E1 + E4（`signal` 已能携带任意 `data`） |
| 编码方/解码方分工 | covered | specified | E1 |
| G²CP `serialize()` 有损、不能当 wire | covered | specified | E2（`g2cp/protocol/serializer.py @e94f13d3` 源码） |
| ref 是短别名而非内容地址 | covered | specified | E2（ABNF `1*16`）+ E1 |
| ref 索引表的所有者/生命周期 | **unresolved** | planned | E1 |
| "只承载协调意图"的四条理由 | covered | specified | E2 + E4 |

---

## 5. 与 UPP 的关系：并存 + 复用编码层 + 新增一条绑定

### 5.1 决定

**并存。不替换任何一方。** 但"复用编码层"这一点不是建议，是**既成事实**，证据在源码注释里：

- `src/verse/crp.h:59`：`typedef UppBuf CrpBuf;` —— 注释写明这个别名的存在理由就是让 CRP 代码用自己的词汇读写，**而不产生第二套会漂移的转义实现**。
- `src/verse/crp.h:43-46`：CRP 与 UPP 共享 `upp_buf_*`、`upp_json_write`、`upp_is_safe_int`、`upp_now_ms`。

所以 §77「不新增概念」在这里的落实方式很直接：**桥接层不需要自己的分帧、缓冲或转义**。

### 5.2 为什么不能互相替换

| 不能替换 CRP | 不能替换 UPP |
|---|---|
| UPP 没有跨节点寻址 | CRP 没有 ABI 协商（`welcome` 的 `abiRange` 不相交 → `incompatible ABI ranges: A vs B`） |
| UPP 没有能力令牌，没有 `verse/peer/capabilities/exp` 四元组 | CRP 没有**进程崩溃**语义（`crash` 带 error / exit code） |
| UPP 没有 seq 重放保护，也没有撤销集合 | CRP 没有宿主四态机（running / crashed / stopped / incompatible） |
| UPP 的 `id` 语义（**空 `id` 删掉整个 key**）与协调消息语义无关，套用会误删状态 | CRP 的令牌不告诉你"这个 agent 宿主还活着吗" |

**分工一句话**：
- UPP 答：**这个 agent 宿主活着吗？ABI 兼容吗？崩了吗？**
- CRP 答：**它的协调消息送得到吗？它有权限吗？会话序列一致吗？**
- Layer 答：**它的承诺现在是什么状态？谁在什么时候改的？**

### 5.3 唯一需要新建的连接件

**UPP 的崩溃/心跳超时 → Layer 的承诺 `VIOLATED` 记录。**

今天这条线不存在：`crash` 只改 UPP 的会话状态；`check_violations()` 只改 G²CP 的内存 `list`；`crp.c` 的内存事件环与 Layer 的 `events.log` 之间没有任何桥（`grep` 确认 `src/` 中没有任何 `commitment` 概念，见 §9）。

规则草案（不新增概念，只用既有机制）：UPP 帧的 `id` 作为关联键 → 该宿主名下所有 `ACTIVE`（`value=1`）的 `commitment:*` cell → `vl_layer_put(cell=..., value=3)`。

两条可实现性约束（都来自既有实现，不是设计选择）：
1. **关联键必须走 UPP 帧的 `id` 字段，不能走时间戳** —— UPP 的 `timestamp` 只接受整数，而 `json_min.c` 明确拒绝浮点（对端 `Date.now()` 是浮点，会整行拒绝）。
2. **心跳相等是允许的**：UPP 的 `seq`/`timestamp` 判定是"非递减"，相等合法；超时判定是严格 `>`。绑定规则必须尊重这两条，否则会在合法帧上误判违约。

判定标注：

| 结论 | coverage | implementation | evidence |
|---|---|---|---|
| UPP/CRP 共享编码层 | covered | **implemented** | E4（`crp.h:59`、`crp.h:43-46` + `verse_upp_crosscheck`） |
| UPP 不进传输层（`inim-server`/`inim-client` 无 `--upp`） | covered | partial | E4（源码结构） |
| 两者不能互相替换的理由 | covered | specified | E4 + E1 |
| UPP 崩溃 → 承诺违反的绑定 | **unresolved** | planned | E1（今天不存在，grep 已确认） |

---

## 6. 明确不做的（反模式）

1. **不把 G²CP 的 `serialize()` 放上 wire** —— 有损，见 §4.2。
2. **不移植 Neo4j / 不引入知识图谱** —— §77 要求下一阶段不增加概念数量。图操作在桥接层只降级为**指针**（节点/边 id 列表），真正的图由内容面 + Layer 承载。G²CP 自己声明 `neo4j>=5.0.0`、`openai`、`sentence-transformers`、`pydantic` 等一行依赖，把它搬进来等于给引擎加一整套 Python 运行时。
3. **不把 G²CP 的 `CommitmentStore` 当权威** —— 它是进程内 `list`，无持久化、无链、无签名；用它替换 Layer 是降级。
4. **不让 Force 承担权限** —— 见 §4.5。
5. **不新增 CRP 帧类型** —— 三个够用，`event` 已是自由字符串。
6. **不改 `crp_encode` 的 key 顺序** —— 已有 115 条 text-identical 的交叉校验语料（§9），改顺序等于让全部语料失效。
7. **不把承诺正文放进 Layer cell** —— `value` 是 `long long`，做不到。
8. **不在 wire 上放自然语言** —— MUST NOT #2。

---

## 7. 未决问题（`unresolved`，按 §67.6 的债务分类）

| # | 问题 | 债务类型 |
|---|---|---|
| 7.1 | **64 事件环不落盘**：`CrpEvent events[CRP_EVENT_WINDOW]` 在会话结构体内（`crp.c:703`），淘汰逻辑 `crp.c:1176-1182`。协调事件今天**不进**任何持久审计。要么扩 Layer 承载协调事件，要么明确接受"协调事件不进审计"并写进文档 | `missing_owner` |
| 7.2 | fallback ref 索引表的所有者与生命周期（§4.4） | `missing_owner` / `hidden_dependency` |
| 7.3 | `SIGNAL.event` 是否加封闭枚举校验：加了就与 SLIP 的 12 值（或 G²CP 的 7 值）耦合，不加则没有词法门禁 | `unbounded_scope` |
| 7.4 | 承诺正文存内容面之后，内容面的撤销/过期与 Layer 里的承诺状态如何保持一致（§55.13 #4：内容缓存必须遵守过期和撤销） | `hidden_dependency` |
| 7.5 | **谁判定 grounding 与 `apply_if_valid`** —— 桥接层的外部校验器是新的信任主体，必须显式记账 | `missing_owner` |
| 7.6 | `commitment:<id>` 的命名空间碰撞：同一会话里多个 agent 的 id 来源唯一性由谁保证 | `term_drift` |
| 7.7 | **`PORTAL` 的签发侧只有调用方认证、没有 per-`(verse,peer)` 授权**（`crp-portal-auth` `40f6094` 之后 —— 这条前提今天变了一半，理由必须拆成两句）：**(a) 认证** —— `/portal` 现在要求 enrollment 证明 `base64url(HMAC-SHA256(CRP_ENROLL_SECRET, String(verse) + "\0" + String(peer)))`，判定点**只有一处** `crp_enroll_check()`（`src/verse/crp.c:1057`），`crp_registry_portal` 与 HTTP 监听器**共用它**；未配 `CRP_ENROLL_SECRET` 时 **fail-closed** 403 `portal enrollment is not configured`，无证明/证明错 → 403 `invalid enrollment proof`。它证明的是**调用方知道 hub 级共享 secret**，这是*调用方认证*（caller authentication）。**(b) 授权** —— 它**不是**针对特定 `(verse, peer)` 的*授权*（authorization）：证明的输入只有 `verse` 与 `peer` 两个字符串、没有第三个因子，所以知道该 secret 的人**仍可为任意 `(verse, peer)` 现算证明并取得 `signal` 令牌**；今天每个 hub 成员对**每一个**会话都有签发权。⇒ **UPDATE → PORTAL 的映射仍然只是形式上的**，缺口从「完全没有检查」变成「只有 hub 级成员资格，没有 per-`(verse,peer)` 授权」。在补上 per-pair 授权之前，§2.2 的 UPDATE 映射不成立 | `optimistic_claim` |
| 7.8 | 引擎里根本没有 `commitment` 概念（`grep` 确认），所以 §3.2 的编码方案是**纯设计**，未经任何实现验证 | （状态说明） |
| 7.9 | **「智能体」的身份没有任何已验证的载体**：`(verse, peer)` 是唯一出现在**被认证的**产物里的身份，但令牌只证明持有者知道 hub 级共享 secret（§7.7），而 `peer` 字符串由调用方自选；Layer 的 `actor`/`role` 是**调用方提供的自由字符串**（`vl_layer_put` 不校验）；`CAP_AI` 由 mod 在 `spi_meta()` 里**自声明**（`src/runtime/runtime.c:1156`，进程内、线上不可验证）；`ai_boundary`（`src/mod/say_stream.c:70`/`:98`）只是对 meta 的**子串匹配**，不是凭据校验。⇒ 本设计必须先写死一条规则：**桥接层不得采信调用方提供的 `actor`/`role`，承诺的 debtor 必须由令牌 scope 派生**。至于「谁是这个 peer」今天没有答案 —— 要不要把 peer 名变成真实主体是一次**先决断、先改参考实现**的事（见作业单 [streams/agent-bridge.md](../docs/streams/agent-bridge.md) §1 Q1/Q3） | `missing_owner` |

### 7.10 决断记录（`agent-bridge-round2`，2026-10-02，base main `5f23e95`）

> 本节是**决断**，不是备选清单。每条给出「证据 / 决断 / 理由」，并显式写明**引擎侧本轮零改动**。
> 标注约定：`coverage`（这套语义覆盖到哪一层）/ `implementation`（今天到底有没有实现）/ `evidence`（证据等级按 §64.2 E0–E6）。
> **说清本轮性质**：以下四条全部是**文档决断**，`implementation = 零`（引擎里 `grep -rniE "\bagent" src/` 与 `grep -rni "commitment" src/` 都是**零命中**，本轮复测）。任何一条要落地都必须**先改判据方 `tools/crp_relay.js`**，再动引擎，否则 `tools/crp_engine_crosscheck.js`（今天 `115 records, text-identical`）立刻判分歧。

#### 7.10.1 ①|peer 身份**不**升级为真实主体（决断）

- **证据**：`/portal` 的 enrollment 证明（`tools/crp_relay.js:29`）逐字为 `crypto.createHmac('sha256', enrollSecret).update(\`${verse}\0${peer}\`).digest('base64url')` —— 输入只有 `verse` 与 `peer` **两个调用方自选的字符串**，没有第三个因子；铸出的令牌 claim 里 `capabilities = ['signal']`（`tools/crp_relay.js:18` 默认值），`checkToken`（`tools/crp_relay.js:19`）的全部判据是 `p.verse === verse && p.peer === peer && p.exp > Date.now() && p.capabilities.includes(capability)` ⇒ **令牌表达的是"谁在说"，不是"谁被允许做什么"**。Layer 侧 `vl_layer_put(VlLayer *l, const char *idempotency_key, const char *actor, const char *role, const char *cell, long long value, int steps[VL_COMMIT_STEPS])`（`src/verse/layer.h:50-53`）对 `actor`/`role` **不做任何校验**，注释只管「seq/rev are assigned here, not by the caller」。`CAP_AI` 由 mod 在 `src/runtime/runtime.c:1156` 于 `spi_meta()` 里自声明，进程内、**线上不可验证**；`ai_boundary`（`src/mod/say_stream.c:70`/`:98`）只是对 meta 的**子串匹配**。而 hub/portal 路径**今天根本不能签名**：`CMakeLists.txt:96-115` 的 `verse_crp_probe` / `crp-hub` / `crp-peer` 三个目标**都没有链接 `src/common/ed25519.c`**（只有 `:121`、`:133`、`:191`/`:195`/`:199`、`:301` 链接了）。
- **决断**：**不升级**。`(verse, peer)` **继续只是 scope 名**，不是主体（principal）；`peer` 保持为**调用方自选、hub 不校验的字符串**。桥接层只承认一条身份规则：**债务的 `debtor` 必须由令牌 scope 派生**（即 hub 回给调用方的 `p.verse` / `p.peer`），**任何调用方自报的 `actor`/`role` 一律不得进入承诺记账**。
- **理由**：把 `peer` 变成真实主体**需要两件事同时发生** ——(a) 判据方新增一个载体（Ed25519 公钥或等价物）并让 `/portal` 的证明覆盖它；(b) 引擎侧 `CMakeLists.txt` 给上述三个 hub 目标补链 `src/common/ed25519.c`。这是**新机制**，落在本设计的「不新增第五套机制」原则之外，且**本轮任务明令不写引擎代码**。⇒ 本轮**决断为不动**，并给出可接受性论证（不是留白）：**今天的 scope 名已经足够表达"哪个会话里的哪条流"**，承诺记账只需要一个**稳定、hub 生成、调用方不能替换**的键 —— `(verse, peer)` 满足这一点（因为它是 hub 在 `/portal` 响应里返回的、且被令牌签名绑定的）。「这个 peer 背后是哪个人/哪个进程」**不是本设计要解决的问题**，它是 §55.6 平台会话面与未来身份层的事。**在身份层落地之前，把 `(verse, peer)` 当主体用会产生"自称即身份"的漏洞，所以宁可不升级。**
- **`coverage`**：设计层（桥接层规则已冻结）/ **`implementation`**：零（无任何代码）/ **`evidence`**：E1 实测（源码逐行）+ E4（BM: 结构事实）。

#### 7.10.2 ②|per-`(verse, peer)` 授权缺口：**收，但最小形态是"扩大证明输入"**（决断）

- **证据**：§7.7 已定论 —— 今天 `/portal` 只有**调用方认证**（证明你知道 hub 级共享 secret），**没有 per-pair 授权**：知道 secret 的人可为**任意** `(verse, peer)` 现算证明并取得 `signal` 令牌。「授权」在判据方里**没有表达面**：`grep -n "status" tools/crp_relay.js` **零命中**，令牌 claim 里唯一的授权位是 `capabilities: ['signal']`，而它是**常量默认值**，不随 `(verse, peer)` 变化（`tools/crp_relay.js:18`）。
- **决断**：**收，且最小形态已确定** —— 把 `/portal` 的证明输入从「两个自选字符串」扩展为**一个由 hub 持有的 per-pair 注册项**。具体：
  1. **判据方先改** `tools/crp_relay.js`：新增一张 hub 侧 per-pair 表（例如 `enrollments: Map<\`${verse}\0${peer}\`, {secret, capabilities}>`），`enrollProof` 的密钥**不再是全局 `enrollSecret`，而是该 pair 自己的 `secret`**；`makeToken` 的 `capabilities` **取自该表**而不是常量默认值；未登记的 pair → 与今天未注册 verse 同级的拒绝（**fail-closed**，沿用既有 403/404 语义顺序）。
  2. **必改的相邻项**：`enrollOk(auth, verse, peer)`（`tools/crp_relay.js:30`）的签名不变，但必须在**查注册表之后**才能知道用哪把 key ⇒ **403/404 的顺序要重新规定**：今天是 `!enrollSecret` → 403、`!enrollOk` → 403、未注册 verse → 404（`tools/crp_relay.js:80-92`）。新形态下"这个 pair 没登记"与"证明错"**必须不可区分**（都 403），否则 404 会变成一个**枚举 oracle**（攻击者可逐个试探哪些 pair 存在）。这是本轮**发现并写死的新约束**。
  3. **引擎侧**：`crp_enroll_check()`（`src/verse/crp.c:1057`，`:1069` 的 `crp_registry_portal` 与 HTTP 监听器共用的**唯一判定点**）对应改造；**但本轮不动**。
- **理由**：这是**在既有机制内部加一个因子**，不是新机制 —— 复用同一套 HMAC、同一个判定点、同一组 403/404，只把「一把全局 key」换成「一对一把 key」。它**不引入账号体系**（明令禁止），不引入新帧、不改令牌格式（`capabilities` 字段本来就在 claim 里，只是今天恒为常量）。相比之下，「另开一个 authorization 服务/新端點」才是新机制，故否决。
- **本轮不改判据方的理由（必写）**：改 `tools/crp_relay.js` 会让 `tools/crp_engine_crosscheck.js` 的语料（今天 **115 条**，其中 `relay_portal` **13 条**、`relay_resume` **10 条**）立刻分歧，因为引擎侧没有对应的 per-pair 表 ⇒ **判据方与引擎必须同一次改动落地**，而那需要先开新流（作业单已写明「若某决断确实要动引擎，先开流、写作业单、报给协调者」）。**本轮零代码的法律后果是：缺口按原样留着，但不再是无主债务。**
- **`coverage`**：设计层已到"可改"的粒度（含 key 归属、失败语义、顺序约束）/ **`implementation`**：零 / **`evidence`**：E1 实测（`tools/crp_relay.js` 逐行）+ E4。

#### 7.10.3 ③|REJECT 在 Layer 上落**独立 cell**，**禁止**复用 `commitment:<id>` 的 `value=4`（决断）

- **证据（外部，按 SHA 重取）**：`karim0bkh/G2CP_AAMAS @ e94f13d313c49f86129e23186a4f004fe983c098` 的 `g2cp/protocol/messages.py` 里 `Performative.REJECT` 的语义逐字是 *"Sender indicates receiver's operation violated constraints"*；`g2cp/protocol/commitments.py` 的 `create_from_message` 对 `REJECT` 造的是 **`C(sender, receiver, violated(op, constraint))` 且 `state=CommitmentState.ACTIVE`** —— 即**发指控的那一方自己欠了一条债**，要靠 `check_violations(timeout_seconds: float = 30.0)` / `violate()` 推进到 `VIOLATED`。**REJECT 从来不是"这条消息被拒了"**。另一侧：判据方的所有拒绝（400/403/404/409）都是 **HTTP 状态码 + 错误字符串**（`tools/crp_relay.js:80-92` 的 portal 三连、`:98` 的 `/signal`、`:105` 的 `/session/resume` 409），**一律不写任何事件/日志** ⇒ 「错误响应不进事件日志」是判据方的**原文事实**。
- **决断**：
  1. 桥接层对 REJECT 的承载**不复用** `commitment:<id>` 的 `value=4`。§3.2 已把 `4` 定义为 `CANCELLED`（**自愿撤销**）；把「被拒绝」也塞进 4 会让**撤销与拒绝不可区分**，而两者的责任方不同（撤销 = 自己收回，拒绝 = 指控对方违约）。
  2. 落地方式：**新增一条独立 cell 命名空间**（设计层，不实现）—— `reject:<id>`，值域**另立**，语义固定为 *"发送方声称接收方的操作违反了约束"*，与 G²CP 的 `C(sender, receiver, violated(op, constraint))` 同构（**debtor = 指控者**）。
  3. **可审计性的边界必须写死**：这条 cell 记录的是**发送方的声称**，不是"拒绝真的发生过"。Layer 的 `commit.head` 抗改写（§3.4）**不等于真实性**。所以文档与任何下游消费者都**不得**把 `reject:<id>` 读作"该操作确实被拒绝了"。**「被记录 ≠ 发生过」这句必须随 cell 定义一起出现。**
  4. **与「错误响应不进事件日志」共存的方式**：**不试图把 HTTP 拒绝搬进 Layer**。传输层/协议层的拒绝（403/404/409）继续**只存在于线路上、不进审计** —— 这是判据方的设计，桥接层不推翻它。要被审计的拒绝，必须由某一方**显式发一条 `SIGNAL`** 主动表达（§2.2 的 SIGNAL 已经是承载面），落成上面的 `reject:<id>`。⇒ **两条路径分开：协议拒绝 = 不可审计（且不改它）；语义指控 = 可审计（且要显式发信号）。** 桥接层永不把它们混为一谈。
- **理由**：这条决断同时满足两个硬约束 ——(a) 「错误响应不进事件日志」原样保留（不改判据方、不改引擎）；(b) 拒绝**仍然可审计**，但审计的是**声称**，并且文档里明说这一点。§0 结论 5 不把「可审计」偷换成「可验证为真」的原则在这里**逐字兑现**。
- **`coverage`**：设计层（cell 命名空间与语义已冻结）/ **`implementation`**：零（`commitment` 在 `src/` 零命中）/ **`evidence`**：E5（外部按 SHA 读源）+ E1（判据方逐行）。

#### 7.10.4 §7.1（64 事件环不落盘）|**显式非决断**（决断为"本轮不决断"，并给理由）

- **证据**：`CrpEvent events[CRP_EVENT_WINDOW]` 在会话结构体内（`CrpEvent` 定义 `src/verse/crp.c:692-708`），淘汰逐字为 `src/verse/crp.c:1176-1182` 的 `if (s->nevents == CRP_EVENT_WINDOW) { vj_free(s->events[0].event); vj_free(s->events[0].data); memmove(&s->events[0], &s->events[1], (CRP_EVENT_WINDOW - 1) * sizeof s->events[0]); s->nevents--; }`，**无任何落盘**。但**平台层本来就建模了「环答不了 → 要快照」**：`im_crp_session_resume_plan`（`src/platform/crp_session.c:84-98`）在 `behind && gap <= window` 时给 `*replay_from = last_ack_seq + 1; *needs_snapshot = 0;`，否则 `*replay_from = 0; *needs_snapshot = 1;`。且判据方**根本没有 status 面**（`grep -n "status" tools/crp_relay.js` 零命中）：`/session/resume`（`tools/crp_relay.js:103-109`）里 `const replay = (sessionEvents.get(key) || []).filter(e => e.seq > seq);` —— 窗口外的旧事件**被静默丢弃**，响应里**没有任何"你的 replay 不完整"信号**。引擎侧 `needsSnapshot` 的唯一出口是 `crp_registry_status_json()`（`src/verse/crp.c:1287`），调用者只有 `src/verse/crp_hub.c:217`（hub 的 `op:"status"` 分支）与 `src/verse/crp_probe.c:766`/`:943`/`:983` ⇒ **`status`/`now` 是引擎侧测试钩子，判据方里不存在**。
- **决断（非决断）**：**本轮不决断"协调事件要不要进持久审计"**。明确写下**理由**（不是留白）：
  1. **缺的是连接件，不是概念**：平台层已经算出 `needs_snapshot`，缺的只是把它接到 CRP hub 的 `/session/resume` 响应上。这个改动**同时触及判据方与引擎**，与 7.10.2 是**同一类改动**（必须一次落地），应合并进同一条新流，不应拆成两次 crosscheck 分歧。
  2. **它没有 owner**（`missing_owner` 成立）：把协调事件写进 Layer 意味着**轮子属于谁**必须由某次决断分配 —— 而 §7.6（`commitment:<id>` 命名空间碰撞）与 §7.10.2（scope 归属）都还没落地，先分配会导致返工。
  3. **今天可接受性论证**：协调事件**当前并不存在**（`src/` 里 `agent`/`commitment` 零命中），所以"协调事件不进持久审计"**损失为零** —— 没有事件可丢。**这个论证会随阶段 2/3 的实现到期**，届时必须重新决断（见 §8 阶段 5）。
- **⇒ 移交给下一轮的条件**：当且仅当 (a) 7.10.2 的 per-pair 授权已落地，且 (b) §8 阶段 3（承诺落 Layer）已实现 ⇒ 必须回来决断 §7.1。
- **`coverage`**：无（非决断）/ **`implementation`**：零 / **`evidence`**：E1 实测（引擎 `:692-708`、`:1176-1182`、`:1287`、`crp_hub.c:217`）+ E1（判据方 `:103-109`）。

#### 7.10.5 本轮**明确没做**的事（防误读）

- **没有改引擎任何一行**（`src/**` 零改动）；**没有改判据方** `tools/crp_relay.js`（所以 `tools/crp_engine_crosscheck.js` 仍 `115 records, text-identical`）；**没有新增帧类型、没有新增能力位、没有新增 cell 之外的存储**。
- **没有**把 `future/` 里任何内容表述为已实现；本轮四条全是**设计层决断**，`implementation` 一律为零。
- **没有**开新流：7.10.2 与 7.10.4 的落地需要新流（含 `CMakeLists.txt` 与 `tools/crp_relay.js` 改动），按规矩**先报协调者**，本轮只登记「可改的粒度 + 失败语义 + 顺序约束」。

---

## 8. 分阶段落地计划与验收标准

> **先设计、评审通过再实现。** 以下每阶段的验收命令都必须真的能跑出 `gate: OK` 且 exit 0。
> **门禁规则**（`tools/gate.sh` 七阶段：build / ctest / economy / node / plugin / links / doc-paths）：
> 新增测试必须同步 bump `tools/gate.sh:43` 的 `EXP_CTEST`（当前默认 95），并让 `grep -c "add_test(" CMakeLists.txt` 的计数吻合 —— 今天两者都是 95。同时按 `docs/BOARD.md` §3 与 `docs/STATUS.md` §1 的规则，**三处数字一起改**，否则 gate 的 ctest 阶段会因 `0 tests failed out of $EXP_CTEST` 不匹配而失败。

### 阶段 0：语义冻结（纯文档，零代码）
- **内容**：本文件；`docs/STATUS.md` 加一条路线图条目。
- **验收**：
  ```
  python3 tools/check_doc_paths.py    # 0 broken
  python3 tools/check_links.py        # 0 broken
  ```
  以及本文件每条结论都带 `coverage`/`implementation`/`evidence` 三标记（人工检查项，无自动检查）。
- **状态**：`specified` / E1。

### 阶段 1：SLIP 词法门禁（离线，无 wire 改动）
- **内容**：在 `tools/` 下加一个 SLIP v3 子集校验器（Node 或 Python，与既有 `tools/*.test.*` 风格一致）：12 值 Force 白名单、token 字符集 `[A-Za-z0-9]+`、agent id 1–20、payload ≤20、token ≤30、fallback ref 1–16、`parse(format(m)) == m` 的 roundtrip 断言。语料自建（不得引用外部仓库的自报测试数）。
- **验收**：
  ```
  tools/gate.sh            # 七阶段全绿，打印 gate: OK，exit 0
  grep -c "add_test(" CMakeLists.txt   # 与 tools/gate.sh:43 的 EXP_CTEST 吻合
  ```
  若走 `tools/node_suites/run_all.js`，则三处数字同步（该脚本目前 11/11）。
- **状态**：`planned`。

### 阶段 2：SLIP 行嵌入 `SIGNAL`（**预计零引擎改动**）
- **内容**：把 SLIP 行放进 `payload.data.slip`。因为 `data` 已是任意 JSON，**`crp.c` 无需改动**；本阶段的工作量全在测试与语料。
- **验收**：
  ```
  node tools/crp_engine_crosscheck.js ./build/verse_crp_probe
  # 语料从 115 条扩到含 SLIP 行的 N 条，且 text-identical（N 由新增语料决定）
  ./build/verse_crp_probe          # 检查项数增加且 0 failures
  INIM_CRP_HUB_BIN=./build/crp-hub python3 tools/crp_closed_loop.test.py ./build/crp-peer
  # 断言一条 SLIP 行经 hub 往返后在 resume 的 replay 里字形不变
  ```
- **状态**：`planned`。

### 阶段 3：承诺落 Layer（复用既有 `put`，不改 `layer.c`）
- **内容**：§3.2 的 5 态数值编码 + 幂等键 = 消息 id + `actor`/`role` 承载 debtor/creditor。
- **验收**：扩展 `verse_closed_loop`（今天 67 项）新增用例：
  1. 生成 → 兑现 → 违反三条记录依次落盘，`status` 读回终值；
  2. 用同一个幂等键重复 `put`，**不产生第二条记录**；
  3. 就地改写 `events.log` 中一条记录 → `hello` 与 `status` 返回 `recovery_required`，`drain` 返回 `drained:false`；
  4. 杀进程后重启 → `vl_layer_anchor_check()` 仍通过，`vl_layer_replay()` 重算哈希与线上状态一致。
  ```
  ctest --test-dir build -R verse_closed_loop --output-on-failure
  tools/gate.sh
  ```
- **状态**：`planned`。
- **门禁**：新增 CTest ⇒ bump `tools/gate.sh:43` 的 `EXP_CTEST` 并让 `grep -c "add_test(" CMakeLists.txt` 吻合。

### 阶段 4：UPP ↔ 承诺绑定（唯一可能真的需要新 C 代码的阶段）
- **内容**：§5.3 的绑定规则。最小实现可以在 hub/peer 侧加一条转发，而不必改 `crp.c`。
- **验收**：一个**真实子进程**测试（沿用 `verse_crp_closed_loop` / `verse_closed_loop` 的跨进程手法）：kill 掉一个 peer → 断言 Layer 里出现该宿主的 `value=3` 记录，且 `commit.head` 仍校验通过。同时加反例：心跳 `seq`/`timestamp` **相等**的合法帧不得被判成违约。
- **状态**：`planned`。

### 阶段 5：持久协调日志（**决策记录，可能结论是"不做"**）
- **内容**：解决 §7.1。两个选项：(a) 让 Layer 承载协调事件；(b) 明确写下"协调事件不进持久审计"并给出理由与剩余风险。
- **验收**：本文件新增一节决策记录，字段按 §67.7（context / options / chosen / rejected / invariants_preserved / new_risks / revisit_condition）。
- **状态**：`unresolved`。

**阶段依赖**：0 → 1 → 2 → 3 → 5；阶段 4 依赖阶段 3（无 Layer 承诺记录则无违反可写）。

---

## 9. 证据索引（可复现命令与原始输出）

### 9.1 引擎既有能力（本机实测，2026-10-02，main `e6e4936`）

```
$ ctest --test-dir build -R "crp|upp|json_min|verse_" --output-on-failure
100% tests passed, 0 tests failed out of 16
Label Time Summary:
crp           =   2.97 sec*proc (4 tests)
protocol      =   4.91 sec*proc (10 tests)

$ ./build/verse_crp_probe
crp_probe: 185 checks, 0 failures

$ node tools/crp_engine_crosscheck.js ./build/verse_crp_probe
crp_engine_crosscheck: 115 records, text-identical

$ node tools/upp_engine_crosscheck.js ./build/verse_upp_probe
upp crosscheck: ok (111 ops, engine and reference agree)

$ INIM_CRP_HUB_BIN=./build/crp-hub python3 tools/crp_closed_loop.test.py ./build/crp-peer
crp_peer: 31 exchanges, 0 failures
hub: {"ok":true,"served":1,"exchanges":30,"bye":true}
crp_closed_loop: 19 checks, 0 failures
```

同一份闭环记录里可直接观察到本设计的三个前提。**前两条已按 `crp-portal-auth`（`40f6094`）合入后的行为重写** —— 此前版本在这里写的是「`peer-b` 未做任何认证即取得令牌」，**那句话今天会失败**：

- **`/portal` 今天会拒绝未带证明的调用方**：
  - `{"op":"portal","payload":{"verse":"demo","peer":"peer-a"}}`（根本没有 `auth`）→ `403 {"error":"invalid enrollment proof"}`；`"auth":"not-a-proof"` → 同样 `403`。
  - 未注册的 verse、或缺少 `peer` → `404 {"error":"verse not found"}`（注册表检查在证明检查**之前**）。
  - 带正确证明 → `200`，返回 `{"token":"…","verse":"demo","peer":"peer-a","expires":…}`。
- **证明确实绑定 `(verse, peer)`，但它没有第三个因子**：同一个证明字符串 `Ya3OicHgEBEpv3igKkYFtZNKS4EhYmFeyj7hVKvALPY` 对 `peer-a` 是 `403`、对 `peer-b` 是 `200`（同一次运行里的第 22 与第 52 条交换）。绑定为真的原因是证明的输入**只有** `verse` 与 `peer` 两个字符串 —— `tools/crp_relay.js:29` 逐字是 `crypto.createHmac('sha256', enrollSecret).update(`${verse}\0${peer}`).digest('base64url')`。因此它证明的只是**调用方知道 hub 级共享 secret**；知道该 secret 的人对**任意** `(verse, peer)` 都能现算证明。这正是 §7.7 必须拆成「认证」与「授权」两句的原因。
- `{"op":"signal", ...}` 用**别人的**令牌 → `403 {"error":"invalid capability token"}`；令牌被 `revoke` 之后的 `signal` / `resume` 同样 `403` —— 拒绝是**状态码**，不产生事件记录（§2.2 / §3.3）。

### 9.2 结构性事实（源码，可复核）

```
$ grep -n "CrpEvent\|CRP_EVENT_WINDOW" src/verse/crp.c src/verse/crp.h
src/verse/crp.c:697:} CrpEvent;
src/verse/crp.c:703:    CrpEvent    events[CRP_EVENT_WINDOW];
src/verse/crp.c:1176:    if (s->nevents == CRP_EVENT_WINDOW) {
src/verse/crp.c:1179:        memmove(&s->events[0], &s->events[1], (CRP_EVENT_WINDOW - 1) * sizeof s->events[0]);
src/verse/crp.c:1182:    CrpEvent *ev = &s->events[s->nevents];
src/verse/crp.c:1246:    im_crp_session_resume_plan(&s->sess, (uint64_t)last_seq, CRP_EVENT_WINDOW,
src/verse/crp.h:52:#define CRP_EVENT_WINDOW            64

$ sed -n '491,494p' src/verse/crp.c        # crp_type_is_valid：只有三个帧类型
$ grep -n 'static const char \*DEFAULT_CAPS' src/verse/crp.c
355:    static const char *DEFAULT_CAPS[] = { "signal" };
363:    static const char *DEFAULT_CAPS[] = { "signal" };

$ grep -rn "commitment" src/ --include=*.c --include=*.h -i
（无输出，rc=1 —— 引擎里没有 commitment 概念）

$ sed -n '43p' tools/gate.sh
EXP_CTEST="${EXP_CTEST:-95}"
$ grep -c "add_test(" CMakeLists.txt
95
```

**行号漂移提示**：本设计的早期版本引用的 `crp.c:1102-1105`（事件环淘汰）与 `crp.c:1082`/`:1146`（令牌检查）**已经过期** —— 今天分别是 `crp.c:1176-1182` 与 `crp.c:1155-1156`/`:1220`。`:1082` 今天是「The portal's scope is a pair of non-empty strings…」注释，`:1146` 是「Map.has(p.verse): a non-string never matches a string key.」注释。引用行号时请以本节的实测为准。

### 9.3 外部系统事实的核实方式与落盘

外部仓库一律**按 commit SHA 固定**读取。`raw.githubusercontent.com` 在本机不可达（`curl` exit 35，"Recv failure: Connection reset by peer"，可复现），因此改用 GitHub contents API 带 `Accept: application/vnd.github.raw` 读取**同一提交的同一字节** —— 仍是同一份原始字节。arXiv / Zenodo / doi.org 正常。

复现方式（把 SHA 钉死，结果可逐字节比对）：

```bash
GH=https://api.github.com/repos
H='Accept: application/vnd.github.raw'
# Slipstream 规范与 ABNF（含 MUST NOT #2 原文）
curl -sL -H "$H" "$GH/anthony-maio/slipcore/contents/spec/spec-00-invariants.md?ref=2b5e0e8569c9aa275734c22195a40dde54bb0fd6"
curl -sL -H "$H" "$GH/anthony-maio/slipcore/contents/spec/slipstream-v3.abnf?ref=2b5e0e8569c9aa275734c22195a40dde54bb0fd6"
curl -sL -H "$H" "$GH/anthony-maio/slipcore/contents/LICENSE?ref=2b5e0e8569c9aa275734c22195a40dde54bb0fd6"
# G²CP 协议源码
curl -sL -H "$H" "$GH/karim0bkh/G2CP_AAMAS/contents/g2cp/protocol/messages.py?ref=e94f13d313c49f86129e23186a4f004fe983c098"
curl -sL -H "$H" "$GH/karim0bkh/G2CP_AAMAS/contents/g2cp/protocol/commitments.py?ref=e94f13d313c49f86129e23186a4f004fe983c098"
# 递归文件树（用来确认"六个入口都存在，没有遗漏"）
curl -sL "$GH/karim0bkh/G2CP_AAMAS/git/trees/e94f13d313c49f86129e23186a4f004fe983c098?recursive=1"
```

**这些原始字节没有留在仓库里。** 原因不是"仓库放不下"，而是两条：

1. 它们是**第三方材料**（两份不同许可证的源码与文档），本仓库的 `future/` 只保存自己的设计与愿景。
2. 实测过一次真实的门禁事故：把这批材料放进工作区（未跟踪）时，`tools/check_links.py` 报 **12 条 broken** —— 抓下来的第三方 README 里全是它自己仓库的相对链接。这个缺陷**已经被修掉**（本次工作期间，另一个会话提交了 `63c79c3 gate: make links/doc-paths enumerate git-tracked files, not the working tree`，把两个检查器改为用 `git ls-files` 枚举**已跟踪**文件，并明确拒绝回退到遍历工作树）。所以今天的规则是：未跟踪的临时目录不再影响门禁，但**已跟踪**的第三方材料仍会**永久**污染 links 阶段 —— 因此正确做法仍然是不把它提交进仓库。

核实过程的原始字节保留在仓库**外**（`/tmp/inimerse-verify-evidence/`，16 个文件），本文件的每条外部引用都可用上面的命令重新取回并逐字节核对。

> ⚠️ **一个必须记住的实操后果**：既然两个检查器只枚举 `git ls-files` 能看到的文件，那么**新增一篇文档后必须先 `git add` 再跑门禁**，否则 `check_links` / `check_doc_paths` 会直接**跳过这篇新文档**而依然打印 `0 broken` —— 绿灯是真的，但它没检查过你的文件。

**下文引用简写**（避免每条都重复 40 位 SHA）：
- `g2cp/protocol/*.py` = G²CP 仓库该路径 @ **`e94f13d3`**（`e94f13d313c49f86129e23186a4f004fe983c098`）。
- `README.md` / `LICENSE` / `spec/*` = slipcore 仓库该路径 @ **`2b5e0e85`**（`2b5e0e8569c9aa275734c22195a40dde54bb0fd6`）。

判定标注：§9.1 全部为 **validated / implemented / E4**（本机可复现）；§9.2 为 **implemented / E4**；§9.3 为 **E2**（按 SHA 读到的静态样例，未运行这两个外部系统）。

---

## 10. 外部事实核实结论（含被推翻的前提）

**方法即结论的一部分**：以下每条都给了固定 SHA 的一手来源。**没有一条引用外部自报的测试数量或性能数字。**

### 10.1 被推翻的前提（此前描述与实际不符，必须更正）

| 此前描述 | 实际情况 | 来源 |
|---|---|---|
| slipcore 是 **Apache-2.0** | **仓库内 `LICENSE` 正文是 MIT**（`MIT License / Copyright (c) 2024 SLIPCore Contributors`），GitHub 的检测器也报 `spdx_id: MIT`。但 README 第 6 行徽章、第 102 行与 `pyproject.toml` 三处都自称 Apache-2.0 —— **该仓库自相矛盾**。任何"它就是 Apache-2.0"的陈述都与随仓库发布的 LICENSE 文本冲突 | `LICENSE @2b5e0e85`、`README.md @2b5e0e85` |
| "**README 自述约 506 项**一致性测试" | **README 里完全没有出现 "test" 这个词**（`grep -niE "test" README.md` 零命中）。506 只出现在 Zenodo 论文描述里 | `README.md @2b5e0e85`、Zenodo 记录描述 |
| G²CP 作者只有 **Ben Khaled & Monticolo** | 对**论文**成立；对**软件**不完整 —— `LICENSE` 署名还有第三位 Maxime Mastagli | `LICENSE @2b5e0e85`、arXiv 2602.13370 |

### 10.2 已确认（可作为设计依据）

**Slipstream**（`anthony-maio/slipcore`，HEAD `2b5e0e8569c9aa275734c22195a40dde54bb0fd6`）：
- 线上格式 `SLIP v3 <src> <dst> <Force> <Object> [payload...]`，README 第 14 行与规范 MUST 规则 2 一致；MUST 2 原文："MUST contain exactly 6+ space-separated tokens … Messages with fewer than 6 tokens are invalid."
- **12 值封闭 Force**，在三个互相独立的位置完全一致：规范 MUST 规则 3、README 第 73 行、`spec/slipstream-v3.abnf` 的 `force` 规则（"This set is immutable within a major version."）。
- 全部词法约束确认：token `[A-Za-z0-9]+`（MUST 4）、agent id 1–20（MUST 5 + ABNF `1*20`）、payload ≤20 token（MUST 8 + ABNF `0*20`）、每 token ≤30 字符（MUST 7 + ABNF `1*30`）、Fallback ref 1–16 位字母数字（MUST 6 + ABNF `1*16`）、roundtrip `parse(format(msg)) == msg`（MUST 9）。
- **零核心依赖**仅对 `core` 包成立（MUST NOT 3 + `pyproject.toml` 的 `# ZERO DEPENDENCIES IN CORE` / `dependencies = []`）；`ml`/`a2a`/`langgraph` 是可选的额外依赖。引用时必须带 core 限定词。
- DOI `10.5281/zenodo.18063451` **可解析，但它是 concept DOI**；标题 *"Streamlined Interagent Protocol (Slipstream): Semantic Quantization for Efficient Multi-Agent Coordination"*，作者 Anthony Maio，2026-02-20，v3 preprint，CC-BY-4.0。
- **无 NOTICE 文件、无 COPYING、源码文件无许可头**。

**G²CP**（`karim0bkh/G2CP_AAMAS`，HEAD `e94f13d313c49f86129e23186a4f004fe983c098`）：
- arXiv **2602.13370** 存在，标题 *"G2CP: A Graph-Grounded Communication Protocol for Verifiable and Efficient Multi-Agent Reasoning"*，作者 Karim Ben Khaled、Davy Monticolo，2026-02-13 提交（仅 v1），cs.MA/cs.AI/cs.CL，CC-BY-4.0。论文 HTML 中两位作者的单位均为 **University of Lorraine, LORIA, Nancy, France**。
- `g2cp/protocol/` 的六个入口**全部存在，无遗漏**；`messages.py`（253 行）中九个类型**全部存在**。
- **Performative 是 7 个成员**（不是 3 个）：`REQUEST, INFORM, QUERY, PROPOSE, CONFIRM, REJECT, UPDATE`。`ReturnFormat` 三个：`SUBGRAPH, PATHS, LEAVES`。
- 三条承诺语义与描述**逐字符合**，且 `UPDATE` 的方向是 `C(receiver, sender, apply_if_valid(delta_G))`（字面字符串是 `delta_G`）。7 条完整清单见 §1 与 §3.1 引用的源码。
- 依赖是 **Python >=3.10 + `neo4j>=5.0.0` + openai + tiktoken + sentence-transformers + pydantic + pyyaml + numpy + rich + cryptography**，且 README 要求 OpenAI API key；`messages.py` 直接 `from pydantic import BaseModel`。此前描述"需要 Python 3.10+ 与 Neo4j 5.x"**不完整**。
- 数据计数一手核对：`data/queries/test_queries.json` 恰 **500** 条，`data/queries/real_world.json` 恰 **21** 条 —— 与摘要中的 500 / 21 吻合（这**只**证明数据文件行数，不证明实验结果）。
- MIT 是**文件正文**的说法；GitHub 自己的检测器对该仓库报 `NOASSERTION` / `other`。

### 10.3 明确不得引用为已验证

1. **"slipcore 是 Apache-2.0"** —— LICENSE 是 MIT。
2. **"Slipstream README 说约 506 项一致性测试"** —— README 里没有 "test" 这个词。
3. **"Slipstream 有 506 项一致性测试"** 作为可验证数字 —— 仅见 Zenodo 描述（自报）。本机结构计数是 **363** 个 `def test_*` 函数（未运行 pytest，参数化会抬高收集数，故 363 是下界）；一致性向量文件是 `valid.jsonl` 20 + `invalid.jsonl` 17 + `roundtrip.jsonl` 10 = **47** 条；而仓库自己的提交信息说 "594 passing"。**506 无法从源码复现**。
4. **"README 说 token 压缩 (41.9 → 7.4) / 82%"** —— 该数字在 Zenodo 描述里，不在 README；无论如何都是自报。
5. **"~40+ token 压缩到 6–8 wire token"作为实测结果** —— README 第 17 行的句子确实存在，但**没有任何基准或协议**支撑，是裸主张。
6. **"G²CP 被 AAMAS 2026 接收"** —— arXiv 元数据里没有；只有仓库徽章与它自己的 BibTeX。**仅作者自述**。
7. **G²CP 的 73% / +34% / 消除级联幻觉 / 完全可审计的推理链** —— 摘要自报；本机未运行 `evaluation/run_evaluation.py`。README 的 "Key Results" 表（准确率 0.90 vs 0.67/0.74/0.71 等）同样是作者自报。
8. **"G²CP 作者 = Ben Khaled & Monticolo"不加 Mastagli** —— 对软件不完整。
9. **"GitHub 认为 G²CP 是 MIT"** —— GitHub 报 `NOASSERTION`。
10. **任何关于 slipcore 的 NOTICE 信息** —— 该文件不存在。
11. **引用时效**：本设计的引用一律按 SHA 固定，因为两个仓库都在活跃改动（slipcore 的 `pushed_at` 晚于其 master HEAD）。

### 10.4 与本设计的关系

- §2、§3、§5 的 G²CP 语义全部来自按 SHA 读到的源码（`messages.py`、`commitments.py`），**不是**来自论文摘要。论文摘要里的性能数字在本设计中**一律未使用**。
- §4 的 SLIP 词法全部来自规范原文（`spec-00-invariants.md` + ABNF），**不是**来自 README 的转述。
- G²CP 的 `serialize()` 有损这一条（§4.2）是本文件对两个系统关系的**核心论点**，它来自读过 `g2cp/protocol/serializer.py @e94f13d3` 与 `messages.py` 的源码，不是推测。
