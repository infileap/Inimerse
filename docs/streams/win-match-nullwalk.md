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
4. 本案没有提交脚本；可重跑的是第 9 节那几条命令

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

我另外做了两份 ASan 构建（`-O1` 与 `-O0`）跑同一输入，**都没拿到报告**。这句话只能读成「**ASan 在解释器自备栈这条路径上静默不可用**」—— 解释器把脚本跑在自备栈上，ASan 对那块栈没有红区信息 —— **不能**读成「ASan 对这段源码没用」：把同一段源码抽到普通进程栈上，ASan 立刻报越界读（§7）。就本文这条路径而言，这份静默仍然既不证明那里有越界读、也不证明没有。

因此本文不对「Linux 为什么没事」下断言：它可能是同一个越界读恰好读到一个非零且可读的垃圾值，也可能这条路径在 Linux 上根本没走到。要分清，需要能在那条循环上停下来的调试器，或对那块栈手工投毒 —— 两样这轮都没做。

## 7. ASan 那条路：静默的是「解释器自备栈」，不是 ASan

**来源**：这一节的配方与第一组读数来自另一会话 `noble-orchid`（经 `ivory-zephyr` 转来），**不是我的**。下表两列数字是我按同一配方**自己重跑**的（`gcc 15.2.0`，`-std=c11 -g -O0`／另加 `-fsanitize=address`），命令在节末。把「谁的读数」写死，是为了后人别把这一段记成本文作者的 Windows 读数。

### 7.1 切片

取 `444c5b8` 上 `src/runtime/runtime.c` 的 **897–1092 行**（196 行）：从 `re_find_close`（找配对括号）起，经 `re_seq`（回溯器），到 `regex_match`（公开入口）止，首尾都落在函数边界上，能独立编译。切完查三件：首行是 `static const char *re_find_close(`；末行是 `regex_match` 的收尾 `}`；块内 `regex_match` **恰好一次且是定义**（1073 行起、1092 行收尾）。第三查原写成「出现两次（定义 + 调用）」是错的 —— 调用点在 **1103 行**，在切片之外，那一查会永远报「切早了」。

两个坑：`regex_match` 返回 **int（0/1）**，返回指针的是 `re_seq`，按指针用编译器会直接拦；切片自己没有 `#include <stdlib.h>`，而块里用了 `malloc`／`free`，驱动必须先包 `stdio`／`stdlib`／`string` 再 `#include` 切片（在 gcc 15 上是硬错，不是警告）。

### 7.2 驱动与两份构建

驱动只做转调，不复刻任何逻辑：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regex_block.inc"

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    printf("%d\n", regex_match(argv[1], argv[2]));
    return 0;
}
```

```sh
gcc -std=c11 -g -O0                    -o one_inc  main.c
gcc -std=c11 -g -O0 -fsanitize=address -o one_asan main.c
```

### 7.3 十模式 × 八串 = 80 格，每格一个独立进程

模式 `a*b` `a?b` `x*y` `ab*` `a*` `a+b` `a*c` `[ab]*c` `a*b*c` `(ab)*c`；串 `a` `ab` `aab` `x` `b` `abc` `aac` `c`。**必须一格一进程**：批量跑时第一格段错误会把整批带走，80 格全变成 127 —— 那是量具的读数，不是实现的。

**还有一格更阴的坑：输入里只要有一个通配式。** 模式 `[ab]*c` 在 bash 里是通配式，若在 `for p in $模式列表`（不加引号）这种位置传参，会先被展开成当前目录里的文件名 —— 我在切片目录里跑时它变成了 `block.inc`，于是 `regex_match("block.inc", …)` 那四格全返回 0，**不报错、不警告**，只是静静地不崩。我第一次跑出的 16/80 就是它（见 §7.4）。两种防法：每个模式逐个加引号，或 `set -f` 关掉通配；验法是在**跑循环的那个目录**里执行 `printf '<%s>\n' [ab]*c`，打出文件名就是被吃了。

| 模式 | 量词下界 | 原样构建崩格数 / 8 | 崩在哪几格 |
| --- | --- | --- | --- |
| `a*b` | 0 | 2 | `a`、`aac` |
| `a?b` | 0 | 3 | `a`、`aab`、`aac` |
| `x*y` | 0 | 1 | `x` |
| `a*c` | 0 | 4 | `a`、`ab`、`aab`、`abc` |
| `a*b*c` | 0 | 4 | `a`、`ab`、`aab`、`b` |
| `(ab)*c` | 0 | 2 | `ab`、`aab` |
| `ab*` | 0 | 0 | — |
| `a*` | 0 | 0 | — |
| `[ab]*c` | 0 | 4 | `a`、`ab`、`aab`、`b` |
| `a+b` | 1 | 0 | — |

### 7.4 读数

- 原样构建：**20 / 80 格崩**（全部 `rc=139`）。
- ASan 构建：**同样 20 格命中** `stack-buffer-overflow READ of size 8`，且与原样构建**逐格 1:1** —— 没有一格是「原样崩、ASan 静默」，也没有一格是「ASan 命中、原样不崩」。这条一致性是本节最硬的一点：它说明那 20 格崩的是同一个越界读，不是 20 种死法。
- 命中的 20 格全在 `qmin == 0`；`qmin == 1` 的 `a+b` 一格不崩。但 `qmin == 0` **不是充分条件**：`ab*` 与 `a*` 的下界也是 0，在这八串上一个都不崩 —— 它们的贪心段要么直接成功、要么根本没进回溯，走不到「减到 0」，与 §5 的判别表同一结论。
- **一处自纠，留在原处因为它会被重犯**：我第一次跑出的是 **16 / 80**，并把「与 `noble-orchid` 报的 20/80 不一致」当成跨会话分歧记了下来。他定位到差的四格全在 `[ab]*c`，并猜我是被 shell 通配吃了 —— 查实：在切片目录里 `printf '<%s>\n' [ab]*c` 打出的是 `<block.inc>`，模式在那一步就被展开成了文件名。逐个加引号／`set -f` 重跑即得 **20 / 80**，ASan 也 20 格，与他的读数逐格同值。教训：**输入里只要有一个通配式，读数的分母就不再是你以为的那个分母**，而且它不报错、不警告，只让四格静静地不崩。
- **这个读数有两个独立来源**：`ivory-zephyr` 在 Linux x86-64 / `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0` 上从头复现（不复用任何人的数字），得 plain 崩 20 / ASan 命中 20 / `mismatch = 0`，崩格集合与本节的 20 格逐个相同。所以 20 不是孤例，**16 才是那个被量具吃掉四格的读数**。

### 7.5 越界点

ASan 的定位逐字是 `regex_block.inc:172 in re_seq`；我切的是 `444c5b8` 的 897–1092 行，故 172 行 = 源文件 **1068 行**，正是 `t = pos[count - 1];`。越界对象是那个 4096 项数组，访问点落在其**下界之下**（下标 −1）。

### 7.6 两条限定，必须同时带着

1. 这段实现**只在 Windows 分支参与编译**，POSIX 走 libc 正则、没有这条回溯。所以本节证明的是「这段源码配这个输入会越界、会崩」，**不能**推成「Linux 引擎会崩」。
2. **不是「② 已修」**。`noble-orchid` 另做过一次判别实验：把那一行改成 `t = count > 0 ? pos[count-1] : s;` 后 80 格 0 崩、60 个非崩格逐格同值 —— 但那是在**切片上、普通进程栈上**量的，不是引擎里的 ②。② 这一批不修，板行仍是「进行中」。

### 7.7 一处措辞更正（也是 `noble-orchid` 对自己的更正）

不要写「正解是先减再判」：**守卫的先后顺序本来就是对的**（判在减之前）。真正的缺口是那个数组存的是**元素匹配之后**的位置，所以「零次重复」这个候选位置（= 进入量词时的 `s`）在数组里根本不存在 —— 减到 0 之后再怎么取下标都取不到它，只会越界。本文此前若有「先减再判」的说法，以这段为准。

### 7.8 复现命令

```sh
git -C /home/sakiko/inimerse show 444c5b8:src/runtime/runtime.c | sed -n '897,1092p' > regex_block.inc
gcc -std=c11 -g -O0                    -o one_inc  main.c
gcc -std=c11 -g -O0 -fsanitize=address -o one_asan main.c
for p in 'a*b' 'a?b' 'x*y' 'ab*' 'a*' 'a+b' 'a*c' '[ab]*c' 'a*b*c' '(ab)*c'; do
  for s in a ab aab x b abc aac c; do
    ./one_inc  "$p" "$s" >/dev/null 2>&1; echo "$p $s raw_rc=$?"
    ./one_asan "$p" "$s" 2>&1 | grep -m1 'ERROR: AddressSanitizer'
  done
done
```

## 8. ① 的修复改变了什么、没改变什么

`f1a0461` 把三处尾调用改成迭代：`$`（`if (*s != '\0') return NULL;` 之后 `re++; continue;`）、顶层交替（`re = re + alt + 1; continue;`）、无量化续接（`re = elnext; s = t; continue;`）。它在新代码的注释里自己写明「The quantifier path still recurses」，而 ② 走的正是这条量词路径。

所以 ① 的修复对 ② 的唯一影响是**崩溃点的地址**（旧树停在 `$` 的三元式上，今天停在同义的那句 `if`），不是可达性。

`docs/AUDIT.md` §1.70 的 F.1 已经把「`pos[count - 1]` 无守卫回溯读」登记为与 ① 并存的独立缺陷。本文补的不是新缺陷，是**这条缺陷的今天读数**：仍可触发、最小输入 30 字符、崩在解引用 `NULL` 上、与 ① 无因果关系。

## 9. 复现命令

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

## 10. ① 的调用链：崩溃前一刻它到底走不走得动

这一节回答的是「`RtlCaptureStackBackTrace` 拿不到那 26 帧，是因为**数据不在**，还是因为**那一刻没有可用的栈或展开信息**」，**不是**「怎么修」。本节出现的一切都只在旧树 `f6b3d87` 的符号构建上量。

### 10.1 链在不在、读不读得到（第 26 帧上量）

把断点设成「第 26 次进入 `re_seq`」（此时栈还没碰到守卫页），按 `0x8080` 的步长从 `rsp` 往上逐字读：

| 位置 | 读到 |
| --- | --- |
| `rsp + k*0x8080`，k = 0…24 | 25 个连续返回地址，**全部等于同一个值**（`re_seq` 的返回地址） |
| k = 25 | 换成 `regex_match` 的返回地址（链的顶） |
| k = 26、27 | 不可读（nil） |

⇒ **链在、可读、步长规整**。这一步只说明「那 26 帧的返回地址确实写在可读内存里」，不说明 API 走得动。

### 10.2 展开信息走不走得动

同一时刻让 gdb 自己走（它读 `.pdata`，遇到没有展开信息的函数再靠序言分析兜底）：**29 帧全部走完**，包括穿过 `___chkstk_ms` 的那几帧。

⇒ 在崩溃前一刻，**展开信息也是可用的** —— 至少对一个带兜底的展开器是。

### 10.3 进程内 API 这条路：量具自证失败，不报读数

想在进程内直接调一次 `RtlCaptureStackBackTrace` 只能借 gdb 的 `call`，而 gdb 会先插一个自己的哑帧。**正控没通过**：在第 3 帧上做同一个调用，也只 `CAPTURED=1`。所以这一格记「没量到」，不记「量到走不动」。

### 10.4 今天有没有帧指针可走

`re_seq` 的序言是 `push %r15; mov $0x8038,%eax; push %r14/%r13/%r12/%rbp/%rdi/%rsi/%rbx; call ___chkstk_ms; sub %rax,%rsp` —— **没有** `mov %rsp,%rbp`；`%rbp` 是被保存下来的通用寄存器，函数体里还被复用（`mov %rax,%rbp`、`lea 0x30(%rsp),%rbp` 等）。

⇒ 「`-fno-omit-frame-pointer` + 手走 `rbp` 链」这条路今天**没有可走的东西**，它必须先重编译，也就改掉了被测对象，不能拿现有发布证据演示。

### 10.5 合起来的读法

| 位置 | 内存里有没有那 26 帧 | 展开信息能不能过 | 这一格的结论 |
| --- | --- | --- | --- |
| 崩溃前一刻（第 26 帧） | **有**（10.1） | **能**（10.2） | 数据层面不是「拿不到」 |
| 出错那一刻（handler 内） | 未单独量（同一条栈，早于它时就已可读） | 未单独量 | `docs/AUDIT.md` §1.70 记的是 7 帧 |
| 进程内 API 在第 26 帧 | 未量到（10.3 正控失败） | 未量到 | 不报 |

所以「① 的调用链结构性拿不到」这句**过强**：在崩溃前一刻，链和展开信息都在。剩下没量的是「那一刻 API 自己为什么停」，以及 §1.70 记下的「`___chkstk_ms` 那几帧缺展开信息」是否就是全部原因。

**`StackWalk64` 不是第二条路**，它是同一条路加一层依赖：它同样要读那页内存、同样要读展开信息。真正的第二条路只有两类 ——「不必读那页内存」（追尾栈、把帧先复制出来），或「不必读展开信息」（帧指针链，或自己记账）。

## 11. 没做的

- 没改 `src/**`（本作业是测量）
- 没修。修复方向只列不选：进回溯循环前先挡 `count < 1`；或把 `pos[-1]` 的语义定死；或把量词路径也做成迭代。选哪个要另开一笔，并且得配一条把 `*`、`?` 两条下界为 0 的路径钉住的测试 —— 这两条路径今天在 Windows 本来就是红的，我在 Linux 门禁上没法替它挡住（那里的 `match` 根本不是这份实现）
- 没改 `docs/AUDIT.md`、`docs/BOARD.md` 的措辞
- 没碰发布证据目录 `/mnt/d/inim-rel/src/build-windows-gcc`
