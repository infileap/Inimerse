# 作业单：`crp-portal-auth` —— 给能力令牌的签发加上调用方认证（**先改判据方**）

**状态**：**已交付并合入 `main`（`40f6094`，fast-forward `922f9af..40f6094`）** —— 见 [STATUS.md](../STATUS.md) §10.12。第一版（`e47f903`）被驳回，原因与三处被证伪的分歧都记在那里；遗留四点登记为 `crp-portal-postcheck`（`task-9`）。本作业单保留原样，作为该流的判据记录，**不要按它重新开工**。
**冲突域（别人别碰）**：`tools/crp_relay.js`、`tools/crp_reference.js`、`tools/crp_relay.test.js`、`tools/crp_engine_crosscheck.js`、`tools/crp_closed_loop.test.py`、`tools/crp_session.test.py`、`tools/crp_session_flow.test.js`、`tools/crp_client.js`、`tools/crp_client.test.js`、`src/verse/crp_hub.c`、`src/verse/crp.c`、`src/verse/crp.h`、`src/verse/crp_probe.c`、`src/verse/crp_peer.c`
**禁碰**：`tools/gate.sh`、`CMakeLists.txt`（`vversecli` 持有）、`tools/vverse_*`、`src/mod/verse_dist_mod.c`、`src/common/**`、`docs/**`（协调者持有）、`tools/node_suites/**`

---

## 1. 缺陷（协调者实测，你开工前要自己复现）

`POST /portal` 是 CRP 的**唯一授权入口**：拿到令牌之后，`/signal`、`/session/resume`、`/session/stop`、`/ws` **全部只认令牌**（`tools/crp_relay.js:70`、`:77`、`:83`、`:137`）。也就是说**令牌本身就是全部授权**。

而这个令牌**任何人都能索取**：

```js
// tools/crp_relay.js:63-65
if (req.method === 'POST' && req.url === '/portal') {
  const p = await read(req); if (!verses.has(p.verse) || !p.peer) return json(res, 404, { error: 'verse not found' });
  const token = makeToken(p.verse, p.peer); return json(res, 200, { token, verse: p.verse, peer: p.peer, expires: Date.now() + tokenTtlMs });
}
```

只检查「verse 已注册且 `peer` 非空」，**不看调用方是谁**。引擎侧 `src/verse/crp_hub.c:240-241` → `crp_registry_portal`（`src/verse/crp.c:1017`）**行为一致**；唯一的前置 `verify_frame`（`src/verse/crp_hub.c:158-187`）是**自洽性**检查（客户端自带的 `frameText` 的 `type` 与 payload 字段须与请求一致），**不含签名、不含身份**。

**危害不止「拿到令牌」**：`crp_registry_portal` 还会 `registry_get_session`（`src/verse/crp.c:879-899`，**不存在就新建**）并 `im_crp_session_apply("start")` + `im_crp_session_lease_begin`（`:1036-1041`）。⇒ 未认证调用方可以：

1. 用任意 `peer` 字符串**无上限地造 session**（内存 DoS）；
2. 若该 `(verse, peer)` 已有会话，**重置并夺取租约**（会话劫持）。

## 2. 为什么这是契约级改动（**必须先改判据方**）

引擎忠实实现了参考实现，所以**这不是引擎实现缺陷**。本仓库的判据是「参考实现是裁判」（见 `src/common/vverse_pack.h` 抬头的同类表述），`tools/crp_engine_crosscheck.js` 会把引擎与参考实现的输出**逐行文本比对**。⇒ 单方面给引擎加认证，会立刻被 crosscheck 判为分歧。

**用户已决断：先改参考实现，再让引擎跟随。** 这是破坏性的契约变更，必须两边同时改、语料同时更新。

## 3. 要做什么

1. **参考实现先改**（`tools/crp_relay.js`）：`/portal` 必须要求调用方证明它有权为 `(verse, peer)` 开 portal。
   - 建议机制（可细化，但要保持仓库既有的 HMAC 惯用法）：新增一个 hub 侧预共享的 **enrollment secret**（与既有 `options.secret` 区分开，别复用同一个值），请求须带 `auth`，值为 `base64url(HMAC-SHA256(enrollSecret, verse + "\0" + peer))`；hub 用 `crypto.timingSafeEqual` 校验后才签发。
   - **默认必须 fail-closed**：未配置 enrollment secret 时 `/portal` 返回 403，而不是「默认开放」。安全默认值不能靠调用方记得打开。
2. **引擎跟随**：`crp_registry_portal`（`src/verse/crp.c:1017`）做同样的校验；`src/verse/crp_hub.c` 的 op 分派把 `auth` 传下去；校验用既有的 ed25519/HMAC 或 `src/common/` 里已有的原语，**不要新引加密依赖**。
3. **同步所有既有套件**：`tools/crp_relay.test.js`、`tools/crp_closed_loop.test.py`、`tools/crp_session.test.py`、`tools/crp_session_flow.test.js`、`tools/crp_client*.js` 里凡是走 `/portal` 的地方，都要配置 enrollment secret 并带上 `auth`。**它们必须仍然全绿** —— 门禁不能因为你改契约而变红。
4. **更新 crosscheck 语料**：`tools/crp_engine_crosscheck.js` 必须把「带正确 auth」「带错误 auth」「不带 auth」三种情形都纳入，并**逐行文本一致**。
5. **负面测试是必须的**（不是加分项）：不带 `auth`、`auth` 错、`auth` 对但 `(verse, peer)` 不匹配 —— 三种都必须被拒，且**不得**产生任何 session 或租约副作用。要断言 session 计数没有增长。

## 4. 验收判据

- [ ] **修前必须失败**：新的负面测试在**未改动**的参考实现/引擎上**必须失败**（即：未认证请求现在真的能拿到令牌）。贴原始输出。
- [ ] 未配置 enrollment secret 时 `/portal` **fail-closed**（403），不是默认放行。
- [ ] 不带 / 带错 / 不匹配的 `auth` 一律拒绝，**且 session 计数与租约状态无任何变化**（要断言，不要只看状态码）。
- [ ] 带正确 `auth` 的正常路径仍然工作：`tools/crp_closed_loop.test.py` 的 27 次交换、`tools/crp_relay.test.js`、`tools/crp_session.test.py`、`tools/crp_session_flow.test.js` **全部仍然通过**。
- [ ] `tools/crp_engine_crosscheck.js` 三种情形（正确/错误/缺失）**逐行文本一致**，并贴出记录数。
- [ ] `bash tools/gate.sh --fast` 七阶段仍全绿（`EXP_CTEST` 不应变化 —— 你不新增 CTest）。
- [ ] 交付说明写清：参考实现与引擎各自的改动点、enrollment secret 的传递方式（配置从哪来）、以及**为什么默认 fail-closed**。

## 5. 边界

- **不要**改 `tools/gate.sh` / `CMakeLists.txt`（`vversecli` 正在用）。
- **不要**动 `docs/**` —— 协调者合并后统一同步。
- **不要** `push`、**不要** `merge`；交回分支与 commit 哈希。
- **不要**为了让测试变绿而放宽断言。若发现参考实现与引擎在某点上真的不能一致，**停下报告**。
- 范围**不含**给 hub 加 TLS、加账户体系或改部署方式 —— 本流只解决「签发需要调用方证明身份」这一件事。
