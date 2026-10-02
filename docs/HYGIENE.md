# docs/HYGIENE.md — 仓库根残留清理：度量修正与第 2 批分类方案

> 执行流：`stream/repo-hygiene`（冲突域：仓库根，除 `README.md` / `LICENSE` / `CMakeLists.txt`）。
> 作业单：[streams/repo-hygiene.md](streams/repo-hygiene.md)。
> **第 1 批与第 2 批的「迁移」部分已执行（§1、§10）；第 2 批的「删除」部分仍是方案（§3–§7）。**
> §3–§9 是动工前的方案，其数字与结论仍按原文保留；**执行事实以 §10 为准**。

## 0. 状态一览

| 批次 | 范围 | 状态 |
| --- | --- | --- |
| 第 1 批 | 24 个 `CHANGES_*.txt` + `CMakeLists.txt.bak` | **已执行并提交**（§1） |
| 第 2 批 · 桶 B | 22 个文件迁出根目录（§4） | **已执行**（迁 `examples/`，见 §10） |
| 连带项 · §6.2 | 8 个二阶孤儿迁出根目录 | **已执行**（见 §10） |
| 第 2 批 · 桶 A | 118 个零引用文件删除（§3） | **已否决（不删）** —— 判据按构造不可满足，见 §11 |
| 第 2 批 · 桶 C | 6 个留根文件（§5） | **已决断：留根**（一个都没动，见 §11） |
| 连带项 · §6.1 | 20 个随桶 A 删除的二阶孤儿 | **已否决（不删）** —— 随桶 A 一起，见 §11 |

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
   `ctest --test-dir build --output-on-failure -j4`（应 97/97 —— 本文件写在 85 的年代，
   2026-08 集成加了 UPP 与 `.vverse` 各两个探针、CRP 三个、`json_min` 一个、超大包一个、
   `.im` 打包往返一个）、
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
   → **协调者裁决：见 §11。既不要求逐条证据、也不迁移、更不删除——该判据按构造不可满足
   （`vtest/` 测语言运行时，这批测特性与缺陷复现），桶 A 与 §6.1 整体否决（保留）。**
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

## 10. 第 2 批「桶 B 迁移」执行记录（已执行）

> 本节是**执行事实**，写于第 2 批的迁移部分完成之后。作业单只批了「只搬不删」的
> 桶 B 迁移，没有批桶 A 的删除，因此 §3 的 118 个文件**一个都没删**，§6.1 的 20 个
> 连带删除项也**一个都没删**。

### 10.1 规模与去处

| 去处 | 数量 | 来源 |
| --- | --- | --- |
| `examples/scripts/` | 5 | 桶 B（§4） |
| `examples/regressions/` | 7 | 桶 B（§4） |
| `examples/legacy-ui/` | 8 | 桶 B（§4） |
| `examples/bench/` | 2 | 桶 B（§4） |
| `examples/bench/` | 7 | §6.2 二阶孤儿（`cpu1/cpu8/cpu8s`、`es_200/400/800/1600`） |
| `examples/assets/` | 1 | §6.2 二阶孤儿（`monster8.bmp`） |
| **合计** | **30** | 22 个桶 B + 8 个二阶孤儿 |

分桶计数**实测复核过**：桶 B 22 个，5 / 7 / 8 / 2 的拆分与 §4 逐条一致；§6.2 的 8 个文件
全部存在。**§6.2 的排除条件（被 `CMakeLists.txt`、`vtest/` 或桶 C 引用则不迁）不触发**——
全仓检索确认这 30 个文件没有任何一处被 `CMakeLists.txt`、`vtest/` 或 §5 的 6 个桶 C 文件引用
（引用者只有 `cpu_measure.ps1`、`es_bench.ps1`、`entity_stress2.im` 三处，全部随本批一起处理）。

### 10.2 逐文件哈希核对（唯一验收点）

`git hash-object`（blob 哈希，等于逐字节内容）在迁移前后各算一次，30 个文件**全部相同**，
文件模式也全部保持 `100644`。索引里旧路径**一个都不剩**（`git ls-files <旧路径>` 为空），
`git status` 全部显示为 `R`（重命名），没有出现「删一个、加一个」的假重命名。

| 原路径 | 新路径 | 迁移前 blob | 迁移后 blob | 判定 |
| --- | --- | --- | --- | --- |
| `canvas_demo.im` | `examples/scripts/canvas_demo.im` | `79df0699b10eaaa7e18e234f800a880e935fabe7` | `79df0699b10eaaa7e18e234f800a880e935fabe7` | 相同 |
| `verse_biome_demo.im` | `examples/scripts/verse_biome_demo.im` | `5c8ec8ff87049137a69df76c7484ded822395169` | `5c8ec8ff87049137a69df76c7484ded822395169` | 相同 |
| `im2d_test.im` | `examples/scripts/im2d_test.im` | `efbb9c50fdb2a4e3a71a02bf05070976f0ec873d` | `efbb9c50fdb2a4e3a71a02bf05070976f0ec873d` | 相同 |
| `block_edit.im` | `examples/scripts/block_edit.im` | `f73e762a85aa6683a2c5c0fd896629f6b1ddd985` | `f73e762a85aa6683a2c5c0fd896629f6b1ddd985` | 相同 |
| `textbox.im` | `examples/scripts/textbox.im` | `d357f5ef5728b9d16ba4cde21eaca4d535a37fdc` | `d357f5ef5728b9d16ba4cde21eaca4d535a37fdc` | 相同 |
| `atomic_test.im` | `examples/regressions/atomic_test.im` | `44a067080e7682dc7d58382bce17da876b2a77cf` | `44a067080e7682dc7d58382bce17da876b2a77cf` | 相同 |
| `cpu8m.im` | `examples/regressions/cpu8m.im` | `7ce2e75ffbe6da7e2aa169e8d1737fb19e580967` | `7ce2e75ffbe6da7e2aa169e8d1737fb19e580967` | 相同 |
| `label_test.im` | `examples/regressions/label_test.im` | `fcd2d978704eeaea9e5bb9037f1406a063a09729` | `fcd2d978704eeaea9e5bb9037f1406a063a09729` | 相同 |
| `restart_stress.im` | `examples/regressions/restart_stress.im` | `97e7f954f94ff3e161911501308aafa23cdb7ef5` | `97e7f954f94ff3e161911501308aafa23cdb7ef5` | 相同 |
| `string_nested_crash_test.im` | `examples/regressions/string_nested_crash_test.im` | `7d9e45eb5ff99587d4b2addd9bbfb5905be3e359` | `7d9e45eb5ff99587d4b2addd9bbfb5905be3e359` | 相同 |
| `task_loop_crash.im` | `examples/regressions/task_loop_crash.im` | `5c4dd0c463d1411808fae67bfb1ec5b902c3cdbc` | `5c4dd0c463d1411808fae67bfb1ec5b902c3cdbc` | 相同 |
| `thread_test.im` | `examples/regressions/thread_test.im` | `eaddf5ddf4d03743a5e0f9c5e030564d694fe558` | `eaddf5ddf4d03743a5e0f9c5e030564d694fe558` | 相同 |
| `chat.html` | `examples/legacy-ui/chat.html` | `35dfabf59e9b56baad511d3915bffb504db81edc` | `35dfabf59e9b56baad511d3915bffb504db81edc` | 相同 |
| `desktop.html` | `examples/legacy-ui/desktop.html` | `f0272142623bf64b34824f73f5b242422fe348b1` | `f0272142623bf64b34824f73f5b242422fe348b1` | 相同 |
| `hl_renderer.html` | `examples/legacy-ui/hl_renderer.html` | `564f2292c6e8f0172156a097ae2f48cd8cb6b956` | `564f2292c6e8f0172156a097ae2f48cd8cb6b956` | 相同 |
| `home.html` | `examples/legacy-ui/home.html` | `740c4724a338ae2596ce8093b507fc0c943b430e` | `740c4724a338ae2596ce8093b507fc0c943b430e` | 相同 |
| `netplay.html` | `examples/legacy-ui/netplay.html` | `e0b6d05510ea9a703ec906b7b40a699bd9b37f21` | `e0b6d05510ea9a703ec906b7b40a699bd9b37f21` | 相同 |
| `verse_forge.html` | `examples/legacy-ui/verse_forge.html` | `2e6a53104ec1fe975c413a3f3091c61df7bcbff8` | `2e6a53104ec1fe975c413a3f3091c61df7bcbff8` | 相同 |
| `wb.html` | `examples/legacy-ui/wb.html` | `3c346112d2d9e27590219ee04e47f90c8479b1a0` | `3c346112d2d9e27590219ee04e47f90c8479b1a0` | 相同 |
| `workbench_web.html` | `examples/legacy-ui/workbench_web.html` | `c05d9a6cba57b347d7d309343fdc6aa9f1473634` | `c05d9a6cba57b347d7d309343fdc6aa9f1473634` | 相同 |
| `cpu_measure.ps1` | `examples/bench/cpu_measure.ps1` | `7efe473aabf26fc438573ff329dc623f9052ef91` | `7efe473aabf26fc438573ff329dc623f9052ef91` | 相同 |
| `es_bench.ps1` | `examples/bench/es_bench.ps1` | `92f5c447fb6e5d4b5920939048f673611d6b5ea6` | `92f5c447fb6e5d4b5920939048f673611d6b5ea6` | 相同 |
| `monster8.bmp` | `examples/assets/monster8.bmp` | `de113074f5a4a558791b87fb6868ab056e635f82` | `de113074f5a4a558791b87fb6868ab056e635f82` | 相同 |
| `cpu1.im` | `examples/bench/cpu1.im` | `2d2e9e7b3063a30982a36bbb39dcd4fb7cf17ae0` | `2d2e9e7b3063a30982a36bbb39dcd4fb7cf17ae0` | 相同 |
| `cpu8.im` | `examples/bench/cpu8.im` | `3e80fad764a18f78e01aecaa255aa7fa08d755ef` | `3e80fad764a18f78e01aecaa255aa7fa08d755ef` | 相同 |
| `cpu8s.im` | `examples/bench/cpu8s.im` | `69f6df68096d6c2b8822680036a107feafcdeaa0` | `69f6df68096d6c2b8822680036a107feafcdeaa0` | 相同 |
| `es_200.im` | `examples/bench/es_200.im` | `3d6d71a2d8ba24aa12321e3870f631f575260591` | `3d6d71a2d8ba24aa12321e3870f631f575260591` | 相同 |
| `es_400.im` | `examples/bench/es_400.im` | `ffa0cf86ce8c2a3bec7b77f96f798da174f8973e` | `ffa0cf86ce8c2a3bec7b77f96f798da174f8973e` | 相同 |
| `es_800.im` | `examples/bench/es_800.im` | `20ff9bfdda29a420d4ad094f897f68dde369fe74` | `20ff9bfdda29a420d4ad094f897f68dde369fe74` | 相同 |
| `es_1600.im` | `examples/bench/es_1600.im` | `380bd132f7749f13a7fdacf5efcf41203f9d011a` | `380bd132f7749f13a7fdacf5efcf41203f9d011a` | 相同 |

**结论：30 / 30 逐字节相同，0 个内容变化。本批是迁移，不是删除，也不是改写。**

### 10.3 本批同时改动的**非迁移**文件（与上表无关，单独列出）

| 文件 | 改动 | 为什么 |
| --- | --- | --- |
| `entity_stress2.im`（第 8 行，桶 A，**未删**） | `gui_sprite("m"+n, "monster8.bmp")` → `gui_sprite("m"+n, "examples/assets/monster8.bmp")` | 该字符串是**相对 cwd 的文件系统路径**（`src/mod/gui_mod.c` 的 `load_bmp_cached`/`load_bmp` 原样交给 `LoadImageA`/WIC，不做资源解析）；纹理搬走后不改这一处，留在根的脚本才会坏 |
| `README.md` | Layout 增加一行 `- examples/ — …` | 根布局变了，索引要跟着变 |
| `examples/README.md`、`examples/{scripts,regressions,legacy-ui,bench,assets}/README.md`（5 个新增） | 新增索引与说明 | 迁移需要去处说明；`docs/HYGIENE.md` 不再唯一承载「这些文件为什么在这里」 |

**代价（不粉饰）**：桶 A 的 118 个文件按作业单**一个都没删**，但 `entity_stress2.im` 的
**内容**被改了——它是桶 A 文件，不在 10.2 的哈希表里。这是本批唯一一处「搬运之外」的
源码改写，用途是保住一个留在根的脚本不被搬动作废。

### 10.4 已知残留与未解决项

1. **`es_200/400/800/1600.im` 仍写着裸名 `"monster8.bmp"`（知道会 dangling，故意没改）**。
   它们是被**迁移**的文件，改一个字节就破坏 10.2 的验收点；因此保留原样，并把影响写进
   `examples/bench/README.md`。这四个脚本只有在进程 cwd 里能看见 `monster8.bmp`
   （例如 cwd 设为 `examples/assets/`）时才能找到纹理。
2. **`home_embed.h` 是过期快照**：把根目录 6 个 `*_embed.h` 的 C 字符串数组解码后逐字节比对
   HTML，结果是 `chat.html` / `desktop.html` / `netplay.html` / `wb.html` 与各自 header
   **除末尾一个换行外完全相同**；`verse_forge.html` 与 `forge_embed.h` 只差「生成器在每个
   换行前插了一个空格」（26 449 B vs 26 894 B）；而 **`home.html`（5 537 B）比
   `home_embed.h`（解码 3 900 B）新**，多出的标记（如 `.profile-head`）只存在于 HTML 里。
   `hl_renderer.html`、`workbench_web.html` 没有对应的 embed header。仓库里**没有**
   任何生成器脚本能重建这些 header。移走的 HTML 没被构建读取，因此不影响构建；
   但**人类可读的那份源现在在 `examples/legacy-ui/`**，这个对应关系只记录在
   `examples/legacy-ui/README.md` 和本节。
3. **本批自己的零引用计数与 §2 的 138 / 146 不一致**：我按「文件名全仓字面子串」重扫
   （排除 `.git/`、`.worktrees/`、`build/`、`node_modules/`、`.npm-tmp/`、文件自身、
   `docs/streams/repo-hygiene.md` 与 `docs/HYGIENE.md`），得到根目录 **128** 个零引用文件
   （115 `.im` + 8 `.html` + 4 `.ps1` + 1 `.inim`），既不是 138 也不是 146。
   已定位到的具体差异来源：`docs/STATUS.md` 里点名了 `ports.bat` 与 `smoke2.im`（所以它们
   不算零引用），而 `docs/HYGIENE.md` 自身点名了每一个候选（所以按 §2 的口径要排除它）。
   **差异没有被我抹平**；桶 B 的 22 个与 5/7/8/2 拆分不受影响，实测与 §4 一致。
4. **引用面没有全覆盖**：`docs/STATUS.md` 在叙述段落（行内代码）里提到 `block_edit.im`、
   `textbox.im`、`cpu8m.im`。这不是 markdown 链接，`check_links.py` 与 `check_doc_paths.py`
   都看不见它；`docs/` 除本文件外是 `docs-audit` 的冲突域，本流没动，只在此登记。
5. **`examples/bench/*.ps1` 是 Windows 专用**（硬编码 `D:\inimerse_stable\inimerse.exe`），
   本机无法实际执行，所以「迁移后这些脚本仍能跑」这一点**我没有运行验证**，只有静态的
   路径与目录关系核对。
6. **本批没跑**：没有删除任何文件、没有触碰 `.gitattributes`、没有 `git add --renormalize`、
   没有 push、没有合 `main`、没有动 `src/verse/`、`src/common/`、`src/mod/verse_dist_mod.c`。

---

## 11. 裁决：桶 A **不删**（`hygiene-bucket-a`，已否决）

> 协调者裁决，2026-10。板上原写「先交出一张『候选文件 ↔ 覆盖它的 `*_test.im` 断言』对照表，再谈删」。
> **结论：这张表不做了，桶 A 与 §6.1 的 20 个连带项一起不删；桶 C 的 6 个维持留根。**
> 板上的 `hygiene-bucket-a` 行状态记为 **`已否决`**（见 [BOARD.md](BOARD.md) §5 的状态取值）。

三条理由，按分量排序：

**1. 那条判据按构造就不可满足——不是「还没核对」，是「核对不了」。**
桶 A 的判据是「已被 `vtest/` 覆盖」。但 `vtest/` 里是 46 个 `*_v04.im`，全部由
`CMakeLists.txt:340-365` 逐条 `add_test` 注册，测的是**语言运行时**（result / collection /
type / posix 等）；桶 A 里那 28 个 `*_test.im` 测的是**特性与缺陷复现**（`arr_test.im`、
`try_edge_test.im`、`string_nested_crash_test.im` …）。两者是**不同的层**，不存在「同一断言的
两份实现」这种对照关系。继续把这一行挂在板上，等于把一件**做不了的事**伪装成**待办的事**——
这正是 §5 修订说明里警告过的「板子会自己骗自己」，只是方向相反（这次是骗自己「还欠着工作」）。

**2. 它们不是死文件，是「没人跑的手动套件」。**
实测：**没有任何东西枚举根目录的 `.im`**。`CMakeLists.txt` 只注册 `vtest/*.im`；
`tools/` 下没有 glob 根目录 `.im` 的 runner（`tools/release_verify.py`、`tools/inim.test.py`
都不扫）；而 `contract_test.im:2` 自己写着
`# usage: inimerse.exe --time-limit 60 contract_test.im`——**手动**调用。
同时 `docs/STATUS.md:567` 与本文 §3「特性小测」把它们**按名字列为引擎测试清单**。
也就是说「零引用」量的是**自动化**引用面，不是价值；删掉等于删掉那份清单的**唯一副本**。
（真正的缺口是「这套手动测试没有 runner」，不是「这些文件多余」——但补 runner 不在本流内，
见下面再议条件。）

**3. 删除不可逆，而保留的代价已经接近于零。**
桶 B 的迁移已经证明这批「零引用」文件里有真东西：30 个里 7 个是**已修缺陷的唯一复现脚本**
（§4「已修缺陷的唯一复现脚本」）。桶 A 里同类风险**已经暴露过一次**——`string_nested_crash_test.im`、
`task_loop_crash.im`、`restart_stress.im`、`cpu8m.im`、`atomic_test.im`、`thread_test.im`、
`label_test.im` 被作者从桶 A 移到桶 B 保内容（§9）。既然连方案作者都自述这个判断是**人工的、
可能有漏网**（§9），那么用「不可逆的删除」去换「根目录少 118 个文件」在成本上不划算：
保留的代价是 118 个文件占着根目录，删除的代价可能是永久丢掉一份没有被任何自动化覆盖的证据。

**再议条件（什么情况下可以重开）**：

- **（a）真的逐条核对了**：把 28 个 `*_test.im` 的断言与 `vtest/` 入册用例逐条对照，且结论是
  「每条断言都有等价的自动化覆盖」。这是原判据的字面要求，做出来了就可以删。
- **（b）反过来做（更可能）**：不删，而是给这批文件补一个**真正的 runner**，把「零引用」变成
  「被引用」。这条路的**风险由 runner 的设计承担，而不是由删除承担**，所以它比 (a) 更安全；
  但它不在本流内，因为其中含压力/上限/交互类脚本（`stress_mem.im`、`endless_busy.im`、
  `timeout_test.im`、`test_room.im`、`textbox.im`），直接塞进自动门禁会挂——必须先分类
  「可无人值守」与「需人工观察」。
- **（c）** §8 第 1、2、3 条那三个分歧点（`ai_build.ps1`/`imai.ps1`、`icon_spec.md`、8 个 HTML 原型）
  **本次一并维持原判**：前两个留根，HTML 原型已在第 2 批迁进 `examples/legacy-ui/`。
  它们与桶 A 的删除问题解耦，不因本次否决而改变。

**本次没有做的事**：没有删除任何文件；没有移动任何文件；没有改 `tools/check_doc_paths.py`
（`archive-changes-refs` 走的是「明文规则 ＋ 就地补注」，见 [STATUS.md](STATUS.md) §1 硬规则 6）。
本次裁决只改文档，工作树里除这四个 `.md` 外无增删——**根目录既没有多一个文件也没有少一个文件**。
（口径提醒：本文件 §10 曾用「根目录条目数 228→198」作为桶 B 的规模指标，那是 `ls -1 | wc -l`
的结果，**包含未跟踪的构建产物** `build/`、`inimerse`、`inimerse.exe`、`inimerse.lib`；
按 `git ls-files` 计，根目录跟踪文件是 **181** 个、跟踪条目 **200** 个。两个口径不要混用。）
