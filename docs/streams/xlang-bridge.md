# 作业单：`xlang-bridge` —— 让 Python / Java 桥接从「生成器」变成「真产物」

> **冲突域见 §3。本条流独占 `CMakeLists.txt`** —— 另外两条在飞的流（`wasm-simd-gc`、`aot-backend`）
> 被明确要求**不碰它**，所以你是这一波唯一能新增源文件与 CTest 的人。请珍惜这个独占期：
> 你要的东西加完就尽快交回，别把它当长期持有。

---

## 1. 现状（开工前请自己复核一遍，不要采信本段）

`docs/STATUS.md` §3.1 的「**声称但未交付（设计未实现）**」表里有五条，其中**前两条**属于本条流：

| `docs/archive/RELEASE_0.5.0.md` 声称 | 核实结果 |
| --- | --- |
| 「Python extension bridge: `inimerse_extension.c` implementing `PyInit_inimerse()`」（第 26 行） | 仓库中**不存在** `inimerse_extension.c`，全仓**无** `PyInit_inimerse` 实现 |
| 「Java native bridge: `InimerseBridge.java`」（第 27 行） | 仓库中**不存在** `InimerseBridge.java` |
| 发布产物 `.whl` / `.jar`（第 123–130 行） | 仓库中**不存在**任何 `.whl` / `.jar` 产物 |

**但生成器是有的、而且是被测过的** —— 这一点决定了本条流的形状：

- `tools/bindgen.py`：一份 `.def` 驱动 C/C++/Java/Python 四语言绑定生成，`--language {c,java,python}`、
  `--out <dir>`。文件头自述「Unsupported constructs (overloads, default parameters, callbacks, async)
  are rejected with a precise error so types are checked at generation time, not at link time」，
  错误约定是「C: int return；Java: `InimerseException`；Python: `InimerseError`」。
- `examples/interface.def`、`examples/build.gradle`、`examples/pom.xml` 都在。
- `examples/python_bridge.im` 与 `examples/java_bridge.im` **只是一句 `say "see …"`**，
  内容是注释里的流水线说明 —— 它们**不是**桥接产物，别把它们当成已有实现。
- `tools/bindgen.test.py` 与 CTest `bindgen_regression` / `scan_tools_regression` 覆盖的是**生成器本身**。

⇒ **缺的不是「生成绑定的能力」，缺的是「被生成出来的东西真的能跑」**：一个能 `import` 的 Python
扩展模块，和一个能被 `java` 加载的 JNI 桥，以及它们的打包产物。

---

## 2. 目标（两件，都要有可复现证据）

### 2.1 Python：`inimerse_extension.c` + `PyInit_inimerse()` + `.whl`

- 真实现 `PyInit_inimerse()`，模块名 `inimerse`，使 `import inimerse` 成立。
- 至少暴露**一个真实的引擎能力**（不是 echo）。建议从已有 C API 里挑一个可确定性验证的
  （例如跑一段 `.im` 取回结果、或调 `src/compilation/checksum.*` / 版本查询）。**挑之前先确认该 C API 真的可链接**。
- 产出可安装的 wheel：`python3 -m build` 或 `pip wheel`，产物落到 `bindings/python/dist/`（或同等位置）。
- **验收必须是「装进干净 venv 再 import」**，不是「在同目录 `python -c` 能跑」——后者会掩盖 `setup.py`
  的 `packages`/`ext_modules` 配错。

### 2.2 Java：`InimerseBridge.java` + JNI C 侧 + `.jar`

- 真实现 `InimerseBridge.java`（包名自定，建议 `dev.inimerse`），**经 JNI** 调用引擎，
  而不是纯 Java 重写一遍逻辑。
- 至少一个可运行入口，使 `java -cp <jar> …` 打印出与 Python 侧**同一个**期望值
  （两侧对同一个能力给出相同结果 —— 这本身就是一条跨语言一致性断言）。
- 产出 `.jar`：`javac` + `jar`，或走 `examples/build.gradle` / `examples/pom.xml`。

### 2.3 自动化回归（必须进树）

两条桥各要有断言，且**修复前必须失败**。形式不限（CTest 或 `tools/*.test.py`），但：
- 不能依赖网络、不能依赖已安装的全局包；
- Java 侧若本机缺工具链，见 §5 的诚实条款。

---

## 3. 冲突域

**你的写域（独占）**

- `CMakeLists.txt`（**独占** —— 这一波只有你能改）
- `src/bridge/**`（新建）
- `bindings/**`（新建：`bindings/python/**`、`bindings/java/**`、产物目录）
- `tools/bindgen.py`、`tools/bindgen.test.py`、`tools/python_scan.py`
- `examples/interface.def`、`examples/build.gradle`、`examples/pom.xml`、
  `examples/python_bridge.im`、`examples/java_bridge.im`

**不要碰**

- `src/common/**`（本仓库的硬禁碰区）
- `src/compilation/**` —— `wasm-simd-gc` 与 `aot-backend` 的写域
- `src/platform/http_posix.c`、`src/verse/crp.*` —— `crp-portal-postcheck` 的写域
- `tools/gate.sh` 的 `EXP_CTEST` 只能**同步**（你加了 CTest 就 +1），不能为了让门禁变绿而改它
- `docs/STATUS.md` / `docs/BOARD.md` —— 协调者拥有；你要改口径就在交接说明里写清楚

**允许但需说明**：为了链接引擎，你可能需要把 `inimerse` 引擎目标改造成一个可复用的
`OBJECT`/`STATIC` 库。这会动 `CMakeLists.txt` 的结构，是本条流最容易出事的地方 ——
**改完必须验证 `ctest` 仍是 95/95（或 95+N）而不是少了一整个 `add_test`**。

---

## 4. 判据（缺一项就不收）

1. `import inimerse` 在**干净 venv** 里成功，且调用结果等于期望值；命令与输出贴进交接。
2. `java -cp <jar> …` 打印同一个期望值；命令与输出贴进交接。
3. `.whl` 与 `.jar` **真的存在**（`ls -l` 贴出来），且是从本分支源码可重复构建的。
4. 两条新回归：**在 main 上失败**、在本分支通过 —— 贴两边的命令与输出。
5. `bash tools/gate.sh`（**全量，不是 `--fast`**，因为你要动 `CMakeLists.txt`）七阶段全 PASS。
6. `git diff --stat <base>..HEAD`，以及**没做什么 / 已知没解决什么**。

---

## 5. 诚实条款（本条流最容易犯的错）

- **不要把「生成器通过」当作「桥接产物存在」。** `bindgen_regression` 早就绿了，
  而 `inimerse_extension.c` 至今不存在 —— 这两件事必须分开陈述。
- **不要伪造产物。** 如果本机没有 `javac` / `jni.h` / `Python.h`，或者引擎是 C 程序而 Python
  开发头文件缺失，**停下来报告**，不要交一个跑不起来的 `.jar` 或一个只 `say` 的示例充数。
- **不要用「示例脚本能跑」替代「产物能加载」。** 仓库里已经有三个 `examples/*_bridge.im`
  是 `say` 说明脚本了，再加一个不算交付。
- 如果 Python 与 Java 两侧只能完成一侧，**明确只交那一侧**并说明另一侧的阻塞点
  —— 半条真实交付比两条假交付有价值。

---

## 6. 已知的坑

- `tools/bindgen.py` 的错误约定在**目标语言侧**是 `InimerseException` / `InimerseError`；
  写桥接时别自己发明第三种。
- `examples/*_bridge.im` 是 `.im` 脚本，**不是** C/Java 源码；别被文件名骗了。
- `docs/STATUS.md` §3.1 那张表是**当前的口径权威**。你做完之后，那两行要改，
  但改的是**协调者**（见 §3）。你只需在交接说明里给出「这两行现在该怎么写」的建议文本。
- 仓库根的 `hl_bridge.c` 与本条流**无关**，别去动它。
