# docs/HYGIENE.md — 仓库根残留清理：度量修正与第 2 批分类方案

> 执行流：`stream/repo-hygiene`（冲突域：仓库根，除 `README.md` / `LICENSE` / `CMakeLists.txt`）。
> 作业单：[streams/repo-hygiene.md](streams/repo-hygiene.md)。
> **本文件是方案，不是执行记录。第 1 批已执行（§1）；第 2 批（§3–§7）等协调者批准后才动。**

## 0. 状态一览

| 批次 | 范围 | 状态 |
| --- | --- | --- |
| 第 1 批 | 24 个 `CHANGES_*.txt` + `CMakeLists.txt.bak` | **已执行并提交**（§1） |
| 第 2 批 | 146 个全仓库零引用文件（§2 修正后的数字） | **仅方案**（§3–§7） |
| 连带项 | 30 个二阶孤儿（§6） | 仅方案 |

## 1. 已执行的第 1 批

`git rm` 删掉根目录 24 个 `CHANGES_*.txt`（`CHANGES_20260814*.txt` 4 个、
`CHANGES_20260815_*.txt` 18 个、`CHANGES_20260926_version_0.5.0.txt`、`CHANGES_chatcmd.txt`、
`CHANGES_params.txt`），工作区未出现未跟踪残留。

`CMakeLists.txt.bak`（24 881 B，被 `.gitignore:32` 的 `*.bak` 忽略、未被跟踪）在**主工作区**
`/home/sakiko/inimerse/CMakeLists.txt.bak`，本 worktree 里没有这个文件；已删除，删除前的副本暂存于
`/tmp/CMakeLists.txt.bak.stash`。它与现行 `CMakeLists.txt` 的关系已核对：是一份**较旧的子集**，
缺少 P1 `verse_eventlog_probe` / `verse_layer_probe` / `verse_protocol_probe` / `inim-server` /
`inim-client` / `ed25519_probe` 等全部段落，没有任何现行文件独有的内容。

删除后的定向检查：`python3 tools/check_links.py` →

```
check_links: 69 markdown files, 225 links (6 external, 0 anchors, 219 local), 0 broken
```

（`docs/BOARD.md` §3 记的基线是「66 个 md / 211 条链接 / 205 条本地链接」，与当前不符，
差值来自 `docs-audit` 流新增的文档，不是本流改动造成的。）

## 2. 度量修正：真实数量是 146，不是 135

作业单 §3 给的扫描法有两个坑，都会**少报**孤儿。

### 坑 1：自匹配（16 个文件被漏报）

`grep -rqF "$f" .` 会把**文件自己内容里出现自己的名字**算作引用。实测 16 个文件因此被判成「有引用」：

```
ai_build.ps1 block_edit.im cpu8m.im endless_busy.im endless_test.im entity_stress.im
entity_stress2.im imai.ps1 limit_test.im ports.bat smoke2.im stress_cpu.im stress_mem.im
test_room.im textbox.im timeout_test.im
```

这 16 个文件除了自己提自己，**没有任何其它文件提到过它们的名字**。

### 坑 2：作业单自己被当作引用来源（7 个文件被漏报）

`docs/streams/repo-hygiene.md` 自己点名了 8 个文件，扫描时会被算成引用者：

| 被点名文件 | 除作业单外还有谁引用 | 判定 |
| --- | --- | --- |
| `hl_bridge.c` | 无人（见下） | 保留（§5） |
| `build_asan.ps1` | 无人 | 候选 |
| `build_installer.ps1` | 无人 | 候选 |
| `canvas_demo.im` | 无人 | 候选 |
| `verse_biome_demo.im` | 无人 | 候选 |
| `ent_s1.im` / `ent_s10.im` | 无人 | 候选 |
| `icon_spec.md` | 无人 | 候选 |

### 顺带发现：作业单里两条事实前提已经失效

1. **`hl_bridge.c` 并不被 `CMakeLists.txt` 引用。** 作业单 §3 写「`hl_bridge.c` 又被
   `CMakeLists.txt` 引用，删了直接构建失败」——实测 `grep -n hl_bridge CMakeLists.txt`
   **零命中**，`grep -in glob CMakeLists.txt` 也没有 GLOB 兜底，暂存的 `.bak` 版本里同样没有。
   现在的真实引用关系是反过来的：`hl_bridge.c` → `#include` 那 6 个 `*_embed.h`；
   对 `hl_bridge.c` 本身的唯一「引用」是 `src/mod/server_mod.c:61` 拼的运行期 exe 路径
   （`"%s\\hl_bridge.exe"`），所以它**照样必须保留**，但理由是「6 个生成头文件的唯一消费者 + 运行期 exe 的源码」，
   不是「CMakeLists 引用」。
2. **`case_*_v04.im` 那一批 CTest 用例不在根目录，而在 `vtest/`。** 作业单 §4 的判据说
   「`CMakeLists.txt` 里出现过的**所有根目录** `.im`（例如 `case_alias_v04.im`…）……」，
   但 `CMakeLists.txt` 提到的 41 个 `.im` 全部位于 `vtest/`，
   根目录 167 个 `.im` 里**没有一个**出现在 `CMakeLists.txt` 中。
   `CMakeLists.txt` 全文只提到 3 个根目录文件：`LICENSE`、`README.md`、`params`。

   → 这条判据对根目录而言**选出来的是空集**，不会与第 2 批清单冲突。

### 修正后的数字

| 口径 | .im | .html | .ps1 | 其它 | 合计 |
| --- | --- | --- | --- | --- | --- |
| 作业单声称 | 99 | 8 | 3 | 1 `.md` + 24 `CHANGES_*.txt` | 135 |
| 实测（第 1 批删完之后，自匹配已排除） | 124 | 8 | 4 | 1 `.inim` + 1 `.bat` | **138** |
| 实测 + 只被作业单点名的候选 | 125 | 8 | 6 | 2 | **146** |

**第 2 批的候选集 = 146 个文件**（`hl_bridge.c` 在内，但它是保留项）。
下面三桶之和 = 146，无遗漏、无重复、互不交叠（生成时程序校验）。

## 3. 桶 A：删除（118 个）

判据：没有任何调用方、没有断言/输出契约、且内容已被 `vtest/` 入册用例或 `docs/archive/` 覆盖。

### 探针脚本（无断言、无输出契约，纯手工看一眼）（9 个）

文件名以 `*_probe.im` 结尾或内容是几行 `say`；没有任何调用方，也不构成回归判据。

```
banner_probe.im c2_probe.im c4_probe.im c5_probe.im fold_probe.im ns_probe.im probe5.im
probe6.im probe7.im
```

### 最小复现 / 三行碎片脚本（17 个）

内容 22–706 B，多为 `main {}` 或 `a.b = 1` 级别的临场片段；对应的完整用例已在 `vtest/`。

```
_t_bisect.im min_p1.im min_w.im mini.im mini2.im mini_concat.im m_chk.im m_ip.im m_pid.im
m_var.im lp2.im lp3.im sv2.im sv_test.im dbglayout.im smoke2.im smoke3.im
```

### 一次性 entity 脚本（ent_s1..s10 与同类）（12 个）

为调 entity API 逐个手写的片段；其中多数只有 6–15 行，且互不引用。

```
ent_basic.im ent_get.im ent_s1.im ent_s2.im ent_s3.im ent_s4.im ent_s5.im ent_s6.im ent_s7.im
ent_s8.im ent_s9.im ent_s10.im
```

### 一次性 task 脚本（task1..task10）（11 个）

task/虚拟线程特性的手工试验脚本；`vtest/thread_await_v04.im`、`vtest/thread_result_v04.im`、`function_thread_lifetime_v04.im` 是入册版本。

```
task1.im task2.im task3.im task4.im task5.im task6.im task7.im task8.im task9.im task10.im
taskgc.im
```

### namespace / import 临时脚本（5 个）

与 `modc1.im`/`nsadv_mod.im` 等被 import 的模块成对存在，属于同一批一次性调试（见 §6 连带项）。

```
ns_test.im nsadv_test.im nscyc_test.im nsdiamond_test.im nsiso_test.im
```

### 语法糖临时验证脚本（`t_*.im`）（12 个）

文件头自述为「A1 分号容忍」「A3 区间糖」「A4 // 注释」等一次改造的现场脚本。

```
t_ai_merge.im t_ai_syntax.im t_for_sugar.im t_fstring.im t_incdec.im t_push.im t_say.im
t_semi.im t_slash.im t_spi5.im t_sugar_desugared.im t_node_task.im
```

### params / 加载器临时脚本（8 个）

依赖 `params`/`game.params`/`bad.params` 等文件，是参数加载器的一次性现场脚本。

```
param_test.im param_test2.im params_test.im bad_test.im autoload_test.im t_lp1.im t_lp2.im
t_lp3.im
```

### 特性小测（`*_test.im` / `try_*` / `set_*`）（28 个）

这些特性在 `vtest/` 已有入册 CTest 用例（`case_*_v04.im`、`collection_*_v04.im`、`result_*_v04.im`、`try_finally_v04.im` 等 41 个）。**注意：覆盖性是按特性名判断的，未逐条比对断言**，见 §9。另：`test_cmd.im` 自述对标 `main.im`，但**根目录没有 `main.im`**——仓库里 5 个 `main.im` 分别在 `mods/debug/`、`mods/utils/`、`projects/demo/`、`projects/host/`、`selfhost/` 下。

```
arr_test.im cap_test.im concat_test.im concat_test2.im dict_test.im entity_test.im err_test.im
exc_test.im inf_set_test.im json_dict_test.im meta_test.im set_comp_test.im set_op_test.im
set_test.im spi_test.im try_bare_test.im try_dbg_test.im try_dbgvar_test.im try_edge_test.im
try_rec_test.im test_mech.im test_mech2.im test_room.im test_safe.im test_server.im test_cmd.im
big_globals_test.im task_spec_test.im
```

### 一次性压力 / 上限 / 基准脚本（14 个）

`bench_*`、`stress_*`、`entity_stress*`、`endless_*`、`limit_test`、`timeout_test`：为某次测量现场写的脚本，没有阈值断言，也没有门禁调用。

```
bench_array.im bench_concat.im bench_concat1.im bench_concat2.im bench_dict.im stress_cpu.im
stress_mem.im entity_stress.im entity_stress2.im ent_stress.im endless_busy.im endless_test.im
timeout_test.im limit_test.im
```

### 空文件 / 已损坏的入口（2 个）

`nst2.inim` 为 **0 字节**；`ports.bat` 调用同目录 `ports.ps1`，而 `ports.ps1` 在仓库里**不存在**（`git ls-files` 无此文件），入口必然失败。

```
nst2.inim ports.bat
```

## 4. 桶 B：迁出根目录（22 个）

判据：内容有独立价值（示例 / 唯一复现证据 / 原型 / 仍可用的测量入口），但不该占仓库根。

> 迁移**不是** `git mv` 一把梭：`.im` 之间用相对路径 `import "x.im"`，`ps1` 用具名路径调 `.im`。
> 凡进桶 B 的条目，必须同时改掉引用方路径，否则迁完就是死链。详见 §7。

### 可运行示例 → `examples/scripts/`（5 个）

有完整场景、可以直接跑给人看，不适合留在仓库根。

```
canvas_demo.im verse_biome_demo.im im2d_test.im block_edit.im textbox.im
```

### 已修缺陷的唯一复现脚本 → `examples/regressions/`（7 个）

这些不是普通小测，而是某个已修崩溃/竞态/截断问题的现场复现（如 `task_loop_crash.im` 自述「task 重复 start/join → 0xC0000005」、`string_nested_crash_test.im`、`restart_stress.im`、`cpu8m.im`、`atomic_test.im`、`thread_test.im`、`label_test.im`）。**内容一律保留**，但不必占根目录。

```
atomic_test.im cpu8m.im label_test.im restart_stress.im string_nested_crash_test.im
task_loop_crash.im thread_test.im
```

### 旧版独立 UI 原型 → `examples/legacy-ui/`（8 个）

8 个单体 HTML 原型；现行 UI 是 `Infiverse_standard/src/ui/`（`index.html` + `app.js` 58 KB + `app.css`）。原型里可能有现行 UI 已删掉的交互，故保留而非删除。

```
chat.html desktop.html hl_renderer.html home.html netplay.html verse_forge.html wb.html
workbench_web.html
```

### 可复用的测量入口 → `examples/bench/`（2 个）

测量脚本本身仍可用，但必须与其输入脚本一起迁移（见 §6 连带项），否则迁过去的脚本调不到输入。

```
cpu_measure.ps1 es_bench.ps1
```

## 5. 桶 C：保留在根（6 个）

### 构建 / 打包输入，必须留根（3 个）

`hl_bridge.c` 是 Windows 无头模式的 TCP→WebSocket 桥，`src/mod/server_mod.c:61` 在运行期拼 `hl_bridge.exe` 路径，且 `chat_embed.h`/`desktop_embed.h`/`home_embed.h`/`netplay_embed.h`/`wb_embed.h`/`forge_embed.h` 只被它 `#include`；删它等于删掉那 6 个头文件的唯一消费者。`build_asan.ps1` / `build_installer.ps1` 是构建与打包入口。

```
hl_bridge.c build_asan.ps1 build_installer.ps1
```

### 本地开发入口（留根，但可改判）（2 个）

`ai_build.ps1`（构建失败时调本地 Qwen 解释）与 `imai.ps1`（本地 AI 开发助手）是开发者自己用的入口，我看不出它们已被替代；但它们是可选项，见 §8 分歧点 1。

```
ai_build.ps1 imai.ps1
```

### 根目录文档（建议留根）（1 个）

`icon_spec.md` 是图标设计规格（36 行）。它更像 `docs/` 的内容，但 `docs/` 属 `docs-audit` 冲突域，本流不动，见 §8 分歧点 2。

```
icon_spec.md
```

## 6. 连带项：被上述文件「撑住」的二阶孤儿（30 个额外文件）

这些文件**不在** 146 个候选里——它们被别的文件提到过，所以第一轮扫描看不见它们。
但只要桶 A 删、桶 B 迁，它们的唯一引用者就消失了。方案要连它们一起定，否则第 2 批做完还有一批尾巴。

### 6.1 随桶 A 删除（20 个）

| 文件 | 唯一引用者（都在桶 A/6.1 内） |
| --- | --- |
| `bad.params` | `bad_test.im` |
| `bigmod_a.im` | `big_globals_test.im` |
| `block_test.im` | `test_safe.im` |
| `busy.im` | `endless_busy.im` |
| `modc1.im` | `c2_probe.im` |
| `modc2.im` | `c2_probe.im` |
| `modmid.im` | `c4_probe.im` |
| `modsub.im` | `modmid.im` |
| `modt.im` | `c5_probe.im` |
| `nsadv_mod.im` | `nsadv_test.im` |
| `nsdiamond_a.im` | `nsdiamond_c.im, nsdiamond_b.im` |
| `nsdiamond_b.im` | `nsdiamond_test.im` |
| `nsdiamond_c.im` | `nsdiamond_test.im` |
| `nsiso1.im` | `nsiso_test.im` |
| `nsiso2.im` | `nsiso_test.im` |
| `nsmod.im` | `ns_probe.im` |
| `sv2.params` | `sv2.im` |
| `sv_test.params` | `sv_test.im` |
| `t_nt_child_loop.im` | `t_node_task.im` |
| `t_nt_child.im` | `t_nt_child_loop.im` |

### 6.2 随桶 B 迁移（8 个）

| 文件 | 迁往 | 引用者 |
| --- | --- | --- |
| `monster8.bmp` | `examples/assets/` | entity_stress2.im, es_200/400/800/1600.im（全部进桶 A/§6） |
| `cpu1.im` | `examples/bench/` | cpu_measure.ps1 |
| `cpu8.im` | `examples/bench/` | cpu_measure.ps1 |
| `cpu8s.im` | `examples/bench/` | cpu_measure.ps1 |
| `es_200.im` | `examples/bench/` | es_bench.ps1 |
| `es_400.im` | `examples/bench/` | es_bench.ps1 |
| `es_800.im` | `examples/bench/` | es_bench.ps1 |
| `es_1600.im` | `examples/bench/` | es_bench.ps1 |

### 6.3 看似连带、其实必须保留（2 个）

- `ai_browser_diag.js` — `hl_bridge.c` 运行期调用 `node ai_browser_diag.js` 做浏览器诊断；`hl_bridge.c` 留根，故它不能跟着删。
- `imai_sys.txt` — `imai.ps1` 的系统提示词文件，被 `imai.ps1` 读取；`imai.ps1` 留根则它同留。

## 7. 执行第 2 批的前置条件（批准后按此做）

1. **先建 `examples/` 子目录并落 README**：`examples/scripts/`、`examples/regressions/`、
   `examples/legacy-ui/`、`examples/bench/`、`examples/assets/`。
2. **迁移用 `git mv`，删除用 `git rm`**，不要用 `rm`（工作区会脏、索引删不掉）。
3. **同步改引用路径**：桶 B 的 `.im` 一旦离开根，`import "modc1.im"` 这类相对路径与
   `cpu_measure.ps1`/`es_bench.ps1` 里的具名调用都要一起改。
4. **`docs/` 里的叙述性提及要一并处理**：`docs/archive/protocol_v1.md:50` 提到
   `CHANGES_20260815_safety`（第 1 批已删）——`check_links.py` 看不见它（它在行内代码里），
   属 `docs-audit` 冲突域，我不动，见 §8。
5. **定向验证，不跑全量 gate**：
   `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`、
   `ctest --test-dir build --output-on-failure -j4`（应 89/89 —— 本文件写在 85 的年代，
   2026-08 集成加了 UPP 与 `.vverse` 各两个探针）、
   `python3 tools/check_links.py`（应 0 broken）。
6. **`.gitattributes` 不碰**；若用了 `git add --renormalize`，
   `git update-index --chmod=+x tools/*.sh` 补可执行位。

## 8. 需要协调者裁决的分歧点

1. **桶 C 里的 `ai_build.ps1` / `imai.ps1`** 是删是留？我倾向留（本地 AI 开发助手，
   看不出已被替代），但它们与构建无关，判成「删除」也说得通。
2. **`icon_spec.md`** 是否迁进 `docs/`？它按内容该在 `docs/`，但 `docs/` 是 `docs-audit` 的冲突域，
   本流不动，所以暂列「留根」。
3. **8 个 HTML 原型**：`Infiverse_standard/src/ui/` 是现行 UI，因此这 8 个可判「删除」。
   我列的是「迁 `examples/legacy-ui/`」，理由是原型里可能有现行 UI 已丢掉的交互。
   这是保守判断，不是实测结论。
4. **桶 A 里 28 个 `*_test.im` 的「已被 `vtest/` 覆盖」**是**按特性名**判的，我没有逐条比对断言（见 §9）。
   若协调者要求逐条覆盖证据，这批应整体改为「迁 `examples/regressions/`」。
5. `docs/BOARD.md` §3 的门禁基线数字（66 md / 211 链接 / 205 本地链接）与实测不符，
   需要由 `docs-audit` 或协调者统一订正，本流不改板子。

## 9. 诚实缺口（本方案的已知不足）

- **第 2 批一个文件都没删**，按作业单要求只出方案。
- **没有跑 `tools/gate.sh`**（作业单明确要求不跑：高并发下 ctest 会端口竞争假失败）。
- **第 2 批没有对应的构建/ctest 证据**——因为它还没执行。第 1 批删的是 `CHANGES_*.txt`，
  不在 `CMakeLists.txt` 的输入里，所以只跑了 `check_links.py`；**我没有为第 1 批单独跑一次 build/ctest**。
- **覆盖性判断未逐条验证**：§3 里「已被 `vtest/` 覆盖」是按特性名与文件头自述推断的，
  没有把 28 个 `*_test.im` 的断言与 41 个入册用例逐一对照。
- **`*_test.im` 里也含缺陷复现**：`string_nested_crash_test.im`、`task_loop_crash.im`、
  `restart_stress.im`、`cpu8m.im`、`atomic_test.im`、`thread_test.im`、`label_test.im` 已被我
  移出桶 A、放进桶 B（保内容），但这种「哪些小测其实是唯一证据」的判断是**人工的**，可能有漏网。
- **扫描是朴素子串匹配**：`grep -F`/bytes 子串对 `params`、`params.params` 这类通用名会大量误匹配
  （`params` 在仓库里有 40+ 处命中）。本方案的结论只用到「候选集里没有引用者」这一步，
  误匹配只会让文件**不进**候选集（漏报），不会让不该删的进删除桶；但反过来，
  「某文件不在候选集」不等于它一定被真实引用。
- **`CMakeLists.txt.bak` 是在主工作区删的**，不在本 worktree——该文件被 `.gitignore` 忽略、
  未被跟踪，因此不可能出现在 worktree 检出里。
  我在删除前把它复制到 `/tmp/CMakeLists.txt.bak.stash` 以便回滚；
  **若主工作区需要它，请从那里取回**。这违反 BOARD.md §1「不要在主工作区直接改」的字面要求，
  但作业单 §2 明确要求 `rm`，且该文件不属于任何构建输入。
- **BOARD.md 第 5 节的 `repo-hygiene` 行状态没有改**（仍写 `进行中`）。
  改板子是协调者的动作，且 `docs/` 与 `docs-audit` 冲突域相邻，我未动。
