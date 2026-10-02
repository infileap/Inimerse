# 作业单：`crp-portal-postcheck` —— 把监听器的 `/portal` 收进注册表那条路径

**状态**：进行中
**冲突域（别人别碰）**：`src/platform/http_posix.c`、`src/verse/crp.c`、`src/verse/crp.h`、`src/verse/crp_probe.c`、`src/platform/http_probe.c`、`tools/crp_relay.js`、`tools/crp_relay.test.js`、`tools/crp_engine_crosscheck.js`、`tools/crp_session_flow.test.js`、`tools/reconnect_generation.test.py`
**禁碰**：`tools/gate.sh`、`CMakeLists.txt`、`tools/vverse_*`、`src/mod/verse_dist_mod.c`、`src/common/**`、`docs/**`（协调者持有）、`tools/node_suites/**`

> 本流是 `crp-portal-auth`（`40f6094`，已合入 `main`）的收尾。那一条流堵住了「任何人可索取能力令牌」，但它**有意没修**下面四条 —— 因为**每一条都在 `main` 的 `922f9af` 上原样复现**，即它们先于那次交付存在，不是它引入的。当时的合并记录见 [STATUS.md](../STATUS.md) §10.12 末段。

---

## 1. 缺陷（协调者实测，你开工前要自己复现）

四条全部由 `/tmp/pa_wildcard_probe.py` 与 `/tmp/pa_peer_probe.py` 在**真引擎、裸 socket**上打出。**判别器**：`POST /portal` 返回 200 = 铸发成功，403 = 被拒。

### R1（最重）空 scope 被读成「任意 scope」

`token_allows`（`src/platform/http_posix.c:284`，判定在 `:287`）逐字是：

```c
return (!g_tokens[i].verse[0] || (verse && !strcmp(g_tokens[i].verse, verse))) && (!g_tokens[i].peer[0] || (peer && !strcmp(g_tokens[i].peer, peer)));
```

`!g_tokens[i].verse[0]` 的意思是「这条令牌的 verse scope 是空串」⇒ **匹配任何 verse**。而 `/portal` 取 scope 用的是：

```c
snprintf(scope_verse, sizeof scope_verse, "%s", vj_str(vj_get(pbody, "verse"), ""));
snprintf(scope_peer,  sizeof scope_peer,  "%s", vj_str(vj_get(pbody, "peer"),  ""));
```

（`src/platform/http_posix.c:2046-2047`）—— `vj_str(v, "")` 对**任何非 `VJ_STR` 的值**都返回那个 `""`。于是 `{"verse":5,"peer":null}` 配一个为 `"5"`/`"null"` 算出的正确证明，铸出的令牌**两个 scope 都是空串**。

**端到端实测**（同一次探针运行，逐字）：

| 铸发 body | 回显 | 拿该令牌打 `{"verse":"totally-other-verse","peer":"other-peer"}` |
|---|---|---|
| `{"verse":"demo","peer":"p1"}` | `{"token":"posix-…","verse":"demo","peer":"p1"}` | `/signal` → **403**、`/session/stop` → **403** |
| `{"verse":5,"peer":null}` | `{"token":"posix-…","verse":"","peer":""}` | `/signal` → **200 `{"ok":true,"accepted":true}`**、`/session/stop` → **200** |

**必须两个 scope 都空才是全域通配。** 只把 `peer` 设成 `null`、`verse` 仍是 `5` 时，peer scope 还是 `"p1"`，两个令牌都 403 —— 我第一次跑就被这条中间结果误导过。

**这一条在 `main` 上更严重**：把探针的 `ENGINE` 换成 `/home/sakiko/inimerse/build/inimerse`（main 的构建产物），**六行输出逐行相同**，而 main 的 `/portal` **连证明都不需要**。所以本次交付是**收紧**，不是引入。同理，main 的旧读法 `json_field_string`（`src/platform/http_posix.c:416`，`:419` 是 `if (*p != '"') return 0;`）对 `{"verse":5}` 直接**失败**、`out` 保持 `""` ⇒ 旧代码铸出的**也是**空 scope 通配令牌。

**今天为什么还不算致命**：enrollment secret 是**一个 hub 级的值**，持有它的人本来就能为任意 `(verse, peer)` 出证明。所以 R1 不是权限提升，而是**铸发侧把「没有 scope」写成了「所有 scope」** —— 一个将来会咬人的默认值错误。

### R2 重复 JSON 键：引擎 first-wins，`JSON.parse` last-wins

`vj_get` 返回**第一个**匹配键（main 的 `json_field_string` 也是 first-wins）。实测 `{…,"auth":"<good>","auth":"x"}` → **200**，`{…,"auth":"x","auth":"<good>"}` → **403**。不知道 secret 不可利用（两条路径对同一份字节是确定性的），但同一份 body 在引擎与 JS 参考实现下会得出不同结论。

### R3 非字符串值的回显与参考实现不一致

参考实现 `tools/crp_relay.js:78-84` 回显的是**解析后的原值**（`{"verse":5}` 回显 `"verse":5`），监听器回显 `vj_str(…,"")` → `""`。与 R1 同源。

### R4 监听器的 `/portal` 从不查注册表

`src/platform/http_posix.c:2020-2027` 的注释称调用方「prove[s] it may open a portal for this exact (verse, peer)」，但代码里**没有任何注册表查询**：证明有效就为**任意** `(verse, peer)` 铸令牌，包括未注册的 verse。而同一个仓库里的另外两条路径都答 404：

- `crp_registry_portal`（`src/verse/crp.c:1069`）：`:1082 if (!vstr || !registry_has_verse(r, vstr) || !vj_truthy(peer))` → 404 `verse not found`；
- 参考实现 `tools/crp_relay.js:82`：`if (!verses.has(p.verse) || !p.peer) return json(res, 404, { error: 'verse not found' });`。

即**同一个「开 portal」动作有三份语义，监听器那份最松**。

---

## 2. 相关既有代码（动手前读这几处）

- 唯一判定点：`crp_enroll_check`（`src/verse/crp.c:1057`），被 `crp_registry_portal`（`:1069`，调用在 `:1078`）与监听器（`src/platform/http_posix.c:2039`）共用。**这是 `crp-portal-auth` 第二版的全部意义，不要把它拆回两份。**
- 监听器的 `/portal` 分派块：`src/platform/http_posix.c:2019-2055`（`strstr(req,"\r\n\r\n")` 定位 body → `vj_parse` → `vj_get` → `crp_enroll_check` → 铸发 → `vj_free`）。
- 令牌表与判定：`token_register`（`:276`）、`token_allows`（`:284`）、`token_revoke`。依赖 `token_allows` 的端点：`/signal`（`:1160`）、`:1434`、`:1512`、`:1984`、`:1989`。
- 参考实现：`tools/crp_relay.js:78-90`；crosscheck 驱动：`tools/crp_engine_crosscheck.js`（**注意它走的是 `verse_crp_probe`，即 crp.c 路径，不经过 HTTP 监听器** —— 这是 R4 至今没被门禁发现的原因）。

## 3. 判据（验收条件）

1. **R1 必须消失，且是「按构造」消失**：非字符串 `verse`/`peer` 不再能产生空 scope。二选一（协调者倾向第一种）：
   - **(a) 首选**：监听器的 `/portal` 整体走 `crp_registry_portal` —— 一条路径、一套拒绝、一套 scope 语义；
   - **(b)** 若因架构原因不能合流，则必须在铸发前**复现**它的前置：非字符串 `verse` → 404 `verse not found`、falsy `peer` → 404 `verse not found`，且**任何情况下都不再写入空 scope**。
   无论走哪条，`token_allows` 也不该把「没有 scope」读成「所有 scope」—— 空 scope 应当**匹配不到任何东西**，并有一条测试钉住它。
2. **新回归（必交）**：用非字符串 `verse` 铸发之后，该令牌对**另一个** verse 必须被拒（403）。这条测试在今天的 `main` 上必须**失败**。
3. **R4 必须消失**：监听器对**未注册**的 verse 答 404 `verse not found`，与 `crp_registry_portal` 和参考实现一致。
4. **R2/R3 至少要一致化其一**：要么让引擎与 `JSON.parse` 在重复键上同解，要么在作业单里写明「本仓库规定 first-wins」并让参考实现跟随（**先改判据方**，这是 `crp-portal-auth` 立的规矩）。R3 的回显要么改成回显原值，要么让参考实现改成回显空串 —— 同样先改判据方。
5. **门禁全绿**：`rm -rf build && tools/gate.sh` 七阶段全 PASS。**`EXP_CTEST` 不许变**（当前 95）；新断言请住进**既有** CTest 条目或明确报备新增条目数。
6. **修前必失败**：在 worktree 里 `git checkout main -- src/`（保留你的 `tools/` 测试）重编后，新回归必须失败；改回来必须通过。把两条原始输出贴进交接说明。

## 4. 边界与已知取舍

- `src/common/**` **禁碰**（`json_min` 的 `\u0000` 裸 NUL 是另一条独立缺陷，不在本流）。
- **不要**为了 R1 去改 `token_allows` 的调用点语义 —— 那些端点的 403 行为已被既有测试覆盖。
- 若走 3(a)（合流到 `crp_registry_portal`），注意它会 `registry_get_session`（不存在就新建）并 `im_crp_session_apply("start")` + `im_crp_session_lease_begin`。监听器今天的路径**不建会话** ⇒ 合流会**改变行为**（会开始建会话）。这是**期望的**（两条路径本就该一致），但必须写进交接说明并在 crosscheck 语料里体现，否则 `tools/crp_engine_crosscheck.js` 的 112 条语料会分歧。
- 提交策略：**只 commit 不 push、不合 main**。协调者在门禁全绿后合入。

## 5. 复现探针

协调者的两个探针在 `/tmp/pa_wildcard_probe.py` 与 `/tmp/pa_peer_probe.py`（裸 socket 打真引擎，逐字节控制请求体，用 `tools/testports.py` 的 `start_hub_bound_ports` 起 hub）。要点：脚本内容必须是 `say "hub"\nwait 60\n`（**空脚本会让引擎立刻退出**，随后报 `ConnectionRefusedError`）；`wait_http_ping(port, host, timeout, path)` 的 **port 是第一个参数**；证明按 JS 语义算（`str(None)` 是 `"None"` 而不是 `"null"` —— 这个坑我踩过，会伪造出三处假 403）。
