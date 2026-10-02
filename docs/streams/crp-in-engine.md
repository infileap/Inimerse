# 流简报：`crp-in-engine`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/crp-in-engine`，工作树 `.worktrees/crp-in-engine`，
> 冲突域 `src/verse/`、`src/common/`、`src/mod/verse_dist_mod.c`。

## 0. 先纠正一件事：引擎**不是**从零开始

`docs/BOARD.md` 那一行原来写「引擎侧无实现」——**这句话是错的**，写本简报时已更正。
引擎里已经有一份相当完整的 CRP **会话**状态机：

| 文件 | 行数 | 覆盖 |
| --- | --- | --- |
| `src/platform/crp_session.h` | 109 | `ImCrpState` 11 态、能力位、`ImCrpHello` |
| `src/platform/crp_session.c` | 220 | 实现 |
| `src/platform/crp_session_probe.c` | 90 | 已有断言 |

已有的能力（**不要重写**）：

- 状态机 `IM_CRP_IDLE/RUNNING/STOPPED/CRASHED/INCOMPATIBLE` ＋ §55.6 生命周期
  `DEGRADED / DISCONNECTED_GRACE / REATTACHING / RESUMED / READ_ONLY / EXPIRED`
  （「掉线不等于离开世界」就是这一段）。
- `im_crp_session_hello(my_version, my_caps, peer_version, peer_caps, &out)` 版本 + 能力协商，
  不兼容**显式报错**（§24.6：不许假装兼容）。能力位 `EVENTS / SNAPSHOT / UDP / RELAY`。
- `lease_begin` / `lease_touch` / `lease_expired`（默认 30 s 租约）。
- `accept()`：1 = 接受、0 = 重复（可安全丢弃）、-1 = 缺口（需重同步）。
- `resume_plan()`：按落后程度决定**重放 vs 快照**。
- `disconnect()` / `grace_expired()` / `reattach_plan()`；权威换代后只给快照，绝不给重放。

**你要补的是它外面那一层**：线帧格式、能力令牌、FIND 注册表、PORTAL 签发。

## 1. 要交付什么

新增（命名对齐 `src/verse/upp.{h,c}` 那一套）：

- `src/verse/crp.h` / `src/verse/crp.c` —— CRP 帧编解码 + FIND 注册表 + 能力令牌
- `src/verse/crp_probe.c` —— 进程内断言探针
- `tools/crp_engine_crosscheck.js` —— 与 JS 参考实现的**逐行文本比对**（照抄
  `tools/upp_engine_crosscheck.js` 的做法：用参考实现**自己的构造器**造语料）
- `CMakeLists.txt` 注册探针与 CTest 用例

## 2. 帧格式（照抄 `tools/crp_reference.js`，54 行，先读完再写）

- `TYPES = {FIND, PORTAL, SIGNAL}`，`MAX_FRAME_BYTES = 1 MiB`；**一帧一行**（JSONL）。
- 帧形状 `{crp:1, type, payload}`；`id` 为空时**整个键不输出**（不是 `id:""`）。
- `frame()`：type 不在 `TYPES` 里报 `unsupported CRP type: <type>`；`payload` 必须是对象
  （数组/null/标量都拒绝）。
- 构造器各自的校验：
  - `FIND(query, {limit, cursor, id})`：`query` 必须是字符串；`limit` 默认 50，整数且
    **1..1000**，否则 `FIND limit must be 1..1000`；`cursor` 默认 `null`，否则必须是字符串。
  - `PORTAL(verse, peer, {token, expires, id})`：`verse`/`peer` 必须是非空字符串
    （`!value.trim()` 也算空）；`token` 默认 `null`、`expires` 默认 `null`。
  - `SIGNAL(verse, event, data = {}, {timestamp, id})`：`verse`/`event` 非空字符串；
    `data` 必须是对象；`timestamp` 默认**当前时间**。
- `encode()`：先校验 `crp === 1` 且 type 合法（否则 `invalid CRP frame`），再按
  **UTF-8 字节**判 1 MiB，超了报 `CRP frame exceeds 1 MiB`，末尾补 `\n`。
- `decode()`：同样先判字节数，再 `JSON.parse`，再校验 `crp === 1` 与 type。

## 3. 能力令牌（照抄 `tools/crp_relay.js:18-19`，这是「签名校验」的实体）

```js
makeToken(verse, peer, capabilities = ['signal']):
  exp   = Date.now() + tokenTtlMs          // 默认 5*60*1000
  body  = base64url(JSON.stringify({verse, peer, capabilities, exp}))
  sig   = base64url(HMAC-SHA256(secret, body))
  token = `${body}.${sig}`

checkToken(token, verse, peer, capability):
  按 '.' 切成 [body, sig]
  expected = base64url(HMAC-SHA256(secret, body))
  timingSafeEqual(sig, expected)           // 长度不等即 false
  且 p.verse === verse && p.peer === peer
  且 p.exp > Date.now()
  且 p.capabilities.includes(capability)
```

**三条必须照做的保真细节**（写错任何一条都会与参考实现对不上）：

1. `secret = options.secret || crypto.randomBytes(32).toString('hex')`，
   即默认密钥是 **64 字符十六进制字符串**，HMAC 的 key 是这个字符串的 **UTF-8 字节**，
   **不是**把它 hex 解码后的 32 字节。测试里要给固定 secret 才能比对。
2. 用的是 **base64url**（`-`/`_`，**无 `=` 填充**），不是 `src/common/vverse_pack.c` 里那份
   **标准 alphabet 带填充**的 base64。引擎现在**没有** base64url：
   `grep -rn base64_ src/` 零命中，只有 `src/common/vverse_pack.c:169-215` 与
   `src/mod/verse_dist_mod.c:75-106` 两份 **static** 的标准态编解码。
   你想抽公共 `src/common/b64.{h,c}` 可以，但**必须同时保住两处现有语义**
   （`vverse_pack.c` 那份是**严格**解码：非规范 padding 要拒绝），
   更保守的做法是 base64url 先单独放在 `crp.c` 里。
3. `crypto.timingSafeEqual` 在长度不等时**抛异常**，参考实现用 `try/catch` 兜成 `false`。
   C 侧：先比长度，不等直接失败；再逐字节异或累积，**全程不早退**。

引擎侧可用的原语：`src/common/sha256.h` 有 `sha256_init/update/final`（流式）与
`sha256_digest`（一次性）——HMAC 直接用流式那套写（ipad/opad 两轮），
**不要**用一次性函数拼长输入。**引擎里没有 HMAC**（`grep -rni hmac src/` 零命中），
所以 HMAC-SHA256 要你实现，放 `src/common/` 还是 `crp.c` 由你判断并写进提交说明。

## 4. 回环语义（`tools/crp_relay.js` 的 CRP 子集）

口径以 `tools/crp_relay.test.js`（39 行）为准，它逐条钉了下面每一条。**只做 CRP 相关的**：

| 行为 | 规则 |
| --- | --- |
| 注册 | `id` 与 `endpoint` 缺任一即 400；成功记 `updated` 时间戳 |
| FIND | 先按 `registryTtlMs`（默认 30 min）**剔除过期项**；匹配规则是 `id.includes(q)` **或** `name.toLowerCase().includes(q.toLowerCase())`；**空 q 返回全部**；返回体里**去掉 `updated`** |
| PORTAL | verse 不存在或 peer 缺失 → 404 `verse not found`；否则签发，返回 `expires = now + tokenTtlMs` |
| SIGNAL | verse/event 缺失 → 400；verse 未注册 → 404；**带 token 时**：token 在吊销集里 **或** `checkToken` 失败 → 403 `invalid capability token`；`seq` 若已是**安全整数且 ≥ 0** 就用它，否则用 `sessions[key] + 1`；key 是 `verse + "\0" + (peer ?? "")`；每 session **只保留最近 64 条**事件 |
| resume | 缺 verse/peer/token → 400；token 无效或被吊销 → 403；`seq` 默认 0；**`seq < prev` 且未置 `replay` → 409** `session sequence out of order` 并回 `lastSeq`；`seq > prev` 才推进；回 `{resumed, lastSeq: max(prev, seq), replay: 事件中 seq > 请求 seq 的那些}` |
| revoke | 成功后所有校验失败；吊销集**有上限**（默认 10000），超了按**插入顺序**淘汰最旧的 |

**不属于本流**（`crp_relay.js` 里有，但与本流的冲突域和判据都无关）：
`/friends`、`/route`、`/nat/*`、`/content*`、`/package*`、`/ws` 的 WebSocket 中继。
`/content` `/package` 的服务端截断问题另有 `hub-large-package` 一行管。

**要复用而不是重写的**：`seq` 的去重/缺口判定、`resume` 的重放 vs 快照决策、
租约与生命周期，都已经在 `src/platform/crp_session.c` 里。注册表/令牌这一层应当调用它，
而不是再写一份 —— 如果发现接口不够用，改 `crp_session.*` 并在交接说明里写清楚为什么。

## 5. 验收判据

1. `tools/crp_relay.test.js` 的**等价场景**在引擎侧跑通 —— 逐条对照它的断言，
   而不是「我自己写了一个测试且它过了」。对照清单写进交接说明。
2. **真实两进程证据**，形状对齐 P1 Layer 那套
   （`build/inim-server` ↔ `build/inim-client`，canonical JSON 行走 stdin/stdout，
   端到端脚本 `tools/verse_closed_loop.test.py` 是范例）：
   一个进程持注册表与密钥，另一个进程 FIND → PORTAL → SIGNAL → 被吊销后 SIGNAL 被拒。
   贴上**逐字的协议转写**，不要只写「通过了」。
3. `tools/crp_engine_crosscheck.js` 与 JS 参考实现**逐行文本比对**一致。
4. 探针在 **ASan + UBSan** 下干净（照抄 UPP 流的做法；`-DCMAKE_C_FLAGS="-fsanitize=address,undefined"`）。
5. 定向门禁：`ctest --test-dir build -j4`（本流合并前应是 **89/89**，加你自己的用例会变多）、
   `node tools/node_suites/run_all.js`（**11/11**）、
   `python3 tools/check_links.py` 与 `python3 tools/check_doc_paths.py` 均 0 broken。

## 6. 已踩过的坑（别再踩一遍）

- **`src/verse/json_min.c` 只收整数**：遇 `.`/`e`/`E` 报 `non-integer number unsupported`。
  CRP 的 `timestamp`/`exp` 参考实现用 `Date.now()`（毫秒整数，安全），
  但 `SIGNAL` 的 `data` 里若有小数就会**整行帧被拒**。`upp` 流为此把 `timestamp` 限制成整数
  并把这条写进了「没做什么」。**本流要面对同一个问题**：要么同样明确记录该限制，
  要么把 `json_min` 的数字语法扩展成支持小数（**风险**：它同时被
  `layer`/`eventlog`/`protocol` 的 canonical JSON 与 `state_hash` 复用，改了有破坏事件日志契约的风险）。
  无论选哪条，写清楚。
- **`VL_AUTHORITY_FIELDS[] = {seq, rev, head, balance, state_hash, committed}`** 客户端送即
  `client_authority`。注意 CRP 有自己的 `seq`（应用层），**别和 Layer 的 `seq` 混**。
- 语言层 `verse_verify(data, sig, pub)` 与 C 层 `ed25519_verify(pub, msg, msglen, sig)`
  **参数顺序相反**。
- 长消息哈希用流式 `Sha512Ctx` + `sha512_init/update/final`，**不要**用一次性 `sha512_buf`
  （>8192 字节静默截断）。
- HTTP body 解析从 `strstr(req, "\r\n\r\n") + 4` 起 —— 若你走 HTTP，别从偏移 0 解析。
- `tools/` 下的 `crp_*.js` 是**只读参考实现**，不要为了迁就引擎去改它们。

## 7. 不要做

- 不改 `docs/archive/`、`future/archive/`。
- 不改别人的冲突域（`src/common/vverse_pack.c`、`src/platform/http_posix.c` 的 hub 部分）。
- 不为了「让测试过」而放宽任何校验。参考实现报错的地方，引擎也要报错。
- **只 commit，不 push；不许合 `main`。**
