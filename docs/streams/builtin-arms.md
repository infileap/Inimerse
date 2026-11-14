# 作业单：`builtin-arms` — 把内建轴一格一格钉到边界集合

**状态**：进行中（内建轴已 closed，剩边界复核）
**流**：`stream/builtin-*`（每格一支，见 `docs/STATUS.md` §10.162 起的各节）
**母题**：一格一判据、判据与实现同树先红、注入各只红本组、docs 不重写历史

---

## 1. 两道路径坑（2026-11 实测，写在这里不写在 HYGIENE.md）

`HYGIENE.md` 讲的是根目录残留清理，不是作业规程；这两条是**流程**坑，属于本作业单。

### 1.1 在 `merge-win-src` 的 `main` 上直接开发

`merge-win-src` 是**合并树**，它的 `main` 只用来合分支。我在 §10.191 期间直接在它的 `main` 上提交了判据/实现/文档/pin 四笔，随后为了跟上远端执行了 `git checkout -B main origin/main` —— 分支指针被重置，那四笔提交立刻变成孤儿（`git reflog` 里仍是 `db72a636`，可用 `git branch -f stream/<cell> db72a636` 找回并正常 `merge --no-ff`）。

正确顺序：格子分支上开发（`git worktree add -b stream/<cell> .worktrees/<cell> origin/main`）→ 在 `merge-win-src` 上 `git checkout main && git merge --no-ff stream/<cell>` → 在合并树上重钉三枚 pin → gate → `git push origin HEAD:main`。合并树上永远不改格子内容。

### 1.2 `check_line_refs.py` 拒 dirty 树，而我提交了假 pin

取 citations pin 时树必须干净（工具会 `REFUSED|the worktree is dirty; a pin taken here would name a tree that no commit contains`）。我在拿到这行拒绝后仍然 `git add tools/citations.pin.json && git commit`，结果 pin 里记的 `tree` 是一个**不存在的提交**（它是上一次在别的提交上取的旧值）。gate 的 pin 阶段会因此红在「pin 指向不存在树」，而且红的位置不点名提交者。

规则：每个 pin 工具拒绝就不要提交那个文件；截断提交必须 `git reset --hard <干净提交>` 后重跑工具、再提交。三个 pin 必须**各自一个提交**，后一个工具要求前一个提交已落地且树干净。

---

## 2. 已走过的手续（供接手者照抄）

每格固定链：判据（父 sha 上先红，读数逐字进提交说明）→ 实现 → 亲跑 harness（`python3 tools/aot_mir.test.py --build <abs>/build --tool <abs>/build/aot-mir`）→ 按读端分组打注入（逐次恢复重建）→ STATUS 节 → 三枚 pin（cover/headings/citations，各自一笔）→ 合并到 `merge-win-src` → **重建 `build/aot-mir`** 后重钉三枚 pin → 全量 `tools/gate.sh` → push → `git ls-remote` 亲验 → 主树 `merge --ff-only`。

合并后不重建二进制是最常见的假红来源：cover 读到旧 `build/aot-mir` 会报 `refused=N`，读数不是树的。
