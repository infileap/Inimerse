# 分派事故记录：一次由 `$NF` 误读引起的越权派活

- **认领者**：`ivory-zephyr`（协调会话）
- **分支**：`stream/board-status-refresh`
- **测量 ref**：`d0ad1c1`
- **本文件的位置说明**：初版写在 `/home/sakiko/dispatch-incident-2026-10-21.md`，即**仓库之外、无版本控制**的目录。移入这里的理由正是本文 §9 第 8 例：**工作区外既无版本控制也无 review，其他会话读不到**，一份「共享记录」放在那里就失去了作用。原件已删除，此处为受管版本。

测量 ref：`d0ad1c1`。本文记录 2026-10-21 的一次分派失误及其善后，供后续会话避坑。

## 1. 发生了什么

我（`ivory-zephyr`）受人类委托「继续分配任务给其他会话」，读了 `docs/BOARD.md` 四条未完成行的认领列，判定它们无主，于是向四个会话派了活：

| 派出对象 | 任务 | 我当时的依据 |
|---|---|---|
| `amber-badger` | `lang-migration` 补逐 stage 记录 | 「认领为空」 |
| `lively-kestrel` | `win-source-attribution` 判据⑤ 读数重取 | 「认领为空」 |
| `noble-orchid` | `infiverse-handoff` 判据② 取证 | 「认领为空」 |
| `fair-lantern` | `decfy-design` 判据② 括号 | 「认领为空」 |

**那个依据是假的。**

## 2. 根因：`$NF` 在带尾随分隔符的行上恒为空串

```bash
# 我用的（错）
sed -n '285p' docs/BOARD.md | awk -F'|' '{print $NF}'      # ⇒ 空串
# 正确的
sed -n '285p' docs/BOARD.md | awk -F'|' '{print $(NF-1)}'  # ⇒ `vivid-anchor`
```

BOARD 的行形如 `| 状态 | slug | 任务 | 写域 | 判据 | 认领 |`，**末尾还有一个 `|`**，于是 `$NF` 取到的是最后一个分隔符之后的空字段，而认领列在 `$(NF-1)`。

重读后四条**全部有主**：

| 行 | slug | 真实认领 |
|---|---|---|
| 98 | `lang-migration` | `stream/lang-migration` |
| 101 | `infiverse-handoff` | `stream/infiverse-handoff` |
| 145 | `decfy-design` | 协调者 |
| 285 | `win-source-attribution` | `vivid-anchor` |

### 这条坑的第二个面（`lively-kestrel` 补）

改成 `$(NF-1)` **还不够**：若某行末两列之间恰好是真空单元格，`$(NF-1)` 也取到空串，**而那次空串是真值**（该任务确实没写该字段），与 `$NF` 的结构空串在输出上同形。

⇒ **记录用的规则**：先用表头数出列数，列数与表头不符的行**单列成档**；**空串不得直接当「未认领」**。

## 3. 同轮的另外两个错误

**错误二：空转的「验证」。** 我说「三条任务均明令禁碰 `docs/BOARD.md`」，验证命令是

```bash
grep -c 'docs/BOARD.md' <<< "$(echo amber kestrel orchid)"
```

输入里根本没有文件内容，什么都没证明。这是本仓反复记的那类「引的证据与它证的命题不是同一个量」。

**错误三：分支存在性检测。** 我先用 `git rev-parse --verify --quiet <name>`，该命令在名字不是引用时会把它当**路径**解析并返回 rc=0，我据此说「分支不存在」；随后又据 `git log` 空输出再错一次。

**权威判据**：`git show-ref --verify --quiet refs/heads/<name>`。

（用这个一查，三个 worktree 与分支**确实被建出来了**。）

## 4. 善后

四个会话全部收到撤回。三个已建的分支与 worktree——`stream/lang-migration-stage-record`、`stream/infiverse-handoff-anchors`、`stream/decfy-design-criterion-fix`——经核**全为空**（HEAD 与 `main` 同为 `d0ad1c1`、零提交、零脏文件、零未跟踪），已 `git worktree remove --force` + `branch -D` 删除；远端从未推送。主工作区 0 行脏。

`stream/win-source-attribution-recount` 从未被创建（`kestrel` 停在询问那一步，没有擅自开工）。

## 5. 唯一有价值的产出：`lively-kestrel` 的四条读数更正

它没有盲从，而是先核前提，抓出我给的数里的四处毛病，**全部成立**：

| # | 我给的 | 更正 |
|---|---|---|
| 1 | `077997a` 上 `.md` 是 92，今天 171 | **跨 ref 比 `.md` 必须 `-z`**：`git ls-tree -r --name-only` 默认 C 式引号，含非 ASCII 的路径末尾是 `md"` 而非 `md`，被 `grep '\.md$'` 漏掉 10 个。`-z` 口径下 `077997a` 是 **102**，今天 **181**，新增 **79**（不是 89） |
| 2 | 新增 79 的分解 61／16／1 | **61／16／1／1**——漏了 `website/README.md`（顶层，非 scratch） |
| 3 | 「`eef6dba` 加了 61 个 `.md`」 | **假**。逐文件 `git log --diff-filter=A` 溯源：61 个分散在 **60 个引入提交**里（每个最多 2 个），`eef6dba` 只加 2 个；另 1 个 `website/scratch/README.md` 无 A 记录 |
| 4 | 判据⑥「被 skip 规则跳过」 | **措辞不准**。`tools/check_doc_paths.py` 的 `scan_files()` 是 `os.listdir` **非递归**，只列 `docs/` 与 `future/` 顶层 ⇒ 该文件**根本没进候选集**，不是「进了又跳过」 |

第 1 条尤其要紧：没有它，我会把一个**多出 10 个不存在文件的假分母**写进交付。

## 6. 由此确立的两条规则

1. **派活前必须先证明你读过那张表**，而不是读过你以为是那张表的东西。用 `$(NF-1)` 且核对列数；空串不等于「无主」。
2. **跨 ref 数文档分母一律 `-z`**——门禁的三个 checker（`check_links.py:80`、`check_doc_paths.py:99`、`check_text_integrity.py:100`）全部用 `-z`，所以门禁印的永远是 `-z` 口径的数；拿不带 `-z` 的命令去比，天然差 10。

## 7. 遗留（未解决，需人类或协调者裁定）

- `docs/BOARD.md:285`（`win-source-attribution`）的认领者 `vivid-anchor` **是一个真实存在过的 handle**，不是标题。取证（此前我判错过一次，见 §8）： [obs: d0ad1c1]

  | 查法 | 结果 |
  |---|---|
  | `.dsh/conversation-link/state.json` 的 `handles[session-b58201d9-0a40-4971-ab4f-e912ed4756dc]` | **`vivid-anchor`** |
  | 该会话的投影 `title` | `agent3 [已交接]` |
  | `.dsh/sessions/*/session-b58201d9…`（会话目录与日志） | **不存在** |

  ⇒ 该 handle 属于**已归档/已清理的 `agent3` 会话**：它在 handle 账本里仍有记录，但会话实体已不在盘上。

  **「主不在」与「任务被转交」是两件事，后者才是决定性的**（顾问指出，这一步必须另查）。已查：

  | 查法 | 结果 |
  |---|---|
  | `ls .dsh/handoff/*.md \| wc -l` | **16 个包**（权威计数；先前我写过「12 个」和「15 个」，两个都是错的手写数，见 §9） |
  | 16 个包里搜 `win-source-attribution` | **0 命中** |
  | 改搜**可能替代任务名的写法**（顾问提醒：转交未必复述任务名） | `L285` 0、`:285` 0、`判据⑤` 0、`判据五` 0、`holders-probe` **2**、`attribution` 0 |
  | 那 2 处 `holders-probe` 的语境 | `agent3` 的**工作目录**，出现在「改动过的文件」清单（`wt-051/holders-probe/wu5.py` 等）与一次交件的 `pwd`——**不是转交声明** |
  | `agent3` 自己的交接包 `2026-10-18T10-53-24-ed4756dc.md` | 在册；`原会话：session-b58201d9…（agent3）`，即该 handle 的会话 |
  | 包内提及 `vivid-anchor` 的内容 | 只作为**收件人/发言人**（`[your handle is vivid-anchor]`），没有一句把 `win-source-attribution` 转给别人 |

  ⇒ 结论（**收紧措辞**）：**未发现转交**，不是「无转交记录」。该行仍登记为 `vivid-anchor` 的写域，而那个会话已归档 ⇒ 处于「有主、主已归档、未发现转交」的状态。判据⑤ 读数过期（102/586 → 181/618 等）属实，但**改派与否不是我能决定的**，需人类或协调者裁定。

  **注意措辞边界**：「15 个交接包里 0 命中」证明的是**这些包没有转交它**，不等于「从未有过任何转交」——转交也可能写在会话正文或别的信里，而我只能核我读到的载体。

## 8. 我在这一轮犯的第四个同类错误：把 handle 当标题查

我先前写下「`vivid-anchor` 这个会话不存在」，取证方式是**扫每个 session json 的 `title` 字段**。而 `vivid-anchor` 是 **handle**，不是标题——该会话的标题是 `agent3 [已交接]`。标题里搜不到它，与它是否存在无关。

**这一轮的四个错误共享同一个形状**：

| # | 用错的读法 | 后果 |
|---|---|---|
| 1 | `awk -F'\|' '{print $NF}'` | 把有主当无主，越权派了四份活 |
| 2 | `grep -c 'docs/BOARD.md' <<< "$(echo …)"` | 空转验证，什么都没证明 |
| 3 | `git rev-parse --verify --quiet <name>` | 名字非引用时当**路径**解析并返回 0，误判分支不存在 |
| 4 | 扫 `title` 字段找 **handle** | 误判一个存在的 handle 不存在 |

**共同教训**：这四次都不是「算错」，而是**用一个不回答该问题的工具去回答该问题，然后把它返回的空当成「没有」**。空输出、rc=0、空串——三者都可能是**读法的产物**，不是事实。

正确的 handle 来源是 `.dsh/conversation-link/state.json` 的 `handles` 映射；正确的分支存在性判据是 `git show-ref --verify --quiet refs/heads/<name>`；正确的 BOARD 列读法是 `$(NF-1)` 且核对列数与表头。
- 另外三条未完成行（`:98`、`:101`、`:145`）同样有主：前两条归各自的 `stream/*` 分支，第三条归协调者。


### 第 8 例（险而未发）：`>` 的重定向目标从未验证

我写 `dispatch-incident-2026-10-21.md` 与更早的 `handoff-lane-map.md` 时，**都没有验证过目标路径落在哪个工作区**。顾问指出这一点后我补查：

```
$ cd /home/sakiko && git rev-parse --show-toplevel
fatal: not a git repository (or any of the parent directories): .git
$ 逐层向上找 .git → 到 / 都没有      ⇒ /home/sakiko 不是 git 工作区
$ git -C /home/sakiko/inimerse status --porcelain | wc -l
0                                    ⇒ 主工作区未被影响，HEAD 仍 d0ad1c1
```

**这次没有后果，但我没有靠检查得到这个结论，是靠运气。** 若当时 `cd` 在某个 `.worktrees/*` 里、或相对路径落进主工作区，就会真的留下未跟踪文件，破坏我自己反复强调的「main 零改动」。

**它属于同一族**：把「我以为的路径」当成「实际的路径」。

**★ 这条的护栏我原本写反了（顾问指出，成立）**：我写的是「落在工作区外才允许随手写」——**恰好反了**。工作区外既无版本控制、也无 review 与备份，**那是最该被拦住的一端**，不是被放行的一端。这份档案当时就落在 `/home/sakiko`（无仓库目录）里，其他会话（`kestrel`／`amber-badger`）**无法从仓库里读到它**，它的「共享记录」作用因此打折。

**改正后的护栏**：写文件前先 `git rev-parse --show-toplevel` 确认目标**在预期工作区内**；**落在工作区外必须显式说明理由，或改用受管位置**（仓库的 docs 分支、或既定的 notes 仓库），不得默认放行。

**并且这条有实例**：`noble-orchid` 把两份证据副本存在 `.scratch/handoff-anchors-bak/`，而 `.scratch/` 被 `.gitignore:96` 覆盖——**同样活在「无版本控制」的位置**。它自己也报了这一条（「把证据保存在一个会被环境清掉的路径上」）。⇒ 两份副本已由我抢救到 `/home/sakiko/handoff-anchors-salvage/`，但它们仍不在版本控制里，正式落盘应由认领方 `stream/infiverse-handoff` 决定。

## 9. 追加两个同形错误（第 6、7 例）

| # | 用错的读法 | 后果 |
|---|---|---|
| 6 | `grep -l … \|\| echo '  12 个包全部 0 命中'` —— **12 是我手写进 echo 的字面量** | 结论里的包数与下一句列出的清单不符；实际是 **16 个**（`ls .dsh/handoff/*.md \| wc -l`）。这正是「把硬编码值当成测量值」 |
| 7 | 用**任务名**一个串去证明「没有转交」 | 转交未必复述任务名。改搜 `L285`／`:285`／`判据⑤`／`判据五`／`holders-probe`／`attribution` 六个写法后，`holders-probe` 命中 2 处（是 `agent3` 的工作目录，非转交声明），其余 0 |

**第 7 例的教训比前六例更值钱**：前六例是「工具的返回形状与问题不匹配」，这一例是**问题本身问窄了**——「搜不到任务名」与「没有转交」是两件事，而我把前者当成了后者。正确写法是搜**所有可能指代它的方式**，并**逐处读语境**，然后说「未发现转交」。

## 10. 一句话总结这七例

七次都不是算术错误，而是**把某个能表示「没有」或「已知」的值，当成了对当前问题的回答**：空串、空输出、`rc=0`、`-1`、手写字面量、以及「搜不到」本身。

**可执行的护栏**（三条）：

1. 凡 `find`／`grep`／`rev-parse` 这类可能返回哨兵值的调用，**使用前把哨兵单独判一次并打印**；
2. 结论里的每个数字，**必须能指到一条产出它的命令**，不许出现在 `echo` 的字面量里；
3. 关键结论**至少两个独立来源**：`vivid-anchor` 这件事我核了四层（handle 账本 → 投影标题 → 会话目录 → 交接包多写法搜索）才敢下结论，单看任何一层都会引向错误答案。

## 11. 第 9 例（`amber-badger` 自报，2026-10-21）：`git checkout -- <file>` 的右端是 HEAD，不是 `origin/main`

**这一例不是我的观测，是 `amber-badger` 会话自己报上来的**，我复核后收进来。文件名里的 `eight` **不改** —— 改动只追加；改标题等于把已经引用过这个路径的地方全部弄失效，而那个代价比标题里少一个数更大。下面这篇的正文其实早就数不到 8（§9 说「追加两个」、§10 说「这七例」、标题说 eight），本节的编号按**实际顺序**取 9。

**发生了什么。** 我建议它「把自己的改动还原，腾出主工作区」。它执行了 `git checkout -- CMakeLists.txt tools/gate.sh tools/check_frozen_identity.py tools/check_frozen_identity.test.py`。**它的 HEAD 当时落后 `origin/main` 7 个提交**，于是这几条命令把文件还原成了**旧版**，而不是主干版：

```
还原前  tools/check_frozen_identity.py       4fb467d8   （= origin/main ✓）
还原后  tools/check_frozen_identity.py       278206c6   ← 退回到它自己 dbb3428 的 3 格旧版
同样    tools/check_frozen_identity.test.py  9ba28e68 → a17fac44
```

**它没有丢任何东西**（新版一直在 `origin/main` 里），随后 `git merge --ff-only origin/main` 修正，五件逐字节等于远端、`porcelain=0`。

**形状。** 与前八例同形，而且是同一族里最隐蔽的一支：**`git checkout -- <path>` 里的 `<path>` 不是「文件的内容」，是「索引里的那份内容」**；不带起点时，起点是 **HEAD** —— 不是 `origin/main`，也不是「我刚才看到的那份」。我给它的措辞「把改动还原」在**落后**的树上方向上正好相反：我想要的是「取主干那份」，它执行出来的是「取我脚下这份」。

**护栏（增补一条，与 §10 的三条并列）。**

4. 凡「把 X 变成 Y 的样子」的 git 命令，**必须把 Y 写成显式 ref**（`git checkout origin/main -- <paths>`、`git restore --source=origin/main -- <paths>`），**不能写成不带起点的 `--` 形式**；并且执行前先 `git rev-list --count HEAD..origin/main`，**非 0 时先 ff、再谈还原**。

**为什么它值一条。** 前八例里没有一条是「命令做对了它被要求做的事，而那个要求本身是错的」。这一例是：**我的意图对、措辞错**，而措辞恰好落在一条会把工作区推向旧版的命令上。⇒ 协调者给别的会话下命令时，「还原」「同步」「对齐」这类**有方向**的动词，必须连着 ref 一起说；只要 ref 不在句子里，对方就只能猜起点，而猜出来的起点是 HEAD。

**诚实边界。** 本节全部事实来自 `amber-badger` 会话的自报（含那两个 blob 前缀与「`porcelain=0`」），**我没有独立复现它还原前的那棵树**（它已经 ff 修正过了）；我独立确认的只有一件事：`f5c7369` 上那五件与 `origin/main` 逐字节相同、全量门禁 16/16。
