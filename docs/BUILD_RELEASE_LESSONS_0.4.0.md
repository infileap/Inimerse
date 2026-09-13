# 0.4.0 构建与发布经验

本文记录 Inimerse 0.4.0 从源码构建到 Linux 发布候选验证过程中确认的工程约束。
它服务于后续版本维护，不把研究性设计或未完成的平台能力写成已交付功能。

## 版本信息必须单一化

版本号会同时出现在 CMake、公共头文件、CLI 输出、安装器、WASM probe、发行包
文件名和 CI 工作流中。任何一处单独维护都会产生“包名正确但运行时版本错误”或
“源码版本正确但发布门禁仍接受旧版本”的漂移。

当前约定：

- CMake 通过 `INIMERSE_VERSION_STRING` 注入构建版本；
- 非 CMake 构建在 `src/common/common.h` 保留与当前发布线一致的回退值；
- CLI、CPack、安装器和 WASM probe 都由同一发布版本检查；
- 修改版本号时，必须同时检查 `CMakeLists.txt`、`common.h`、`installer.iss`、
  工作流和发布验证脚本。

## CMake 与 Makefile 必须覆盖同一源集

项目同时支持 CMake 和 Makefile。新增运行时模块时，如果只更新其中一个构建入口，
会出现本地测试通过但另一条构建链缺符号、漏注册或漏测的情况。新增源文件后应检查：

```bash
rg -n "src/|closure|registry|typeset|enum|error_types|ed25519" CMakeLists.txt Makefile
```

构建入口应覆盖相同的核心模块，并至少各自完成一次干净构建和测试。

## 并行构建不能依赖伪顺序

`make -j2 clean all` 会把 `clean` 和 `all` 当作可并行目标，可能在编译过程中删除
刚生成的对象文件。需要顺序清理和构建时，应使用递归 Make：

```make
linux:
	$(MAKE) clean
	$(MAKE) all
```

或者分成两个显式命令执行。不要把 `clean all` 放在同一个可并行目标列表中。

## 发行包检查内容，不只检查文件名

文件存在和 SHA-256 正确并不能证明包可用。发布验证还应检查：

- `tar.gz`、`zip`、`deb` 的版本名；
- tar/zip 中的 `bin/inimerse` 和 `bin/inim`；
- Unix 可执行权限；
- WASM probe 的安装位置；
- DEB 的 `/usr/bin` 入口和运行时依赖；
- `SHA256SUMS` 对当前文件重新计算后匹配；
- Winget manifest 不含占位摘要。

特别是 ZIP：压缩包内的权限位和 Python 解压后的文件模式不一定表现相同，因此
验证器应直接读取 ZIP 条目的 Unix mode，而不是只检查解压目录中的权限。

## DEB 运行时依赖必须显式声明

`inim` 的本地/HTTP registry 辅助路径依赖 Python 运行时。构建机存在 Python
不能替代用户安装时的依赖声明，CPack DEB 必须生成：

```text
Depends: python3
```

发布门禁使用 `--require-deb-python3` 强制检查这一点。

## WASM probe 的版本策略

WASM probe 同时承担“资产确实被安装”和“构建版本正确”的轻量检查。0.4.0 使用：

- probe 标记：`0x0400`；
- ABI revision：`1`。

发布版本标记可以随版本递增；稳定 ABI revision 只有在 ABI 合约发生不兼容变化时
才递增。两者不能混为一个数字，也不能只验证源码中的常量而不验证发行包中的文件。

## 发布门禁要验证可复现证据

发布验证脚本应拒绝损坏归档、缺失入口、错误权限、错误依赖、摘要不匹配和占位值。
测试文件同时覆盖正例和负例，避免验证器因为“文件存在”而放过不可安装或不可运行的包。

0.4.0 的最小重复验证命令：

```bash
cmake --build build-local
ctest --test-dir build-local --output-on-failure
make -j2 check
python3 tools/inim.test.py
python3 tools/eidos_desugar.test.py
python3 tools/eidos_runtime.test.py build-local/inimerse
python3 tools/release_verify.test.py
python3 tools/release_verify.py build-local --version 0.4.0 \
  --require-wasm --require-deb-python3
git diff --check
```

重新运行 CPack 后必须重新生成摘要文件：

```bash
sha256sum build-local/inimerse-0.4.0-Linux-x86_64.tar.gz \
  build-local/inimerse-0.4.0-Linux-x86_64.zip \
  build-local/inimerse-0.4.0-Linux-x86_64.deb \
  > build-local/SHA256SUMS
```

## 发行平台边界要写清楚

本轮交付的是 Linux `tar.gz`、`zip` 和 `deb`。Windows 构建工作流可以继续作为
编译和测试验证，但在没有真实 Windows 安装器及真实 SHA-256 之前，不应把它放入
正式 Release 资产，也不应让 Winget manifest 通过发布门禁。

同理，`future/` 下的 Infiverse、完整 Eidos 和 Inim OS 设计文档属于规划材料。
发布说明只能引用已经实现并经过测试的子集。

## 0.4.0 验证记录

截至 2026-09-13：

- CTest：66/66 通过；
- `make -j2 check` 通过；
- `inim`、Eidos desugar/runtime 和发布验证测试通过；
- Linux 三种发行包结构、摘要、WASM probe 和 DEB `python3` 依赖通过；
- 集合审计三种模式结果一致，规模 10000，每模式 3 次，峰值 RSS 最大约
  70324 KiB。
