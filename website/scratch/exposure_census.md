# ㉚ 甲 —— 26 处无 `--` 的 git 调用：暴露面普查

★ 本件回答的是 exact-otter 16 号/24 号要的**甲**。**分母 = 26**（㉚ 乙 的 A 档），**三档之和 = 26**。
★ 口径：**这是暴露面，不是缺陷。** 三档问的是「它的实参集合里**可能出现**路径形态的字符串吗」，
**不是**「它现在传的是不是修订」。⇒ 凡答「不会」的，依据必须是**实参位置上的东西不是变量**，
或者**变量的值域由一条可核的规则钉住**；不许用「今天它传的是修订」当依据。

## 0. 一条先量的实测（它决定了后面所有「会出现」的失效模样）

```
$ node report_diff.js docs/AUDIT.md          # argv[2] 被灌了一个路径形态的字符串
  分支：stream/website-ruling-registration    tip：2efd22f
  base：写死的 docs/AU
  ① 分支级   git diff --stat docs/AU..2efd22f
  fatal: ambiguous argument 'docs/AUDIT.md..2efd22f69e984a0fa11b5bde999d7cb25938fc83':
         unknown revision or path not in the working tree.
  Use '--' to separate paths from revisions, like this:
  'git <command> [<revision>...] -- [<file>...]'
```

⇒ **在这个方向上，路径形态的实参是【吵闹】地失败的，而 git 自己把修法印了出来。**
★ 但这只覆盖**一种**方向：字符串**不是一个合法修订**。**静默**需要那个字符串**同时是一个合法修订**
—— `git show --no-renames --format= -- docs/AUDIT.md 4829766` 就是这样（rc=0、0 字节、连 HEAD 都不读）。

## 1. 26 处逐处一行

| # | 位置 | 实参（逐字） | 档 | 依据（实参位置上的东西是什么） |
|---|---|---|---|---|
| 1 | `check_citations.js:50` | `'show', `${ref}:${file}`` | **会出现** | `${file}` **就是路径**（`docs/AUDIT.md`），且这是 `<rev>:<path>` 的 object-spec 语法 ⇒ 位置正确 |
| 2 | `check_citations.js:291` | `'rev-parse','--quiet','--verify', `${sd.ref}^{commit}`` | **判不了** | `sd.ref` 是**数据字段**；要答「它的值域里没有路径」得先量那个值域，本件没量 |
| 3 | `check_citations.js:345` | `'rev-parse', MSR_SPEC.resolve` | **判不了** | `MSR_SPEC.resolve` ← `D.markerScanRef.resolve \|\| D.ref`（数据） |
| 4 | `check_citations.js:357` | `'rev-parse', ATT_SPEC.resolve` | **判不了** | `ATT_SPEC.resolve` ← `D.attribution.tree`（数据） |
| 5 | `check_citations.js:369` | `'ls-tree','-r','--name-only', MSR_SHA, 'docs/'` | **会出现** | `'docs/'` **是路径**，位置在 tree-ish **之后** ⇒ `ls-tree` 的合法语法 |
| 6 | `check_citations.js:590` | `'rev-parse', RULING_ROWS_READ_AT` | **不会** | 右值是字面量 `'4829766'`（`check_citations.js:504`），**不是变量** |
| 7 | `check_citations.js:592` | `'ls-tree','-r','--name-only', sha, 'docs/'` | **会出现** | 同 #5：`'docs/'` 是路径字面量、位置合法。（`sha` 本身来自 #6 的常量） |
| 8 | `check_citations.js:1752` | `'rev-parse','--quiet','--verify', `${r}^{commit}`` | **判不了** | `r` ← `resolvedBuilt(e)`，由台账解析而来 |
| 9 | `check_citations.js:2497` | `'rev-list','--reverse', `${T29_BASE}..HEAD`` | **不会** | `T29_BASE = '8736eb3'`（`:2495` 字面量） |
| 10 | `check_citations.js:2511` | `'show','-s','--format=%s', sha` | **不会** | `sha` 是 `rev-list` 的**输出**（`:2526` 一带的 `T29_COMMITS`） |
| 11 | `check_citations.js:2529` | `'merge-base','--is-ancestor', c.sha, down.sha` | **不会** | 同上，两个都是 `rev-list` 输出 |
| 12 | `check_citations.js:2548` | `'rev-list','--count', `${b}..HEAD`` | **会出现** | `b` ← `rev-parse` 的**输出**；★ **分支名可以含 `/`**（本分支逐字叫 `stream/website-ruling-registration`，**它就是一个路径形态的字符串**）★ 但它被 `..` 包成 range ⇒ 与 #1/#5 同理：出现，且位置由语法承接 |
| 13 | `check_citations.js:2746` | `'rev-parse','--quiet','--verify', `${sp.ref}^{commit}`` | **判不了** | `sp` ← `specs.forEach`（`:2743`），`sp.ref` 是 `spec.js` 的字段 |
| 14 | `check_citations.js:2756` | `'merge-base','--is-ancestor', sp.ref, MSR_SHA` | **判不了** | 同上 |
| 15 | `report_diff.js:28` | `'rev-parse','origin/main'` | **不会** | 字面量 |
| 16 | `report_diff.js:30` | `'rev-parse','HEAD'` | **不会** | 字面量 |
| 17 | `report_diff.js:31` | `'rev-parse','--abbrev-ref','HEAD'` | **不会** | 字面量 |
| 18 | `report_diff.js:39` | `'diff','--stat', range` | **会出现** | ★ `range = ${base}..${tip}`，`base` 逐字 `= process.argv[2] \|\| git('rev-parse','origin/main')`（`report_diff.js:28`）⇒ **命令行实参直灌** —— **上面 §0 那条实测就是它** |
| 19 | `report_diff.js:50` | `'rev-list','--reverse', `${base}..${tip}`` | **会出现** | 同上 |
| 20 | `report_diff.js:52` | `'rev-list','--parents','-n','1', c` | **不会** | `c` 是 `rev-list --reverse` 的输出 |
| 21 | `report_diff.js:53` | `'log','-1','--format=%s', c` | **不会** | 同上 |
| 22 | `report_diff.js:65` | `'rev-list','--left-right','--count', `origin/${branch}...${branch}`` | **会出现** | `branch` ← `rev-parse --abbrev-ref HEAD`，**实测值 = `stream/website-ruling-registration`**（路径形态） |
| 23 | `run_snippets.js:194` | `'rev-parse','HEAD'` | **不会** | 字面量 |
| 24 | `run_snippets.js:195` | `'show','-s','--format=%cI', ref` | **判不了** | `ref` ← `git(['rev-parse','HEAD'])` 的输出 ⇒ 是**运行时**解析出来的；本件没量它在所有输入下是不是恒为修订 |
| 25 | `run_tutorial.js:197` | `'rev-parse','HEAD'` | **不会** | 字面量 |
| 26 | `run_tutorial.js:198` | `'show','-s','--format=%cI', ref` | **判不了** | 同 #24 |

**三档之和：会出现 = 7 ／ 不会 = 12 ／ 判不了 = 7 ／ 和 = 26 ✓**

★ 那 7 处「判不了」**不是含糊**，它是一个**可执行的待办**：要把它降成「不会」，得量那个数据字段的值域
（`decisions.js` / `spec.js` 里 `ref`/`source.file`/`resolve` 这些位置的**全部取值**），而**本件没量**。
★ 我**不**把「它今天传的是修订」写成「它只可能传修订」—— 那正是 24 号禁掉的那一步。

## 2. 乙：B 那 3 处逐条

| # | 位置 | 实参（逐字） | 档 | 依据 |
|---|---|---|---|---|
| 1 | `check_citations.js:2504` | `'show','--no-renames','--format=', sha, '--', q` | **只是形状危险** | 修订 `sha` 在 `--` **之前**，路径 `q` 在 `--` **之后** ⇒ 次序正确 |
| 2 | `check_citations.js:2764` | `'diff','--numstat', sp.ref, MSR_SHA, '--', sp.file` | **只是形状危险** | 两个修订都在 `--` 之前，路径 `sp.file` 在 `--` 之后 ⇒ 次序正确 |
| 3 | `report_diff.js:69` | `'diff','--stat', `${base}..${tip}`, '--', 'docs/'` | **只是形状危险** | 修订在 `--` 之前；且它的同源调用 `:39` 灌路径时**实测吵闹地失败**（§0） |

**三档之和：真的量错 = 0 ／ 只是形状危险 = 3 ／ 判不了 = 0 ／ 和 = 3 ✓**

★★★ **这一件的头条不是这 3 行，是「C 档今天为 0」那句话的两种读法：**
**一个「今天为 0」的档，它的 0 可能不是「世界干净」，而是「那一处已经被搬走了」。**
本族有一个**逐字的实例**：那个真的量错过的调用（`git show --no-renames --format= -- <path> <sha>`）
曾经就在这个文件里，**是 `b7253ee` 把它修好的**；修好之后它落在 **B 档**（修订到了 `--` 之前）——
**也就是说 `C = 0` 与 `b7253ee` 是同一件事的两种说法，而 `C = 0` 单独读起来像「从来没出过事」。**
★ 这就是为什么 ㉚ 乙 的判据一开始就写了「**C 档为 0 不构成判据**」，以及为什么必须补那条**效力证明**。
