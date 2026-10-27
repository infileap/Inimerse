# 有界形式化（T4 分之一）：INIMBC 往返的字节账

本文是 `dec-formal-bounds.md`（T1）与 `dec-formal-bounds-t2.md`、`dec-formal-bounds-t3.md` 的下一节。
**它不证明任何东西**：它先把"往返"变成一个可复量的字节账，再回答"哪个格子真的危险"。

## 0. 边界与标签

四种标签，互不替代：

| 标签 | 含义 |
|---|---|
| `proved` | 有界模型里 unsat，且公理全部 measured |
| `proved-conditional` | unsat，但依赖至少一条 asserted 公理，公理点名 |
| `measured` | 跑出来的读数，可复量 |
| `asserted` | 没有读数也没有证明，只是声明 |

两个**互相独立的轴**，任何结论都要两个都写：

- 强度轴：`proved` / `proved-conditional`
- 可达性轴：`writer-reachable`（编译器写出来的镜像能走到）/ `hand-built`（只可能由手工拼字节走到）

可达性轴与强度轴无关：一个 `hand-built` 的格子可以是 `proved`，一个 `writer-reachable` 的格子也可能只是 `measured`。

## 1. 一条命令

```
python3 tools/z3bounds/check_inimbc_roundtrip.py <image.inim>
```

退出码：0 = 自证一致；1 = 自证不一致（镜像与内嵌基线打架）；2 = 输入缺失或读侧拒绝（`Refused`）。

读数所用的镜像由引擎自己写出，工具不生成：

```
/tmp/z3build/inimerse buildc hello.im hello.inim
```

**在世条件**：树外构建 `/tmp/z3build`，二进制 sha256 短 `ee513b79`；镜像 `hello.im` 内容为一行 `say "hello"`，跑起来打印 `hello`。

## 2. 一个计数器，两处使用

`written` 与 `consumed` **是同一个字节计数器的两次应用**：写侧镜像从 0 走到 N 得到 `written`，读侧镜像从 0 走到 M 得到 `consumed`。两侧用同一个函数数的，所以 `delta = written - consumed` 才是一句话；用两个不同计数器量的两张表不可比。

**格式排除（不是缺陷）**：依赖 trailer 不在"header + 流"域内。

- trailer 由 `src/compilation/deps.c` 的 `deps_write_trailer` 追加，布局是每条 `[u32 plen][路径][64 字节 sha]`，再接 16 字节页脚 `[magic][abi][count][trailer_len]`； [obs: b30314e]
- 页脚 magic 是 `0x53504449`（`"IDPS"`，见 `src/compilation/deps.h`）； [obs: b30314e]
- **`trailer_len` 含那 16 字节页脚**（`src/compilation/deps.c:12` 的注释与 `:51` 的 `start = fsize_end - trailer_len`），所以流的长度是 `filesize - trailer_len`，不是 `filesize - 16 - trailer_len`。 [obs: b30314e]

这两个数值我自己先猜错过两次，读数当场抓住：

1. magic 猜成 `0x1BC0DEFF` ⇒ 每个 trailer 都被判成 absent（`trailer_bytes=0`）；
2. 长度语义猜成"不含页脚" ⇒ 真镜像读到 `delta=-16`。

改对之后真镜像是 `written=317 | consumed=317 | delta=0 | trailer=92`（92 = 1 条依赖 `hello.im`：`4+8+64+16`）。**"猜一个常量"与"读一行源码"的差别，就是 +0 与 -16 的差别。**

## 3. 读数

真镜像（引擎写出，工具解码）：

```
TRAILER|case=real:hello.inim|trailer_bytes=92|deps=1|verified=yes
BYTES|case=real:hello.inim|written=317|consumed=317|delta=0|trailer=92|file=417
ROUNDTRIP|case=real|fields_differing=0|-|reencoded_bytes=317
CLASS|case=real:hello.inim|form=accepted
```

合成结构（手工构造，只为触发读侧的分支）：

```
BYTES|case=synthetic:funcs=1024|written=37916|consumed=37916|delta=0|form=accepted
BYTES|case=synthetic:funcs=1025|written=37953|consumed=37916|delta=37|form=truncate-without-drain
BYTES|case=synthetic:threads=33|written=1249|consumed=1212|delta=37|form=truncate-without-drain
CLASS|case=synthetic:glen=4100|form=refuse|verdict=reader_returned_null
BYTES|case=synthetic:glen=4095|written=4126|consumed=4126|delta=0|form=accepted
CAPS|funcs=1024|threads=32|glen=4096|source=src/compiler/bytecode.c:274,289,319 [obs: b30314e]
```

`form=accepted` 的定义：读侧**不返回 NULL**，且字段级比较无差异。`delta=0` 单独不足以叫往返，见 §6。

## 4. 三种形态并排（同一种失败的不同外衣）

| 形态 | 判据 | 本读侧的实例 |
|---|---|---|
| `truncate-without-drain` | 读侧返回非 NULL，但读到的计数与文件里的计数字段不同 ⇒ **成功本身就是损坏的证据** | `func_count>1024` 读到 1024；`thread_count>32` 读到 32 |
| `refuse` | 走 `goto fail`，返回 NULL | `glen<=0`、`glen>4096`、短读、负计数 |
| `skip` | 留下空洞但报告了 | **本读侧不存在** |

`skip` 的"不存在"是读数不是省略：读侧的每一次 `fread` 要么填满字段、要么 `goto fail`，函数体内没有一处 `continue` 跳过。所以三种形态在这里只能观察到两种——把不存在的形态写成"未观察到"，比凭空造一个例子诚实。

`truncate-without-drain` 的危险在于它的**返回值是成功的**：调用方拿到一个非 NULL 的 `Bytecode*`，其中的 `func_count` 已经被静默改小，而被截掉的那些函数的字节留在流里（`delta=37` 就是那 37 个没人认领的字节）。"读侧返回 NULL 的错误好查，返回成功的错误没人查"。

## 5. 可达性：先问"谁走得到"，再问"有多危险"

这是本节最重要的一条，也是**我不同意本次交付说明的地方**。

交付说明给的 T4 主例是：写侧 `src/compiler/bytecode.c:231` 附近对 `glen` 没有上界，读侧 `:319` 却有 `glen > 4096` 拒绝，所以"超过 4095 字节的全局标识符会写出一个读不回来的镜像"，并称这条 **writer-reachable（不需要 asserted 公理）**。 [obs: b30314e]

按写侧输入往上走一层，这条不成立：

1. 写侧的输入是 `bc->global_names[i]`；
2. `global_names` 在 `src/` 里只有一处写点：`src/compiler/compiler.c:3149` 的 `comp->mainBC->global_names[i] = … strdup(comp->globals[i].name) …`； [obs: b30314e]
3. `comp->globals[i].name` 在 `src/compiler/compiler.c:198` 处由 `strdup(full)` 赋值，而 `full` 是 `register_global` 里的 `char full[512]`，经 `ns_full(comp, name, full, sizeof(full))` 填入 ⇒ **写侧能产生的 `glen` 上界是 512**，远低于 4096。 [obs: b30314e]

实测曲线（同一个编译器，标识符长度递增，量出来的 `glen`）：

```
CURVE|ident_len=100|max_glen=101
CURVE|ident_len=200|max_glen=201
CURVE|ident_len=255|max_glen=256
CURVE|ident_len=256|max_glen=257
CURVE|ident_len=511|max_glen=257
CURVE|ident_len=4200|max_glen=257
```

标识符到 256 字节就被截住，之后无论写多长，`glen` 都停在 257。于是：

- `glen > 4096` 的格子是 **`hand-built`**，不是 writer-reachable；
- 它仍然是缺陷（读侧有拒绝分支、写侧没有对偶约束），但它的强度标签只能是 `measured`（合成镜像触发），不能借"写侧走得到"升格。

截断本身还带一个**静默错答案**，实测：

```
COLLIDE|globals=24|max_glen=257|two_names_declared=2
```

成因不在 bytecode 层，在编译器层（由另一车道复现并定位，已立卡 `10729bdb-3d6a-4e64-ab29-cffa5a28b3e5`）：`src/compiler/compiler.c` 里有 7 处 `char name[256]; snprintf(name, sizeof(name), "%.*s", …)`，`snprintf` 的返回值被丢掉，于是截断是静默的；截断后的名字再进 `register_global` / `lookup_local` 做字符串比较，两个不同变量就折叠成一个。 [obs: b30314e]

复现读数（同一模式，长度扫一遍）：`len=255` 正确（`A=1 B=2`），`len=256` 折叠（`A=2 B=2`），301 / 512 / 513 同；局部变量同样折叠（函数体内的两个 `L*255` 前缀名 ⇒ `A=2 B=2`，退出码 0）。

这条的形态与 §4 的 `truncate-without-drain` 同族：**截断之后成功返回**。修法属于 C 侧车道（7 处站点逐一处理，要求先红后修），本文只记最小反例与定位，不改 `src/`。

两个只在第 257 个字符之后不同的全局名（300 字符前缀相同），编译成功、退出码 0、全局表只多出 **1** 项 ⇒ 两个不同的名字被折叠成同一个全局变量，赋值互相覆盖。这不是本车道要修的缺陷，按纪律记录最小反例即可：`g…gA = 1` 与 `g…gB = 2`（前缀 300 个 `g`）编译后只有一个全局。

**由此得到的判断规则**：一条"可达"主张，必须能给出从**语法输入**到**该字段**的每一跳；给出不出的，写成 `hand-built`。可达性不是说服力，它是链条。

### 5.1 信任边界：可达性说的是“谁造得出来”，不是“值不值得关心”

上面那条结论（危险格子全在 `hand-built` 侧）是一个**可达性**结论。把它读成“所以只有测试镜像才危险”，就读反了。

- `bytecode_read` 的输入**不是**只有 `buildc` 写出来的镜像：路径由调用方给，字节由文件给，没有验签，也没有编译器在场；
- `buildc` 是**自己人**：它写出来的镜像，读回来 `delta=0 / fields_differing=0`（§3 两行）；
- `crafted` 是**外人**：旧构建缓存、被打断的写入、从别处下载的 `.inim`、同机另一个进程改过的文件。§4 的 `truncate-without-drain` 与 §5 的 `glen>4096` 两条路全部在这一侧。

所以本文的标题强度是：

> **危险格子全部只能由人造镜像（crafted artifact）走到——这是可达性结论，不是危险度结论。**
> 恰恰相反：这一侧没有编译器把关，所以它比“自己人”那一侧更需要测试。

一句可用于评审的判据：把可达性当成“要不要关心”的筛子，等于假设“输入总是自己写的”。这个假设在编译器自己的测试里成立，在引擎里不成立。


## 6. `decode(encode(x)) == x` 的定义

不是字节相等（`bytecode_write` 的填充不假设），是**字段逐个相等**，字段表：

```
count / code / string_pool / float_pool / func_count / funcs / func_argc / func_names
thread_count / threads / thread_argc / thread_names / try_entries / global_names
```

工具对每个字段单独比较，打印 `fields_differing=<n>` 并在非零时点名差异字段。真镜像那一行是 `fields_differing=0`，且**重编码得到 317 字节，等于原流长度**——两个方向都对上了，才叫往返。

## 7. 这一节到底是什么强度

- **没有任何 `proved`**。这里的镜像是 `src/compiler/bytecode.c` 的 python 复述，读数只能证明"这份复述与引擎写出的字节自洽"。 [obs: b30314e]
- 真镜像那一行：`measured`（引擎写、镜像读）。
- `funcs/threads` 截断与 `glen` 拒绝：`measured` + `hand-built`（§4、§5）。
- 自证行的 `DEF`、`CAPS` 是**从源码抽出的常量**，不是从符号表里抄的：`CAPS` 同时给出 `src/compiler/bytecode.c` 的三个行号，改源码不改工具就会自证失败。 [obs: b30314e]
- 自证失败时退出码 1，不是 0：这条工具不许"总是绿"。

## 8. 未做（明确列出，不假装）

1. **有 writer-half 测试，没有 reader 契约**：`tools/check_inimbc_writer_half.py` 只用二进制（`build/inimerse`）自己造镜像、让引擎读回来执行，并把 `written/consumed/delta/trailer/file/fields_differing` 与 tree `319fd35` 的读数逐字比对；注册在 runner 的排队点，本文不注册。它证的是**写方产物可被读回**，不是 `decode(encode(x))==x`。
   **注册政策（分开两件事）**：可以注册的是**正向不变量**——引擎写出的镜像 `delta=0 / fields_differing=0`，期望值就是 §3 那两行真镜像读数，这条进 CTest 是稳的；**不要**把 `truncate-without-drain` 的合成矩阵注册成“预期行为”，那等于把一个缺陷冻进测试。矩阵留在本文当 `measured`，它的归宿是碰撞/截断那张卡上的“修前红、修后绿”测试，注册权在 runner。
2. **selfhost 侧只有写方**：`selfhost/compiler.im` 的 INIMBC 渲染没有对应的读者，所以那一半只能写 `proved(writer-half)`，且目前连 writer-half 也没有证明，只有 C 侧这一对。 [obs: b30314e]
3. **trailer 与 try 表的 `ignore` 位**是格式排除：前者不进字节账（§2），后者读侧一律填 0，属于格式本身的不对称，不列为缺陷。
4. 字节序、对齐、填充一律按读侧 `fread` 的语义处理，没有单独建模。

## 9. 树号与改动

读数与本文所引的每一行都取自树 `319fd35`（`origin/main`，本分支的基）。本文不写自身提交号（页面自指会漂移），用 `git log -1 -- docs/streams/dec-formal-bounds-t4.md` 取。

本分支新增：`tools/z3bounds/check_inimbc_roundtrip.py`（仪表）、本文。未碰 `src/`、`selfhost/`、门禁脚本、CTest 计数。 [obs: b30314e]
另交两件工具，与本文内容无关但由这次的教训逼出来：`tools/check_md_fences.py`（量「页面能不能读」：围栏不成对 / 围栏被反斜杠转义 / 散文被噎进围栏，三形态各自计数，退出码 0/1/2）与 `tools/check_inimbc_writer_half.py`（只用二进制、自己造镜像的 writer-half 测试）。两者都不注册 CTest，排队点在 runner。


## 10. 三条计数纪律（都是自己犯过才写下来的）

### 10.1 两个数只要可能被相加，标签里就必须写单位

这页初版曾被反斜杠转义（见 §9 的工具）。复量那次缺陷时我报了两个数：围栏 14 行、行内 242 次——**两个单位**，并排写就等于暗示它们可加，而 14 + 242 = 256 不对应任何东西。真实分解是：

PAGE|docs/streams/dec-formal-bounds-t4.md|odd=0|fence_lines=14|fence_backticks=42|inline_escapes=230|swallowed=0
FENCES|files=103|odd=0|fence_lines=14|fence_backticks=42|inline_escapes=242|escaped_total=284|swallowed=0
UNITS|escaped_total=fence_backticks+inline_escapes|check=284=42+242|verdict=PASS

42 + 230 = 272 才是那一页的反斜杠+反引号总数，284 = 42 + 242 是全仓总数。判据：**escaped_total 必须等于 fence_backticks + inline_escapes**，工具自己把这条等式打进 UNITS 行并自检，别人就不必替它加。
**第一组负控：同一页、同一条命令、只差 `--rev`**（`--focus` 让零也照印，因为零是读数不是缺省）。

修前（`--rev b30314e`）：

    PAGE|docs/streams/dec-formal-bounds-t4.md|tree=b30314e|odd=0|fence_lines=14|fence_backticks=42|inline_escapes=230|swallowed=0
    FENCES|tree=b30314e|files=103|odd=0|fence_lines=14|fence_backticks=42|inline_escapes=242|escaped_total=284|swallowed=0

修后（`HEAD`）：

    PAGE|docs/streams/dec-formal-bounds-t4.md|tree=d288dec|odd=0|fence_lines=0|fence_backticks=0|inline_escapes=0|swallowed=0
    FENCES|tree=d288dec|files=104|odd=0|fence_lines=0|fence_backticks=0|inline_escapes=12|escaped_total=12|swallowed=0

判定方向写死：**同一个 `--focus` 下，页面那一行必须从 `42 + 230 = 272` 落到 `0`，页数从 103 变成 104 不是缺陷**（多出的那页就是本页）；任何一回滚到非零都算回归。

**读数行一律带 `tree=`，余额不写进页面**：`pointer delta`、`headroom` 这类数随合并变动，余额更是随树漂移的派生量；能进页面的只有「在某一棵树上量到多少」，以及「换树必须重量」。我报过 `+216`、验收者在合并后的树上量到 `+215`——差 1 不是谁的错，是树不同；但页面上写「余量 7」会把一个派生量伪装成读数。

**验收者数到的 270 与本页的 272 差 2**：本页在 `b30314e` 上逐字符数是 272（`fence_backticks=42` 来自 14 行示范围栏 × 3，`inline_escapes=230` 来自逐行扫描），复量脚本见 §9。若那 2 个是在另一棵树上或用 `grep -o` 的行计数法得到的，请以工具输出为准并指出命令；两个数不一致时，**先对齐命令再对齐数**，不要先改数。

### 10.2 按名字筛，不按位置取

writer-half 测试的第一版读镜像输出时取了**最后一条** BYTES 行，而仪表同时打印合成用例，于是它读到的是 glen=4095 合成行的 written=4126，测试对着错的数报 FAIL。改成按 case=real 筛才绿。

判据：**读数行按名字筛（case=real），不按位置取**。这与「测试与代码共用同一个错误」是同一族的两个方向——**测试失败首先可能是测试自己取错了行**。

### 10.3 「不准静默通过」也要有读数

exit 2 加一行 not registered: nothing was checked 写在代码里只是承诺。四格负控把它量出来：

NEGCTL|case=missing-binary|rc=2|named_line=yes|verdict=PASS
NEGCTL|case=missing-instrument|rc=2|named_line=yes|verdict=PASS
NEGCTL|case=unreadable-image|rc=2|named_line=yes|verdict=PASS
NEGCTL|case=measured-wrong-image|rc=1|named_line=no|verdict=PASS

最后一格是关键：**能读但读错**必须被量出来（exit 1，带差异清单），**读不出来**才拒绝测量（exit 2）。两者混成一个「失败」，等于把「没检查」和「检查不过」写成同一件事。

swallowed 一项的诚实写法，照抄这句，不要缩短成「修前红」：**它在工作区中间态量到过 1，提交前已修掉，所以没有一棵 git 树能复现那条红；可复现的只有负控 fixture 与工具本身。**
