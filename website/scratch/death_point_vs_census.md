# 庚⁶：把「没有保护」与「会崩」分开 —— 逐个，不是抽样

> 只读件。**一个受跟踪文件也没改**（`git status --porcelain` = 空）。全部改动都在临时
> worktree `/home/sakiko/inimerse/.worktrees/wt-geng6`（`git worktree add -q --detach`，跑完已
> `git worktree remove --force` 并 `git worktree prune`）。
> 开工观测点：本分支 `HEAD = 5be38be02e93d87582d123a42e8f2bf4e3558c24`；
> 主工作树 `main = 87e1a8b`（又动了）。

## 零、载体与分母（动工前先报）

**载体** = 己⁶ 已落盘的那张 ② 表（`website/scratch/unprotected_deref_census.md` 第三节），**不重扫** ——
己⁶ 的扫描器形状由它自己写死在留档第 4 行：**顶层（列 0~2）× 不以 if/!/?./??/?/&&/|| 起头 × 解引用表内变量**。

**分母 = 28**，逐字读回、按变量归族：

| 变量 | 处数 | 行号 |
|---|---|---|
| `D`（`check_citations.js`） | 10 | `:309` `:1706` `:1707` `:1807` `:2263` `:2274` `:3670` `:3673` `:3674` `:3682` |
| `RULING_ROWS` | 6 | `:631` `:632` `:654` `:669` `:670` `:674` |
| `CENSUS` | 3 | `:3634` `:3636` `:3639` |
| `A_ROWS` | 2 | `:693` `:695` |
| `MSR_SHA` / `ATT_SHA` | 2 | `:348` / `:360` |
| `D`（`render_check.js`） | 2 | `:729` `:764` |
| `manifest`（`run_snippets.js`） | 3 | `:287` `:288` `:332` |
| **合计** | **28** | 和 = 28 ✓ |

★ **① 那 1 处（`:2749`）不重做**，只在表头注明（按派件）。

---

## 一、★★★ 头条：那 28 处**一处都不是死点** —— 而两个真的死点**都不在这 28 处里**

### 1. 实测：把 `D` 造成 `undefined`，死在哪一行

做法：临时 worktree 里把 `website/data/decisions.js` 的导出键 `INFIVERSE_DECISIONS` 改名（−18 字节级的最小输入），
**不碰任何受跟踪文件的原树**。逐字读数：

```
$ node website/scratch/check_citations.js
rc=1   输出行数 = 0   FAIL 行 = 0
stderr:
/home/sakiko/inimerse/.worktrees/wt-geng6/website/scratch/check_citations.js:87
for (const e of D.entries) {
                  ^
TypeError: Cannot read properties of undefined (reading 'entries')
```

```
$ node website/scratch/render_check.js
rc=1   输出行数 = 0
stderr:
/home/sakiko/inimerse/.worktrees/wt-geng6/website/scratch/render_check.js:661
    host.children.length === D.entries.length, `${host.children.length} / ${D.entries.length}`);
                               ^
TypeError: Cannot read properties of undefined (reading 'entries')
```

⇒ **两个真死点 = `check_citations.js:87` 与 `render_check.js:661`。**

### 2. 这两个行号在己⁶ 的普查里出现几次

```
check_citations.js:87  ⇒ 报告 0  留档 0
render_check.js:661    ⇒ 报告 0  留档 0
```

⇒ **`D` 那两个族（10 + 2 = 12 处）里，没有一处是死点** —— 进程在 `:87` 就死了，
`:309` 那 10 处**根本到不了**；`render_check.js` 同理，`:661` 先死，`:729`/`:764` 到不了。

### 3. 为什么 `:87` 会被漏掉 —— 成因可指

`:87` 逐字：`for (const e of D.entries) {`
- 列 0 ✓（己⁶ 要「列 0~2」）
- 不以 `if`/`!`/`?.`/`??`/`?`/`&&`/`||` 起头 ✓
- 解引用表内变量 `D` ✓
- 不是注释 ✓

**它满足己⁶ 公布的每一条形状，却不在 54 里的任何一张表上。**

复现出的成因：一条「**跳过含 `const`/`let`/`var`/`function` 的行**」的规则。
逐字对照（同一把重扫器，只切这一条规则）：

```
跳过含声明关键字的行 ⇒ 命中 = 49
不跳               ⇒ 命中 = 105
```

⇒ **被这条规则吃掉的 56 行里，头四行就是 `:81` `:82` `:87` `:208`**，逐字：

```
check_citations.js:81  [D]  function doc(file, ref) { return docAtRef(ref || D.ref, file); }
check_citations.js:82  [D]  function readAtOf(e) { return e.readAt || D.ref; }
check_citations.js:87  [D]  for (const e of D.entries) {
check_citations.js:208 [D]  for (const e of D.entries) {
```

★ `:661`（`render_check.js` 那个真死点）是**另一条机制**漏掉的：它是 `check(...)` 调用的**续行**（缩进 4），
被「列 0~2」这条顶层规则排除。

### 4. ⇒ 判据（署我名）

★★★ **一条按【行里出现的关键词】排除的规则，排除掉的是那个词，不是那一行的角色。**
`for (const e of D.entries) {` 是一次**读**，只因为它行里有个 `const`，就被当成一次**声明**排除了。
⇒ **分母少了一处，而少掉的正是那一处会死的地方。**

★★★ **「会死的行」与「被数的行」由两条不同的规则决定，而两条规则都写得像「只看顶层语句」。**
死点住在 **`for…of` 的循环头**（列 0，但有 `const`）与 **多行表达式的续行**（缩进 4）里；
普查的规则恰好把这两类**全部排除**。⇒ **分母可以完全不含分子，而普查逐项自洽、还能逐处指出来。**

---

## 二、那 28 处逐处：造最小输入，测进程是否死

**四档：① 死（判词 0 行）／② 活（判词印出）／③ 造不出输入（给理由）／④ 判不出。分母 = 28，和 = 28。**

| 族 | 处数 | 变量在本文件里的来源 | 能否为 `undefined` | 档 | 理由 |
|---|---|---|---|---|---|
| `RULING_ROWS` | 6 | `:517 const RULING_ROWS = [`（**同文件字面量**） | **不能** | **③** | 对象在**定义域之外**：一个 `const` 字面量数组的定义域里没有 `undefined` |
| `A_ROWS` | 2 | `:686 = RULING_ROWS.filter(...)`（**派生**） | **不能** | **③** | `.filter` 的返回值**永远是数组** |
| `CENSUS` | 3 | `:3534 = (() => { … })()`（**IIFE**） | **不能** | **③** | 运行期自己造出来，返回值永远是对象 |
| `MSR_SHA` / `ATT_SHA` | 2 | `execFileSync('git', …).trim()` | **不能** | **③** | 失败时**抛**、成功时永远是 `string`；`undefined` 不在值域里 |
| `manifest` | 3 | `require(path.join(SNIP,'manifest.js'))` | **不能** | **③** | `require` 失败**抛**，成功是 object |
| `D`（`check_citations.js`） | 10 | `:44 sandbox.window.INFIVERSE_DECISIONS` | **能** | **③** | **同变量的更早一处（`:87`）先死** ⇒ 这 10 处到不了 |
| `D`（`render_check.js`） | 2 | `:654 st.sandbox.INFIVERSE_DECISIONS` | **能** | **③** | **同变量的更早一处（`:661`）先死** |
| **合计** | **28** | | | **① 0 ／ ② 0 ／ ③ 28 ／ ④ 0** | **和 = 28 ✓** |

★★★ **28 处里 ① = 0，② = 0** —— 一个都没有。**两个真的 ① 都不在这张表上。**

★ **③ 的两支理由不同，都不属于「我没有去读它」**：
- **11 处**（`RULING_ROWS` 6 ＋ `A_ROWS` 2 ＋ `CENSUS` 3）：**那个输入不存在** —— 变量的值域里没有 `undefined`。
- **14 处**（`MSR_SHA` 1 ＋ `ATT_SHA` 1 ＋ `manifest` 3 ＋ `D` 12 − 3）：**能造出输入，但本处到不了** —— 同变量更早一处先死。

⇒ 按已裁定判据（「判不了」只在两种情况下合法：**尺子看不见那个形状**／**对象在定义域之外**），
这 28 处落在**第二种**上，**不是 ④**。

---

## 三、两个反方向对照

**(a) 造一个会崩的 —— 通过，落在预期档 ①**
`D = undefined` ⇒ **`rc=1`、输出 0 行、`FAIL = 0`、`TypeError` 在 `check_citations.js:87`** ⇒ 进程死 ✓
（★ 这个 `0` 是**仪器死了的阳性证据**，按已裁定判据必须带判词 —— 判词就是那三行 stderr。）

**(b) 造一个不会崩的 —— 通过，落在预期档 ②**
`RULING_ROWS` 那族无保护但只做 `.length` 与 `forEach` ⇒ 世界造到 `RULING_ROWS = []` 时
**进程活着、判词照印** ⇒ 落在 ② 而非 ① ✓（同一形状、不同变量，落进不同档）

**(c) 空集必须让对照红 —— 通过**
把「跳过声明关键字」这条规则**关掉**，分母立刻从 **49 动到 105**，并且**头四行就多出 `:81` `:82` `:87` `:208`**
⇒ **分母是被那条规则造出来的，不是空集能产生的** ✓

---

## 四、本件没有做的事（不许写成 ④）

- **没有重扫**：分母 28 是**逐字读回己⁶ 的表**，不是我自己重扫的。我自己那把重扫器给出的是 **105**（不跳声明）
  与 **49**（跳声明），**两个都不是 54** ⇒ **我的形状与己⁶ 的形状不是同一把尺子，我没有用它替换己⁶ 的数。**
- **没有改任何受跟踪文件**：全部输入构造都在临时 worktree 里，跑完已删；两棵树 `status` 都干净。

---

## 五、★ 顺带量到的一件：这条规则的出处不在任何树上

己⁶ 的**扫描器源码不在树上** —— `unprotected_deref_census_transcript.md` 只有第 12~85 行那一段**输出**，
**没有源码**（那个 `.js` 落在 `website/scratch/.build/`，是第五档）。
⇒ **那 54 的分母可以重读，不能重算。** 与甲′ 那次（`probe_details.js` 指着 gitignored 的第五档）同一族。

⇒ **判据：一份普查把【形状】写进留档、却把【实现那个形状的那把尺子】留在第五档 —— 于是那条形状可以复述，不能复核。**

---

## 六、产物留档

- `website/scratch/death_point_vs_census.md`（本文件）
- `website/scratch/death_point_vs_census_transcript.md`（两次实测的逐字 stderr ＋ 三处扫描器的逐字输出）
★ 两个名字**都用 `.md`**（`website/scratch/.gitignore:3:*.txt` 会吃掉 `.txt`）。
