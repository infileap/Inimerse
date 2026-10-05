# Inimerse 0.5.1

0.5.1 是一个**修检查**的版本。它不改语言、不改运行时语义，改的是一族同形状的缺陷：

> **一个检查断言了「结论」，却没有断言「分母」。**

这类缺陷的共同点是**绿得看不出来**——不是检查失败，是检查从来没有跑过它以为跑过的东西。0.5.0 自己的发布门禁就是受害者之一（见下）。

## 基线

| 项 | 值 |
|---|---|
| 版本 | 0.5.1 |
| 测试 | Linux **139 / 139 真通过**（本地十二阶段门禁，0 跳过）；Windows `Total Tests: 129`，运行 120，**0 失败**，2 按设计跳过，9 按裁定 DISABLED |
| 相对 `v0.5.0` | 该 tag 之后的 **50** 个提交（`git rev-list --count v0.5.0..v0.5.1`） |

## 修了什么

**五处同一个形状**，加一处同族的语法错误；第 6 条出现在本轮新增的断言自己身上：

1. **发布门禁只跑了 24 / 123。** `ctest -N` 的编号是**右对齐**的（`Test   #1:` 三个空格，`Test #100:` 一个），而抽取脚本要求恰好一个空格，于是只有三位数编号的测试被收集：`release.yml` 收了 24 / 123，`linux-build.yml` 收了 37 / 136 —— **两个工作流都报成功并照常打包**。修法不是「换个更聪明的 sed」，而是抽出 `tools/ctest_enumerate.sh`，并断言 `collected == Total Tests`：模式可以再写错，断言不会。

2. **模糊测试 pin 了分歧数，没 pin 跑过。** `tools/gate.sh` 只断言 `DIVERGE = 0` / `THREW = 0` —— 而一个程序都没跑时这两个计数**同样是 0**，空跑与干净跑打印同一行绿。现在种子集显式固定（`EXP_FUZZ_SEEDS` 默认 `1 2 3`），逐种子断言程序数等于 `EXP_FUZZ_COUNT`，四个分桶（agreed + DIVERGE + THREW + untranslated）之和必须闭合，种子集为空直接红，pin 在累计总数上判。

3. **三处文档声称有 CTest 覆盖，而没有任何东西跑它。** `docs/API.md` 的 `case` 两行把 **fixture 文件名**填在「证据（CTest）」列；`docs/REQUIREMENTS_ANALYSIS.md` 与 `docs/STATUS.md` 把 `tools/migrate_report.py` 与 `bindgen_regression` / `scan_tools_regression` 并列当证据，而那个工具**没有任何测试**。现在：两个 `case` fixture 各注册一条 CTest（各带一条**指向对方警告**的 `FAIL_REGULAR_EXPRESSION`），`migrate_report_runtime` 断言**分母**（每条规则必须触发；表头计数必须等于表体行数——把表头改成常量 `0` 而表体仍是满表，正是它抓的东西），`--desugar` 唯一的证据从**零引用**的 shell 探针换成 `tools/desugar.test.py`。

4. **插件验证器验的是「另一个」checkout。** `tools/dsh-inimerse/verify.mjs` 从一份**提交进仓库的**配置里读出 `repoRoot` 并据此验证，于是在任何其他 worktree 或干净克隆里，它验的都是 `/home/sakiko/inimerse` 那棵树。现在锚定在**验证器自己所在的 checkout**；配置里那个路径只在不一致时打一条 `note:`。

5. **两处 lint fixture 用了 `=`，而 `be` 只接受 `:`。** `dir be Direction = "N"` 是解析错误——`be` 走 `src/parser/parser.c:1383-1391`（只认 `:`），`type X = …` 是另一个语句 `parse_type_stmt`（`:1218-1227`，只认 `=`），**两条语法、两个分隔符**。这两个 fixture 只被 `--lint` 跑，而 **`--lint` 不打印解析错误** ⇒ CTest 匹着 warning 绿着，**而 fixture 描述的程序从来没有被解析过**。修完才有「没有 fixture 会吐解析错误」这个不变量可以断言（`fixture_parse_runtime`：扫描 67 个 `vtest/*.im`，断言吐解析错误的个数为 0，并**同时打印两个数**，`MIN_FIXTURES = 50` 防「0 of 0 读作干净」）。

顺带更正 `tools/README.md` 里的阶段表：它写着 `Seven stages` 并只列了 7 个，而门禁实际有 **12** 个阶段。

6. **同一族的第六处，出在本轮新增的那条分母断言自己身上。** `tools/migrate_report.test.py` 用 `text=True` 起子进程却没给 `encoding=`，于是 Windows 上 Python 用本地编码（gbk）解 UTF-8 报告，在 `subprocess` 的读线程里抛 `UnicodeDecodeError`，测试最后以一个 `NoneType` 的 `TypeError` 收场——**又一条不具名的红**。它只在这条流水线的 Linux 侧跑过；这与 0.5.0 的 `100d3b1`（同样修法，33 处）是同一个教训：**只在一侧跑过的检查，不知道另一侧会怎样**。修后 Windows 上 `#128 migrate_report_runtime` 0.88 s 通过。

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
