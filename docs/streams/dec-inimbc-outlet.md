# 自举产物出口：compiler.im 能不能直接产 INIMBC

判定页（对应卡 8f1abccb「自举产物出口：compiler.im 能否直接产出 INIMBC」）。
只判定、不实现；不碰 selfhost 任何实现文件；不改断言与旧读数。

## 结论

**选 A：让 compiler.im 直接写 INIMBC。** 缺的接口只有一个——运行时的「二进制安全写文件」；
引擎侧的装载器已经存在（下面的 B 原型已把同一份自举指令装进引擎跑起来）。
B（写 dec-artifact v1 装载器）不是错，但它要补的是文本格式里那条**没有写清楚的转义约定**，
装出来的产物仍然要靠猜；A 把这一层整段绕开。

## 证据一：容器与写出顺序已经对齐

- 头：8 字节 `INIMBC` + 版本字节 3 + 第 8 字节 0（src/compiler/bytecode.h）。
- 段顺序（src/compiler/bytecode.c 的 bytecode_write）：count(int)、字符串池（每项 len + bytes，含 NUL）、
  浮点池（double[]）、指令流（每条 4 个 int：op、r1、r2、r3，固定 16 字节）、函数段（递归同一形状 +
  argc + 名字）、线程段（同形）、try 表（每项 4 个 int）、全局名表。
- 写文件入口是 bytecode_write_file，读侧是 bytecode_read_file；引擎装载走 src/main.c 的 bytecode_read_file。
- opcode 编号在 selfhost/compiler.im 顶部常量与 src/compiler/bytecode.h 的枚举之间 0..47 逐值相同，
  整数 op 号可直接写进容器，不需要翻译表。自举表里没有 OP_CONCAT（引擎把它放在枚举末尾）。

## 证据二：A 的唯一硬障碍是「没有二进制安全写出」

运行时只有一个写文件内建（src/runtime/runtime_posix.c 里的 posix_write_file），实现是
`fputs(data.sval ? data.sval : "", f)`：C 字符串语义，遇到 NUL 就截断。而 Value 只有 `char *sval`，
没有长度字段（src/vm/vm.h）。INIMBC 里每个 int32 计数都带 NUL 字节，所以**自举语言今天没有任何写字节的能力**。
这与「A 要写多少代码」直接相关：先补这个内建，再谈编码。

## 证据三：B 原型——引擎能装载，也能跑，但产物语义有偏

用一个一次性 Python 转换器（照上面段顺序 1:1 写，未入库）把 dec-artifact v1 转成 INIMBC：

- `say "hi"` 的 108 字节产物 → 91 字节 INIMBC，引擎直接跑出 `hi`，rc=0。
- 数组/字典小程序（push / len / 字典下标）513 字节，输出 `4` 与 `1`，rc=0。
- 整份 compiler.im 的自举产物（7876 行、133941 字节，main code=493 strings=87 floats=0）→ 118818 字节 INIMBC，
  引擎**能装载、能跑起来**：模块初始化跑完，带 `--emit` 时把目标编译出来了（`emitted /tmp/q7.dec`，rc=0）。

转换途中的两个坑（B 的装载器将来都要处理）：

1. 段头不能靠「这行里有没有 code=」认。池条目 `func s 5 " code="`（字符串内容就是「 code=」，来自
   seg_header 里拼 `" code=" + str(len(...))`）会被误当成段头，按空格切分后它带着 `code="`。
   判据要改成「第二列是不是 s / m / f」。
2. 装载后的产物**行为不等于 C 侧产物**。同一个目标 `say "hi"`：C 侧产物跑出来是 108 字节、真换行；
   自举产物跑出来是 114 字节，首个差异在第 16 字节——自举侧写入的是字面反斜杠加 n，不是换行。
   同一份源 `say "a\\nb"`，引擎直接跑打印的是真换行，自举编译器发出的池条目却是转义过一次的形式
   （`main s 0 "a\\nb"`）——两侧对「字符串字面量里的反斜杠」不是同一套语义。

第 2 条不是转换器的锅（转换前后 head 段完全一致），它说明 dec-artifact v1 的池字符串转义层
**在写侧没有被写成一份可照抄的约定**：光看文本，装载器没法判断一个池条目该解几层转义。
B 要补的正是这一条，而这条属于文本格式的规格工作，不是「再写两百行解析器」能消掉的。
顺带一提，这个格式本来也表达不了 try 表与全局名表（它没有这两段）。

## 第一步（若 A）

1. src/runtime/runtime_posix.c：加一个带长度的写文件内建（`fwrite(data, 1, len, f)`，长度从参数取），
   并在 src/runtime/runtime.c 的内建注册处登记。**约 15–25 行 C。** 验法：从 .im 里写一个含 NUL 的文件，
   用 `od -c` 读回确认字节完整。
2. selfhost/compiler.im：补 4 字节小端整数编码、字符串池/浮点池写出、指令流写出，然后让
   render_artifact 走新出口。**约 100–150 行 .im。** 这一步之前先确认第 2 条转义语义按哪一侧为准
   （见「没验证」第 1 条）。

## 会不会动 CTest 计数

**不动，仍是 163。** 只有当出口单独注册一条测试（例如 selfhost_inimbc_outlet）时才 163 → 164，
并按门禁的规矩在同一行沿革注释里追加。上表的两步本身不新增测试。

## 没验证的部分

1. 那一处转义差异到底该由写侧还是 lexer 侧负责，没有定论；只确认了两侧不一致（同一份源，引擎跑出真换行，
   自举发出的池条目是转义形式）。
2. try 表：compiler.im 自己的产物里没有 try 段，但普通源可以带 try；A 的写出侧要不要支持 try 表、
   引擎读侧怎么匹配，没量。
3. 全局名表：自举产物不带全局名（B 原型按 70 个空名补过，行为不变，所以它不是 A 的阻塞项，但也没有定论）。
4. 浮点池：compiler.im 的产物 floats=0，带浮点的源没测。
5. 性能与大产物上限：118818 字节的 INIMBC 装载与执行没有计时。

## 复现命令

    ./build/inimerse selfhost/compiler.im --emit <abs>/selfhost/compiler.im /tmp/page.dec
    ./build/inimerse /tmp/page.c.inim --emit /tmp/t.im /tmp/q2.dec      # C 侧产物
    ./build/inimerse /tmp/page2.inim --emit /tmp/t.im /tmp/q7.dec       # 转换后的自举产物
    cmp /tmp/q2.dec /tmp/q7.dec

（/tmp/page2.inim 与 /tmp/conv.py 都是一次性原型，未入库；本页只记读数。）
## 实施结果（A 落地，树 f393dda 之后）

判定被采纳后按 A 实现，分两步提交：

1. 运行时：内建 write_bytes（追加在 src/runtime/runtime_posix.c 末尾；原型与注册并进既有行，上方零新增行，
   所以被引行号不位移）。第二参是 int 数组（每元素取低 8 位）或字符串（写到首个 NUL），返回写入字节数、
   失败 -1。实测 NUL、0x80、0xff 都原样落盘。
2. 自举侧：compiler.im 里的 render_inimbc 与 `--emit-inimbc <target.im> <out.inim>`，段顺序逐字节照
   bytecode_write。浮点池仍不产：遇到浮点常量会打印「浮点池未实现」并停。

读数（同一棵树、同一命令复跑）：

    gen1-vs-gen2: same sha256=40a5298ea099ac05d23a521031e7ca7c37c4958743fe3d0600c7758c199c37af bytes=125685
    artifact-format: selfhost=INIMBC 125685 bytes, c=INIMBC 125561 bytes

gen1 是引擎跑源码的产物，gen2 是 gen1 这个镜像自己跑出来的产物 —— 环在自举产物本身上闭合，不再借 C 产物的道。
已注册为 CTest selfhost_inimbc_outlet_fixedpoint（EXP_CTEST 163 -> 164，同行沿革注释）。

## 两条保真缺口（实测：镜像的行为不等于源码路径的行为）

1. stop all：自举前端把它解析成「线程控制」（selfhost/parser.im 的 431 行一带），C 侧的规则是先匹配 all
   生成整程序停止（src/parser/parser.c 的 1781 行）。后果：自举镜像里 stop 语句是 no-op —— 程序
   say "before" / stop all / say "after" 在镜像里连印两行，在 C 编译的产物里只印 before；镜像跑
   `--emit-inimbc` 写完文件后还会贯穿到默认分支，多印一句「无法读取文件: --emit-inimbc」。
2. 字符串转义：同一行 say "a\nb" 源码跑打印真换行，`--emit-inimbc` 出的镜像打印字面 a\nb。连带可见：
   镜像 `--emit` 出的 dec-artifact 把段分隔写成字面反斜杠 n（149668 B、0 换行；源码路径 141363 B、8305 行，
   差 8305 = 换行数）。

两条都不是编码器引入的：换一个只含 say 的程序同样复现。修不修、怎么修由后续卡决定，本页只记读数。

## 没验证的部分（本次新增）

- 浮点池仍未实现，带浮点常量的源会停在「浮点池未实现」。
- 镜像与 C 编译产物的行为差异只枚举了上面两条，没有系统扫；try 表与全局名表的结论沿用上一节。

