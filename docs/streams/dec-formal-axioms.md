# T7 公理台账 —— 每条假设有名、有地址、有状态

T1..T6 的结论里有一串 `proved-conditional`：那个形容词是**一句话**，不是**一条依赖链**。
本页交的是台账：模型里每一条假设，被谁喂、拿什么喂、抽掉它哪一格会翻——都写成读数。
台账不是新证明，它是让已有证明的"条件"变成可查对象。

## 0 边界与标签

三档状态，定义先写好，后面每个数字都挂在这三档上：

- `discharged`（被喂饱）：假设的值由**可指名的代码行**决定，台账把那些行逐字验过。
- `asserted`（没人喂）：没有任何一行代码保证它，只能声明；台账必须同时给出
  `discharge-candidate=`（一条具体行号）与 `why-not=`（一句话说清为什么现在还不能算被喂）。
- `vacuous`（没被用到）：把它从模型里删掉，**所有变体的判定一个都不变**。这一档最容易被当成
  "小问题"跳过，其实是三档里信息量最大的：一条没被任何结论消费的假设，既不能支撑结论，
  也不会削弱结论；而一条**过强**的假设会让结论变成空话（T2 的 `0 <= frame_sp[i]` 就是这么错的：
  它排掉了正常空栈返回路径，于是"证明"的是一个不可达状态）。所以判据写成一句话：

  > **没有变异测试的公理，与注释同权。**

每行还带一个 `role=`，因为假设在**不同的主张**里可以有不同的状态，压成一行就是撒谎：

- `role=z3-model`：这个假设进入 z3 模型，被 `RESULT` 行消费。
- `role=extraction-premise`：这个假设是抽取类主张（T2 的 A-FRAME 穷尽性）的前提，不进 z3。

读数分级：`measured` = 由命令产出；`asserted` = 写在台账里、但这一轮没有人喂它；
本页没有 `proved` 级的读数——台账不证明任何东西，它只记账。

页内**不出现任何 `路径:行号` 形式的引用**：这样本页不占全仓 pointer 预算（T1..T6 那批页
已经把预算用到上限），而逐字读数（含每行 `decl=` 的行号）另存在同目录的
`docs/streams/dec-formal-axioms.readings.txt`，本页只引它的形状与行数。这条"引用形状、不抄行号"
的做法是验收者定的：新页只写路径名与 `[obs: <树>]` 记录。

## 1 一条命令

```
cd /tmp/wt-z3c
python3 tools/z3bounds/axiom_ledger.py --root /tmp/wt-z3c
```

构造（读数依赖环境，环境也要报）：工作目录 `/tmp/wt-z3c`（git worktree，分支 `z3bounds-t1`），
读数树 `f046f20`（`git rev-parse --short=7 HEAD`），工作树干净（`git status --porcelain` 无输出），
`python3 --version` = `Python 3.14.4`；变异列要用 Windows 解释器
`/mnt/c/Python314/python.exe`（`z3 5.3.2`），脚本经 `wslpath -w` 换成交给它的
Windows 路径——T1 时 WSL 的 `python3` 没有 z3，所以那一路要走 Windows；本机后来装了 z3 5.3.2，同一个切换器现在也接受命令名，并把 POSIX 解释器直接喂给脚本，两种解释器跑出的读数逐行相同（见 §14.6a）。

退出码：`0` 台账完整（含"变异列未跑"的诚实降级，见 §3 第 3 条）；`2` 连生成器/树都读不到。

逐字输出（45 行，树 `f046f20`）存在 `docs/streams/dec-formal-axioms.readings.txt`，
下面是它的首行与末行，用来核对"读的是同一个东西"。

这段构造描述是 T1 时代抄进去的：那 45 行与那个树号都属于当时。从 T12 起，那个文件的行数与树号由它自己的抬头负责（记录、校验、以及为什么必须这样，见 §14），本页不再转述——本页转过一次，就错过一次。

```
AXIOMS|sites=18|extracted=9|unparsed=9
LEDGER|axioms=8|discharged=3|asserted=0|vacuous=5|mutation=measured|sites=18|extracted=9|unparsed=9
```

## 2 为什么站点要对齐机器，而不是对齐散文

`AXIOMS|sites=` 不是手数的：台账正则扫生成器里**引入假设的位置**（注册表行、`z3.Bool(`、
`Solver().add`、我自己的 `asserted` 标记），然后给每个站点找最近的注册表行当归属；
找不到归属的，**逐字打印成 `unparsed`**，不猜、不吞。本次 `unparsed=9`，九行原文分别是
（只抄内容，行号在 readings.txt 里）：两处 `self.add()` 表达式构造、一处 `z3.Bool` 声明发射器、
一处 `asserted` 注释、五处 `s.add(` 发射口。它们各自是"把已注册的假设写进脚本"的**发射口**，
不是假设本身；台账承认自己没把它们归到某条公理名下，而不是硬安一个归属把 `unparsed` 压成 0。

把 `unparsed` 压到 0 的诱惑与把 `suspicious` 压到 0 的诱惑是同一种：
**让数字好看，而不是让事实清楚**。

## 3 三种状态在读数里长什么样

行形状（这里用 `<path>:<line>` 占位，逐字行号见 readings.txt）：

```
AXIOM|name=<id>|status=discharged|role=z3-model|scope=pointwise|kind=measured|decl=<path>:<line>|text=<原文>|settles=<变体>
AXIOM|name=<id>|status=vacuous|role=z3-model|scope=pointwise|kind=measured|decl=<path>:<line>|text=<原文>|drop-verdict=same|note=<为什么没被用到>
AXIOM|name=<id>|status=asserted|role=extraction-premise|scope=exhaustive|kind=measured|decl=<path>:<line>|text=<原文>|discharge-candidate=<path>:<line>|why-not=<一句话>
```

三条实例分别解释这三档（本条只抄 `text=` 与结论字段，行号见 readings.txt）：

- **被喂饱**：`A3`，`text=VM_FRAME_REGS == 1024`，`settles=P4|asis`——抽掉它，`P4|asis` 从
  unsat 翻成 sat（`changed=2`）。`decl=` 指向的是 `src/vm/vm.h` 里那一行宏。
- **没被用到**：`A6`，`text=param_slots == (func_argc[fidx] > argc ? func_argc[fidx] : argc)`，
  `drop-verdict=same`——抽掉它，17 个变体一个不动。
- **两种事实并存**：`A8`，`text=t->base >= 0 && t->base % 1024 == 0`，模型里空转（`vacuous`），
  但它自己的主张是**穷尽性**的（`src/` 里 `t->base` 有 7 处写点，注册表只 pin 了 3 处），所以它同时带着
  `discharge-candidate=` 与 `why-not=`。两个事实都写出来，不合并。

## 4 读数：三个汇总行 + 八条变异

```
AXIOMS|sites=18|extracted=9|unparsed=9
CONDITIONAL-SUMMARY|claims=8|open=0|all-asserted=8
LEDGER|axioms=8|discharged=3|asserted=0|vacuous=5|mutation=measured|sites=18|extracted=9|unparsed=9
MUTATE|axiom=A3|drop|verdict-changed=yes|from=unsat|to=sat|variant=P4|asis|changed=2
MUTATE|axiom=A5|drop|verdict-changed=yes|from=unsat|to=sat|variant=P1|extent|changed=1
MUTATE|axiom=A7|drop|verdict-changed=yes|from=unsat|to=sat|variant=P4|asis|changed=2
MUTATE|axiom=A1|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=17
MUTATE|axiom=A2|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=17
MUTATE|axiom=A4|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=17
MUTATE|axiom=A6|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=17
MUTATE|axiom=A8|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=17
```

三条纪律，因为它们各自对应一种"看起来绿了"的假象：

1. **变异列必须是真跑过 z3 的**。`MUTATE` 行的 `from=`/`to=` 是同一套 17 个变体在"基线脚本"与
   "删掉这条公理的脚本"两次 `RESULT` 行上的对比，不是推断。台账不自己写 z3 语义：它调用同一个
   生成器（`--drop-axiom`）产出两个脚本，再用同一个 Windows 解释器跑——**两边是同一个模型，只差这一条假设**。
2. **`settles=` 要指到具体变体**。写"这条公理很重要"没有信息量；写 `settles=P4|asis` 才让下一个人
   能去翻那一行 `RESULT`。同理 `variant=` 与 `changed=<n>` 一起给：只报"翻转了"，
   读者不知道翻了几格，也不知道是不是只有一格。
3. **降级要说出来**。这台机器上没有 z3 时（`--python /nonexistent/python`），变异列全变成
   `verdict-changed=unmeasured`，并多打一行：

   ```
   LEDGER-UNMEASURED|why=no z3 interpreter at /nonexistent/python|effect=no axiom can be called vacuous in this run, so every status below is registry-only
   LEDGER|axioms=8|discharged=6|asserted=2|vacuous=0|mutation=unmeasured|sites=18|extracted=9|unparsed=9
   ```

   注意降级后 `vacuous=0`、`discharged=6`：**这不是说这些公理被喂饱了，而是说这一轮没有能力判断谁被用到**。
   一个不写这句降级的台账，会把"没跑变异"读成"全部承重"。反过来，把降级做成 `exit 2` 也不对：
   其余三列仍然是真读数，扔掉它们等于因为一列不可测而拒答三列。

## 5 变异图：只有三条公理是承重的

| 公理 | 角色 | 抽掉后 | 结论 |
|---|---|---|---|
| A3 `VM_FRAME_REGS == 1024` | z3-model | `P4|asis` unsat→sat（changed=2） | 承重 |
| A5 `func_count` 界限 | z3-model | `P1|extent` unsat→sat（changed=1） | 承重 |
| A7 `param_slots <= 1024` | z3-model | `P4|asis` unsat→sat（changed=2） | 承重 |
| A1 `reg_cap == 2097152` | z3-model | 17 格全不动 | 空转 |
| A2 `frame_cap == 2048` | z3-model | 17 格全不动 | 空转 |
| A4 `-1 <= sp <= 1023` | z3-model | 17 格全不动 | 空转（见 §6） |
| A6 `param_slots` 的定义式 | z3-model | 17 格全不动 | 空转 |
| A8 `base % 1024 == 0` | z3-model | 17 格全不动 | 空转 |
| A-FRAME `frame_sp` 槽位 | extraction | `element_writes` 2→0 | 承重（抽取侧） |

这张图是 T1/T2 交付以来第一次说清"**证明到底站在哪几条假设上**"：站在 A3、A5、A7 三条上，
而不是站在"COMMON 那五条都成立"这种集体名词上。它同时解释了 T5 的机制论证为什么能那么硬：
`R` 与 @@B```B@@ 的具体数值（A1/A2）根本没被消费，被消费的是 @@B```B@@ 与 `guard` 的关系式（A3/A7）。

空转的五行不是废纸：它们是**可撤回的假设**。该做的不是删掉它们（删了模型更弱、故事更短），
而是让它们各自带着状态躺在台账里，谁将来要用到就先把状态改掉。

## 6 A4 的两张脸：模型里空转，抽取侧仍是前提

同一个 `A4` 在两种主张里状态不同，所以台账给两行：

- `role=z3-model`：`status=vacuous`、`drop-verdict=same`。没有任何 z3 结论消费 sp 的范围
  （抽掉它 17 格不动）。
- `role=extraction-premise`：`status=asserted`。T2 的 A-FRAME 穷尽性主张继承了"`frame_sp` 槽位落在
  栈范围内"，而那条范围就是 sp 的范围——它没有被抽取工具穷尽验证过（`src/` 里 `t->sp` 有 36 处
  写点，注册表只 pin 了 2 处），所以 A-FRAME 那条主张今天仍然**有条件**。

两行并存不是重复，是拒绝把"在不同主张里有不同状态"压成一句话。这也直接解释了为什么
`CONDITIONAL-SUMMARY` 只说 z3 侧全闭合，而 A-FRAME 那条要单独写：

```
CONDITIONAL|claim=T2-A-FRAME|variant=exhaustion|axioms=A-FRAME,A4|discharge=open|open-axioms=A4
```

## 7 A-FRAME 行：抽取工具的读数进台账

```
AXIOM|name=A-FRAME|status=discharged|scope=exhaustive|kind=measured|writes=2|text=frame_sp slots stay inside the stack range
MUTATE|axiom=A-FRAME|rename-token|verdict-changed=yes|from=element_writes=2|to=element_writes=0|instrument=extract_stack_effects
FRAME|FRAME-SP-NEGCTL|rename|frame_sp=>frameSp|element_writes=0|expected_drop=2|drop=2|name_exact=YES
FRAME|FRAME-SP-NEGCTL|decoy|frameSp[]=|decoy_sites=1|element_writes=2|unchanged=YES|counted_as=0|matcher=word-bounded
DEPENDS|axiom=A-FRAME|on=A4|why=both element writes store t->sp, so the frame slot range IS the sp range
```

`A-FRAME` 的变异不是 `--drop-axiom`（它不在 z3 注册表里），而是**改词**：把真词改名，读数必须掉到 0；
把假词（`frameSp`）塞进去，读数必须不动。两个方向都给了，判据同 T2 页：**负控制要写清允许往哪边动**。

## 8 台账第一次运行就抓到一个洞：声明了但没人查的 pin

做 T7 时，`pin_status` 报 A4 声明的第二条 pin **不在 pin 表里**。查下去：T5 那轮因为行号整体下移，
用"新树的行号集合"重建了 `PINNED`，而 A4 声明的第二条 pin 恰好不在那个集合里——于是
**33 条声明 pin 里只有 32 条真的被查**，而且没有任何读数会红：pin 检查遍历的是 pin 表，
表里没有的条目自然"通过"。

这不只是补一行的事，它是**"读数所依赖的每一行都要有 pin"这条纪律的反面案例**：
一条被声明、却没进表的 pin，等于一条被忘掉的 pin。补回那条守卫行（`src/vm/vm.c` 里 `push_int` 的
`sp >= 1023` 守卫）之后，脚本头部多一行 pin 清单，模型指纹从 `434b93d1d0508758` 变成
`dda5ff4a5fc75eb5`（`bytes=37648`）。**模型没有变松，它多了一条被检查的 pin。**
T5 页 §11 的期望值跟着改，改的理由与改动同一个提交——这条是验收者的 EXPECT 纪律。
补回后 `--self-check-pins` 仍是 `SELFCHECK-PIN|summary|cases=5|pass=5|fail=0`，
而 pin 漂移那格的故事也更清楚了：`line-shift` 现在点名的正是这条新补的 pin（第一处漂移），
说明"补回一条 pin"同时**扩大**了检查面。

同一轮还有第二个发现，即 §5 的变异图：`A6` 空转，恰好是验收者预判的那一格；
而 `A4`/`A8` 这两条**穷尽性**假设在 z3 侧也空转——它们的真价值在抽取侧（§6）。

## 9 什么没有被证明

- 本页**没有**任何代码级证明。`vacuous` 是**模型级**结论：一条公理可以在这个 z3 模型里空转，
  同时仍是代码的真实不变量（A4 就是：z3 侧空转，抽取侧仍是前提）。
- 变异列依赖 Windows 解释器上的 z3；机器上没有它时，台账降级并明说（§4 第 3 条），
  此时**不能**把 `vacuous=0` 读成"全部承重"。
- 写点计数是**词法**计数（`src/` 里 `t->sp` 36 处、`t->base` 7 处），它说明"注册表只覆盖了几个站点"，
  不构成穷尽性证明；用正则数写点，仍然可能漏掉宏或间接写。
- `unparsed=9` 是读数：九个发射口没被归到公理名下。把 `unparsed` 归零要做的是**改归属规则**，不是改数字。
- CTest 注册不归本 lane：台账可以在无 z3 的环境里跑（变异列降级），但**注册与否是 runner 的队列点**，
  本页不写测试名、不改 `CMakeLists.txt`、不动 `EXP_CTEST`。

## 10 树号与改动清单

- 读数树：`2891d55`（本页 §11 那一节提交时的树；抬头写它是因为它能被检出——先前的 `f046f20`
  是 `--amend` 之前的树，不在交付线上，见 §11.6）。父 `1699e3e` = 当时的 `origin/main`；工作树干净；
  `src/`、`selfhost/` 未动。
- 新文件：`tools/z3bounds/axiom_ledger.py`（344 行，纯 python3，无第三方依赖）、
  `docs/streams/dec-formal-axioms.readings.txt`（45 行逐字输出）。
- 已改文件：`tools/z3bounds/gen_bounds_proof.py`（`--drop-axiom` 支持；`PINNED` 补回 A4 的第二条 pin；
  `SELF_CHECK_EXPECT_SHA256` 随理由一起改成 `dda5ff4a5fc75eb5`）、
  `docs/streams/dec-formal-bounds-t5.md`（§11 期望值与"基线为什么动"一段）。
- 可复现性：同一命令连跑两次输出逐字相同（`cmp` 无差异）；本页自身的提交号不写进正文——自引用会漂，
  理由见 T2 页 §10。
## 11 一张表怎么证明自己是完整的

§8 记过 A4 那半个洞：`PINNED` 里少了 A4 的第二条 pin，于是 33 条声明只被检查了 32 条，而台账仍报
"ok / discharged"。那一次是靠人眼的偶然发现，不是读数。这一节把它变成可跑的一格。

判据一句话：**一张表的完整性不能由遍历表本身来证明。** 遍历 `PINNED` 的检查，对"表里少了东西"
天生沉默——少的那条不会被访问，也就不会被抱怨。

### 11.1 把期望写在表外

`EXPECT_PIN_COUNT = 33` 是写在 `PINNED` 之外的常量，并注明：移动它必须与理由同一个提交
（与本页 § 的"期望值是读数不是目标"同一条纪律）。检查往两个方向走，两个方向的参照物都在表外：

- 条数：`held != EXPECT_PIN_COUNT` ⇒ `pin entry missing: expected 33, table holds 32`；
- 声明：`AXIOMS` 与每个命题的 `guard_anchor` 声明的 pin，若表里没有 ⇒
  `declared pin not in the table: <path>:<line>`。A4 那次掉的正是这一条：声明的 33 条里，表只认识 32 条。

正常路径多一行读数（在"pin ok"之后、写脚本之前）：

    PINS|files=5|pins=33|expect=33|declared=19|missing_from_table=0

`files` 是涉及的源文件数，`pins` 是表里的条目数，`declared` 是被声明条的**去重**条数
（19 条声明会展开到 33 个 pin：A4、A8 这类穷举型公理声明一组写点）。
数不相等不是错，但两个数必须都在读数里——否则"表 33 条"和"被检查 33 条"会被混成一句。

### 11.2 负控四格（含一格走真退出路径）

进程内三格 + 真 CLI 一格，合计 `PINSCHECK|summary|cases=4|pass=4|fail=0`：

    PINSCHECK|case=intact|expect=exit0+pins=33+declared=19|got=no findings|verdict=PASS
    PINSCHECK|case=dropped-entry|expect=pin entry missing: expected 33, table holds 32|got=pin entry missing: expected 33, table holds 32|verdict=PASS
    PINSCHECK|case=declared-not-in-table|expect=declared pin not in the table: <path>:<line>|got=<同一句>|verdict=PASS
    PINSCHECK|case=cli-dropped-entry|expect=exit2+'pin entry missing: expected 33, table holds 32'+out_not_created|got=exit2+named+out=not_created|verdict=PASS

前三格把表作为**参数**递给检查函数（所以能递进去一张坏表）；第四格用 `--drop-pin-entry path:line`
走真进程，断言的是三件事同时成立：退出码 2、那句原话被印出来、**脚本没有被写出来**。
只断言退出码会漏掉"退出码对但已经写了半个脚本"，只断言文本会漏掉"文本对但退出码 0"。

### 11.3 为什么单开一个开关，而不是塞进旧自检

`--self-check-pins` 的 summary 行 `SELFCHECK-PIN|summary|cases=5|pass=5|fail=0` 已经被 runner
注册的测试钉住了。往那条自检里加格而改它的 cases 数，等于让别人的绿取决于我的改动；新开关
`--self-check-pin-table` 有自己的 summary，旧的那条一字未动（读数仍是 5/5）。
这也是"两个读数各自带地址"的一个具体用处：谁红，就知道是模型死了还是表缺了。

同一轮还量到一个小缺陷，属于同一族：两个自检开关**同时**给的时候，第二个被静默跳过——
因为分派处先 `return`。它不写在任何读数里，于是"没跑"和"跑了且通过"长得一样。
改法是两个请求都跑、退出码按位或，单开关的输出逐字不变（否则钉住那行 summary 的测试会红）：
`SELFCHECK-ALL|cells=9|kinds=pin-drift+completeness|note=every requested check runs, and none of them is a skip`（数字按实际请求的格数累加：两种开关 9，三种 14——一条把 14 说成 9 的合计行，就是静默跳过换了一层楼）。
判据同样一句话：**并排两个检查时，"都没报错"不是一个读数，要分别报。**

### 11.4 降级路径的实测格

`--python /nonexistent/python` 跑一次（该格在没有 z3 的机器上也能跑）：

    MUTATE|axiom=A7|drop|verdict-changed=unmeasured|from=-|to=-|note=no z3 interpreter at /nonexistent/python
    LEDGER-UNMEASURED|why=no z3 interpreter at /nonexistent/python|effect=no axiom can be called vacuous in this run, so every status below is registry-only
    LEDGER|axioms=8|discharged=6|asserted=2|vacuous=unmeasured|mutation=unmeasured|sites=18|extracted=9|unparsed=9

判据两条：必须真的印出 `LEDGER-UNMEASURED`；`vacuous` 那个位置必须是**词**而不是 `0`。
原来这一格印的是 `vacuous=0`，读者会把它读成"没有空转公理"，也就是"八条全部承重"——
把"没量"读成"最强的结论"，这是降级路径最危险的读法。改成 `unmeasured` 后，同一格无法被误读。

### 11.5 负控会污染它要检查的读数（本轮的实事故）

幽灵声明写成字面量 `("A9", ...)` 时，台账的站点扫描把它当成第九条公理，读数变成
`AXIOMS|sites=19|extracted=10`；改成 `tuple([...])` 构造后仍是 19/10——因为**注释里也写了那个字面形状**，
而扫描器连注释一起扫。把注释改成不含该形状的措辞后回到 `sites=18|extracted=9`。

教训与 §10 的"命令吃自己"同族，但方向相反：那边是判据把自己算进去，这里是**负控把自己算成被测对象**。
可操作的规则：负控的形状不要写成被测对象的形状；写成形状之后，连解释这句的散文也要检查一遍。

### 11.6 readings.txt 的抬头必须能被检出

`docs/streams/dec-formal-axioms.readings.txt` 原来抬头写 `tree: f046f20`，而 `f046f20` 是
`--amend` 之前的那棵树，`git merge-base --is-ancestor f046f20 9c13615` 非零——它不在交付线上，
没人能 `git show` 到它。规则：**抬头写一个能被检出的树号**，否则"逐字输出"里的树号等于自证。

改法不用改任何数字：在新树上重跑一遍，若两棵树读数逐字相同，就把"两棵树、一条读数"写进 § 的读数说明；
若不同，写明差在哪一行、为什么。读数不因为换了树而改，只有树号会被指认。

这一节自身也守页内零 locator 的规矩：上面所有行号都留在 `readings.txt` 里，正文只写文件名与形状。

## 12 命令、输出、与「我的机器」

§11 证明了一张表能自证不完整；这一节说的是同一件事在**引文**上的翻版：一段「命令→输出」如果只在写它的那台机器上成立，它就不是读数，是回忆。ivory 复现 T8 时抓到两处，都是我引的。

### 12.1 被静默模式吃掉的那一行

T8 的信里我写：

```
python3 tools/z3bounds/gen_bounds_proof.py --out /tmp/t.py --quiet
→ PINS|role=declared|files=5|pins=33|expect=33|declared=19|missing_from_table=0
```

照抄三遍，一个字都没有——`PINS` 行被包在 `if not args.quiet:` 里面。这与 270 是同一族：**不是数错，是命令与它声称的输出对不上**。责任不在读者，在读数的形状：一条计数的读数不该被「安静」掉。

改法不是把 `--quiet` 从引文里删掉（那样只是让引文变得能跑，读数仍然可以被一个开关删掉），而是让安静模式**只安静解释、不安静测量**：`--quiet` 现在只抑制逐条 `pin ok` 的旁白，`PINS`、`sha256`、`GIT-BLOB` 三行无论安静与否都印。一个「少说话」的开关能删掉的读数，就是读者引不动的东西。

同一处还捞出一个更硬的：`GUARD-PIN MISMATCH` 的比对整块原来也在 `if not args.quiet:` 里，也就是说**安静模式下这道检查根本没跑**，而它的沉默与「跑过且通过」完全一样。现在比对在安静模式下也跑，红依然 exit 2。

### 12.2 默认根必须来自树，不来自安装位置

台账的 `--root` 默认值是 `os.path.dirname(HERE)`，而 `HERE` 是 `tools/z3bounds` ⇒ 默认根是 **`tools/`** 这个目录本身。这个默认值在任何树、任何工作目录下都错，只有显式传 `--root` 才活；我信里的降级格省了 `--root`，读者照抄得到一段裸 traceback（`FileNotFoundError: .../tools/src/vm/vm.h`）。

病因比「少了参数」更硬：**默认路径若来自脚本安装位置，那么「只拿着一个 sha 的人」就永远复跑不了。** 两个工具现在都这样取根：先问 `git rev-parse --show-toplevel` 并验 `src/vm` 在不在；不行就从脚本所在目录逐级上溯找 `src/vm`；都找不到就打印一句 `cannot locate the repo root; --root is required` 并 exit 2——**不许 traceback**。显式传 `--root` 却指向没有 `src/vm` 的目录，同样具名拒绝。

### 12.3 三条负控：开关自己也要有控制组

§11 拿「删一条表项」当控制组，这一节把同样的做法用到开关上。`--self-check-flag-modes` 走真 CLI 五格：

- 单开 A ⇒ `SELFCHECK-PIN|summary|cases=5`；
- 单开 B ⇒ `PINSCHECK|summary|cases=4`；
- 同开 ⇒ 两个 summary **都在**，外加 `SELFCHECK-ALL|cells=9`；
- `--out … --quiet` ⇒ `PINS` 仍在；
- 仓库外、无 `--root` ⇒ exit 2 + 具名 + 无 traceback。

读数是 `FLAGMODES|summary|cases=5|pass=5|fail=0`。判据与 runner 的「`ctest -N` 在没跑门禁前是 168」是同一条：**一个不存在的测量与一个通过的测量，必须印出不同的东西**；三模式的格数 5 / 4 / 9 若有一个变了，这一格就红，而不是安静地报告「我这次恰好跑的那个模式」。

### 12.4 抬头必须让只拿 sha 的人复跑

`readings.txt` 的抬头原来写 `--root /tmp/wt-z3c`——那个目录读者没有，也不该有。现在抬头是「树的顶层 + 一条不含任何机器路径的命令」。复跑本身也成了这处修复的证据：在 `2891d55` 上跑同一条命令会**崩在 traceback**（那时 `--root` 默认指向 `tools/`），补上 `--root <该树顶层>` 之后与本节读数的差别只有五条 unparsed 行号（5 行换 5 行）。**信里引的命令与抬头里的命令必须是同一条**：T8 那封信恰恰是它们不一样，这才是 270 的成因，而不是笔误。

## 13 假设的射程：写点清单

§12 收尾于一件小事：一句引文必须能在别人机器上跑出来。这一节把同一把尺子对准**我自己的诚实**——A4/A8 的 \`why-not\` 里那句「the claim quantifies over all 36 write sites of t->sp; the registry pins 2 of them」，读起来像一条自我批评，其实是一句**散文**：没有清单、没有地址、没有可复跑的判据。T10 把它变成数；数一出来，那句话本身被证伪了。

### 13.1 「36」是怎么来的，以及它为什么不是 36

旧计数器用 \`re.escape(needle)\` 把 \`t->sp\` 当**子串**匹配，于是 \`nt->sp\`（新线程对象，5 处）和 \`nt2->sp\`（1 处）全被算进 \`t->sp\`；它又按**行**计数，于是一行里两个写点只算一处。36 = 30 处真实写点 + 5 + 1 处子串假象。这正是 §6「用名字代替结构」那一族的第二副面孔：**匹配器把别人的名字当成了我的名字**。

新的读数是数出来的，不是脚注出来的：

\`\`\`
WRITESITES|field=t->sp|sites=30|direct=30|unparsed=0|pinned-writesites=1|unpinned-writesites=29|source=src/**/*.{c,h}
WRITESITES|field=t->base|sites=7|direct=7|unparsed=0|pinned-writesites=3|unpinned-writesites=4|source=src/**/*.{c,h}
WRITESITES-SUMMARY|role=coverage|fields=2|sites=37|pinned-writesites=4|unpinned-writesites=33|constants=2
\`\`\`

每一处写点都单独成行（\`WRITESITE\`，37 行，逐字留在 \`readings.txt\` 里），所以「37」不是一个我记得的数，而是我数过的行数：自检的第一格就是拿 \`WRITESITE\` 的行数去对 \`sites\`。

### 13.2 钉的定义沿用同一把尺子——于是「pins 2 of them」也塌了

\`pinned=yes\` 的判据写死在读数行里，而且是**已有的那把尺子**，不是我新立一套：当且仅当该 \`(path, line)\` 在树的 pin 表里、且 \`pin_ok\` 认为原文逐字相等。行号漂了算 \`no\`，与 pin 检查同一个判据。

按它量，A4 声明的两处钉：一处是**字段声明**（\`int sp;\` 所在行），一处是 push 之前的**守卫**（\`sp >= 1023\` 那行）——没有一处是写点。写点里唯一被钉住的那一处（\`vm.c\` 里一行 \`t->sp--\`）并不在 A4 声明的两处之内，它是台账里别的用途钉的。所以旧文的「the registry pins 2 of them」同时错了两件事：写点总数错（36 vs 30），**钉的归属也错**（A4 的两处钉覆盖 0 处写点）。

A8 则相反：三处钉全部落在写点上（\`base += FRAME_REGS\`、\`base = frame_base[...]\`、\`base = 0\`），7 处里 3 处。因此 A4 与 A8 虽然都是 \`vacuous\`（z3 侧没人消费），它们「钉不住」的程度并不一样：一个是 1/30 且那 1 处不归它，一个是 3/7 且三处都归它。

### 13.3 那句话现在是一个格式化函数，并且被变异负控证明

\`why-not\` 里的数字与短语不再手写，而是由这份清单渲染：总数、被钉数、以及「声明的两处钉分别是什么角色」。证明方式是**把数改掉，看句子跟不跟着动**：

\`--census-drop-pin\` 把一条**落在写点上的**钉（A8 的 \`base += FRAME_REGS\` 那行）从本次运行的 pin 表里删掉，读数于是变化——\`pinned-writesites\` 3 → 2、\`unpinned-writesites\` 4 → 5——而 A8 的 \`why-not\` 也跟着从「the pin table names 3 of them」变成「the pin table names 2 of them」，并且前面多出「declared pin not in the table」的拒绝理由（§11 的那条路照旧在跑）。

这一格是整节的判据，ivory 的话说得很准：**一句话若在数变了之后还能照旧存在，它就是手写的。** 同时也捞出一条设计上的次序问题：第一版里「钉缺失」的分支会**替换**整句 \`why-not\`，于是变异之后那句关于写点的话消失了——数变了，句子没跟着变，只是被另一句顶掉。现在两句并置（拒绝理由 | 射程句），缺一不可。

### 13.4 抽取必须印出它没读懂的东西

恒等式是 \`WRITESITES|sites = direct + unparsed\`。读不懂的写点不静默并入 \`direct\`，而是逐行印 \`unparsed-write\`：本次树上 \`t->sp\`/\`t->base\` 都是 \`unparsed=0\`，所以「印不出来」这件事由**合成树**证明——内存拷贝、取地址后间接写、把字段当参数交给大写宏，三格分别落到 \`indirect\` / \`indirect\` / \`unparsed\`；一行纯读不算写点；一行注释掉的写点进 \`WRITESKIP\` 而不进 \`direct\`。

最后这条不是空谈：第一次跑，\`src/runtime/runtime_posix.c\` 里一段**注释掉的示例代码**被算成了一格真实的 \`direct\` 写点，因为它当时只做单行注释剥离，不携带块注释状态。修法是按行携带 \`in_block\`。这与 §11.5「控制组污染了它自己要检验的读数」是同一条病的第三例——**注释被数成了代码**，而唯一的征兆只是总数比预期多一。

### 13.5 不适用要写，超范围也要写

- 字段本身是规格常量的，不印空行：\`R\` 与 \`F\` 各一行 \`sites=0|note=spec constant\`。
- 同一个槽位经由**别的名字**被写的（\`g_cur_thread\`、\`nt\`、\`nt2\`、\`vm\`、\`f\`，本次合 18 处），单独一行 \`WRITESITES-ALIAS\`。它们按名字在索赔的射程外——**按名字，不是按测量**。真要按「同一个槽位」算，A4 的射程是 48 而不是 30；正文不偷偷合并这两个数，因为索赔写的确实是 \`t\`。

### 13.6 仍然钉不住的那一处，写在正文里

一个不写出字段名的宏（例如内部完成 \`sp++\` 的 \`SP_PUSH(t)\`）对任何**文本**扫描都不可见：没有名字可匹配，也就没有行可印。因此这份清单的完整性只对「写点里出现了字段名」的那些写成立。A4 在 T2 的提取里仍然是 \`asserted\`（role=extraction-premise），正是这一条洞；它现在有数，但数的**射程也有洞**，两件事必须同时说。

### 13.7 合计必须是求和的结果，不能是记忆

ivory 给的判据，写进这里当作规矩：**任何「合计」字段都必须是求和的结果，不能是记忆；常量只允许出现在「期望值」一侧**（\`EXPECT_PIN_COUNT=33\` 那种，且必须和表分开写、能被表不一致地证伪）。

本节怎么落实的：\`WRITESITES-SUMMARY\` 由两个字段行相加；\`constants=2\` 由常量列表长度算；自检的 \`WRITECHECK|summary|cases=6\` 里那个 6 也是 \`len(cells)\`。回想 §12.3 修掉的 \`SELFCHECK-ALL|cells=9\`——三个开关同开跑了 14 格却报 9，正是同一条规矩的反例：**合计字段一旦写死，它就变成了一句关于过去的记忆。**

六格的读数是 \`WRITECHECK|summary|cases=6|pass=6|fail=0\`：恒等式与行数、钉的变异、钉的角色、合成树上的三种不可读形状、常量行，以及下一节的「变异只走它声明的那条道」。

### 13.8 变异只许走它声明的那条道

ivory 复现 T10 时说：这次变异改掉了 A3，而 A3 并不声明被删的那处钉。我先复现这句话——**它的前提不成立**：A3 的登记里正好有那一行（它的第二处钉就是 push 之前的那个守卫），所以删掉它之后 A3 从 `discharged` 变成 `asserted` 是**对的**，不是越界。同一行被两处公理登记，是这棵树的事实，不是 bug。

但这条来信立的规矩成立，而且比它想修的那处缺陷更硬：**「只动了它该动的」如果没有人算过「该动的是谁」，那它就不是读数，是记忆。** 原来的变异格只断言「A8 变了」，从来没有断言「别的没变」；正因为没有人算过道，读者只能凭印象怀疑越界，被怀疑的一方也拿不出证据自证。

现在道是算出来的，一行印出来：

```
CENSUS-LANE|dropped=<path>:<line>|in-lane=A3,A8|rule=an axiom is in the lane iff its registry pins include the dropped line
```

判据是**相等**，不是包含：变了的公理名集合必须恰好等于道（本次 `lane=A3,A8|axioms-changed=A3,A8`）。只断言 A8 会重新变成一个可以靠运气通过的格子。

删一处钉还会带动一批**引用公理状态**的行：八条 `CONDITIONAL` 行的 open-axioms 多出 A3，`CONDITIONAL-SUMMARY` 从「全关」变成「全开」，`LEDGER` 的 asserted 计数跟着加一。这些不是新结论，是道内的推论，所以按**推导**验而不是按白名单放行：任何进入或离开某个 open-axioms 集合的公理必须在道内；`LEDGER` 的 asserted 增量必须恰好等于「道内从 discharged 变 asserted 的条数」。本次 `derived-rows=9|out-of-lane=0`。

顺手把 `CONDITIONAL-SUMMARY` 也变成求和的对象：从两次运行的行重算 claims / open / all-asserted 再与印出来的比。这一格自己先踩了一次 §13.1 的坑——第一版把 T2-A-FRAME 那一行（由 frame 小节单独印出，是**另一个**合计）混进了这个合计，重算得 9 而行里写着 8。凶手不是数学，是**我的检查把别人的合计当成了这一个合计**，与「`nt->sp` 被当成 `t->sp`」同族。改法是显式只取 `P<n>` 形状的索赔行，并把这条写进注释：一个能被证伪的过滤器，必须说得出它筛掉的是什么。

最后是 ivory 点名的「两个数同名」：`PINS` 行现在带 `role=declared`，写点清单的合计带 `role=coverage`，字段名也从 `pinned=` / `unpinned=` 改成 `pinned-writesites=` / `unpinned-writesites=`。声明的钉有 33 个、落在写点上的只有 4 个，两个数都对；错的是让读者以为它们是同一个数——读数行里出现同名字段，读者就会合并它们，合并之后两个数都变成废话。

改名还顺手弄红了**另一格**，值得记：`--self-check-flag-modes` 的 `quiet-keeps-pins` 格原来找的是字面前缀 `PINS|files=5|pins=33`；我在行首加上 `role=declared` 之后它找不到，于是「安静模式保留了读数」这一格报红——而读数其实还在。这不是改名的噪音，是**前缀判据把字段顺序偷偷写进了契约**：往后追加字段它照过，往前加就红，一个看起来对称的写法藏着不对称的行为。现在那一格按「`PINS|` 之后出现 `files=5|pins=33`」判，同样的判断，不再附带顺序假设。

两个数同名最贵的代价因此不是读者误解，而是**所有按字面读这两行的地方都跟着脆**：本次它只弄红了一格按需自检，但同一个格子在门禁里就是一次假红，或者更糟——一次假绿（前缀对上了，后面接的却是另一个数）。

这一节没有给 CTest 增加任何一格：`WRITECHECK` 是按需读数，不是门禁；注册与否是 runner 的队列点，§12.3 的示范格已经够它用了。



## 14 读数文件是命令的产物，或者它就不是读数

验收者的判据只有一句：**一份可以手抄的文件，迟早会被手抄错；它要么是命令的产物，要么它就不是读数。**

在此之前，读数文件的抬头是 24 行手写散文：每一轮我复制它、改树号、改说明。T9 抓到过一次（抬头写了一个 `git show` 取不到的树），T11 又抓到一次（说明里引用的行号随上方插入而漂移）。两次的修法都只是"下次小心"。本节取消的是那个动作本身。

### 14.1 记录：抬头与树由程序写

```
python3 tools/z3bounds/axiom_ledger.py --record-readings <path> --python python3
READINGS-RECORD|path=<path>|tree=531e9ef|lines=103|body=93|rc=0
```

抬头 10 行（7 行规则散文 + 树 + 解释器 + 分隔行）由代码里的常量写，正文是**同一次运行的 stdout**，逐字，93 行。文件里不再有一句话是手写的，因此也没有一句话能过期而没人知道。

脏树拒绝写：

```
READINGS-REFUSED|why=the worktree has uncommitted changes, so the header would name a tree that is not what printed|first-change=M <path>
```

退出码 2，且一个字节都不写。这条规矩当场抓到了我自己：第一次记录时工作树里正躺着上一次记录的旧文件，命令拒绝执行；我把它恢复成 HEAD 版本之后才记成。判据是**抬头指的那棵树必须包含打印它的东西**——未提交的改动让这句话为假。

未跟踪文件不算脏（只查被跟踪的改动）：编辑器残渣不改变树里被跟踪的内容。这一条也有一个格子（14.3 的第五格）。

### 14.2 校验：第一个不同的行被点名

```
python3 tools/z3bounds/axiom_ledger.py --check-readings <path> --python python3
READINGS|same=yes|lines=93|tree=531e9ef|python=python3
READINGS-DIFF|line=<n>|want=<文件里的那行>|got=<这棵树打印的那行>
```

先查抬头：树能解析、树是 HEAD 的祖先、且 `tools/z3bounds` 在这两个提交之间没有变（变了说明这份读数属于另一棵树）；解释器与抬头记录的必须一致。然后才逐行比正文。行号是**正文内**的行号，不是文件行号——这一点在 14.4 里咬了我一口。

### 14.3 六个格子，四个是反例

```
READINGSCHECK|case=file-is-its-own-product|got=rc=0 lines=93|verdict=PASS
READINGSCHECK|case=tamper-one-line|got=rc=1 line-named=yes|verdict=PASS
READINGSCHECK|case=wrong-tree-header|got=rc=1 verdict=no|verdict=PASS
READINGSCHECK|case=missing-file|got=rc=2 named=yes|verdict=PASS
READINGSCHECK|case=untracked-junk-is-not-dirty|got=rc=0 written=yes fields=yes|verdict=PASS
READINGSCHECK|case=tracked-edit-refuses-to-record|got=rc=2 refused=yes dirty=yes written=no|verdict=PASS
READINGSCHECK|summary|cases=6|pass=6|fail=0
```

两个"记录"格在 HEAD 的一次性 worktree 里跑，跑完 `worktree remove --force`：一个测试不应该把它正在测的那棵树弄脏。

### 14.4 反例自己两次不合格——这是本节真正的主读数

第一版 `tamper-one-line` 改的是正文的最后一行。正文最后一行是 `DEPENDS|…`，里面**没有数字**，于是文件逐字未变、校验返回 0，格子按"没点名"报红——它差一格就变成"什么都不改也算通过"的形状。改法不是换个位置，是按需要的性质选行（最后一个含数字字段的正文行），并额外断言改动确实发生。

第二版仍然红：期望行号我算成了文件绝对行号，而 `READINGS-DIFF` 印的是正文行号，差着一个抬头。

两条同一族：**控制组自己也是一个读数，它同样会抄错。** 一个不会失败的负控不是负控，它只是一句关于负控的记忆。14.3 里那条 `line-named=yes` 是这两次失败之后才得到的。

### 14.5 为什么开关在台账上，不在生成器上

来信的措辞是"让读数文件自己生成"。读数文件的正文是**台账的 stdout**，不是生成器的；把记录器挂在生成器上，就变成两个程序往同一个文件里写字，而没有一条命令能重跑整份文件。所以 `--record-readings` / `--check-readings` 是台账的开关。这是对措辞的更正，不是对目标的更改。

### 14.6 顺手抓到的两个洞：解释器与 role

（a）`--python` 原来只接受**存在的路径**，而默认值与文档里写的是一个命令名。本机实测 `--python python3` 会悄悄降级成"no z3 interpreter"，整列变异变成 unmeasured。现在命令名用 `shutil.which` 解析；并且 POSIX 解释器不再被 `wslpath -w` 换成一个 Windows 路径——把 Windows 路径交给 POSIX 解释器，与把 POSIX 路径交给 Windows 解释器一样，都是 file not found，而且报错长得像"脚本不存在"。改完之后，本机 WSL 侧的 z3 5.3.2 跑出 measured 列，与 Windows 解释器那次逐行相同：A3 的 `verdict-changed=yes|from=unsat|to=sat|variant=P4|asis|changed=2`，`LEDGER` 的 discharged=3 / asserted=0 / vacuous=5。**环境声明从此也是可核对的读数，而不是一句描述。**

（b）自检新增两格：`role-is-first-field` 与 `census-sums`。前者抓到 `AXIOM|name=A-FRAME` 的三行**从不说自己扮演什么 role**——同一类洞：一行的身份没有写出来，读者只能靠上下文猜它是 z3 模型还是抽取工具的读数。现在它带 `role=extraction-instrument`。后者断言每一行与合计都满足 `pinned-writesites + unpinned-writesites == sites`，且合计的 sites 等于字段行之和、`fields=` 等于字段行数。

读数：`WRITECHECK|summary|cases=8|pass=8|fail=0`，其中 `role-is-first-field` 得 `summary-first=role|summary-role=coverage|axioms-without-role=0`，`census-sums` 得 `fields=2|sums-ok=3`。

一处必须写清的分寸：AXIOM 行的 `role` 是**一个**量的枚举值（z3-model / extraction-premise / extraction-instrument），不是两个读数同名。所以"role 必须是第一个字段"的强形式只适用于**合计行**（`PINS` 与 `WRITESITES-SUMMARY` 都已满足），对 AXIOM 行断言的是弱形式：不许有一行不说自己的 role。这条区分写在格子的 expect 文本里，而不是留在这一页。

### 14.7 本页 §1 那段构造说明的位置

§1 里"45 行、树 `f046f20`"是 T1 时代的构造描述。现在是 103 行（抬头 10 + 正文 93），作者已经从人换成程序：**以文件自己的抬头为准，不要以本页转述为准**——本页转述过一次，就错了一次。§1 顺带说的"WSL 的 python3 没有 z3"在本机也不再成立（见 14.6a）。

这一节同样没有给 CTest 增加任何一格：`READINGSCHECK` 是按需读数；注册与否仍是 runner 的队列点。

## 15 「两侧一样」是一句结论，结论要么有命令，要么是口报

### 15.1 一句没有守住的话

上一轮为了绕开「主机防火墙拦住跨边界启动」，默认解释器从 Windows 侧的 exe 换成了本机 WSL 的 `python3`，并在信里写了一句：两侧读数一致。那句话是对的，但它是一个**关于两台机器的断言**，而当时没有任何东西在跑它：它既没有命令，也没有失败的可能。同一来源的判据在这条链上已经出现过三次（被 `--quiet` 吞掉的 `PINS`、在 `--quiet` 下**根本没执行**的守卫钉比对、被写死成 9 的 `SELFCHECK-ALL`），这次是第四次，只是换了说法——不是「检查被跳过」，而是「结论被口述」。

### 15.2 命令与它印出来的那行

    python3 tools/z3bounds/axiom_ledger.py --side-equivalence --side-left /mnt/c/Python314/python.exe --side-right python3

    SIDE-EQUIV|left=/mnt/c/Python314/python.exe (3.14.7, z3 5.3.2)|right=/usr/bin/python3 (3.14.4, z3 5.3.2)|lines=93|same=yes|differing=0|rc-left=0|rc-right=0

两个解释器都真的启动了（一个 3.14.7，一个 3.14.4，z3 都是 5.3.2），各自完整跑一遍台账，93 行逐行比，0 行不同。这才叫「两侧一样」。

### 15.3 七个格子，六个是反例

    SIDECHECK|case=one-side-not-installed|got=rc=2 named=True no-same=True|verdict=PASS
    SIDECHECK|case=side-without-z3|got=rc=2 named=True no-same=True|verdict=PASS
    SIDECHECK|case=one-side-differs|got=rc=1 first-diff-line=13 same=no=True|verdict=PASS
    SIDECHECK|case=both-sides-identical|got=rc=0 same-yes=True|verdict=PASS
    SIDECHECK|case=degenerate-defaults|got=rc=2 named=True no-same=True|verdict=PASS
    SIDECHECK|case=defaults-are-not-degenerate|got=rc=0 printed=True refused=False forbidden=False|verdict=PASS
    SIDECHECK|case=no-local-z3-falls-back-loudly|got=rc=0 said=yes measured=yes|verdict=PASS
    SIDECHECK|summary|cases=7|pass=7|fail=0

译成中文的规格：缺一侧不许算相同；有解释器但没有 z3 不许算相同；有真差别必须点名行号并 rc=1；同一个解释器两遍才允许 same=yes；**默认值解析成同一个解释器必须拒绝**；默认值要么真的是两个东西、要么拒绝；本机没有 z3 时默认回落必须出声。

### 15.4 会说话的负控

第一版的 `one-side-differs` 得 `first-diff-line=1`，而第 1 行是 `CENSUS-MUTATION`——负控**自己**的通报。于是「两侧不同」与「负控把自己印了出来」在输出上无法区分：一个只回显自己参数的比较，长得跟一个真读了数字的比较一模一样。改法是让注入的变异在那一侧**不出声**（丢钉子的通报关掉），差异于是只能落在数据行上：第一处不同落在第 13 行，A3 从 `discharged` 变 `asserted`；第 19 行是 A8 的 why-not 从「3 个」变「2 个」；第 28 行是 P1 从 `closed` 变 `open`。格子现在断言的不只是「rc=1」，还有「第一处不同的行号大于 1」——**差异必须落在被检查的对象上，不能落在这条命令的注脚上**。

### 15.5 名字不是对象

抬头里的 `python:` 从 `python3` 改成 `/usr/bin/python3 (3.14.4, z3 5.3.2)`。名字回答不了「这台机器上有没有 z3」，而整列变异的有无恰恰由它决定。校验侧因此也不再拿名字比名字：抬头里的解释器与本次运行的解析结果不同时，报的是一句**具名的不 同**（「这份读数是用 X 生产的，本次运行用的是 Y」），而不是把「机器不同」降级成「读数相同」。

代价要写明：换一台机器，这份文件**注定**报这条不同。那正是这条规矩在正常工作——它说的就是「这份读数属于那台机器」。

### 15.6 被忽略掉的决定也要是读数

`READINGS-RECORD` 在**成功**时也把忽略掉的东西印出来：

    READINGS-RECORD|path=<path>|tree=<sha>|lines=103|body=93|rc=0|dirty=no|untracked=ignored|first-change=none

拒绝时印 `dirty=yes` 与第一个改动。这一条不是措辞：14.3 里那两个记录格现在断言这三个字段本身，删掉任何一个字段，格子就红——否则「未跟踪文件不算脏」只是 一句传统。

### 15.7 这一节不证明什么

`same=yes` 说的是**这一台机器、这个提交**上两边印出的文本相同。它不证明另两台机器也相同；它也不证明两个解释器的 z3 版本不同时会相同——那种差异会落在变异列上，而那是**机器的事实**，不是树的事实。所以 `--side-equivalence` 只回答一个问题：换一个解释器跑同一棵树，读数会不会变。本机的回答是不会。

### 15.8 默认值自己也要是一条读数

上一轮把「两侧一样」变成命令之后，还剩一个默认值没被问过：`--side-left` / `--side-right` 的缺省是**谁来比较**。缺省若解析成同一个解释器，这个命令就会永久地印 `same=yes`——它比较的是一次运行与它自己，而输出与一次真比较一模一样。所以：

1. 只要有一侧来自默认值，就先印 `SIDES-DEFAULT|left=…|right=…|why-distinct=yes|no|from=both defaults|one default`，并且 `why-distinct=no` 时**拒绝**（`SIDE-UNMEASURED`，rc=2），而不是照旧比较。
2. 命令随之变成「两侧都给定」或「都不给定」：前者是显式比较（允许同侧，那是 `both-sides-identical` 这一格），后者必须真是两台解释器。

同一轮里 `--python` 的缺省也改了：它原本是 `os.environ.get("PY_WIN", DEFAULT_PY_WIN)`——本机 WSL 侧已经装了 z3 5.3.2，`PY_WIN` 却没设，于是台账**开箱**就走 Windows exe，本机的 z3 列从来没被当作测量列；而且任何一份读数文件换台机器开箱即红（「这份读数是用 X 生产的」）。现在缺省由 `default_body_python()` 决定（`PY_WIN` → 本机 `python3` 且能 `import z3` → 宿主 exe），并且把选择**印出来**：

    PY-SELECT|python=/usr/bin/python3|why=PY_WIN is unset and the local python3 imports z3

它走 **stderr**：这是**机器的事实**，而读数文件（stdout）是**树的事实**，要逐行比较。把机器的事实混进被比较的正文，两台机器就永远不可能相同。

### 15.9 我自己引入的那个缺陷（这一轮最值得写的）

把 `--side-left` 的缺省改成 `None` 之后，`side_equivalence()` 里那两行仍然写着 `side_output(args, args.side_left)`——于是两侧都拿 `None` 去跑，`resolve_python(None)` 解析不出解释器，整列变异**静默降级**成 `unmeasured`。两侧都是 unmeasured，输出逐字相同，`same=yes|differing=0` 堂堂正正地印了出来。

唯一露出来的是**行数**：`lines=94` 而不是 93。那一行多出来的不是数据，是 `LEDGER-UNMEASURED` 的通报——降级自己留下了痕迹，而结论没有变。所以 `lines=` 这个字段在这一轮里不是装饰：它是唯一没有被降级污染的量，也是我发现自己把「两边都没测」写成「两边一致」的唯一线索。

两条教训：

- `same=yes` 只在**两侧都 measured** 时才有意义。判断「测了没有」不能靠读结论，要靠一个与结论无关的量（这里是行数，也可以是 `rc-left`/`rc-right` 与 `mutation=` 的组合）。
- 改一个**参数缺省**时，要找的不是「哪里读了这个参数」，而是「哪里把参数原样传了下去」——`args.side_left` 在改名后仍然是个合法值（`None`），语法、类型、任何单侧运行都不会报错。现在两侧改用解析后的局部量，`None` 不可能再流进去。

## 16 用到的公理是测出来的，不是声明的

CONDITIONAL 那一行一直带着一个 `axioms=` 名单。它看起来像结论，其实是**作者的意图**：写的人记得这几条公理参与了这条 claim 的证明，就把它写上。没有一行代码验证过它，也没有任何一次失败会因为它写错而发生。

于是这一件事以前是这样处理的：我在信里说「T1 的证明只靠 A3、A5、A7」，ivory 去核，核完了互相点头。两边都没有说谎，但这句话在树上没有对应的量——它是一句**口报**。

这一节把它换成一个数：把每条公理单独拆掉，重跑全部 17 个变体；某条 claim 的判定变了，这条 claim 就用到了这条公理。用到就是「判定变了」，没有别的判据。

### 16.1 命令与读数

从树顶跑：

`
python3 tools/z3bounds/axiom_ledger.py
`

八条 claim 的实测依赖：

`
USES|claim=P1|variant=asis|axioms=none|source=measured|why=no single axiom drop flips this claim's verdict
USES|claim=P1|variant=extent|axioms=A5|source=measured|why=dropping A5 flips this claim's verdict
USES|claim=P2|variant=asis|axioms=none|source=measured|why=no single axiom drop flips this claim's verdict
USES|claim=P3|variant=asis|axioms=none|source=measured|why=no single axiom drop flips this claim's verdict
USES|claim=P4|variant=asis|axioms=A3,A7|source=measured|why=dropping A3,A7 flips this claim's verdict
USES|claim=P5|variant=asis|axioms=A3,A7|source=measured|why=dropping A3,A7 flips this claim's verdict
USES|claim=P6|variant=candidate_guard|axioms=none|source=measured|why=no single axiom drop flips this claim's verdict
USES|claim=P7|variant=asis|axioms=none|source=measured|why=no single axiom drop flips this claim's verdict
USES-GAP|missing=none|extra=none|rule=the union of the USES rows equals the set of DISCHARGED axioms -- set equality, not containment; a discharged axiom that no claim uses is MISSING from the union, and a used axiom that is not discharged is EXTRA in it
USES-SUMMARY|claims=8|links=3|unused-discharged=0|conflicts=0|gap=no
USES-COST|claims=8|axioms=8|cells=64|runs=9|variants-per-run=17|wall=3.3s|note=one drop run yields every claim at once, so the 64 cells cost no extra runs; restricting to the 3 discharged axioms would save runs today, but an axiom that is asserted now could start flipping a claim later and its row would silently go missing
`

读法只有三句：**判定动了才算用到**；**并集必须等于 discharged 集合**；**声明可以比实测宽，不能比实测窄**。

### 16.2 全集相等，不是包含

`USES-GAP` 的判据是**相等**。这一条比它看起来严格，方向也不太直觉：

- 一条 **discharged** 的公理，如果没有任何 claim 用到它，它就从并集里**缺**了 —— 差集印在 `missing=` 一侧。这正是「一条被算作已解除、但其实谁也不需要」的样子，也是这一格真正想抓的东西：一条可以整条删掉的公理，被登记成「已解除」，等于把证据记在了没人读的那一页。
- 反过来，一条 claim 用到、但状态不是 discharged 的公理，会出现在 `extra=` 一侧。那种情况说明变异列证明了它的必要性，而登记表不承认它已解除 —— 这时要看的是登记表，不是这一行。

我一开始把 `missing` 与 `extra` 写反了：注入一条「声明为 discharged、没人读」的公理时，我断言 `extra=AXI9`，实测却是 `missing=AXI9`。错的是断言，不是工具 —— 集合相等只有一种方向，被判据写下来的方向才是真的方向。ivory 的委任信里写的也是 `extra`，回信里点名了这一处，因为它和 T8 那次「负控写成了被测物的形状」是同一类：**用直觉的词汇描述一个集合关系，然后在断言里把这个直觉焊死。**

### 16.3 声明比实测宽了 15 倍

八条 CONDITIONAL 行合计声明 **45** 条公理，实测只有 **3** 条连接（并集 `A3,A5,A7`）。逐条看：

`
USES-SUPERSET|claim=P1|variant=extent|declared=A1,A2,A3,A4,A5|measured=A5|unused=A1,A2,A3,A4|note=a declared axiom the verdict does not need is information: the claim is weaker than its own declaration
USES-SUPERSET|claim=P4|variant=asis|declared=A1,A2,A3,A4,A5,A7,A8|measured=A3,A7|unused=A1,A2,A4,A5,A8|note=a declared axiom the verdict does not need is information: the claim is weaker than its own declaration
USES-SUPERSET|claim=P5|variant=asis|declared=A1,A2,A3,A4,A5,A6,A7,A8|measured=A3,A7|unused=A1,A2,A4,A5,A6,A8|note=a declared axiom the verdict does not need is information: the claim is weaker than its own declaration
`

其余五行（P1 的 asis、P2、P3、P6、P7）实测是 `none`：**单独拆掉任何一条公理，它们的判定都不动。** 这五行不是「没有公理」的意思 —— 它们的意思是「这五条 claim 的判定不由单条公理决定」，可能因为它们的结论由求解器的模型直接读出，也可能因为它们的假设被别人的判定挡在前面。这两种情况在这一列里长得一样，所以这一列只敢说「单条拆掉不动」，不敢说「不需要任何公理」。

这份宽声明不是错误，是**信息**：它说明登记表里的名单一直比实际需要宽一个数量级，而宽的那部分从来没有代价 —— 直到有人想删掉一条公理，才发现名单不能回答「删了会不会塌」。现在能回答了：删 A1、A2、A4、A6、A8 不会动任何 claim 的判定（它们本来就被判 vacuous，这里从另一侧确认了同一件事）；删 A3、A5、A7 会动，动的正是上表里点名的那几行。

### 16.4 四个负控与一个反证

`
USESCHECK|case=uses-union-equals-discharged|expect=the union of the USES rows is exactly the set of discharged axioms, and the gap row says none/none|got=rc=0 rows=8 axioms=8 union-miss=none union-extra=none gap=none/none|verdict=PASS
USESCHECK|case=injected-unused-axiom-is-a-gap|expect=an axiom declared discharged that no variant reads leaves every USES row alone and turns the gap red: the union is now MISSING it (missing= names the gap in the direction the set equality actually runs)|got=rc=1 rows-same=yes missing=AXI9 extra=none|verdict=PASS
USESCHECK|case=declared-reader-misses-the-gap|expect=the same injected axiom with the declared reader is NOT red: rc=0 and the gap is not checked, which is what makes the measured gap a measurement|got=rc=0 gap=not-checked/not-checked reader=declared|verdict=PASS
USESCHECK|case=declared-list-too-short|expect=a declared list missing an axiom the verdict uses is a conflict, not a note|got=rc=1 conflicts=1 names-claim=yes|verdict=PASS
USESCHECK|case=declared-reader-misses-the-short-list|expect=the declared reader cannot notice that its own list is short: rc=0 and no conflict row|got=rc=0 conflicts=0|verdict=PASS
USESCHECK|case=attribution-agrees-with-the-mutation-column|expect=every axiom a USES row names has a MUTATE row saying verdict-changed=yes, and the cost row adds up (runs = 1 + axioms, cells = claims x axioms)|got=agree=True runs=9 cells=64|verdict=PASS
USESCHECK|summary|cases=6|pass=6|fail=0
`

反证是中间那两行：`--uses-from-declared` 让 USES 直接抄 CONDITIONAL 名单。对同样的两条坏输入（一条没人读的 discharged 公理、一份被故意缩短的声明），它**两条都不红，rc=0**。一个抄名单的实现永远不会和自己的名单吵架 —— 所以那两格 PASS 不是「它做对了」，而是「它证明了另一条路是瞎的」。这条反证在 `USESCHECK` 里占两格，是这一节里唯一一处把「错误实现」当成读数印出来的地方。

负控自己也错了两次，两次都值得写：

- 第一版让 `--uses-declare-short` 在这条 claim 的声明里**随便弹掉一个元素**。实测 rc=0、`conflicts=0` 于是这一格红。原因不是工具没查，而是弹掉的那个公理本来就不被用到：声明里 45 条有 42 条是宽出来的，随机弹一条，最可能弹中的就是那条宽的。改成「弹掉一条**实测用到**的公理」之后才红。这就是 T13 那条纪律的第二次应用：负控必须选一个**机制坏掉就一定会变**的输入，而不是一个碰巧会变的输入。
- 第二版才轮到方向上那个错（见 16.2）。两次都是**断言错、工具对**，而两次都是我先怀疑工具。

### 16.5 成本：9 次跑，64 个格子

`USES-COST` 说清楚了三件事：格子数 64（8 条 claim × 8 条公理）、实际跑数 9（1 次基线 + 8 次各拆一条）、整段 3.3 秒。64 个格子只花 9 次跑，因为**一次拆解同时给出全部 claim 的判定变化**：拆 A3 的那次跑，八条 claim 的判定都在输出里，归因只是把它们逐条取出来。所以「逐条 claim 归因」这件事没有额外成本，成本是「每条公理一次跑」。

能不能只跑那 3 条 discharged 的？今天可以（省 5 次跑，约 2 秒）。不做，因为一条今天处于 asserted 的公理，明天补上证据、或者被别人的改动牵进某个判定时，它的 USES 行会**静静地不存在**——而「没有这一行」在读数里和「这一行是 none」长得一模一样。8 条公理的一次全跑比一个需要记得去扩的名单便宜。

### 16.6 它不证明什么

- 一次只拆一条，所以抓到的只有**单条充分性**：某条公理单独拆掉就让判定翻面。两条公理**合起来**才必要的情况（拆任意一条都不动、拆两条才塌），这一列看不见。要抓它得做组合拆分，成本是 C(8,2)=28 次跑起，而且组合爆炸不会停在二元。
- 「判定变了」是**求解器的判定变了**，不是「语义上真的依赖」。z3 的 sat/unsat 是对编码的回答；一条公理可能因为编码的方式被用到，也可能因为编码冗余而写进去却不影响判定。所以这一列的正确读法是「在**这个编码**里，这条判定需要这条公理」，而不是「这条定理在数学上依赖这条公理」。
- `none` 那五行同样只敢说「单条拆掉不动」。它们既可能真的不依赖单条公理，也可能依赖一个被别人的判定先挡住的公理 —— 这一列分不开这两种情形。**这两种情形在你想删公理时的动作完全不同**：前者可以直接删，后者删了会在别人的判定上塌下来，而这一列对两者印的是同一行。

这一节和 T13 的那一节是同一个动作：把「两侧一样」从信里搬进命令；这一次搬的是「用到哪几条」。两次的判据都是同一句 —— 一句结论要么有命令，要么就是口报。


## 17 进了逐行比较的东西，必须换一台机器也不变

### 17.1 一条会重复红的读数

同一棵树、同一台机器、几秒内连跑三次 `--check-readings`：

```
run1 rc=1  1 diff  got wall=2.5s
run2 rc=1  1 diff  got wall=2.7s
run3 rc=1  1 diff  got wall=2.5s
```

唯一一处差异是 `READINGS-DIFF|line=55`，落在成本行上：`want=wall=3.0s`、`got=wall=2.5s`，除 `wall=` 之外逐字相同。把文件里的 `wall=3.0s` 改成 `99.9s` 也照样红，说明比较是逐字节的，没有把 `wall` 排除在外。

所以此前那句 `READINGS|same=yes|lines=112` 是**巧合**：那一次耗时恰好也打印成 `3.0s`。「规则保证的对」与「巧合造成的对」，区别只在换一次运行还成不成立。一份装机器事实的正文，探的不是内容漂移，是机器身份。

根因在设计层：`USES-COST` 一行里混了两种量。`cells=64|runs=9|variants-per-run=17` 是树的，`wall=3.3s` 是机器的（同树三跑：3.2s / 2.9s / 2.6s；更早一次 6.6s）。一行里装机器事实，逐行比较就必然跟着机器走。

### 17.2 修法：机器事实搬到 stderr，不进正文

成本行只留计数；耗时另起一行 `USES-COST-WALL` 走 stderr。这与 §15 的 `PY-SELECT` 是同一条规则：**机器事实不进被比较的正文**。

顺带一条：`#` 开头的行在本实现里并不特殊 —— 正文就是 stdout，注释照进档案，所以「把 wall 注释掉」不是修法，搬出正文才是。

三跑的结果：

```
06bca6ffd182   wall=3.2s
06bca6ffd182   wall=2.9s
06bca6ffd182   wall=2.6s
```

正文逐字节相同，耗时在旁边变。记录后连查两次都印 `READINGS|same=yes|lines=112|tree=a462296`。

判据一句话：**能进逐行比较的，只有「换一台机器也不变」的量。**

### 17.3 缺陷二：一个字段名，两个判词

声明读取器跑 `--uses-from-declared --uses-inject AXI9` 时曾经同时印出：

```
USES-GAP|missing=not-checked|extra=not-checked|rule=a declared reader has no way to notice a discharged axiom that nothing uses
USES-SUMMARY|claims=8|links=8|unused-discharged=1|conflicts=0|gap=yes
```

`rc=0`。三处读数里有一处是错的，而这个字段名同时承担了两个判词：`gap=yes` 在实测模式里是「确实有缺口」，在声明模式里是「不知道」。两种「修法」都通向坏结果 —— 把声明模式的 rc 改成非零，反证格当场失效（反证要的正是「抄名单读不出缺口」）；相信 `gap=yes`，等于把一份抄来的名单当成发现。

现在两种模式各自说自己的话：

```
声明模式  USES-GAP|missing=not-checked|extra=not-checked|...
          USES-SUMMARY|claims=8|links=8|unused-discharged=1|conflicts=0|gap=not-checked   rc=0
实测模式  USES-GAP|missing=AXI9|extra=none|...
          USES-SUMMARY|claims=8|links=3|unused-discharged=1|conflicts=0|gap=yes             rc=1
```

「这个字段能不能被相信」由字段值本身回答，不由读者去查模式。

### 17.4 两个新格，一个旧格变严

- `body-carries-no-machine-facts`：记录下来的正文里没有 `wall=`、没有解释器身份，而同一次运行仍在 stderr 印出耗时。两半都要成立 —— 只查「正文干净」会漏掉「耗时被删掉了」。
- `same-file-twice-both-same`：同一棵树连查两次都绿。缺陷的形态正是「第一次就红、第二次还红」，所以这个格不能只查一次。
- `summary-does-not-say-yes-when-unchecked`（`USESCHECK` 第七格）：声明模式的 `gap=` 必须是 `not-checked`。

### 17.5 `--uses-declare-short` 收的是 claim 身份

例如 `P4|asis`。按公理名传会得到 `rc=2` 与 `problem=no such claim` —— 这不是缺陷，是「身份」与「名字」的区别：一个 claim 的身份包含它的变体名。这一条已写进 help，因为它靠猜是猜不出来的。

### 17.6 这一节不证明什么

这一节证明的是「正文里没有机器事实」，不证明耗时被记录过：耗时现在只在 stderr，任何档案都不留，要看就重跑。`body-carries-no-machine-facts` 靠关键字判定，若有人把 `wall` 搬到第三个地方（日志文件、环境变量），这一格看不见 —— 它守的是正文，不是整台机器。

### 17.7 摘要也要带主语

我上一轮写信报「正文是三跑的 md5 `06bca6ffd182`」，ivory 照这句话去核对，算出来是 `40f9ff8f02d7`。两个都对，因为它们算的不是同一份字节：它拿的是 `--check-readings` 的 stdout（那里只有判词，没有正文），我拿的是档案正文。一句话里的一个摘要，读者无法判断它是对哪一份字节算的 —— 这与我上一轮在 T13 修的是同一个病：**名字不是对象，摘要也不是对象**。

修法与 `wall=` 同源：让数自带主语。

```
READINGS|same=yes|lines=112|body-md5=134d4cd85e36|tree=53d716a|python=/usr/bin/python3 (3.14.4, z3 5.3.2)
```

定义写在字段名里：`body-md5` 是**被比较的正文各行**用单个换行连接后的 md5，不带结尾换行，不含以 `#` 开头的抬头行。同一条规则也用到了记录行：`READINGS-RECORD|...|body=112|body-md5=134d4cd85e36|...`。

于是同一份档案现在有三个数，各有各的主语，都不许互相替代：

| 数 | 算什么 | 谁印 |
|---|---|---|
| `134d4cd85e36` | 正文 112 行，单换行连接，无结尾换行，无抬头 | `body-md5=` 字段 |
| `06bca6ffd182` | 同 112 行加结尾换行（`md5sum` 的算法） | 手工 `md5sum` |
| `40f9ff8f02d7` | `--check-readings` 的 stdout | 没有字段，也不该有 |

第三个不是缺陷，是提醒：**任何能算出摘要的命令都值得问一句「它对哪一份字节算的」**。前两个的差别只有一个结尾换行——差异小到没人会注意到，正好是「摘要需要主语」最好的例子。
ivory 替我把这句话说得更准：**读者最容易犯的错不是漏写主语，是看见两个不同的数就判其中一个错**。我上一轮正是这么犯的：把 `06bca6ffd182` 报给它当「正文的 md5」，它算出 `40f9ff8f02d7` 之后我去查，才发现两个都不是错的读数——是我把「档案正文」与「命令 stdout」当成了同一个东西。给摘要加主语是补救；补救之前得先承认**两个数可以同时为真**。同一条也适用于 pin：`base=1348` 与 `now=1352` 同时为真，一个是旧基线、一个是这棵树的计数（见 §18）。

### 17.8 两格：摘要必须能被独立重算

- `body-md5-has-a-subject`：这一格**从档案正文自己重算** md5，再与命令打印的字段比；同时断言 `--check-readings` 的 stdout 里没有正文（`check-prints-body=no`）。第二半是必要的：若这条命令会把正文印出来，字段就可能只是正文的副本，重算也就证明不了它是摘要。读数：`field=134d4cd85e36 recomputed=134d4cd85e36 check-prints-body=no`。
- `header-edit-is-not-a-body-change`：改掉抬头里的一行散文，仍须 `same=yes` 且摘要不变，读数 `rc=0 edited=yes same-digest=yes`。这一格挑的是「只有整文件摘要才会不变」的输入：对任何从未改过抬头的运行，整文件摘要与正文摘要一样恒定，差异只在这类输入上才现形。

`READINGSCHECK` 现在报 `READINGSCHECK|summary|cases=11|pass=11|fail=0`。**这个数不作为常数写进句子**：它是各行求和的输出，格数变一次只写在行里就够了；写进散文就会过期——这正是本系列反复删的那种「记忆」。三格都走真实 CLI，没有一格是「我把字段加上去了所以它是对的」。

这一节不证明的是：摘要说的是「这些字节是这样」，不是「这些字节是对的」。一份被整体替换、且替换者也被记录下来的正文，`body-md5` 会老老实实地跟着变——它守的是一致性，不是正确性。
**这一格不许调用 `body_digest()`。** 第一版就是调它算的，而 `body_digest()` 正是被检验的那个实现：这样重算只能证明「打印值等于函数输出」，永远证明不了「函数按字段名里写的规则算」。改成长手写一遍规则（正文各行用单个换行连接、不带结尾换行、不含抬头、取前 12 位十六进制），证据才与被证物分开。这与 `check-prints-body=no` 是同一条纪律的两种用法：**证据不能与被证物共用实现，也不能与被证物共用字节**。

- `digest-subject-is-not-the-file`：对同一份档案，整文件 md5 与「正文加一个结尾换行」的 md5 都是读者真能算出来的数，所以「摘要说的是正文」只有在**按名字拒绝这两个数**时才是一条读数。读数：`field=134d4cd85e36 whole-file=dbf8d20b00e1 body+newline=06bca6ffd182`。后两列不是陪衬——它们正是 §17.7 那张三行表里的两个数，现在由一格断言替读者记着。

## 18 一个 `net=0` 能证明什么，不能证明什么

### 18.1 三个对象，一个字段名

HEADINGS 这条读数里其实有三个对象：**被数的那棵树**、**写出来的 pin 文件**、**承载它的那个提交**。字段名却只有一个 `tree=`。于是同一行可以被读成两句不同的话，两句都自洽：

```
HEADINGS|tree=e3d2dc2|base=1348|now=1352|removed=0|added=4
HEADINGS|pin|tree=ef8d7a9|docs=107|files=107
HEADINGS|stale|pin-tree=ef8d7a9|head=e3d2dc2|net=4
```

我在 e3d2dc2 上跑出来的就是这三行：`now=1352` 对，**但 pin 还是 1348**（pin 由 ef8d7a9 写下，漂移 +4）。ivory 承认这是它这一侧的缺陷，并把「三个对象各起名」派给了 runner（`measured-tree=` / `lands-in=` / `pin-measured-tree=` / JSON 里 `measured_on`）。这一节记的是我在这件事里学到的两条判据，而不是改名本身。

### 18.2 我那句要求是结构性不可能的

我原先要求「pin 行的 `tree=` 必须等于 `GATE|end` 的树，否则我不 rebase」。错在把纪律当成了等式：**pin 只能在数完之后写进一个提交**，所以「pin 的树」永远是「被数的树」的后继，两者相等在构造上不可能。该问的不是两个 sha 相等，而是 `net=0`。这条我认。

### 18.3 但 `net=0` 守的是计数，不是身份

承认完上面那条之后，还剩一个更要紧的问题：**`net=0` 够不够承担结论？** 不够。pin 文件里只有三个东西：文件数、标题总数、以及每个文件的标题数：

```
{
 "docs": 107,
 "headings": 1344,
 "per_file": { "docs/API.md": 15, "docs/AUDIT.md": 531, ... }
}
```

没有标题文本。检查器自己也印 `identity=per-file-count`。所以 `net=0` 与 `removed=0` 合起来的诚实读法是：**没有文件改变它二级标题的条数**。一个文件里把一个标题改名、或者删一个再加一个，计数一动不动，`net=0` 照旧绿，而「没有一节消失」这句话已经不成立了。检查器收尾那句「绿灯不说明幸存标题下的文字还说了同样的话」就是这个意思，只是它写在了最后一行，容易被当成客套。

结论要按域来说：`net=0` 对「计数」是充分的，对「身份」只是必要的；要守身份，pin 得存标题文本（`identity-domain` 里已经预留了 `per-heading-identity`，v1 只用了前者）。这不是缺陷报告，是**一条读数该被读到哪一行为止**。

### 18.4 命令（任何人可复跑）

```
cd <任意树>
python3 tools/check_docs_headings.py          # HEADINGS|tree / pin / stale / drift / added
python3 - <<'EOF'                              # 自己数一遍，不信任何一行
import pathlib
print(sum(sum(1 for l in p.read_text(encoding="utf-8", errors="replace").splitlines()
              if l.startswith("## ")) for p in pathlib.Path("docs").rglob("*.md")))
EOF
```

e3d2dc2 上的读数：`1352`（103 个文件有 L2，107 个在册，4 个为 0）；+4 的构成是 `docs/AUDIT.md` 535→537 与 `docs/streams/dec-formal-axioms.md` 16→18，其余 101 个文件不变，1348+2+2=1352。三级标题不进账，因为判据是 `startswith("## ")`，第三个字符必须是空格。

### 18.5 这一节不证明什么

不证明 pin 的正文身份（见 18.3），也不证明「绿 = 内容未变」。它能证明的只有一件事：**数目字对得上，且漂移能被拆到具体文件**——而这两件，比一个「pin-tree 等于 head」的等式有用得多。
