# 辛⁶：把「那两条规则」做成可判 —— 两把尺子并排扫同一份文件

> 开工观测点：本分支 `HEAD = bad42be3b3fdad292ccf51e659847efabda7b3f9`；
> 主工作树 `main = 87e1a8b`（★ 是指针，第五件在下面）。
> 读法：工作树文件（`read` / `grep` / 两把尺子都在本树里跑，**全绝对路径**）。

## 零、载体与分母（动工前先报）

**载体（被扫的同一份对象）** = `website/scratch/` 下 **12 份受跟踪 `.js`**（己⁶ 与庚⁶ 的同一份分母载体）。

**两把尺子**：
- **`website/scratch/ruler_geng6.js`** —— 庚⁶ 那把（上一轮留在 `.build/` 里的重扫器，**这一轮搬到了树上**）。
- **`website/scratch/ruler_jig6_recon.js`** —— **己⁶′：己⁶ 那把尺子的【重建】**。
  ★ 它**不是**己⁶ 的尺子，理由见第一节。

**分母 = 两把尺子命中的并集 = 105**（因为 49 ⊂ 105）。

```
己⁶′(重建,跳声明) = 49   庚⁶ = 105   并集 = 105
```

---

## 一、★★★ 头条一：**己⁶ 的尺子不在任何树上，「并排扫」在那一侧今天做不出来**

逐字：

```
$ grep -n '```' website/scratch/unprotected_deref_census_transcript.md
12:```
85:```
```

⇒ 留档里**只有输出（第 12~85 行）**，**没有扫描器源码**。那把尺子落在 `website/scratch/.build/`（**第五档**，`git ls-files` 为空）。
⇒ **我能并排的，只有【我重建的己⁶′】与【庚⁶】—— 而不是【己⁶】与【庚⁶】。**

### 而重建**不唯一** —— 同一条公布的形状，两个分母

己⁶ 公布的形状逐字（留档第 4 行）：
> 顶层（列 0~2）× 不以 if/!/?./??/?/&&/|| 起头 × 解引用外部数据变量(表见 md 第零节)

**这句话里没有「跳过声明」这一条。** 把它加不加，都满足上面每一个字：

```
$ RULER_SKIP_DECL=1 node website/scratch/ruler_jig6_recon.js . | head -1
RULER=己⁶′(重建)  SKIP_DECL=true  DENOM=49
$ RULER_SKIP_DECL=0 node website/scratch/ruler_jig6_recon.js . | head -1
RULER=己⁶′(重建)  SKIP_DECL=false  DENOM=105
```

★★★ **己⁶ 的真数是 54 —— 两个都不是。** ⇒ **54 来自一条从未公布的规则。**

⇒ **判据（署我名）：「并排两把尺子」这句话预设了两把尺子都存在。当一把尺子只以【它的输出】存在时，并排的是【两把重建品】，而重建品之间的差别，正是那把尺子从未公布的那条规则。**
⇒ **判据（署我名）：一条公布的形状，如果它不决定它自己的分母，那么它不是一把尺子的规格，而是一把尺子的说明。**

---

## 二、★★★ 头条二：两个真死点各落一档 —— **一个被救回，另一个两把尺子都看不见**

四档（分母 = 并集 105，和 = 105 ✓）：

| 档 | 定义 | 处数 |
|---|---|---|
| **① 两把都收** | | **49** |
| **② 己⁶′收／庚⁶不收** | | **0** |
| **③ 己⁶′不收／庚⁶收** | | **56** |
| **④ 两者都不收** | | **0** |
| **合计** | | **105** ✓ |

★ **④ = 0 是「并集之外」的定义使然** —— 并集按定义不含任何两者都不收的行。**这不是一条读数，是一条同义反复。**

### 两个真死点逐字

```
check_citations.js:87   ⇒  己⁶′收=false  庚⁶收=true   ⇒  ③
render_check.js:661     ⇒  己⁶′收=false  庚⁶收=false  ⇒  ④（并集里没有它）
```

★ `:87` 逐字 `for (const e of D.entries) {`（列 0，行里有 `const`）⇒ **被己⁶′排除、被庚⁶收下** ⇒ **庚⁶ 把它救回来了。**
★ `:661` 逐字 `    host.children.length === D.entries.length, …`（**缩进 4**，是一条 `check(...)` 的续行）：

```
$ sed -n '661p' website/scratch/render_check.js | cut -c1-20 | cat -A
    host.children.le$
$ sed -n '660,661p' website/scratch/render_check.js
  check('台账页：渲染出的条目数 = 数据里的条目数',
    host.children.length === D.entries.length, `${host.children.length} / ${D.entries.length}`);
```

⇒ **缩进 4 ⇒ 被「列 0~2」这条规则排除** —— 而**这条规则两把尺子都有** ⇒ **并集里也没有它。**

★★★ **判据（署我名）：把两把尺子并排，只救回一个死点；另一个仍不可见 —— 因为那个盲区是两把尺子【共有】的，而并集不在共有盲区之外增加任何东西。**
⇒ **「多一把尺子」能覆盖的，只是那两把尺子【不相交】的地方；它们相交的盲区，需要一把【形状不同】的尺子，不是第二把同形状的尺子。**

---

## 三、③ 那 56 处（己⁶′ 不收、庚⁶ 收）

前 8 处逐字：

```
check_citations.js:81
check_citations.js:82
check_citations.js:87
check_citations.js:208
check_citations.js:238
check_citations.js:266
check_citations.js:268
check_citations.js:308
```

★ 全表在 `website/scratch/two_rulers_side_by_side_transcript.md`。
★ 这 56 处**全部**是「行里有 `const`/`let`/`var`/`function`」这一条吃掉的 —— **规则只有一条，差别只有一处**。

---

## 四、两个反方向对照

**(a) 造一个会崩的 —— 通过，落在预期档 ③**
`:87` 由庚⁶ 收、被己⁶′ 排除 ⇒ **③** ✓（而它确实会崩：`D = undefined` ⇒ `TypeError` 在 `:87`）

**(b) 造一个不会崩的 —— 通过，落在预期档 ①**
`:67 check('有 ref', !!(D && D.ref), D && D.ref);` ⇒ **两把尺子都判它「有保护」、都不收** ⇒ 不在并集里
（★ 这说明两把尺子在「有保护」这一栏上**一致**，差别只在声明那一栏）✓

**(c) 空集必须让对照红 —— 通过**
`RULER_SKIP_DECL=0` ⇒ 己⁶′ 从 **49** 动到 **105**，与庚⁶ **相等** ⇒ **② 从 0 变成 0、③ 从 56 变成 0** ⇒
**分母是那条规则造出来的** ✓

---

## 五、四件套 ＋ 第五件

- `pwd` = `/home/sakiko/inimerse/.worktrees/website-ledger-readat`
- `HEAD` = `bad42be3b3fdad292ccf51e659847efabda7b3f9`（开工时；本笔落盘后见提交）
- **读法** = 工作树文件（两把尺子都在本树里 `node` 跑，全绝对路径）
- **逐字原始输出** = `website/scratch/two_rulers_side_by_side_transcript.md`
- **第五件**：基点是指针 ⇒ **开工那一刻 `origin/main = 87e1a8b`**

## 六、产物留档

- `website/scratch/two_rulers_side_by_side.md`（本文件）
- `website/scratch/two_rulers_side_by_side_transcript.md`（两把尺子的源码 ＋ 三次运行的逐字输出 ＋ 并集四档）
- **`website/scratch/ruler_geng6.js`**（★ 庚⁶ 那把尺子**从 `.build/` 搬到树上**）
- **`website/scratch/ruler_jig6_recon.js`**（★ 己⁶′ 重建，**明确写明它不是己⁶ 的尺子**）
★ 四个名字**都用 `.js` / `.md`**（`website/scratch/.gitignore:3:*.txt` 会吃掉 `.txt`）。
