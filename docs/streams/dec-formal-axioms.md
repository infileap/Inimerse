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
Windows 路径——WSL 的 `python3` 没有 z3，这一点从 T1 起没变。

退出码：`0` 台账完整（含"变异列未跑"的诚实降级，见 §3 第 3 条）；`2` 连生成器/树都读不到。

逐字输出（45 行，树 `f046f20`）存在 `docs/streams/dec-formal-axioms.readings.txt`，
下面是它的首行与末行，用来核对"读的是同一个东西"：

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


