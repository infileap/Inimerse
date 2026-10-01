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

合入 main 前必须全绿。五个阶段，任一失败即整体失败：

| 阶段 | 命令 | 期望 |
| --- | --- | --- |
| build | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | 0 error |
| ctest | `ctest --test-dir build --output-on-failure -j4` | **85 / 85 通过**，无 `WILL_FAIL` 记账项 |
| economy | `python3 tools/economy_migration.test.py` | `economy migration: ok`（**39 / 39**） |
| plugin | `node tools/dsh-inimerse/verify.mjs --live` | **55 / 55** |
| links | `python3 tools/check_links.py` | **0 broken**（当前 66 个 md / 209 条链接 / 203 条本地链接） |

```bash
tools/gate.sh                # 全量
tools/gate.sh --fast         # 跳过 configure，复用已有 build/
tools/gate.sh --only links   # 只跑一个阶段：build|ctest|economy|plugin|links
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
| 未认领 | `verse-upp` | UPP 本地参考协议与 Verse manifest | `tools/upp_reference*`、`src/verse/` | `node tools/upp_reference.test.js` 全过 + 新增契约测试 | — |
| 未认领 | `verse-crp` | CRP `FIND` / `PORTAL` 回环与签名校验 | `tools/crp_reference*`、`src/verse/` | `node tools/crp_reference.test.js` 全过 + 回环有真实进程证据 | — |
| 未认领 | `vverse-pack` | `.vverse` 打包、预览、下载与启动 | `tools/vverse_*`、`vtest/` | `node tools/vverse_validate.test.js` 全过 + 打包产物可被客户端启动 | — |
| 未认领 | `oauth-bind` | GitHub / Bilibili OAuth token 交换与资料绑定 | `Infiverse_standard/` | 端到端有真实（或明确标注的假）回环证据 | — |
| 未认领 | `forge-panels` | Verse Forge 第一批时空 / 物理 / 蓝图面板 | `Infiverse_standard/` | 面板可用 + 截图或录屏证据 | — |
| 未认领 | `repo-hygiene` | 仓库根残留清理（24 个 `CHANGES_*.txt`、`_t_bisect.im`、`CMakeLists.txt.bak`、`nst2.inim`、`params*`、`vtest_signed.vverse`、若干 `*.html`） | 仓库根**除** `README.md`/`LICENSE`/`CMakeLists.txt` | 门禁全绿 + 根目录只剩应有的文件 | — |
| 未认领 | `docs-audit` | 文档口径复查：README 基线数字、四标记词汇一致性、`archive/` 引用 | `docs/`、`README.md`、`future/` | `tools/check_links.py` 0 broken + 抽查每处数字有出处 | — |

> 路线图 1–5 来自 [STATUS.md](STATUS.md) §9.2 末尾「路线图上的下一步」，**已立项**；
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
