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
