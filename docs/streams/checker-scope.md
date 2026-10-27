# 射程与删除：两个读数管不到的地方

## 0. 这两条不是缺陷，是读数的边界

行引用检查器给出 `VERDICT|assertions=0` 时，很容易读成"引用都对"。它不这么说。它说的是两件更窄的事：**没有越界**（增量没超过上界）、**没有等式破坏**（explicit 增量仍是 +12）。这两个读数各有一个方向它管不到，下面的两节把它们写成页面，因为一条只活在量具源码里的边界，下一位读页面的人会以为是缺陷。

## 1. 射程 = 被扫描的文档集，不是仓库

扫描集是仓库里受跟踪、以 `docs/` 开头、以 `.md` 结尾的文件，共 105 个；它们的行里若点名了两个目标文件之一并带内联行号，才进输入集。读数行现在把这件事写成三个字段，读者不必再去读源码推断：

```
check_line_refs: readings -- explicit 185 (base 173), unanchored 571 (base 562), mis-labelled 0 (base 0). tree=1699e3e repo=<abs> worktree=clean swept=105 file(s) under docs/**/*.md targets=2 writings=prefix,prefix-range,link+bare,bare.
```

`swept=` 是被扫的文档数，`targets=` 是被点的目标文件数，`writings=` 是这次真的出现过的写法（`prefix`、`prefix-range`、`link+bare`、`bare` 四种，某种一次都没出现时它自己从列表里消失）。

边界就在这里：**代码注释里写的引用不在射程内**。源码里、构建清单里、检查器自己的源码里，写 「某文件某行」这样的字句（形如路径加冒号加行号），读数一位都不动，`rc` 仍是 0。这不是漏扫，是射程本来就只覆盖文档——要管代码里的引用，得另立一个扫描集，而不是把这个的射程悄悄放大。

这一格有负控测试（`tools/checker_scope.test.py`，注册在 CTest 里），三格：往 `docs/` 里写一条引用必须让 explicit 动（否则 `swept=` 就是装饰）；往构建清单里写一条必须不动；往检查器源码里写一条也必须不动。三格都在一个临时 worktree 里跑，被测的树不会被改。

## 2. `assertions=0` 不等于"引用完整"

pins 是"增量上界 + explicit 等式"。**引用变少既不在上界里，也不在等式里**：把一条不带行号的引用改成散文，unanchored 从 571 掉到 570、增量从 +9 掉到 +8，explicit 不动，`rc` 仍是 0。删掉一条引用，检查器不会红。

所以两句必须分开写：

- `assertions=0`：这次没有越界，也没有等式破坏；
- 它**不**表示"引用完整"、"没有引用被删"、"每一条锚点都对"。

真要对"少了什么"有读数，需要的是单调性而不是上限——上限管增长，删除是反方向的，再加一个上限永远不会看见删除。这就是下一节那个工具存在的全部理由。

## 3. 删除的盲区：把"曾经有什么"钉成基线

`tools/check_docs_headings.py` 数 `docs/` 下每个 markdown 文件的二级标题（`## ` 开头）个数，与 `tools/docs_headings.pin.json` 里钉住的逐文件计数比较，读数形如：

```
HEADINGS|base=1301|now=1301|removed=0|added=0
HEADINGS|pin|tree=1699e3e|docs=105|files=105
```

判据不对称，这是刻意的：

- `removed > 0` 即红。有一节连同它承载的所有引用一起消失了，而下限值永远看不见它；恢复该节，或者有意识地 `--write-pin` 并在提交信息里说清为什么。
- `added > 0` 只打印，不红。增长正是上界们负责的方向，这里不重复管。

pin 文件里数与树号写在一起：一个没有树号的数是对任何东西都不成立的断言，而更新 pin 是一个有意识动作（`--write-pin`），不是运行时的副作用。这个工具**不提供上限开关**——给删除加一个可调上限，等于把删除重新变回不可见。

负控测试（`tools/docs_headings.test.py`）四格：干净树绿；从活着的文件里删一节必须红且点名该文件；整份文档删除时 `removed` 必须等于该文件被钉住的总数（不是 1）；新增一节必须仍是绿。

## 4. 读数

本轮三组，命令写在前面，树号随读数一起给。

**射程**（树 9159df0，命令 `python3 tools/checker_scope.test.py`；三个 case 都在一个临时 worktree 里跑，被测的树没被改）：

```
scope-control: base readings -- explicit 185, unanchored 571, mis-labelled 0, swept 105, targets 2, writings prefix,prefix-range,link+bare,bare; rc=0
scope-control: in-scope-docs -- ok: explicit 185 -> 186, rc=1
scope-control: out-of-scope-CMakeLists -- ok: readings unchanged (explicit 185), rc=0
scope-control: out-of-scope-checker -- ok: readings unchanged (explicit 185), rc=0
scope-control: cases=3 red=0 green=3 -- none
```

**删除**（树 6e41869，命令 `python3 tools/docs_headings.test.py`）：

```
HEADINGS|base=1301|now=1301|removed=0|added=0
docs-headings: section-deleted -- ok: rc=1, removed=1, file named=True (dropped '## 1. 口径')
docs-headings: document-deleted -- ok: rc=1, removed=12 (want removed=12, the whole file counted)
docs-headings: section-added -- ok: rc=0, added=1: growth is what the ceilings bound, not this
docs-headings: cases=5 red=0 green=5 -- none
```

**本页自己也在扫描集里**：本页未提交时同一命令报 `swept=105`，加入之后报 `swept=106`——扫描集是按受跟踪文件算的，新增一页就多一份文档，这也正是 `swept=` 该有的行为。标题基线则要跟着重新钉一次（本节所在提交把本页的 6 个标题一并钉进基线），否则本页的章节从明天起就在"删除"里。

## 5. 不适用条件

- 这两个读数都不判"内容对不对"：一节还在、字数没变、段落换成了相反的意思，两者都是绿。
- 标题计数只认 ATX 二级标题（`## ` 开头）；用下划线式标题、或者把标题写在代码块里的文档，它不数。
- `swept=` 报的是文件数，不是行数；某份文档变大变小，只要引用没有新增越界，读数不动。
- 临时 worktree 里的控制测试需要 git 与 python；两者缺一，工具拒绝执行（`not registered: nothing was checked`），不会被当成绿。
