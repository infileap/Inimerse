# Inimerse 0.4.0 发布范围

## 定位

Inimerse 0.4.0 发布为“可移植脚本运行时与离线包生态”版本。它冻结并交付
VM/PAL 基础、可验证的语言运行时子集和 `inim` 包闭环；它不是 Infiverse
完整宇宙平台，也不是 Inim OS 完整桌面系统。

## 本版本交付

- 可移植 C VM、PAL/VFS 基础能力，以及 Windows/POSIX 核心运行时 parity 子集。
- Result 原语、函数级 `?`、`thread_await`、异常展开、`finally` 控制流诊断和线程完成值。
- 集合推导、TypeSet/有限枚举基础、结构化 `case`、`case try` 和有限集合覆盖诊断。
- lambda、闭包、函数值生命周期、跨线程消息和 GC 根保持。
- `eidos`/`ed` 外糖可执行子集：字段默认值、构造参数、方法闭包、单继承、覆盖和有限
  `super.method(...)`。
- `inim` 离线/本地/HTTP registry 包管理闭环：内容寻址缓存、锁文件、签名验证、
  离线重放、路径逃逸和篡改拒绝。
- 稳定的基础网络/协议探针、WASM 导出探针和 JIT 开关；`template`/`optimized` 在
  未命中特化时安全回退解释器。

## 明确不属于 0.4.0

- 完整 Eidos 对象模型：Mixin、可见性、sealed/frozen/invariant、原生对象布局、
  vtable、热修改和跨 Verse 对象映射。
- 真正的模板/类型特化 JIT、NaN boxing、分代/增量 GC 和性能加速承诺。
- 完整 Infiverse/CRP：DHT、中继、NAT 穿透、Traveller 身份、资产跨宇宙转换和共识。
- Inim OS 完整桌面、层级空间、渲染后端、裸机/微内核部署和安全中心。
- 远程 registry 运营、官方 Winget 提交、Windows 安装器和跨平台最终发行门禁。

## 发布门禁

发布候选必须满足：

1. CMake 构建、CTest、语言回归和 `inim` 回归全部通过。
2. `git diff --check` 无新增空白错误。
3. 集合审计三种 JIT 模式结果哈希一致，峰值 RSS 不超过固定门槛。
4. 发行包结构、WASM probe、DEB 的 `python3` 依赖和 `SHA256SUMS` 通过
   `tools/release_verify.py --require-wasm --require-deb-python3`；只有实际生成 Windows
   安装器并替换真实 SHA-256 后，才额外校验 Winget manifest。
5. 发布说明不把上述非目标描述为已实现。

当前本地证据（2026-09-13）：

- CTest：66/66 通过。
- `make -j2 check`：通过，包含 10 项 UPP/CRP/VDP 协议回归和 POSIX 探针。
- `tools/inim.test.py`：通过。
- Linux `tar.gz`/`zip`/`deb` 包中的 `inimerse` 与 `inim` 入口均保留可执行位。
- WASM probe：`inimerse_probe() == 0x0400`，稳定 ABI revision 仍为 `1`。
- 集合审计：规模 10000、每模式 3 次；三种模式在两个场景的结果哈希均稳定，
  峰值 RSS 最大 70324 KiB。

本文件定义发布范围，不代表 GitHub Release、签名发行包或跨平台 CI 已经完成。

构建、打包和发布门禁的具体经验记录在
[0.4.0 构建与发布经验](BUILD_RELEASE_LESSONS_0.4.0.md)。
