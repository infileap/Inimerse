# 作业单：`portal-per-pair` —— per-`(verse, peer)` 授权（决断 ② 的实现）

**状态**：进行中
**分支**：`stream/portal-admission`（worktree `.worktrees/portal-admission`），base `main@58e1a6b`
**认领**：eager-juniper（协调者）
**来源**：`docs/BOARD.md:130` [obs: 58e1a6b] `agent-bridge-round2`（`63fef73`）决断 ②，板上原话：
> per-`(verse, peer)` 授权：**收**，最小形态 = 把 `/portal` 证明输入从「两个自选字符串」扩为「hub 持有的 per-pair 注册项」（per-pair secret + per-pair `capabilities`）；并**发现一条新约束**：未登记 pair 与证明错**必须不可区分（都 403）**，否则 404 变成枚举 oracle。判据方与引擎必须同一次改动落地，故本轮零代码。

**冲突域（别人别碰）**：`tools/crp_relay.js`、`tools/crp_relay.test.js`、`tools/crp_engine_crosscheck.js`、`src/verse/crp.c`、`src/verse/crp.h`、`src/platform/http_posix.c`、`tools/crp_client.js`、`tools/crp_session.test.py`、`tools/crp_closed_loop.test.py`、`tools/crp_wire_neg.test.js`、`tools/reconnect_generation.test.py`、`tools/node_suites/crp_session_flow.test.js`、`tools/node_suites/crp_client.test.js`、`docs/streams/portal-per-pair.md`
**禁碰**：`src/verse/client.c`、`src/verse/server.c`（UPP 传输层刚落地，别回去踩）、`src/common/**`（`vj_get` 的 R2 重复键问题本波硬禁碰，见 `docs/STATUS.md:1043` [obs: 58e1a6b]）、`docs/AUDIT.md`、`tools/vverse_*`、`src/mod/verse_dist_mod.c`

> 这不是重开 `crp-portal-auth`（`40f6094`）或 `crp-portal-postcheck`（`d565253`）的决定。那两条交付的是**调用方认证**（知道 hub 级共享 secret）与 scope／注册表收紧，并且**有意没做** per-pair 授权——把这条写成了决断 ② 的「收」。本流补的就是那一半。**wire 格式不变**。

---

## 1. 今天的机制（开工前自己复现并贴读数）

- 判据方 `tools/crp_relay.js`：`enrollProof(verse, peer) = HMAC-SHA256(CRP_ENROLL_SECRET, verse + "\0" + peer)`（`:27`），`enrollOk` 只比这一个值（`:30`）。⇒ **知道那把全局密钥 = 可以为任意 pair 算证明**。
- 引擎：`src/verse/crp.c:1057` [obs: 58e1a6b] `crp_enroll_check(enroll_secret, verse, peer, auth)` —— 四个输入全是「库给的全局密钥 + 请求里的两个自选字符串」。
- 监听器 `/portal`（`src/platform/http_posix.c:2060-2102` [obs: 58e1a6b]）：`:2070` 未配密钥 ⇒ `403 {"error":"portal enrollment is not configured"}`；`:2071` 证明错 ⇒ `403 {"error":"invalid enrollment proof"}`；`:2083` verse 非字符串／未注册 ⇒ `404 {"error":"verse not found"}`；`:2087-2097` 铸 `posix-<hex>` + `expires=now+ttl` + `token_register(issued, now+ttl, scope_verse, scope_peer)`。
- `g_verses[128][128]`（`:75`）由 `/register` 运行时填（`:1988-1989`），不验身份、无所有者；`portal_verse_registered()`（`:313-317`）只 `strcmp`；peer 是任意非空串。⇒ **认证成立，授权不成立**：今天不存在「这一对被允许」这个事实。

复现三条（开工前贴给我）：
1. 带 `CRP_ENROLL_SECRET=X` 起 relay，为 `(demo,p1)` 与 `(demo,p2)` 各算 proof ⇒ **两条都 200**（要收的缺口）。
2. 不配 `CRP_ENROLL_SECRET` ⇒ 全 403（既有 fail-closed 先例，`tools/crp_relay.test.js:66` [obs: 58e1a6b]）。
3. 引擎侧同两条都 200。

## 2. 设计（最小形态，不改 wire 形状）

准入表 = 一组 `(verse, peer) -> { secret, capabilities }`，**由 hub 持有**。

- 证明式形状**不变**：`base64url(HMAC-SHA256(pair_secret, String(verse) + "\0" + String(peer)))`。变的只有密钥来源：一把全局密钥 → 这一对登记的那把。客户端逻辑不必改，改的是它拿哪把密钥。
- **`CRP_ENROLL_SECRET` 变成死的事实**：表一旦启用，它**不再参与任何判定**（env 在场也不放行未登记 pair）。这是不兼容变更，写进 §8 与 STATUS，不许只说「迁移后客户端不用改」。
- **常时比较两侧都做**：文案相同、耗时不同一样是 oracle。
- 表来源**只有一个，选定之后跨级也不许回退**（协调者裁定，比「同级不混用」更硬）：`options.admissions`（仅嵌入/测试）→ 环境变量 `CRP_ADMISSIONS_FILE` → hub 目录下的 `portal_admissions.tsv`。一旦选中某个来源，**读不到就是空表全拒**（日志 `cause=admissions-unreadable`），**绝不降级到下一个来源** —— 否则「文件配错 ⇒ 悄悄用默认路径 ⇒ 拿旧表放行」是最坏的一种静默。
- **hub 目录只有一个来源**：进程启动时确定的 root，或 `INIMERSE_HUB_DIR`，二者取其一；**判定路径里不许猜 cwd**。解析不到 ⇒ 空表全拒。
- **同一个 `(verse, peer)` 写两行 ⇒ 整份文件作废 + 全拒 + 日志 `cause=admissions-duplicate`**。不做 first-wins 也不做 last-wins：两行两把 secret 的含义是「轮换做到一半」或「有人手滑」，两种都该让运维看见。`R2`（`vj_get` first-wins vs `JSON.parse` last-wins）就是同一形状的病，别在新代码里再种一次。
- **未知 `capabilities` 值 ⇒ 整份作废**（fail-closed）。不许静默接受垃圾值再说「反正本轮不参与判定」。
- 加载时 `stat` 一次权限：**group/other 可读 ⇒ 只记日志 `cause=admissions-perms-loose`，不拒绝**。明文 secret 落盘是诚实边界，那就让运维看得见自己的文件是 644。
- **两侧解析必须逐字一致**：JS 与 C 各写一遍 TSV 解析，这是本流最大的分歧源；`tools/crp_engine_crosscheck.js` 语料要加**解析边角组**：空行、`#` 注释、CRLF、行尾空格、字段不足 4、`capabilities` 空字段、未知 capability 值、secret 含空格（不含 tab）。
- 文件格式（TSV；`#` 注释、空行忽略）：`verse \t peer \t secret \t capabilities(逗号分隔，可空)`。**本轮 `capabilities` 只解析、不参与判定**（见 §8.1）。
- 热加载：每次 `/portal` 先 `stat`（`mtime`+`size`），变了才重读；**读失败 ⇒ 空表（fail-closed）**，日志记 `cause=admissions-unreadable`。不保留上一版——那会让写坏的文件静默续命旧权限。

## 3. 语义（两条，必须同时成立）

1. **空表 ⇒ 全拒**（用户裁定）。表缺失、空文件、只有注释、读失败，全等价于「一对都没登记」。**默认起法就拒**，不许靠测试里手工塞表「制造」这个结论。
2. **未登记 pair 与证明错不可区分。** 状态行、响应头、响应体**逐字节相同**（同一个 `403 {"error":"invalid enrollment proof"}`）。成因只进日志：`cause=unregistered-pair` vs `cause=bad-proof` 是**日志里**的差别，响应里不出现。也不许把 `404 verse not found` 扩用成侧信道：**先判准入，再判注册表**，未登记 pair 连「这个 verse 是否存在」都不能泄漏。

## 4. 判据（正向 5 条；每条负向 403 都要有一个 200 读数陪着）

1. **空表全拒 + 逐字节不可区分**：不带任何表起 relay（默认起法），`(demo,p1)` 用旧全局密钥算出**合法形状**的 proof ⇒ `403`。再把同一请求改成 proof 末尾翻一位（证明无效）⇒ 再一个 `403`。抓两份响应的**原始字节**整体比对，必须相同。**比较口径写死**：状态行 + **除 `Date` 外的全部响应头** + body，逐字节；`Date` 单独断言格式合法（秒级时间戳，两次请求跨秒必不同，不排除它就是一条会在 CI 上间歇红、然后被「顺手放宽」的判据）。`fetch` 拿不到状态行与原始头，用裸 socket 或 `http.request` 读 `rawHeaders` + body 缓冲。**不许只断言 `status === 403`**。
2. **元判据（证明上面那条断言是活的）**：把拒绝文案改成 `pair not admitted` 后重跑 ⇒ 判据 1 必须**红**。同时断言日志里 `cause=` 两个值分别出现（证明「不可区分」不是靠把信息全丢了换来的）。
3. **登记后同一 pair 由 403 变 200**：按 §6 写一行（secret = 升级前那把全局密钥），同一请求 ⇒ `200`，`verse`/`peer` 回显逐字相同、`expires ∈ [now, now+ttl]`。**这条要真跑，不许写在注释里当承诺。**
4. **未登记 pair 一律 403（哪怕持有旧全局密钥）**：`(demo,p2)` 用旧全局密钥算出合法形状 proof ⇒ 403，且字节与判据 1 相同。
5. **拒绝发生在铸币之前**：403 之后该 pair 没有进 token 表 —— 用「该 pair 未获取任何 token」的可观测方式证实（token 计数端点，或拿一个未被签发的名字打 `/signal` ⇒ 403），不是「拒了但已进表」。
6. **两侧同答**：引擎 hub 与参考 relay 对同一语料逐字相同 —— 沿用 `node tools/crp_engine_crosscheck.js build/verse_crp_probe` 的记录法，**今天 115 records text-identical**，本流要给出新数字并说明为什么。

## 5. 反向判据（至少四条，每条一个真跑读数；「看起来会红」不算）

1. **删准入检查**（表当永远通过）⇒ 判据 4 红：持旧全局密钥的未登记 pair 变回 200。
2. **先铸后拒**（`token_register` 排在准入判定之前）⇒ 判据 5 红：令牌已进表，能打 `/signal`。
3. **把两种失败文案区分开**（未登记改成 `pair not admitted`）⇒ 判据 1 的字节比较与判据 2 同时红。这条专测「不可区分」约束本身，是本流最容易糊过去的一条。
4. **一刀切全拒**（表非空也拒，连已登记 pair 也 403）⇒ 判据 3 红。**这条是为了防止「为了安全把功能整个关掉还宣称通过」。**
5. **控制留读数**：删实现看红、恢复看绿，照 UPP transport 那次的三个控制做法。

## 6. 迁移路径（**交付物，不是免责声明**）

空表 fail-closed 是用户裁定，所以「升级即断」是有意的；迁移步骤就是把「断」变成「照做就恢复」，且**恢复与否可判定**。

1. 取到升级前在用的 `CRP_ENROLL_SECRET`。
2. 为今天在跑的每一对写一行：`demo<TAB>p1<TAB><旧全局密钥><TAB>signal` —— **secret 先沿用旧全局密钥**，于是已经在算 `HMAC(旧密钥, verse\0peer)` 的客户端**一个字节都不用改**。
3. 重启 hub（或等一次 `stat` 变化；relay 与引擎都按 `stat` 重读，文件热加载不需要重启进程）。
4. 判据：升级前 200 的 pair，照做后**同一请求仍 200**；任何没写进表的 pair 即使知道旧全局密钥也 403。
5. 之后按需逐对轮换 secret（本轮**不做**轮换工具，只说明「改这一行 + 同步给该 peer 的客户端」）。
6. **把 `CRP_ENROLL_SECRET` 从环境里撤掉。撤掉是收尾，不是可选** —— 留着它虽已不参与判定，但会让人以为还有一条全局通道。

## 7. 写域与门禁口径

- **判据方先改**：`tools/crp_relay.js`（`enrollProof`/`enrollOk`/`/portal` 同笔扩成 per-pair 注册项）+ `tools/crp_relay.test.js` 新用例。
- 引擎跟随：`src/verse/crp.{c,h}`（新增 `crp_admission_lookup`；`crp_enroll_check` 签名不变、内部按 pair 取密钥）、`src/platform/http_posix.c`（顺序：准入 → 注册表 → 铸发；两处拒绝共用**一个** body 常量）。
- 语料：`tools/crp_engine_crosscheck.js` 增加「登记 / 未登记 / 错证明」三组记录；**分歧数不许靠放宽 crosscheck 掩盖**。
- 既有套件补登记表（它们今天都用全局密钥算证明）：`tools/crp_session.test.py`、`tools/crp_closed_loop.test.py`、`tools/crp_wire_neg.test.js`、`tools/reconnect_generation.test.py`、`tools/node_suites/crp_session_flow.test.js`、`tools/node_suites/crp_client.test.js`、`tools/crp_client.js`。
- 同笔：`tools/gate.sh` 的 `EXP_CTEST`（只按新增注册数加）、`CMakeLists.txt` 注册、`docs/STATUS.md` §7.7 那句「只做认证不做逐 (verse, peer) 授权」改到事实、`docs/BOARD.md` 本行状态。
- 门禁：`./tools/gate.sh --jobs 4`（**禁 `--fast`、禁搬 `.verify`**）全绿；`EXP_LINE_REFS_UNANCHORED_DELTA_MAX=12` **余量为零**——要动行号就重钉 citations pin，**不许用 `citations.exempt.json` 豁免**。

## 8. 诚实边界（落地时写进 STATUS，不许事后补）

1. **`capabilities` 本轮不参与判定**：只解析、留在条目里占位。token 的 `capabilities` 仍由签发方决定。别在文档里说成「已能按能力授权」。
2. **表里的 secret 是明文**：文件权限就是边界，没有加密、没有 HSM、没有轮换工具。谁读得到文件，谁就能为那一对铸令牌。
3. **准入表是运维提供的静态事实**：无所有者、无审批流、无租约与吊销。§7.7 里说「权限的第一块」，不说「权限做完了」。
4. **`R2` 重复 JSON 键仍不调和**（`vj_get` first-wins vs `JSON.parse` last-wins），`src/common/**` 本波硬禁碰，本流不碰。
5. **Windows 半侧只编译不运行**（判定面是 HMAC + 文件读取，但只有 POSIX 树跑测试）。
6. **`CRP_ENROLL_SECRET` 的行为变更**：启用准入表之后它不再参与判定，且**撤掉它是迁移收尾**（不是可选）。
7. **热加载一致性边界**：`stat` 变化才重读，两次 `/portal` 之间可能落在旧表上；这是有意的（不引入锁），但要写在文档里。
