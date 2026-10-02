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

合入 main 前必须全绿。六个阶段，任一失败即整体失败：

| 阶段 | 命令 | 期望 |
| --- | --- | --- |
| build | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | 0 error |
| ctest | `ctest --test-dir build --output-on-failure -j4` | **85 / 85 通过**，无 `WILL_FAIL` 记账项 |
| economy | `python3 tools/economy_migration.test.py` | `economy migration: ok`（**39 / 39**） |
| plugin | `node tools/dsh-inimerse/verify.mjs --live` | **55 / 55** |
| node | `node tools/node_suites/run_all.js` | **11 / 11**（`tools/` 下每个可独立运行的 JS 套件；`crp_session_flow.test.js` 需活跃 hub，由 CTest 的 `crp_session_flow_regression` 驱动，不在此列） |
| links | `python3 tools/check_links.py` | **0 broken**（当前 66 个 md / 211 条链接 / 205 条本地链接） |

```bash
tools/gate.sh                # 全量
tools/gate.sh --fast         # 跳过 configure，复用已有 build/
tools/gate.sh --only links   # 只跑一个阶段：build|ctest|economy|node|plugin|links
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

状态取值：`未认领` / `进行中` / `待验收` / `已完成` / `阻塞`。

| 状态 | slug | 任务 | 冲突域（别人别碰） | 验收判据 | 认领 |
| --- | --- | --- | --- | --- | --- |
| 阻塞 | `marketplace-watch` | 上架 dsh-m：npm 包已发布，PR [iasiv5/dsh-m#1](https://github.com/iasiv5/dsh-m/pull/1) 等待维护者点 *Approve and run* | `tools/dsh-inimerse/marketplace/` | PR 合并后 `node scripts/validate-registry.mjs` 全绿 | 协调者 |
| 未认领 | `upp-in-engine` | UPP 目前只有 **JS 参考实现**（`tools/upp_reference.js` + `upp_session.js`，29 条断言）；引擎侧没有对应状态机 | `src/verse/`、`src/mod/verse_dist_mod.c` | 引擎能跑完 hello→start→heartbeat→（失联）crash→recover→reset 全序列，且与 JS 参考实现逐事件对照一致 | — |
| 未认领 | `crp-in-engine` | CRP 的 `FIND` / `PORTAL` 回环签名校验同样只在 JS 参考实现里；引擎侧无实现 | `src/verse/`、`src/mod/verse_dist_mod.c` | 引擎实现能通过 `tools/crp_relay.test.js` 的等价场景，且有真实两进程证据 | — |
| 未认领 | `vverse-produce` | `.vverse` **打包器**（`tools/vverse_pack.js`）只在 JS 侧；引擎不会产出 `.vverse` | `src/`、`vtest/` | 引擎产出的 `.vverse` 能通过 `tools/vverse_validate.js` 校验并被 `inim-server` 装载 | — |
| 未认领 | `ws-client-coverage` | `tools/crp_ws_client.js` 只有 **1 条**断言，且没有任何文档或脚本引用它（`upp_session` 同样 0 引用） | `tools/crp_ws_client*`、`tools/upp_session*` | 连接/重连/排队各自有断言；两个套件至少被一份文档引用 | — |
| 未认领 | `oauth-bind` | GitHub / Bilibili OAuth token 交换与资料绑定 | `Infiverse_standard/` | 端到端有真实（或明确标注的假）回环证据 | — |
| 未认领 | `forge-panels` | Verse Forge 第一批时空 / 物理 / 蓝图面板 | `Infiverse_standard/` | 面板可用 + 截图或录屏证据 | — |
| 未认领 | `repo-hygiene` | 仓库根残留清理（24 个 `CHANGES_*.txt`、`_t_bisect.im`、`CMakeLists.txt.bak`、`nst2.inim`、`params*`、`vtest_signed.vverse`、若干 `*.html`） | 仓库根**除** `README.md`/`LICENSE`/`CMakeLists.txt` | 门禁全绿 + 根目录只剩应有的文件 | — |
| 未认领 | `docs-audit` | 文档口径复查：README 基线数字、四标记词汇一致性、`archive/` 引用 | `docs/`、`README.md`、`future/` | `tools/check_links.py` 0 broken + 抽查每处数字有出处 | — |

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
