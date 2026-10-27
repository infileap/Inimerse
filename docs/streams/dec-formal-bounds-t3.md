# 有界证明 T3：三前端等价的探针表——从源码重生成，不是手抄

本页接 T1（VM 索引守卫的 SMT 有界证明，`docs/streams/dec-formal-bounds.md`）与 T2（栈平衡的可抽性，`docs/streams/dec-formal-bounds-t2.md`）。
T3 的问题是：同一段源码过三个前端（C 前端、自举前端、将来的字节码镜像），读数是否一致。
本页**不证等价**，只交它的前置：那张探针表本身必须能**从源码重生成**。
手抄的「30 个构造、28 same、2 differ」在本页视同无效——这正是 T2 页 §3 记的病：形状没印出来的读数，读者无法复核。

## 0. 边界与标签

- `measured` = 本页命令在页面标明的树上跑出来的；`asserted` = 写下来但没跑；`proved` = 求解器 unsat（本页没有求解器参与）。
- 三种读数状态分开，不混：`same`（两端同 rc、同 MARK）、`differ`（同 rc、MARK 不同）、`unexplained`（不是 same 又写不出 why）。
  最后一种一律计数；把它压成 0 的通行做法是不给它开口——本页不给这个口。

## 1. 一条命令

    python3 tools/z3bounds/extract_probe_table.py --bin /tmp/z3build/inimerse

退出码：0 = 读数与自证一致；1 = 自证不符（pinned baseline 或 `unexplained`）；2 = 这棵树不像工具描述的那棵（源码表或二进制缺失）。
二进制必须**树外构建**，否则 `build/` 会进 worktree 的未跟踪清单：`cmake -S /tmp/wt-z3c -B /tmp/z3build -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/z3build -j`。

## 2. 表从源码来，不从名字来

表取自 `tools/dec_construct_matrix.py:36`（`PROBES` 那行）[obs: 13e14f9]：用 `ast` 解析该文件的语法树，再 `ast.literal_eval` 那个列表，**不 import 该模块**。
理由不是洁癖：模块一旦被 import，它交出来的表与它文件里的表可以是两个东西；而「同一个构造」在不同树上必须是同一段程序。
同名的 `LIBX` 一并取出，它的 sha 印在 `SOURCE` 行——探针 `import` 依赖它，漏了它就是拿两个不同的夹具比读数。

每个构造的源码正文另落盘 30 份并写 `manifest.json`，每行还带 `script_sha256`（正文 sha 前 8 位）：
没有这个 sha，「同一个构造」只是同一个名字。

## 3. 读数（本页所有粘贴块产生于树 `13e14f9`，`src/`、`selfhost/` 与读数一致）

    SOURCE|tools/dec_construct_matrix.py:36|table=PROBES|libx_sha256=e64ba670
    BIN|path=/tmp/z3build/inimerse|sha256=f656c142|size=899136
    SELFHOST|selfhost/compiler.im|sha256=7f4ebff5
    VERDICTS|same=28|differ=2|c-fails=0|sh-fails=0|skip=0
    CASES|total=30|same=28|differ=2|from_source=tools/dec_construct_matrix.py:36
    UNEXPLAINED|n=0|
    SCRIPTS|dir=/tmp/z3work/probe_scripts|n=30|manifest=/tmp/z3work/probe_scripts/manifest.json
    EXPECT|total=30|same=28|differ=2|status=match|source=pinned-baseline

逐行形态是 `case=<名>|script_sha256=<8>|c=<rc>:<MARK>|sh=<rc>:<MARK>|verdict=<判定>|why=<理由>`，
例如 `case=recursion|script_sha256=7c5a3244|c=0:MARK=55|sh=0:MARK=55|verdict=same|why=-`。
`c=` 与 `sh=` 是**读数**，不是通过标记：`c-fails` 表示 C 前端自己就没印 MARK（探针坏了），`sh-fails` 表示自举端超时或 rc 不同——三类失败各有名字，不合并成「differ」。

## 4. 两个 differ：名字、why、逐字证据

    WHY|repeat-count|<why>|sh_first_error=parse error: 期望 : 但得到 [=] 第3行
    WHY|do-until|<why>|sh_first_error=parse error: 期望 : 但得到 [=] 第3行

- `repeat-count`：`repeat 3 { i = i + 1 }`，C 端 `MARK=3`，自举端 `MARK=0`，**两端 rc 都是 0**。
  自举端把块体解析失败（`parse error: 期望 : 但得到 [=]`），却照跑照退 0——**静默错答案，不是拒绝**。
- `do-until`：`do { i = i + 1 } until i == 3`，同上，`MARK=3` vs `MARK=0`，rc 都 0，首条 stderr 逐字相同。

这两条差异比报错更危险：报错会被人看见，`rc=0` 不会。也正因为如此，本页把它们标 `differ` 并写 why，而不是塞进「sh-fails」这个更省事的桶。
（这是读数，不是修：本页不动 `src/`、`selfhost/`。）

## 5. 自证的边界：`EXPECT` 是 pinned baseline，不是重算

`EXPECT` 那一行写死的 30/28/2 是**旧读数**，作用是挡住「数变了没人发现」；它不是再从源码推出来的数。
差一个数退出 1 并打印 `EXPECT-FAIL|... != pinned ...`；`unexplained` 非 0 同样退出 1。
派生数（`CASES|...`、`VERDICTS|...`）全部由本轮测量得出；两者对不上时，**先信派生数**，再改 baseline——反过来就是把工具调到符合记忆。

## 6. 这张表不能代替 T3

30 个构造覆盖的是语言表面的一角，且每个探针只印一个 MARK：表说明「这些探针上两端一致」，不说明「两端等价」。
要走到等价命题还差三样：三个前端各自的语义定义（哪一层是规范）、有界输入域（探针空间之外怎么抽）、以及那 2 条 differ 的**判定**——「镜像缺语句形态」还是「语义不同」是镜像车道的判断，本页只交读数与 why。

## 7. 树号、改动清单、没做的事

- 树号：`13e14f9`，分支 `z3bounds-t1`，基 `origin/main` = `9a3e53a`；worktree `/tmp/wt-z3c`（已 `git worktree lock`）。本页所在提交号不写死，用 `git log -1 -- docs/streams/dec-formal-bounds-t3.md` 取——页内自指会随提交漂移。
- 新增：`tools/z3bounds/extract_probe_table.py`、本页。**未改** `src/`、`selfhost/`、`tools/dec_construct_matrix.py`、门禁脚本与 CTest 计数——表一开始就能抽，不需要改它。
- 没做：CTest 注册（门禁处没有 z3，而且本表不是证明，注册即 SKIP，而 skip 不是 pass）；三前端等价命题本身；镜像侧的 op 覆盖对账（T2 §8 的 `MIRROR` 只给了读数）。
- 二进制 provenance：`BIN` 行给了 sha 与大小，但**构建来源不写在二进制里**——它是从 `13e14f9` 的树外构建来的，这一点只能靠本页这句散文与上面的构建命令对上，不能靠 `strings` 验。
