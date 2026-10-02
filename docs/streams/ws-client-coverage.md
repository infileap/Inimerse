# 作业单：`ws-client-coverage`

> **状态横幅（动工前快照）。** 本文写于 `d1bef61` 之上。里面的「现状」记的是**写下那一刻的代码事实**，
> 判断某件事做没做，以 [STATUS.md](../STATUS.md) §10 与 [BOARD.md](../BOARD.md) §5 为准。
> 本仓库已有过两次「文档自己骗自己」的事故（BOARD §5 的 2026-08 修订说明、§6 末尾的集成记录），
> 所以下面每一条判据都写成**可当场执行、当场看输出**的形式。

## 1. 为什么开这条流

[BOARD.md](../BOARD.md) §5 的 `ws-client-coverage` 行原文写的是：

> `tools/crp_ws_client.js` 只有 **1 条**断言，且没有任何文档或脚本引用它（`upp_session` 同样 0 引用）

**开工前我实测，这句话有一半是错的**，先把事实钉住再派活：

| 板上说法 | 实测 | 结论 |
| --- | --- | --- |
| `crp_ws_client.js` 只有 1 条断言 | `tools/crp_ws_client.test.js` 6 行，实为 **2 条**断言（`c.queue.length === 1`、`close()` 后 `=== 0`） | 基本属实（数量差 1） |
| 没有任何文档或脚本引用它 | `tools/regression.js:4` 的 `tests` 数组里**列了** `'crp_ws_client.test.js'` | **错** |
| `upp_session` 同样 0 引用 | `tools/upp_engine_crosscheck.js:30` 直接 `require('./upp_session')`；`docs/REQUIREMENTS_ANALYSIS.md:178`、[streams/upp-in-engine.md](upp-in-engine.md):34 都点名了它 | **错** |

⇒ 真正缺的**不是引用**，而是两件具体的事：

1. **`crp_ws_client` 的行为没有测**。它的三个核心机制 —— 连接、重连（退避 / 重试耗尽 / generation 守卫）、
   排队（未连接时入队、`open` 后按序 flush）—— **一条断言都没有**。现有 2 条只覆盖了 `queue` 的入队与清空。
2. **`crp_ws_client` 在 `docs/` 里没有任何出处**（`upp_session` 有，`crp_ws_client` 没有）。

## 2. 现状事实（我实测，供你核对）

- `tools/crp_ws_client.js`（23 行）导出 `CrpWebSocketClient`，构造签名
  `constructor(url, options = {})`，`options` 认 `retries`（默认 3）、`backoffMs`（默认 100）、`onMessage`（默认空函数）。
  方法：`async connect()`、`async _connectLoop(generation)`、`send(value)`、`close()`。
- **关键约束：它直接用全局 `WebSocket`**（`crp_ws_client.js:10` `new WebSocket(this.url)`、
  `:12` `ws.readyState === WebSocket.OPEN`）。仓库**没有任何 WebSocket 服务端实现**，
  也没有 `ws` 依赖。Node 24 有内建**客户端** `WebSocket`，但没有服务端。
- `tools/upp_session.js`（32 行）导出 `UppSession`、`STATES`；`tools/upp_session.test.js`（19 行）
  **已经有约 14 条断言**（心跳乱序、时间戳乱序、staleness 边界 `isHeartbeatStale(1000,500)===true` /
  `(500,500)===false`、crash→recover→start、`cannot start from crashed`、`invalid role`）。
  **这个文件不需要补断言**，只需要文档出处（见 §3 判据 3）。
- 两个套件**已经在门禁里**：`tools/node_suites/run_all.js:20` 列 `'upp_session.test.js'`、`:24` 列
  `'crp_ws_client.test.js'`。⇒ **你不需要改门禁、不需要改 `CMakeLists.txt`、不需要动 `EXP_CTEST`。**

## 3. 判据（每条都能当场执行）

1. **`crp_ws_client` 的连接 / 重连 / 排队各自有断言**，且断言在**离线**下可复现（无网络、无外部依赖）：
   - **连接**：`open` 触发后 `connect()` 兑现；`error` 触发后按 `retries`/`backoffMs` 重试。
   - **重连**：重试次数耗尽后 `connect()` **reject**；`close()` 之后**不再**重连（`closed` 守卫）；
     旧 generation 的 `close` 事件**不能**让新连接被顶掉（`_generation` 守卫）。
   - **排队**：未连接时 `send()` 入队且**不**调用底层 `send`；`open` 后按**入队顺序** flush；
     flush 后 `queue` 为空；`close()` 清空 `queue`。
   - 建议做法：在 `require('./crp_ws_client')` **之前**把一个假的 `WebSocket` 类装到
     `globalThis.WebSocket`（带 `OPEN` 常量与可手动触发的 `open`/`message`/`error`/`close` 事件），
     测试结束再还原。**不要**为了这条流去引第三方 `ws` 依赖，也不要依赖真实端口。
2. **修复前必须失败**：把新断言在**未改动的** `crp_ws_client.js` 上跑一遍，证明它们**确实会红**
   （否则你不知道测的是新行为还是空气）。把命令与输出贴进交付说明。
3. **两个套件都有 `docs/` 出处**：`crp_ws_client` 需要新增一处（`upp_session` 已有）。
   出处要写**它是什么、在哪、怎么跑**，不是只丢一个文件名。加完 `tools/check_links.py` 与
   `tools/check_doc_paths.py` 必须仍 **0 broken**。
4. **门禁**：`node tools/node_suites/run_all.js` 通过，且总数仍为 `N/N passed`；
   `bash tools/gate.sh --fast --only node` 通过。**不需要**跑全量（你的写域不碰 C 代码）。
5. 不许为了让测试变绿而**削弱** `crp_ws_client.js` 的既有行为（重试/退避/generation 语义都是它存在的理由）。

## 4. 写域（别人别碰，你也别出界）

**你可以改：**
- `tools/crp_ws_client.test.js`
- `tools/crp_ws_client.js`（**只在测试暴露真缺陷时**改；改了必须在交付说明里逐条列出行为变化）
- `tools/upp_session.test.js`（如需补断言；不是必须）
- 一份 `docs/` 文档（建议在 `docs/API.md` 或 `docs/STATUS.md` 就近加一节；**若你改 `docs/STATUS.md`，只加你自己的小节，不要动别人的行**）

**你绝对不能碰：**
- `CMakeLists.txt`、`tools/gate.sh`、`tools/node_suites/run_all.js` —— **另一条流（`httpfix`）正持有它们**
- `src/**` —— 本条流不碰引擎
- `docs/BOARD.md`、`docs/STATUS.md` 的既有行 —— **板子与状态由协调者统一改**
- 不要 `git push`、不要 `git merge`、不要动 `main`

## 5. 交付物

1. 一个 commit（在 `stream/ws-client-coverage` 分支上），消息说清改了什么。
2. 交付说明里给出：新增断言**逐条**列出（断言什么、为什么它是行为而不是实现细节）、
   **修复前失败**的命令与输出、`node tools/node_suites/run_all.js` 的输出、
   `gate.sh --fast --only node` 的输出、以及你新增的文档出处路径。
3. 如果你发现 `crp_ws_client.js` 有**真缺陷**（不是测试写不出来），**先说清现象与最小复现**，
   不要顺手改掉——那需要协调者决定是修引擎侧语义还是改测试期望。
