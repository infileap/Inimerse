# Eidos 现状普查：那道缝有多宽

**这一件要答的问题**：**「Eidos 已经实现的部分，到底在哪一层；引擎缺的那一层，缺的是什么。」**
**判据**：**「已实现（仅经外部工具）」是一个位置陈述，不是一个程度陈述** —— 所以下面量的是**工具链到引擎之间那道缝**，不是「Eidos 完成度百分之几」。

**四项回报**
- `pwd` = `/home/sakiko/inimerse/.worktrees/eidos-state`
- 基 = `main` = **`f6e22f5`**；分支 `stream/eidos-state`；本文件是这条分支上唯一的改动
- 读法 = **读码**（`git grep` / 逐行读 `tools/eidos_desugar.py`）；**本轮没有跑端到端脱糖链**（见 §5）
- 全部读数为提交前的工作树，`git status` 只显示本文件

---

## 1. 脱糖器实现了什么：目标语言是**引擎已经认识的普通 Inimerse**

`tools/eidos_desugar.py`（516 行）。**它的 docstring 写的是「Translate Eidos aliases to the current record core syntax」**，但**读码得到的产物比这句更窄**：它把类**降级成字典 + 闭包**，只在最后一步把剩下的 `eidos`/`ed` 这个词替换成 `record`。

**它认识的形状（逐条，读码）：**

| 形状 | 出处 | 变成什么 |
|---|---|---|
| `eidos Name { … }` / `ed Name { … }` | `_translate_eidos_decls` 的词边界匹配 `re.match(r'(eidos\|ed)\b', …)` | 一个同名工厂函数 `func Name(__eidos_arg0, …)` |
| 字段 `name = expr` | `_parse_eidos_members` 的 `body[i] == '='` 分支 | 工厂参数 + `__eidos_obj["name"] = __eidos_argN ?? (默认值)` |
| 方法 `name(params) { body }` | 同函数的 `{` 分支 | 自由函数 `func Name__name(__eidos_obj, params) { body }`，再挂进字典：`__eidos_obj["name"] = (params -> Name__name(__eidos_obj, …))` |
| 单行方法 `name(params) -> expr` | 同函数的 `->` 分支 | 同上，body 外面包一层 `return` |
| 无参方法 `name { body }` | `params or []` | 同上，闭包写成 `(() -> Name__name(__eidos_obj))` |
| 单继承 `eidos Child: Base` | `src[cursor] == ':'` 分支 | **脱糖期展平**：`fields = list(inherited['fields'])`、`effective_methods = dict(inherited['methods'])`，子类按名字覆盖 |
| `super.method(args)` | `_replace_super_refs` | `Base__method(__eidos_obj, args)` |
| 方法体里的裸字段名 | `_replace_field_refs` | `__eidos_obj["field"]`（**不是参数、且前一字符不是 `.` 时**） |
| `f >> g` | `_compose_code` | `(x -> g(f(x)))` |
| 其余位置的 `eidos` / `ed` 这个词 | `translate` 的最后一个循环 | **`record`** |

**⇒ 目标语言 = 引擎已有的构造，一个都不是新的**：`func`、字典字面量 `{}`、闭包 `(x -> …)`、`??`、以及 `record` 这个**引擎词法器里真实存在的关键字**。

**被显式拒绝的（`raise ValueError`，不是静默跳过）**：mixin（`Eidos mixins are not supported by the v0.4 desugar subset`）、父类未先声明（`Eidos parent {parent} must be declared before {name}`）、空默认值、非法参数名、无法识别的成员（`unsupported Eidos member`）。

## 2. 引擎侧缺什么：**缺的不是 Eidos，是「认识 Eidos」这件事本身没必要**

**边界（这次写全，不让上一族那个盲区再咬我一次）** —— `git grep -c -i 'eidos'` 逐目录：

| 目录 | 命中文件 | 命中次数 |
|---|---|---|
| `src/` | **0** | **0** |
| `mods/` | **0** | **0** |
| `selfhost/` | **0** | **0** |
| `vtest/` | **0** | **0** |
| `tools/` | 5 | 66 |
| `CMakeLists.txt` | 1 | 8 |

⇒ **全部实现与全部引用都在 `src/`、`mods/`、`selfhost/` 之外**（`tools/` + `CMakeLists.txt`）。这不是「`src/` 里没找到」，是**这四个目录里一个字节都没有**。

**独立复核了 `docs/API.md` 的那句话**（它逐字写「核心前端零实现」）：`git grep -nw 'eidos\|ed' -- src/parser/ src/compiler/` **零命中**；`src/lexer/lexer.c` 的关键字表里有 `{"record", TOK_RECORD}, {"recorded", TOK_RECORD}`，**没有 `eidos`/`ed` 的条目**。⇒ **那句判词今天仍然成立，而且我把它从 `src/lexer`/`src/parser`/`src/compiler` 扩到了 `mods/`、`selfhost/`、`vtest/`，结论不变。**

**缝的宽度，逐项量：** 脱糖产物里的每一个构造，引擎都**已经**认识 ——
- `record` → `src/parser/parser.c` 的 `if (t.type == TOK_RECORD) return parse_record_stmt(p);`
- `??` → `src/parser/parser.c` 的 `else if (t.type == TOK_QUESTION && peek_next(p).type == TOK_QUESTION)`，以及二元运算循环里的同形判断
- `(x -> …)` → `src/lexer/lexer.c` 把 `->` 造成 `TOK_ARROW`，`src/parser/parser.c` 解析 `( ident -> … )`

⇒ **引擎缺的那一层是空的，而且它是被设计成空的**：Eidos 的**整个表面在到达引擎之前就被别名掉了**。引擎收到的从来不是 Eidos，是 `record` + 字典 + 闭包。**这不是「还没接上」，是「用别名接上了」** —— 计划书 §3.3 要人在「进引擎」与「永远外部」之间明确选一个，而**今天的读数说明第二条已经被事实上选中了**，只是没有被写成裁决。

## 3. 盯着它的两条测试：**一条算数，一条不算**

计划书 §9 的判据是「**必须断言数值，不能只断言退出码**」。逐条读：

| 测试 | 出处 | 它断言什么 | 按 §9 算不算 |
|---|---|---|---|
| `eidos_desugar_runtime` | `tools/eidos_desugar.test.py`（70 行） | **只调 `translate()`，从不运行引擎**；断言的是**生成文本的子串**（`assert 'func Counter__inc(__eidos_obj, n)' in object_src`、`assert '__eidos_obj["name"]' in out`、`assert 'record = 1' in out`） | **不算** —— 它没有数值可断言，它是脱糖器的文本单测 |
| `eidos_runtime` | `tools/eidos_runtime.test.py`（56 行） | **运行引擎**：`subprocess.run([sys.argv[1], script])`，然后 `if lines[-6:] != ['4', '7', '7', '4', '3', '9']: raise SystemExit(...)` | **算** —— 它断言的是**一串具体数值** |

**但 `eidos_runtime` 的断言有一条必须写下来的削弱**：它在比较之前先做了一次**白名单过滤** ——
`lines = [line.strip() for line in result.stdout.splitlines() if line.strip() in {'3', '4', '7', '9'}]`
⇒ **任何不属于 `{'3','4','7','9'}` 的输出行会被静默丢掉**，然后才比较最后六个。所以它**不是**「程序打印的正好是这六个值」，而是「程序打印的值里，过滤之后剩下的最后六个正好是这六个」。**额外的输出不会被发现，值域内的错值也不会被发现。**
**结论照实写**：**两条测试里只有一条按 §9 算数；算数的那一条用的是过滤后的投影。** 这不是「只断言退出码」（它确实断言了数值），但也**不是**「打印等于期望」那种强断言。

**顺带一条**：`vtest/eidos_object_probe_v04.im`（10 行）**不含 `eidos`/`ed` 关键字**（`grep -nw` rc=1），它是**手写的目标形态**（字典 + 闭包），不是 Eidos 输入；`tools/check_orphan_fixtures.py` 把它列在 `ALLOWED` 里，理由逐字是「a sample quoted in `docs/API.md:201` to show a factory function returning a dict of closures; the feature is asserted by `tools/eidos_runtime.test.py`, which carries its own inline scripts」。⇒ **没有测试跑这个文件；真正跑引擎的是那条内联脚本。**

## 4. U111 / U112：**逐字引它，然后回答「仓库里有没有文档答过」**

逐字（`docs/streams/i-json-user-statements.md:174` 的引语栏，`U111`/`i.json#223` 与 `U112`/`i.json#225` 同句连发两遍；`U111` 写的是 spirit，`U112` 同位置改为 sprite）：

> **将sprite进行面对对象化重构；如何进行eidos的开放和封闭，结合其他各种语言告诉我**

**「仓库里有没有任何一份文档回答过它」——独立复核结果：没有。**

`git grep -n '开放和封闭' -- docs/ future/` 只有 **3 处，全部在 `docs/PLAN_V06.md`**（引用这个问题的那一行、写着「连一份设计文档都不存在」的那一行、§5.3 表格里的那一行），加上台账 `docs/streams/i-json-user-statements.md:174` 的引语栏。**⇒ 这个词组在仓库里的每一次出现，都是这个问题本身的一次复述，没有一次是回答。**

把词放宽到「开放」/「封闭」分别搜，`docs/` + `future/` 的命中**全部属于别的题**：门禁的封闭性（`docs/streams/gate-hermeticity.md`、`docs/STATUS.md`）、`case try` 的开放错误域（`docs/REQUIREMENTS_ANALYSIS.md`、`docs/archive/API_CATALOG.md`）、OAuth 开放平台（`docs/archive/OAUTH.md`）、`crp-portal-auth` 的 fail-closed 默认值、集合化类型的谓词可判定边界（`docs/TYPESET_V06.md`）。**没有一条在讲「Eidos 的开放与封闭」。**
**⇒ 我复核了 headagent 的「没有」，复核结果是「没有」，而且我能说出这个「没有」是怎么量的 —— 不是「没搜到」，是「搜到的每一处都是问题本身」。**

## 5. 诚实边界

1. **我没量的**：**没有跑端到端脱糖链**（没有把一份 `.eidos` 源文件喂给 `tools/eidos_desugar.py` 再把产物喂给引擎跑一遍）。第 3 节引的两个测试是**别人写的断言**，我读的是它们的**断言文本**；我自己没有独立复现那条 `['4','7','7','4','3','9']`。
2. **我只读了这些目录**：`src/`、`mods/`、`selfhost/`、`vtest/`、`tools/`、`CMakeLists.txt`、`docs/`、`future/`。**`future/archive/` 里的 Eidos 材料我没有逐份读**（`future/archive/面对对象.md` 有 34 处 Eidos 提及，是全仓最多的文件，我只看了它的命中计数，没有读内容）。
3. **凡「未出现」，本文一律写成「未出现」，不写成「已覆盖」** —— 这两者在输出上完全一样、成因相反。第 2 节的 `src/`/`mods/`/`selfhost/`/`vtest/` 四个零，是**零命中**，不是**已覆盖**。
4. **本文不含设计**：计划书 §3.3 要的第 1 步产物是「Eidos 的开放与封闭是什么、边界在哪」的一份设计。**这份文档只报现状，不给设计** —— 而现状里**没有可推的东西**：`tools/eidos_desugar.py` 里没有任何一处处理「开放/封闭」（搜 `open`/`close`/`sealed`/`frozen` 只命中 Python 内建 `open()`、参数名 `opening`、以及 `unclosed Eidos body` 这类错误串，**没有一条是可见性/开放性的分支**）。离它最近的是 `docs/API.md` 的「不支持」清单逐字：`mixin（被显式拒绝）、可见性（private/public）、sealed/frozen/invariant、热修改、自动无括号方法调用、表达式级 case、复杂部分应用` —— **那是一串被拒绝的修饰符，不是一套开放/封闭的语义。**

## 6. 没测到

- 端到端脱糖链的实跑（见 §5.1）—— 所以「脱糖器能正确脱糖**并且**引擎能运行其产物」这句判词，我复核的是它的**后半个来源**（测试断言），不是它的**运行**。
- `future/archive/` 里那些 Eidos 材料讲了什么（尤其 `future/archive/面对对象.md`）—— 这可能是「开放与封闭」最早的讨论场所，**我没读，所以我不说它没有**。
- `ed` 作为别名与源码里已有的 `ed25519` 之类标识符的边界（脱糖器的 `(eidos|ed)\b` 是词边界匹配，`ed25519` 不匹配；但 `ed` 单用是否撞上别的东西**未测**）。
- 那五个注册计数照旧不下判断。
