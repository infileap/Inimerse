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
| ctest | `ctest --test-dir build --output-on-failure -j4` | **95 / 95 通过**，无 `WILL_FAIL` 记账项。**数量是断言，不是标签**：`stage_ctest` 会检查输出里确有 `0 tests failed out of 95`，否则阶段失败 —— 光看 ctest 退出码是看不出一整个 `add_test( )` 悄悄掉了的。改动用例数时同步 `tools/gate.sh` 的 `EXP_CTEST` 与本表 |
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
| 已完成 | `vverse-cli`（`stream/vverse-cli`，`task-7`） | **原话要更正（协调者实测）**：`.im` 脚本**能**调用打包内建——`src/mod/verse_dist_mod.c:2693` 注册了 `vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1\|CAP_VERSE\|CAP_NET, 0)`，`src/lobby_online_src.im:40` 早就在用 `r1 = verse_pack(".", "universe/room.vverse")`，我实测一条 3 行 `.im` 能跑通并产出文件（exit 0）。**真正的缺陷是格式**：该内建（`b_verse_pack`，`src/mod/verse_dist_mod.c:1109`，自己手工拼 `"files":{…}` 且用 sha256 **hex**）写出的是**裸 JSON**（实测头 32 字节 `{"id":"src","version":"1.0.0","publisher":"e4610a1d…`），而真正的 `.vverse` 是 **gzip 的 `{"format":"vverse-1","files":{…base64…}}`**（`tools/vverse_pack.js:8` 与 `src/common/vverse_pack.c`）。把内建产物喂给参考实现：`node tools/vverse_pack.js preview <pkg>` → `vverse pack: incorrect header check`，`unpack` 退出码 1 ⇒ **内建是遗留的另一种格式，与整条工具链不兼容**。库路径本身正常：`build/vverse_pack_probe --pack <dir> <out> [seed-hex]`（fixture 需 `manifest.json`/`blueprint.json`/`laws/rule.im`/`mods/entry.im`/`assets/*`，见 `src/common/vverse_pack_probe.c:162`） | `src/mod/verse_dist_mod.c`、`tools/vverse_*` | 一条 `.im` 脚本或 CLI 能直接产出**通过 `node tools/vverse_validate.js <unpacked> --strict`** 的 `.vverse`——即内建必须改走 `src/common/vverse_pack.c` 的容器，而不是自己写 JSON。**已交付并合入 `main`（`82a2789`，merge `9bcf322`）**：`b_verse_pack` 变成 `vverse_pack()` 的薄适配层（163 行手工拼 JSON 删除），`src/common/vverse_pack.c` + `src/common/gzip.c` 链进引擎目标；**顺带修掉一个更深的断裂** —— `verse_open("verse://local/…")` 走遗留 `verse_unpack`（`src/mod/verse_dist_mod.c:685`）不认 gzip，换写侧后引擎**读不懂自己刚写的东西**（`[VDP] bad package json`、`open=0`，且当时无任何测试覆盖），现按首字节嗅探（`1f 8b` ⇒ `vverse_unpack_mem()`），遗留分支逐字节未动。`tools/verse_pack.test.py` 里把遗留容器钉死的步骤 7（对 `vtest_signed.vverse` 逐字节重生成）**退役**，该文件由此变成**没有生产者的冻结遗留输入向量**（角色改变，非覆盖丢失；抬头 `:27-42` 有醒目说明），步骤 5、6 逐字保留以保住遗留**读侧**回归。两条有意为之的能力损失：`seed = NULL` 故不再自动签名、遗留元数据校验对真容器不适用。见 [STATUS.md](STATUS.md) §10.11 | `stream/vverse-cli` |
| 已完成 | `hub-large-package` | **静默截断（真缺陷）**：`src/platform/http_posix.c:312 hub_body()` 的 `GET /v/<id>` 与 `GET /package/<id>` 用 `fread(body, 1, cap, vf)` 读文件，超 `cap` 即截断且**仍返回 200**（HTTP 调用点 `:1063` 缓冲 65536，实测 207991 字节包只回 65536，由 vverse 流交叉测试记录）；UDP 调用点 `:52-55` 缓冲 60001 且要求 `blen < 60000` 才发，即**根本不发、让对端超时**。`POST /package` 与 `POST /content` 上限各 49152。对照：`GET /content/<hash>` 因回算 sha256 会报 `content_corrupt` 500，**不静默** | `src/platform/http_posix.c` | 超大包要么完整送达，要么**明确报错**（413 / 长度协商），禁止 200 + 截断；补 >64 KiB 往返测试。**已交付并合入 `main`（`578eb95`）**：见 [STATUS.md](STATUS.md) §10.10 —— 四条路径改为显式 413（`read_file_capped`；`POST /package/fork` 在 `write_atomic` **之前**拒绝，不落盘）、UDP 发送侧加 `status == 200` 闸、引擎支持 `--port 0` / `--http-port 0` 由内核分配端口并把**真实**端口报出来；端口竞态实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。`tools/vverse_cross.test.py:415-428` 原先把截断**固化成了期望**，现改为断言 413 + 真实大小 | `stream/http-truncation-and-ports` |
| 已完成 | `ws-client-coverage` | `tools/crp_ws_client.js`（23 行）的**连接 / 重连 / 排队三个机制没有任何断言**——`tools/crp_ws_client.test.js` 6 行只有 2 条（入队、`close()` 清空队列）；且 `crp_ws_client` 在 `docs/` 里没有出处。**开工前实测：板上原话「没有任何文档或脚本引用它（`upp_session` 同样 0 引用）」是错的**——`tools/regression.js:4` 的 `tests` 数组里列了 `'crp_ws_client.test.js'`，而 `tools/upp_session` 被 `tools/upp_engine_crosscheck.js:30` 直接 `require`，并被 `docs/REQUIREMENTS_ANALYSIS.md:178` 与 [streams/upp-in-engine.md](streams/upp-in-engine.md):34 点名。另外 `tools/upp_session.test.js`（19 行）**已有约 14 条断言**（心跳乱序、时间戳乱序、staleness 边界、crash→recover→start、`invalid role`），不需要补。两套件**早已在门禁里**（`tools/node_suites/run_all.js:20`/`:24`），故本条流**不需要动 `CMakeLists.txt` 或 `EXP_CTEST`** | `tools/crp_ws_client*`、`tools/upp_session*` | 见作业单 [streams/ws-client-coverage.md](streams/ws-client-coverage.md) §3：连接/重连/排队各自有断言且**离线可复现**、**修复前必须失败**（要贴命令与输出）；`crp_ws_client` 有一处 `docs/` 出处；`gate.sh --fast --only node` 通过；`links`/`doc-paths` 仍 0 broken。**已交付并合入 `main`（`066e8fd`）**：见 [STATUS.md](STATUS.md) §10.9 —— 2 条断言 → **31 条**（连接 7 / 重试 8 / 重连 8 / 排队 8），离线且确定性（在 `require` 之前装手写假 `globalThis.WebSocket`，不引 `ws`、不占端口）；**非空证据 = 变异测试** `tools/crp_ws_client.mutation.py`（29 个单行变异：旧套件只看得见 2 个，新套件 29 个全杀；2 条结构性屏蔽已逐条点名遮蔽者）；`tools/crp_ws_client.js` **未改** —— 31 条断言在未改动模块上即全绿 | `stream/ws-client-coverage` |
| 已完成 | `gate-hermeticity` | **门禁自己骗自己（协调者实测）**：`links` 阶段不封闭。`tools/check_links.py:57-74` 的 `iter_markdown_files()` 用 `os.walk(REPO_ROOT)` 遍历**工作区**，只靠 `SKIP_DIRS`/`SKIP_PREFIXES`/`SKIP_FILES` 黑名单挡杂物。实测 `python3 tools/check_links.py` → **85 markdown files, 314 links, 12 broken**，而 `git ls-files '*.md' \| wc -l` = **78**、`find . -name '*.md' -not -path './.git/*' \| wc -l` = **325**。那 12 条断链**全部来自 `.verify/`** —— 另一个会话在仓库根留下的研究暂存目录，其 `README.md` 引用 `SECURITY.md`/`GOVERNANCE.md`/`MAINTAINERS.md`（GitHub 社区健康文件，本仓库没有）。⇒ **无关会话往仓库根丢一个暂存目录，就能把本仓库的门禁判红**，这会训练人不再信任门禁（正确反应会从「去修引用」退化成「先看看这次红的是不是杂物」）。`tools/check_doc_paths.py` **无**此问题（固定 11 文件清单，实测封闭）。另：`universe/_cache/`（引擎资产缓存，`src/mod/verse_dist_mod.c:1172`）与 `.verify/` 都不被 `.gitignore` 覆盖（`git check-ignore` rc=1） | `tools/check_links.py`、`.gitignore`、`tools/check_doc_paths.py` | 见作业单 [streams/gate-hermeticity.md](streams/gate-hermeticity.md) §4：①报的 `files` 数等于 `git ls-files '*.md' '*.markdown' \| wc -l`；②当前工作区 0 broken 且不再出现 `.verify/README.md`；③**反向验证**——往已跟踪 `.md` 里临时插一条断链必须仍 `exit 1`（否则「0 broken」可能只意味着「什么都没扫」）；④仓库根造未跟踪 `scratch-xyz/README.md` 必须看不见；⑤`gate.sh --fast --only links` 与 `--only doc-paths` 通过。**已交付并合入 `main`（`63c79c3`，merge `fa30036`）**：`tools/check_links.py` 的 `iter_markdown_files()` 改为 `git ls-files -z -- '*.md' '*.markdown'`，`SKIP_DIRS`/`SKIP_PREFIXES`/`SKIP_FILES` 三个黑名单常量**整段删除**（它们就是错误形状的修法）；git 缺失或非零退出时**响亮 `sys.exit`**，**不留 `os.walk` 回退**。**队友还纠正了我的一个错误测量**：我在作业单里写「`check_doc_paths.py` 实测封闭，大概率不用动」是**错的** —— 它 `scan_files()` 用 `os.listdir`，一个**未跟踪**的 `docs/*.md` 带断掉的反引号引用就能让它 `12 files / 1 broken` / exit 1；已按写域条款一并修复，且修法是**用已跟踪集合过滤列表**而非换成 pathspec，理由是 git 的 `docs/*.md` pathspec **会递归匹配 `docs/archive/*.md`**（51 vs 非递归 6），会把该检查器刻意不评判的归档件拖进来。**我亲自复核（不采信自报）**：`--json` 报 `files=78` == `git ls-files '*.md' '*.markdown' \| wc -l` = 78；`.verify/` **仍在磁盘上**的情况下 78 files / 0 broken；反向验证（往已跟踪 `README.md` 插断链）→ `BROKEN README.md -> ./definitely-missing-xyz.md`、1 broken、exit 1，恢复后 `git diff --exit-code` 干净；未跟踪 `scratch-xyz/README.md`（指向 `NOPE.md`）**看不见**；`doc-paths` 的同类反向验证也做了（**逐字输出见 [STATUS.md](STATUS.md) §10.8，那里放在围栏代码块内**）。**这里原先踩了自己的坑**：本单元格最初把探针输出逐字抄进了**行内 code span**，而 `check_doc_paths.py` 只清空**围栏代码块**、不清空行内 span，于是两个并不存在的探针名被当成真引用，`doc-paths` 在 `main` 上判红（`12 markdown files, 167 backtick refs, 2 broken`，exit 1）。**教训**：检查器「跳过围栏块」**不等于**「跳过示例路径」；要贴检查器输出就贴进围栏块，或者别写成 `docs/…` 结尾 `.md` 的形状。修法是改本格的写法，**没有动任何检查器代码** | `stream/gate-hermeticity` |
| 已完成 | `json-min-nul-escape` | `src/verse/json_min.c:95` 的 `if (cp < 0x80) out[len++] = (char)cp;` 在 `\u0000` 时把**裸 NUL** 塞进字符串，而 `VjVal` 的字符串是**没有长度字段的 `char *s`**（[json_min.h:19](https://github.com/infileap/Inimerse/blob/main/src/verse/json_min.h#L19)）⇒ 所有基于 `strlen` 的下游消费者看到的是被截断的字符串。解析器被 layer / eventlog / protocol / crp / upp / vverse_pack 共用（`src/` 下 **63 处 `vj_str` 调用点**）；`src/platform/http_posix.c:1760` 用它解析**来自网络**的经济域导入包，`:688`/`:705`/`:836` 拿 `account` 名去算 `balances_hash` ⇒ `alice\u0000A` 与 `alice\u0000B` **解析后字节相同**。协调者开工前实测**三处静默出错**（含**违反与 Node 逐字节一致的既有契约**）：`\uD83D\uDE00` → CESU-8 `ED A0 BD ED B8 80`（Node：`F0 9F 98 80`）、孤立代理 → 非法 UTF-8（Node：U+FFFD `EF BF BD`）、`x\u0000y` → `78`（Node：`78 00 79`）。**修法已定**（证据见作业单）：代理对按 4 字节合成、孤立代理输出 U+FFFD、`\u0000` **显式拒绝**——理由是引擎写出器 `for (p = s; *p; p++)` 的循环**无法产出 `\u0000`**（最小可产出转义是 `\u0001`），故拒绝不构成对任何可生成输入的收窄。**不采用**给 `VjVal` 加长度字段（63 调用点连锁，且不解决 CESU-8） | `src/verse/json_min.c`、`src/verse/json_min.h` | 见作业单 [docs/streams/json-min-nul-escape.md](streams/json-min-nul-escape.md) §3：9 行对照表与 Node 逐字节一致（`\u0000` 行显式报错）、修复前必须失败的进树回归、crosscheck 语料扩项仍逐行一致、`\u0001`–`\u001f` 往返、ASan+UBSan 覆盖贴边 `\uXXXX`、`gate.sh` 七阶段 PASS 且 ctest 计数同步。**已交付并合入 `main`（`cef77f5`，merge `60a2338`）**：`src/verse/json_min.c` 103 行改动、新 `src/verse/json_min_probe.c`（76 checks，**链入 `src/verse/upp.c`** 使往返测试用出厂写出器）、`CMakeLists.txt` 的 `verse_json_min_probe`、`tools/gate.sh` 的 `EXP_CTEST` 92→93、crosscheck 语料 101→107；fails-before-fix 探针 `20 failures` → `0`；ASan+UBSan（`detect_leaks=1`）干净；`json_min.c:88` 的 `P->p[0..3]` 前读证得**只靠侥幸安全**并加了显式 NUL 守卫；门禁七阶段 PASS、ctest 93/93。一次被驳回的交付（`canonicalRecord()` 归一化）与反向验证记录见 [STATUS.md](STATUS.md) §10.7 | — |
| 已完成 | `archive-changes-refs` | **两条路都走了**（原来写成「(a) 补说明 **或** (b) 写规则」二选一 —— 但 (b) 单独做不足以消除「检查器绿着而引用断着」，(a) 单独做则每个新断点都要手工补）。**(b)** [STATUS.md](STATUS.md) §1 新增**硬规则 6**：`docs/archive/` 与 `future/archive/` 是历史快照，其引用不受有效性约束，`tools/check_doc_paths.py` 按设计跳过这两个目录（见其 `:24-25` 的 `frozen history` 注释）；并写明推论「只把检查器放绿**不等于**把引用接上」。**(a)** `docs/archive/protocol_v1.md:50` 就地补注 `CHANGES_20260815_safety` 已于 2026-08 随 hygiene 第 1 批删除、内容见 git 历史，并指回 §1 硬规则 6 | `docs/archive/`、`docs/STATUS.md` §1 | 规则明文 **且** 断引用处有去向说明；`links` / `doc-paths` 两阶段仍 0 broken | 协调者 |
| 阻塞 | `oauth-bind` | GitHub / Bilibili OAuth token 交换与资料绑定 | `Infiverse_standard/` | 端到端有真实（或明确标注的假）回环证据。**阻塞原因（协调者实测）：工具链不在这台机器上。** `Infiverse_standard/` 是 Tauri（Rust + WebView2）桌面应用，而本沙箱**没有 `cargo`、没有 `rustc`**（`command -v` 均为空），只有 Windows 侧的 npm 全局 shim `/mnt/c/Users/Lenovo/AppData/Roaming/npm/tauri`；`Infiverse_standard/README.md` 自述编译环境是 `D:\Infiverse_standard\` + `%USERPROFILE%\.cargo`（用户级 rustup、rsproxy 镜像）。⇒ **本环境既不能构建也不能验证**；OAuth 回环需要真实弹窗与回调，判据「截图或录屏」也只能在 Windows 侧产出 | — |
| 阻塞 | `forge-panels` | Verse Forge 第一批时空 / 物理 / 蓝图面板 | `Infiverse_standard/` | 面板可用 + 截图或录屏证据。**阻塞原因同上**：面板住在 `Infiverse_standard/src/ui/`（纯前端 HTML/CSS/JS，可单独预览），但「可用」的验收挂在 Tauri 壳里，而 Rust 工具链不在本环境。**可拆出的部分**：`src/ui/` 的纯前端逻辑理论上可在本环境开发并用 DOM 断言验证，但那会把一条验收判据降级成「DOM 断言通过」而不是「面板可用」—— 判据要改就必须先明说，不能悄悄放宽 | — |
| 已完成 | `repo-hygiene` | 仓库根残留清理。**第 1 批已合入 `main`（`382ab67`）**：删 24 个 `CHANGES_*.txt`（740 行）＋ `CMakeLists.txt.bak`。**第 2 批已合入 `main`（`6b56af1`，只做桶 B）**：30 个文件 `git mv` 出根（桶 B 22 ＋ 二阶孤儿 8），**30/30 blob 哈希逐字节相同**、无删除、无模式变化，根目录 228→198；核对表固化在 [HYGIENE.md](HYGIENE.md) §10.2，记录见 [STATUS.md](STATUS.md) §10.3。分类方案 146 个候选：**A 删 118 / B 迁 22 / C 留根 6**，A 与 C 一个都没动 | 仓库根**除** `README.md`/`LICENSE`/`CMakeLists.txt`、`examples/`、`docs/HYGIENE.md` | 门禁全绿 ＋ 桶 B 逐文件哈希无变化。见 [STATUS.md](STATUS.md) §10.3 | — |
| 已否决 | `hygiene-bucket-a` | **不删（118 个全部保留）**，§6.1 的 20 个连带项一并保留。原判据是「桶 A 已被 `vtest/` 覆盖」，要删就得先交出「候选文件 ↔ 覆盖它的 `*_test.im` 断言」对照表——**这张表按构造做不出来**：`vtest/` 里 46 个 `*_v04.im` 由 `CMakeLists.txt:340-365` 逐条注册、测的是**语言运行时**，而桶 A 的 28 个 `*_test.im` 测**特性与缺陷复现**，两者不存在「同一断言的两份实现」。且实测**没有任何东西枚举根目录 `.im`**（`CMakeLists.txt` 只注册 `vtest/*.im`，`tools/` 无根目录 runner），而 `contract_test.im:2` 自述 `# usage: inimerse.exe --time-limit 60 contract_test.im`（**手动**套件）、`docs/STATUS.md:567` 与 [HYGIENE.md](HYGIENE.md) §3 又把这批**按名字列为引擎测试清单**——「零引用」量的是自动化引用面，不是价值。桶 B 的 30 个里已有 7 个是**已修缺陷的唯一复现**，桶 A 里同类风险也暴露过一次（7 个脚本被作者从 A 移到 B 保内容），用不可逆删除换根目录少 118 个文件不划算 | 仓库根、`vtest/` | **否决理由与再议条件见 [HYGIENE.md](HYGIENE.md) §11**；重开条件：(a) 真的逐条核对出「每条断言都有等价自动化覆盖」，或 (b) 给这批补一个真正的 runner 把「零引用」变成「被引用」（风险由 runner 承担，且须先分出「可无人值守」与「需人工观察」两类） | 协调者 |
| 已完成 | `docs-audit` | 文档口径复查：`REQUIREMENTS_ANALYSIS.md` 的失效路径实为 **23 条 / 46 处**（板上的 22 条来自 ASCII 反引号 grep，漏掉了 CJK 文件名的 `docs/archive/工作台使用教程.md`），已逐条 `test -e` 改指 `docs/archive/` 或仓库根；新增独立检查器 `tools/check_doc_paths.py` 并接成门禁第 7 阶段 `doc-paths`（`links` 阶段看不见反引号里的路径 —— 它必须先剥离行内代码） | `docs/`、`README.md`、`future/` | `tools/check_links.py` 0 broken **且** `tools/check_doc_paths.py` 0 失效 + 抽查每处数字有出处 | `stream/docs-audit` |
| 已完成 | `crp-portal-auth`（`stream/crp-portal-auth`，`task-8`） | **能力令牌的签发没有任何调用方认证（协调者实测，引擎与参考实现一致）**。`POST /portal` 是**唯一**的授权入口：拿到令牌后 `/signal`、`/session/resume`、`/session/stop`、`/ws` 都要求令牌（`tools/crp_relay.js:70`/`:77`/`:83`/`:137`）⇒ **令牌本身就是全部授权**，而它可以被任何人索取。参考实现 `tools/crp_relay.js:63-65` 只检查「verse 已注册且 `peer` 非空」，**不看调用方是谁**；引擎 `src/verse/crp_hub.c:240-241` → `crp_registry_portal`（`src/verse/crp.c:1017`）**行为一致**，唯一的前置 `verify_frame`（`src/verse/crp_hub.c:158-187`）是**自洽性**检查（客户端自带的 `frameText` 的 `type` 与 payload 字段须与请求一致），**不含签名、不含身份**。具体危害不止「拿到令牌」：`crp_registry_portal` 还会 `registry_get_session`（`src/verse/crp.c:879-899`，**不存在就新建**）并 `im_crp_session_apply("start")` + `im_crp_session_lease_begin`（`:1036-1041`）⇒ 未认证调用方①可用任意 `peer` 字符串**无上限地造 session**（内存 DoS），②若该 (verse, peer) 已有会话，**会重置并夺取租约**（会话劫持）。**这不是引擎实现缺陷** —— 引擎忠实实现了参考实现；它是**契约级缺口**，所以修它必须**先动判据方**（参考实现），否则 crosscheck 立刻分歧 | `tools/crp_relay.js`、`src/verse/crp_hub.c`、`src/verse/crp.c` | **需先决断**（见下）：(a) 认定不在威胁模型内 ⇒ 在 [STATUS.md](STATUS.md) 写明「hub 假定处于受信网络，签发即授权」并**保留现状**；(b) 认定是缺陷 ⇒ **先改参考实现**（例如 `/portal` 要求带一个 hub 侧预共享的 enrollment 凭证，或把「谁可以为一个 peer 开 portal」变成已注册事实），再让引擎跟随并同步 crosscheck 语料。**未经此决断，任何单方面给引擎加认证的改动都会被 `tools/crp_engine_crosscheck.js` 判为分歧**。**已交付并合入 `main`（`40f6094`，fast-forward `922f9af..40f6094`）**：见 [STATUS.md](STATUS.md) §10.12 —— 走的是板上 (b)：**先改判据方**（`tools/crp_relay.js` 的 `enrollProof()`），引擎跟随。判定点**只有一处** `crp_enroll_check()`（`src/verse/crp.c:1057`），`crp_registry_portal` 与 HTTP 监听器都调它；未配置 `CRP_ENROLL_SECRET` ⇒ fail-closed 403；两条拒绝都在查注册表**之前**。第一版（`e47f903`）被驳回：它在 `src/platform/http_posix.c` 里**又写了一份原始文本扫描推导**并声称与 `enrollProof()` 逐字节一致，协调者用裸 socket 证伪三处（`\"` 转义不还原、冒号后不跳换行、`strstr` 扫整个请求故请求头能遮蔽 body）；第二版把那份推导**整段删除**，改为 `vj_parse` + 同一个检查。**遗留四点，都不是本次引入 —— 每一条都在 main `922f9af` 上原样复现**：①空 scope 被 `token_allows`（`src/platform/http_posix.c:287`）读成「任意」⇒ 非字符串 verse/peer 铸出 hub 全域通配令牌（main 上同样复现，且**连证明都不需要**，故本次是**收紧**）；②重复 JSON 键 first-wins vs `JSON.parse` last-wins；③非字符串值回显差异；④监听器 `/portal` 仍不查注册表。四点见 `task-9` | 协调者 |
| 已完成 | `crp-portal-postcheck`（`stream/crp-portal-postcheck`，`task-9`，作业单 [streams/crp-portal-postcheck.md](streams/crp-portal-postcheck.md)） | **`crp-portal-auth` 的遗留四点**（详见 [STATUS.md](STATUS.md) §10.12 末段，逐条都已在 main `922f9af` 上复现，故没阻塞那次合入）。最重的一条是**空 scope 等于「任意」**：`token_allows`（`src/platform/http_posix.c:287`）的 `(!g_tokens[i].verse[0] \|\| …)` 把「没有 scope」读成「所有 scope」，而 `/portal` 用 `vj_str(vj_get(pbody,"verse"), "")`（`:2046-2047`）取 scope，**对任何非字符串 JSON 值都得到 `""`** ⇒ `{"verse":5,"peer":null}` 铸出 hub 全域通配令牌。端到端实测：铸出后 `POST /signal` / `POST /session/stop` 打 `{"verse":"totally-other-verse","peer":"other-peer"}` → **200 `{"ok":true,"accepted":true}`**（对照：`{"verse":"demo","peer":"p1"}` 铸的令牌 → 403）。另三点：重复 JSON 键 first-wins vs `JSON.parse` last-wins（`vj_get` 取首个，main 的 `json_field_string` 也是首个）；非字符串值回显差异（参考实现回显原值 `5`，监听器回显 `""`）；监听器 `/portal` **仍不查注册表**，对未注册 verse 也铸令牌，而 `crp_registry_portal` 与 `tools/crp_relay.js` 都答 404 `verse not found` —— `src/platform/http_posix.c:2023-2028` 的注释称调用方证明了自己可以为「this exact (verse, peer)」开 portal，对非字符串值这句不成立 | `src/platform/http_posix.c`、`src/verse/crp.c`、`src/verse/crp.h`、`tools/` | 监听器的 `/portal` 要么整体走 `crp_registry_portal`（一条路径、一套拒绝），要么复现它的证明后拒绝：非字符串 verse → 404、falsy peer → 404，且不再有「意外为空」的 scope。补一条回归：用非字符串 verse 铸发后，该令牌对**另一个** verse 必须被拒。**已交付并合入 `main`（`d565253`，merge `c400223`）** —— 见 [STATUS.md](STATUS.md) §10.13：**判据方先改**（`tools/crp_relay.js` 的 `/portal` 要求 verse 是字符串、已注册，`peer` 是非空字符串），监听器新增 `portal_verse_registered()` 扫 `g_verses`（即 `POST /register` 填的那张表，与参考实现的 `verses` Map 同一集合），`token_register` 拒存空 scope 半边，`token_allows` 不再把「没有 scope」读成「所有 scope」，scope 改用 `vv->s`/`pv->s` 直取。协调者裸 socket 探针 **11 条断言全过**，**同一探针打 main 是 6 条 FAIL**（含逐字的 hub 全域通配令牌）。**行为改变**：`/portal` 只为 `POST /register` 注册过的 verse 铸发令牌，三个在树套件已补 `/register`。遗留 **R2 重复 JSON 键**（`vj_get` first-wins vs `JSON.parse` last-wins）未调和，原因见 §10.13 | 协调者 |
| 进行中 | `xlang-bridge`（`stream/xlang-bridge`，`task-10`，作业单 [streams/xlang-bridge.md](streams/xlang-bridge.md)） | **v0.5 补全 ①：跨语言绑定补真。** `RELEASE_0.5.0.md` 声称的 `inimerse_extension.c`（`PyInit_inimerse()`）与 `InimerseBridge.java` 在仓库中**都不存在**，`.whl`/`.jar` 也不存在 —— 而**生成器 `tools/bindgen.py` 早已存在且被测过**（CTest `bindgen_regression`/`scan_tools_regression`），`examples/{python,java}_bridge.im` 只是 `say "see …"` 的说明脚本。⇒ 缺的不是「生成绑定的能力」，是「**被生成出来的东西真能跑**」 | **`CMakeLists.txt`（本波独占）**、`src/bridge/**`（新）、`bindings/**`（新）、`tools/bindgen*`、`tools/python_scan.py`、`examples/{interface.def,build.gradle,pom.xml,*_bridge.im}` | 干净 venv 里 `import inimerse` 成功且结果等于期望值；`java -cp <jar>` 打印**同一**期望值；`.whl`/`.jar` 真实存在且可重复构建；两条新回归**在 main 上失败**；`gate.sh` **全量**七阶段 PASS。见作业单 §4 | `vversecli`（跑 `xlang-bridge`） |
| 进行中 | `wasm-simd-gc`（`stream/wasm-simd-gc`，`task-11`，作业单 [streams/wasm-simd-gc.md](streams/wasm-simd-gc.md)） | **v0.5 补全 ②：Wasm SIMD / GC。** `src/compilation/wasm_backend.h:10` 原文 `SIMD/GC/heaps are future work.`，而 `RELEASE_0.5.0.md:38` 声称「supporting SIMD optimizations and WebAssembly GC」。当前是 MVP 数值子集，判据底座是 `tools/wasm_backend.test.py` 的**解释器 vs wasm host 双跑等价性**（13 例）。目标二选一：真做出来（线性内存堆 + 回收、`v128` SIMD，**要有实测数字**），或给出证据充分的降级结论并**改写那句 `future work`** | `src/compilation/wasm_backend.{c,h}`、`tools/wasm_backend.test.py`、`tools/wasm_run.js`、`docs/WASM*.md`。**不碰 `CMakeLists.txt`**（本波由 `xlang-bridge` 独占） | 等价性断言在新增特性上仍全过；新例**在 main 上失败**；SIMD 有实测数字或明确降级；`wasm_backend.h:10` 那句已改写；`gate.sh` 全量 PASS 且 **ctest 计数未变** | `jsonmin`（跑 `wasm-simd-gc`） |
| 进行中 | `aot-backend`（`stream/aot-backend`，`task-12`，作业单 [streams/aot-backend.md](streams/aot-backend.md)） | **v0.5 补全 ③：AOT。** `RELEASE_0.5.0.md:43` 声称「outperform the interpreter by at least 2x」，实测 **1.09x**，且 `--aot` 是**打包通道**（引擎副本 + 嵌入规范化字节码，**复用同一个 C 解释器**），`SELFHOST_BENCHMARK.md:39` 自己已承认不满足 ≥2x；口径散在至少三份文档里且互相不一致。**先测量再动手**：原样复现 1.09x 并写清分子分母 → 然后（A）做真数值子集 AOT 并给出**测出来的**加速比，或（B）给出证据充分的降级结论并把口径收敛到一处 | `src/compilation/aot*`（新）、`tools/selfhost_bench.py`、`tools/perf_compare.py`、`docs/archive/SELFHOST_BENCHMARK.md`。**第一波不碰 `CMakeLists.txt` 与 `src/main.c`**（后者只读） | 1.09x 原样复现且分子分母明确；`--aot` 真实行为有源码行号；加速比是**测出来**的；口径收敛到一处；`gate.sh` 全量 PASS 且 ctest 计数未变 | `crp-engine`（跑 `aot-backend`） |
| 进行中 | `selfhost-0.5-record` | **v0.5 补全 ④：自举对比记录。** `docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md` 文末「0.5.0 自举对比记录（待填充）」的 4 项仍是 `[ ]`（二进制大小、测试通过率、运行时版本一致性、性能对比），且其中命令指向**已被删除的 `build-local`**。P0 其余 6 项在 [STATUS.md](STATUS.md) §5 已 `[x]`，这是「版本号统一」剩下的唯一尾巴 | `docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md`、`selfhost/` | 四项有数值，且命令在当前树上可复现（不指向已删除目录） | 协调者 |
| 待验收 | `agent-bridge`（`stream/agent-bridge`，`task-13`，作业单 [streams/agent-bridge.md](streams/agent-bridge.md)） | **多智能体协调桥接层的复核、更新与接流程。第一轮只出设计，不写代码。** 设计文档 `future/multi-agent-coordination-bridge.md`（624 行）质量很高，但由 `88a2109`（subject 只谈 doc-paths 门禁）卷进 `main`，**那条 commit 的正文一个字都没提它**；`git log --all` 只有这一条，除自身外**全仓库零引用**，而 `future/README.md:11` 还写着「保留的 **3** 份指导文件」、`future/` 下却有 4 份内容文件 ⇒ 按 BOARD §5 的规矩，它此前**等于不存在**。本轮三件事：① §9 的证据数字全部重跑更新到当前 `main`（`e6e4936`，旧值几乎全过期：`out of 15`→**16**、`crp_probe: 173`→**185**、`107 records`→**115**、`crp_peer: 27`→**31**、`exchanges:26`→**30**、`crp_closed_loop: 17`→**19**、`EXP_CTEST 93`→**95**），并把漂移的行号重定位（`crp.c:1102-1105`→**`:1176-1182`**、`crp.c:1082`/`:1146`→**`:1155-1156`/`:1220`**）；② §7.7 整行改写 —— 前提今天变了一半，理由必须拆成两句：**(a) 我们加的只是调用方认证**（enrollment 证明＝知道 hub 级共享 secret），**(b) 不是 per-`(verse, peer)` 授权**（证明的唯一输入就是这两个字符串，没有第三个因子）⇒ 「`UPDATE` = `PORTAL` + `SIGNAL` **仍只是形式上的**」，缺口从「完全没有检查」变成「只有 hub 级成员资格」；并同步 §2.2 的 ⚠️ 段落与判定标注、新增 §7.9（**智能体的身份今天没有任何已验证的载体**：令牌 scope 被认证但 `peer` 由调用方自选、Layer 的 `actor`/`role` 是自由字符串、`CAP_AI` 是进程内自声明、`ai_boundary` 只是子串匹配 ⇒ 桥接层不得采信调用方给的 `actor`/`role`，承诺的 debtor 必须由令牌 scope 派生）；③ 接进流程：`future/README.md` 3→4 份并列表、`docs/STATUS.md` 新增 §9.3 路线图条目（挂靠 §4 阶段四「强制 AI 标识与权限边界」）、BOARD 本行 | `future/multi-agent-coordination-bridge.md`、`future/README.md`、`docs/streams/agent-bridge.md`、`docs/BOARD.md`（**仅 §5 本行**）、`docs/STATUS.md`（**仅 §9 新增一节**）。**禁碰** `src/**`、`tools/**`、`CMakeLists.txt`、`.gitattributes`、`future/archive/**`、`docs/BOARD.md` §1–§4。**本轮不需要 `CMakeLists.txt`**（本波由 `xlang-bridge` 独占） | 见作业单 [streams/agent-bridge.md](streams/agent-bridge.md) §3：① `tools/gate.sh` **全量**七阶段 PASS 且 **ctest 计数仍是 95**（本轮不新增 CTest、不改 `CMakeLists.txt`）；② 新增文档先 `git add` 再跑门禁（两个检查器只枚举 `git ls-files`）；③ 作业单 §5 的复现命令原样跑得出 §2.1 对照表里的每个新值；④ `git diff --name-only main...HEAD` **只含文档**（`src/**`/`tools/**`/`CMakeLists.txt` 零改动）；⑤ 旧数字只允许出现在「行号漂移提示」这类注解性说明里，正文不再作为事实引用。**已交付（分支 `stream/agent-bridge`，base `e6e4936`；内容提交 `f0333a0`）：`git diff --numstat e6e4936..HEAD` = 只改 5 个文档、+209 / −27 行（`docs/BOARD.md` 1、`docs/STATUS.md` 18、`docs/streams/agent-bridge.md` 150、`future/README.md` +2/−1、本设计文档 +38/−26），`src/**`、`tools/**`、`CMakeLists.txt`、`.gitattributes` 零改动；`tools/gate.sh` 全量七阶段全 PASS（`gate: OK — every stage passed.`，ctest 95/95、economy 39/39、node 11/11、plugin 55/55、links 0 broken、doc-paths 0 broken）。交付时 `main` 已前进到 `117456f`（`aot-backend` 合入），与本流文件零交集，且 `EXP_CTEST` 仍为 95** | `stream/agent-bridge` |

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
