# 流简报：`repo-hygiene`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/repo-hygiene`，冲突域**仓库根**（除 `README.md` / `LICENSE` / `CMakeLists.txt`）。

> **状态：第 1 批已交付（`382ab67`）；第 2 批已重新认领，范围为「只做桶 B」（见 §8）。**
> 第 1 批（删 24 个 `CHANGES_*.txt` + `CMakeLists.txt.bak`）已合入 `main`。
> [HYGIENE.md](../HYGIENE.md) 的 146 个候选分桶 A 删 118 / B 迁出根 22 / C 留根 6，
> **本批只执行桶 B（无损可逆）；桶 A 与 C 不动** —— 桶 A 的「已被 `vtest/` 覆盖」是
> 按特性名与文件头**自述**推断，未逐条对照 28 个 `*_test.im` 的断言，那是最大的判断风险。
> 第 1 批的交付内容见 [STATUS.md §10.3](../STATUS.md#103-仓库根清理repo-hygiene)。
> **本文件 §1–§7 是第 1 批动工前的作业单**（其中的 135、85、六阶段等数字均已过期）；
> 判断某件事做了没有以 `docs/BOARD.md` §5、`docs/STATUS.md` §10 与 **下面 §8** 为准。

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

## 8. 本批范围：只做桶 B（2026-08 重新认领）

**唯一目标：把桶 B 的 22 个文件从仓库根移到 `examples/` 下，一个文件都不删。**

- 清单以 [HYGIENE.md](../HYGIENE.md) 为准（分桶表）。**先自己重新生成一遍并核对计数**：
  桶 B 应为 22 个，迁移目标 `examples/scripts` 5 / `examples/regressions` 7 /
  `examples/legacy-ui` 8 / `examples/bench` 2（若实测与这三个数对不上，**停下来在交接说明里写清楚差异**，
  不要自己改口径）。
- **同时迁移「随 B 走的二阶孤儿」**：HYGIENE.md §6.1 列了 30 个二阶孤儿，
  其中 8 个是随桶 B 一起迁的。逐个确认它们只被同一个桶 B 里的文件引用，
  **若某个二阶孤儿同时被 `CMakeLists.txt`、`vtest/` 或桶 C 的文件引用，就不迁**。
- 桶 C 六个文件（`hl_bridge.c`、`build_asan.ps1`、`build_installer.ps1`、
  `ai_build.ps1`、`imai.ps1`、`icon_spec.md`）**留在根**，不动。
- 桶 A 的 118 个**一个都别删**。

### 硬要求

1. **用 `git mv`**，不要 `mv` + `git add` —— 保留重命名历史，`git status` 也要干净。
2. **修所有断掉的引用**：根目录的 `.im` 之间、`.ps1`/`.bat` 之间的相对路径，
   以及 `README.md` / `docs/*.md` 里指向这些文件的链接。
   迁完必须 `python3 tools/check_links.py` 与 `python3 tools/check_doc_paths.py` 都是 0 broken。
3. **逐文件核对无内容丢失**：迁移前后对每个文件做 `git hash-object` 比对（内容必须逐字节相同），
   把这张表贴进交接说明。**这是本批唯一真正的验收点** —— 它证明「迁移」不是「删除」。
4. **门禁全绿**：`tools/gate.sh`（现在是**七阶段**：build / ctest / economy / node / plugin / links / doc-paths）。
   注意 **ctest 期望是 89/89**，不是 85（§5 那条已过期）。串行跑，不要同时开两个 gate。
   构建用 `-j4`，**不要用 `nproc`**（12 核，多路并行会互抢导致端口竞争假失败）。
5. `docs/HYGIENE.md` 里记录本批**已执行**：迁走的 22(+n) 个文件、去处、核对表结论；
   桶 A 仍标注**未执行**。

### 不要做

- 不删任何文件（包括桶 A）。本批**只搬不删**。
- 不改 `.gitattributes`（里面 `src/mod/gui_mod.c text eol=crlf` 是刻意例外）。
- 不用 `git add --renormalize`（会清掉 `tools/*.sh` 的 `100755` 可执行位）。
- 不碰别人的冲突域：`src/verse/`、`src/common/`、`src/mod/verse_dist_mod.c` 归 `crp-in-engine`。
- **只 commit，不 push；不许合 `main`。**
