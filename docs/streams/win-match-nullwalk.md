# 作业单：`win-match-nullwalk` —— ② 类访问违例今天还崩不崩

- **认领者**：`amber-badger`
- **分支**：`stream/win-match-nullwalk`（base `main` = `2fa00e1`）
- **工作树**：`.worktrees/win-match-nullwalk`
- **裁定依据**：`docs/BOARD.md` 的 `win-match-stack-overflow` 行（其中 ② 那一栏）与 `docs/AUDIT.md` §1.70（① 的机制与修复）。本文只回答「② 今天还崩不崩」，不修、不改源码。
- **测量 ref**：**`2fa00e1`**（今天树）；对照旧树 **`f6b3d87`**（板行写下时的基准）

## 0. 结论先行

**② 今天仍崩**：`code=0xC0000005`，引擎映像内同样是七帧，同样有两帧 `re_seq`。

`f1a0461`（① 的修复）没有触及它 —— 它只把崩溃点从旧 `$` 那句三元式搬到了新 `$` 分支的同一处解引用上。

真正的触发条件不是「长」，是**量词下界为 0**：`*` 与 `?` 崩，`+` 不崩；末尾有没有 `$` 一样崩；30 个字符就够。

## 1. 四项回报

1. `pwd` = `/home/sakiko/inimerse/.worktrees/win-match-nullwalk`
2. 测量 ref = `2fa00e1`；对照 ref = `f6b3d87`
3. 读法：Windows 侧构建（ucrt64 gcc 16.1.0、Ninja、`RelWithDebInfo` 与 `Release` 各一份），`nm` 取符号表做「运行基线 + RVA → 符号 + 偏移」换算，`objdump` 读崩溃指令，`gdb` 读崩溃帧的局部变量；`inimerse_crash.log` 每次跑之前清空、只读一条条目（它追加写入且按 CWD 落盘）
4. 本案没有提交脚本；可重跑的是第 8 节那几条命令

## 2. 先证明量具能重现旧读数（受控复现）

要拿「今天仍崩」做结论，第一件事必须是：**同一套量具在旧树上能重现板行当初记下的读数**。在 `f6b3d87` 上按同配置重建，符号化后逐帧对上：

| 帧 | 本文实测（`f6b3d87`） | 板行原文 |
| --- | --- | --- |
| `#7` | `re_seq (+0x35)` | `re_seq (+0x35)` |
| `#8` | `re_seq (+0x25a)` | `re_seq (+0x25a)` |
| `#9` | `builtin_match.part.0 (+0x12d)` | `builtin_match.part.0 (+0x12d)` |
| `#10` | `vm_execute_thread (+0x3f64)` | `vm_execute_thread (+0x3f64)` |
| `#11` | `vm_run (+0x181)` | `vm_run (+0x181)` |
| `#12` | `load_and_run (+0xed)` | `load_and_run (+0xed)` |
| `#13` | `main (+0xab5)` | `main (+0xab5)` |

七个偏移逐位相同 ⇒ 量具与当初那把等效，下面的差分不是量具差异。板行记下的 `code=0xC0000005` 也一并重现。

## 3. 今天仍崩

同一输入（`s` 为 30 个 `a`，模式 `^a*$zzz`），今天树 `2fa00e1`：

| 帧 | 本文实测（`2fa00e1`） |
| --- | --- |
| `#7` | `re_seq (+0x3a)` |
| `#8` | `re_seq (+0x1a9)` |
| `#9` | `builtin_match.part.0 (+0x12d)` |
| `#10` | `vm_execute_thread (+0x1afa)` |
| `#11` | `vm_run (+0x181)` |
| `#12` | `load_and_run (+0xed)` |
| `#13` | `main (+0xa57)` |

函数序列与旧树完全一致；偏移变动来自代码改动（`re_seq` 与 `vm_execute_thread` 都动过），不是换了个崩溃点。

- `code=0xC0000005`（访问违例），进程 `rc=139`
- 连跑 5 次，5 次都是 `0xC0000005`（不是偶发）
- `Release` 构建同样崩（`code=0xC0000005`）⇒ 不是 `RelWithDebInfo` 的配置幻觉

## 4. 机制（读到的，不是推的）

`gdb` 停在崩溃帧上，逐字如下（路径前缀略）：

```
Thread 1 received signal SIGSEGV, Segmentation fault.
re_seq (re=re@entry=0x14c413 "$zzz", s=0x0) at src/runtime/runtime.c:994
994	            if (*s != '\0') return NULL;
#0  re_seq (re="$zzz", s=0x0)                 <-- $ 分支第一句
#1  re_seq (re="a*$zzz", s=30 个 a)          <-- 回溯循环里那句
#2  regex_match
#3  builtin_match
```

崩的是 `$` 分支的第一句 `if (*s != '\0') return NULL;`（今天树的第 994 行），`s` 是 **NULL**。反汇编侧对得上：崩溃 `rip` 落在 `cmpb $0x0,(%rsi)`，即解引用 `s`。

调用帧的局部变量（同一份 `gdb` 会话）：

```
elnext = 0x7dc463 "$zzz"
count  = 0
pos    = { 0x7dc3d1 … 0x7dc3ee "", 0x0 <repeats 350 times>, ... }
```

`pos[0]`…`pos[29]` 是贪心吃下的 30 个位置；数组内其后未用到的部分是 0。而 `count` 已经是 **0** —— 于是出事的那个 `t` 只能来自 `pos[count - 1]`，也就是 `pos[-1]`：**数组下面那个字**。这一版它正好是 0，于是下一句 `re_seq(elnext, NULL)` 立刻解引用。（`count == 0` 与 `elnext` 是 `gdb` 直接读到的；`pos[-1] == NULL` 是由「`count` 为 0 时 `t` 的取值只可能来自这一处」加「被调帧拿到的 `s` 是 NULL」两条合起来的，不是直接读到的。）

源码里的那三行（数量词路径，两棵树都在这条路上）：

```c
if (count <= qmin) return NULL;
count--;
t = pos[count - 1];
```

`qmin == 0` 时，`count` 从 1 减到 0 不会被上面那道守卫拦住（`1 <= 0` 为假），于是读到 `pos[-1]`。

## 5. 判别实验：触发条件是量词下界，不是长度、也不是 `$`

每格一个独立进程，跑前清空崩溃日志：

| 输入（`s` 恒为 30 个 `a`） | 旧树 `f6b3d87` | 今天 `2fa00e1` | 量词下界 |
| --- | --- | --- | --- |
| `match(s, "^a*$zzz")` | 崩 `0xC0000005` | 崩 `0xC0000005` | 0 |
| `match(s, "^a*zzz")` | 崩 `0xC0000005` | 崩 `0xC0000005` | 0 |
| `match(s, "^a?$zzz")` | 崩 `0xC0000005` | 崩 `0xC0000005` | 0 |
| `match(s, "^a+$zzz")` | rc=0 | rc=0 | 1 |
| `match(s, "^a+zzz")` | rc=0 | rc=0 | 1 |

三读：

1. **与 `$` 无关** —— 去掉 `$` 照崩（崩点是续接里第一个解引用 `t` 的地方，谁先碰到谁崩）。
2. **与长度无关** —— 30 个字符就崩；① 那条要 2030 个，两者不是同一个构造。
3. **只与下界有关** —— `qmin == 1` 时守卫在 `count == 1` 就拦住（`1 <= 1` 为真），永远读不到 `pos[-1]`，五格全绿。

## 6. 平台对比（只写量到的）

Linux 上同一输入 `rc=0`，两个答案都对（`impossible=false`、`possible=true`）。

我另外做了两份 ASan 构建（`-O1` 与 `-O0`）跑同一输入，**都没拿到报告**。解释器把脚本跑在自备栈上，ASan 对那块栈没有红区信息，所以这份「静默」**既不能证明那里没有越界读，也不能证明有**。

因此本文不对「Linux 为什么没事」下断言：它可能是同一个越界读恰好读到一个非零且可读的垃圾值，也可能这条路径在 Linux 上根本没走到。要分清，需要能在那条循环上停下来的调试器，或对那块栈手工投毒 —— 两样这轮都没做。

## 7. ① 的修复改变了什么、没改变什么

`f1a0461` 把三处尾调用改成迭代：`$`（`if (*s != '\0') return NULL;` 之后 `re++; continue;`）、顶层交替（`re = re + alt + 1; continue;`）、无量化续接（`re = elnext; s = t; continue;`）。它在新代码的注释里自己写明「The quantifier path still recurses」，而 ② 走的正是这条量词路径。

所以 ① 的修复对 ② 的唯一影响是**崩溃点的地址**（旧树停在 `$` 的三元式上，今天停在同义的那句 `if`），不是可达性。

`docs/AUDIT.md` §1.70 的 F.1 已经把「`pos[count - 1]` 无守卫回溯读」登记为与 ① 并存的独立缺陷。本文补的不是新缺陷，是**这条缺陷的今天读数**：仍可触发、最小输入 30 字符、崩在解引用 `NULL` 上、与 ① 无因果关系。

## 8. 复现命令

Windows 侧（WSL 里调 MSYS2 的 bash；构建目录必须落在 Windows 盘上，MSYS2 的 cmake 不认 WSL 路径）：

```
git archive 2fa00e1 | tar -x -C /mnt/d/nw/today
/mnt/c/msys64/usr/bin/bash.exe --noprofile --norc -c \
  'export PATH=/usr/bin:/c/msys64/ucrt64/bin:$PATH; cd /d/nw/today && \
   cmake.exe -S . -B build-sym -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DINIMERSE_BUILD_ENGINE=ON -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe && \
   cmake.exe --build build-sym --target inimerse -j 8'
```

被测脚本（`c_star_dollar.im`，与判别实验里的 `+` 版本只差一个字符）：

```
s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
say "r=" + match(s, "^a*$zzz")
say "done"
```

跑一次并读崩溃（**先清空日志**，否则会串到上一条）：

```
/mnt/c/msys64/usr/bin/bash.exe --noprofile --norc -c \
  'export PATH=/usr/bin:$PATH; cd /d/nw/today && rm -f inimerse_crash.log && \
   ./build-sym/inimerse.exe c_star_dollar.im; echo "rc=$?"; \
   grep -o "code=0x[0-9A-Fa-f]*" inimerse_crash.log | head -1'
```

符号化（`nm` 给绝对 VA，用 `ImageBase 0x140000000` 换算成 RVA，再减运行基线）：

```
nm.exe -n --defined-only build-sym/inimerse.exe   # 找 <= RVA 的最近符号
objdump.exe -d --start-address=0x1400547d0 --stop-address=0x140054850 build-sym/inimerse.exe
gdb.exe -batch -ex run -ex 'bt 4' --args ./build-sym/inimerse.exe c_star_dollar.im
```

## 9. 没做的

- 没改 `src/**`（本作业是测量）
- 没修。修复方向只列不选：进回溯循环前先挡 `count < 1`；或把 `pos[-1]` 的语义定死；或把量词路径也做成迭代。选哪个要另开一笔，并且得配一条把 `*`、`?` 两条下界为 0 的路径钉住的测试 —— 这两条路径今天在 Windows 本来就是红的，我在 Linux 门禁上没法替它挡住（那里的 `match` 根本不是这份实现）
- 没改 `docs/AUDIT.md`、`docs/BOARD.md` 的措辞
- 没碰发布证据目录 `/mnt/d/inim-rel/src/build-windows-gcc`
