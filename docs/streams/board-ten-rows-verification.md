# 作业单：十条在途行逐行实核（`docs/BOARD.md` §5）

- **认领者**：`ivory-zephyr`（协调会话，人工授权逐行验收）
- **分支**：`stream/board-status-refresh`（base `main`）
- **工作树**：`.worktrees/board-status-refresh`
- **测量 ref**：**`069c316`**（下文所有行号一律读于该树；带 `[obs: 069c316]` 的引用是**记录**，不是指向 HEAD 的指针）
- **共同凭据**：`NODE_PATH=$HOME/.local/inimerse-jsdom/node_modules bash tools/gate.sh --jobs 6` → `gate: OK — every stage passed (16/16 stages ran).`、exit 0、16 个 `✔`、0 个 `✘`；同一棵树 `ctest --test-dir build -j8` → `100% tests passed, 0 tests failed out of 157`

## 0. 这次核的是「判据是否成立」，不是「交付物在不在」

十条行**没有一条缺交付物**：九个 `stream/<slug>` 分支（除 `decfy-design`）全部已是 `main` 的祖先
（`git merge-base --is-ancestor` 逐条 rc=0、`git rev-list --count main..stream/<slug>` 逐条为 0）。
缺的只有两件事：**状态列没刷新**、**读数没重取**。

## 1. `be-removal` —— 5/5

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 类型集合回归 | ✅ | `./build/inimerse --no-mods vtest/type_collection_v04.im` → 逐字 `typecoll x=42` |
| ② 越界赋值被拒 | ✅ | `type Byte = [0~255]` + `x = 300` → stderr `[exception] uncaught: type_mismatch`、rc=1、`must-not-reach` 不打印 |
| ③ 五条 CTest 在案 | ✅ | `be_removed_decl_runtime`（在 `CMakeLists.txt` 里按名字定位该 add_test [obs: 069c316]）、`be_removed_assign_runtime`、`be_removed_bare_runtime`、`bind_init_overflow_runtime`（同文件，按名字定位 [obs: 069c316]）、`gc_bound_root`；`ctest -R` 五条 → `100% tests passed, 0 tests failed out of 5` |
| ④ GC 标记根 | ✅ | `src/vm/vm.c:2845-2847` [obs: 069c316] `for (int i = 0; i < vm->globalCount; i++) { if (vm->global_bound[i] > 0) {`；`gc_bound_root` Passed |
| ⑤ 旧语法遗留仅刻意项 | ✅（有边界） | `vtest/be_removed_decl_v04.im:15`、`vtest/range_metadata_probe_v06.im:11` 是刻意钉报错 [obs: 069c316] |

**边界一**：另有 **30 个 `svprobe/*.im` 仍写旧 `be` 语法**（`svprobe/base_swallow.im:2`、 [obs: 069c316]
`svprobe/ln_decl.im:3`、`svprobe/old/big_globals_test.im:168` 等）。`svprobe/` 被 git 跟踪但 [obs: 069c316]
`CMakeLists.txt` 零引用。实跑 `./build/inimerse --no-mods svprobe/ln_decl.im` → 引擎按设计报错
（rc=0）。判据⑤的「10 条迁移」只覆盖判据正则命中的那批，`svprobe/` 是**范围外**，不是漏。

**边界二**：不带 `type Byte` 声明、只写 `x: Byte = 42` 时**不报错**（`Byte` 当未定义名，赋值成功、
rc=0）。判据②成立的前提是**类型真的声明过**。

## 2. `win-source-attribution` —— 4/6 通过，1 条口径需重述、1 条为空绿

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 四项回报齐备 | ✅ | `docs/streams/win-source-attribution.md:11-13` 有 `pwd`、`git rev-parse HEAD`、读法、测量 ref `d27413a` [obs: 069c316] |
| ②③ 机制句可核 | ✅ | `src/main.c:1067` [obs: 069c316] `register_core_modules(&vm);`（无条件）、`:1068` `if (load_mods) register_world_modules(&vm);`、`:1084` `if (load_mods) mod_load_all(&vm, "mods");`、`:1000` `load_mods = 0;`、`:875` `int load_mods = 1;` |
| ④ 只改本文件 | ✅ | `git diff --name-only 9969e5b 0077c90` → 仅 `docs/streams/win-source-attribution.md` |
| ⑤ 三阶段读数 | ⚠️ 已过期 | 文档记 links 102/586/0、doc-paths 21/932/0、text-integrity 825/0；今天 181/618/0、26/1146/0、1052/0 |
| ⑥ `doc-paths` 对本文空绿 | ✅（空绿） | `tools/check_doc_paths.py:27` [obs: 069c316] 逐字 `skipped  docs/streams/` |

**⑤ 为什么变**：分母暴涨主因是 `website/scratch/` 有 **61 个被跟踪的 `.md`**（由 `eef6dba` 加入）。
交付 ref `077997a` 上 `.md` 总数 **92**、`website/scratch` 计 **0**；今天 **171**，其中 61 来自该目录。
⇒ 文档当时的分母在它自己那一刻仍然成立，**不算不达标，算「读数已过期」**——这正好是
`docs/BOARD.md:3` 那条「一个数是写下它那一刻的读数」的实例。 [obs: 069c316]

## 3. `typeset-v06` —— 5/5

- ① `check_doc_paths` 26 files / 1146 refs / 0 broken；`check_links` 181 files / 618 links / 0 broken ✅
- ② 四处口径一致：`docs/archive/ROADMAP.md:9` 逐字 `集合化类型系统~~延期至 v3.1~~ **已于 2026-10 提前为 v0.6 目标**`； [obs: 069c316]
  `docs/archive/ROADMAP_3.1.md:3` 原文保留、`:5` 加抬头「本文件的时间表已被取代（2026-10）」＋「规格内容一条未变」； [obs: 069c316]
  `docs/STATUS.md:491` §4；`docs/STATUS.md:564` 用删除线＋「2026-10 由人裁定提前为 v0.6 目标」。 [obs: 069c316]
  `ROADMAP_3.1` 的改动只有 `4 insertions(+)`（`f1d2286`），主体零改写 ✅
- ③ `docs/TYPESET_V06.md` 抽 20 条 `文件:号` 全在界内；反例成立：`src/types/` 存在且 13 个文件 ✅
- ④ `docs/TYPESET_V06.md:368` `## 7. 逐条状态：已裁定与未决`、`:390` `### 7.8 仍未决` ✅ [obs: 069c316]
- ⑤ 各条裁定在 `docs/TYPESET_V06.md` §3.1–3.6/§8 与 `docs/STATUS.md` §4 双处出现 ✅

## 4. `infiverse-handoff` —— 判据② **不达标**

- ① `links` / `doc-paths` 双绿 ✅
- ② 「每个数字都能指到机器/目录/ref/时刻」—— **不达标**，三条实证：
  - `docs/HANDOFF_INFIVERSE.md` 正文**没有自报测量 ref**（对照第 2 行写明 `pwd`/`HEAD`/读法）。
  - **行号已漂移**：文档写 `src/compiler/compiler.c:2867` 的 GBK `"\xBF\xD5"`，今天在 [obs: fb92b57]
    `src/compiler/compiler.c:2900` [obs: 069c316]；写 `src/vm/vm.c:1479` 的 UTF-8 `"\xE7\xA9\xBA"`，今天在
    `src/vm/vm.c:1552` [obs: 069c316]。
  - **§4.4 那条已不成立**：`docs/SYNTAX.md:39` 那句「没有块注释」已改写为「加一种嵌套块注释」。 [obs: 069c316]
- 判读：文档第 9 行自己立了「任何『实测数字』都必须说清哪台机器、哪个目录、哪个 ref」，
  却**没有在正文落实自己这条规矩** ⇒ 记为「规矩写对了、执行没到位」。这一条**不能推进状态**。

## 5. `decfy-design` —— 4/4 通过，但状态**按设计永不推进**

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 六节齐全 | ✅ | `docs/DECFY_DESIGN.md` §2 层次划分 / §3 IR 收敛 / §4 引导 / §5 FFI-ABI / §6 迁移顺序+可证伪判据 / §7 风险与不做 |
| ② 事实性主张带 `文件:号` | ✅ | 1157 行里 `文件:号` 形态 **169 处** |
| ③ 五类搬不动的 C 逐条给理由 | ✅ | `docs/DECFY_DESIGN.md:45` 起逐条 [obs: 069c316] |
| ④ 「待批准变更」只列不改 | ✅ | `## 9. 待批准变更（本文档只写不改）` 明言「本次未修改任何引擎源码」 |

**两处必须记的**：
- 行里写的 `stream/decfy-design` **不存在**；仓库有 17 条 `stream/decfy-*` 分支，没有一条叫这个。
  交付物是 `docs/DECFY_DESIGN.md`（`022d002` 起、`ec3008f` 加 §0.5）。
- 判据②的括号「本文档 `[实测]` 栏为『无』」**按开行时点才成立**：`022d002` 那版 249 行、`[实测]` 2 行、无 §0.5；
  今天 `[实测]` **18 行**，§0.5 由 `ec3008f` 补写并明言「原文（§1–§10）保留原样……以现状表为准」。
  那是文档**后来长出了实测**，不是当初造假。
- **状态不推进**：该行判据栏自己写着「本节状态永远是『进行中』，设计文档不等于交付实现」。
  本轮核验时曾把它改成「已完成」，随即按这条自己写下的规则撤回。

## 6. `docs-comment-and-h4` —— 4/4

- ① 文档改动最小门禁覆盖 ✅（全量 16/16）
- ② 两条实测性质成立：`x = 3 #[ inline ] say 88` → 只打 `3`（同行余下代码被丢，`88` 不出）；
  嵌套 `say 1 #[ a #[ b ] c ]` → 打 `1` 再打 `2`（`depth` 计数正确）。
  `src/lexer/lexer.c:74-96` 在交付树 `4450320` 上；今天漂到 `:78-105` [obs: 069c316]（+4/+9，非同一偏移）✅
- ③ H4 引用的路径全部存在（`tools/migrate_report.py`、`src/runtime/runtime.c`、`src/runtime/runtime_posix.c`）✅
- ④ 数字带 ref/命令 ✅
- 交付提交 `4450320`（2026-10-05）只改 `docs/BOARD.md` +1 与 `docs/SYNTAX.md` +44/−2，已合入 `main`。

## 7. `sayout-ignore` —— 4/4

- ① `git check-ignore -v vtest/sayout.txt` → 逐字 `.gitignore:122:/vtest/sayout.txt	vtest/sayout.txt`，rc=0 ✅
- ② 交付提交 `8d28c60`（10-06 10:46）`--name-only` 只有 `.gitignore`、`1 file changed, 23 insertions(+)` ✅
- ③ 六阶段 links / ignored-credentials / orphan-fixtures / orphan-targets / doc-paths / text-integrity
  逐个 `--only` 全 PASS（**诚实边界**：这是六次单跑的并集，未选中的行印 `SKIP --only <name>`，
  不等于十六阶段全绿；十六阶段全绿由 ④ 那次全量运行单独给出）✅
- ④ 全门禁 16/16 ✅
- 另：`vtest/sayout.txt` 残渣已不存在。
- **更正一条**：行里 `@ 8d28c60` 是**交付提交**，`458fa29` 是**开行的看板提交**（只改一行 BOARD），二者不矛盾。

## 8. `lang-migration` —— 6/7，1 条记录缺失 + 1 个门禁读数陷阱

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 逐 stage 记录 | ⚠️ 记录缺失 | 行里只写「逐 stage 与 `3abe35f` 完全相同」，**没有逐 stage 明细落盘**；`git grep -c '3abe35f' -- docs/` = 1（就是这一行自己）⇒ 不可独立复核 |
| ② 内建族级重叠 | ✅ | `count` / `range` 都在注册表：`src/runtime/runtime.c:1916` / `:1943` [obs: 069c316]；`src/runtime/runtime_posix.c:1298` / `:1307`；`compiler_new` 在 `src/compiler/compiler.c:2885`、`STMT_WITH` 在 `:1542` [obs: 069c316] |
| ③ 三处冲突解法 | ✅ | 行里点名 `tools/gate.sh` 的冲突行、`docs/STATUS.md` 的两处（`44` 与 `51`），并写了「同一行的两种真值、不取任何一侧」 [obs: 069c316] |
| ④ 两侧同源派生 | ✅ | 今天三个数一致：`grep -c 'add_test(' CMakeLists.txt` = **157** == `tools/gate.sh` 里 `EXP_CTEST="${EXP_CTEST:-157}"` 那一行 [obs: 069c316] == `ctest -N` 的 `Total Tests: 157` |
| ⑤ 最终门禁逐 stage 一致 | ⚠️ 有保留 | 16/16 全过，但 `cargo` **不在本机**，`oauth_loop` 阶段逐字印 `cargo not found -- skipping the oauth_loop crate` 却仍记 `✔` |
| ⑥ 五条支线并集 | ✅ | `git merge-base --is-ancestor` 逐条 rc=0：`stream/be-removal`@`05c3a3e`、`stream/count-size`@`60d0b36`、`stream/range-semantics`@`30ee4d0`、`stream/surgery-verify`@`c1c656f`、`stream/docs-comment-and-h4`@`4450320` |
| ⑦ 三处缺口带着走 | ✅ | 行里已登记 |

**⑧ 本条要写进口径**：「门禁 16/16」**不等于**「十六件事都真验了」。`oauth_loop` 因本机无 `cargo` 而跳过、
仍记 `✔`。下一个会话读到 `16/16` 时，必须知道这一格是**跳过而非通过**。

## 9. `xrange-t2` —— 7/7（判据①是现建旧树实测）

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 两条路对同一个值同答 | ✅ | 见下 |
| ② fixture 与正则 | ✅ | `vtest/xrange_t2_v06.im`(3560B)、`vtest/member_on_nonname_v06.im`(1113B)；`CMakeLists.txt` 里 `xrange_t2_runtime` 与 `member_on_nonname_runtime` 两条注册 [obs: 069c316]；实跑 fixture 逐字 `xrange-ok bad=0 x.range=set(Z interval) arr0.range=set(Z interval) id.range=set(Z interval) … 300in.range=true` |
| ③ 两条 CTest | ✅ | `xrange_t2_runtime`、`member_on_nonname_runtime` 均 Passed |
| ④ 报错文本 | ✅ | `src/compiler/compiler.c:1237-1241` [obs: 069c316] `Error: '.%s' on something that is not a name is not implemented (write '?.%s' for a nil-safe index)`，注释说明 `It used to \`return -1\`, and every caller took that -1 for a register number` |
| ⑤ 全门禁 | ✅（换口径） | 行里写「12/12 —— 未跑」；今天是 **16 阶段**且全过。`12/12` 是当时的阶段数，**非同一量** |
| ⑥ 计数两侧同源 | ✅ | 157 == `EXP_CTEST` |
| ⑦ `return -1` 已修 | ✅ | 同 ④ |

**①的实测方法**：开临时工作树于 `58f1a44`，`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` 构建成功
（`inimerse` 885984 字节）。旧树**没有** `vtest/xrange_t2_v06.im`，直接拷新 fixture 过去会 SIGSEGV
（rc=139，旧树不支持 `.range`/`.declared`），故用最小探针（`type Byte = [0~255]` / `x: Byte = 42` /
`arr = [x]` / `func id(a) { return a }` / 三行 `say str(...)`）：

- **旧树引擎**：`set(R interval) | set(Z interval) | set(Z interval)`，`300 in` → `false | true`
- **新树引擎**：`set(Z interval) | set(Z interval) | set(Z interval)`，`300 in` → `true | true`

⇒ 分歧真实存在、新树三路同答。临时工作树已清理，`git worktree list` 回到 55 条。

## 10. `decision-points-delegation` —— 7/7

- 判据「本流只动 `docs/`」✅ 三笔提交逐条验：`0c5a8af`（仅 `docs/streams/v06-decision-points-delegation.md`）、
  `70b9c0d`（`docs/BOARD.md` + `docs/streams/v06-decision-points.md`）、`711a339`（`docs/BOARD.md` +
  delegation 文件给 35 处引用补树名）—— `src/**`、`selfhost/**`、`tools/**` 零改动
- 判据「指针行不含 `文件:号`」✅ `docs/streams/v06-decision-points.md:171` 无任何 `文件:号` 形态 [obs: 069c316]
- 判据「18 点拆分可核」✅ delegation 文件 `:80-84` 逐字 `18 = 独立 12 ｜ 混合 3 ｜ 纯委托 3`
  （`独立 12`=A1 A3 A5 A7 M1 M3 M4 T1 T2 T3 T4 T5、`混合 3`=A2 A6 M5、`纯委托 3`=A4 M2 M6）；
  更严口径（含产生答案的规则）= **9** 个
- 判据「漂移可复现」✅ `a=$(git show f6e22f5:src/vm/vm.c | sed -n "${o}p")` /
  `b=$(git show 9f3761e:src/vm/vm.c | sed -n "${n}p")`：`347→394`、`3508→3555`、`3515→3562`、`4161→4208`
  四对逐字相同（`int vm_truthy(const Value *v) {`、`L_AND: {`、`L_OR: {`、`L_MOD: {`），偏移恰 **+47**。
  对照 `src/parser/parser.c:254` / `:420`、`src/compiler/compiler.c:254` / `:420` 偏移 **0** [obs: 069c316]
  ⇒「一个文件整体挪了、别的都没动」成立

## 汇总

| 行 | 位置 | 结论 | 本轮状态 |
|---|---|---|---|
| `be-removal` | `docs/BOARD.md:99` | 5/5 | 待验收 → 已完成 [obs: 069c316] |
| `win-source-attribution` | `docs/BOARD.md:285` | 4/6；⑤读数过期 | **保持待验收** [obs: 069c316] |
| `typeset-v06` | `docs/BOARD.md:100` | 5/5 | 待验收 → 已完成 [obs: 069c316] |
| `infiverse-handoff` | `docs/BOARD.md:101` | 判据②**不达标** | **保持待验收** [obs: 069c316] |
| `decfy-design` | `docs/BOARD.md:145` | 4/4，但按设计永不推进 | **保持进行中** [obs: 069c316] |
| `docs-comment-and-h4` | `docs/BOARD.md:282` | 4/4 | 进行中 → 已完成 [obs: 069c316] |
| `sayout-ignore` | `docs/BOARD.md:284` | 4/4 | 待验收 → 已完成 [obs: 069c316] |
| `lang-migration` | `docs/BOARD.md:98` | 6/7；①明细未落盘 | **保持待验收** [obs: 069c316] |
| `xrange-t2` | `docs/BOARD.md:287` | 7/7（①为现建旧树实测） | 待验收 → 已完成 [obs: 069c316] |
| `decision-points-delegation` | `docs/BOARD.md:288` | 7/7 | 进行中 → 已完成 [obs: 069c316] |

**两条横向结论**：

1. **十条没有一条缺交付物** —— 缺的全是「状态列没刷新」与「读数没重取」。待验收＝只差刷状态；
   进行中＝设计上永远进行中（`decfy-design`）或已交付待认（`docs-comment-and-h4`、
   `decision-points-delegation`）。
2. **门禁绿 ≠ 十六件都验了**：`oauth_loop` 因本机无 `cargo` 而 SKIP、仍记 `✔`。这条必须写进口径。

## 另见：`docs/STATUS.md` §2 的基线读数

本轮另行发现 §2「当前基线」不只是测试数陈旧（148 → 157），整张表在 `069c316` 上重测后都变了。
按 `docs/STATUS.md` 自己的规矩（数字是写下那一刻的读数，**不覆盖旧值**），本轮只把**测试计数**
一处补成「旧值 + 新 ref 读数」，其余各行**只报告不修改**：

| 项目 | 表内旧值 | `069c316` 重测 |
|---|---|---|
| `add_test(` 注册数 / `EXP_CTEST` | 148 / 148 | **157 / 157** |
| `src/` `.c` 文件数 | 114 | **115** |
| `src/` `.h` 文件数 | 51 | 51 |
| `src/` `.c`+`.h` 行数 | 53,547 | **54,391** |
| `src/` `.c` 单独行数 | 50,706 | **51,525** |
| `vm_register_builtin*` 调用 | 531 | **612** |
| `selfhost/` `.im` 行数 | 2,316 | **2,337** |
| 仓库 `.im` 总数 / 根目录 | 316 / 148 | **398 / 149** |
| `tools/` 条目 | 98 | **140** |
| 编译告警 | 35 warnings / 0 error | **0 warnings / 0 error**（`tools/gate.sh` 的 `--clean-first` tally 逐字 `warnings: 0`） |

**「35 warnings」这一格必须单独说明**：`CMakeLists.txt` 与 `tools/gate.sh` **全文没有 `-Wall`**
（`git log -S'-Wall' -- CMakeLists.txt` 返回空），所以 `stage_build` 的 tally 量的是**默认编译选项下**
的告警，而 35 那个数出自 §2.5 修复时的**另一套读数口径**。两格今天都是 0，但**它们不是同一个量**——
这正是本仓反复记的那类「引的证据与它证的命题不是同一个量」。
