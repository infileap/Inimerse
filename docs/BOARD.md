# docs/BOARD.md — 多会话协调板

> **本文件是多会话并行工作时唯一的分派与认领记录。**
> 每个新会话开工前：①读本文件 → ②读 [STATUS.md](STATUS.md) 里对应章节 → ③用 `tools/stream.sh new <slug>` 开自己的工作区。
>
> 与 [STATUS.md](STATUS.md) 的分工：STATUS.md 记录「**已经**做到哪了」（事实与证据），
> 本文件记录「**正在**做什么、谁在做、到什么程度」（在途与认领）。两者不重叠。

## 1. 三条硬规则

多个 DSH 会话共用 `/home/sakiko/inimerse`。共用一个工作区时，一个会话的 `git add -A`
会把另一个会话未完成的改动卷进自己的 commit —— 这件事**真的发生过一次**：§43.5 的
用户在制品被顺手带进了 `fdcdb10`，事后只能靠「用户已批准」补救。所以：

1. **一个工作区一个会话。** 不要在主工作区直接改。用 `tools/stream.sh new <slug>`
   在 `.worktrees/<slug>` 开独立工作树，各自有独立 `build/`。
2. **认领即建分支。** 分支 `stream/<slug>` 存在 = 该任务被认领。`tools/stream.sh list`
   能看到全部在途工作树、分支、脏状态和领先 main 的提交数。
3. **只有协调者能合进 main。** 会话只推自己的 `stream/<slug>` 分支；合入 main 由
   协调者在 `tools/gate.sh` 全绿后执行。**不要在别人的分支上提交。**

## 2. 开工流程

**先读自己那条流的作业单**：`docs/streams/<slug>.md`（已开流的都有；没有的说明还没派活）。
里面有现状代码位置、参考实现、量化判据、已知的坑——比第 5 节那一行详细得多。

```bash
tools/stream.sh new <slug>          # 建 .worktrees/<slug> 与分支 stream/<slug>
cd .worktrees/<slug>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

# …干活…

tools/gate.sh                       # 必须全绿
git push origin stream/<slug>
```

然后在本文件第 5 节把该行状态改成 `待验收`，并在交接说明里写清楚。

收工时：

```bash
tools/stream.sh list                # 确认没有别的会话在等这个分支
tools/stream.sh rm <slug>           # 有未提交改动会拒绝；确认丢弃才加 --force
```

## 3. 门禁（`tools/gate.sh`）

合入 main 前必须全绿。七个阶段，任一失败即整体失败：

| 阶段 | 命令 | 期望 |
| --- | --- | --- |
| build | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | 0 error |
| ctest | `ctest --test-dir build --output-on-failure -j4` | **94 / 94 通过**，无 `WILL_FAIL` 记账项。**数量是断言，不是标签**：`stage_ctest` 会检查输出里确有 `0 tests failed out of 94`，否则阶段失败 —— 光看 ctest 退出码是看不出一整个 `add_test( )` 悄悄掉了的。改动用例数时同步 `tools/gate.sh` 的 `EXP_CTEST` 与本表 |
| economy | `python3 tools/economy_migration.test.py` | `economy migration: ok`（**39 / 39**） |
| plugin | `node tools/dsh-inimerse/verify.mjs --live` | **55 / 55** |
| node | `node tools/node_suites/run_all.js` | **11 / 11**（`tools/` 下每个可独立运行的 JS 套件；`crp_session_flow.test.js` 需活跃 hub，由 CTest 的 `crp_session_flow_regression` 驱动，不在此列） |
| links | `python3 tools/check_links.py` | **0 broken** |
| doc-paths | `python3 tools/check_doc_paths.py` | **0 broken**（反引号内的 `docs/…`/`future/…` `.md` 引用；`links` 阶段看不见这类，因为 `check_links.py` 必须先剥离行内代码，见 [streams/docs-audit.md](streams/docs-audit.md) §3.2） |

```bash
tools/gate.sh                # 全量
tools/gate.sh --fast         # 跳过 configure，复用已有 build/
tools/gate.sh --only links   # 只跑一个阶段：build|ctest|economy|node|plugin|links|doc-paths
```

**门禁数字变了就必须同时改这张表和 [STATUS.md](STATUS.md) §1 的基线行**，否则下一个会话会拿旧数字当验收线。

## 4. 认领与交接

认领 = 建分支 + 把第 5 节对应的行的「状态 / 认领」两列改掉。冲突域写在行里：
**不要碰别的行所列目录**，那是别人的。若确实要改别人的目录，先在会话里说明，由协调者裁决。

交接给协调者时，在会话里给出这五项（缺一项就不收）：

1. slug 与分支名、commit 范围
2. `tools/gate.sh` 的结论表（贴输出，不要复述）
3. 改了哪些文件（`git diff --stat <base>..HEAD`）
4. **没做什么 / 已知没解决什么**——这一项最容易漏，也最重要
5. 对应的 `docs/`（或 `future/`）文档是否同步；口径变更要引用 [STATUS.md](STATUS.md) §1 的四标记词汇

## 5. 任务板

状态取值：`未认领` / `进行中` / `待验收` / `已完成` / `已否决` / `阻塞`。

`已否决` 表示**经论证决定不做**——既不是「还没做」也不是「做完了」。这一格必须写明否决理由**和再议条件**，否则「否决」和「忘了」在板上长得一样。

| 状态 | slug | 任务 | 冲突域（别人别碰） | 验收判据 | 认领 |
| --- | --- | --- | --- | --- | --- |
| 阻塞 | `marketplace-watch` | 上架 dsh-m：npm 包已发布，PR [iasiv5/dsh-m#1](https://github.com/iasiv5/dsh-m/pull/1) 等待维护者点 *Approve and run* | `tools/dsh-inimerse/marketplace/` | PR 合并后 `node scripts/validate-registry.mjs` 全绿 | 协调者 |
| 已完成 | `upp-in-engine` | UPP 引擎侧已实现：`src/verse/upp.{h,c}` 帧编解码 + 分片 decoder + 状态机，213 条断言探针，并**逐事件对照**过参考实现（111 个 op 同语料两边跑，逐行文本比对）。遗留边界（不是没实现）：`timestamp` 只收整数、尚未接入 `inim-server`/`inim-client` 传输层 —— 见 [STATUS.md](STATUS.md) §10.1 | `src/verse/`、`src/mod/verse_dist_mod.c` | 引擎能跑完 hello→start→heartbeat→（失联）crash→recover→reset 全序列，且与 JS 参考实现逐事件对照一致 | `stream/upp-in-engine` |
| 已完成 | `crp-in-engine` | 引擎侧 CRP 线层已实现并验证（见 §6 与 [STATUS.md](STATUS.md) §10.6）。会话层 `src/platform/crp_session.{h,c}` 本来就存在 —— 原「引擎侧无实现」的说法是错的，已更正。**遗留边界**：`/friends`、`/content`、`/package` 三个端点按边界仍未实现（`crp_relay.test.js` 的 23 条断言里 11 条属此）；`session_store_seq()` 把 §55.6 的缺口判定降级成诊断（`acceptVerdict`），线上行为对齐参考实现 | `src/verse/`、`src/common/`、`src/mod/verse_dist_mod.c` | 见 [STATUS.md](STATUS.md) §10.6 | — |
| 已完成 | `vverse-produce` | 引擎侧 `.vverse` 打包器已实现：`src/common/gzip.{h,c}`（确定性 gzip，写侧只做 stored 以换取逐字节可复现）＋ `src/common/vverse_pack.{h,c}`，与 JS 参考实现**双向**交叉验证（47 项，含 node `crypto.verify` 认可引擎产出的 DER SPKI）。遗留（不是没实现）：无 `.im`/CLI 入口；>64 KiB 的包经 hub 会被截断，见下两行 | `src/`、`vtest/` | 引擎产出的 `.vverse` 能通过 `tools/vverse_validate.js` 校验并被 `inim-server` 装载 | `stream/vverse-produce` |
| 未认领 | `vverse-cli` | **原话要更正（协调者实测）**：`.im` 脚本**能**调用打包内建——`src/mod/verse_dist_mod.c:2693` 注册了 `vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1\|CAP_VERSE\|CAP_NET, 0)`，`src/lobby_online_src.im:40` 早就在用 `r1 = verse_pack(".", "universe/room.vverse")`，我实测一条 3 行 `.im` 能跑通并产出文件（exit 0）。**真正的缺陷是格式**：该内建（`b_verse_pack`，`src/mod/verse_dist_mod.c:1109`，自己手工拼 `"files":{…}` 且用 sha256 **hex**）写出的是**裸 JSON**（实测头 32 字节 `{"id":"src","version":"1.0.0","publisher":"e4610a1d…`），而真正的 `.vverse` 是 **gzip 的 `{"format":"vverse-1","files":{…base64…}}`**（`tools/vverse_pack.js:8` 与 `src/common/vverse_pack.c`）。把内建产物喂给参考实现：`node tools/vverse_pack.js preview <pkg>` → `vverse pack: incorrect header check`，`unpack` 退出码 1 ⇒ **内建是遗留的另一种格式，与整条工具链不兼容**。库路径本身正常：`build/vverse_pack_probe --pack <dir> <out> [seed-hex]`（fixture 需 `manifest.json`/`blueprint.json`/`laws/rule.im`/`mods/entry.im`/`assets/*`，见 `src/common/vverse_pack_probe.c:162`） | `src/mod/verse_dist_mod.c`、`tools/vverse_*` | 一条 `.im` 脚本或 CLI 能直接产出**通过 `node tools/vverse_validate.js <unpacked> --strict`** 的 `.vverse`——即内建必须改走 `src/common/vverse_pack.c` 的容器，而不是自己写 JSON | — |
| 已完成 | `hub-large-package` | **静默截断（真缺陷）**：`src/platform/http_posix.c:312 hub_body()` 的 `GET /v/<id>` 与 `GET /package/<id>` 用 `fread(body, 1, cap, vf)` 读文件，超 `cap` 即截断且**仍返回 200**（HTTP 调用点 `:1063` 缓冲 65536，实测 207991 字节包只回 65536，由 vverse 流交叉测试记录）；UDP 调用点 `:52-55` 缓冲 60001 且要求 `blen < 60000` 才发，即**根本不发、让对端超时**。`POST /package` 与 `POST /content` 上限各 49152。对照：`GET /content/<hash>` 因回算 sha256 会报 `content_corrupt` 500，**不静默** | `src/platform/http_posix.c` | 超大包要么完整送达，要么**明确报错**（413 / 长度协商），禁止 200 + 截断；补 >64 KiB 往返测试。**已交付并合入 `main`（`578eb95`）**：见 [STATUS.md](STATUS.md) §10.10 —— 四条路径改为显式 413（`read_file_capped`；`POST /package/fork` 在 `write_atomic` **之前**拒绝，不落盘）、UDP 发送侧加 `status == 200` 闸、引擎支持 `--port 0` / `--http-port 0` 由内核分配端口并把**真实**端口报出来；端口竞态实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。`tools/vverse_cross.test.py:415-428` 原先把截断**固化成了期望**，现改为断言 413 + 真实大小 | `stream/http-truncation-and-ports` |
| 已完成 | `ws-client-coverage` | `tools/crp_ws_client.js`（23 行）的**连接 / 重连 / 排队三个机制没有任何断言**——`tools/crp_ws_client.test.js` 6 行只有 2 条（入队、`close()` 清空队列）；且 `crp_ws_client` 在 `docs/` 里没有出处。**开工前实测：板上原话「没有任何文档或脚本引用它（`upp_session` 同样 0 引用）」是错的**——`tools/regression.js:4` 的 `tests` 数组里列了 `'crp_ws_client.test.js'`，而 `tools/upp_session` 被 `tools/upp_engine_crosscheck.js:30` 直接 `require`，并被 `docs/REQUIREMENTS_ANALYSIS.md:178` 与 [streams/upp-in-engine.md](streams/upp-in-engine.md):34 点名。另外 `tools/upp_session.test.js`（19 行）**已有约 14 条断言**（心跳乱序、时间戳乱序、staleness 边界、crash→recover→start、`invalid role`），不需要补。两套件**早已在门禁里**（`tools/node_suites/run_all.js:20`/`:24`），故本条流**不需要动 `CMakeLists.txt` 或 `EXP_CTEST`** | `tools/crp_ws_client*`、`tools/upp_session*` | 见作业单 [streams/ws-client-coverage.md](streams/ws-client-coverage.md) §3：连接/重连/排队各自有断言且**离线可复现**、**修复前必须失败**（要贴命令与输出）；`crp_ws_client` 有一处 `docs/` 出处；`gate.sh --fast --only node` 通过；`links`/`doc-paths` 仍 0 broken。**已交付并合入 `main`（`066e8fd`）**：见 [STATUS.md](STATUS.md) §10.9 —— 2 条断言 → **31 条**（连接 7 / 重试 8 / 重连 8 / 排队 8），离线且确定性（在 `require` 之前装手写假 `globalThis.WebSocket`，不引 `ws`、不占端口）；**非空证据 = 变异测试** `tools/crp_ws_client.mutation.py`（29 个单行变异：旧套件只看得见 2 个，新套件 29 个全杀；2 条结构性屏蔽已逐条点名遮蔽者）；`tools/crp_ws_client.js` **未改** —— 31 条断言在未改动模块上即全绿 | `stream/ws-client-coverage` |
| 已完成 | `gate-hermeticity` | **门禁自己骗自己（协调者实测）**：`links` 阶段不封闭。`tools/check_links.py:57-74` 的 `iter_markdown_files()` 用 `os.walk(REPO_ROOT)` 遍历**工作区**，只靠 `SKIP_DIRS`/`SKIP_PREFIXES`/`SKIP_FILES` 黑名单挡杂物。实测 `python3 tools/check_links.py` → **85 markdown files, 314 links, 12 broken**，而 `git ls-files '*.md' \| wc -l` = **78**、`find . -name '*.md' -not -path './.git/*' \| wc -l` = **325**。那 12 条断链**全部来自 `.verify/`** —— 另一个会话在仓库根留下的研究暂存目录，其 `README.md` 引用 `SECURITY.md`/`GOVERNANCE.md`/`MAINTAINERS.md`（GitHub 社区健康文件，本仓库没有）。⇒ **无关会话往仓库根丢一个暂存目录，就能把本仓库的门禁判红**，这会训练人不再信任门禁（正确反应会从「去修引用」退化成「先看看这次红的是不是杂物」）。`tools/check_doc_paths.py` **无**此问题（固定 11 文件清单，实测封闭）。另：`universe/_cache/`（引擎资产缓存，`src/mod/verse_dist_mod.c:1172`）与 `.verify/` 都不被 `.gitignore` 覆盖（`git check-ignore` rc=1） | `tools/check_links.py`、`.gitignore`、`tools/check_doc_paths.py` | 见作业单 [streams/gate-hermeticity.md](streams/gate-hermeticity.md) §4：①报的 `files` 数等于 `git ls-files '*.md' '*.markdown' \| wc -l`；②当前工作区 0 broken 且不再出现 `.verify/README.md`；③**反向验证**——往已跟踪 `.md` 里临时插一条断链必须仍 `exit 1`（否则「0 broken」可能只意味着「什么都没扫」）；④仓库根造未跟踪 `scratch-xyz/README.md` 必须看不见；⑤`gate.sh --fast --only links` 与 `--only doc-paths` 通过。**已交付并合入 `main`（`63c79c3`，merge `fa30036`）**：`tools/check_links.py` 的 `iter_markdown_files()` 改为 `git ls-files -z -- '*.md' '*.markdown'`，`SKIP_DIRS`/`SKIP_PREFIXES`/`SKIP_FILES` 三个黑名单常量**整段删除**（它们就是错误形状的修法）；git 缺失或非零退出时**响亮 `sys.exit`**，**不留 `os.walk` 回退**。**队友还纠正了我的一个错误测量**：我在作业单里写「`check_doc_paths.py` 实测封闭，大概率不用动」是**错的** —— 它 `scan_files()` 用 `os.listdir`，一个**未跟踪**的 `docs/*.md` 带断掉的反引号引用就能让它 `12 files / 1 broken` / exit 1；已按写域条款一并修复，且修法是**用已跟踪集合过滤列表**而非换成 pathspec，理由是 git 的 `docs/*.md` pathspec **会递归匹配 `docs/archive/*.md`**（51 vs 非递归 6），会把该检查器刻意不评判的归档件拖进来。**我亲自复核（不采信自报）**：`--json` 报 `files=78` == `git ls-files '*.md' '*.markdown' \| wc -l` = 78；`.verify/` **仍在磁盘上**的情况下 78 files / 0 broken；反向验证（往已跟踪 `README.md` 插断链）→ `BROKEN README.md -> ./definitely-missing-xyz.md`、1 broken、exit 1，恢复后 `git diff --exit-code` 干净；未跟踪 `scratch-xyz/README.md`（指向 `NOPE.md`）**看不见**；`doc-paths` 的同类反向验证也做了（**逐字输出见 [STATUS.md](STATUS.md) §10.8，那里放在围栏代码块内**）。**这里原先踩了自己的坑**：本单元格最初把探针输出逐字抄进了**行内 code span**，而 `check_doc_paths.py` 只清空**围栏代码块**、不清空行内 span，于是两个并不存在的探针名被当成真引用，`doc-paths` 在 `main` 上判红（`12 markdown files, 167 backtick refs, 2 broken`，exit 1）。**教训**：检查器「跳过围栏块」**不等于**「跳过示例路径」；要贴检查器输出就贴进围栏块，或者别写成 `docs/…` 结尾 `.md` 的形状。修法是改本格的写法，**没有动任何检查器代码** | `stream/gate-hermeticity` |
| 已完成 | `json-min-nul-escape` | `src/verse/json_min.c:95` 的 `if (cp < 0x80) out[len++] = (char)cp;` 在 `\u0000` 时把**裸 NUL** 塞进字符串，而 `VjVal` 的字符串是**没有长度字段的 `char *s`**（[json_min.h:19](https://github.com/infileap/Inimerse/blob/main/src/verse/json_min.h#L19)）⇒ 所有基于 `strlen` 的下游消费者看到的是被截断的字符串。解析器被 layer / eventlog / protocol / crp / upp / vverse_pack 共用（`src/` 下 **63 处 `vj_str` 调用点**）；`src/platform/http_posix.c:1760` 用它解析**来自网络**的经济域导入包，`:688`/`:705`/`:836` 拿 `account` 名去算 `balances_hash` ⇒ `alice\u0000A` 与 `alice\u0000B` **解析后字节相同**。协调者开工前实测**三处静默出错**（含**违反与 Node 逐字节一致的既有契约**）：`\uD83D\uDE00` → CESU-8 `ED A0 BD ED B8 80`（Node：`F0 9F 98 80`）、孤立代理 → 非法 UTF-8（Node：U+FFFD `EF BF BD`）、`x\u0000y` → `78`（Node：`78 00 79`）。**修法已定**（证据见作业单）：代理对按 4 字节合成、孤立代理输出 U+FFFD、`\u0000` **显式拒绝**——理由是引擎写出器 `for (p = s; *p; p++)` 的循环**无法产出 `\u0000`**（最小可产出转义是 `\u0001`），故拒绝不构成对任何可生成输入的收窄。**不采用**给 `VjVal` 加长度字段（63 调用点连锁，且不解决 CESU-8） | `src/verse/json_min.c`、`src/verse/json_min.h` | 见作业单 [docs/streams/json-min-nul-escape.md](streams/json-min-nul-escape.md) §3：9 行对照表与 Node 逐字节一致（`\u0000` 行显式报错）、修复前必须失败的进树回归、crosscheck 语料扩项仍逐行一致、`\u0001`–`\u001f` 往返、ASan+UBSan 覆盖贴边 `\uXXXX`、`gate.sh` 七阶段 PASS 且 ctest 计数同步。**已交付并合入 `main`（`cef77f5`，merge `60a2338`）**：`src/verse/json_min.c` 103 行改动、新 `src/verse/json_min_probe.c`（76 checks，**链入 `src/verse/upp.c`** 使往返测试用出厂写出器）、`CMakeLists.txt` 的 `verse_json_min_probe`、`tools/gate.sh` 的 `EXP_CTEST` 92→93、crosscheck 语料 101→107；fails-before-fix 探针 `20 failures` → `0`；ASan+UBSan（`detect_leaks=1`）干净；`json_min.c:88` 的 `P->p[0..3]` 前读证得**只靠侥幸安全**并加了显式 NUL 守卫；门禁七阶段 PASS、ctest 93/93。一次被驳回的交付（`canonicalRecord()` 归一化）与反向验证记录见 [STATUS.md](STATUS.md) §10.7 | — |
| 已完成 | `archive-changes-refs` | **两条路都走了**（原来写成「(a) 补说明 **或** (b) 写规则」二选一 —— 但 (b) 单独做不足以消除「检查器绿着而引用断着」，(a) 单独做则每个新断点都要手工补）。**(b)** [STATUS.md](STATUS.md) §1 新增**硬规则 6**：`docs/archive/` 与 `future/archive/` 是历史快照，其引用不受有效性约束，`tools/check_doc_paths.py` 按设计跳过这两个目录（见其 `:24-25` 的 `frozen history` 注释）；并写明推论「只把检查器放绿**不等于**把引用接上」。**(a)** `docs/archive/protocol_v1.md:50` 就地补注 `CHANGES_20260815_safety` 已于 2026-08 随 hygiene 第 1 批删除、内容见 git 历史，并指回 §1 硬规则 6 | `docs/archive/`、`docs/STATUS.md` §1 | 规则明文 **且** 断引用处有去向说明；`links` / `doc-paths` 两阶段仍 0 broken | 协调者 |
| 未认领 | `oauth-bind` | GitHub / Bilibili OAuth token 交换与资料绑定 | `Infiverse_standard/` | 端到端有真实（或明确标注的假）回环证据 | — |
| 未认领 | `forge-panels` | Verse Forge 第一批时空 / 物理 / 蓝图面板 | `Infiverse_standard/` | 面板可用 + 截图或录屏证据 | — |
| 已完成 | `repo-hygiene` | 仓库根残留清理。**第 1 批已合入 `main`（`382ab67`）**：删 24 个 `CHANGES_*.txt`（740 行）＋ `CMakeLists.txt.bak`。**第 2 批已合入 `main`（`6b56af1`，只做桶 B）**：30 个文件 `git mv` 出根（桶 B 22 ＋ 二阶孤儿 8），**30/30 blob 哈希逐字节相同**、无删除、无模式变化，根目录 228→198；核对表固化在 [HYGIENE.md](HYGIENE.md) §10.2，记录见 [STATUS.md](STATUS.md) §10.3。分类方案 146 个候选：**A 删 118 / B 迁 22 / C 留根 6**，A 与 C 一个都没动 | 仓库根**除** `README.md`/`LICENSE`/`CMakeLists.txt`、`examples/`、`docs/HYGIENE.md` | 门禁全绿 ＋ 桶 B 逐文件哈希无变化。见 [STATUS.md](STATUS.md) §10.3 | — |
| 已否决 | `hygiene-bucket-a` | **不删（118 个全部保留）**，§6.1 的 20 个连带项一并保留。原判据是「桶 A 已被 `vtest/` 覆盖」，要删就得先交出「候选文件 ↔ 覆盖它的 `*_test.im` 断言」对照表——**这张表按构造做不出来**：`vtest/` 里 46 个 `*_v04.im` 由 `CMakeLists.txt:340-365` 逐条注册、测的是**语言运行时**，而桶 A 的 28 个 `*_test.im` 测**特性与缺陷复现**，两者不存在「同一断言的两份实现」。且实测**没有任何东西枚举根目录 `.im`**（`CMakeLists.txt` 只注册 `vtest/*.im`，`tools/` 无根目录 runner），而 `contract_test.im:2` 自述 `# usage: inimerse.exe --time-limit 60 contract_test.im`（**手动**套件）、`docs/STATUS.md:567` 与 [HYGIENE.md](HYGIENE.md) §3 又把这批**按名字列为引擎测试清单**——「零引用」量的是自动化引用面，不是价值。桶 B 的 30 个里已有 7 个是**已修缺陷的唯一复现**，桶 A 里同类风险也暴露过一次（7 个脚本被作者从 A 移到 B 保内容），用不可逆删除换根目录少 118 个文件不划算 | 仓库根、`vtest/` | **否决理由与再议条件见 [HYGIENE.md](HYGIENE.md) §11**；重开条件：(a) 真的逐条核对出「每条断言都有等价自动化覆盖」，或 (b) 给这批补一个真正的 runner 把「零引用」变成「被引用」（风险由 runner 承担，且须先分出「可无人值守」与「需人工观察」两类） | 协调者 |
| 已完成 | `docs-audit` | 文档口径复查：`REQUIREMENTS_ANALYSIS.md` 的失效路径实为 **23 条 / 46 处**（板上的 22 条来自 ASCII 反引号 grep，漏掉了 CJK 文件名的 `docs/archive/工作台使用教程.md`），已逐条 `test -e` 改指 `docs/archive/` 或仓库根；新增独立检查器 `tools/check_doc_paths.py` 并接成门禁第 7 阶段 `doc-paths`（`links` 阶段看不见反引号里的路径 —— 它必须先剥离行内代码） | `docs/`、`README.md`、`future/` | `tools/check_links.py` 0 broken **且** `tools/check_doc_paths.py` 0 失效 + 抽查每处数字有出处 | `stream/docs-audit` |

> **2026-08 修订说明（重要）。** 上一版把 `verse-upp` / `verse-crp` / `vverse-pack` 三行写成
> 「未认领」，验收判据是「`node tools/<x>.test.js` 全过」——**这是错的**：那八个套件当时
> 就已存在并且全部通过（合计 92 条断言）。照原样派活，会话一到手就会发现判据早已满足，
> 然后合理地把它标成「已完成」——**板子会自己骗自己**。
>
> 真正缺的不是测试，是**引擎侧实现**：UPP/CRP 的状态机与 `.vverse` 打包器至今只存在于
> `tools/*.js` 参考实现里（共 706 行），引擎没有对应代码，所以「测试全过」证明的是参考
> 实现自洽，不是引擎具备该能力。上表四行已按这个真实缺口重写。
>
> 同时补上一项**当时无法发现的**问题：这 8 个套件当时**没有任何门禁调用它们**
> （`CMakeLists.txt` 只注册 Python 套件），也就是说参考实现发生回归不会让任何门禁变红。
> 已加 `tools/node_suites/run_all.js` 为门禁第四阶段修复，见 §3。

> 其余任务来自 [STATUS.md](STATUS.md) §9.2 末尾「路线图上的下一步」，**已立项**；
> 具体接口定义见 [API.md](API.md) 与 `future/` 下的设计草案，不要在会话里重新发明。

## 6. 已结项（不要再重开）

| slug / 主题 | 结论 | 证据位置 |
| --- | --- | --- |
| §43.5 经济域迁移导入重构 | 方案 B：改用 `src/verse/json_min.{h,c}` 结构化解析 + 逐条重算链哈希，`partial_slice` 由包自身字节推导 | [STATUS.md](STATUS.md) §9.1 |
| Ed25519 `S` 归约缺陷 | `for (i = 0; i < 4; …)` → `i < 8`，失败概率恰为 1/32 | [STATUS.md](STATUS.md) §2.3 |
| Ed25519 长消息静默截断 | 改流式 SHA-512，20 个边界长度全部通过参考实现 | [STATUS.md](STATUS.md) §2.4 |
| P1 最小 Layer 闭环 | `inim-server` / `inim-client` 两进程、canonical JSON、外部锚点 | [STATUS.md](STATUS.md) §5 |
| P0 文档诚实化收口 | `docs/` 39 份 + `future/` 7 份归档，四标记词汇统一 | [STATUS.md](STATUS.md) §1 |
| DSH harness 桥 | `tools/dsh-inimerse/` 五个工具，已装入 web profile | [../tools/dsh-inimerse/README.md](../tools/dsh-inimerse/README.md) |
| `ctest -j12` 偶发失败（`hub_dist_regression` UDP 超时） | 真因是 `verse_http_start()` **先让 TCP 接受、后绑 UDP**：就绪信号说谎约 150 ms，窗口内的单发数据报永久丢失（UDP 不重传）。已改为**先绑 UDP 再监听 TCP** | [STATUS.md](STATUS.md) §2.5 |
| `free_port()` 重复分配 | 单发 `free_port()` 是 TOCTOU；改为 `tools/testports.py` 的 `distinct_ports()` / `PortPool`（持有 socket 直到释放） | [../tools/testports.py](../tools/testports.py) |
| dsh-m 上架（发布部分） | `dsh-inimerse@0.1.0` 已发布，产物与工作副本逐字节一致 | [../tools/dsh-inimerse/marketplace/README.md](../tools/dsh-inimerse/marketplace/README.md) |
| `closure_probe` 堆损坏 | 探针在 `release(e)` 之后仍把 `e` 当源传给 `copy_slot`；Release 下 `assert` 被 `-DNDEBUG` 编译掉，退化成静默堆破坏（十二路并发 3/720）。**只修探针，引擎不动** | [STATUS.md](STATUS.md) §2.6 |
| `socket_probe` exit 11 | 发送后只 peek 一次即断言可读；loopback 有负载时字节尚在途中。改为最多 200 次 × 1 ms 轮询（2/480 → 0/480） | [STATUS.md](STATUS.md) §2.7 |
| hub HTTP 绑定失败静默 | `src/main.c:1013` 只在成功时打印；`errno 98 = EADDRINUSE` 时 hub 半活、调用方等到 10 s 超时。补失败分支响亮报错 | [STATUS.md](STATUS.md) §2.8 |
| 套件端口跨池重叠 | `distinct_ports()` 的保证只在单次调用内成立（两次调用重叠 5/200；`economy_migration` 内部独立取号撞已持有端口 9/500）。合并为单次调用 | [STATUS.md](STATUS.md) §2.9 |
| UPP 引擎侧实现 | `src/verse/upp.{h,c}`（帧编解码 + 分片 decoder + 状态机）；213 条断言探针 + 111 个 op 的**逐事件对照**（`tools/upp_engine_crosscheck.js`） | [STATUS.md](STATUS.md) §10.1 |
| `.vverse` 引擎侧打包器 | `src/common/gzip.{h,c}` + `vverse_pack.{h,c}`；与 JS 参考实现双向交叉验证 47 项，含 node `crypto.verify` | [STATUS.md](STATUS.md) §10.2 |
| CRP 引擎侧线层 | `src/verse/crp.{h,c}`：`FIND`/`PORTAL`/`SIGNAL` 帧、base64url + HMAC-SHA256 能力令牌、FIND 注册表、PORTAL 签发 —— 架在**既有**的 `src/platform/crp_session.{h,c}` 之上，没有重写它。两进程闭回环 27 次交换 0 失败；crosscheck **101 条语料逐行文本相同**；ASan+UBSan 下 173 项检查 0 失败 | [STATUS.md](STATUS.md) §10.6 |
| 仓库根 24 个 `CHANGES_*.txt` | 开发日志（740 行）已删；第 1 批同时清掉主工作区的 `CMakeLists.txt.bak`。第 2 批**只做桶 B**：30 个文件 `git mv` 出根，**30/30 blob 哈希逐字节相同**，根目录 228→198（`6b56af1`）。桶 A 的 118 个**仍未执行** | [HYGIENE.md](HYGIENE.md) §10、[STATUS.md](STATUS.md) §10.3 |
| `REQUIREMENTS_ANALYSIS.md` 失效路径 | 23 条 / 46 处改指 `docs/archive/` 或仓库根；根因是 `check_links.py` 必须剥离行内代码 ⇒ 反引号里的路径它天生看不见，已补第 7 阶段 `doc-paths` | [STATUS.md](STATUS.md) §10.4 |

> **2026-08 集成记录。** 上面四条（UPP / `.vverse` / 仓库根清理 / 文档口径）是
> `.worktrees/` 下四条并行流的产出，由协调者在 `integration/streams-2026-08`
> 上合并、串行跑 `tools/gate.sh` 后进 `main`。合并**没有冲突**（`upp-in-engine` 与
> `vverse-produce` 都改了 `CMakeLists.txt`，但落在不同区段）。
> 两条流各自只看到自己那条分支上的 ctest 数 87，合并后是 **89**（85 + 2 + 2）；
> 这正是 §3 那句「门禁数字变了就必须同时改表和 STATUS.md」要防的事。
>
> 合并提交 `9cf685e` 已推送（`f031565..9cf685e`）。四条流的分支与 worktree 随后一并删除
> （`crp-in-engine` 那个空 worktree 也删了，重新认领时 `stream.sh new` 重建即可）。
> 四份作业单保留在 [streams/](streams/) 并在文件头加了**状态横幅**：它们是**动工前的快照**，
> 里面的「现状」记的是当时的代码事实，判断某件事做没做以 [STATUS.md](STATUS.md) §10 为准。
> 之所以要加横幅，是因为本表 §5 的「2026-08 修订说明」记过同一类事故：原
> `verse-upp` / `verse-crp` / `vverse-pack` 三行的验收判据在写下时就已经成立，会话一到手
> 就会合理地把它标成「已完成」——**文档会自己骗自己**，作业单也一样。

## 7. 行尾：`src/mod/gui_mod.c` 的例外

`.gitattributes` 顶部是 `* text=auto eol=lf`，但 `src/mod/gui_mod.c` **在索引里存的就是 CRLF**（3932 行全部带 `\r`）。两条规则打架的结果：任何新检出都会立刻显示

```
 M src/mod/gui_mod.c                      # git status
 3932 insertions(+), 3932 deletions(-)    # git diff --stat，实际只有 \r 的差异
```

并伴随警告 `warning: in the working copy of 'src/mod/gui_mod.c', CRLF will be replaced by LF`。**这不是谁改坏了文件**——把主工作区那份与新 worktree 那份 `cmp` 一下是逐字节相同的。

危害不在于英文提示难看，而在于 `tools/stream.sh` 的「有没有未提交改动」判断（`rm` / `sync` / 交接时的干净度检查）会对**每个新建的 stream 都误报**，让人开始不信任这个检查本身。

处置：`.gitattributes` 末尾给这一个文件单独定 `eol=crlf`，并**把索引里的 blob 归一化一次且真正提交**（`git add --renormalize src/mod/gui_mod.c`）。归一化必须落进 commit，因为 `.gitattributes` 是**从 commit 读的**，不是从工作区读的——不提交的话，基于旧 commit 建出的新 worktree 依然脏（这一点已实测确认）。

该文件在 [../CMakeLists.txt](../CMakeLists.txt) 的 `if(WIN32)` 分支里（它 `#include <windows.h>`），因此归一化其行尾不影响 Linux 门禁。
