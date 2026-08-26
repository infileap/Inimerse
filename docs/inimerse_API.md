# Inimerse API 与语言参考（0.2.0）

本文描述仓库当前实现（源码位于 src/）。future/ 中的设计属于规划，除非特别标注，不应视为已可用 API。

## 1. 最小程序

~~~im
say "Hello, Inimerse!"
~~~

源文件通常使用 .im 扩展名；工作台和命令行解释器均以 UTF-8 读取文件。语句以换行或分号分隔，支持 # 单行注释与 /* 多行注释 */。

## 2. 值与字面量

~~~im
i = 42
f = 3.14
name = "Ada"
ok = true
none = nil
arr = [1, 2, 3]
dict = {"x": 1}
set = {1, 2, 3}
~~~

运行时类型包括 Int、Float、Str、Bool、Array、Dict、Set、Function、Task 等。Float 遵循 IEEE-754；NaN 不属于实数集合 R。字符串使用 UTF-8，支持常见反斜杠转义。

## 3. 集合与区间

逗号表达式和花括号都可构造有限集合：

~~~im
a = 1, 2, 3
b = {1, 2, 3}
~~~

区间端点使用方括号表示闭区间、圆括号表示开区间，波浪号表示离散整数范围：

~~~im
open = [1, 10)
closed = (1, 10]
ints = [1~10]
left = (, 5]
right = [5, )
real_range = R[0.0~1.0]
~~~

集合推导式：

~~~im
squares = {x * x in [1~10] | x % 2 == 0}
~~~

### in 运算

x in S 判断标量成员关系；数组或集合作为左值时，判断其是否为右侧集合的子集。

~~~im
3 in {1, 2, 3}
[1, 2] in [1~5]
{1, 3} in [1~5]
~~~

### be 约束绑定

be 声明带集合约束的变量，并在初始化及后续赋值时检查：

~~~im
level be Z[1~100] : 1
mode be {"easy", "hard"} : "easy"
~~~

越界赋值会抛出运行时错误。Z 表示整数集合，R 表示有限实数集合。

## 4. 运算符

支持算术 + - * / %、比较 == != < > <= >=、逻辑 and or !（兼容 && ||）、区间 ..、集合推导 |、箭头 ->。复合赋值包括 +=、-=、*=、/=；自增自减为 ++、--。复杂表达式建议使用括号。

## 5. 变量、常量与作用域

~~~im
int count = 0
float ratio = 0.5
str title = "demo"
bool ready = false
const VERSION = "1.0"
global shared = 10
declare external_value
~~~

块作用域由大括号创建；函数内部变量默认局部。global 显式访问模块全局变量。with 可提供临时上下文。

## 6. 控制流

~~~im
if score >= 90 {
    say "A"
} elif score >= 60 {
    say "pass"
} else {
    say "retry"
}
while ready {
    tick()
    if done { break }
    continue
}
for item in items {
    say item
}
repeat 3 { say "once" }
do { step() } until finished
~~~

## 7. case 匹配

case 支持常量、比较条件、集合成员和正则匹配：

~~~im
case x {
    1, 2 : { say "small" }
    < 0 : { say "negative" }
    in {3, 4} : { say "allowed" }
    match "yes.*" : { say "matched" }
    else : { say "other" }
}
~~~

分支按源代码顺序检查；else 为默认分支。

## 8. 函数

~~~im
func add(a, b) {
    return a + b
}
result = add(2, 3)
~~~

fn 是由脱糖层转换的函数简写形式。函数可作为值传递，也可捕获外层变量。

## 9. 任务、线程与消息

~~~im
task worker {
    send "done"
}
t = start worker
message = recv t
join t
~~~

task 创建可调度任务；thread 请求独立线程。生命周期操作：start、join、stop、pause、resume、restart、kill。yield 主动让出执行权；send/recv 用于消息传递；lock/unlock 保护共享资源。

## 10. 异常与模块

~~~im
try {
    risky()
} catch err {
    say err
} final {
    cleanup()
}
throw "invalid input"
~~~

~~~im
import math
import net as network
include "common.im"
using math
~~~

final 块无论是否发生异常都会执行。import 加载模块，include 插入源文件，using 导入名称，as 设置别名。模块搜索路径由运行目录、标准库目录和环境变量共同决定。

## 11. GUI / Stage DSL

~~~im
stage main {
    background "#10131a"
    sprite hero {
        goto 100, 80
        show
        size 1.0
    }
    text "Welcome"
}
~~~

动作和事件包括 show、hide、move、goto、face、turn、point_to、velocity、gravity、bounce、sound、music、broadcast、clone、forever、when、cursor、window 和 delete。

## 12. 内置 API

运行时内置函数按功能分为：IO（文件、目录、标准流）；系统（环境变量、路径、时间、进程、平台信息）；网络（HTTP、Socket、端口探测）；构建（imjar 打包、模块扫描）；社交与身份（GitHub/Bilibili 授权）；AI（模型、会话、多目标 say）；Verse（项目发布、下载、列举、删除）；GUI（窗口、舞台、精灵、事件）。

稳定函数签名见 API_REFERENCE.md 和 API_BUILTIN_TABLE.md。平台能力应通过 src/platform 的 PAL 接口访问。

POSIX 核心构建额外提供以下跨平台基础函数：`read_file(path)` 返回文件文本，`write_file(path, content)` 返回布尔成功值，`input(prompt)` 读取一行标准输入，`time_ms()` 返回单调毫秒计数，`sleep_ms(ms)` 暂停当前任务。文件路径仍受宿主工作目录和沙箱策略约束。

## 13. 命令行

~~~text
inimerse                启动 REPL
inimerse run file.im   执行脚本
inimerse build project 构建项目或 imjar
inimerse --help        查看选项
~~~

## 14. UPP 本地协议参考

`tools/upp_reference.js` 提供无传输依赖的 JSONL 协议实现，可承载于标准输入输出、TCP 或测试回环。每帧包含 `upp: 1`、`type` 和 `payload`，单帧上限为 1 MiB。

已实现的控制帧包括：

- `hello` / `welcome`：角色、Verse manifest 和能力协商。
- `heartbeat`：递增序号与时间戳。
- `start` / `stop`：启动入口、参数和停止原因。
- `log`：`debug`、`info`、`warn`、`error` 结构化日志。
- `crash`：错误信息、退出码和时间戳。
- `incompatible`：报告所需与实际协议版本。

使用 `node tools/upp_reference.test.js` 可运行协议回归测试。
使用 `node tools/upp_reference.js --generate <project-dir>` 可根据项目目录生成包含入口、能力、接口和文件 SHA-256 摘要的 manifest。

CRP 阶段 2A 的本地参考实现位于 `tools/crp_reference.js`，提供 `FIND`（发现 Verse）、`PORTAL`（建立连接）和 `SIGNAL`（事件信号）三类 JSONL 帧；测试命令为 `node tools/crp_reference.test.js`。
阶段 2B 提供无第三方依赖的 HTTP 中继参考服务 `tools/crp_relay.js`，端点为 `POST /register`、`GET /find?q=`、`POST /portal`、`POST /signal`，以及 `POST /content`、`GET /content/<sha256>` 内容寻址接口；测试命令为 `node tools/crp_relay.test.js`。
`tools/crp_client.js` 提供带指数退避和取消支持的 `CrpClient`，可调用 `find`、`portal` 和 `signal`；测试命令为 `node tools/crp_client.test.js`。
`CrpClient.fetchContent(hash, sources)` 会按顺序尝试多个中继源，并在返回后重新计算 SHA-256；任一源返回篡改内容都会被拒绝并继续尝试下一源。
中继 `PORTAL` 返回绑定 Verse/对端的短期 HMAC 能力令牌；向 `SIGNAL` 携带 `token` 时会验证签名、有效期和能力范围。

## 15. 当前实现边界与 future 规划

以下能力已在 future/ 中设计但不保证当前版本可用：Eidos 面向对象完整语法、getter/setter、Mixin、sealed/frozen/invariant、热修改与宇宙变换、Inim OS 用户态进程/VFS、JIT/AOT/LLVM、NaN Boxing、指针压缩、Rope、分代 GC、SIMD 和 Native ABI。

规划细节请阅读：

- [愿景.md](../future/愿景.md)
- [面对对象.md](../future/面对对象.md)
- [优化路线.md](../future/优化路线.md)
- [优化路线pro.md](../future/优化路线pro.md)
- [Inim OS总纲.md](../future/Inim%20OS总纲.md)
- [Inim OS特性.md](../future/Inim%20OS特性.md)

## 16. 兼容性与性能建议

使用 UTF-8、显式括号和明确类型；避免依赖目录遍历顺序及操作系统路径格式。大量集合查询应复用集合对象，避免循环中重复构造区间。并发任务之间优先传递消息而非共享可变状态；IO 和网络调用应放在独立任务中。

API 以源码和测试为最终依据。若文档与实现不一致，请提交最小复现并注明运行平台、版本和输入文件。
