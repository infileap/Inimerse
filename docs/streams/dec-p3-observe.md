# 第 3 级 · P3 驱动面观测（只量，不改码）

树：2a165b5（工作树：本页之外无改动）。所有命令在仓库根跑，产物落 `/tmp`。这一页只回答「argv 怎么走、产物怎么比」，不给方案；方案在主会话手上。

## 1 argv 怎么进编译器

命令（0 次递归的基准线）：

```
./build/inimerse selfhost/compiler.im --emit <abs>/selfhost/compiler.im /tmp/a.txt
```

期望：`args()` 的契约是「脚本路径之后的参数」，脚本名不在里面。

实测：印 `emitted /tmp/a.txt`；`compiler.im` 从 `clargs[0]` 开始认分支（`--emit` 不足三个参数时印用法）。探针面：`./build/inimerse /tmp/args_probe.im X Y` 印 `argc=2 / arg:X / arg:Y`。

结论：是。编译期 argv = 解释器给的、脚本路径之后的那一段。

## 2 编译后的程序怎么拿 clargs

命令：

```
./build/inimerse compile /tmp/args_probe.im /tmp/ap1.inim
./build/inimerse /tmp/ap1.inim X Y
./build/inimerse run /tmp/ap1.inim X Y
```

实测：两条运行路径都印 `argc=2 / arg:X / arg:Y`，与解释器逐字相同。产物里没有 argv 副本：`.inim` 的字符串池只出现内建名 `args` 与脚本路径一次（依赖尾注），`args()` 是一次不带操作数的 CALL_BUILTIN；argv 由引擎在装载时放进 `vm->argv`。

结论：是——运行期读的是**执行引擎**的 argv，不是编译期那份；产物本身与 argv 无关（两次编译同一脚本 `/tmp/ap1.inim` 与 `/tmp/ap2.inim` 逐字节相同）。

## 3 递归编译一次 vs 两次（产物）

| 递归次数 | 命令 | 实测 |
|---|---|---|
| 0 | 解释器跑 `selfhost/compiler.im --emit … /tmp/a.txt` | 7876 行 / 133941 B / md5 `8e0b5f1aa836f97300aa630a4a74e2b5` |
| 1 | `./build/inimerse compile selfhost/compiler.im /tmp/compiler.inim` 再 `./build/inimerse /tmp/compiler.inim --emit … /tmp/s2.txt` | 与 a.txt `cmp` 相同、md5 相同；再跑一次得 `/tmp/s2b.txt` 也相同 |
| 2 | 拿 dec-artifact 当下一代编译器 | **未定** |

第 2 次未定的原因不是没跑，是没有入口：`dec-artifact v1` 只有 `render_artifact` 写出，`src/`、`tools/`、`CMakeLists.txt` 里 grep 不到任何读取或执行它的路径（命中 0 处）。两份产物格式也不同名：C 产物是 `INIMBC` 二进制（`/tmp/compiler.inim` 118772 B），自举产物是 `dec-artifact v1` 文本（133941 B）；前者能跑，后者今天谁都不跑。

另外测了 P3 卡标题那件事（编译后的编译器能不能继续编译）：`./build/inimerse selfhost/compiler.im selfhost/compiler.im` 走的是「编译目标再 `vm_exec` 它」的分支，但**相对路径下内层读不到文件**（引擎把 CWD 切到脚本目录，`selfhost/compiler.im` 变成两层目录），印 `无法读取文件: selfhost/compiler.im`；换成绝对路径则 25 秒无输出、被 `timeout` 杀掉（rc 124）。两条都不是「一次编译就停」。

结论：0 次 vs 1 次**是**逐字节相同（在 2a165b5 上，md5 `8e0b5f1a…`）；2 次**未定**，缺的是一个能执行 `dec-artifact v1` 的东西。

## 4 负对照 A：不传参数

命令：`./build/inimerse /tmp/compiler.inim`

实测：`用法: inimerse selfhost/compiler.im <script.im>`，rc 0。

结论：它没有去编译空文件，走的是 `len(clargs) < 1` 的用法分支；但错误路径同样 rc 0（与 `--dump` 读不到文件同形），**rc 不能当判据**。

## 5 负对照 B：传三个参数

命令：`./build/inimerse /tmp/compiler.inim A B C`

实测：`无法读取文件: A`，rc 0。

结论：`clargs[0]` 就是第一个实参，**不含脚本名**；`compiler.im` 里那条 `clargs[0] == "compiler.im"` 的兼容分支在今天的调用形状下走不到（要有名字等于脚本名的 argv[0] 才成立）。

## 6 留给下一格的两件事

1. 2 次递归缺执行器：要么 P5 的定点只比 0 次与 1 次（读数已经够），要么先写 `dec-artifact` 的执行器——那是新功能，属主会话决定。
2. 嵌套 run 的预期语义（P3 卡标题）：今天的行为是「编译目标并立刻执行」，不是「只编译」；相对路径那条失败是引擎切 CWD 与自举前端相对路径解析撞在一起，定位到这句话就够了。
