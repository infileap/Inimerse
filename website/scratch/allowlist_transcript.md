# 戊⁷ 逐字留档：允许表探针的四次运行 ＋ 三个分类脚本

本文件是【输出】，不是【源码】。探针源码在 `website/scratch/allowlist_probe.js`（受跟踪）。
三个分类脚本在 `website/scratch/.build/`（第五档，gitignored）—— **那三个数可以重读，不能重算**。

---

## 运行 1：第一版探针（R4 的谓词写的是「不在只读表」，而规则印的是「非只读」）

```
=== 判据 F0：载体与分母
  分母 B（带命令的处，承丁⁷ 的定义）  = 583  ⇒ 丁⁷ 是 572 ★ 差 11
  ★ 本件的分母（⑤ 我的射程之外）      = 188  ⇒ 丁⁷ 是 239 ★ 差 -51
=== 判据 F1：允许表逐条 —— 每条挡下的处数（一条命令可以同时命中多条，所以两个数都印）
  R1  含 shell 元字符            =  136
  R3  git 后面没有子命令            =    1
  R4  git 子命令非只读             =    1   ★★★ 真值是 57 —— 见运行 3
  R5  本探针不跑这一族               =   56
  ★ 命中总数（有重叠）= 194   而分母 = 188   ⇒ 差 6 = 一条命令命中多条规则的处数
  --- 按【首次命中】划分（互斥，和必须 = 分母）
  R1 = 136 / R3 = 1 / R5 = 51
  ★ 和 = 188  ⇒ = 分母 239 ✓    ★★★ 比的是写死的 239，不是分母本身（判据 F1.3）
=== 判据 F2：代理量
  代理上跑得动（可执行名能解析、git 有子命令）= 187
  代理上跑不动（可执行名解析不到）              = 1
  其中【会改工作树】的 git = 1   ★★★ 真值是 47 —— 见运行 3
=== 判据 F3：四档
  ① 对象的性质（可执行名／子命令不存在）  = 1
  ② 允许表的性质（跑得动，是我不许）      = 1
  ③ 提取器的性质 = 见下（逐处读）
  ④ 判不出 = 见下（逐处读）
=== ★ R1 逐处（含 shell 元字符）—— 元字符是哪一个
  元字符 "\\\\"   = 38
  元字符 "("    = 33
  元字符 "|"    = 31
  元字符 "<"    = 27
  元字符 "$"    = 4
  元字符 ";"    = 2
  元字符 ">"    = 1
  --- 前 25 处逐字（cmd 与它所在的那一行）
  docs/AUDIT.md:528   cmd=`grep -c 'add_test(' CMakeLists.txt`
      **计数**：①–③ 的两次修复共 `grep -c 'add_test(' CMakeLists.txt` **108 → 110**（`tools/gate.sh` 的 `EXP_CTEST` 那一行同步 **110**）；④ 这条回归
  docs/AUDIT.md:616   cmd=`sed -n 's/^  DIVERGE *\\([0-9]*\\)$/\\1/p'`
```

★ 运行 1 的三处缺陷（逐条已写进判据）：
  (a) `R4` 的谓词写的是「git 子命令**不在只读表**」，而规则印的是「git 子命令**非只读**」⇒ 56 处静默落空（判据 F15）
  (b) 互斥那一份的「和 = 分母 ✓」比的是一个**写死的 239**，不是分母本身（判据 F1.3）
  (c) 会改工作树的 git 只报 1，因为只读白名单把「在不在表上」当成了「它做什么」（判据 F16）

---

## 运行 2：认引号之后（判据 F7）

```
=== 判据 F0：载体与分母
  分母 B（带命令的处，承丁⁷ 的定义）  = 583  ⇒ 丁⁷ 是 572，差 11
  ★ 本件的分母（⑤ 我的射程之外）      = 188  ⇒ 丁⁷ 是 239，差 -51
=== 判据 F1：允许表逐条 —— 每条挡下的处数
  --- 命中（有重叠）
  R1  含 shell 元字符            =  136
  R3  git 后面没有子命令            =    1
  R4  git 子命令非只读             =    1
  R5  本探针不跑这一族               =   56
  ★ 命中总数 = 194   分母 = 188   差 6 = 一条命令命中多条
  --- 按【首次命中】划分（互斥）
  R1 = 136 / R3 = 1 / R5 = 51
  ★ 和 = 188  ⇒ = 分母 188 ✓   ★ 这一次比的是分母本身
=== ★★★ 判据 F7：R1 那条规则，认引号 vs 不认引号
  R1（不认引号，丁⁷ 那一版）挡下  = 136
  R1（认引号，只算引号外的元字符）= 59
  ★ 差 = 77 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】
  --- 不认引号时，元字符分布
    "\\\\"   = 38   "("    = 33   "|"    = 31   "<"    = 27   "$"    = 4   ";"    = 2   ">"    = 1
  --- 认引号后，元字符分布
    "|"    = 31   "<"    = 22   "("    = 5    "$"    = 1
  --- 被认引号【放行】的那批逐字（前 20）
    docs/AUDIT.md:528   cmd=`grep -c 'add_test(' CMakeLists.txt`
    docs/AUDIT.md:616   cmd=`sed -n 's/^  DIVERGE *\\([0-9]*\\)$/\\1/p'`
    docs/AUDIT.md:808   cmd=`grep -c "EXPR_ARRAY\\|EXPR_SET\\|EXPR_DICT"`
    docs/AUDIT.md:1438   cmd=`ctest -R "incremental|dep|compile|selfhost|cli_"`
    docs/AUDIT.md:2677   cmd=`grep -c 'add_test('`
    docs/AUDIT.md:3370   cmd=`grep -c '\\$REPO_ROOT/' tools/gate.sh`
    docs/AUDIT.md:3522   cmd=`git -C "$REPO_ROOT" diff --name-only "$range"`   ★★ 双引号里的 $ 仍会展开（判据 F14）
    docs/AUDIT.md:3749   cmd=`python3 -c "import os; print(os.path.splitext('.gitignore'))"`
```

---

## 运行 3：修好 F12 / F15 / F16 之后（本件报的数）

```
=== 判据 F0：载体与分母
  分母 B（带命令的处，承丁⁷ 的定义）  = 583  ⇒ 丁⁷ 是 572，差 11
  ★ 本件的分母（⑤ 我的射程之外）      = 245  ⇒ 丁⁷ 是 239，差 6
  ⇒ 差的原因见读数第三节（丁⁷ 的产物成了载体）。本件不压回。

=== 判据 F1：允许表逐条 —— 每条挡下的处数
  --- 命中（有重叠）
  R1  含 shell 元字符            =  136
  R3  git 后面没有子命令            =    1
  R4  git 子命令非只读             =   57
  R5  本探针不跑这一族               =   56
  ★ 命中总数 = 250   分母 = 245   差 5 = 一条命令命中多条
  --- 按【首次命中】划分（互斥）
  R1  含 shell 元字符            =  136
  R3  git 后面没有子命令            =    1
  R4  git 子命令非只读             =   57
  R5  本探针不跑这一族               =   51
  ★ 和 = 245  ⇒ = 分母 245 ✓

=== ★★★ 判据 F7：R1 那条规则，认引号 vs 不认引号
  R1（不认引号，丁⁷ 那一版）挡下  = 136
  R1（认引号，只算引号外的元字符）= 59
  ★ 差 = 77 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】
  --- 不认引号时，元字符分布
    "\\"   = 38
    "("    = 33
    "|"    = 31
    "<"    = 27
    "$"    = 4
    ";"    = 2
    ">"    = 1
  --- 认引号后，元字符分布
    "|"    = 31
    "<"    = 22
    "("    = 5
    "$"    = 1
  --- 被认引号【放行】的那批逐字（前 20）
    docs/AUDIT.md:528   cmd=`grep -c 'add_test(' CMakeLists.txt`
    docs/AUDIT.md:616   cmd=`sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p'`
    docs/AUDIT.md:808   cmd=`grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"`
```

```
=== 判据 F2：代理量 —— 那 245 处里，代理上跑得动几处
  代理上跑得动（可执行名能解析）= 245
  代理上跑不动（解析不到）      = 0
  ★★ 这是【代理量】，不是「真的跑得动」—— 判据 F2：允许表的存在就是让后者不可测。
  其中【会改工作树】的 git（代理上跑得动，但跑了就违反「一个受跟踪文件都不改」）= 47
    [会改树] docs/AUDIT.md:512   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`
    [会改树] docs/AUDIT.md:572   cmd=`git checkout HEAD -- src/vm/vm.c`
    [会改树] docs/AUDIT.md:1287   cmd=`git mv`
    [会改树] docs/AUDIT.md:1984   cmd=`git stash push -- src/main.c`
    [会改树] docs/AUDIT.md:2063   cmd=`git stash push -- src/runtime/runtime_posix.c`
    [会改树] docs/AUDIT.md:2099   cmd=`git checkout`
    [会改树] docs/AUDIT.md:3563   cmd=`git merge --ff-only main`
    [会改树] docs/AUDIT.md:3694   cmd=`git merge main`
    [会改树] docs/AUDIT.md:4370   cmd=`git add -A`
    [会改树] docs/AUDIT.md:5134   cmd=`git commit`
    [会改树] docs/AUDIT.md:5330   cmd=`git rm tools/aot_native_build.sh`
    [会改树] docs/AUDIT.md:6411   cmd=`git worktree add --detach .worktrees/sv-ref-prev  c89e077~1`
    [会改树] docs/AUDIT.md:6413   cmd=`git worktree prune`
    [会改树] docs/AUDIT.md:7047   cmd=`git worktree prune`
    [会改树] docs/BOARD.md:117   cmd=`git add -A`
    [会改树] docs/BOARD.md:118   cmd=`git mv`
    [会改树] docs/BOARD.md:124   cmd=`git checkout main -- src/compilation/wasm_backend.{c,h}`
    [会改树] docs/BOARD.md:126   cmd=`git rm`
    [会改树] docs/BOARD.md:131   cmd=`git add`
    [会改树] docs/BOARD.md:133   cmd=`git mv`
    [会改树] docs/BOARD.md:140   cmd=`git add`
    [会改树] docs/BOARD.md:149   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`
    [会改树] docs/BOARD.md:150   cmd=`git checkout HEAD -- src/vm/vm.c`
    [会改树] docs/BOARD.md:167   cmd=`git mv`
    [会改树] docs/BOARD.md:228   cmd=`git mv`
    [会改树] docs/BOARD.md:262   cmd=`git stash push -- src/main.c`
    [会改树] docs/BOARD.md:263   cmd=`git stash push`
    [会改树] docs/BOARD.md:264   cmd=`git stash push -- src/runtime/runtime_posix.c`
    [会改树] docs/BOARD.md:267   cmd=`git stash push -- src/vm/vm.c`
    [会改树] docs/BOARD.md:270   cmd=`git stash push -- src/runtime/runtime_posix.c`
    [会改树] docs/BOARD.md:284   cmd=`git add -A`
    [会改树] docs/BOARD.md:286   cmd=`git worktree add --detach .worktrees/t2old 58f1a44`
    [会改树] docs/BOARD.md:287   cmd=`git worktree add --detach .worktrees/t2old 58f1a44`
    [会改树] docs/STATUS.md:1041   cmd=`git checkout 1fe93be -- src/`
    [会改树] docs/STATUS.md:1146   cmd=`git checkout HEAD -- src/parser/parser.c`
    [会改树] docs/STATUS.md:2626   cmd=`git rm`
    [会改树] docs/STATUS.md:2731   cmd=`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c`
    [会改树] docs/STATUS.md:2810   cmd=`git checkout HEAD -- src/vm/vm.c`
    [会改树] docs/STATUS.md:3467   cmd=`git mv`
    [会改树] docs/STATUS.md:3636   cmd=`git stash push -- src/main.c`
    [会改树] docs/STATUS.md:3663   cmd=`git stash push -- src/runtime/runtime_posix.c`
    [会改树] docs/STATUS.md:3692   cmd=`git stash push -- src/vm/vm.c`
    [会改树] docs/SYNTAX.md:927   cmd=`git mv`
    [会改树] docs/SYNTAX.md:1003   cmd=`git rm src/mod/gui_mod.c.bak2_20260808_221050`
    [会改树] docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:254   cmd=`git checkout HEAD -- src/parser/parser.c`
    [会改树] website/scratch/custody_transcript.md:267   cmd=`git add -A`
    [会改树] website/scratch/rerun_on_main.md:3   cmd=`git worktree add --detach ./wt-main dcff720`
  --- ★★★ 判据 F16：按【它做什么】判 vs 按【在不在白名单上】判
    R4 那条规则（不在 11 项只读白名单上）挡下        = 57
    实际上【会改工作树】的 git（按子命令语义判）      = 47
    实际上【只读】的 git（在白名单【之外】的真只读）   = 53
    子命令语义未知的                                = 0
    ★ 白名单回答的是「在不在表上」，不是「它做什么」。两个数并排：
      [真只读·不在白名单上] docs/AUDIT.md:882   git show   cmd=`git show 2a14f68:<path> | sed -n '<n>p'`
      [真只读·不在白名单上] docs/AUDIT.md:2951   git archive   cmd=`git archive`
      [真只读·不在白名单上] docs/AUDIT.md:3051   git show   cmd=`git show <ref>:src/runtime/runtime.c`
      [真只读·不在白名单上] docs/AUDIT.md:3066   git show   cmd=`git show c71ea00:src/runtime/runtime.c | sed -n '966p;971p;9`
      [真只读·不在白名单上] docs/AUDIT.md:3099   git rev-list   cmd=`git rev-list --count <refA>..<refB>`
      [真只读·不在白名单上] docs/AUDIT.md:3335   git show   cmd=`git show a64c4fd:docs/AUDIT.md | grep -c 'LICENSE'`
      [真只读·不在白名单上] docs/AUDIT.md:3337   git show   cmd=`git show 4b7b37c:docs/AUDIT.md | grep -c 'LICENSE'`
      [真只读·不在白名单上] docs/AUDIT.md:3373   git show   cmd=`git show 4b7b37c:tools/gate.sh | grep -c '\$REPO_ROOT/'`
      [真只读·不在白名单上] docs/AUDIT.md:3522   git diff   cmd=`git -C "$REPO_ROOT" diff --name-only "$range"`
      [真只读·不在白名单上] docs/AUDIT.md:3561   git reflog   cmd=`git reflog show v0.5.2`
      [真只读·不在白名单上] docs/AUDIT.md:3572   git diff   cmd=`git diff --name-only 4ca013d <head> -- tools/gate.sh`
      [真只读·不在白名单上] docs/AUDIT.md:3636   git show   cmd=`git show origin/main:docs/DECFY_DESIGN.md | grep -c '尚未合进'`
      [真只读·不在白名单上] docs/AUDIT.md:3640   git show   cmd=`git show <你读的那棵树>:.gitignore | grep -cxF '/vtest/sayout.txt'`
      [真只读·不在白名单上] docs/AUDIT.md:3648   git show   cmd=`git show <你读的那棵树>:docs/AUDIT.md | sed -n '3851p'`
      [真只读·不在白名单上] docs/AUDIT.md:3685   git archive   cmd=`git archive`
      [真只读·不在白名单上] docs/AUDIT.md:3759   git ls-files   cmd=`git ls-files | wc -l`
      [真只读·不在白名单上] docs/AUDIT.md:3788   git grep   cmd=`git grep -c '<name>' -- vtest/`
      [真只读·不在白名单上] docs/AUDIT.md:4160   git diff   cmd=`git diff --stat c68894b d94622b -- <四目录> CMakeLists.txt tool`
      [真只读·不在白名单上] docs/AUDIT.md:4170   git ls-tree   cmd=`git ls-tree -r --name-only <ref>`
      [真只读·不在白名单上] docs/AUDIT.md:4313   git show   cmd=`git show 3f612b8^:src/index.js | grep -n`
      [真只读·不在白名单上] docs/AUDIT.md:4388   git grep   cmd=`git grep -n '开放\|封闭' -- docs/ future/ | cut -c1-170 | head -`
      [真只读·不在白名单上] docs/AUDIT.md:4659   git log   cmd=`git log -S 'capsv->type == VAL_BOOL' -- src/runtime/runtime.`
      [真只读·不在白名单上] docs/AUDIT.md:5975   git check-ignore   cmd=`git check-ignore -v`
      [真只读·不在白名单上] docs/AUDIT.md:6306   git cat-file   cmd=`git cat-file -p c71ea00:tools/gate.sh | sed -n '54p'`
      [真只读·不在白名单上] docs/AUDIT.md:6419   git grep   cmd=`git grep -n 'wt-f401f82\|wt-3f3e5b9' -- docs/`
      [真只读·不在白名单上] docs/AUDIT.md:6521   git check-ignore   cmd=`git check-ignore -v`
      [真只读·不在白名单上] docs/AUDIT.md:6743   git check-ignore   cmd=`git check-ignore -q`
      [真只读·不在白名单上] docs/AUDIT.md:6763   git check-ignore   cmd=`git check-ignore -q`
      [真只读·不在白名单上] docs/AUDIT.md:6831   git check-ignore   cmd=`git check-ignore`
      [真只读·不在白名单上] docs/AUDIT.md:6833   git check-ignore   cmd=`git check-ignore`
      [真只读·不在白名单上] docs/AUDIT.md:7183   git show   cmd=`git show <ref>:<path>`
      [真只读·不在白名单上] docs/AUDIT.md:7199   git grep   cmd=`git grep -l '硬规则' -- docs/ | wc -l`
      [真只读·不在白名单上] docs/AUDIT.md:7371   git show   cmd=`git show b09a58a^:src/lexer/lexer.c | grep '"type"'`
      [真只读·不在白名单上] docs/DECFY_DESIGN.md:1033   git show   cmd=`git show 022d002:docs/AUDIT.md | grep -n 'DIVERGENCE'`
      [真只读·不在白名单上] docs/DECFY_DESIGN.md:1079   git ls-files   cmd=`git ls-files -z \| xargs -0 grep -n 'DECFY_DESIGN.md:[0-9]'`
      [真只读·不在白名单上] docs/DECFY_DESIGN.md:1120   git show   cmd=`git show <ref>:docs/STATUS.md`
      [真只读·不在白名单上] docs/ERROR_CODES_V06.md:347   git blame   cmd=`git blame`
      [真只读·不在白名单上] docs/PLAN_V06.md:489   git show   cmd=`git show HEAD:<path>`
      [真只读·不在白名单上] docs/PLAN_V06.md:526   git grep   cmd=`git grep -l -i 'verse forge' HEAD | wc -l`
      [真只读·不在白名单上] docs/RELEASE_0.5.1.md:29   git ls-tree   cmd=`git ls-tree --name-only <那棵树> -- vtest/ | grep -c '\.im$'`
      [真只读·不在白名单上] docs/RELEASE_0.5.2.md:73   git show   cmd=`git show 751b2a0:<path>`
      [真只读·不在白名单上] docs/RELEASE_0.5.2.md:75   git grep   cmd=`git grep 'RELEASE_0\.5\.2'`
      [真只读·不在白名单上] docs/STATUS.md:1098   git ls-files   cmd=`git ls-files | grep -c xlang-toolchain`
      [真只读·不在白名单上] docs/STATUS.md:1625   git show   cmd=`git show f923f4d:CMakeLists.txt | grep -c 'aot-native'`
      [真只读·不在白名单上] docs/STATUS.md:2928   git show   cmd=`git show <旧提交>:src/mod/gui_mod.c.bak2_20260808_221050`
      [真只读·不在白名单上] docs/STATUS.md:3230   git show   cmd=`git show d6bc6a2^:src/main.c | sed -n '247p'`
      [真只读·不在白名单上] docs/TYPESET_V06.md:337   git grep   cmd=`git grep 'builtin_pow\|"pow"\|TOK_CARET' main -- src`
      [真只读·不在白名单上] docs/TYPESET_V06.md:395   git ls-files   cmd=`git ls-files '*.im' | xargs grep -n 'size('`
      [真只读·不在白名单上] docs/streams/builtin-platform-census.md:3   git show   cmd=`git show <sha>:<path>`
      [真只读·不在白名单上] docs/streams/builtin-platform-census.md:13   git grep   cmd=`git grep -n '"<name>"' <ref> -- src/`
      [真只读·不在白名单上] docs/streams/surgery-verify-report.md:208   git show   cmd=`git show f6b3d87:<file>`
      [真只读·不在白名单上] docs/streams/v06-decision-points.md:7   git grep   cmd=`git grep -n <symbol>`
      [真只读·不在白名单上] docs/streams/v06-decision-points.md:150   git grep   cmd=`git grep -ln 'OP_AND\|vm_truthy\|L_MOD\|set_contains' -- src`

=== 判据 F3：四档（分母 = 245）
  ① 对象的性质（可执行名不存在）                = 0
  ② 允许表的性质（跑得动、会改工作树、是我不许） = 47
  ③ 提取器的性质（那段反引号里根本不是一条命令） = 59  ★ 见下逐处读
  ④ 判不出                                      = 139

=== ★ ③ 那批逐处（认引号后仍含元字符的，前 40）
  docs/AUDIT.md:882  元字符="<"  cmd=`git show 2a14f68:<path> | sed -n '<n>p'`
```

---

## 分类脚本 1：把 ⑤ 分成【不是命令／模板／真命令】

```
=== 分母（⑤）= 244
=== ★★★ 三个新分类（互斥，和必须 = 分母）
  N1_不是命令          =   11
  N2_模板            =   27
  N3_真命令           =  206
  ★ 和 = 244 = 分母 ✓

=== ★ N1「不是命令」逐处（全部）
  docs/AUDIT.md:2180   cmd=`python3`
  docs/AUDIT.md:2969   cmd=`node`
  docs/AUDIT.md:4183   cmd=`cmake`
  docs/AUDIT.md:6001   cmd=`bash -e`
  docs/BOARD.md:138   cmd=`python3`
  docs/DECFY_DESIGN.md:1081   cmd=`python3`
  docs/DECFY_DESIGN.md:1097   cmd=`python3`
  docs/EIDOS_V06.md:328   cmd=`python3`
  docs/STATUS.md:162   cmd=`ctest -j$(nproc)`
  docs/STATUS.md:2656   cmd=`cargo --version`
  docs/SYNTAX.md:1007   cmd=`python3`

=== ★ N2「模板」逐处（全部）—— 占位符是什么
  docs/AUDIT.md:882   占位符=["<path>","<n>"]   cmd=`git show 2a14f68:<path> | sed -n '<n>p'`
  docs/AUDIT.md:3051   占位符=["<ref>"]   cmd=`git show <ref>:src/runtime/runtime.c`
  docs/AUDIT.md:3099   占位符=["<refA>","<refB>"]   cmd=`git rev-list --count <refA>..<refB>`
  docs/AUDIT.md:3572   占位符=["<head>"]   cmd=`git diff --name-only 4ca013d <head> -- tools/gate.sh`
  docs/AUDIT.md:3640   占位符=["<你读的那棵树>"]   cmd=`git show <你读的那棵树>:.gitignore | grep -cxF '/vtest/sayout.txt'`
  docs/AUDIT.md:3648   占位符=["<你读的那棵树>"]   cmd=`git show <你读的那棵树>:docs/AUDIT.md | sed -n '3851p'`
  docs/AUDIT.md:3788   占位符=["<name>"]   cmd=`git grep -c '<name>' -- vtest/`
  docs/AUDIT.md:4160   占位符=["<四目录>"]   cmd=`git diff --stat c68894b d94622b -- <四目录> CMakeLists.txt tools/`
  docs/AUDIT.md:4170   占位符=["<ref>"]   cmd=`git ls-tree -r --name-only <ref>`
  docs/AUDIT.md:6250   占位符=["<ref>"]   cmd=`python3 .probe-ie/fifth/four_words.py <ref>`
  docs/AUDIT.md:7183   占位符=["<ref>","<path>"]   cmd=`git show <ref>:<path>`
  docs/BOARD.md:106   占位符=["<pkg>"]   cmd=`node tools/vverse_pack.js preview <pkg>`
  docs/DECFY_DESIGN.md:32   占位符=["<n>"]   cmd=`sed -n '<n>p' | wc -c`
  docs/DECFY_DESIGN.md:236   占位符=["<区间>"]   cmd=`sed -n '<区间>p' src/vm/vm.c`
  docs/DECFY_DESIGN.md:1120   占位符=["<ref>"]   cmd=`git show <ref>:docs/STATUS.md`
  docs/PLAN_V06.md:489   占位符=["<path>"]   cmd=`git show HEAD:<path>`
  docs/RELEASE_0.5.1.md:29   占位符=["<那棵树>"]   cmd=`git ls-tree --name-only <那棵树> -- vtest/ | grep -c '\.im$'`
  docs/RELEASE_0.5.2.md:73   占位符=["<path>"]   cmd=`git show 751b2a0:<path>`
  docs/STATUS.md:987   占位符=["<pkg>"]   cmd=`node tools/vverse_pack.js preview <pkg>`
```

---

## 分类脚本 2：R4 的 57 分成【会改树／真只读】

```
R4 = 57
  其中【真的会改工作树】 = 47
  其中【真只读·不在白名单上】 = 10
  未知 = 0

=== R4 里【真只读·不在白名单上】的那批（逐处）
  docs/AUDIT.md:2951   git archive   cmd=`git archive`
  docs/AUDIT.md:3561   git reflog   cmd=`git reflog show v0.5.2`
  docs/AUDIT.md:3685   git archive   cmd=`git archive`
  docs/AUDIT.md:5975   git check-ignore   cmd=`git check-ignore -v`
  docs/AUDIT.md:6521   git check-ignore   cmd=`git check-ignore -v`
  docs/AUDIT.md:6743   git check-ignore   cmd=`git check-ignore -q`
  docs/AUDIT.md:6763   git check-ignore   cmd=`git check-ignore -q`
  docs/AUDIT.md:6831   git check-ignore   cmd=`git check-ignore`
  docs/AUDIT.md:6833   git check-ignore   cmd=`git check-ignore`
  docs/ERROR_CODES_V06.md:347   git blame   cmd=`git blame`

=== ★ 载体分布：那 47 处【会改树】的，落在哪些文件
   19  docs/BOARD.md
   14  docs/AUDIT.md
    9  docs/STATUS.md
    2  docs/SYNTAX.md
    1  docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md
    1  website/scratch/custody_transcript.md
    1  website/scratch/rerun_on_main.md

=== ★★★ 其中落在【普查自己的报告】里的（website/scratch/**）
  website/scratch/custody_transcript.md:267   cmd=`git add -A`
  website/scratch/rerun_on_main.md:3   cmd=`git worktree add --detach ./wt-main dcff720`

=== ★ 子命令频次（R4 全体）
   11  git checkout
   10  git stash
    7  git mv
    6  git add
    6  git check-ignore
    6  git worktree
    4  git rm
    2  git archive
    2  git merge
    1  git reflog
    1  git commit
    1  git blame
```

---

## 分类脚本 3：那 59 分成【真管道／占位符／两者／都无】

```
认引号后仍含元字符的 = 59
  含 |（真的管道）        = 35
  含 <...>（占位符）      = 23
  两者都有                = 5
  两者都无（只剩 ( 或 $） = 6
  ★ 互斥划分：
    只有真管道     = 30
    只有占位符     = 18
    两者都有       = 5
    两者都无       = 6
    和 = 59

=== 只有占位符（没有真管道）的逐处
  docs/AUDIT.md:3051   cmd=`git show <ref>:src/runtime/runtime.c`
  docs/AUDIT.md:3099   cmd=`git rev-list --count <refA>..<refB>`
  docs/AUDIT.md:3572   cmd=`git diff --name-only 4ca013d <head> -- tools/gate.sh`
  docs/AUDIT.md:4160   cmd=`git diff --stat c68894b d94622b -- <四目录> CMakeLists.txt tools/`
  docs/AUDIT.md:4170   cmd=`git ls-tree -r --name-only <ref>`
  docs/AUDIT.md:6250   cmd=`python3 .probe-ie/fifth/four_words.py <ref>`
  docs/AUDIT.md:7183   cmd=`git show <ref>:<path>`
  docs/BOARD.md:106   cmd=`node tools/vverse_pack.js preview <pkg>`
  docs/DECFY_DESIGN.md:1120   cmd=`git show <ref>:docs/STATUS.md`
  docs/PLAN_V06.md:489   cmd=`git show HEAD:<path>`
  docs/RELEASE_0.5.2.md:73   cmd=`git show 751b2a0:<path>`
  docs/STATUS.md:987   cmd=`node tools/vverse_pack.js preview <pkg>`
  docs/STATUS.md:1125   cmd=`node tools/wasm_run.js --bench <mod> <n>`
  docs/STATUS.md:2928   cmd=`git show <旧提交>:src/mod/gui_mod.c.bak2_20260808_221050`
  docs/streams/builtin-platform-census.md:3   cmd=`git show <sha>:<path>`
  docs/streams/builtin-platform-census.md:13   cmd=`git grep -n '"<name>"' <ref> -- src/`
  docs/streams/surgery-verify-report.md:208   cmd=`git show f6b3d87:<file>`
  docs/streams/v06-decision-points.md:7   cmd=`git grep -n <symbol>`

=== 两者都无的逐处（只剩 ( 或 $ 的）
  docs/STATUS.md:162   元字符="$"   cmd=`ctest -j$(nproc)`
  docs/STATUS.md:1096   元字符="("   cmd=`ctest (expect 97/97, 0 skipped)`
  docs/STATUS.md:2576   元字符="("   cmd=`ctest (expect 108/108, 0 skipped)`
  docs/STATUS.md:2654   元字符="("   cmd=`ctest (expect 108/108, 0 skipped)`
  docs/streams/json-min-nul-escape.md:178   元字符="("   cmd=`ctest (expect 92/92)`
  docs/streams/vverse-cli.md:74   元字符="("   cmd=`ctest (expect N/N)`

=== 只有真管道的（前 35）
  docs/AUDIT.md:3066   cmd=`git show c71ea00:src/runtime/runtime.c | sed -n '966p;971p;977p;981p;999p'`
  docs/AUDIT.md:3335   cmd=`git show a64c4fd:docs/AUDIT.md | grep -c 'LICENSE'`
  docs/AUDIT.md:3337   cmd=`git show 4b7b37c:docs/AUDIT.md | grep -c 'LICENSE'`
  docs/AUDIT.md:3373   cmd=`git show 4b7b37c:tools/gate.sh | grep -c '\$REPO_ROOT/'`
  docs/AUDIT.md:3636   cmd=`git show origin/main:docs/DECFY_DESIGN.md | grep -c '尚未合进'`
  docs/AUDIT.md:3759   cmd=`git ls-files | wc -l`
  docs/AUDIT.md:4313   cmd=`git show 3f612b8^:src/index.js | grep -n`
  docs/AUDIT.md:4388   cmd=`git grep -n '开放\|封闭' -- docs/ future/ | cut -c1-170 | head -30`
  docs/AUDIT.md:6306   cmd=`git cat-file -p c71ea00:tools/gate.sh | sed -n '54p'`
  docs/AUDIT.md:7199   cmd=`git grep -l '硬规则' -- docs/ | wc -l`
  docs/AUDIT.md:7371   cmd=`git show b09a58a^:src/lexer/lexer.c | grep '"type"'`
  docs/DECFY_DESIGN.md:201   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c | sort -u | wc -l`
  docs/DECFY_DESIGN.md:822   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/aot_native.c \| sort -u \| wc -l`
  docs/DECFY_DESIGN.md:823   cmd=`grep -oE 'case OP_[A-Z0-9_]+' src/compilation/wasm_backend.c \| sort -u \| wc `
  docs/DECFY_DESIGN.md:868   cmd=`grep -rn 'EXPR_ARRAY' src/ | wc -l`
  docs/DECFY_DESIGN.md:1009   cmd=`grep -rn 'EXPR_ARRAY' src/ | wc -l`
  docs/DECFY_DESIGN.md:1033   cmd=`git show 022d002:docs/AUDIT.md | grep -n 'DIVERGENCE'`
  docs/DECFY_DESIGN.md:1068   cmd=`grep -rln 'def find_engine' tools/ | wc -l`
  docs/DECFY_DESIGN.md:1079   cmd=`git ls-files -z \| xargs -0 grep -n 'DECFY_DESIGN.md:[0-9]'`
  docs/DECFY_DESIGN.md:1100   cmd=`grep -rl "im_typeset" --include=*.c --include=*.h src/ \| grep -v "^src/types/`
  docs/HANDOFF_INFIVERSE.md:68   cmd=`grep -rn '"count"' src/ --include=*.c | wc -l`
  docs/HANDOFF_INFIVERSE.md:95   cmd=`grep -rn '@bits\|@bytes' src/ --include=*.c --include=*.h | wc -l`
  docs/HANDOFF_INFIVERSE.md:100   cmd=`grep -rn 'TOK_CARET\|OP_POW' src/ --include=*.c --include=*.h | wc -l`
  docs/PLAN_V06.md:526   cmd=`git grep -l -i 'verse forge' HEAD | wc -l`
  docs/REQUIREMENTS_ANALYSIS.md:192   cmd=`grep tilemap\|scene::Node\|collision::AABB\|camera::Camera`
  docs/STATUS.md:1098   cmd=`git ls-files | grep -c xlang-toolchain`
  docs/STATUS.md:1625   cmd=`git show f923f4d:CMakeLists.txt | grep -c 'aot-native'`
  docs/STATUS.md:2508   cmd=`grep nv_error|nv_throw|nv_panic|abort()`
  docs/STATUS.md:3230   cmd=`git show d6bc6a2^:src/main.c | sed -n '247p'`
```

---

## 分类脚本 4：五档

```
分母 = 245
  ①     =    0
  ②     =   47
  ③     =   54
  ⑤a    =   60
  ⑤b    =   42
  ⑤c    =   42
  ★ 和 = 245 = 分母 ✓

=== ② 之外，R4 里【语义只读】的 10 处，各自落哪一档
  ③  docs/AUDIT.md:2951   cmd=`git archive`
  ③  docs/AUDIT.md:3561   cmd=`git reflog show v0.5.2`
```
