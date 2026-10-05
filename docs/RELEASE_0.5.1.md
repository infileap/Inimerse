# Inimerse 0.5.1

0.5.1 是一个**修检查**的版本。它不改语言、不改运行时语义，改的是一族同形状的缺陷：

> **一个检查断言了「结论」，却没有断言「分母」。**

这类缺陷的共同点是**绿得看不出来**——不是检查失败，是检查从来没有跑过它以为跑过的东西。0.5.0 自己的发布门禁就是受害者之一（见下）。

## 基线

| 项 | 值 |
|---|---|
| 版本 | 0.5.1 |
| 测试 | 发布点 `v0.5.1` → `4e444dd` 上：Linux **148 / 148 真通过**（本地十二阶段门禁，0 跳过）。本版「修检查」的工作是在 `release/051-final` @ `c71ea00` 上做的，**那棵树上是 142 / 142**。**两个数各自属于各自的树、并列而不互相取代**（`git rev-parse --short 'v0.5.1^{commit}'` 与 `git rev-parse --short c71ea00` 各自可复核；142 → 148 的差是语言迁移落地后重数出来的）。Windows `Total Tests: 131`，运行 122，**0 失败**，2 按设计跳过，9 按裁定 DISABLED（该读数的出处与身份见 `docs/AUDIT.md` §1.65） |
| 相对 `v0.5.0` | 该 tag 之后的 **67** 个提交（`git rev-list --count v0.5.0..v0.5.1`） |

## 修了什么

**五处同一个形状**，加一处同族的语法错误；第 6 条出现在本轮新增的断言自己身上。第 7～9 条不在这一族里（一处是**守卫自己的算术溢出**，一处是**字面量走了不该走的路**，一处是**静默截断**），但都是本版修掉的：

1. **发布门禁只跑了 24 / 123。** `ctest -N` 的编号是**右对齐**的（`Test   #1:` 三个空格，`Test #100:` 一个），而抽取脚本要求恰好一个空格，于是只有三位数编号的测试被收集：`release.yml` 收了 24 / 123，`linux-build.yml` 收了 38 / 137（这个洞被发现时它在 134 个测试的树上是 35 / 134）—— **两个工作流都报成功并照常打包**。修法不是「换个更聪明的 sed」，而是抽出 `tools/ctest_enumerate.sh`，并断言 `collected == Total Tests`：模式可以再写错，断言不会。

2. **模糊测试 pin 了分歧数，没 pin 跑过。** `tools/gate.sh` 只断言 `DIVERGE = 0` / `THREW = 0` —— 而一个程序都没跑时这两个计数**同样是 0**，空跑与干净跑打印同一行绿。现在种子集显式固定（`EXP_FUZZ_SEEDS` 默认 `1 2 3`），逐种子断言程序数等于 `EXP_FUZZ_COUNT`，四个分桶（agreed + DIVERGE + THREW + untranslated）之和必须闭合，种子集为空直接红，pin 在累计总数上判。

3. **三处文档声称有 CTest 覆盖，而没有任何东西跑它。** `docs/API.md` 的 `case` 两行把 **fixture 文件名**填在「证据（CTest）」列；`docs/REQUIREMENTS_ANALYSIS.md` 与 `docs/STATUS.md` 把 `tools/migrate_report.py` 与 `bindgen_regression` / `scan_tools_regression` 并列当证据，而那个工具**没有任何测试**。现在：两个 `case` fixture 各注册一条 CTest（各带一条**指向对方警告**的 `FAIL_REGULAR_EXPRESSION`），`migrate_report_runtime` 断言**分母**（每条规则必须触发；表头计数必须等于表体行数——把表头改成常量 `0` 而表体仍是满表，正是它抓的东西），`--desugar` 唯一的证据从**零引用**的 shell 探针换成 `tools/desugar.test.py`。

4. **插件验证器验的是「另一个」checkout。** `tools/dsh-inimerse/verify.mjs` 从一份**提交进仓库的**配置里读出 `repoRoot` 并据此验证，于是在任何其他 worktree 或干净克隆里，它验的都是 `/home/sakiko/inimerse` 那棵树。现在锚定在**验证器自己所在的 checkout**；配置里那个路径只在不一致时打一条 `note:`。

5. **两处 lint fixture 用了 `=`，而 `be` 只接受 `:`。** `dir be Direction = "N"` 是解析错误——`be` 走 `src/parser/parser.c:1383-1391`（只认 `:`），`type X = …` 是另一个语句 `parse_type_stmt`（`:1218-1227`，只认 `=`），**两条语法、两个分隔符**。这两个 fixture 只被 `--lint` 跑，而 **`--lint` 不打印解析错误** ⇒ CTest 匹着 warning 绿着，**而 fixture 描述的程序从来没有被解析过**。修完才有「没有 fixture 会吐解析错误」这个不变量可以断言（`fixture_parse_runtime`：扫描 67 个 `vtest/*.im`，断言吐解析错误的个数为 0，并**同时打印两个数**，`MIN_FIXTURES = 50` 防「0 of 0 读作干净」）。

顺带更正 `tools/README.md` 里的阶段表：它写着 `Seven stages` 并只列了 7 个，而门禁实际有 **12** 个阶段。

6. **同一族的第六处，出在本轮新增的那条分母断言自己身上。** `tools/migrate_report.test.py` 用 `text=True` 起子进程却没给 `encoding=`，于是 Windows 上 Python 用本地编码（gbk）解 UTF-8 报告，在 `subprocess` 的读线程里抛 `UnicodeDecodeError`，测试最后以一个 `NoneType` 的 `TypeError` 收场——**又一条不具名的红**。它只在这条流水线的 Linux 侧跑过；这与 0.5.0 的 `100d3b1`（同样修法，33 处）是同一个教训：**只在一侧跑过的检查，不知道另一侧会怎样**。修后 Windows 上 `#128 migrate_report_runtime` 0.88 s 通过。

7. **`substr` 的钳位算术自己溢出，于是守卫失效。** 两侧的守卫原本写成 `if (start + len > sl) len = sl - start;`，而 `start + len` 在 `int` 里相加会溢出成负数，比较恒假、`len` 保持约 2³¹，随后按这个长度 `memcpy`。实测 `substr("abcdefghij", 5, 2147483647)` **段错误、rc=139**；阈值恰好落在 `INT_MAX`（`len=2147483642` 相加不溢出 ⇒ 正常返回 `fghij`，`len=2147483643` 溢出 ⇒ 崩溃），翻转点与溢出点逐字吻合。两侧现在用同一条不做加法的规则（`if (len > sl - start) len = sl - start;`），Windows 那份还补齐了缺失的分配失败检查（POSIX 有、Windows 没有，同一处的第二个不对称）。新测试 `substr_boundary_runtime` 用**三个刚好跨 `INT_MAX` 的长度**钉住：2147483642 / 2147483647 / 0 与 2147483647 都必须得到同样的答案，所以「一律钳到 0」的假修法也过不去。

8. **字面量主机名也在付完整解析器的钱（Windows 特有的超时）。** `socket_probe` 在 Windows 上偶发 `***Timeout 11.31 sec`（CTest 默认上限 10 s），而它自己 20 次串行重跑的用时是 151–8564 ms。逐调用计时驱动量出的机制是：`im_socket_init` 稳定 1–4 ms，而**每一个走名字解析的调用**是秒级（`listen` 3662 / 895 / 902 / 1029 / 2265 ms，`port_available` 到 1478，`connect` 到 1542，`port_open` 到 2116）——`resolve_addr_timed` 把 `"127.0.0.1"` 这种**字面量**也丢进完整解析器并起线程，而探针每轮做四次解析。修法是字面量短路（`InetPtonA` / `inet_pton`，两个解析入口都接），不是把上限调大：改后同一驱动整轮约 5 ms。新测试 `literal_resolve_runtime` 在 POSIX 上用 `LD_PRELOAD` **数 `getaddrinfo` 调用次数**，而不是量毫秒——Linux 上两条路径都是微秒级，计时断言是盲的。

9. **POSIX 的 `match` 把一个超长模式截断后再运行，给出了一个形状正确的错答案。** `src/runtime/runtime_posix.c:131` 用 `char translated[2048]` 装翻译后的模式，复制循环的条件是 `j + 16 < sizeof translated`，于是最多抄 2031 字节、**超出部分直接丢掉且无诊断**，再拿截断后的串去 `regcomp`。丢掉的那半句如果是最关键的约束，答案就反了：`match(s, "^" + 2030 个 a + "$zzz")` —— 一个**永不可能匹配**的模式 —— 返回 `true`（2028/2029 是正确的 `false`，翻转点恰好是溢出点）。改用按 `strlen(pattern)` 动态分配（16 倍是 `\w` → `[[:alnum:]_]` 展开的**上界估计**），任何路径都不再静默截断；分配失败返回 `false` —— 对本来该匹配的超长模式那是**错答案，只是不再沉默**。缺陷是 **POSIX-only**：`src/runtime/runtime.c:986` 从来没有这个定长缓冲，所以这条测试**跨平台注册**，把「Windows 侧本来就是对的」也钉住。

   这条的断言自己也被修了一次，而且第一个修法**被实测证伪**：注释承诺「恒答 false 的假修法过不了」，而 `PASS_REGULAR_EXPRESSION` 只命名了 `impossible` 那一半。把断言写成 `;` 分隔的两项**看起来**补上了 —— 但 CMake 的 `PASS_REGULAR_EXPRESSION` 是列表内**任一命中即通过（或，不是与）**，一个恒 `push_bool(vm, 0)` 的假修法实测**照样 `100% passed`**。落地的形态是 `PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"` + `FAIL_REGULAR_EXPRESSION "match-long [0-9]+ possible=false"`（`FAIL_` 命中即红）。两方向都走 CTest 本身：真修法 `100% passed`，假修法 `***Failed ... Regex=[match-long [0-9]+ possible=false]`。细节见 `docs/STATUS.md` §10.97 / §10.98。

## 合并进来的内建契约

同期合并的 **11 条内建契约 pin** 把两侧运行时的现行值钉死，并落了一条通用规则（D14）：**共享状态的实参类型错误抛 `type_mismatch`，产值的返回裁定值**。另有注册表守卫：同一个内建名注册两次会被拒绝并打印 `[vm] builtin 'X' is already registered; the first one stays`（此前后一次注册**不可达**，而计数仍然算它）。

门禁新增两个阶段：`text-integrity`（受版本控制的文本文件不含 NUL 字节）与 `orphan-fixtures`（每个测试输入都必须真的被跑到——一个没有任何东西跑的 fixture **既不能失败也不能通过：它不是证据**）。

## 平台

- **Windows**：与 0.5.0 相同——hub 为减配版本，**9 个 POSIX hub 回归测试显式 `DISABLED`**（按维护者裁定，移植未做），两个 xlang 桥（Java / Python）按设计以退出码 77 跳过，其余全部运行。**没有任何东西被静默跳过**：跳过是显式的，本地门禁还会数 `***Skipped`，非零即红。
- **Linux**：完整功能，十二阶段门禁全绿。

## 资产

| 平台 | 资产 |
|---|---|
| Linux | `inimerse-0.5.1-Linux-x86_64.tar.gz`、`inimerse-0.5.1-Linux-x86_64.zip`、`inimerse-0.5.1-Linux-x86_64.deb`、`SHA256SUMS` |
| Windows | `inimerse-0.5.1-Windows-x86_64.zip`（含 `bin/inimerse.exe`、`bin/aot-native.exe`、`bin/inim.py`）、`InfiverseSetup-0.5.1.exe`（Inno Setup 7 装机包）、`SHA256SUMS-Windows` |

`SHA256SUMS` 与 `SHA256SUMS-Windows` 里都是**裸文件名**，在下载目录里 `sha256sum -c` 可直接校验。

## 诚实边界

- 分母断言证明的是**与 `ctest -N` 一致**，不是「跑的是对的那些测试」。
- 模糊测试的分母**不是覆盖率**：种子集固定之后，「这几个种子全 0 分歧」仍然只是这几个种子。
- 文档里点名了测试，**不改变那些行的实现状态**（`--lint` 的 `case` 覆盖仍是部分实现）。
- `migrate_report_runtime` 断言的是那个工具**自己的分母**，不是它的迁移正确性。
- `fixture_parse_runtime` 的 `MIN_FIXTURES`/`ALLOWED` 只能防「空扫描」与「白名单退化成豁免名单」，**不能**证明每个 fixture 都断言了有意义的东西。
- `substr_boundary_runtime` 钉的是**跨 `INT_MAX` 的那三个长度**，不证明其他实参组合都对；它断言的是「不再崩」，不是「语义已穷尽验证」。
- `match_long_pattern_runtime` 的 `FAIL_REGULAR_EXPRESSION` 钉的是「没有任何一行报 `possible=false`」，它看不到那两行是否**同一个长度**、也不数 `possible=true` 出现几次；`match-long` 这个前缀是断言与 fixture 之间的**隐式契约**。
- - `literal_resolve_runtime` 数的是 **`getaddrinfo` 调用次数**，所以它钉的是「字面量没走解析器」，**不是**「快了多少」；Windows 上「整轮降到约 5 ms」来自本机自己的计时驱动，不是 CTest 断言（Linux 上两条路径都是微秒级，计时断言在那里是盲的）。修后 Windows 全量跑一次绿，不等于超时的分布已经消失。
