# `src/` 作用域普查的盲区：正向 4 个真实例，反向 2 个设计接口

## 1. 四项回报

- `pwd` = `/home/sakiko/inimerse/.worktrees/src-scope-blindspot`
- 测量基准 ref = **`344a37b`** —— 本文所有行号与计数都在这一棵上量；写文档时 `main` 已前进到 `f1dfe62`
- 读法：历史用 `git log --all -S<name>`；调用点用 `git grep -nEo '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' <ref> -- src/ mods/ selfhost/ tools/`，`docs/` 与 `future/` 不进扫描集
- 脚本（可重跑）：正向 `/tmp/oob/boundary.py <ref>`；反向 `/tmp/oob/boundary_rev.py <ref>`

## 2. 那对死符号：从来没被调用过，不是回归

`bytecode_read_file_compat` 与 `bytecode_check_compatible` 今天的样子是：外层 0 调用点，内层的唯一调用者在外层体内。

```
344a37b:src/compiler/bytecode.c:716  Bytecode *bytecode_read_file_compat(const char *path, int expected_byte_version, int expected_abi_version) {
344a37b:src/compiler/bytecode.c:717      if (!bytecode_check_compatible(path, expected_byte_version, expected_abi_version)) return NULL;
344a37b:src/compiler/bytecode.c:726  int bytecode_check_compatible(const char *path, int expected_byte_version, int expected_abi_version) {
344a37b:src/compiler/bytecode.h:147  Bytecode *bytecode_read_file_compat(const char *path, int expected_byte_version, int expected_abi_version);
344a37b:src/compiler/bytecode.h:148  int  bytecode_check_compatible(const char *path, int expected_byte_version, int expected_abi_version);
```

`git log --all -S` 对两个名字各只返回**一笔**提交：`7be36c6`（2026-10-01，"Complete v0.5: …"）。在 `7be36c6^` 两个字符串**不存在**；在 `7be36c6` 它们已带着那条内部调用存在。⇒ **没有任何一笔提交「去掉过」调用点 —— 因为没有可去掉的。** 按「零有两种」的判据，这是**第一种：从来没被调用过（设计遗留）**。

**边界**：`7be36c6` 是一笔**压平的发布提交**（59 文件、+26740/−167），所以「它诞生时有没有调用者」只能在提交边界上回答，而边界上恒为「没有」。它自己的 `docs/CHANGELOG_0.5.0.md:129` 说这两个名字 `matched a non-existent header layout; rewritten against the real INIMBC container` —— 那只能证明它在同一版里被重写过，**不能证明重写前后有没有调用者**。⇒ **一次 squash 会把「它当时有没有调用者」变成不可观测的；压平之后，设计遗留与「曾经有过、后来断了」在历史上不可分**（在今天的树上同样不可分）。

## 3. 正向：`src/` 内定义、调用点只在 `mods/` —— 4 个

| 符号 | 定义（`344a37b`） | `src/` 内 | `src/` 外调用点 | 属于哪个模块 |
|---|---|---|---|---|
| `vm_register_builtin_safe` | `src/vm/vm.c:1721`（声明 `src/vm/vm.h:400`） | 2（定义+声明） | `mods/build/build_mod.c:741` | build | [obs: 344a37b]
| `compiler_get_using_mods` | `src/compiler/compiler.c:3156` | 2 | `mods/build/build_mod.c:351` | build | [obs: 344a37b]
| `vm_debug_threads` | `src/vm/vm.c:5129` | 2 | `mods/debug/debug_mod.c:604` | debug | [obs: 344a37b]
| `vm_debug_jump` | `src/vm/vm.c:5072` | 2 | `mods/debug/debug_mod.c:593`、`:599` | debug | [obs: 344a37b]

四个都是**导出的（非 `static`）**、都在 `src/` 内、**唯一调用者都在 `mods/`**。

⇒ **一个只扫 `src/` 的「零调用点」普查，今天会把 4 个活符号报成零。`mods/` 是每一个 `src/` 作用域普查的盲区。**

这一族里最刺眼的一个是 `vm_register_builtin_safe`：头文件注释逐字写着 `/* dangerous: blocked in safe_mode */`，而它**恰好只有一个调用者，那个调用者就是 `build`**。它的名字描述的功能与它是否存在，是两件事；它的名字描述的**危险**与它是否被用，也是两件事。

## 4. 被排除的 6 个候选，与两种假阳性来源

同一个脚本在 `src内<=2 且 src外>=1` 下报出 10 个，逐条读原文后排除 6 个：

| 候选 | 排除理由（逐字） | 假阳性来源 |
|---|---|---|
| `builtin_gui_fullscreen` | `tools/check_text_integrity.py:12` 是一段**文档示例**：`$ grep -n 'gui_fullscreen' src/mod/gui_mod.c` / `1661:static int builtin_gui_fullscreen(VM *vm) {`；且它是 `static` | 文本在描述一次命中 | [obs: 344a37b]
| `compile_index_set_chain` | `selfhost/compiler.im:393 func compile_index_set_chain(ctx, target, last_idx_expr, value) {` —— 自举编译器自己的同名函数 | 跨语言同名 | [obs: 344a37b]
| `handle` | `tools/mcp_server.js:59 handle(msg);` —— JS 局部函数 | 跨语言同名 | [obs: 344a37b]
| `vts_double` | `tools/aot_native.test.py:43` 是**注释**：`# interpreter's vts_double (src/vm/vm.c) exactly, …` | 文本在描述一次命中 | [obs: 344a37b]
| `test_drain` | `tools/verse_closed_loop.test.py:134 def test_drain(root):` —— Python 测试函数 | 跨语言同名 | [obs: 344a37b]
| `test_undo` | `tools/verse_closed_loop.test.py:146 def test_undo(root):` —— Python 测试函数 | 跨语言同名 | [obs: 344a37b]

⇒ **一个按名字匹配的普查，它的假阳性有两种来源 —— 名字在别的目录里、以及名字在别的语言里；第二种不会因为扩大目录范围而消失。** 还有第三种，与前两种不同：**「命中」有两种 —— 真的命中，和一段文本在描述一次命中**（前两个候选属于这种）。

## 5. 反向：`mods/` 内定义、引用只在 `src/` —— 2 个

| 符号 | 定义（`344a37b`） | `mods/` 内 | `mods/` 外的命中 | 性质 |
|---|---|---|---|---|
| `build_mod_register` | `mods/build/build_mod.c:739` | 1（定义） | `src/main.c:39`（声明）、**`src/main.c:592`（调用，在 `register_world_modules` 里）** | 宿主调 mod 的注册函数 | [obs: 344a37b]
| `build_project_impl` | `mods/build/build_mod.c:611` | 1（定义） | `src/main.c:6`（声明）、**`src/main.c:1181`（调用）**、`src/platform/posix_stubs.c:9`（**POSIX 桩定义**） | 宿主调 mod 的实现，POSIX 侧用桩顶掉 | [obs: 344a37b]

⇒ **这条规律是双向的，但不是对称的**：反向只有 2 个，而且两个都是**设计好的宿主↔mod 接口**（各只有 1 个真实调用点，其余是声明或桩定义）；正向有 4 个，那才是盲区真正咬人的地方 —— 它们是 `src/` 自己的符号，只是恰好只有 `mods/` 在调。

## 6. 判据

1. **只扫 `src/` 的「零调用点」普查会把 4 个活符号报成零**；一个边界错一次是疏忽，**同一个边界在同一行上错两次，说明那条边界从来没被写下来过**（`mods/build/build_mod.c:741` 先后纠正了同一个人的两个结论）。 [obs: 344a37b]
2. **出现次数 ≥ 3 不证明可达**：`bytecode_check_compatible` 有 3 次出现，足以骗过一次按次数做的普查 —— 要读调用者。
3. **「零」有两种**：从来没被调用过（设计）与曾经被调用过、后来断了（回归），今天的样子完全一样；**而一次 squash 会把前一种的「诞生时有没有调用者」变成不可观测的**。
4. **按名字匹配的普查有三种假阳性**：名字在别的目录里、名字在别的语言里、以及一段文本在描述一次命中。第二种不会因为扩大目录范围而消失。

## 7. 没测到

- `bytecode_read_file_compat` 是给谁留的（是否给旧 ABI 的读入路径预留）
- `7be36c6` 压平之前的中间状态
- `mods/` 之外还有没有别的目录级盲区（本轮只量了 `selfhost/` 与 `tools/`，**它们各有假阳性、0 个真实例**）
- 反向普查只覆盖了 `mods/` 两个 `.c` 文件（`mods/build/build_mod.c`、`mods/debug/debug_mod.c`）里定义的非静态函数；`mods/*/main.im` 里的 Inimerse 侧符号不在这个脚本的口径里
