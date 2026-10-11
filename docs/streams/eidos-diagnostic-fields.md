# Eidos 诊断的字段：四个实现今天是什么样

**这一栏要答的问题**：`docs/EIDOS_V06.md` §3.2（同名而不同 ⇒ 必须覆写）与 §3.4（字段的合并规则）**要求诊断能表达什么**，而**今天真的实现了诊断的三个编译器各自给了什么** —— 逐字段并排。

★ **本文件不落任何裁定。** 它落的是**四个实现今天是什么样**；裁定由 `exact-otter` 写、由用户选。本文件里没有一句话是「Eidos 应该用 X」。

★ **本文件不写任何 `<仓库文件>:<行号>` 形式的引用** —— 引节号，或引一条会印出行号的命令。理由是这一族的既有教训：**行号是位置、节号是名字，位置会随插入失效**。

**观测点**：本文件的全部读数在 **`17571f7`** 上取。开工与收工各跑一次 `git rev-parse HEAD`，两次同值。

**一个前提更正，写在最前面**：★ **§3.2 今天没有字段清单** —— 它有的是一段**给人看的渲染**（见 §1）。所以下面 §1 的 R1–R9 是我**从那段渲染推出来的**，每条标明【草案原文】还是【推断】。

---

## 1. 草案要求诊断能表达什么（R1–R9）

§3.2 的原文逐字（`grep -n 'comes from' docs/EIDOS_V06.md`）：

```
error: `bark` comes from `animal` and `pet` with different bodies
  → override it here (discards both), or
    keep one:  bark from animal
```

§3.4 的原文逐字（`grep -n 'has different defaults' docs/EIDOS_V06.md`）：

```
error: field `hp` has different defaults: `animal.hp = 10`, `pet.hp = 20`
  → restate it here with one value
```

§9.2 的验收逐字（`grep -n 'keep one' docs/EIDOS_V06.md`）：

> **删掉诊断里那句 `keep one: bark from animal`** ⇒ 必须有一条断言红

| # | 要求 | 出处逐字 | 性质 |
| --- | --- | --- | --- |
| R1 | 严重级别 | `error:` | 【草案原文】 |
| R2 | 冲突成员的名字 | `bark` / `hp` | 【草案原文】 |
| R3 | **N 个来源，每个来源一个名字** | `comes from \`animal\` and \`pet\`` | 【草案原文】 |
| R4 | 每个来源一个位置 | —— | 【**推断**】「点名」不含位置就没法跳过去 |
| R5 | 一句后果说明 | `(discards both)` | 【草案原文】 |
| R6 | 主 fix | `override it here` | 【草案原文】 |
| R7 | **备选 fix，且必须携带「它选了哪个来源」** | `keep one:  bark from animal` | 【草案原文】 |
| R8 | 两个默认值 | `\`animal.hp = 10\`, \`pet.hp = 20\`` | 【草案原文】 |
| R9 | 那句话必须**逐字稳定** | §9.2 要拿它写断言 | 【**推断**】「逐字稳定」是「能写断言」的前提 |

★ **R1–R9 是本文件唯一的推断部分**；其余每个字段名都是原样抄的命令输出。

---

## 2. 四个实现的字段清单（逐条原样键名）

每一条都同句给出产出它的命令。三个编译器的版本：`~/.cargo/bin/rustc --version` ⇒ `rustc 1.99.0 (b940084d7 2026-09-28)`；`g++ --version | head -1` ⇒ `g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`；`clang++ --version | head -1` ⇒ `Ubuntu clang version 21.1.8 (6ubuntu1)`。

### 2.1 `rustc --error-format=json`（JSON Lines，一行一个对象）

命令：`~/.cargo/bin/rustc --error-format=json q1.rs`（`q1.rs` 见 §6），逐行 `json.loads` 后取 `sorted(d.keys())`。

- 顶层键：`$message_type`, `children`, `code`, `level`, `message`, `rendered`, `spans`
- `code` 是一个**对象** `{code, explanation}`，或 `null`；`explanation` 是整篇解释文档
- `spans[]` 键：`byte_end`, `byte_start`, `column_end`, `column_start`, `expansion`, `file_name`, `is_primary`, `label`, `line_end`, `line_start`, `suggested_replacement`, `suggestion_applicability`, `text`
- `children[]` 键：`children`, `code`, `level`, `message`, `rendered`, `spans`（**可递归**；每个 child **自带 `spans[]`**）

### 2.2 `g++ -fdiagnostics-format=json`（一个数组）

命令：`g++ -fdiagnostics-format=json q1.cpp -o /dev/null 2> q1.gcc.json` 与 `g++ -Wall -fdiagnostics-format=json q5.cpp -o /dev/null 2> q5.gcc.json`，然后 `sorted(e.keys())`。

- 条目键（error）：`children`, `column-origin`, `escape-source`, `kind`, `locations`, `message`
- 条目键（warning）：以上 **+ `option`**（实测 `'-Wunused-variable'`）**+ `option_url`**（一条 gcc 文档链接）
- `children[]` 键：`escape-source`, `kind`, `locations`, `message`
- `locations[]` 键：`caret`, **`finish`**；`caret` 键：`byte-column`, `column`, `display-column`, `file`, `line`

### 2.3 `g++ -fdiagnostics-format=sarif-file`

命令：`g++ -fdiagnostics-format=sarif-file q1.cpp -o /dev/null`（★ **不写 stdout/stderr，它写源文件旁边的 `q1.cpp.sarif`**），再 `json.load` 取键。

- 顶层：`$schema`, `runs`, `version`（`2.1.0`）
- `runs[0]`：`artifacts`, `invocations`, `originalUriBaseIds`, `results`, `tool`
- `results[]`：`level`, `locations`, `message`, `relatedLocations`, `ruleId`
- `locations[0]`：`logicalLocations`, `physicalLocation`；`physicalLocation`：`artifactLocation`, **`contextRegion`**, `region`
- `relatedLocations[]`：`message`, `physicalLocation`, `properties`（实测 `{"nestingLevel": 0}`）
- `artifacts[0]`：`contents`, `location`, `roles`, `sourceLanguage` —— ★ **`contents.text` 是整份源文件**
- `invocations[0]`：`arguments`, `endTimeUtc`, `executionSuccessful`, `startTimeUtc`, `toolExecutionNotifications`, `workingDirectory` —— ★ **`arguments` 是完整的 `cc1plus` 命令行**
- `tool.driver`：`fullName`, `informationUri`, `name`, `rules`, `version`

### 2.4 `clang++ -fdiagnostics-format=sarif`（★ **写进 stderr**）

命令：`clang++ -fdiagnostics-format=sarif q1.cpp -o /dev/null 2>&1 | grep '^{' > clang.sarif`，再 `json.load` 取键。★ **`grep '^{'` 不是装饰**，见 §5 的操作性一条。

- `runs[0]`：`artifacts`, **`columnKind`**（实测 `unicodeCodePoints`）, `results`, `tool`
- `results[]`：`level`, `locations`, `message`, `ruleId`, **`ruleIndex`**
- `locations[0]`：**只有 `physicalLocation`** —— 实测 **`relatedLocations` 0/3 条、`contextRegion` 无、`logicalLocations` 无**
- `artifacts[0]`：`length`, `location.index`, `location.uri`, `mimeType`, `roles`（实测 `["resultFile"]`）
- `tool.driver`：`fullName`, `informationUri`, `language`, `name`, `rules`, `version`；`rules[0]` = `{"defaultConfiguration": {"enabled": true, "level": "error", "rank": 50}, "fullDescription": {"text": ""}, "id": "2991", "name": ""}`

### 2.5 `clang++ -fdiagnostics-parseable-fixits`（★ fix 在 SARIF **之外**）

命令：`clang++ -fsyntax-only -fdiagnostics-parseable-fixits q6.cpp` ⇒ 末行逐字 `fix-it:"q6.cpp":{1:22-1:22}:";"`。

★ 也就是说：**clang 的「建议怎么写」不在这四个格式里**（SARIF 那份实测无 fix 字段），它在一个**另外的**格式里。

---

## 3. 逐要求对照

### 3.1 主表

| 要求 | rustc json | gcc json | gcc SARIF | clang SARIF | 判定 |
| --- | --- | --- | --- | --- | --- |
| R1 级别 | `level` | `kind` | `results[].level` | `results[].level` | 草案有 · **四家都有** |
| R2 成员名 | ✗ 只在 `message` 散文里 | ✗ 同 | ✗ 同 | ✗ 同 | ★ 草案有 · **四家都没有** |
| R3 来源名 | ✗ 只在 child 的 `message` 散文 | ✗ 只在 `message` 散文 | ✗ 只在 `relatedLocations[].message` 散文 | ✗ 连散文都不带名 | ★★ 草案有 · **四家都没有** |
| R4 来源位置 | ✔ `children[].spans[]` | ✔ `children[].locations[]` | ✔ `relatedLocations[].physicalLocation` | ✗ 扁平 `results[]` | 草案有（推断）· 三家有 |
| R5 后果说明 | ✔ `children[].level` 区分 `note`/`help` | ✔ `children[].kind` | ✔ `relatedLocations[].message` | ✗ | 草案有 · 三家有 |
| R6 主 fix | ✗ 只在 `rendered` 的人读文本里 | ✗ | ✗ | ✗ | ★ 草案有 · **四家都没有** |
| R7 备选 fix + 它选了谁 | ✔ `suggested_replacement` + `suggestion_applicability` | ✗ | ✗ | ✗ | ★★★ 草案有 · **只有 rustc 有** |
| R8 两个默认值 | ✗ | ✗ | ★ 在 `relatedLocations[].contextRegion.snippet.text` 里 | ✗ | 草案有 · 一家「顺带」带上 |
| R9 逐字稳定文本 | ✔ `rendered` | ✗ 要自己拼 | ✗ | ✗ | ★★ 草案有 · **只有 rustc 有** |

### 3.2 「草案有 · 四家都没有」的格子 —— 本文件存在的理由

★ **先说口径，因为这一节六格全是【零】，而零需要被归因。「四家都没有」与「我没在四家身上找到」在表里长得一样。**

六格用的是**两种**证据，强度不同：

- **键集枚举**（六格全用）：§2 里每个格式的键表都是**对真实输出跑 `sorted(obj.keys())` 得到的完整键集**，不是一个我按名字去找的候选清单。⇒ 「这个键不存在」的证据**就是那份键集本身**，它印在 §2 里，别人重跑同一条命令会得到同一份。
- **值搜索**（R2 / R3 / R8 用）：那个名字**确实在输出里**，只是住在字符串值里 —— 逐字引在下面。⇒ 这三格不是「找不到」，是「**找到了，而它在一个不该被解析的地方**」。

★ **两种证据各自的上界**：键集枚举只覆盖 **§2 列过的那些对象**（顶层 / 条目 / span / child / results / locations / physicalLocation / relatedLocations / artifacts / invocations / tool.driver）；**我没有枚举每一个嵌套对象的键**（`region`、`properties`、`toolExecutionNotifications`、gcc `rules[]` 的内部）。⇒ 严格的说法是「**在我列出的那些键空间里，没有任何一个键承载它**」，不是「四个实现的 JSON 里没有这个键」。

- ★★ **R2（成员名）**：命令 `rustc --error-format=json q1.rs` ⇒ `message` 逐字 `multiple applicable items in scope`，**名字 `f` 不在任何结构化字段里**，只在 `rendered` 的 `^ multiple \`f\` found` 与人读标签里。
- ★★ **R3（来源名）**：同一个命令 ⇒ 来源名 `A`/`B` 只出现在 child 的 `message` 逐字 `candidate #1 is defined in an impl of the trait \`A\` for the type \`S\`` 里。gcc 那边 `g++ -fdiagnostics-format=json q1.cpp` ⇒ child 的 `message` 逐字 `candidates are: ‘int Y::hp’`。★ **两家都只有散文。**
- ★★ **R6（主 fix）**：`rustc` 的 `rendered` 里有那句「怎么写」，而**四个格式里没有任何一个字段承载它**（`suggested_replacement` 在 rustc 上承载的是 **R7 的备选**，不是主 fix）。
- ★★★ **R7（备选 fix + 它选了谁）**：命令 `rustc --error-format=json q1.rs` ⇒ 两条 `help` child 的 `suggested_replacement` 逐字 `'A::f(&s)'` / `'B::f(&s)'`。★ **「它选了 A 还是 B」这件事，今天只能靠解析那个替换串** —— `spans[]` 里没有「trait 名」这个字段。★ 补一句免得被误读：**SARIF 规范里有 `fixes`，而这次两家都没发它** —— `results[]` 的键集里没有它（§2.3 / §2.4 的键表就是证据）。⇒ 所以这一格是「**这次这条诊断上没有**」，不是「SARIF 不支持」。
- ★ **R8（两个默认值）**：`g++ -fdiagnostics-format=sarif-file q1.cpp` ⇒ `relatedLocations[0].physicalLocation.contextRegion.snippet.text` 逐字 `'struct Y { int hp = 20; };\n'`。★ **值在源码片段里，不在字段里** —— 而且这是四家里唯一一家把片段带上的。
- ★★ **R9（逐字稳定文本）**：唯一给得出的是 `rustc` 的 `rendered`，而它是**人读渲染**。

### 3.3 「草案没要求 · 实现给了」的格子

★ 一条我单拎出来，因为它与 R3/R4 直接冲突：★★ **「第 27 列」有歧义，而三家都处理了它** —— `clang++ -fdiagnostics-format=sarif q1.cpp` 给 `columnKind = unicodeCodePoints`；`g++ -fdiagnostics-format=json q1.cpp` 给 `byte-column` 与 `display-column` **两列**；`rustc --error-format=json q1.rs` 给 `column_start` **加** `byte_start`。**草案一个字没提。**

其余：`$message_type`、`rendered`、`byte_start`/`byte_end`、`expansion`（宏展开上下文）、`logicalLocations`（**包住这个位置的那个函数名，结构化**，实测 `main`/`function`）、`artifacts[].contents.text`（整份源文件）、`invocations[].arguments`（完整命令行）、`defaultConfiguration.rank`、`contextRegion.snippet`、`properties.nestingLevel`、`option_url`、`finish`（位置的结束点）。

---

## 4. 每一条字段「在哪一类诊断上成立」

★ **一份字段清单如果不写它在哪一类诊断上，它列的是并集，而并集在每一行上都不成立。**

| 字段 | 在哪一类诊断上出现 | 反例（命令 + 原样输出） |
| --- | --- | --- |
| rustc `code` | 只在**真诊断**上 | `rustc --error-format=json q1.rs` 的三行里，第 2、3 行 `$message_type` 是 `diagnostic` / `failure-note`，`code` 是 **`NoneType`** |
| rustc `spans` | 同上 | 同一命令：那两行 `spans=0` |
| rustc `suggested_replacement` / `suggestion_applicability` | 只在 **`level=help` 的 child 的 span** 上 | `rustc --error-format=json q1s.rs` ⇒ 主 span 的 `suggested_replacement` 是 `None`，只有 `help` child 上有（`'len'`） |
| rustc `$message_type` | 三行三个值 | 同一命令：`diagnostic` / `diagnostic` / `failure-note` |
| gcc `option` / `option_url` | 只在 **`kind=warning`** 上 | `g++ -Wall -fdiagnostics-format=json q5.cpp` ⇒ error 条目键里没有它们，warning 条目有 |
| gcc `children` | error 有、warning 没有 | 同一命令：error `children=2`、warning `children=0` |
| gcc `relatedLocations`（SARIF） | error 有 2 条 | `g++ -fdiagnostics-format=sarif-file q1.cpp` ⇒ `results n= 1`、`rel n= 2` |
| clang `ruleId` | 三行都有，但**值不是同一个东西** | `clang++ -fdiagnostics-format=sarif q1.cpp` ⇒ error 那行 `'2991'`、两条 note 是 `'5769'` |
| clang `relatedLocations` | **从来没有** | 同一命令 ⇒ `relatedLocations? False`（0/3） |

---

## 5. 红对照

**(a) 一个真能核的字段：`suggestion_applicability` 至少两档，两档都实测到了。**

- `rustc --error-format=json q1.rs` ⇒ 两条 `help` child 的 `suggestion_applicability` 逐字 **`HasPlaceholders`**（`suggested_replacement` = `'A::f(&s)'` / `'B::f(&s)'`）
- `rustc --error-format=json q1s.rs` ⇒ `help` child 的 `suggestion_applicability` 逐字 **`MaybeIncorrect`**（`suggested_replacement` = `'len'`）

★ **这是四家唯一的「这个 fix 有多大把握」字段。**

**(b) 一个看起来能核、实际不能的：`ruleId`。**

★ 我原以为「三个编译器都给稳定错误码」。**它不成立**，命令与输出：

- `rustc --error-format=json q1.rs` ⇒ `code.code` 逐字 **`E0034`**（**唯一一家有稳定码**）
- `g++ -fdiagnostics-format=json q1.cpp` ⇒ **条目键里没有码字段**（见 §2.2 的键表）
- `g++ -fdiagnostics-format=sarif-file q1.cpp` ⇒ `results[0].ruleId` 逐字 **`'error'`** —— ★ **就是 `kind`，不是码**
- `clang++ -fdiagnostics-format=sarif q1.cpp` ⇒ `results[0].ruleId` 逐字 **`'2991'`**，而 `tool.driver.rules[0]` 逐字 `{"defaultConfiguration": {...}, "fullDescription": {"text": ""}, "id": "2991", "name": ""}` ⇒ ★ **`name` 与 `fullDescription.text` 都是空串**

⇒ ★ **SARIF 规范要求 `ruleId`，而两家填的都是占位 —— 一个「规范要求」的字段可以在一份实现里是空的，而它读起来像已经填了。**

**(c) 操作性的一条（不是语义的，但决定谁能用）：clang 的 SARIF 写进 stderr，和一行人读警告混在一起。**

```
clang++ -fdiagnostics-format=sarif q1.cpp -o /dev/null 2> clang.raw.sarif
python3 -c 'import json;json.load(open("clang.raw.sarif"))'
```

⇒ 文件第一行逐字 `clang++: warning: diagnostic formatting in SARIF mode is currently unstable [-Wsarif-format-unstable]`，`json.load` 抛 `json.decoder.JSONDecodeError: Expecting value: line 1 column 1 (char 0)`。★ **机器可读的通道就是人读的通道 —— 而它之所以不可读，是因为它旁边有一句人读的话。**

---

## 6. 复现

探针（写在 `.scratch/df/`，gitignored，**本文件交付前已删**；四个源文件逐字如下）：

```c
// q1.cpp 与 q5.cpp 的前三行相同，q5.cpp 的 main 里多一个未使用变量
struct X { int hp = 10; };
struct Y { int hp = 20; };
struct Z : X, Y {};
int main(){ Z z; return z.hp; }          // q1.cpp
int main(){ int unused = 1; Z z; return z.hp; }   // q5.cpp（配 -Wall 才出 warning）
```

```c
// q6.cpp
int main(){ int x = 1 return x; }
```

```rust
// q1.rs
trait A { fn f(&self) -> &'static str { "A.f" } }
trait B: A { fn f(&self) -> &'static str { "B.f" } }
struct S;
impl A for S {}
impl B for S {}
fn main() { let s = S; let _ = s.f(); }
```

```rust
// q1s.rs
struct S;
impl S { fn len(&self) -> usize { 0 } }
fn main() { let v = S; let _ = v.lenght(); }
```

八条命令：

```sh
~/.cargo/bin/rustc --error-format=json q1.rs     # E0034：HasPlaceholders + 两个来源
~/.cargo/bin/rustc --error-format=json q1s.rs    # E0599：MaybeIncorrect + 'len'
g++ -fdiagnostics-format=json q1.cpp -o /dev/null 2> q1.gcc.json
g++ -Wall -fdiagnostics-format=json q5.cpp -o /dev/null 2> q5.gcc.json
g++ -fdiagnostics-format=sarif-file q1.cpp -o /dev/null   # 落 q1.cpp.sarif
clang++ -fdiagnostics-format=sarif q1.cpp -o /dev/null 2>&1 | grep '^{' > clang.sarif
clang++ -fsyntax-only -fdiagnostics-parseable-fixits q6.cpp
clang++ -fdiagnostics-format=sarif q1.cpp -o /dev/null 2> clang.raw.sarif   # §5(c) 的反例
```

★ **绕开 rustup 的 shim，用真二进制路径** —— 这一句是可照抄的，不是一条抱怨：本机 `command -v rustc` 给 `/snap/bin/rustc`（shim），直接调它逐字报 `internal error, please report: running "rustup.rustc" failed: cannot create transient scope: DBus error "org.freedesktop.DBus.Error.UnixProcessIdUnknown": [Failed to set unit properties: No such process]`；换成 `~/.cargo/bin/rustc` 就好。★ **这个形状在同一台机器上出现到第二次了**：一次是 `running "rustup.cargo" failed: cannot create transient scope`（当时让 `oauth_loop` 那个门禁阶段红过一次，定性是**环境问题、不是回归**；这一处由 `exact-otter` 转述），一次是这次的 `rustup.rustc`（本节实测）。⇒ ★ **「一条读数在一个环境里不可重跑」与「这条读数错了」是两件事，而它们在同一份报告里长得一样**；两个坑同一个修法。

---

## 7. 边界

① 全部读数本机实测，每条都同句带了命令；探针文件已删，源文件在 §6 里逐字给出。
② ★ **R1–R9 是我从 §3.2/§3.4/§9.2 那段渲染推出来的，不是草案原文** —— 这是本文件唯一一处推断，R4 与 R9 已单独标【推断】。
③ 三个编译器各只有**一条**歧义诊断作为样本（同一个 C++ 歧义、同一个文件）。**「字段按种类变」那一条用了两个种类**（error/warning、diagnostic/failure-note），**没有穷举**。
④ 本文件读的是**语言实现的行为**，不是**语言规范**。
⑤ D 的 `invariant` 块、Eiffel 的类不变量、Ada 的子类型约束**核不了**（本机无工具链，且本机网络不可达）；`git grep -il 'Eiffel' -- docs/ future/ website/` 零命中。
⑥ 本文件**不落裁定**。§3.1 表里的「判定」栏是**读数**（某格式有没有那个字段），不是「Eidos 该不该要它」。
