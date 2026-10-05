# 作业单：`count-size`

- **认领者**：`count-smith`
- **分支**：`stream/count-size`（base `main` = `f6b3d87`）
- **工作树**：`.worktrees/count-size`
- **提交**：`01da596`
- **裁定依据**：`docs/TYPESET_V06.md` §7.8 第 2 条（2026-10，人类选择「1、2」）；完整记录见 `docs/STATUS.md` §10.94。

## 目标

1. `count(A)` 落地为**集合基数的规范名**内建。
2. `size` 在**集合语境**下降为**已弃用别名**并迁移调用点；对**字符串／数组／字典**保留；作为 **GUI 关键字**不动。
3. 新增 CTest 钉住 `count` 的行为（含**非集合输入的决定**）与保留侧的 `size` 行为。
4. 更新 `docs/SYNTAX.md` 与 `docs/TYPESET_V06.md`。
5. 全门禁绿。

## 已裁定的语义（不要重新讨论）

- **`count` 认所有容器**（集合／数组／字典／字符串）；**非容器一律 `nil`**（人类批准的超集选择）。
- **不继承 `size` 的强转兜底**：`count(42)` = `nil`、`count(3.7)` = `nil`，而保留侧 `size(42)` = `42`、`size(3.7)` = `3` 逐字钉住。
- **三条 nil 来路分开钉**：① 类型拒绝（`count(42)`）；② `size` 尾部 `if (n < 0)`（`size(-5)` = `nil`，负整数被当成「无答案」）；③ 枚举器拒绝（`count(Z)`，无格点）。
- **集合代码与 `size` 共用**：无分量集合走 `iCount + count` 闭式，含分量集合（`1, 2, Z[7~9]`）走 `vm_set_to_array` + `vm_array_len` 枚举 ⇒ 「`count(A)` 与今天 `size(A)` 同答」按构造成立。

## 迁移的意图说明（由协调者要求补写）

29 处里 **16 处迁 `count`**、**10 处保留 `size`**、**3 处只在注释里**。三处需要写清**意图**而不是只改名：

1. **`inf_set_test.im:85`（原 `o = size(1,2,3)`）—— 必须改意图，不许改名了事。**
   它印出 `3` **只是因为内建只读最后一个实参**（等价于 `size(3)`），**纯属巧合**，不是 `{1,2,3}` 的基数；直接改名成 `count(1,2,3)` 会得到 `nil`（整数被拒）。**意图**：这一行想量的是「三个元素的集合」。**处置**：先绑 `o = 1,2,3`，再 `count(o)`，并在原处留注释写明巧合的机制与它被钉在哪条测试里。
2. **`set_op_test.im:39` 与 `projects/set_op_test.im:39`（`l = list(a)`; `say size(l)`）—— 保留 `size`。**
   **意图**：量的是 `list()` 的结果，而 **`list()` 返的是数组、不是集合**。按「`size` 对数组保留」不动。（此处本流最初归错成集合语境，复核时抓出，共 2 处。）
3. **多参陷阱单独登记**：**只有最后一个实参会被读到** —— 每个内建只读栈顶，而 `src/vm/vm.c:3805` 记的 `vm->cur_argc` **无人消费**，返回时也只弹一项（`src/vm/vm.c:3825`）。实测 `size(5,5,5)` = `5`、`size(7,8)` = `8`、`len(9)` = `9`、`count(7,8)` = `nil`、`sum(7)` = `nil`。**这是既有行为、不是本次引入的缺陷**，但 `size(1,2,3)` 那种写法在语言里**没有任何一处会报错**而人会读成「三个元素」，所以必须钉成显式行为。已钉进 `vtest/count_builtin_v06.im`。

## 数出来的数必须带 ref

「全仓 `.im` 里 `size(` 出现 N 次」这个句式是错的——**三个测量各自为真，且只在各自的 ref 上为真**：

| ref | `git ls-files '*.im'` | `size(` |
|---|---|---|
| 本流基线 `f6b3d87` | 346 | 29 处 / 17 文件 |
| `stream/be-removal`（`be-surgeon`） | 351 | 29 |
| 集成分支（`surgery-verifier` 量） | — | 30 |

⇒ 一律写「**在 ref X 上是 N**」；**落地迁移前必须在当时的 ref 上重数**。全盘 `grep -r "size("` 得到 195 一类的大数是把 5 棵 worktree 的副本与 `.verify/` 一起数了，**不是迁移代价**。

## 交叉写者（必须在合并时对齐）

`count-size` 与 `be-removal` 在 **7 个文件**上重叠：`docs/BOARD.md`、`docs/STATUS.md`、`inf_set_test.im`、`src/compiler/compiler.c`、`src/runtime/runtime.c`、`src/runtime/runtime_posix.c`、`tools/gate.sh`。

- 在 `src/runtime/runtime.c` / `src/runtime/runtime_posix.c` 里本流**只碰 `size`/`count` 相关的函数与注册行**，没有动 `builtin_range`／`posix_core_range`／`be_bound` 任何一行。
- 在 `inf_set_test.im` 里本流只改 `:85-102`（那两处 `size`），**`be` 语法的段落在 `:115` 之后，一行没碰**。
- **`docs/BOARD.md`／`docs/STATUS.md` 里那个「29」必须写成带 ref 的形态**，不要写成另一个绝对数——两个分支对同一个量各给一个数字、而**它都各自为真**，正是这棵树反复示范的形状。
- **计数冲突已由协调者处理**：本分支原生 `add_test` 137 → 138；集成基线正确值是 **143**（137+5+1）。本分支不追。

## 交付物

- `src/runtime/runtime_posix.c:63` `posix_core_count`（注册 `:1172`）
- `src/runtime/runtime.c:149` `builtin_count`（注册 `:1819`）
- `src/compiler/compiler.c:2894` `strdup("count")`（卫生）
- `vtest/count_builtin_v06.im` + `CMakeLists.txt` 的 `count_builtin_runtime`
- 16 处迁移（含 1 处改意图）
- `docs/TYPESET_V06.md` §3.2 与 §7.8、`docs/SYNTAX.md` 核心高频内建一节、`docs/STATUS.md` §10.94

## 验收判据与结果

| # | 判据 | 结果 |
|---|---|---|
| 1 | `count(A)` 在集合上给出与 `size(A)` 相同答案（含区间分量集合） | **过** —— 共用同一份代码，按构造成立 |
| 2 | 新增 CTest 钉住 `count`（含非集合输入的决定），注册进 `CMakeLists.txt` | **过** —— `count_builtin_runtime` |
| 3 | 字符串／数组／字典上的 `size` 行为不变 | **过** —— 同一 fixture 内分开钉住 |
| 4 | GUI 的 `size` 关键字不变 | **未验证（诚实缺口）** —— GUI 语境在 346 个受版控 `.im` 里出现 0 次 ⇒ CTest 零覆盖；本流未改动相关代码，但**没有验证**，「门禁全绿」不能当作证据 |
| 5 | 文档更新 | **过** —— `docs/TYPESET_V06.md` §3.2/§7.8、`docs/SYNTAX.md`、`docs/STATUS.md` §10.94 |
| 6 | 全门禁绿 | 见 `docs/STATUS.md` §10.94 与本流报告 |

## 未做（留给接手者或明确记为缺口）

1. **`src/runtime/runtime.c`（WIN32）没有编译证据** —— 它在 Linux 不参与构建，且按 `docs/AUDIT.md` §1.16 连 `-fsyntax-only` 都过不了；只做到「两份同体」。
2. **GUI `size` 的探针** —— 若要覆盖，需要先有 GUI 语境的 `.im` 样本（当前 0 处）。
3. **`Byte` 不可枚举**（`src/vm/vm.c:2182` 的 `nameIdx > 23` 闸门）**不在本流范围内**：人类裁定要修，但必须先裁「`R` 的格点是什么」，且 `src/vm/vm.c:2187` 的 `isInt = (c->nameIdx <= 3)` 是承重的（对 24 为假 ⇒ `frac=11` ⇒ `scale=10^11` 的假格点）——**删闸门会得到另一个错答案**。
