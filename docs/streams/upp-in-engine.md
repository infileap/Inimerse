# 流简报：`upp-in-engine`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/upp-in-engine`，冲突域 `src/verse/`、`src/mod/verse_dist_mod.c`。

## 1. 要交付什么

**引擎侧真的能跑 UPP 全序列**，而不是只有一份 JS 参考实现。

现状是分裂的：`tools/*.js` 里有一套完整的 UPP 参考实现和 19 条断言，全过；但引擎里**一行对应代码都没有**。「测试全过」证明的是参考实现自洽，不是引擎能用。

## 2. 现有什么

引擎侧（`src/verse/`，合计 3194 行，全部已构建并测试）：

| 文件 | 行数 | 内容 |
| --- | --- | --- |
| `src/verse/eventlog.c` | 540 | 规范 JSON 事件日志、sha256 哈希链 |
| `src/verse/layer.c` | 642 | Layer 的 create/put/undo/status/drain、锚点校验 |
| `src/verse/protocol.c` | 357 | 帧编解码、能力协商、权威字段拒绝 |
| `src/verse/json_min.c` | 272 | 整数专用 JSON 解析器（`vj_parse`/`vj_free`/`vj_get`） |
| `src/verse/server.c` | 76 | `inim-server` 入口 |
| `src/verse/client.c` | 153 | `inim-client` 入口 |
| `src/verse/*_probe.c` | 816 | 三个探针（eventlog 290 / layer 290 / protocol 236） |

参考实现（JS，**只读，别改**）：

- `tools/upp_reference.js`（198 行）导出：`VERSION`、`MAX_FRAME_BYTES`、`frame`、`encode`、`decodeLine`、`createDecoder`、`validateManifest`、`hello`、`negotiate`、`control`、`heartbeat`、`start`、`stop`、`log`、`crash`、`incompatible`、`readManifest`、`generateManifest`
- `tools/upp_session.js`（32 行）导出：`UppSession`、`STATES`
- 断言在 `tools/upp_reference.test.js`（19 条），已由 `tools/node_suites/run_all.js` 纳入门禁

## 3. 线上格式（照抄，别自己发明）

- JSONL：一行一帧，**换行分隔**，无长度前缀
- 帧形状 `{ upp: 1, type, id?, payload }`；`id` 为空时**整个键删除**，不是空串
- `VERSION = 1`；`MAX_FRAME_BYTES = 1024 * 1024`（**按 UTF-8 字节数**算，不是字符数）
- `ROLES = { host, verse, client }`
- `CONTROL_TYPES = { heartbeat, start, stop, log, crash, incompatible }`
- `validateManifest` 的硬规则：`id`/`name`/`version`/`engine`/`entry` 五个字符串必填且非空白；`id` 匹配 `^[a-z0-9][a-z0-9._-]{0,63}$`（忽略大小写）；`version` 必须是 semver；`abi` 是正整数；`abiRange` 是 `N` 或 `N..M`

## 4. 状态机（这是任务的核心）

`STATES = { idle, running, stopped, crashed, incompatible }`

```
idle --start--> running --stop--> stopped
                    |
                    +--crash--> crashed --recover--> idle
                    +--心跳 15s 无--> crashed
stopped --recover--> idle
```

必须与参考实现逐条一致：

- `start`：已在 `running` 则**幂等返回**，不报错；从 `crashed` 或 `incompatible` 起 `start` 必须**抛错**
- `stop`：无条件进 `stopped`
- `crash`：进 `crashed`，`error` 取 `payload.error`，缺失时为 `'unknown crash'`
- `recover`：**只允许**从 `crashed` 或 `stopped` 出发；从别的状态抛错；recover 会清空 `error`、`lastHeartbeat`、`lastHeartbeatAt`
- `reset`：无条件回 `idle` 并清空全部
- 心跳：`seq` 必须是安全整数且**不得小于**上一个（等于可以）；`timestamp` 有限且**不得小于**上一个；`checkHeartbeat` 只在 `running` 时判定，超时 `timeoutMs = 15000` 后置 `crashed`、`error = 'heartbeat timeout'`
- 协商失败（`acceptHello` 抛错）→ 状态置 `incompatible` 且记下 `error`

## 5. 判据（量化，别含糊）

1. 引擎能跑完 `hello → start → heartbeat → （失联）crash → recover → reset` **全序列**
2. 与 JS 参考实现**逐事件对照一致**：同一输入序列喂给两边，产出的帧序列（含 `type`/`payload` 字段与状态转移）逐条相等；不一致的用例要打印双方原文
3. 边界必须有断言：`start` 幂等、从 `crashed` 起 `start` 报错、心跳乱序被拒、15s 超时转 `crashed`、`recover` 从非法状态报错、1 MiB 帧被拒、`id` 为空时键不存在
4. 新增 C 探针（`src/verse/upp_probe.c` 或类似）注册进 `CMakeLists.txt`，进 `ctest`
5. `tools/gate.sh` 全绿

## 6. 已知的坑（都是踩过的）

- **权威字段**：`VL_AUTHORITY_FIELDS[] = {"seq","rev","head","balance","state_hash","committed"}`，客户端送这些字段会被服务端以 `client_authority` 拒绝。UPP 的 `heartbeat.seq` 是**应用层**序号，别跟 Layer 的 `seq` 混了
- **参数顺序陷阱**：语言层 `verse_verify(data, sig, pub)`，C 层 `ed25519_verify(pub, msg, msglen, sig)`。两者顺序相反
- **锚点要在多处校验**：曾经只在 `drain` 校验 `commit.head`，导致 `status` 把被篡改的值当已提交返回。现在 `vl_handle_hello` 和 `VL_CMD_STATUS` 都查
- **JSON 解析器只收整数**：`vj_parse` 遇到 `.`/`e`/`E` 直接报 `non-integer number unsupported`。UPP 的 `timestamp` 如果是浮点，别指望 `json_min` 能解
- **HTTP body 要从 `\r\n\r\n` 之后开始解析**：`req` 缓冲含请求行和头部
- **别碰别人**：`crp-in-engine` 流的 worktree 已开但**阻塞**在这一条上，等这条合入 main 才放行。别提前替它实现 CRP

## 7. 交回时给我

按 [BOARD.md](../BOARD.md) §4 的五项，第 4 项「没做什么 / 已知没解决什么」不能漏。
