# 作业单：`gate-hermeticity`

> **状态横幅（动工前快照）。** 本文写于 `d1bef61` 之上。里面的「现状」记的是**写下那一刻的实测输出**，
> 判断某件事做没做，以 [STATUS.md](../STATUS.md) §10 与 [BOARD.md](../BOARD.md) §5 为准。

## 1. 为什么开这条流（实测，不是推测）

门禁第三/七阶段的 `links` 阶段**不封闭**：它扫的是**工作区**，不是**仓库**。

```
$ python3 tools/check_links.py
BROKEN  .verify/README.md  ->  SECURITY.md  (does not exist)
BROKEN  .verify/README.md  ->  GOVERNANCE.md  (does not exist)
BROKEN  .verify/README.md  ->  MAINTAINERS.md  (does not exist)
...
check_links: 85 markdown files, 314 links (18 external, 0 anchors, 296 local), 12 broken

$ git ls-files '*.md' | wc -l      # 78
$ find . -name '*.md' -not -path './.git/*' | wc -l   # 325
```

`tools/check_links.py:57-74` 的 `iter_markdown_files()` 用 `os.walk(REPO_ROOT)` 遍历工作区，
只靠一张 `SKIP_DIRS` / `SKIP_PREFIXES` 黑名单挡杂物（`.git`、`node_modules`、`.worktrees`、`build`、`.dshm-pr`…）。
**黑名单漏掉的东西会直接把门禁判红**：本次是另一个会话在仓库根留下的研究用暂存目录 `.verify/`，
它里面的 `README.md` 引用了 `SECURITY.md` / `GOVERNANCE.md` / `MAINTAINERS.md`（GitHub 社区健康文件，本仓库没有）。

为什么这不是「小事」：

- 门禁的职责是回答「**这个仓库**能不能合」。工作区里别人（或别的会话、别的工具）丢下的暂存文件
  与这个问题的答案**无关**，却能让答案变成「不能」。
- 更糟的是它会**训练人不信任门禁**：一旦 `links` 阶段时不时红，正确反应就不再是「去修引用」，
  而是「先看看这次红的是不是又是什么杂物」——那一刻这条门禁就废了。
- 这个仓库已经吃过同类的亏两次，见 [BOARD.md](../BOARD.md) §5 的「2026-08 修订说明」与 §6 末尾的集成记录
  （判据写下时就已成立、文档自己骗自己）。**门禁自己骗自己**是同一族缺陷里最贵的一种。

## 2. 现状事实（实测）

- `tools/check_links.py`（168 行）导出/实现：`iter_markdown_files()`（`:57`，`os.walk` + 黑名单）、
  `targets_in(text)`（`:77`，先 `strip_code_fences` 再用 `INLINE_CODE_RE` 抹掉行内代码，然后收
  `INLINE_RE` 与 `ANGLE_RE`）、`classify(target)`（`:86`，返回 `external`/`anchor`/`path`）、`main()`（`:106`）。
  `main()` 对每个 `path` 类目标做 `os.path.normpath(os.path.join(REPO_ROOT, dirname(rel), path))`
  再 `os.path.exists`，不存在即 `broken`；有 `broken` 就 `return 1`。支持 `-v` 与 `--json`。
- 黑名单现状：`SKIP_DIRS = {".git","node_modules",".worktrees","target","userdata","__pycache__",".venv","venv",".pnpm-store"}`、
  `SKIP_PREFIXES = ("build", ".dshm-pr", ".npm")`、`SKIP_FILES = {"node_modules"}`。
  **`.verify/` 不在其中**，所以被扫了。
- `tools/check_doc_paths.py`（门禁第七阶段）**没有**这个问题：它用一份固定的文件清单（11 份），
  不遍历工作区（已实测：其输出恒为 `11 markdown files, N backtick refs`）。
- 门禁调用点：`tools/gate.sh` 的 `stage_links`（**该文件正被另一条流 `httpfix` 持有，你不能碰**）。
- 工作区当前还有 `universe/_cache/`（未跟踪，由引擎 `verse_pack` 的 `cache_put` 写出的资产缓存，
  见 `src/mod/verse_dist_mod.c:1172`；`universe/` 有 4 个**已跟踪**条目，`_cache/` 不在其中）。
  `git check-ignore universe/_cache .verify` 两者都**不**被忽略（退出码 1）。

## 3. 要做的改动

1. **把 `iter_markdown_files()` 从「遍历工作区」改成「枚举 git 已跟踪文件」**，即
   `git ls-files -z -- '*.md' '*.markdown'`（在 `REPO_ROOT` 下执行）。
   理由：门禁要回答的是「**仓库**里的引用有没有断」，那么被检查的集合就应当**就是仓库的内容**。
   这比继续往黑名单里加目录**根本**得多——黑名单永远漏，而 `git ls-files` 按定义不遗漏也不多收。
2. **不要用「多加几条 `SKIP_*`」来糊过去。** 如果你发现某个已跟踪文件让检查器变红，那是**真的断了**，
   要去修那个引用，或者（若属归档件）按 [STATUS.md](../STATUS.md) §1 硬规则 6 的口径处理——**不要把它跳过**。
3. **git 不可用时要响亮失败**，不要静默回退到 `os.walk`。回退会把本条流刚修掉的缺陷原样带回来，
   而且更难发现（只有 git 不在时才复现）。宁可 `sys.exit` 并打印一条清楚的错误。
4. `.gitignore` 补两行运行时/暂存产物：`universe/_cache/` 与 `.verify/`。
   （`universe/_cache/` 是引擎的资产缓存；`.verify/` 是会话级研究暂存目录。）
   注意：**这两行是卫生措施，不是本缺陷的修复**——修复是第 1 条。别把两者搞混，
   也别因为加了这两行就以为第 1 条可以不做。

## 4. 判据（每条都要当场执行、贴输出）

1. **文件数守恒**：`python3 tools/check_links.py --json` 报的 `files` 必须等于
   `git ls-files '*.md' '*.markdown' | wc -l`。把两个数字都贴出来。
2. **当前工作区转绿**：改完后 `python3 tools/check_links.py` 报 `0 broken`，
   且**不再出现** `.verify/README.md` 那三行。贴改前与改后的输出。
3. **反向验证（这条最重要）**：证明检查器**仍然能红**。做法二选一，把命令与输出贴出来：
   - 在一个**已跟踪**的 `.md` 里临时插入一个指向不存在文件的相对链接，跑检查器，必须 `exit 1` 并点名它；
     然后还原。（推荐用 `git stash` 或直接改回并 `git diff --exit-code` 确认干净。）
   - 或写一个最小用例直接调用 `iter_markdown_files()` / `main()` 的等价逻辑，断言它能看见断链。
   **没有这一条，`0 broken` 可能只意味着「什么都没扫」。**
4. **杂物免疫**：在仓库根临时造一个 `scratch-xyz/README.md`，里面写一个指向 `NOPE.md` 的链接，
   确认检查器**看不见它**（因为未跟踪）；然后删掉这个目录。贴输出。
5. **门禁**：`bash tools/gate.sh --fast --only links` 通过；`bash tools/gate.sh --fast --only doc-paths` 通过。
   （`--only links` 需要 `tools/gate.sh` 支持单阶段，它已支持。）
6. 交付说明里逐条列出：改了什么、**为什么这样改比加黑名单更根本**、以及第 3 条反向验证的输出。

## 5. 写域（别人别碰，你也别出界）

**你可以改：**
- `tools/check_links.py`
- `.gitignore`
- `tools/check_doc_paths.py`（**仅当**你能证明它也有同类不封闭问题时；它目前实测是封闭的，大概率不用动）
- 一份 `docs/` 记录（建议在 `docs/STATUS.md` 新增你自己的小节；**不要动别人的既有行**）

**你绝对不能碰：**
- `tools/gate.sh`、`CMakeLists.txt`、`tools/node_suites/run_all.js` —— **另一条流 `httpfix` 正持有它们**
- `tools/crp_ws_client*`、`tools/upp_session*` —— **另一条流 `wscoverage` 正持有它们**
- `src/**` —— 本条流不碰引擎
- `docs/BOARD.md`、`docs/STATUS.md` 的既有行 —— **板子与状态由协调者统一改**
- 不要 `git push`、不要 `git merge`、不要动 `main`

## 6. 交付物

一个 commit（在 `stream/gate-hermeticity` 分支上）＋ 一份交付说明，含：改动的文件与理由、
§4 六条判据各自的命令与输出（**第 3 条反向验证必须有**）、以及你**没有**做的事。
