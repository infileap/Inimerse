# Inimerse `.im` 语言语法参考

> **性质**：全量语法清单 + 冗余/危险语法标注。
> **取证方式**：每条语法都给出源码位置（`路径:行号`）或可复现的实测命令与输出；**证不出的不写**。
> **口径**：本文描述**当前实现的实际行为**，不是设计意图。实现与设计不一致处，以本文为准并已在第 7 节标出。
> **配套文档**：[`docs/API.md`](API.md)（内建函数目录）、[`docs/STATUS.md`](STATUS.md)（能力状态）、[`docs/BOARD.md`](BOARD.md)（缺陷台账）。

本文第 7 节是**重点**：它列出的是已经实测确认、会导致静默错误或误导性报错的语法。

---

## 0. 取证来源

| 层面 | 权威来源 |
|---|---|
| 关键字与 token | `src/lexer/lexer.c:5-53`（`keywords[]`）、`src/lexer/lexer.h:7-40`（`InimerseTokenType`） |
| 词法规则 | `src/lexer/lexer.c`（`lexer_next`、`skip_comment`、`skip_whitespace`） |
| 语句语法 | `src/parser/parser.c`（`parse_stmt_impl`、`parse_simple_stmt`） |
| 表达式语法 | `src/parser/parser.c`（`parse_expr` 及其下降链） |
| AST 形状 | `src/parser/ast.h:41-49`（`ExprType`）、`:84-104`（`StmtType`） |
| 语义与字节码 | `src/compiler/compiler.c`、`src/compiler/bytecode.h` |
| 内建函数 | `vm_register_builtin*` 调用点，全仓 447 个（见第 5 节） |
| 可运行语料 | `vtest/*.im`（45 个）、`examples/`、`selfhost/*.im` |

实测用引擎：`./build/inimerse`。**注意**：引擎在程序输出之前会向 stdout 打印三行模块装载信息（`[TBP] timeBeginPeriod(1) …`、`[infiverse mod] loaded …`、`[verse_dist mod] VDP loaded …`）以及形如 `[0]="str" [1]="len"` 的调用回显。任何断言 stdout 的测试都必须容忍这些前缀行。

---

## 1. 词法

### 1.1 源文件与编码

- 源文件按**字节**读取，UTF-8 原样透传。
- 标识符接受任何 `>= 0x80` 的字节（`src/lexer/lexer.c:236`：`isalpha(c) || c == '_' || (c & 0x80)`），因此中文标识符可用，但**任何非 ASCII 垃圾字节也会被当作标识符**。
- 源码文件本身**没有**编码校验：仓库里有 14 个受版本控制的文件含 U+FFFD 替换字符（见 §7.4）。

### 1.2 注释

两种行注释，**没有块注释**（`src/lexer/lexer.c:74-90`、`:102-105`）：

```im
// C/JS 风格行注释
# 井号行注释
```

两者可连续使用，且与空白一起在 `lexer_next` 之前被跳过。

### 1.3 标识符

```
[A-Za-z_][A-Za-z0-9_]*   外加任意 >=0x80 字节
```

关键字优先于标识符：`src/lexer/lexer.c:244-247` 先查 `keywords[]`，命中即返回关键字 token。因此**保留字不能用作变量名、函数名或参数名**——实测 `func int(x) { }` 报 `expected 'function name', but got 'int' (type 29)`。

### 1.4 数字字面量

| 形式 | 例 | 实测 |
|---|---|---|
| 十进制整数 | `42` | ✓ |
| 浮点 | `1.5` | ✓ |
| 前导点浮点 | `.5` | ✓（`src/lexer/lexer.c:197`） |
| 十六进制 | `0x1F`、`0Xab` | ✓（`say str(0x1F + 1)` → `32`） |
| 科学计数 | `1e6`、`1.5e-3`、`2E4` | ✓；**非法指数会回退**（`1.5e` = `1.5` 后跟标识符 `e`，`src/lexer/lexer.c:225-232`） |

`..` 不参与浮点切分：`1..5` 是 `1` `..` `5`（`src/lexer/lexer.c:202`：`lex->src[lex->pos+1] != '.'`）。

> **实现细节（冗余）**：十六进制有**两条扫描路径**——`src/lexer/lexer.c:190-195` 的顶层早返回分支，以及 `:205-213` 嵌在十进制扫描器内部的回退分支。顶层分支只设 `text` 不设 `intVal`，靠编译器重新解析文本；两条路径重复且只有一条可达（见 §7.3）。

### 1.5 字符串字面量

单双引号等价（`src/lexer/lexer.c:139` 起用 `qc` 记录起始引号）：

```im
"double"   'single'
```

转义（`src/lexer/lexer.c:153-183`）：`\n` `\t` `\r` `\\` `\"` `\'` `\0` `\xNN`。
未知转义**原样保留反斜杠**（`default` 分支回写 `\` + 字符）。

**⚠ 危险**：`\0` 与 `\x00` 会写入**真正的 NUL 字节**。token 带显式长度，但下游大量消费者走 `strlen`，于是字符串被截断——实测 `s = "a\0b"` 后 `len(s)` 为 **1**（应为 3）。见 §7.1。

### 1.6 f-string 插值

```im
$"hello {name}"
```

- 词法：`src/lexer/lexer.c:296-305`，`$"` 起始，扫描到**下一个 `"`** 为止。
- 解析：`src/parser/parser.c:126-182`（`parse_fstring`）生成字符串拼接表达式。

**⚠ 危险**：f-string 扫描器**不处理转义、不支持嵌套引号**（`while (lex->src[fend] && lex->src[fend] != '"') fend++;`）。`$"a\"b"` 会在 `\"` 处提前结束。

### 1.7 运算符与分隔符总表

来自 `src/lexer/lexer.c:259-315`。

| 符号 | Token | 说明 |
|---|---|---|
| `+` `-` `*` `/` `%` | `TOK_PLUS` 等 | 算术 |
| `+=` `-=` `*=` `/=` | `TOK_PLUS_EQ` 等 | 复合赋值 |
| `++` `--` | `TOK_PLUS_PLUS` `TOK_MINUS_MINUS` | 自增自减（仅简单变量，见 §4.1） |
| `==` `!=` `<` `>` `<=` `>=` | | 比较 |
| `&&` `\|\|` `!` | `TOK_AND` `TOK_OR` `TOK_NOT` | 与 `and`/`or`/`not` 等价 |
| `=` | `TOK_EQ` | 赋值 |
| `->` | `TOK_ARROW` | **lambda 或类型转换**（见 §7.2） |
| `..` | `TOK_RANGE` | 范围（仅 `for` 与个别位置） |
| `~` | `TOK_TILDE` | 区间分隔符（`[1~5]`） |
| `\|>` | `TOK_PIPELINE` | 管道 |
| `>>` | `TOK_COMPOSE` | 函数组合 |
| `\|` | `TOK_PIPE` | `case` 守卫分隔符，**只能挂在绑定名模式后**（`n \| n > 0:`）；挂在字面量/成员模式后是解析错误（见 §7.2 M12） |
| `?` | `TOK_QUESTION` | Result 传播 / `??` 的前半 / `?.` 的前半 |
| `?.` | | 安全成员访问（两 token）；曾恒为 `nil`，**已修复**（见 §7.1 D11(a)） |
| `??` | | nil 合并（两 token） |
| `.` `,` `:` `;` `(` `)` `[` `]` `{` `}` | | 分隔符 |
| `$`（单独） | `TOK_UNKNOWN` | 非法 |
| `&`（单独） | `TOK_UNKNOWN` | 非法 |
| `<<` `>>=` `**` `//` `&=` `\|=` `^` | | **不存在** |

**⚠ 无按位运算符**：`&` 单独是 `TOK_UNKNOWN`，`|` 是管道 token，`>>` 被函数组合占用，`<<` 与 `^` 完全没有词法规则。要做位运算只能走内建函数。见 §7.3。

### 1.8 空白、换行与语句结束

- 空白：空格、`\t`、`\n`、`\r`、`\r\n`（`src/lexer/lexer.c:54-70`）。`\n`/`\r` 递增 `lex->line`，**且 `lexer_next` 先跳空白再返回当前 token**，所以 `p->lex.line` 是**当前 token 起始行**。
- **换行不是语句分隔符**；语句结束靠语法推断。
- `;` 是**可选的语句分隔符**，被容忍并跳过：顶层 `src/parser/parser.c:1731`，块内 `:827`、`:925`、`:1002`、`:1720`，`case` 分支内 `:1012`，`declare` 内 `:936`。
- **块由 `{ }` 界定，且只在特定位置**——见 §7.1 的裸 `{}` 陷阱。

---

## 2. 字面量与值

| 字面量 | 语法 | 例 |
|---|---|---|
| 布尔 | `true` `false` | `TOK_TRUE` / `TOK_FALSE` |
| nil | 无字面量 | 未声明变量与缺参都求值为 `nil` |
| 列表 | `[a, b, c]` | `[1, 2, 3]` |
| 字典 | `{k: v, ...}` | `{name: "x"}` |
| 集合 | `[a, b, c]` 或 `N`/`Z`/`Float1..9` 前缀 | 见 §3.4 |
| 区间 | `[a~b]`、`(a, b)`、`[a, b)`、`(,)` | 见 §3.4 |

> `nil` 没有字面量关键字：`say str(undefined_var)` 实测输出 `nil` 且**不报错**（见 §7.1）。

---

## 3. 表达式与优先级

### 3.1 优先级表（由低到高）

由 `src/parser/parser.c` 的下降链推出：

| 级别 | 函数 | 运算符 | 结合性 |
|---|---|---|---|
| 1 | `parse_expr:720` | lambda `x -> e`、`(a,b) -> e` | 前缀 |
| 2 | `parse_expr:724-731` | `>>` 函数组合 | 左 |
| 3 | `parse_expr:732-751` | `\|>` 管道 | 左 |
| 4 | `parse_coalesce:687` | `??` | 左 |
| 5 | `parse_logic_or:678` | `or` `\|\|` | 左 |
| 6 | `parse_logic_and:669` | `and` `&&` | 左 |
| 7 | `parse_comparison:626` | `==` `!=` `<` `>` `<=` `>=` `in` `match` | **链式**（见下） |
| 8 | `parse_add_sub:617` | `+` `-` | 左 |
| 9 | `parse_mul_div:608` | `*` `/` `%` | 左 |
| 10 | `parse_unary:596` | `-` `+` `not` `min` `max` | 前缀 |
| 11 | `parse_postfix:503` | 调用、索引、成员、`?.`、`?`、`-> 类型`、集合区间 | 后缀 |
| 12 | `parse_primary:183` | 字面量、标识符、分组、列表、字典 | — |

**链式比较**：`a < b < c` 不是 `(a<b)<c`，而是生成 `EXPR_CHAIN_COMPARE`（`src/parser/parser.c:635-644`、`:662-665`）。证据 `vtest/chained_comparison_v04.im`。

**`in`** 是比较级运算符（`:632`）。**`match`** 在中缀位置降为 `match(a, b)` 内建调用（`:646-657`）；实测 `3 match 3` → `true`。

**`and` / `or` 的求值结果**（2026-10-03 定案，见 `docs/AUDIT.md` §1.0）：一律产出**布尔**，并且**短路**。`1 and 5` → `true`（**不是** `5`），`0 or 7` → `true`（**不是** `7`）。这与 Python/JS 的「返回决定结果的那个操作数」不同，是本仓的**有意选择**；选它的原因是另外两个后端（AOT、wasm）本来就给布尔，而「解释器给操作数」是三者中唯一的少数派。`not` 的优先级见上表（10 级），实测 `not 0` → `true`。

> **⚠ 危险**：`>>` 的循环（`:724`）在 `|>` 的循环（`:732`）**之前**且两者**顺序执行而非统一循环**，所以**两者不能混用**。实测 `3 |> inc >> dbl` → `Error at line 4: expected '')'', but got '>>' (type 137)`。见 §7.2。

### 3.2 后缀运算

`parse_postfix:503-594`，按出现顺序判定：

| 后缀 | 说明 | 陷阱 |
|---|---|---|
| `ident[...]` / `ident(...)` 含 `~` | **集合区间**，不是索引/调用 | `a[1~2]` 是集合不是切片 |
| `ident(...)` 且 `ident` ∈ {`N`,`Z`,`Z+`,`Z-`,`Float1`…`Float9`} | **集合区间** | 同名用户函数无法调用 |
| `?.name` | 安全成员访问 | 曾恒为 `nil`，**已修复并补上断言**（D11(a)） |
| `.name` | 成员访问 | 对字典**实测恒为 `nil`**（D11(b)，命名空间读取，非缺陷）；`.` 后可跟保留字（见下） |
| `[expr]` | 索引 | |
| `?` | Result 传播（`EXPR_PROPAGATE`） | `??` 会让位（`:551-555`） |
| `-> int\|float\|str\|bool` | 类型转换（`EXPR_ARROW_CAST`） | 与 lambda 冲突 |
| `(args)` | 调用 | 命名实参不支持 |

**`.` 后可跟保留字**（`src/parser/parser.c:532-540`）：`x.int`、`x.float`、`x.str`、`x.bool`、`x.type`、`x.match` 都合法。这是**访问 `type` 的唯一途径**——`type` 是保留字，`type(x)` 直接是解析错误（见 §7.1）。

### 3.3 函数式糖

| 形式 | 语法 | 证据 |
|---|---|---|
| 管道 | `value \|> fn(a)` → `fn(value, a)` | `vtest/pipeline_v04.im` |
| 组合 | `f >> g` → `x -> g(f(x))` | `vtest/composition_v04.im` |
| lambda 单参 | `x -> expr` | `vtest/lambda_v04.im` |
| lambda 多参 | `(a, b) -> expr` | 实测 → `3` |
| nil 合并 | `a ?? b` | `vtest/null_coalesce_v04.im` |
| 安全成员 | `a?.b` | 曾恒为 `nil`，**已修复**（D11(a)）；该测试现已带断言 |
| Result 传播 | `f()?` | `vtest/result_propagation_v04.im` |
| Result 解包 | `unwrap(e)`、`unwrap_or(e, d)`、`ok(e)`、`err(e)`、`is_ok(e)` | `vtest/result_v04.im` |

**⚠ `->` 的双重身份**：`parse_lambda_prefix`（`:700-719`）在**表达式最顶端**运行，所以 `ident -> …` **永远是 lambda**；`->` 只在后置位置（非 ident 的操作数）才当类型转换。实测：

```
x = 5 ; say str(x -> int)      → Error: expected 'expression', but got 'int' (type 29)
x = 5 ; say str((x) -> int)    → Error: expected 'expression', but got 'int' (type 29)
say str(5 -> str)              → Error: '->' cast requires a variable
f = x -> str(x) ; say f(5)     → 5      （这是 lambda，不是「转成 str 再调用」）
```

见 §7.2。

### 3.4 集合与区间

集合字面量由 `parse_set_literal:490-501`、`parse_set_interval:399-422`、`parse_bare_interval:454-477` 处理。

| 形式 | 结果 | 实测 |
|---|---|---|
| `[1, 2]` | **列表** | `[1, 2]` |
| `(1, 2)` | **区间** | `set(R interval)` |
| `[1~5]` | 闭区间 | ✓ |
| `(1, 5)` | 开区间 | ✓ |
| `[1, 5)` | 左闭右开 | ✓ |
| `(,)`、`(,5]`、`[~5]` | 无界区间 | ✓ |
| `Z[1~5]`、`N(...)`、`Float2(3,10)` | 带基集的区间 | ✓ |
| `a[1~2]` | 以 `a` 为基集的区间 | `set(? interval)` |

判定逻辑：
- `looks_like_bare_interval:425-452`——括号内**顶层有 `,` 或 `~`** 才是区间。所以 `(1,2)` 是区间而 `(x)` 是分组；`[1,2]` 因为闭括号是 `]` 而**不是**区间（`:440`），于是成了列表。
- `looks_like_interval:375-397`——前缀是内置集名（`name_is_builtin_set:357-372`）或括号内有 `~` 才是区间。

**⚠ 危险**：
1. **没有元组**：`(1, 2)` 是区间。写 `f((1,2))` 传的是区间。
2. **`[1,2]` 与 `(1,2)` 语义完全不同**（列表 vs 区间），只差一个括号形状。
3. **`a[i~j]` 与 `a[i]` 语义完全不同**（集合区间 vs 索引）。
4. **`N`/`Z`/`Z+`/`Z-`/`Float1`…`Float9` 被硬编码为集合前缀**（`name_is_builtin_set`），同名用户函数**无法调用**——实测 `func Z(a,b) { return 999 }` 后 `Z(1,5)` 得到 `set(Z interval)`。
5. **`..` 不是通用表达式运算符**：`say str(1..5)` → `expected '')'', but got '..'`。`..` 只在 `for x in a..b` 里有效（`:1432-1444`）。

见 §7.1 / §7.2。

---

## 4. 语句

语句分派入口 `parse_stmt_impl:1269`，简单语句 `parse_simple_stmt:1680`。

### 4.1 赋值

```im
x = 1                 // 简单赋值
x += 1  x -= 1  x *= 2  x /= 2
x++     x--           // 仅简单变量
x = 1 (scope: "a")    // 赋值后接记录标签（src/parser/parser.c:1657-1659）
int x = 1             // 带类型标注
array xs = [1, 2]
```

- 复合赋值与 `++`/`--` 在 `:1626-1656` 降为普通二元表达式。
- `++`/`--` 只支持简单变量，否则**硬退出**：`Error: '++'/'--' currently only supported on simple variables`（`:1633-1636`）。
- 赋值目标可以是任意表达式（`a.b = 1`、`a[i] = 1`）。

**⚠ 危险**：`int` / `float` / `str` / `bool` / `array` 前缀标注**完全不生效**——`parse_simple_stmt:1722-1723` 只取变量名与初值，类型信息被丢弃。实测：

```
int x = "hello"   → say str(x) 输出 hello
bool y = [1, 2]   → say str(y) 输出 [1, 2]
```

类型标注是纯装饰，**没有任何静态或动态检查**。见 §7.1。

### 4.2 控制流

```im
if cond { } [elif cond { }] [else { }]
unless cond { } [else { }]          // = if not cond
while cond { }
for x in <expr> { }
for x in a..b { }  /  a..b..c { }   // 步长可选
for x in range(n) / range(a, b) / range(a, b, step) { }
repeat n { }
do { } until cond
break [label]
continue
to Label                            // 跳转到标签
Label: { }   /   Label: stmt
x = 1 if cond                       // 后缀 if
x = 1 unless cond                   // 后缀 unless
say "hi" if cond                    // 后缀 if 也可挂在 say 上
wait 100                            // 等待
wait until cond
```

- `unless` 在语句位置降为 `if` + 取反（`:1391-1401`）。
- 后缀 `if`/`unless` 在 `:1661-1675`（普通表达式语句）与 `:1700-1712`（`say`）两处，**必须与宿主语句同行**（提交 `37d4ea6` 的修复，`postfix_cond_here:46-49` 比较 `p->lex.line == p->prevLine`）。
- `range(...)` 特判只在 `for ... in` 内生效（`:1420-1427`）；`range` 无参时**硬退出** `range requires arguments`。
- `do { } until cond` 的 `until` 是必需项（`:1466` 的 `consume(p, TOK_UNTIL, "expected 'until'")`）。

**⚠ `until`/`till` 不是独立循环关键字**：实测 `until i >= 2 { }` → `Error: expected 'expression', but got 'until' (type 20)`。它们只在 `do ... until` 里出现。见 §7.2。

**⚠ 后缀条件的行敏感规则**是**危险**的：它用「同一行」作为消歧依据，所以

```im
f(a)
if true { ... }     // 块形式，OK
```
与
```im
f(a) if true        // 后缀形式，OK
```
都行，但把后缀条件**换行**写就会变成块形式（或直接报错）。这是本次修复引入的显式规则，已在 §7.2 说明残留风险。

### 4.3 函数

```im
func name(a, b) { }
fn name(a, b) { }          // 别名
return
return expr
```

- 定义 `:1479-1495`；参数只接受 `IDENT`（`:1486`），**不支持默认值、可变参数、类型标注**。
- lambda 捕获被拒绝（`vtest/lambda_capture_rejected_v04.im`）。

**⚠ 危险**：**调用时参数个数完全不校验**。实测 `func h(n) { return n }`：

```
h(1, 2)  → 1     （多余实参被静默忽略）
h()      → nil   （缺少实参得 nil）
```

两个方向都**不报错**。见 §7.1。

### 4.4 错误处理

```im
try { } catch (err) { } final { }        // final / finally 等价
try { } catch (err) { } finally { }
throw expr
case [try] subject { ... }
```

- `parse_try:1214`、`parse_throw:1251`。
- `final` 与 `finally` 是同一 token（`src/lexer/lexer.c:47`）。实测两者都输出 `caught` / `fin`。
- `case try` 在 `parse_case:1007`（`stmt->caseStmt.isTry = match(p, TOK_TRY) ? 1 : 0`）。

**`case` 的五种分支模式**（`parse_case:1003-1084`）：

| 模式 | 写法 | 实测 |
|---|---|---|
| 字面量（可逗号并列） | `1, 2: …` | ✓ |
| 比较 | `< 10: …`、`>= 3: …`、`== 1: …`、`!= 1: …` | ✓ |
| 成员 | `in [1, 7]: …` | ✓ |
| 正则匹配 | `match "b": …` | ✓ |
| 默认 | `else: …` | ✓ |
| 别名 | `… as name:` | `vtest/case_alias_v04.im` |
| 守卫 | `<绑定名> \| <条件>:`（多条件用 `,` 连接） | `:1044-1055`；**只能挂在绑定名后**，见 §7.2 M12 |

分支体可以是 `{ }` 或单个语句，以 `:` 引导（`:1057`）。证据：`vtest/case_collection_patterns_v04.im`、`vtest/case_structural_v04.im`、`vtest/case_nested_patterns_v04.im`、`vtest/case_try_v04.im`。

**⚠ `match` 是上下文敏感关键字**（`src/parser/ast.h` 的 `no_infix_match`）：在 `case` 分支体里 `match` 是**分支关键字**，不是中缀运算符（`src/parser/parser.c:633` 的 `!p->no_infix_match && …`）。同一个词在两处含义不同。见 §7.2。

### 4.5 并发

```im
main [endless|daemon|restart|single] { }
task [endless|daemon|restart|single] name[(params)][:] { }
thread [endless|daemon|restart|single] name[(params)][:] { }
on start { }                       // 经 GUI 通路
start name(args)
pause name | this     resume name | this     kill name | this
stop name | stop all
join name [, timeout]              // join(...) 是内建，见下
send name expr
recv var [, timeout]
lock name { }  /  lock name  /  unlock name
yield
Label:  /  thread1 to Label        // 线程跳转
```

- 定义 `:1527-1560`；`task` 与 `thread` 只差 `THREAD_FLAG_TASK`（`:1535`）。
- 名字后的 `:` 是**可选**的（`:1544`、`:1557` 的 `match(p, TOK_COLON)`）。
- **`join` 的双重身份**：后跟 `(` 时走内建函数调用，否则是语句（`:1585` 的 `peek_next(p).type != TOK_LPAREN`）。
- `send name expr` **没有分隔符**（`:1609-1613`）：`send worker "hi"`。
- `lock`/`unlock` 共用 `STMT_LOCK`，用 `isBlock` 区分 0/1/2（`:1600-1608`）。

**⚠ 危险（已缓解）**：循环体内定义 `task`/`thread` **曾经静默无效**；现在在 `:1529-1533` 硬拒绝：

```
Error at line %d: task/thread definitions inside a loop are silently ineffective; define at top level
```

注意 `parse_if_tail` 等**不**递增 `loop_depth`，只有 `while`/`for`/`repeat` 递增（`:1412`、`:1450`、`:1458`）——所以「循环」只算这三种。

### 4.6 模块

```im
import "path" [as ns]
include "file"
using modname
using thread
```

- `import` `:1593-1599`；`include` `src/parser/parser.c:1695`；`using` `:1682-1687`。
- 多文件解析在**编译期**递归处理（`compiler.c`），带命名空间前缀、去重与环检测；旧实现（拼接 + `rename_stmt`）已废弃（见 `src/parser/parser.c:1740-1741` 的注释）。

### 4.7 声明与元数据

```im
const NAME = expr
global a, b, c
type Name = <集合或表达式>
record name = expr [, tags]
record default store = expr
recorded name = expr
tag Label item1, item2
with scope: "entity", store: "both" { }
declare { mem 64 MB }            // 见下
```

- `const` `:1141-1153`；`global` `:1517-1526`；`type` `:1192-1203`；`record` `:1154-1191`；`tag` `:1111-1140`；`with` `:1204-1213`；`declare` `:927-1002`。
- `record` 与 `recorded` 是同一 token（`src/lexer/lexer.c:42`）。实测 `record score = 42, level: 3` → `42`。
- `record default store = …` 是 `parse_record_stmt:1163-1172` 里对 `IDENT` 文本 `"default"` 的**字符串比对特例**。
- 标签语法 `parse_record_tags:1085-1109`：`key: value` 或 `key = value`，逗号分隔；**`key` 必须有值**（`:1096` 无条件 `parse_expr`）。

**⚠ `declare` 的实际语法**：`parse_declare:927-1002` 逐个 `consume(TOK_IDENT)` 后 `consume(TOK_NUMBER)`。实测 `declare { mem64MB; threads8 }` → `Error: expected 'number', but got ';' (type 100)`，因为 `mem64MB` 是**一个标识符**。键与数值必须分开书写。

**⚠ 死代码：`show`/`new`/`block`/`int x` 上的后置 `with` 子句不可达。** `src/parser/parser.c:1690`、`:1692`、`:1693`、`:1722` 都用

```c
if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "with"))
```

判断后置 `with`，但 `with` 在 `keywords[]` 里是 `TOK_WITH`，**永远不会是 `TOK_IDENT`**。实测 `show "a.png" with foo` → 解析失败。独立的 `with … { }` 语句（`:1279` 经 `TOK_WITH`）是好的。见 §7.3。

### 4.8 GUI / 游戏指令

```im
window(w, h [, title])
show "path.png" [at x, y] [layer n] [with tags]
hide expr
new proto [at x, y] [height h] [with tags] [{ init }]
delete expr
cursor expr
say expr
sprite name            // 以下为「通用 GUI 动词」
stage / background / goto / move / box / costume / face / turn / point_to
velocity / gravity / bounce / size / sound / music / text / broadcast
clone / forever / when / on / autosave / quit_on_escape / fullscreen
fixed / ghost / clickable / drag / secret
```

**实现机制（这是本节最重要的事实）**：除 `window`/`show`/`hide`/`new`/`delete`/`cursor`/`say`/`stop` 有专门语法外，其余 GUI 关键字统一走 `parse_gui_stmt:895-924`，它只做两件事：

1. `stmt->guiStmt.verb = t.text;`——**把关键字原文当字符串存下来**（`:899`）；
2. 解析一串**逗号分隔、无类型约束**的实参（`:903-912`）。

`gui_no_args:878-894` 用一个**前瞻启发式**决定「这个动词是否无参」——它把 `stop`/`wait`/`say`/`if`/`while`/`for`/`repeat`/`break`/`return`/`func`/`thread`/`}`/`{`/EOF 都算作「无参开始」。

编译器侧（`src/compiler/compiler.c:1927-1962`）把它变成 `OP_CALL_BUILTIN "gui_<verb>"`：

```c
snprintf(bname, sizeof(bname), "gui_%s", verb);      // compiler.c:1945
```

并且**精灵名参数位置是靠 `argc >= N` 猜出来的**（`compiler.c:1931-1943`），例如 `sprite` 要求 `argc >= 1`、`box` 要求 `argc >= 5`、`move`/`size`/`bounce` 要求 `argc >= 2`。

**⚠ 危险**：**没有 arity 检查、没有类型检查、失败静默**。实测：

```
sprite "x"     → [0]="gui_sprite" [1]="x"
sprite 42      → [0]="gui_sprite" [1]="x"    ← 无任何错误，静默无效
```

而且**当实参个数不足时，精灵名不会被转成字符串常量**（`compiler.c:1949` 的 `i == sip` 判断失败），于是那个标识符被当**变量**求值，运行时得到 `nil`。见 §7.1。

---

## 5. 内建函数

全仓 `vm_register_builtin*` 调用点 **447 个**（去重后），按注册文件分布：

| 文件 | 数量 | 领域 |
|---|---|---|
| `src/mod/gui_mod.c` | 163 | 图形界面（`gui_*`） |
| `src/runtime/runtime_posix.c` | 79 | POSIX 运行时核心 |
| `src/runtime/runtime.c` | 59 | 运行时核心 |
| `src/mod/io_mod.c` | 44 | 文件与 IO |
| `src/mod/verse_dist_mod.c` | 32 | Verse 分布式层 |
| `src/mod/infiverse_mod.c` | 24 | Infiverse 实体 |
| `src/mod/say_stream.c` | 18 | 输出流 |
| `src/mod/say_mod_posix.c` / `_windows.c` | 17 / 17 | `say` 家族 |
| `src/mod/record_mod.c` | 12 | 记录/持久化 |
| `src/mod/result_mod.c` | 11 | Result |
| `src/mod/net_mod.c` | 11 | 网络 |
| 其余（`vm.c`、`server_mod*`、`replay_mod`、`identity_mod`、`ai_mod`、`social_mod`、`lint_mod`、`isolate_mod`、`json_mod`、`identity_mod` 等） | 各 ≤10 | — |

**核心高频内建**（有 `vtest` 覆盖的）：`len` `push` `pop` `str` `int` `float` `bool` `type` `has` `chars` `ord` `chr` `split` `join` `substr` `upper` `lower` `trim` `replace` `startswith` `endswith` `index` `sum` `sqrt` `round` `random` `rand` `range` `read_file` `write_file` `remove` `file_exists` `mkdir` `list_dir` `args` `env` `exec` `vm_exec` `input` `time_ms` `timer_ms` `sleep_ms` `json_parse` `json_serialize` `unwrap` `unwrap_or` `ok` `err` `is_ok` `result_value` `result_error` `match` `join` `thread_await` `thread_result` `thread_release` `gc_now` `gc_stats` `lint_check`。

**⚠ `type` 不可调用**：`type` 已注册为内建，但它是**保留字**（`TOK_TYPE`，`src/lexer/lexer.c:44`），`type(x)` 是解析错误：

```
Error: expected 'expression', but got 'type' (type 129)
```

唯一替代是成员访问 `x.type`（`src/parser/parser.c:532-540` 的特例）。见 §7.1。

---

## 6. 完整关键字表

来自 `src/lexer/lexer.c:5-53`。**所有关键字都至少被 parser 或其它 `src/` 文件引用过一次**（对 `keywords[]` 逐个反查 `src/**/*.c|h` 的结果：只有 `TOK_UNKNOWN` 无引用），所以**没有完全死的关键字**——但「被引用」不等于「有专门语法」，见 §7.3。

### 6.1 别名（映射到同一 token）

| 规范形 | 别名 | 证据 |
|---|---|---|
| `func` | `fn` | `src/lexer/lexer.c:29`；实测两者均可 |
| `say` | `print` | `:33`；实测两者输出一致 |
| `until` | `till` | `:18`；实测两者均可 |
| `record` | `recorded` | `:42`；实测两者均可 |
| `final` | `finally` | `:47`；实测两者均可 |
| `and` | `&&` | `:26` + `:314` |
| `or` | `\|\|` | `:26` + `:306` |
| `not` | `!` | `:26` + `:274` |

### 6.2 按类别

**控制流**：`if` `elif` `else` `unless` `while` `for` `in` `repeat` `until` `till` `do` `break` `continue` `to` `wait` `yield` `stop` `all`

**声明**：`func` `fn` `return` `global` `const` `type` `declare` `record` `recorded` `with` `case` `match` `try` `catch` `throw` `final` `finally` `import` `include` `as` `using` `main` `this` `new` `delete`

**类型名（保留字）**：`int` `float` `str` `bool` `array` `true` `false`

**并发**：`task` `thread` `block` `on` `start` `endless` `daemon` `restart` `single` `pause` `resume` `kill` `join` `lock` `unlock` `send` `recv` `goto`

**GUI / 游戏**：`window` `show` `hide` `at` `layer` `stage` `background` `sprite` `move` `box` `costume` `face` `turn` `point_to` `velocity` `gravity` `bounce` `size` `sound` `music` `text` `broadcast` `clone` `forever` `when` `cursor` `autosave` `quit_on_escape` `fullscreen` `fixed` `ghost` `clickable` `drag` `secret` `tag`

**其它**：`say` `print` `min` `max` `be` `not` `and` `or`

### 6.3 `be` 语句

```im
name be <集合或表达式> [: init]
```

`src/parser/parser.c:1357-1365`，`STMT_BE`。这是仓库里最不常见的语句形式之一。

---

## 7. 疑似冗余或危险的语法

> 本节每条都经过实测或源码反查。分类：
> **危险·静默**＝不报错但结果错误；**危险·误导**＝报错但信息误导或语义反直觉；**冗余**＝重复/不可达/无语法；**卫生**＝源码层面的问题。

### 7.1 危险·静默（不报错，结果就是错的）

#### D1. 「操作数栈非空时的用户函数调用」被 VM 误编译 —— 已于 `df82cf6` 修复

**已于 `df82cf6` 修复**（[`docs/BOARD.md`](BOARD.md) §5 行 `engine-push-call-arg-miscompile` 已完成；[`docs/STATUS.md`](STATUS.md) §10.17）。根因**不是代码生成而是 VM 调用约定**：`L_CALL_FUNC` 在 `frame_count++` 之后写 `frame_sp[frame_count-1]`，而 `L_RETURN` 在 `frame_count--` **之前**读 `frame_sp[frame_count]`，于是返回时 `sp` 被从陈旧帧槽恢复、覆盖挂起的实参。修复一行 `src/vm/vm.c:3692`：`t->sp = t->frame_sp[t->frame_count - 1];`。**下面保留修复前**的实测记录作为历史。

`push(list, <用户函数调用>)` 这一类写法被编错。确定性复现（连跑 3 次结果相同）：

```im
func g(x) { return x }
func m2() {
  a = []
  push(a, g(1))
  push(a, g(2))
  return a
}
```

- 该函数**单独成一个文件** → 得到 `2`（正确）
- 该函数**与其它函数同文件** → 得到 `1`（错误）

**⚠ 上面这个「取决于同文件里还有什么」的读法是假象**，修复时被推翻：真正规律是**第一次调用总是对的、之后的调用错**。单独调用三次 `m2` → `call1=2 call2=1 call3=1`；加一个 `m1` 只是让 `m2` 不再是第一次调用。函数体字节码在两种形态下**逐条相同**，只有函数下标不同。

| 形态 | 实测 | 应为 |
|---|---|---|
| 两次 `push(a, g(n))` | 1 | 2 |
| 三次 `push(a, g(n))` | 2 | 3 |
| `push(d["k"], g(1))`（字典内列表） | 0 | 1 |
| `push(a, g(1))` 后 `len(a)` | 0 | 1 |
| `r = push(a, g(1))` | 0 | 1 |
| `push(a, 1)` / `push(a, 2)` | 2 | 2 ✓ |
| `x = g(1); push(a, x)` | 1 | 1 ✓ |
| `push(a, str(1))`（内建实参） | 1 | 1 ✓ |

**影响面比 `push` 更宽**：凡是**操作数栈非空时发生的用户函数调用**都会中招。仓库内扫描 318 个 `.im`，**21 个文件 / 44 个（外层, 内层）调用点**受影响，其中 `selfhost/parser.im` 10 处（含 `push(stmts, parse_stmt(p))`）、`selfhost/eval.im` 4 处、`selfhost/compiler.im` 3 处、`workbench.im` 4 处、`projects/demo/main.im` 3 处。

**这也是自举工具链曾经只输出一条 `OP_HALT` 的原因**（`selfhost/parser.im` 的 `push` 累积从未落上去）。修复后 `--dump` 从 1 条变成 **120 条指令**，但自举路径仍然一行程序输出都没有 ⇒ 自举还有**第二层**原因未解（[`docs/BOARD.md`](BOARD.md) §5 行 `selfhost-codegen-empty`）。

**退出码与「跑得动」都完全掩盖它**——两种形态都 exit 0。任何回归必须断言**数值**。

#### D2. GUI 动词无 arity/类型检查，失败静默

见 §4.8。`sprite 42` 与 `sprite "x"` 一样「成功」。实参不足时精灵名不会被转成字符串常量，那个标识符被当变量求值得到 `nil`，同样不报错。

#### D3. 类型标注纯装饰

见 §4.1。`int x = "hello"`、`bool y = [1,2]` 均正常通过，无任何检查。**冗余 + 危险**。

#### D4. 函数调用参数个数不校验（两个方向都不报错）

见 §4.3。`h(1,2)` → `1`（多余忽略）；`h()` → `nil`（缺失得 nil）。

#### D5. 未声明变量求值为 `nil`

`say str(undefined_var)` → `nil`，exit 0。拼错变量名不会得到任何提示。

#### D6. 字符串里的 `\0` 截断

见 §1.5。`s = "a\0b"` 后 `len(s)` = **1**。

#### D7. `(1, 2)` 是区间，不是元组

见 §3.4。`say str((1, 2))` → `set(R interval)`。语言里**没有元组**。

#### D8. `a[1~2]` 是集合区间，不是切片

见 §3.4。`a = [10,20,30]`：`a[1]` → `20`，`a[1~2]` → `set(? interval)`。

#### D9. `type` 不可调用

见 §5。`type` 已注册为内建却是保留字，`type(x)` 是解析错误；唯一途径是 `x.type`。

#### D10. `N`/`Z`/`Z+`/`Z-`/`Float1`…`Float9` 被硬编码为集合前缀

见 §3.4。实测 `func Z(a,b) { return 999 }` 后 `Z(1,5)` → `set(Z interval)`，函数**根本没被调用**。

#### D11. `.` 与 `?.` 成员访问对字典求值为 `nil`（`?.` 已修复）

**两部分性质完全不同，不要混为一谈。**

**(a) `?.` 安全成员访问 —— 引擎缺陷，已修复。**

实测（`df82cf6` 之后的 `build`，修复前）：

```
obj = {"name": "inimerse"}
present = obj?.name
say present         →  nil    # 期望 "inimerse"
```

`src/parser/parser.c:512-529` 把 `?.` 解析成 `EXPR_MEMBER{safe=true}`；`src/compiler/compiler.c:1038` 起的 `safe` 分支发 `OP_IS_NIL` + `OP_JUMP_IF_TRUE` + `OP_LOADK_STRING` + `OP_INDEX_GET`，**语义上等价于 `obj["name"]`，本应返回 `"inimerse"`**。根因是同一分支里的**两个独立缺陷**：

- **缺陷①（键截断）**：`src/compiler/compiler.c:1047` 原文为 `int key_idx = bytecode_add_string(comp->curBC, expr->member.member.start);`。`expr->member.member` 是 `StringView`（`start` + `length`，指向源缓冲区内部，**不以 `\0` 结尾**），而 `src/compiler/bytecode.c:66` 的 `int bytecode_add_string(Bytecode *bc, const char *str)` 内部用 **`strcmp` 查重 + `strdup` 复制**，都是 NUL 终止语义。于是查找键不是 `"name"`，而是 **`"name"` 加上源文件剩下的一切**（引擎回显里那个 `[2]="name\nsay present\n\nmissing = nil?.name\nsay missing\n"` 就是它），`OP_INDEX_GET` 必然查不到 ⇒ `nil`。
- **缺陷②（陈旧寄存器）**：对象为 `nil` 时 `OP_JUMP_IF_TRUE` 跳过写 `result`，而 `result` 来自 `alloc_reg()` 的回收寄存器，**保留上一次的值** —— 只修缺陷①后 `nil?.name` 会返回上一次的 `inimerse`。

修复即两处：把键按视图长度 `snprintf("%.*s", …)` 复制（非 `safe` 的 `.` 分支本来就是这么写的，所以同一函数里一个对一个错）；并在 `OP_IS_NIL` 之前 `emit(comp->curBC, OP_MOV, result, object, 0)` 把 `result` 兜底为对象自身（引擎**没有专用 nil 载入操作码**：`src/compiler/bytecode.h` 里 NIL 相关只有 `OP_IS_NIL`，也没有 `EXPR_NIL`，这是选择兜底而非载入 nil 的原因）。

**回归**：`vtest/optional_member_v04.im` 已重写为带 `OM ` 前缀标记的版本，`optional_member_runtime`（`CMakeLists.txt:544-550`）补上 `PASS_REGULAR_EXPRESSION "OM present=inimerse"` 与 `FAIL_REGULAR_EXPRESSION "OM missing=inimerse"`，两条**各有牙且分别验证过**：还原整个修复 → 红在 `Required regular expression not found`；只保留缺陷② → 红在 `Error regular expression found in output`。
**标记是必需的，不是装饰**：引擎的内建调用回显会打印字典字面量，所以输出里**本来就有裸的 `inimerse`** —— 用 `PASS_REGULAR_EXPRESSION "inimerse"` 在坏掉时也会通过，等于没加。这是本项目一条可复用的模式：**当引擎回显会污染输出时，断言必须打在程序自己打印的带标记值行上。**

**(b) `.` 命名空间读取 —— 不是缺陷，是设计，但对字典危险。**

```
d = {"a": 1}
say str(d["a"])     →  1      # 索引访问正常
say str(d.a)        →  nil    # 成员访问失败
```

`src/compiler/compiler.c:1128-1159` 对 `EXPR_IDENT` 对象把 `"d.a"` 拼成字符串，`lookup_local(comp, "d.a")` 失败后调 `register_global(comp, full)` 再 `OP_LOAD_GLOBAL` —— **读的是一个名叫 `"d.a"` 的全局**（`u.count` 这类模块限定名的设计用途）。源码注释写着「读取不创建全局」，但代码调用的正是 `register_global()`。所以对字典用 `.` 会**静默读到 `nil`**，属 D 级危险行为，但**不是成员访问实现缺失**，本条不对它记为待修缺陷。

#### D12. `case` 的 `as` 别名恒绑 `nil`（已修复）

`pattern as name` 把整个 subject 绑定到 `name`，语法顺序固定为 `pattern as name | guard:` —— `src/parser/parser.c:1047-1048` 先吃 `as`，紧随其后才是 `if (match(p, TOK_PIPE))` 守卫。

实测（修复前）：

```
value = 42
case value {
    42 as whole: say whole      # 实测 nil，期望 42
    _: say "miss"
}
```

分支**匹配上了**（没走 `_`），但别名没被绑定。四种模式全中：`42 as whole` / `in [40~50] as w5` / `42 as g | g > 0` 都得到 `nil`。

根因在 `src/compiler/compiler.c` 的 case 分支编译：`if (br->hasAlias) { … emit(OP_STORE_GLOBAL, …); body_start = comp->curBC->count; }` 这一块**排在守卫块之后**，而且把 `body_start` **推进到了 store 之后**。匹配成功的跳转（`body_jumps`）与守卫为真的跳转（`guard_true`）**都被 patch 到 `body_start`** ⇒ 那个 `OP_STORE_GLOBAL` 在所有路径上都不可达，是死代码。`br->alias` 在整个 `src/compiler/compiler.c` 里只有那一处引用。

修复：把 store 提到守卫**之前**并让它成为分支入口（`entry_pc`），`body_jumps` 改指 `entry_pc`，守卫真跳转与 `if (!br->guard)` 的 patch 相应改道；原来那个排在守卫之后的块删掉。**附带修好**：守卫现在能引用别名（`42 as g | g > 0` 命中，`42 as g2 | g2 > 100` 正确不命中）。

**回归**：`vtest/case_alias_v04.im` 重写为带 `alias ` 前缀的四行值行（`alias whole=42` / `alias ranged=42` / `alias guarded=42` / `alias guarded2-miss`），`case_alias_runtime`（`CMakeLists.txt:607-608`）补上 `PASS_REGULAR_EXPRESSION "alias whole=42.*alias ranged=42.*alias guarded=42.*alias guarded2-miss"`。**双向验证过**：还原修复 → 红在 `Required regular expression not found`，程序输出 `alias whole=nil` / `alias ranged=nil` / `alias guarded-miss`。
**标记同样是必需的**：引擎回显里有 `[0]="alias whole=" [1]="str" [2]="alias whole-miss" …`，用裸 `42` 之类的正则会命中回显，等于没加 —— 同 D11(a) 那条模式。

**注意 `as` 只做绑定、不做匹配**：`n as whole2` 里的 `n` 是**裸标识符模式**，语义是值比较（`42 == nil` 不匹配），不是「把 subject 绑到 `n`」。

#### D13. `chr(non-int)` 读 `Value` 的 union 而不看 type tag（已修复）

`chr()` 只对 `VAL_INT` 有定义。Windows 副本（`src/runtime/runtime.c`，**只在 Windows 上编译**）
原本是

```c
int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival;
```

`Value`（`src/vm/vm.h:24-26`，`sizeof == 32`）是
`{int type; union {long long ival; double fval;}; char *sval; void *ptr;}` —— `ival` 与 `sval`
是**同一个 union 的两个成员**，按 type tag 二选一有效。对 `VAL_STRING` 读 `ival` 读的是
**指针的一半**，于是 `chr("A")` 返回一个**每次运行都不同**的控制字符。POSIX 副本
（`src/runtime/runtime_posix.c`）一直是 `v.type == VAL_INT ? v.ival & 0xff : 0`，答空串。

**为什么它是 D 级而不是「参数类型错误」**：设计记录里**没有任何**「参数类型不对时怎么办」的
规定（本节 D3/D4/D5 反而说明引擎整体不校验类型与 arity），所以两侧都**不是**「照规范做」。
这里唯一的硬要求是**确定性**：`chr("A")` 不能每次给不同的答案。

**修复**：Windows 副本改为先判 type tag、非 `VAL_INT` 答 `""`（与 POSIX 一致）：

```c
Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
int n = (v.type == VAL_INT) ? (int)(v.ival & 0xFF) : 0;
```

**`""` 仍是静默错答**（按本节分类属「危险·静默」），所以这条**留在这里而不是标成「正确」**：
`chr("A")` 应该显式报错，但那需要先有一条「参数类型错误怎么办」的规范 —— 见
`docs/AUDIT.md` §1.45 的诚实边界与 §1.46。

**回归**：`vtest/divergent_builtin_contract_v06.im`（CTest `#125`）断言 `chr("A") == ""` 与
`chr(65) == "A"`，两端同跑。**双向验证过**：还原修复 → Windows 引擎上 `chr("A")` 给出随机
控制字符，pin 红。

### 7.2 危险·误导（报错信息误导或语义反直觉）

#### M1. `|>` 与 `>>` 不能混用，报错信息误导

见 §3.1。`3 |> inc >> dbl` → `Error at line 4: expected '')'', but got '>>' (type 137)`。原因：`parse_expr:724` 与 `:732` 是两个**顺序执行**的循环而非一个统一循环。用户看到的 `expected ')'` 完全指不到真正的问题。

#### M2. `->` 既是 lambda 又是类型转换

见 §3.3。`x -> int` 被当 lambda（体内是保留字，解析错误）；`5 -> str` 被当转换但报 `'->' cast requires a variable`。**转换语法实际上只在非 ident、非字面量的操作数上可用**，而那种写法极其罕见。

#### M3. 命名实参不支持，报错误导

实测 `g(x: 1)` → `Error at line 2: expected '')'', but got '(' (type 93)`。原因在 `parse_postfix:580`：调用解析看到 `(` 后跟 `IDENT` 再跟 `:`/`=` 就 `break`，把 `(` 留给别处，于是报错落在别处。

#### M4. 保留字作为成员名

见 §3.2。`x.int`、`x.str`、`x.bool`、`x.type`、`x.match` 合法，但同一批词不能作变量名。`x.type` 还是 `type` 的唯一访问途径（D9）。

#### M5. `until`/`till` 只在 `do ... until` 里存在

见 §4.2。`until cond { }` → `Error: expected 'expression', but got 'until' (type 20)`。关键字表里有它们、`StmtType` 里有 `STMT_DO_UNTIL`，容易误以为可以独立成句。

#### M6. `..` 不是通用运算符

见 §3.4。`say str(1..5)` → `expected '')'', but got '..'`。只在 `for x in a..b` 与字面量位置有效。

#### M7. `show`/`hide`/`new`/`delete`/`cursor`/`window` 后跟 `(` 变成函数调用

`src/parser/parser.c:1299-1309`：这些关键字后跟 `(` 时按**表达式**解析。所以 `show("x")` 与 `show "x"` 是两种不同的东西。

#### M8. `join` 的双重身份

`src/parser/parser.c:1585`：`join(` 是内建函数，`join name` 是线程语句。同一个词，加个括号就换语义。

#### M9. `match` 上下文敏感

见 §4.4。`no_infix_match` 让 `match` 在 `case` 分支体里是分支关键字、在别处是中缀运算符。

#### M10. 后缀 `if`/`unless` 的行敏感规则

提交 `37d4ea6` 引入的规则：**后缀条件必须与宿主语句同行**（`postfix_cond_here:46-49`）。这消除了原有的跨行歧义，但**残留风险**是：

- 规则依赖「同一行」，所以**排版（换行）会改变语义**——把 `f(a) if true` 拆成两行就变成块形式或报错。
- 该判定只看行号，不看缩进或语句边界；`p->lex.line` 是**当前 token 起始行**（`src/lexer/lexer.c:100-101` 先跳空白），所以 `f(a) if\n true {` 中 `if` 与 `f(a)` 同行，仍按后缀解析，条件跨行。

#### M11. `--lint` 恒返回 0

`src/main.c:1252-1257`：`lint_check()` 走独立通道并**无条件 `return 0`**。实测：

```
x = = 5    →  ./build/inimerse --lint bad.im   exit 0
x = = 5    →  ./build/inimerse bad.im          exit 1
```

**`--lint` 不能当作解析谓词使用。** 这条是**工具**问题而非语法问题，但会直接影响任何以 `--lint` 做门禁的脚本。

#### M12. `|` 守卫只能挂在绑定名模式后，挂在别处报错误导

**实测**：

```
case n {
  in [7, 8] | true: say "guard-ok"
}
→  Error: expected 'expression', but got '|' (type 135)
```

而 `vtest/case_try_v04.im:15-26` 的正确写法是**守卫紧跟在绑定名后**：

```
n | n > 10: say "bad-guard"
n | n > 0:  say "guard-ok"
n | n > 0, n % 2 == 0: say "guard-two"
err(e) | e in FileError: say "file-error"
```

实测 `n = 7` 时上面的 `case` 走 `n | n > 0` → `guard-ok`。**守卫是「绑定名 + `|` + 条件」，不是「任意模式 + `|` + 条件」**；`in [...]` 或字面量模式后接 `|` 报的是 `expected 'expression'`，指不到真正原因。

#### M13. 101 个 CTest 里 66 个只断言退出码（起点 73）

`ctest --test-dir build --show-only=json-v1` 的元数据统计（**起点快照**）：

```
total tests: 101
inimerse-driven: 100   script-driven: 1
inimerse-driven with NO PASS/FAIL_REGULAR_EXPRESSION and not WILL_FAIL: 73
```

这 73 个**只要进程退出 0 就算通过**，程序打印什么都不看。对 probe 类（`verse_*_probe`、`*_regression`）退出码可能确实是它们的契约，但对语言行为类（`*_runtime`）**已经证明不够**。按 [`docs/BOARD.md`](BOARD.md) 的 `ctest-assertion-gap` 行逐批补，**当前 66**：

- `optional_member_runtime` 跑 `vtest/optional_member_v04.im`，该程序输出 `nil` 而它自己的源码期望 `inimerse` —— **测试仍然通过**（D11(a)，**已修复并补上双向验证过的断言**，是该类里第一个被补上的）。
- `lambda_capture_runtime` 曾跑 `vtest/lambda_capture_rejected_v04.im`，文件名写着 *rejected*，内容却是一段**正常成功的闭包捕获**（`say add(3)` → `5`）—— 名字与内容脱节。**已 `git mv` 为 `vtest/lambda_capture_v04.im` 并补上断言**；`lambda_runtime`、`lambda_nested_runtime` 同批补上。
- `result_runtime`、`result_question_runtime`、`result_propagation_runtime`、`try_finally_runtime` **已补上**。
- `case_alias_runtime` **已补上**（D12）。
- 仍未补：`pipeline_runtime`、`null_coalesce_runtime`、`chained_comparison_runtime`、`case_collection_patterns_runtime`、`case_structural_runtime`、`float_precision_runtime` 等语言测试。

**两条可复用模式**（写断言前必读，详见 §8）：断言要打在**程序自己打印的带标记值行**上；且**不要用 `FAIL_REGULAR_EXPRESSION` 去匹配字符串字面量** —— 引擎会把程序里每一个字面量回显出来，于是「某件事没发生」这类断言恒为空，必须让没发生的事留下**值**上的痕迹（计数/状态变量）。

这与 M11（`--lint` 恒 0）、以及 `stage_ctest` 早期「用例数只是装饰」是**同一类问题**：门禁在「过程成功」与「结果正确」之间没有桥。`EXP_CTEST` 断言已修（`f9d0270`），本条是它的下一层。

### 7.3 冗余（重复 / 不可达 / 无语法）

#### R1. 关键字别名

见 §6.1。八组等价写法（`func`/`fn`、`say`/`print`、`until`/`till`、`record`/`recorded`、`final`/`finally`、`and`/`&&`、`or`/`||`、`not`/`!`）。**功能上无害，但每种都要在文档、示例与 lint 规则里各维护一遍**。

#### R2. 后置 `with` 子句是死代码

见 §4.7。`src/parser/parser.c:1690`、`:1692`、`:1693`、`:1722` 用 `peek(p).type == TOK_IDENT && sv_eq_cstr(text, "with")` 判断，而 `with` 是 `TOK_WITH`，**该条件恒假**。实测 `show "a.png" with foo` 解析失败。**四段代码不可达。**

#### R3. `parse_stmt_impl` 里重复的条件

`src/parser/parser.c:1286` 与 `:1287` 是**逐字相同的一行**：

```c
t.type == TOK_BOUNCE || t.type == TOK_SIZE || t.type == TOK_SOUND ||
```

（用 `sed -n '1282,1296p' src/parser/parser.c | sort | uniq -c` 验证：该行出现 2 次。）第二个判断是死条件。

#### R4. 十六进制有两条扫描路径

见 §1.4。`src/lexer/lexer.c:190-195` 与 `:205-213` 重复实现；顶层分支不设 `intVal`，靠编译器重新解析文本。

#### R5. 大量 GUI 关键字没有自己的语法

`secret`、`drag`、`ghost`、`clickable`、`fixed`、`autosave`、`quit_on_escape`、`fullscreen`、`sprite`、`stage`、`background`、`goto`、`move`、`box`、`costume`、`face`、`turn`、`point_to`、`velocity`、`gravity`、`bounce`、`size`、`sound`、`music`、`text`、`broadcast`、`clone`、`forever`、`when`、`on` 等 **约 30 个关键字**在 parser 里**只出现在两个位置**：`gui_no_args:878-894` 的 `switch` 和 `parse_stmt_impl:1282-1296` 的分派条件。它们的语法完全由 `parse_gui_stmt` 的「原文当字符串」机制承载（§4.8）。**关键字表因此膨胀，但对语法没有任何约束力。**

#### R6. `TOK_UNKNOWN` 无人处理

`src/lexer/lexer.c:308`、`:314` 对单独 `$`、单独 `&` 返回 `TOK_UNKNOWN`，它是唯一在 `src/` 其它地方零引用的 token。落到 parser 时会报一个通用错误，没有专门提示。

#### R7. 完全没有按位运算符

见 §1.7。`&` 是 `TOK_UNKNOWN`、`|` 是管道、`>>` 被函数组合占用、`<<`/`^`/`>>=`/`**`/`//` 无词法规则。**这是能力缺口而非重复**，但对一门带 VM 的语言来说值得记账。

### 7.4 卫生（源码层面的问题）

#### H1. 14 个受版本控制的文件含 U+FFFD 替换字符

C 源文件的注释是**损坏的 GBK 编码**（在 UTF-8 源码里表现为成片 `锟斤拷` 与 U+FFFD）。实测统计：

| 文件 | U+FFFD 数 |
|---|---|
| `mods/debug/debug_mod.c` | 619 |
| `src/compiler/bytecode.c` | 409 |
| `mods/build/build_mod.c` | 50 |
| `src/vm/vm.c` | 28 |
| `src/parser/parser.c` | 26 |
| `src/vm/vm.h` | 24 |
| `src/main.c` | 15 |
| `src/lexer/lexer.h` | 3 |
| `src/mod/verse_dist_mod.c` | 3 |
| `docs/AUDIT.md` | 2 |
| `src/parser/ast.h` | 1 |
| `src/runtime/runtime.c` | 1 |
| `Infiverse_standard/src/ui/app.js` | 1 |
| `docs/archive/ROADMAP_0.5-0.6.md` | 1 |

例：`src/lexer/lexer.c:151` 的 `/* lI锟? */`、`src/compiler/compiler.c:1926` 的 `/* 鍏朵粬 verb 锟?CALL_BUILTIN "gui_<verb>", args */`。**这些注释已经不可读**，等于丢失了实现说明。

**2026-10-04 复核**：表格原来把 `src/mod/gui_mod.c.bak2_20260808_221050`（1485）列在第一行并计入 14，同时**漏掉了 `docs/AUDIT.md`（2）**（该文件在 `HEAD` 上就已经是 2）。删除那个备份文件后重新统计**仍是 14 个** —— 少一个、补一个，标题里的数字恰好没变；现在最坏的是 `mods/debug/debug_mod.c` 619。

#### H2. `src/mod/gui_mod.c.bak2_20260808_221050` 是入库的备份文件（**已修，2026-10-04**）

1485 个 U+FFFD，文件名带 `.bak2_<时间戳>`。它被 `git ls-files` 收录，会被文档检查器与全文检索当作正式源码。

**已删除**：`git rm src/mod/gui_mod.c.bak2_20260808_221050`，同时删掉**未被版本控制**的 `src/main.c.bak`（34646 字节，`src/main.c` 的另一份拷贝）。删前确认没有任何构建脚本、CTest 或工具引用这两个路径（`tools/`、`CMakeLists.txt`、`*.sh`/`*.py`/`*.js`/`*.cmake` 全部零命中），唯一引用它们的只有文档。记账见 `docs/STATUS.md` §10.48 与 `docs/BOARD.md` §5 的 `backup-files-removed` 行。

#### H3. `ai_browser_diag.js` 含非法 UTF-8

偏移 **478** 处有非法 UTF-8 字节序列（`python3` 的 `decode('utf-8')` 抛 `UnicodeDecodeError`）。这是全仓唯一一个**连 UTF-8 都不是**的文本文件。

---

## 8. 附：本文结论的复现方法

```bash
# 关键字表
sed -n '5,53p' src/lexer/lexer.c

# 每个关键字是否有 parser 通路（结果：只有 TOK_UNKNOWN 无引用）
python3 - <<'PY'
import re,os
hdr=open('src/lexer/lexer.h','rb').read().decode('utf-8','replace')
toks=set(re.findall(r'\bTOK_[A-Z_0-9]+\b',hdr))
cons=set()
for root,d,fs in os.walk('src'):
    for f in fs:
        if f.endswith(('.c','.h')) and 'lexer.' not in f:
            cons|=set(re.findall(r'\bTOK_[A-Z_0-9]+\b',open(os.path.join(root,f),'rb').read().decode('utf-8','replace')))
print(sorted(toks-cons))
PY

# 重复的分派条件
sed -n '1282,1296p' src/parser/parser.c | sort | uniq -c | sort -rn | head -3

# U+FFFD 统计
python3 - <<'PY'
import subprocess
for f in subprocess.run(['git','ls-files'],capture_output=True,text=True).stdout.split():
    try: n=open(f,'rb').read().decode('utf-8').count('\ufffd')
    except Exception: continue
    if n: print(n,f)
PY

# 语法行为实测（引擎会先打三行模块装载信息）
printf 'sprite 42\n' > /tmp/t.im && ./build/inimerse /tmp/t.im
printf 'say str((1, 2))\n' > /tmp/t.im && ./build/inimerse /tmp/t.im
printf 's = "a\\0b"\nsay str(len(s))\n' > /tmp/t.im && ./build/inimerse /tmp/t.im
```

**回归断言的注意事项**：
1. 引擎向 stdout 打印三行模块装载信息与一行调用回显，断言必须容忍前缀行。
2. **退出码经常区分不出对错**——D1（`push` 误编译）、D2（GUI 静默无效）、D5（未声明变量）、M11（`--lint` 恒 0）都返回 0。**必须断言输出数值。**
3. **断言要打在程序自己打印的带标记值行上**。引擎的内建调用回显会把程序里的字面量与实参打印出来（`[0]="str" [1]="len"`、`[2]="name\nsay …"`），所以输出里**本来就有裸值**。用 `PASS_REGULAR_EXPRESSION "inimerse"` 在坏掉时也会通过，等于没加。给每个被测值加一个只有程序自己会打印的前缀（`OM present=`、`alias whole=`、`lambda double=`、`resultq reached=`），断言匹配**完整的前缀+值**。这也是 `vtest/case_alias_v04.im`、`vtest/optional_member_v04.im`、`vtest/lambda_*.im`、`vtest/result_*.im` 里那些标记的由来。
4. **不要用 `FAIL_REGULAR_EXPRESSION` 去匹配字符串字面量**。第 3 条的反面：既然每个字面量都会被回显，`FAIL_REGULAR_EXPRESSION "resultq unreachable"` 这种「断言某段文本不存在」的写法**恒为空** —— 语句根本没执行，字面量照样出现在回显行里。要让「某件事没发生」可断言，就让它**留下值上的痕迹**（把 `reached = 1` 放进那个不该执行的路径，然后断言 `PASS_REGULAR_EXPRESSION "resultq reached=0"`）。
5. **`PASS_REGULAR_EXPRESSION` 的列表语义是「任一匹配即通过」**，比单条正则更弱；要多点校验就写成**一条**用 `.*` 连接的正则（`.*` 能跨行匹配，`CMakeLists.txt:607`、`:617` 是既有先例）。
6. **断言必须双向验证**：不仅要在修复版上绿，还要把实现改回坏版本、重建、确认它**确实变红**，并记下红在哪条（`Required regular expression not found` 还是 `Error regular expression found in output`）。只验证「绿」的断言可能根本没有牙。
7. **正则匹配的是前缀，数值标记必须带终结符**。`chained_comparison_runtime` 最初断言 `chain hits=1`，而把 `src/compiler/compiler.c:828` 的 `OP_AND` 改成 `OP_OR` 后程序打印的是 `chain hits=101` —— 它**以 `chain hits=1` 开头**，正则照样命中，测试仍是绿的。标记已改成 `chain hits=1 end`。凡是被测值是数字，就在它后面加一个不可能被别的数字续上的后缀。
8. **「全部由字符串字面量组成的标记」会被回显行整体满足**。第 3 条的反面加强版：引擎把程序里**所有**字面量回显成**同一行**（`[0]="coll positive-type=" [1]="str" [2]="coll range-hit=" …`），所以一条跨多个纯字面量标记的正则（`coll positive-type.*coll range-hit.*coll wildcard-hit`）**整条被那一行满足**，程序真实行为完全不参与匹配。实测：把 `src/compiler/compiler.c:1682` 的 `OP_IN, tmp, subj, pat` 换成 `pat, subj` 后程序输出确实从 `coll positive-type` 变成 `coll other`，CTest 仍 Passed；`case_structural_runtime` 同理（`OP_EQ`→`OP_NEQ` 后第一行从 `struct record-hit` 变成 `struct bad`，仍 Passed）。**修法：每个标记都要带一个计算值**（`say "coll positive-type=" + str(42)`），使「标记+值」这个串只可能出现在程序自己的输出里；改后同样的两次打断都变成 `Required regular expression not found`。
9. `--lint` 不可用作解析谓词（M11）。
10. **按平台选出来的 `PASS_REGULAR_EXPRESSION` 只在 configure 期选一次。** 当一条断言必须
    写「本平台各自的现行值」时（`round_nonnumber_contract_runtime` 的 `round-nonnumber=nil` /
    `round: expected number`；`spi_caps_contract_runtime` 的 `bool=0` / `bool=65280`），
    `CMakeLists.txt` 里用的是 `if(WIN32) set(...) else() set(...) endif()`，而
    `set_tests_properties` 被**移出了那个 if/else**（否则另一个平台会连断言都没有，得到「无断言的绿」）。
    正则随 `build/CTestTestfile.cmake` 一起在 configure 期落盘，所以**增量构建不会重挑**：
    换平台、或改了那个 `if(WIN32)` 之后直接 `cmake --build`，跑的还是上一次 configure 写下的正则。
    实测（`#131` 在 ucrt64 克隆上）：改完源文件不重跑 `cmake -S . -B <dir>` 时，Windows 仍拿 Linux
    的 `bool=0` 去匹配、测试红；重跑 configure 后 Passed。**凡新增一条按平台分叉的断言，必须在
    两个平台上各做一次干净 configure**，并把「需要重新 configure」写进交接说明。
    这与第 6 条是同一件事的两面：双向验证证明断言有牙，这条证明**牙装在哪一侧**也会过期。
