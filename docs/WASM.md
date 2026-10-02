# Wasm 后端 —— 线性内存堆、数组与 v128（v0.5 后续）

> 状态：**当前有效**（随 stream `wasm-simd-gc` 建立，2026-10-02）。
> 本文只描述 `src/compilation/wasm_backend.{c,h}` 这一个后端（`inimerse compile --abi-target wasm`），
> 不描述独立探针 `tools/wasm_probe.c`（它的 ABI 标记是 `0x0400`，见 `docs/archive/WASM_ABI.md`），
> 也不描述浏览器/Node 宿主适配器 `tools/wasm_host.js`。

本文修正 `docs/archive/RELEASE_0.5.0.md` §38 的表述。那句 "supporting SIMD optimizations and
WebAssembly GC" 把三件不同的事写在了一起：

| 说法 | 本仓库实际状态 |
| --- | --- |
| "heaps" | **已实现**：模块自有线性内存里的堆 + 16 字节块头 + 引用计数回收（本文 §4–§6） |
| "SIMD optimizations" | **部分实现**：`v128` 路径存在且已测量（§8），但**代码生成器不选它**，因此脚本不会自动获得加速 |
| "WebAssembly GC" | **未实现**，且**不是**上面那个堆（§9）。它需要 `--enable-gc` 的 struct/array 引用类型，是另一套工具链特性 |

「线性内存里的堆」和「WebAssembly GC」不是同一件事。把两者当成同一件事，正是上面那句承诺出错的原因。

## 1. 定位与判据

后端把 AST 的一个子集翻译成**独立**的 Wasm MVP 二进制；它的验收方式是**双跑对比**，不是「能编译」：

```
inimerse compile --abi-target wasm p.im p.wasm   # 生成模块
node tools/wasm_run.js p.wasm                    # 宿主运行
inimerse run p.im                                # 解释器运行（判据基线）
```

两者的 stdout 必须逐字节相同。`tools/wasm_backend.test.py` 把这个过程自动化，并同时断言
「拒绝必须是显式的」。**没有任何一种失败可以静默通过**：要么编译期报错，要么运行期
`env.im_error(code)` + trap。

## 2. ABI 与 PAL 导入表

模块导出的探针（`tools/wasm_run.js` 在运行前校验）：

| 导出 | 值 |
| --- | --- |
| `inimerse_probe()` | `0x0500`（后端专用标记；独立探针 `tools/wasm_probe.c` 用 `0x0400`，两者不可混用） |
| `inimerse_abi_version()` | `1` |
| `inimerse_capabilities()` | `0` |
| `inimerse_run(i32)` | 入口；测试程序传 `0` |
| `bench_sum_scalar(i32) -> f64` / `bench_sum_simd(i32) -> f64` | §8 的测量对 |

唯一的对外副作用通道是固定的 5 个 `env` 导入（PAL 原则：不隐式访问 FS / 网络 / DOM）：

```
env.im_print_int(i64)   env.im_print_float(f64)   env.im_print_bool(i32)
env.im_print_nil()      env.im_error(i32)
```

`tools/wasm_run.js` 是参考宿主：`fmtFloat` 镜像 `vts_double()`（`src/vm/vm.c`），
错误码 1–6 映射成人可读名字，退出码与 `inimerse run` 对齐（0 成功 / 1 脚本错误）。

## 3. 模块布局（固定常量，见 `wasm_backend.c` 顶部）

| 区域 | 地址 | 说明 |
| --- | --- | --- |
| 帧区 | `0 .. FRAME_BYTES`（4096） | 每个活动函数的 256 槽帧；`$sp` 指向当前帧 |
| 栈帧区 | `FRAME_BYTES .. GLOBALS_BASE` | `STACK_FRAMES=1024` 帧；`$sp > GLOBALS_BASE` 即 `im_error(3)` |
| 全局区 | `GLOBALS_BASE=4198400 .. HEAP_BASE` | 512 个全局槽 × 16 字节 |
| **堆** | `HEAP_BASE=4206592 .. HEAP_END=8388608` | 约 3.99 MiB 的 arena（`MEM_PAGES=128` → 8 MiB 线性内存） |

一个「值」的槽是 16 字节：`[tag i32 @+0][pad][payload i64 @+8]`。
tag：`0=nil 1=int 2=float 3=bool 4=str 5=array`。
整型运算在 32 位溢出时提升为 float（与解释器一致）；`str` 仍由解释器负责，后端在编译期拒绝。

## 4. 堆：线性内存 + 显式耗尽（"heaps" 这一段）

块头 16 字节，紧邻 payload 之前：

```
payload-16 : size   i32   块总字节数（含头，16 对齐）
payload-12 : refs   i32   引用计数
payload-8  : next   i32   空闲链表的 next（仅空闲块有效）
payload-4  : count  i32   元素个数（数组长度；len() 读这里）
payload+0  : 元素 0 的 tag，payload+8 : 元素 0 的 payload，元素 i 在 payload+16i
```

分配器 `im_heap_alloc(size)`：size 向上取整到 16；先扫 LIFO 空闲链表（first-fit），
否则 bump 指针；`blk + size > HEAP_END` 时**显式** `env.im_error(4)` + `unreachable`。
`im_heap_free` 在块紧邻 bump 顶端时回收 bump，否则压回空闲链表。
`im_retain` / `im_release` 维护 `refs`，归零时按 `count` **递归释放**元素，再交还分配器。

* 新块 `refs = 0`。只有「被某个**拥有**槽持有」时才 +1 —— 这就是为什么
  `repeat 300000 { b = [1,2,3]; s = s + b[0] }` 能在 4 MiB 里跑完：
  `(HEAP_END-HEAP_BASE)/64 = 65344`，不复用的话 6.5 万次就到顶，而实测 30 万次通过。
* 只存在于**临时槽**里的数组不会被回收（见 §6 的边界），这是有界的泄漏，不是悬垂指针。

## 5. 所有权模型（什么时候 retain / release）

| 槽 | 所有权 |
| --- | --- |
| 具名局部变量、全局变量 | **拥有**一个引用 |
| 被调用者的参数槽、返回槽 | **拥有**；参数由**调用方**在返回后释放 |
| 数组元素槽 | **拥有** |
| 帧临时槽（`slot >= TEMP_BASE=128`） | 只**借**用，不 emit 任何引用计数代码 |

写入一个拥有槽的顺序是 **先 retain 新值，再 release 旧值，最后写**：这样
`a = a[0]` 不会释放掉正要保存的对象。写 `a[i] = v` 时还会**先把基底块 pin 住**
（retain 基底、写完 release），因为右值求值可能重新赋值作为基底的变量
（`a[0] = (a = [9])`），否则就会写进已释放的块。

## 6. 数组语义（与解释器逐条对照）

| 程序 | `inimerse run` | wasm 宿主 | 一致 |
| --- | --- | --- | --- |
| `a=[1,2,3]; say a[0]; say a[2]; say len(a)` | `1 3 3` | `1 3 3` | ✔ |
| `a[1]=20; say a[1]` | `20` | `20` | ✔ |
| `b=a; b[0]=99; say a[0]` | `99`（引用语义） | `99` | ✔ |
| `a=[[1,2],[3,4]]; say a[1][0]; a[0][1]=7; say a[0][1]` | `3 7` | `3 7` | ✔ |
| `func g(){return [7,8]} say g()[1]` | `8` | `8` | ✔ |
| 函数实参/返回值传数组 | 支持 | 支持 | ✔ |
| `say a[5]` / `say a[-1]`（越界读） | `nil` | `nil` | ✔ |
| `a[5]=9`（越界写） | **增长数组**，退出 0 | `im_error(5)` + trap，退出 1 | ✖ 见下 |
| `say a`（打印整个数组） | `[1, 2, 3]` | `im_error(6)` + trap，退出 1 | ✖ 见下 |
| 堆耗尽 | 不适用（无固定 arena） | `im_error(4)` + trap，退出 1 | 由设计决定 |

**有意的分歧**：解释器的数组是自动增长、可变打印的；wasm 子集拒绝这两类操作，
但**大声拒绝**而不是给一个不同的结果 —— 静默不一致正是本后端最不能有的失败模式。
`tools/wasm_backend.test.py` 的 `EXPLICIT_FAIL_CASES` 就断言这一点（解释器 rc=0，
宿主 rc≠0 且 stderr 里出现对应错误名）。

**修掉的旧缺陷**：基线后端的打印分派写成 `if (tag != 0) { … }`，于是 tag 0
（nil）什么都不打印；`func f(){} say f()` 在基线上 wasm 输出 `0`、解释器输出 `nil 0`。
现在先判 `i32.eqz` 打 nil，再分级分派。

## 7. 错误码

| code | 名字 | 触发 |
| --- | --- | --- |
| 1 | `division_by_zero` | 除零 |
| 2 | `call_frame_overflow` | 帧溢出 |
| 3 | `call_stack_overflow` | `$sp > GLOBALS_BASE` |
| 4 | `heap_exhausted` | arena 放不下（**新增**） |
| 5 | `array_index_out_of_range` | 越界写（**新增**，有意分歧） |
| 6 | `array_op_unsupported` | 打印数组 / 非数组参与数组运算（**新增**） |

## 8. SIMD：`v128` 已实现、已测量、但**不**被代码生成器选用

模块额外导出两个函数，二者都把 `1.0` 累加 `n` 次（每步都是可精确表示的整数，
所以两个结果都**恰好**等于 `n`，可以逐位相等地断言）：

* `bench_sum_scalar(n)`：标量 `f64.add` 循环；
* `bench_sum_simd(n)`：`v128.const` + `f64x2.splat` + `f64x2.add` 双通道循环。

```
node tools/wasm_run.js --bench <module.wasm> 20000000
```

本机（Node v24.20.0）实测，同一进程内先热身再计时：

| n | scalar | simd | simd/scalar |
| --- | --- | --- | --- |
| 1 000 000 | 2.235 ms | 1.082 ms | 2.067× |
| 5 000 000 | 10.72 ms | 5.41 ms | 1.980× |
| 20 000 000（4 次） | 42.69 / 43.79 / 48.88 ms | 24.93 / 22.26 / 26.87 ms | 1.713 / 1.819 / 1.967 / 2.065× |
| 50 000 000 | 116.74 ms | 52.55 ms | 2.221× |

`wasm bench: results equal` 同时断言两者结果相等。

**为什么脚本不会因此变快**（这才是「已实现」与「已优化」的区别）：

1. 槽是 16 字节的装箱值，`v128` 一次正好覆盖一个槽 —— 批量搬运/填充没有节省；
2. 整型在 32 位溢出时提升为 float（与解释器一致），`i32x4`/`i64x2` 语义不同；
3. 浮点归约需要重结合，结果会变，而判据是**逐字节**与解释器一致。

所以本文件与 `wasm_backend.h` 的措辞是「v128 路径已实现并已测量，生成器暂不选用」，
而不是「已用 SIMD 优化」。

## 9. WebAssembly GC：**未实现**，且与上面那个堆无关

`wasm-gc` 提案要的是引用类型（`(ref null $t)`、`struct.new`、`array.new`）与
`--enable-gc` 的工具链支持，用它可以在 wasm 内部直接分配被 GC 管理的结构。
本后端做的是**线性内存里的手写堆 + 引用计数**：没有 GC 提案的依赖，
在任何 MVP 运行时上都能跑，但回收是手动的、确定的。

两者可以同时存在，也可以只做其中之一；本文只宣称做了后者。要推进 wasm-gc，
需要的是一次独立评估（工具链、Node 支持面、与 `tools/wasm_run.js` 的宿主契约），
不是把这里的堆改写一遍。

## 10. 复现本文的全部证据

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

python3 tools/wasm_backend.test.py   # 期望：wasm backend: ok (22 equivalence cases,
                                     #        3 rejections, 3 explicit failures, simd bench equal)
ctest --test-dir build -R 'wasm' --output-on-failure
bash tools/gate.sh                   # 七阶段全绿，ctest 计数不变

# 单看某一类：
inimerse compile --abi-target wasm p.im p.wasm && node tools/wasm_run.js p.wasm
wasm-validate p.wasm                 # wabt 存在时也由测试脚本断言
```

「在 main 上会失败」的反证：把 `src/compilation/wasm_backend.c` 换回基线后，
同一套用例在 `array_basic` 处即失败
（`error: wasm MVP subset: expression type 10 not supported …`，
10 = `EXPR_LIST`），且 `nil_print` 用例输出 `0` 而不是 `nil 0`。

## 11. 已知边界（未做 / 不做）

* `str` 与除数组外的集合类型仍由解释器负责，后端编译期拒绝（这是设计，不是缺陷）；
* 仅存在于临时槽的数组不会回收（有界泄漏；具名槽的回收是确定的）；
* 数组越界写不会自动增长（有意分歧，见 §6）；
* SIMD 只导出被测量的那一对函数，生成器尚未选路（见 §8）；
* wasm-gc 未实现（见 §9）；
* 后端生成的是**独立**模块，不链接引擎其余部分；与 `tools/wasm_host.js`
  （只注入显式提供的导入）之间没有集成路径 —— 后者的宿主契约要求 zero imports，
  本后端的 5 个 `env` 导入由 `tools/wasm_run.js` 提供。