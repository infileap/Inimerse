# 证据保管：`infiverse-handoff` 判据② 的两份工作稿

- **产出者**：`noble-orchid`（会话 `session-876bbc10-7b85-493b-93e9-f0ae3feba57a`）
- **产出时刻**：2026-10-21 18:50（该轮派活被撤回之前）
- **保管者**：`ivory-zephyr`，收进本仓库的 `stream/board-status-refresh` 分支
- **状态**：**工作稿，不是交付，不是提交**。分支 `stream/infiverse-handoff-anchors` 已删除、其工作树已移除，两份稿子当时**从未提交**。

## 为什么在这里

`noble-orchid` 把这两份存在 `inimerse/.scratch/handoff-anchors-bak/`，而 `.scratch/` 被 `.gitignore:96` 覆盖 ⇒ **不在版本控制里**，且同目录是「会被清理」的位置。它自己也报了这个毛病（「把证据保存在一个会被环境清掉的路径上」）。

我一度把副本搬到 `/home/sakiko/handoff-anchors-salvage/`（仓库外），随后又把那里删了——**删副本时原件仍在 `.scratch`，所以内容没有丢**，但这件事暴露出一个真问题：**「抢救」到仓库外与「抢救」到仓库内，是两件不同的事**。现在收进这里，才算真的保管住。

## 两份文件是什么

| 文件 | 行数 | 内容 |
|---|---|---|
| `workorder.md.txt` | 82 | 作业单草案：逐 ref 表格、性质判定、以及「写作时就一半错」的三条理由 |
| `handoff-patcher-draft.md.txt` | 205 | `docs/HANDOFF_INFIVERSE.md` 的**改后草稿**（对应原文件 200 行） |

## 认领方该怎么用

`docs/BOARD.md` 第 285 行... 不，是第 101 行（`infiverse-handoff`）的认领是 `stream/infiverse-handoff`。**这两份稿子对它的用途只有两种**：

1. 照 `workorder.md.txt` 里的逐 ref 表格**重新演一遍**，取用其中的结论、自己独立复跑；
2. 或者**放弃它们**、从头做。

**不可以做的是**：把 `handoff-patcher-draft.md.txt` 直接当成交付提交上去——它没有跑过门禁、没有经过认领方复核，而且产出者的那一轮是被撤回的。

## 一个必须一起读的结论

`workorder.md` 里有一条比「行号漂移」更硬的判定：那两个数**从来没在同一棵树上同时成立过**。我独立复核过这条（六个两处同时成立的提交 `4e1b58c`／`e0139d2`／`6287523`／`c89e077`／`01da596`／`e99f437`，**没有一个**是 `fb92b57` 的祖先）。⇒ 它是「写作时就一半错」，不是「后来漂了」。

## 为什么扩展名带 `.txt`

它们**不是本仓的文档**，是别人工作稿的原样保管件。`noble-orchid` 的稿子含 25 处未带观测点的 `文件:行` 引用，若以 `.md` 存进 `docs/`，`check_obs_markers.py` 会把它们算作**指向 HEAD 的指针**，吃掉「指针 delta 天花板（211）」的余量。

处置：**原样保存、不改一个字**，用 `.md.txt` 使它不进入 `docs/**/*.md` 扫描集。认领方若要采用，取内容重做时自然按仓库规矩带 `[obs:]`。
