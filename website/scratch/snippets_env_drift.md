# 一份受跟踪的产物，今天重跑它的生成器，产出的不是同一份东西 —— 而两处漂的处置相反

**第五件：开工那一刻 `origin/main = 1a6ad2d12e719bfa5d8a54c34f4b79f15834de55`**（`ae41dad` 已进 main）。
**读法 = 工作树文件**（本工作树 `654db27`）。

---

## 一 · 怎么撞上的

本来只是看一眼自己的树干不干净，`git status --porcelain` 里除了两件我自己的重定向残留（下面第五节）之外，
还站着一行 **` M website/data/snippets.js`** —— 那是**跑生成器之前就在的**：

```
$ git status --porcelain
 M website/data/snippets.js
?? e2.txt
?? o2.txt
```

`website/data/snippets.js` 自己的头两行写着：
```
 * 这个文件由脚本生成，不要手改。
 * 重新生成：node website/scratch/run_snippets.js
```

⇒ 那是一个**自己声明自己是产物**的受跟踪文件。于是问：**今天重跑它的生成器，产出的还是同一份吗？**

---

## 二 · 读数：不是同一份。而且漂的是两处不同性质的东西

```
$ node website/scratch/run_snippets.js
$ git diff -- website/data/snippets.js
```
```diff
@@ -3,8 +3,8 @@
  * 重新生成：node website/scratch/run_snippets.js
  */
 window.INFIVERSE_SNIPPETS = {
-  "ref": "d78a0a3215034ad30ccbd569e153cbcf6e5534df",
-  "ref_date": "2026-10-09T20:17:26+08:00",
+  "ref": "654db273f3f6ddbf5d83dfe0b3725dabf0dbce31",
+  "ref_date": "2026-10-18T02:25:07+08:00",
   "generated_by": "website/scratch/run_snippets.js",
@@ -40,7 +40,7 @@
     "JavaScript": {
       "cmd": "node --version",
-      "out": "v24.20.0"
+      "out": "v22.22.1"
     },
```

**三行文本变了。它们是两件相反的事：**

| 格 | HEAD 上那份（`d78a0a3`，2026-10-09） | 今天重跑（`654db27`，2026-10-18） | 该被怎么处置 |
|---|---|---|---|
| `ref` | `d78a0a3215034ad30ccbd569e153cbcf6e5534df` | `654db273f3f6ddbf5d83dfe0b3725dabf0dbce31` | **设计** —— 它记着自己是**哪棵树**生的 |
| `ref_date` | `2026-10-09T20:17:26+08:00` | `2026-10-18T02:25:07+08:00` | **设计**，同上 |
| `toolchain.JavaScript.out` | `"v24.20.0"` | `"v22.22.1"` | ★★ **缺陷** —— 它记着自己是**哪台机器**量的 |

★★★ **判据**：**这三行在 `diff` 里长得完全一样**（都只是「两行文本变了」），
而**该被怎么处置相反** —— 因为**没有哪一栏写着「这一格是环境的函数」**。
★ 产物顶部那三栏里，`generated_by` 只声明了「**谁**生成的」，
**没有声明「在哪台机器上生成的」**：
```
$ node -e "const fs=require('fs');const w={};new Function('window',fs.readFileSync('website/data/snippets.js','utf8'))(w);\
  console.log(Object.keys(w.INFIVERSE_SNIPPETS).filter(k=>/host|machine|env|plat/i.test(k)))"
[]
```

---

## 三 · 那个 `v22.22.1` 是哪个 `node` —— 两个版本都是对的

```
$ which node          →  /usr/bin/node
$ /usr/bin/node --version  →  v22.22.1
```
★ 而 **HEAD 上那份记的是 `v24.20.0`**。
⇒ **HEAD 上那份是在另一个 `node` 上量的。**
★ 两者**都不是错**：**它们量的不是同一个 `node`。**
★★ 这一格正是**页面上承诺最硬的那一格** —— 跨语言比对页承诺「每一行要有一个能跑的最小片段
**＋ 它自己的原样输出**」。**`v22.22.1` 与 `v24.20.0` 就是那一格的读数。**

---

## 四 · ★ 五格里有几格是环境的函数：**五格全是**，而今天只有一格漂了

```
toolchain.Inimerse   out = "inimerse 0.5.2"
toolchain.Python     out = "Python 3.14.4"
toolchain.JavaScript out = "v22.22.1"
toolchain.Rust       out = "rustc 1.99.0 (b940084d7 2026-09-28)"
toolchain.C          out = "cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
```
★ **五格的取值全由「这台机器上装了谁」决定**，只有 `Inimerse` 那一格是本仓构建出来的。
★★ **今天恰好只漂了一格，不是因为它不漂** —— 是因为这台机器上另外四个可执行文件的版本
恰好与 2026-10-09 那台相同。⇒ **一份「今天只漂了一格」的读数，
不能读成「只有一格会漂」。**

**生成器是确定的**（这一条得单独证明，否则上面全部不成立）：
```
$ cp website/data/snippets.js .build/snip.a && node website/scratch/run_snippets.js && cp … .build/snip.b
$ cmp -s .build/snip.a .build/snip.b   →  逐字节相同   ⇒ 生成器确定
$ cmp -s .build/snip.head .build/snip.b  →  不同        ⇒ HEAD 上那份不是今天的产物
```
★ 这两条要**分开**：**「生成器确定」与「HEAD 上那份等于今天的产物」是两件事。**

---

## 五 · 谁守着它 —— **没有人守着它的内容**

`check_citations.js` 里对 `snippets` 的判定只有一处：
```
check_citations.js:3115:    path.join(HERE, 'run_snippets.js'), path.join(HERE, 'run_tutorial.js')];
```
⇒ 那是**「文件存在」**这一类的判定 ⇒ **没有人守着这一格的【内容】**。
★★ 这与「第五档」是同一族：**文件在树上、而它的内容没有任何守卫看着。**

---

## 六 · 顺手清掉的两件（我自己丢下的）

`e2.txt`（100 B）与 `o2.txt`（50 B）躺在**工作树根**，`mtime` 均为 **Oct 17 22:29**，内容是
stdout/stderr 的捕获（`o2.txt` 头一行 `[mod] utils loaded`；`e2.txt` 头一行
`[infiverse mod] loaded (16 worlds, world 0 active)`），
**全仓提到这两个文件名的源码文件 = 0** ⇒ 它们是**某次 shell 重定向**（`> o2.txt 2> e2.txt`）
写进当前目录的，**不是任何生成器写的**（`run_tutorial.js` 的产物全部落在
`website/scratch/.build/tut/`，见它 `:29`、`:133`、`:144`、`:195`）。
★ 我**删了**它们（未跟踪、无引用），删后 `git status --porcelain` = 空 ✓。
★ 顺带量到：**工作树根有一个 `imai_sys.txt`，它是【受跟踪的】**。

---

## 七 · 这一件的形状与已裁定的三族的关系

| 族 | 特征 | 本件 |
|---|---|---|
| 第五档 | 文件在磁盘上、不进任何索引 | 不是 —— 本件那个文件**在索引里** |
| 「射程错的签名是分母变大」 | 数错了对象 | 不是 —— 本件没有数对象 |
| **本件** | **受跟踪的产物，它的内容有一格是环境的函数，而产物自己不声明这件事** | ★ |

⇒ 判据（请裁）：
**一份【受跟踪】的产物，如果它的某一格是【哪台机器】的函数，那它必须以产物里的某一栏把它标出来；
否则一次重生成会产生一份与 `ref` 漂移【长得一样】的 diff，而两者该被怎么处置相反。**

---

## 八 · 四件套（本件没有改任何受跟踪的产物内容）

```
pwd    = /home/sakiko/inimerse/.worktrees/website-ledger-readat
HEAD   = 654db273f3f6ddbf5d83dfe0b3725dabf0dbce31（本件提交前）
读法   = 工作树文件
第五件 = 开工那一刻 origin/main = 1a6ad2d12e719bfa5d8a54c34f4b79f15834de55
count(base..HEAD) = 2（分支点 c72b202）
git diff --stat c72b202 HEAD -- docs/  =  空 ✓
★ 生成器跑过三次，`website/data/snippets.js` 已在提交前 `git checkout --` 还原；
  本件唯一写进树里的是这一份 .md
★ 本件是【回答与普查】，不是动工：没有动生成器，没有动那一格，没有建任何守卫
```
