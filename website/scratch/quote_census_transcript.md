# 壬⁷ 逐字留档：「引述与断言同形」普查的两次运行

本文件是【输出】。探针源码在 `website/scratch/quote_census_probe.js`（受跟踪）。

★★★ 本文件已把本目录的禁用串掩码为【禁用串·已掩码】（判据 R6.8）—— 探针在输出前掩码。

---

## 运行：修好「` ` ` 不是引号」与「禁用串掩码」之后（本件报的数）

```
=== 判据 R0：载体
  受跟踪 .md 文件数 = 167
  行数 = 68543
  断言 span（去重后 norm 数）= 48487
  引述 span（去重后 norm 数）= 13755
  两者逐字相同的 norm 数 = 400

=== 判据 R4.1：一共有几处
  按 (A,B) 对（判据 R0.4）= 577
  按 norm(A) 去重         = 399

=== 判据 R1.4：按【引号种类】分（`` ` `` 是代码，不是引号）
  {"``":372,"「」":60,"\"\"":145}
  真引号（「」/『』/“”）= 60
  代码（`` ` `` 与 ""）= 517
  ★★★ ` ` ` 不是引号 —— 它是【代码】。把代码里的那一串当成「引述」，
      于是「引述与断言同形」这个普查里的绝大多数处，是【同一段代码被两处引用】，
      不是【一句话被引述】。

=== 判据 R4.2：同一份文件里既有断言又有它的引述
  处数 = 21
    docs/AUDIT.md:85  ←→  docs/AUDIT.md:5671   [「」]
        逐字：修复（用户裁定：布尔语义）
    docs/AUDIT.md:2124  ←→  docs/AUDIT.md:2142   [「」]
        逐字：`builtin_fullscreen` 不可达
    docs/AUDIT.md:3339  ←→  docs/AUDIT.md:3341   [「」]
        逐字：这一笔又加了一行含该词的行
    docs/AUDIT.md:3354  ←→  docs/AUDIT.md:3582   [「」]
        逐字：下表全部在 `4ca013d` 上量
    docs/AUDIT.md:3574  ←→  docs/AUDIT.md:3582   [「」]
        逐字：从未正确只能靠把号对着内容再量一次
    docs/AUDIT.md:4081  ←→  docs/AUDIT.md:3666   [「」]
        逐字：一个结论的作用域被说得比它实际覆盖的范围大
    docs/AUDIT.md:4343  ←→  docs/AUDIT.md:6841   [「」]
        逐字：这两节哪一条是家，这个决定今天没有对象
    docs/AUDIT.md:4875  ←→  docs/AUDIT.md:4963   [「」]
        逐字：读数在不在被读的那个键上
    docs/AUDIT.md:6695  ←→  docs/AUDIT.md:6735   [「」]
        逐字：代码里真的会写文件的地方
    docs/AUDIT.md:8017  ←→  docs/AUDIT.md:8015   [「」]
        逐字：本节零个 `文件名:数字` 形式
    docs/DECFY_DESIGN.md:754  ←→  docs/DECFY_DESIGN.md:532   [「」]
        逐字：实测（本会话独立复跑，两条程序逐字）
    docs/DECFY_DESIGN.md:1054  ←→  docs/DECFY_DESIGN.md:916   [「」]
        逐字：在 wasm 实现字符串与字典/集合
    docs/DECFY_DESIGN.md:1118  ←→  docs/DECFY_DESIGN.md:1069   [「」]
        逐字：行号本身没错，错的是没记口径
    docs/EIDOS_V06.md:60  ←→  docs/EIDOS_V06.md:68   [「」]
        逐字：方法查找从左到右，子类优先
    docs/EIDOS_V06.md:60  ←→  docs/EIDOS_V06.md:70   [「」]
        逐字：`super` 调用最近的父类实现
    docs/ERROR_CODES_V06.md:43  ←→  docs/ERROR_CODES_V06.md:331   [「」]
        逐字：编译器检测到所有都有处理就能安全过关
    docs/PLAN_V06.md:45  ←→  docs/PLAN_V06.md:9   [「」]
        逐字：去 C 化 → Eidos 进引擎 → 【禁用串·已掩码】
    docs/PLAN_V06.md:244  ←→  docs/PLAN_V06.md:9   [「」]
        逐字：去 C 化 → Eidos 进引擎 → 【禁用串·已掩码】
    docs/STATUS.md:1222  ←→  docs/STATUS.md:1258   [「」]
        逐字：当引擎回显会污染输出时，断言必须打在程序自己打印的带标记值行上
    docs/STATUS.md:1863  ←→  docs/STATUS.md:1869   [「」]
        逐字：没有做过一次真实的 provider 往返
    website/scratch/registration_rule.md:737  ←→  website/scratch/registration_rule.md:758   [「」]
        逐字：有别的东西在认它，而我没找到

=== 判据 R4.3：引述带标记而断言没有
  处数 = 15
  标记频次：{"逐字":8,"引":9,"原文":2}
    docs/AUDIT.md:85  ←→  docs/AUDIT.md:5671   标记 = ["逐字"]   引号 = 「」
        逐字：修复（用户裁定：布尔语义）
    docs/AUDIT.md:2753  ←→  docs/BOARD.md:276   标记 = ["引"]   引号 = 「」
        逐字：吐解析错误的 fixture 数 == 0
    docs/AUDIT.md:2887  ←→  docs/DECFY_DESIGN.md:1068   标记 = ["原文"]   引号 = 「」
        逐字：检查在算一个已经不等于它要防的那个量
    docs/AUDIT.md:3354  ←→  docs/AUDIT.md:3582   标记 = ["引"]   引号 = 「」
        逐字：下表全部在 `4ca013d` 上量
    docs/AUDIT.md:3574  ←→  docs/AUDIT.md:3582   标记 = ["引"]   引号 = 「」
        逐字：从未正确只能靠把号对着内容再量一次
    docs/AUDIT.md:4081  ←→  docs/AUDIT.md:3666   标记 = ["引"]   引号 = 「」
        逐字：一个结论的作用域被说得比它实际覆盖的范围大
    docs/AUDIT.md:7700  ←→  docs/BOARD.md:288   标记 = ["逐字","引"]   引号 = 「」
        逐字：全部读数取自 `f6e22f5`
    docs/STATUS.md:3818  ←→  docs/AUDIT.md:3387   标记 = ["原文","引"]   引号 = 「」
        逐字：`migrate_report.py` 的测试本轮不写
    docs/DECFY_DESIGN.md:1118  ←→  docs/DECFY_DESIGN.md:1069   标记 = ["逐字","引"]   引号 = 「」
        逐字：行号本身没错，错的是没记口径
    docs/HANDOFF_INFIVERSE.md:47  ←→  docs/AUDIT.md:10844   标记 = ["逐字"]   引号 = 「」
        逐字：v3.1 的三根柱子仍然是缺的
    docs/STATUS.md:42  ←→  docs/AUDIT.md:3622   标记 = ["逐字"]   引号 = 「」
        逐字：记录一个曾经的绑定，不能写成绑定的形状
    docs/SYNTAX.md:606  ←→  docs/BOARD.md:131   标记 = ["引"]   引号 = 「」
        逐字：已于 `df82cf6` 修复
    docs/SYNTAX.md:636  ←→  docs/BOARD.md:130   标记 = ["逐字","引"]   引号 = 「」
        逐字：操作数栈非空时发生的用户函数调用
    future/archive/README.md:3  ←→  docs/EIDOS_V06.md:25   标记 = ["逐字"]   引号 = 「」
        逐字：已被取代、或被更高层文档覆盖的研究性设计与愿景草案
    future/archive/面对对象.md:359  ←→  docs/EIDOS_V06.md:87   标记 = ["逐字"]   引号 = 「」
        逐字：每次属性修改后自动检查不变量，违反时触发 `on_violation`。

=== 判据 R4.4：把引述从人口里剔掉之后，「一共有几处断言」会从几变成几
  断言 span 总处数（按行）= 51609
  其中【有同形引述】的 = 580
  剔掉之后 = 51029
  ★ 按 norm 数：有 400 / 48487 条断言有同形引述

=== 判据 R4.5：同一行里的自引（单列，判据 R1.3）
  处数 = 2

=== 判据 R3：四档（上报口径 = 真引号，分母 = 60）
  ★ 并排：全族分母 = 577 ／ 真引号分母 = 60 ／ 代码族 = 517
  ★ 两个分母不许互相替代。
  ① = 22
      docs/AUDIT.md:85 ←→ docs/AUDIT.md:5671
      docs/AUDIT.md:2753 ←→ docs/BOARD.md:276
      docs/AUDIT.md:2887 ←→ docs/DECFY_DESIGN.md:1068
      docs/AUDIT.md:3354 ←→ docs/AUDIT.md:3582
      docs/AUDIT.md:3574 ←→ docs/AUDIT.md:3582
      docs/AUDIT.md:4081 ←→ docs/AUDIT.md:3666
  ② = 38
      docs/AUDIT.md:778 ←→ docs/BOARD.md:158
      docs/STATUS.md:3131 ←→ docs/BOARD.md:158
      docs/AUDIT.md:2124 ←→ docs/AUDIT.md:2142
      docs/AUDIT.md:3339 ←→ docs/AUDIT.md:3341
      docs/AUDIT.md:3505 ←→ website/scratch/custody_reading.md:88
      website/scratch/cmdnum_reading.md:57 ←→ website/scratch/custody_reading.md:88
  ③ = 0
  ④ = 0
  ★ 和 = 60  ⇒ = 分母 60
  ★ ③ = 0 ／ ④ = 0 —— 本件没有把「我没有去读它」写进 ④（判据 R3.2）

=== 判据 R5：对照
  (a) 一处已知断言：docs/AUDIT.md:85 ⇒ 落 ①
  (b) 一处已知引述：docs/AUDIT.md:5671 ⇒ 落 ①
  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓
      ★ 而「空集 ⇒ 和 = 0」与「对象里一处都没有」在输出上同形 ⇒ 必须把分母一起印

=== 判据 R6.2：本件有没有跑任何被扫到的命令？
  **没有。** 本探针只 readFileSync 与只读 git ls-files。

=== 判据 R6.8：本目录的禁用串
  命中处数（全族）= 2
  其中真引号族 = 2
  ★ 本报告已把该串掩码为【禁用串·已掩码】—— 逐字写在 docs/PLAN_V06.md:9 与 :45。
  ★★★ 一份普查会把【不许出现在本目录里的字符串】逐字搬进本目录：
      本探针在输出前掩码，是因为它自己会把那个串印进 website/scratch/。

=== 判据 R6.2b：按目录分布
  {"docs":52,"website":3,"future":4,"examples":1}
```
