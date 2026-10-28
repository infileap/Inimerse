# IR 表的对抗性复核：读数、逐行对照、P1 的判据

复核对象：`tools/ir_spec.py`、`tools/ir_spec.test.py`、`docs/IR_DESIGN.md` 的 P0/P1 判据。
树：`426ceef`（main，本页所有行号都在这棵树上读过，逐行带 `[obs: 426ceef]`）；分支 `ir-review-noble-quartz`。
只读：未改 `src/`、`tools/ir_spec.py`、`tools/mod_spec.py`。探针全部落在 /tmp 的副本上，副本只改 `VM_C` 指向。
约定：**[实测]** 后面跟命令与读数行；**[推断]** 只有推理，没有运行。

## 0. 结论摘要

1. 你给的三个数全对，第四个也对：`IR-COVER|interp=67/67|disasm=67/67|baseline=disasm:0`、`IR-SPEC|ops=67|codes=0..67|hole=29`，我独立复算一致。68 的陷阱复现了，但它的构成比「一个幽灵成员」更具体：只剥行注释得 **69 次出现、68 个唯一名**，唯一的幽灵是 `OP_POP_REG`（块注释里那句 29 的历史），`OP_LOADK_INT` 不是新幽灵而是一次重复出现。
2. 表里有两行能被处理器体**当场证伪**：`OP_SET_ADD` 的形状写「无寄存器操作数」，处理器读 `R[r1]` 与 `R[r2]`；`OP_TRY_START` 的形状写「open try r1」，处理器读的是 `r2`，`r1` 由编译器写成常量 0，反汇编器也照 `r1` 印。另有一行不是错但会被误读：`OP_CALL_FUNC` 的**目的寄存器是 r2**，与其余行的「r1 = …」惯例相反。
3. 判据层面最要紧的一条：`IR-DISASM|named=67/67` 里的 named 数的是「有 case」，不是「印对了名字」；名字看不见时检查**跳过**而不是记一笔。三个探针（四处缺陷）实测全部绿，包括「ADD 印出 SUB」。
4. P1 的「逐字节等价」**不可达，除非先写死两条前提**：①层只做 relabelling 不做规范化（与 `docs/IR_DESIGN.md` 第 110 行「①层…只做规范化」直接冲突）；以及 SHAPE 从散文升级为结构化行——否则 MIR 构建器拿不到 dst/srcs，只能再写一套映射，那正是设计文档 §1.2 要杀的第二权威。

## 1. 我实测的读数

### 1.1 工具自己的三条读数 [实测]

```
$ python3 tools/ir_spec.py
IR-SPEC|ops=67|codes=0..67|hole=29|shaped=67|unshaped=none        [obs: 426ceef]
IR-ABI|version=3|abi=2|ops=67                                      [obs: 426ceef]
IR-RETIRED|code=29|name=OP_POP_REG|effect=never-issued             [obs: 426ceef]
IR-DISASM|named=67/67|wrong-name=none                              [obs: 426ceef]
IR-COVER|interp=67/67|disasm=67/67|missing=disasm:none|baseline=disasm:0|aot=na|wasm=na|selfhost=na   [obs: 426ceef]
RC=0
```

```
$ python3 tools/ir_spec.py --self-check
IRCHECK|case=unshaped|red=yes                                      [obs: 426ceef]
IRCHECK|case=stale|red=yes                                         [obs: 426ceef]
IRCHECK|case=baseline|red=yes|op=baseline-tightened-without-a-fix  [obs: 426ceef]
IRCHECK|case=coverage|red=yes|op=OP_MOV                            [obs: 426ceef]
IRCHECK|case=wrongname|red=yes|op=OP_MOV<-OP_LOADK_INT             [obs: 426ceef]
RC=0
```

```
$ python3 tools/ir_spec.test.py
ir spec: ok (67 opcodes, interp 67/67, disasm 67/67, names agree)
RC=0
```

### 1.2 独立复算（不 import 那张表）[实测]

另写一段脚本，只读 `src/compiler/bytecode.h` 与 `src/vm/vm.c`，用**花括号配对**而不是「切到第一个 default:」来定 dispatch 区域：

```
E1 enum: members=67 codes=0..67 holes=[29] dup_codes=0               [obs: 426ceef]
E2 naive: occurrences=69 unique=68 phantom=['OP_POP_REG']            [obs: 426ceef]
E3 dispatch: brace-matched cases=67, tool-slice cases=67, first default at offset +2713 from switch, nested defaults before it=0   [obs: 426ceef]
E4 goto targets: pairs=67 missing_labels=none                       [obs: 426ceef]
E5 disasm: cases=67 narrow_names=67 wide_format_strings=67 skipped_by_narrow=0   [obs: 426ceef]
E6 skipped example: none                                            [obs: 426ceef]
```

E1 与你给的 `ops=67|codes=0..67|hole=29` 一致；E3 说明今天「切到第一个 default:」没有截断（dispatch 内部在它之前没有嵌套 default），所以 `interp=67/67` 是真数，不是截断后少算；E5 说明今天的 67 个 case **全部**能被那套正则看见（`skipped_by_narrow=0`），所以「名字检查会跳过」目前是**潜在**缺口，不是当前漏检——探针之所以重要，是因为它证明这个缺口一碰就开。

关于 68 的口径 **[实测]**：仅剥 `//` 注释时，枚举体里 `OP_` 出现 69 次、唯一名 68 个。多出来的那一个名字是 `OP_POP_REG`，它出现在第 20 行起的块注释里（「29 is a hole: OP_POP_REG used to sit here」）；`OP_LOADK_INT` 则是第 60 行块注释里的重复出现（同一行族还提到 `OP_LOADK_I64` 的编码理由）。所以「数出 68」的读法是：**唯一名数**，而「成员数」是 67——两个不同的对象被同一个词「op 数」覆盖了。表自己的 `declared_ops()` 先把块注释整体删掉再数，因此对，且它在 docstring 里写明了这个坑。

## 2. SHAPE 与处理器体：逐行对照 [实测]

抽查 12 条（覆盖算术、常量、容器、调用、线程、控制）。判定列的行号引用都带 `[obs: 426ceef]`。

| opcode | 表里的形状（`tools/ir_spec.py` 行） | 处理器体（`src/vm/vm.c` 行） | 判定 |
| --- | --- | --- | --- |
| OP_MOV | r1 = r2，第 78 行 | value_assign(&R[ins.r1], &R[ins.r2])，第 3237 行 | 证实 `[obs: 426ceef]` |
| OP_LOADK_INT | r1 = int32(r2)，第 79 行 | Value v = { .ival = ins.r2 } 写入 R[ins.r1]，第 3240 行 | 证实 `[obs: 426ceef]` |
| OP_LOADK_I64 | r1 = r2 低半 ∥ r3 高半（无符号），第 83 行 | (unsigned)ins.r2 ∥ (unsigned)ins.r3 << 32，第 3247-3250 行 | 证实 `[obs: 426ceef]` |
| OP_ADD | r1 = r2 + r3，第 84 行 | a=&R[r2], b=&R[r3]，写 R[r1]，第 3277 行 | 结构证实（另按类型分派 set/string） `[obs: 426ceef]` |
| OP_MOD | r1 = r2 %% r3，第 88 行 | ia %% ib 写 R[r1]，第 4267 行 | 证实 `[obs: 426ceef]` |
| OP_AND | r1 = truth(r2) and truth(r3)，第 96 行 | a=vm_truthy(R[r2]), b=vm_truthy(R[r3])，第 3599-3602 行 | 证实（「short-circuit 在前端」是表外声明，这里不可测） `[obs: 426ceef]` |
| OP_INDEX_SET | r1[r2] = r3，第 110 行 | obj=&R[r1], idxv=&R[r2], valv=&R[r3]，第 3718-3720 行 | 证实 `[obs: 426ceef]` |
| OP_MIN | r1 = min(r2)，第 107 行 | src=R[r2]，set_minmax 先清 dst 再写 R[r1]，第 4358-4359 行与第 2477 行 | 证实（r1 不是累加器） `[obs: 426ceef]` |
| OP_THREAD_STATE | r1 = state(thread r2, attr r3)，第 137 行 | tidx=ins.r2, prop=ins.r3，写 R[r1]，第 4693 行 | 证实 `[obs: 426ceef]` |
| OP_CALL_FUNC | `r2 = func[r1](argc=r3)`，第 121 行 | fidx=ins.r1, res=ins.r2, argc=ins.r3，第 4062 行 | 证实，但**目的寄存器是 r2**，破坏「r1 = …」惯例 `[obs: 426ceef]` |
| OP_DECLARE | -，第 115 行 | kind=ins.r1, float_pool[ins.r2]，第 4193-4194 行 | 半证实：确有两个**立即数**，`-` 只在「无寄存器操作数」这一读法下成立 `[obs: 426ceef]` |
| OP_SAY | say r1，第 128 行 | value_to_string(&R[ins.r1])，第 3923 行 | 证实 `[obs: 426ceef]` |
| OP_SET_ADD | -（set insert），第 105 行 | `R[ins.r1]` 是集合、`R[ins.r2]` 是元素，第 4454-4455 行 | **证伪**：形状声称无寄存器操作数，处理器读两个 `[obs: 426ceef]` |
| OP_TRY_START | -（open try r1），第 142 行 | `f->var_idx = ins.r2`，全程不读 `ins.r1`，第 4440 行 | **证伪**：操作数在 r2，形状写 r1 `[obs: 426ceef]` |

### 2.1 两处真证伪的最小复现

**(a) OP_SET_ADD 不是「无操作数」**。第 76 行给 `-` 的定义是「no register operands at all」；而处理器第 4454-4455 行读 `R[ins.r1]`（集合）与 `R[ins.r2]`（元素），编译器第 605 行 `emit(..., OP_SET_ADD, result, xReg, 0)` 也确实填了两个寄存器，反汇编第 5398 行还把它们印出来。表里的 `-` 与三处代码冲突。

**(b) OP_TRY_START 的操作数写错了一个位置**。编译器第 2292 行 `emit(..., OP_TRY_START, 0, varIdx, 0)`：r1 恒为 0，r2 才是变量下标；处理器第 4440 行取 `ins.r2`，第 4420-4439 行完全不读 `ins.r1`；反汇编第 5372 行却按 `ins->r1` 印 `TRY_START r%d`，于是反汇编器对这条指令**永远印 r0**——一个「有正确答案的形状」的空号。表里的 `(open try r1)` 是这个错的第三次复制。

这两行说明的是一件事：形状是**手写的散文**，而唯一被测量的东西是「这个名字是不是枚举成员」。散文与处理器体之间没有任何一格在比对，所以这两行今天全绿。

## 3. 名字启发式能被合法写法骗过 [实测]

`_disasm_names()` 的判据是：在 case 到下一个 case 之间，找 `snprintf(out, outsz, "` 后面第一段 `[A-Za-z0-9_]`（第 184 行），拿它当「这条指令印的名字」。看不见就 `continue`（第 246-248 行）——跳过，而不是记一笔。而 `IR-DISASM|named=` 数的是「case 在不在」（第 256-257 行），不是「名字对不对」。

探针：把 `tools/ir_spec.py` 复制到 `/tmp/probe/t_names.py`，只把 `VM_C` 指向 `/tmp/probe/vm_doctored.c`（`src/` 未改），在副本里做两处**合法**改动：

    case OP_ADD:  snprintf(out, outsz, "%s r%d, r%d, r%d", "SUB", ins->r1, ins->r2, ins->r3); return;
    case OP_MOV:  { const char *nm = "MOV"; snprintf(out, outsz, "%s r%d, r%d", nm, ins->r1, ins->r2); return; }

第一处让 ADD **印出 SUB**（错答案）；第二处让 MOV 用变量印名字（正确答案，但正则看不见）。读数：

```
$ python3 /tmp/probe/t_names.py            # rc 0
IR-SPEC|ops=67|codes=0..67|hole=29|shaped=67|unshaped=none
IR-DISASM|named=67/67|wrong-name=none
IR-COVER|interp=67/67|disasm=67/67|missing=disasm:none|baseline=disasm:0|aot=na|wasm=na|selfhost=na
```

**[实测]** 工具对一个「印错名字」的反汇编器报 `wrong-name=none`、退出码 0。第二个探针证明的是同一机制的另一面：合法写法（变量、`%s` 占位）让**正确的名字**也看不见——所以 `named=67/67` 不能读成「67 个名字都被核对过」，只能读成「67 个 case 存在」。

同族还有一处合法 C 会静默丢名字：fallthrough（`case OP_A:` 紧接 `case OP_B:` 共用一个 `snprintf`）时，A 的区间是空的，A 没有名字，检查跳过；B 拿到名字。**[推断]** 我没有构造这个探针，机制与上面相同（区间为空 → 正则不命中 → 跳过）。

修复方向（不动语义、只动读数）：把 `named=` 拆成 `cases=`（有 case）与 `names-checked=`/`names-unreadable=`（看得见/看不见），**unreadable 必须逐个点名并且红**；或者要求 `snprintf` 的第一参数就是字面量名字，否则该 case 记 unreadable。两条都遵守「一个名字一个含义、role 在前」。

## 4. `--self-check` 的五格漏掉「表对、代码错」 [实测]

五格（第 277-304 行）全部作用在**输入侧**：删 SHAPE 一行、加一行假形状、把 `BASELINE_MISSING` 调小、从 `meas` 集合里删一个 op、互换 `disasm_names` 的两个值。**没有一格改源文件文本**，因此它们证明的是「表与测量值不一致会被抓」，而不是「表与处理器体不一致会被抓」。

探针 C（表错、代码对）：把 `SHAPE["OP_CALL_FUNC"]` 反写成 `r1 = func[r2](argc=r3)`（与第 4062 行相反）后运行——**[实测]** `rc 0`，`IR-SPEC|ops=67|...|unshaped=none`，一个字都不说。

探针 D（基准自己填）：把 `BASELINE_MISSING` 改成 `{"disasm": 1}`，同时删掉副本里 `OP_YIELD` 的 case——**[实测]** `rc 0`，并且它自己印了：

```
IR-UNIMPLEMENTED|consumer=disasm|op=OP_YIELD|effect=prints-a-number-instead-of-a-name
IR-DISASM|named=66/67|wrong-name=none
IR-COVER|interp=67/67|disasm=66/67|missing=disasm:OP_YIELD|baseline=disasm:1|aot=na|wasm=na|selfhost=na
```

也就是说：`missing=disasm:OP_YIELD` 被印出来了、被点名了，但**没有拒绝**。「只许缩小」这条不变量由填这个数的人保证；今天 `baseline=0` 所以它是硬的（删一个 case 就红，第 283-295 行的 coverage 格证明了这一点），但机制本身不自证。这与前几轮的结论同族：**被检验方自己填的字段，不能当作关于被检验方的证据**。

还有一处小的：`tools/ir_spec.py` 第 42 行的读数形状写 `IR-DISASM|named=68/68`，`tools/ir_spec.test.py` 第 9 行写 `the 68 opcodes`，今天的读数是 67/67。两处都是旧读数——正是「数字没有主语」那一类：它们写在散文里，没有一个运行会让它们变红。

## 5. P1 的判据：可证伪的形状

### 5.1 拿什么比 [实测 + 推断]

仓库里已经有为此准备的出口：`bytecode <input.im>` 子命令（`src/main.c` 第 685-717 行）以及它的打印器 `dump_bc_section`（第 678-683 行）：先印一个段头，然后每条指令一行 `op,r1,r2,r3`。注释第 673-677 行明说它「kept byte-comparable on purpose」，是为了让 C 编译器与自举编译器的产物可以互相 diff 与哈希。

**不要**只用 `src/main.c` 第 681 行那一条 `printf`：它就是 `dump_bc_section` 的行，本身没有独立信息。更要紧的是 dump 的**覆盖面**：它只印 `main:`、`func:<name>:<argc>`、`thread:<name>:<argc>` 三段**码流**，不印 string_pool / float_pool / global 名表 / try_entries。所以「逐字节等价」要分两层比：

1. **码流层**：同一份 `.im`，旧路径与新路径各 `bytecode` 一次，`diff` 必须为空。读数 `ORIG-IDENTICAL|files=N|sections=S|bytes=B|differ=0`；失败时 `ORIG-DIFF|file=…|section=…|line=…|want=…|got=…`（点名到段与行，不许只印个数）。
2. **容器层**：同一份 `.im` 各 `buildc` 成 `.inim` 后 `cmp`——覆盖 dump 看不见的池、全局名表、try_entries、以及编号表本身。注意 `.inim` 带一条依赖尾巴（`src/main.c` 第 719-731 行注释：主源与每个 import 的 SHA-256），所以这条比较只在**同一棵树、同一 flags、同一绝对路径**下有效；跨树比字节会把「路径不同」读成「编译不同」。

### 5.2 比多少样本 [实测]

全量，不抽样：仓库内 `*.im` 共 **399** 个；已提交的 `*.inim` 共 **9** 个，它们必须仍然 `load + run`（这是 ABI 那一侧的判据，和字节等价是两件事，别混进 `differ=`）。`ORIG-IDENTICAL|files=` 必须等于语料数，少一个就要点名哪个文件没跑到——「没跑到」不等于「一样」（这是 T13 那一轮的教训：不可测必须印成 `unmeasured`，不能默认成 `same`）。

### 5.3 什么算失败

- 码流层任何一行不同、容器层任何一字节不同，即失败；失败必须点名 `file` + `section`（+ `line`）。
- 一侧编译失败/超时：记 `unmeasured`，**不**记 `same`，并且失败行要说明是哪一侧失败。
- 负控（必须红）：故意把两条互相独立的指令交换顺序，`differ` 必须 > 0 且点名的行号正是那两条所在段。
- 反证（必须绿）：把比较改成「只比退出码」，同一份被交换的语料必须变绿——证明这条判据真的在读字节，而不是在读信号。这一条与 §9.3 里「把差分改成只比 exit code ⇒ 必须变绿」是同一个口径。

### 5.4 「逐字节等价」可达吗：不可达，除非先写死前提 [推断]

`docs/IR_DESIGN.md` 第 110 行的规则 1 说「①层与语义无关，**只做规范化**」，第 374 行（P1 行）要求「字节码**逐字节等价**」。这两条直接冲突：规范化（常量折叠、比较归一、指令下沉/合并）就是会改字节的东西。第 155 行又明确「不做 SSA」，说明 P1 的 MIR 是寄存器式、可以不重排——但那是一个**决定**，不是一个**事实**。

所以判据要写成有前提的形状：

- 若 P1 的 lowering 是**保结构的重标号**（同序遍历、不折叠、不重排、寄存器编号沿用），字节等价可达，并且是最强判据（它连「同义但不同编码」都抓得住）。这时 §10 第 110 行要改成「①层在 P1 只做重标号，规范化留到 P2 之后」。
- 若允许规范化，字节等价**不可达**，替代判据只能是差分：同一语料两端跑出相同 stdout + exit code（逐例点名），加上按基本块的**语义等价**而不是行等价。这时必须在设计文档里写明「字节等价已放弃，理由是规范化 X」，否则这条判据会为了一个合法改动而红，然后被绕过。

### 5.5 P1 的真正前置：行必须是结构化的 [实测]

MIR 构建器需要每行的 **dst / srcs / 是否读 dst / 是否终止**。今天的 SHAPE 是散文：

- 67 行里只有 **41** 行是 `rN = …` 形式，其余 **26** 行是 `ip = r2`、`G[r1] = r2`、`return r1`、`thread[r1] = start(…)`、`- (set insert)` 这些形状；
- 就是那 41 行里也有陷阱：`OP_CALL_FUNC` / `OP_CALL_VALUE` 的**目的寄存器是 r2**，任何「左边那个 rN 就是 dst」的通用解析会在这两行上静默反写；
- `OP_TRY_START` 是自己和自己矛盾的那一行（字段 `-`，括号里写 r1，代码用 r2）。

因此：如果 P1 直接拿这张表去构建 MIR，它只能再写一套 `op → 角色` 的映射——那就是设计文档 §1.2 命名的病：同一语义第二处副本。可证伪的判据是三条：

1. SHAPE 每一行都有结构化字段（`dst=`、`srcs=`、`reads_dst=`、`terminator=`），覆盖 67/67；今天的散文列由这些字段**生成**，不是手写；
2. 改一条生成出来的散文而不改生成器 ⇒ 必须红（证明散文不是第二权威）；
3. MIR 构建器里 `op → 角色` 的映射表数量 = 0（`grep` 计数），它只能查表。

这三条比「字节等价」更早，因为字节等价是 P1 的**出口**判据，而这三条是 P1 的**入口**判据。

## 6. 复现（全部只读；探针在 /tmp）

```
git -C /home/sakiko/inimerse worktree add -b ir-review-noble-quartz /tmp/wt-ir 426ceef
cd /tmp/wt-ir && python3 tools/ir_spec.py && python3 tools/ir_spec.py --self-check && python3 tools/ir_spec.test.py
python3 - <<'PY'   # 独立复算：枚举成员/编号/洞、花括号配对 dispatch、disasm case 与名字   [见 §1.2]
PY
python3 /tmp/probe/t_names.py      # 探针 A+B：印错名字与看不见的名字
python3 /tmp/probe/t_shape.py      # 探针 C：SHAPE 行反写
python3 /tmp/probe/t_base.py       # 探针 D：基准放宽 + 删一个 case
```

探针脚本与两份 doctored `vm.c` 都在 `/tmp/probe/`（`t_names.py`、`t_shape.py`、`t_base.py`、`vm_doctored.c`、`vm_lost.c`），生成脚本一次性写出，只改 `ROOT` 与 `VM_C` 两个绑定。

## 7. 没做的 / 不适用

- **[实测]** 本机没有已构建的引擎二进制（`find` 未找到 `inim`，仓库里只有 `build*.ps1`），所以 §5.1/§5.2 的采样是**形状建议**，不是实测读数；`ORIG-IDENTICAL|files=399` 里那个 399 是语料数（实测），不是跑过的数。
- `aot` / `wasm` / `selfhost` 三列 P0 声明为 `na`，未复核——表自己说它们到 P3/P4/P6 才可测，这一条我认可。
- `tools/mod_spec.py` 不在我的复核范围（分工外），未读、未跑。
- 我没有动 `docs/streams/dec-formal-axioms.md`，也没有增加指针预算：本页所有行号引用都是 record（行上带 `[obs: 426ceef]`）。

## 8. 一句话

`named=` 点的是 case，不是名字；看不见的名字被跳过，而「跳过」在读法上等于「通过」。所以 P0 交付的是**覆盖与命名的判据**，不是 P1 需要的**语义表**；两者之间缺的不是代码，是行的形状。
