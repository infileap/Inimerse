# 流简报：`repo-hygiene`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/repo-hygiene`，冲突域**仓库根**（除 `README.md` / `LICENSE` / `CMakeLists.txt`）。

> **状态：第 1 批已交付（`382ab67`），第 2 批未执行 —— 本条流没做完。**
> 第 1 批（删 24 个 `CHANGES_*.txt` + `CMakeLists.txt.bak`）已合入 `main`；
> 第 2 批的 146 个候选只是 [HYGIENE.md](../HYGIENE.md) 里的**方案**，一个文件都没动。
> 交付内容与**最大判断风险**（桶 A 的「已被 `vtest/` 覆盖」是自述推断，未逐条对照断言）见
> [STATUS.md §10.3](../STATUS.md#103-仓库根清理repo-hygiene)。
> **本文件是动工前的作业单**，判断某件事做了没有以 `docs/STATUS.md` §10 与 `docs/BOARD.md` §5 为准。

## 1. 真实规模（板子上那一行低估了）

板子原来只列了几个文件。实际测量：

```
仓库根被 git 跟踪的文件：235 个
其中全仓库零引用的：    135 个
                        ├── 99 个 .im
                        ├── 24 个 CHANGES_*.txt（合计 100K）
                        ├──  8 个 .html
                        ├──  3 个 .ps1
                        └──  1 个 .md（icon_spec.md）
另有未跟踪但被 .gitignore 忽略的：CMakeLists.txt.bak
```

所以这是个**分批**任务，不是一次 `rm`。

## 2. 分批做，别一把梭

### 第 1 批：零风险，直接删

- **24 个 `CHANGES_*.txt`**（`CHANGES_20260814*.txt`、`CHANGES_20260815_*.txt`、`CHANGES_20260926_version_0.5.0.txt`、`CHANGES_chatcmd.txt`、`CHANGES_params.txt`）。它们是开发期流水账，内容已被 `docs/` 和 `docs/archive/` 取代
- **`CMakeLists.txt.bak`**——未跟踪且已被忽略，`rm` 即可

用 `git rm` 删已跟踪的（直接 `rm` 会让工作区变脏）。

### 第 2 批：先提方案，别单方面删

99 个未引用的 `.im`、8 个 `.html`、3 个 `.ps1`、`icon_spec.md`。

这里面**有值得留的**：`canvas_demo.im`、`workbench.im`、`verse_biome_demo.im` 之类可能是可用的示例；`build_installer.ps1`、`build_asan.ps1` 之类是 Windows 构建入口。明显可丢的是 `*_probe.im`、`t_*.im`、`bench_*.im`、`*_test.im`、`ent_s1.im`…`ent_s10.im` 这类一次性调试脚本。

**做法**：把 135 个的完整清单按「删除 / 移到 `examples/` / 保留」三档分类，写进 `docs/STATUS.md` 或单独一份 `docs/HYGIENE.md`，交回来给我审。我批了再动第 2 批。

## 3. 怎么算「零引用」（这里有个大坑）

**必须扫全仓库**，不能只扫 `src/` + `docs/` + `CMakeLists.txt`：

```bash
git ls-files | grep -v / > /tmp/root.txt
while IFS= read -r f; do
  grep -rqF --exclude-dir=.git --exclude-dir=.worktrees \
       --exclude-dir=build --exclude-dir=node_modules --exclude-dir=.npm-tmp \
       "$f" . || echo "$f"
done < /tmp/root.txt
```

**踩过的坑**：只扫 `src/`/`docs/`/`CMakeLists.txt` 会把 `chat_embed.h`、`desktop_embed.h`、`home_embed.h`、`netplay_embed.h`、`wb_embed.h`、`forge_embed.h` 判成「无引用」——它们是**生成的**，但被**根目录的 `hl_bridge.c`** `#include`（`hl_bridge.c:19-21`），而 `hl_bridge.c` 又被 `CMakeLists.txt` 引用。删了直接构建失败。

**根目录的 `.c`/`.h` 也是引用来源**，别漏。

## 4. 必须留的东西（keep-set）

- `README.md`、`LICENSE`、`CMakeLists.txt`、`Makefile`、`.gitattributes`、`.gitignore`
- `icon.ico`、`installer.iss`（打包用）
- `hl_bridge.c` 与全部 `*_embed.h`（生成物，但**是构建输入**）
- `CMakeLists.txt` 里出现过的**所有**根目录 `.im`（这些是 CTest 用例，例如 `case_alias_v04.im`、`case_array_v04.im`、`lint_case_try_v04.im`、`composition_v04.im`……）和 `.c`
- 任何被 `tools/`、`examples/`、`vtest/` 引用的文件

判据很硬：**删完 `tools/gate.sh` 必须全绿**（六个阶段，含 85 个 ctest）。

## 5. 判据

1. `tools/gate.sh` 全绿
2. 根目录只剩 keep-set + 第 1 批之外**经我批准**保留的文件
3. 第 2 批的分类方案已成文并交回
4. `tools/check_links.py` 0 broken（删文件可能打断文档链接）

## 6. 已知的坑

- **`git rm` 而不是 `rm`**：已跟踪文件直接 `rm` 会让工作区变脏，而且删不干净索引
- **删文件会打断文档链接**：`README.md` / `docs/*.md` 里可能链到根目录的 `.im`。删完必须跑 `python3 tools/check_links.py`
- **`.gitattributes` 不要动**：里面有一行 `src/mod/gui_mod.c text eol=crlf` 是**刻意**的例外（见 [BOARD.md](../BOARD.md) §7）。动了它，每个新建 worktree 都会一创建就变脏
- **`git add --renormalize` 会清掉可执行位**：`tools/*.sh` 需要 `100755`。如果你用了它，跑 `git update-index --chmod=+x tools/*.sh` 补回来
- **不要碰别人的冲突域**：`docs/` 归 `docs-audit` 流，`src/verse/` 归 `upp-in-engine` 流。你只动仓库根

## 7. 交回时给我

按 [BOARD.md](../BOARD.md) §4 的五项，第 4 项「没做什么 / 已知没解决什么」不能漏。
