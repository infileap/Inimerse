# 乙⁷ 读数：把「仪器把自己算进载体」做成可判

判据见 `website/scratch/self_carrier_criteria.md`（C1–C6）。本文件是**读数**。
探针 = `website/scratch/self_carrier_probe.js`，逐字输出 = `website/scratch/self_carrier_transcript.md`。

---

## 一、逐字回答：那「12 份载体」里有几份是尺子本身或尺子的同类

★★★ **先纠正分母本身：「12」不是任何一条命令的输出。**

让癸⁶ **把它读的载体逐份印出来**（内存里插桩，尺子源码零字节改动）⇒ 它自印 `CARRIERS=18`：

```
website/scratch/check_citations.js              website/scratch/run_snippets.js
website/scratch/kill_boundary_probe.js          website/scratch/run_tutorial.js
website/scratch/kill_boundary_rerun.js          website/scratch/snippets/caught-error-payload.js
website/scratch/render_check.js                 website/scratch/snippets/div-zero-float.js
website/scratch/report_diff.js                  website/scratch/snippets/div-zero-int.js
website/scratch/ruler_geng6.js                  website/scratch/snippets/index-out-of-range.js
website/scratch/ruler_gui6.js                   website/scratch/snippets/int-div-truncation.js
website/scratch/ruler_jig6_recon.js             website/scratch/snippets/manifest.js
website/scratch/ruler_ren6.js                   website/scratch/snippets/mod-zero.js
```

三条枚举并排：

| 枚举 | 数 |
|---|---|
| `git ls-files -- 'website/scratch/*.js'`（`*` **跨目录**） | **18** |
| 同一条命令，只取顶层 | **11** |
| 本会话报告里反复引用的「12 份载体」 | **12 —— 不是任何一条命令的输出** |

⇒ **答案：真实载体 18 份，其中 `ruler_*.js` 一族 = 4 份**，逐份是
`ruler_geng6.js`、`ruler_gui6.js`、`ruler_jig6_recon.js`、`ruler_ren6.js`。

★ 另外 14 份里还有 **2 份是仪器**（`kill_boundary_probe.js` / `kill_boundary_rerun.js` —— 我上一轮（甲⁷）自己落的探针），
但它们**不在 `ruler_*.js` 这一族**，按派件的字面口径不计入「尺子本身或尺子的同类」。

### 1.1 ★★★ 「12」不是「当时对、后来变了」—— 写下它的那一刻就不对

「12 份载体」在受跟踪文件里出现 4 次，其中 3 次是**带命令的断言**：

| 文件 | 行 | 逐字 |
|---|---|---|
| `website/scratch/two_rulers_side_by_side.md` | 9 | **载体（被扫的同一份对象）** = `website/scratch/` 下 **12 份受跟踪 `.js`** |
| `website/scratch/third_ruler.md` | 8 | **载体** = `website/scratch/` 下 **12 份受跟踪 `.js`**（与己⁶／庚⁶ 同一份） |
| `website/scratch/fourth_ruler.md` | 12 | **载体** = `website/scratch/` 下 **12 份受跟踪 `.js`**（`git ls-files -- 'website/scratch/*.js'`，与 己⁶／庚⁶／壬⁶ 逐字同一份） |

在**写下每一句的那一笔自己的树**上，穷举候选命令：

| 那一笔 | 写下它的件 | `ls-tree -r … \| grep -c '\.js$'`（递归） | 只顶层 `.js` | 全部文件（不分扩展名） |
|---|---|---|---|---|
| `a4653ac` | 辛⁶（`two_rulers_side_by_side.md`） | **14** | 7 | 66 |
| `7752ec2` | 壬⁶（`third_ruler.md`） | **15** | 8 | 69 |
| `0931aad` | 癸⁶（`fourth_ruler.md`） | **16** | 9 | 72 |

⇒ ★★★ **三个不同的树、五条候选命令，没有一条给出 12。**
「12」不是「当时是对的、树长了才不对」——**写下它的那一刻就不对**。

⇒ **判据（署我名）：一条把【取得它的命令】与【它的值】并排写下的句子，可以整段是散文 ——
于是它的值错了也不红、树长了也不红、被下一件逐字继承也不红。**
`fourth_ruler.md:12` 里唯一仍然为真的是那句「与 己⁶／庚⁶／壬⁶ **逐字同一份**」
（四把今天确实读同一份载体）—— **而它旁边那个数已经错了三轮。**


## 二、★★★ 两个分母并排（不许合、不许互相替代）

| 分母 | 含 `ruler_*.js` | 排除 `ruler_*.js` |
|---|---|---|
| **癸⁶ 的分母** | **372** | **366** |
| **四把并集的分母** | **465** | **459** |

- 取得癸⁶ 分母的命令：`node website/scratch/ruler_gui6.js .`（载体 = 上面那 18 份）
- 取得并集的命令：`node website/scratch/self_carrier_probe.js .`（四把 = `ruler_jig6_recon.js` / `ruler_geng6.js` / `ruler_ren6.js` / `ruler_gui6.js`，载体同一份 18 份）
- ★ 两个数**今天不同**（372 vs 465），**排除后也不同**（366 vs 459）—— 差距 = **93** 处，是另外三把独有的覆盖。

## 三、★★★ 逐字回答：排除之后两处目标的键会不会变

| 处 | 含 `ruler_*.js` | 排除 `ruler_*.js` | |
|---|---|---|---|
| `check_citations.js:87` | `0110` | `0110` | **键不变** ⇒ 仍档 ② |
| `render_check.js:661` | `0001` | `0001` | **键不变** ⇒ 仍档 ① |

（键序 = 己⁶′/庚⁶/壬⁶/癸⁶）

## 四、★★★ 排除之后逐把（含 / 排除）

| 尺子 | 含 | 排除 | |
|---|---|---|---|
| 己⁶′ | 49 | 49 | **一格没动** |
| 庚⁶ | 105 | 105 | **一格没动** |
| 壬⁶ | 60 | 60 | **一格没动** |
| 癸⁶ | 372 | **366** | **−6** |

⇒ **判据 C3 成立：把 `ruler_*.js` 排除，只有一把尺子的分母动了。**
「仪器把自己算进载体」**不是四把尺子的共同性质，是【癸⁶ 单独的性质】** ——
它是按**数据流**判的那一把（追踪外部数据变量），而尺子文件自己也在读外部数据。

## 五、排除后消失的 6 处（逐处）

```
ruler_geng6.js:33       ruler_gui6.js:59        ruler_gui6.js:145
ruler_jig6_recon.js:38  ruler_ren6.js:28        ruler_ren6.js:66
```

★ 6 处**全部落在 `ruler_*.js` 自己身上**，非 ruler 载体一处没掉 ✓。

## 六、三个对照

| 方向 | 读数 | |
|---|---|---|
| **该动** | 癸⁶ 372 → 366（−6） | ✓ 降了 |
| **该不动** | 非 ruler 载体的命中数 含=[49,105,60,366] 排除=[49,105,60,366] | ✓ 逐把相同 |
| **空集** | 把全部载体排除 ⇒ 剩余 **0** | ✓ |

## 七、★★★ 两处我自己在这个过程里踩到的坑（都记下来）

### 7.1 一个正则把「四把一致」印成了「并集 = 一把」
四把的输出**格式不是同一种**：
- 己⁶′／庚⁶／壬⁶ = `check_citations.js:67<TAB>[D]<TAB>check(...)`（**短名**、**多列**）
- 癸⁶ = `website/scratch/check_citations.js:26<TAB>`（**全相对路径**、**尾随制表符**、**无多列**）

我用**一个**正则 `/\.js:\d+\t?$/` 读四把 ⇒ **己⁶′=0 ／ 庚⁶=0 ／ 壬⁶=0 ／ 癸⁶=372** ⇒
「四把并集 = **372**」—— **等于癸⁶ 一把**，而它在报告里读起来像「四把一致」。

**修法（C2）**：逐把解析器 ＋ **先用尺子自印的 `DENOM` 对账**。对账后：
`己⁶′ 49/49 ✓`、`庚⁶ 105/105 ✓`、`壬⁶ 60/60 ✓`、`癸⁶ 372/372 ✓` ⇒ 并集 **465**。

### 7.2 空集对照的谓词写反了，而红印出来的是那个总数
上一版空集对照数的是**落在载体名单里的**（谓词写反），于是印出
「剩余 = **459** ⇒ ★ 红」—— 而 459 **恰好等于并集总数**。
真实情况是：459 处**全部**被载体名单命中（这正是名单匹配上了的证据），移除后剩余 **0**。

**修法**：数**移除之后剩下的**，并且**自证名单匹配上了**
（`并集 459 处里落在载体名单上的 = 459`）—— 否则「0」分不清是「移除的 0」还是「没匹配上的 0」。

⇒ **判据（署我名）：空集对照若印出的数【恰好等于并集总数】，那不是「仪器不归零」，那是【谓词写反了】。两个方向都印得出一个数，而只有一个方向印得出那个总数。**

## 八、分母的历次（★ 附 otter-letter 131 一.1 的要求）

| 第几次 | 癸⁶ 分母 | 动了什么 |
|---|---|---|
| 癸⁶ 初版 | **362** | —— |
| 我往 `ruler_gui6.js` 加两行之后 | **364** | ★ **仪器改了自己** |
| 甲⁷ 落两个探针之后 | **372** | ★ **我自己的新产物成了载体**（`kill_boundary_probe.js` / `kill_boundary_rerun.js`） |
| 排除 `ruler_*.js` | **366** | 本件 |

⇒ **癸⁶ 的分母不是语料的函数，是它自己的函数**（otter-letter 131 一.1 已裁）。
本表以后每一行都必须带「第几次运行」。
