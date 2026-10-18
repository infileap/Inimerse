# 辛⁷ 逐字留档：Δ 表探针的一次运行 ＋ 三次修错

本文件是【输出】。探针源码在 `website/scratch/delta_probe.js`（受跟踪）。

---

## 运行：修好三处之后（本件报的数）

```
=== 判据 D0：载体与今天的三个集合
  受跟踪 .md 文件数 = 164   语料 = 604
  |①| = 137   |②| = 59   |③| = 61
  今天重算： ①→② = 78   ②→③ = 2   ①→③ = 76
  ★ 定义： ①→② = |① \ ②| ／ ②→③ = |③ \ ②| ／ ①→③ = |① \ ③|
  ★ 方向定义：Δ = 【后一个口径收的】 − 【前一个口径收的】 ⇒ π₁ 是收紧时 Δ 为负

=== 判据 D0.1：本车道的产物（49 份 .md）

=== 判据 D2.1：一共有几处
  分母 P（行）   = 28
  分母 Q（断言） = 10   ★ 上报口径 = Q
  ★ 差 = 18 ⇒ 那是【同一句话的多份副本】（判据 D0.3）

=== 判据 D1.6：按【来源】分（形状判据）
  断言（按行）= 21
  引述（按行）= 5
  源码（按行）= 2
  断言（按断言）= 8
  引述（按断言）= 2
  源码（按断言）= 0
  ★★ 只有【断言】那一档是「本车道自己下的断言」；引述与源码里的那两个数
     与断言逐字相同，在报告里读起来一模一样。

=== 判据 D1：逐处（文件名 ／ 行号 ／ 逐字那一句 ／ 哪一对 ／ 符号 ／ 今天重算）
  website/scratch/allowlist_transcript.md:72   [副本 ×2]
     逐字：★ 差 = 77 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】
     哪一对 = **未写**   符号 = 无   逐字值 = 77   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/quote_reading.md:48   [副本 ×7]
     逐字：★ **② 与 ③ 的差 = 2 处** —— 逐字：
     哪一对 = ②→③   符号 = 无   逐字值 = 2   今天重算 = 2   档 = ①
  website/scratch/quote_reading.md:55   [副本 ×7]
     逐字：★ **① 与 ② 的差 = 78 处**（引号里的字面量）—— 逐处已列在戊⁷ 的读数里，本件不重复。
     哪一对 = ①→②   符号 = 无   逐字值 = 78   今天重算 = 78   档 = ①
  website/scratch/scope_criteria.md:30   [副本 ×1]
     逐字：★ **「差是 0」不是「不相交」的证据 —— 它只是不相交的一个后果。**
     哪一对 = **未写**   符号 = 无   逐字值 = 0   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_reading.md:43   [副本 ×6]
     逐字：★ **三对口径在【同一个对象】上给出三个不同的差。** 而己⁷ 报告里印的那个「差 = 2」是**第三对**（②→③）
     哪一对 = **未写**   符号 = 无   逐字值 = 2   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_reading.md:107   [副本 ×1]
     逐字：- **I2**（`quote_reading.md:36`，己⁷）：**差 = −76**（= O 上 π₁ 收 61 − π₀ 收 137）
     哪一对 = **未写**   符号 = −   逐字值 = 76   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_reading.md:108   [副本 ×1]
     逐字：- **I3**（`allowlist_reading.md:155`，戊⁷）：**差 = −78**（= O 上 π₁ 收 59 − π₀ 收 137）
     哪一对 = **未写**   符号 = −   逐字值 = 78   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_transcript.md:70   [副本 ×1]
     逐字：★ 检查 ③ ⊇ ②：E₁₃ 与 E₁₂ 的差 = -2（=0 表示③与②改判的处完全相同）
     哪一对 = **未写**   符号 = -   逐字值 = 2   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_transcript.md:144   [副本 ×1]
     逐字：★ 逐字回答：有 —— I2（quote_reading.md:36（己⁷））是相交的，**差 = -76**（= O 上 π₁ 收 61 − π₀ 收 137）
     哪一对 = **未写**   符号 = -   逐字值 = 76   今天重算 = —（未写哪一对，无法核对）   档 = ③
  website/scratch/scope_transcript.md:145   [副本 ×1]
     逐字：★ 逐字回答：有 —— I3（allowlist_reading.md:155（戊⁷））是相交的，**差 = -78**（= O 上 π₁ 收 59 − π₀ 收 137）
     哪一对 = **未写**   符号 = -   逐字值 = 78   今天重算 = —（未写哪一对，无法核对）   档 = ③

=== 判据 D2.2：那一句里没有写【哪一对】的处数
  处数 = 8  / 10
    website/scratch/allowlist_transcript.md:72  逐字：★ 差 = 77 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】
    website/scratch/scope_criteria.md:30  逐字：★ **「差是 0」不是「不相交」的证据 —— 它只是不相交的一个后果。**
    website/scratch/scope_reading.md:43  逐字：★ **三对口径在【同一个对象】上给出三个不同的差。** 而己⁷ 报告里印的那个「差 = 2」是**第三对**（②→③）
    website/scratch/scope_reading.md:107  逐字：- **I2**（`quote_reading.md:36`，己⁷）：**差 = −76**（= O 上 π₁ 收 61 − π₀ 收 137）
    website/scratch/scope_reading.md:108  逐字：- **I3**（`allowlist_reading.md:155`，戊⁷）：**差 = −78**（= O 上 π₁ 收 59 − π₀ 收 137）
    website/scratch/scope_transcript.md:70  逐字：★ 检查 ③ ⊇ ②：E₁₃ 与 E₁₂ 的差 = -2（=0 表示③与②改判的处完全相同）
    website/scratch/scope_transcript.md:144  逐字：★ 逐字回答：有 —— I2（quote_reading.md:36（己⁷））是相交的，**差 = -76**（= O 上 π₁ 收 61 − π₀ 收 137）
    website/scratch/scope_transcript.md:145  逐字：★ 逐字回答：有 —— I3（allowlist_reading.md:155（戊⁷））是相交的，**差 = -78**（= O 上 π₁ 收 59 − π₀ 收 137）

=== 判据 D2.3：符号为负的处数
  处数 = 5
    website/scratch/scope_reading.md:107  逐字值 = −76
    website/scratch/scope_reading.md:108  逐字值 = −78
    website/scratch/scope_transcript.md:70  逐字值 = -2
    website/scratch/scope_transcript.md:144  逐字值 = -76
    website/scratch/scope_transcript.md:145  逐字值 = -78

=== 判据 D2.4：今天重算之后值变了的处数
  处数 = 0
  ★ 判据 D2.5：只判 (a)「那个数本身变了」；(b)「数没变而它指的对象变了」单列 —— 本件 (b) = 见下

=== 判据 D3：四档（分母 = 10）
  ① =  2   website/scratch/quote_reading.md:48 website/scratch/quote_reading.md:55
  ② =  0   
  ③ =  8   website/scratch/allowlist_transcript.md:72 website/scratch/scope_criteria.md:30 website/scratch/scope_reading.md:43 website/scratch/scope_reading.md:107 website/scratch/scope_reading.md:108 website/scratch/scope_transcript.md:70 website/scratch/scope_transcript.md:144 website/scratch/scope_transcript.md:145
  ④ =  0   
  ★ 和 = 10  ⇒ = 分母 Q = 10 ✓
  ★ ④ = 0 —— 且本件没有把「我没有去读它」写进 ④（判据 D3.2）

=== 判据 D2.6：③ 那 8 处，若【强行从上下文补出】那一对，会得到什么
  ★ 判据 D1.3：补出来的配对【不改变档位】—— 它只用来回答「那个数今天还对不对」。
  website/scratch/allowlist_transcript.md:72  逐字值 = 77   推断的对 = ①→②（同行补不出，回看三行 ⇒ ①→②）   今天重算 = 78   ★★★ 变了：77 → 78
  website/scratch/scope_criteria.md:30  逐字值 = 0   推断的对 = （补不出）   今天重算 = —
  website/scratch/scope_reading.md:43  逐字值 = 2   推断的对 = （补不出）   今天重算 = —
  website/scratch/scope_reading.md:107  逐字值 = −76   推断的对 = ①→③   今天重算 = 76   （未变）
  website/scratch/scope_reading.md:108  逐字值 = −78   推断的对 = ①→②   今天重算 = 78   （未变）
  website/scratch/scope_transcript.md:70  逐字值 = -2   推断的对 = ②→③   今天重算 = 2   （未变）
  website/scratch/scope_transcript.md:144  逐字值 = -76   推断的对 = ①→③   今天重算 = 76   （未变）
  website/scratch/scope_transcript.md:145  逐字值 = -78   推断的对 = ①→②   今天重算 = 78   （未变）

=== 判据 D4：对照
  (a) 已知写了哪一对：website/scratch/quote_reading.md:48  哪一对 = ②→③  档 = ①
      ⇒ 落 ① ✓
  (b) 已知没写哪一对：website/scratch/allowlist_transcript.md:72  逐字：★ 差 = 77 处：那些【元字符在引号里】，是【字面量】，不是【shell 展开】
      ⇒ 落 ③ ✓
  (c) 空集对照：对象 = ∅ ⇒ 四档全 0，和 = 0 = 分母 0 ✓
      ★★ 而「空集 ⇒ 和 = 0」与「对象里一处都没有」在输出上同形 ⇒ 必须把分母一起印

=== 判据 D5.2：本件有没有跑任何文档里的命令？
  **没有。** 本探针只读文件（`fs.readFileSync`）与只读 git（`git ls-files`），不执行任何被扫到的命令。
```
