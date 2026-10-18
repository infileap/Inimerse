# 乙⁷ 逐字输出留档

命令：`node website/scratch/self_carrier_probe.js .`
树：`/home/sakiko/inimerse/.worktrees/website-ledger-readat`
HEAD：`bda57cae80061bdcd0bbb614866213e321447a3e`

```
=== C1：尺子真正读的载体（逐份，由癸⁶ 自己印）
  癸⁶ 自印 CARRIERS=18，逐份：
     1  website/scratch/check_citations.js
     2  website/scratch/kill_boundary_probe.js
     3  website/scratch/kill_boundary_rerun.js
     4  website/scratch/render_check.js
     5  website/scratch/report_diff.js
     6  website/scratch/ruler_geng6.js
     7  website/scratch/ruler_gui6.js
     8  website/scratch/ruler_jig6_recon.js
     9  website/scratch/ruler_ren6.js
    10  website/scratch/run_snippets.js
    11  website/scratch/run_tutorial.js
    12  website/scratch/snippets/caught-error-payload.js
    13  website/scratch/snippets/div-zero-float.js
    14  website/scratch/snippets/div-zero-int.js
    15  website/scratch/snippets/index-out-of-range.js
    16  website/scratch/snippets/int-div-truncation.js
    17  website/scratch/snippets/manifest.js
    18  website/scratch/snippets/mod-zero.js
  其中 ruler_*.js 一族：4 份 —— ruler_geng6.js、ruler_gui6.js、ruler_jig6_recon.js、ruler_ren6.js

=== C2：逐把解析器 + 用尺子自印的 DENOM 对账
  己⁶′    尺子自印 DENOM=  49  解析器读到=  49  ✓ 一致
  庚⁶     尺子自印 DENOM= 105  解析器读到= 105  ✓ 一致
  壬⁶     尺子自印 DENOM=  60  解析器读到=  60  ✓ 一致
  癸⁶     尺子自印 DENOM= 372  解析器读到= 372  ✓ 一致

=== C3：排除 ruler_*.js 之后，逐把（含 / 排除）
  己⁶′      49 /   49   （一格没动）
  庚⁶      105 /  105   （一格没动）
  壬⁶       60 /   60   （一格没动）
  癸⁶      372 /  366   （−6）

=== C4：排除后消失的 6 处（逐处）
    ruler_geng6.js:33
    ruler_gui6.js:59
    ruler_gui6.js:145
    ruler_jig6_recon.js:38
    ruler_ren6.js:28
    ruler_ren6.js:66

=== C5：两个分母并排（不许合、不许互相替代）
  癸⁶ 的分母：含 ruler_*.js = 372   排除 ruler_*.js = 366
  四把并集  ：含 ruler_*.js = 465   排除 ruler_*.js = 459

=== C6：三个对照（真的跑）
  该动   癸⁶ 含=372 排除=366 ⇒ 降了 6 ✓
  该不动 非 ruler 载体的命中数：含=[49,105,60,366] 排除=[49,105,60,366] ⇒ 逐把相同 ✓
  空集   把【全部载体】从读数里排除 ⇒ 剩余 = 0 ⇒ 0 ✓
         （自证名单匹配上了：并集 459 处里，落在载体名单上的 = 459 ⇒ 全部命中 ✓ 所以剩余 0 是「移除」的 0，不是「没匹配上」的 0

=== 两处目标的键（己⁶′/庚⁶/壬⁶/癸⁶ 顺序；含 ⇒ 排除）
  check_citations.js:87      含=0110  排除=0110  ⇒ 键不变 ✓
  render_check.js:661        含=0001  排除=0001  ⇒ 键不变 ✓
```
