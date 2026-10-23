# 作业单：`win-source-attribution`

- **认领者**：`vivid-anchor`
- **分支**：`stream/win-source-attribution`（base `main` = `9969e5b`）
- **工作树**：`.worktrees/win-source-attribution`
- **裁定依据**：`docs/AUDIT.md` §1.71（Linux 门禁对 12 个 `src/**/*.c` 结构性失明）。本文是该节的**测量附录**，不是它的替代：§1.71 说「这些文件只在 `if(WIN32)` 里」，本文回答**每一个到底是哪一类 Windows-only**。
- **测量 ref**：**`d27413a`**（下文行号一律 `<ref> @ d27413a`）

## 1. 四项回报

1. `pwd` = `/home/sakiko/inimerse`
2. `git rev-parse HEAD` = `9969e5b02a0627d369c613bcd32d3546c30af483`（= `main`）。**全部测量跑在 `d27413a`**：`git merge-base --is-ancestor d27413a 9969e5b` → rc=0，且 `git diff --stat d27413a 9969e5b -- src/ CMakeLists.txt` **输出为空** ⇒ 期间 `main` 只动了 docs，测量可迁移。
3. 读法：①② 用 `git show d27413a:<path>` 读**提交字节**；③ 用 Windows 侧导出树 `D:/inim-rel/d27413a-src`（`git archive d27413a | tar -x` 解出，`grep -c 'add_test('` = 148 钉身份）与它自己的 `build-n`。
4. 原始输出：`/mnt/d/inim-rel/probe/hits-all.txt`（74 条测试 × 14 个断点的逐条计数；`sha256` = `8af40fd012aa45376187fa4fe91c13269ef54e5fddcebba0ac99f9b1fc7febbf`、25353 B、456 行）、`/mnt/d/inim-rel/probe/runhits.log`（**0 字节**，见 §8.4）、脚本 `/mnt/d/inim-rel/probe/mods.gdb`（1625 B）与 `/mnt/d/inim-rel/probe/runhits.sh`（17977 B）。**逐字全文内联在 §1.1** —— 本节此前只给了位置；位置不等于内容。

### 1.1 原始输出（逐字全文）

`/mnt/d/inim-rel/probe/hits-all.txt` 的**全部 456 行**，逐字如下（尾部换行保留）。

- `sha256sum` = `8af40fd012aa45376187fa4fe91c13269ef54e5fddcebba0ac99f9b1fc7febbf`
- 字节数 = **25353**；行数 = **456**（`wc -lc`）
- 形状：每段以 `### <测试名>` 开头，随后 4 行 gdb 线程退出行，再一行 `HITS gui=… hl_loop=…` 十四个计数。
- 为什么把全文放进文档：路径会随那台主机消失，内容不会。这一节是 ① 第四项的解，也是 ③ 那一列的原始见证。

```text
### selfhost_toolchain_parses
[Thread 15808.0x4b28 (id 4) exited with code 0]
[Thread 15808.0xd7c (id 3) exited with code 0]
[Thread 15808.0x417c (id 2) exited with code 0]
[Inferior 1 (process 15808) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### version_runtime
[Thread 11568.0x30d8 (id 4) exited with code 0]
[Thread 11568.0x5ca8 (id 3) exited with code 0]
[Thread 11568.0xba4 (id 2) exited with code 0]
[Inferior 1 (process 11568) exited normally]
HITS gui=0 io=0 ident=0 social=0 ai=0 net=0 server=0 say=0 rtreg=0 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### result_runtime
[Thread 15468.0x6120 (id 3) exited with code 0]
[Thread 15468.0x5e98 (id 2) exited with code 0]
[Thread 15468.0x6558 (id 4) exited with code 0]
[Inferior 1 (process 15468) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### collection_comprehension_runtime
[Thread 14864.0x2cf8 (id 4) exited with code 0]
[Thread 14864.0x1db8 (id 2) exited with code 0]
[Thread 14864.0x1e18 (id 3) exited with code 0]
[Inferior 1 (process 14864) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### collection_core_runtime
[Thread 8732.0x4ff8 (id 3) exited with code 0]
[Thread 8732.0x1ce4 (id 4) exited with code 0]
[Thread 8732.0x63f4 (id 2) exited with code 0]
[Inferior 1 (process 8732) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### type_collection_runtime
[Thread 1772.0x6424 (id 4) exited with code 0]
[Thread 1772.0x6348 (id 3) exited with code 0]
[Thread 1772.0x2384 (id 2) exited with code 0]
[Inferior 1 (process 1772) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### be_removed_decl_runtime
[Thread 2160.0x3d08 (id 4) exited with code 1]
[Thread 2160.0x2a40 (id 2) exited with code 1]
[Thread 2160.0x107c (id 3) exited with code 1]
[Inferior 1 (process 2160) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### be_removed_assign_runtime
[Thread 25140.0x2540 (id 3) exited with code 1]
[Thread 25140.0xcf4 (id 2) exited with code 1]
[Thread 25140.0x10d0 (id 4) exited with code 1]
[Inferior 1 (process 25140) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### be_removed_bare_runtime
[Thread 8960.0x65b0 (id 4) exited with code 1]
[Thread 8960.0x402c (id 3) exited with code 1]
[Thread 8960.0x6dc (id 2) exited with code 1]
[Inferior 1 (process 8960) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### bind_init_overflow_runtime
[Thread 10016.0x1ae0 (id 3) exited with code 1]
[Thread 10016.0x4910 (id 2) exited with code 1]
[Thread 10016.0xc74 (id 4) exited with code 1]
[Inferior 1 (process 10016) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### posix_core_api_runtime
[Thread 15252.0x51c (id 3) exited with code 0]
[Thread 15252.0x68d0 (id 4) exited with code 0]
[Thread 15252.0x2ac4 (id 2) exited with code 0]
[Inferior 1 (process 15252) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### posix_runtime_parity
[Thread 15560.0x3f70 (id 4) exited with code 0]
[Thread 15560.0x6688 (id 2) exited with code 0]
[Thread 15560.0x3668 (id 3) exited with code 0]
[Inferior 1 (process 15560) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### gc_runtime
[Thread 6548.0x1a4c (id 5) exited with code 0]
[Thread 6548.0x418 (id 4) exited with code 0]
[Thread 6548.0x858 (id 2) exited with code 0]
[Thread 6548.0x59c8 (id 3) exited with code 0]
[Inferior 1 (process 6548) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### gc_bound_root
[Thread 15164.0x4ec0 (id 3) exited with code 1]
[Thread 15164.0x3608 (id 2) exited with code 1]
[Thread 15164.0x57f4 (id 4) exited with code 1]
[Inferior 1 (process 15164) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### pipeline_runtime
[Thread 18876.0x63c (id 4) exited with code 0]
[Thread 18876.0x3e74 (id 2) exited with code 0]
[Thread 18876.0x2630 (id 3) exited with code 0]
[Inferior 1 (process 18876) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### result_question_runtime
[Thread 10292.0x4408 (id 4) exited with code 0]
[Thread 10292.0x5e1c (id 2) exited with code 0]
[Thread 10292.0x3cbc (id 3) exited with code 0]
[Inferior 1 (process 10292) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### null_coalesce_runtime
[Thread 20460.0x617c (id 3) exited with code 0]
[Thread 20460.0x49dc (id 4) exited with code 0]
[Thread 20460.0xb44 (id 2) exited with code 0]
[Inferior 1 (process 20460) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### optional_member_runtime
[Thread 11084.0x9d0 (id 4) exited with code 0]
[Thread 11084.0x4a34 (id 3) exited with code 0]
[Thread 11084.0x5d0c (id 2) exited with code 0]
[Inferior 1 (process 11084) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### chained_comparison_runtime
[Thread 24656.0x199c (id 2) exited with code 0]
[Thread 24656.0x3c40 (id 4) exited with code 0]
[Thread 24656.0x2074 (id 3) exited with code 0]
[Inferior 1 (process 24656) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### postfix_condition_runtime
[Thread 27112.0x3620 (id 4) exited with code 0]
[Thread 27112.0x5830 (id 3) exited with code 0]
[Thread 27112.0x3aec (id 2) exited with code 0]
[Inferior 1 (process 27112) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### postfix_condition_multiline_runtime
[Thread 15640.0x2424 (id 3) exited with code 0]
[Thread 15640.0x5a38 (id 2) exited with code 0]
[Thread 15640.0x55cc (id 4) exited with code 0]
[Inferior 1 (process 15640) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### result_propagation_runtime
[Thread 21600.0x66e8 (id 4) exited with code 0]
[Thread 21600.0x48a8 (id 3) exited with code 0]
[Thread 21600.0x35c0 (id 2) exited with code 0]
[Inferior 1 (process 21600) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### result_finally_propagation_runtime
[Thread 3424.0x3990 (id 3) exited with code 0]
[Thread 3424.0x296c (id 4) exited with code 0]
[Thread 3424.0x1374 (id 2) exited with code 0]
[Inferior 1 (process 3424) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### try_finally_runtime
[Thread 18448.0x5598 (id 4) exited with code 0]
[Thread 18448.0x89c (id 2) exited with code 0]
[Thread 18448.0x930 (id 3) exited with code 0]
[Inferior 1 (process 18448) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### finally_return_override_runtime
[Thread 22512.0x407c (id 4) exited with code 0]
[Thread 22512.0x3e60 (id 3) exited with code 0]
[Thread 22512.0x66b0 (id 2) exited with code 0]
[Inferior 1 (process 22512) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_try_runtime
[Thread 24988.0x2b80 (id 4) exited with code 0]
[Thread 24988.0x3a1c (id 3) exited with code 0]
[Thread 24988.0xaa8 (id 2) exited with code 0]
[Inferior 1 (process 24988) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_try_runtime
[Thread 19744.0x69e4 (id 4) exited with code 1]
[Thread 19744.0x1c80 (id 3) exited with code 1]
[Thread 19744.0x3a34 (id 2) exited with code 1]
[Inferior 1 (process 19744) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_enum_runtime
[Thread 7620.0x1144 (id 4) exited with code 1]
[Thread 7620.0x20d4 (id 3) exited with code 1]
[Thread 7620.0x26b8 (id 2) exited with code 1]
[Inferior 1 (process 7620) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_membership_runtime
[Thread 25460.0x3d68 (id 2) exited with code 1]
[Thread 25460.0x19c8 (id 3) exited with code 1]
[Thread 25460.0x204c (id 4) exited with code 1]
[Inferior 1 (process 25460) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_try_members_runtime
[Thread 3456.0x6ba0 (id 3) exited with code 1]
[Thread 3456.0x6a88 (id 4) exited with code 1]
[Thread 3456.0x50f8 (id 2) exited with code 1]
[Inferior 1 (process 3456) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_try_alias_runtime
[Thread 5364.0x45ac (id 2) exited with code 0]
[Thread 5364.0x2b60 (id 4) exited with code 0]
[Thread 5364.0x978 (id 3) exited with code 0]
[Inferior 1 (process 5364) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_collection_patterns_runtime
[Thread 27180.0x6bc4 (id 4) exited with code 0]
[Thread 27180.0x4210 (id 3) exited with code 0]
[Thread 27180.0x3c68 (id 2) exited with code 0]
[Inferior 1 (process 27180) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_structural_runtime
[Thread 26828.0x4da8 (id 4) exited with code 0]
[Thread 26828.0x48ac (id 3) exited with code 0]
[Thread 26828.0x58d4 (id 2) exited with code 0]
[Inferior 1 (process 26828) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_alias_runtime
[Thread 9648.0x617c (id 4) exited with code 0]
[Thread 9648.0x49dc (id 3) exited with code 0]
[Thread 9648.0x84 (id 2) exited with code 0]
[Inferior 1 (process 9648) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_array_runtime
[Thread 23772.0x5d0c (id 4) exited with code 0]
[Thread 23772.0x4a34 (id 3) exited with code 0]
[Thread 23772.0x9d0 (id 2) exited with code 0]
[Inferior 1 (process 23772) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### case_nested_patterns_runtime
[Thread 16092.0x1908 (id 2) exited with code 0]
[Thread 16092.0xaf4 (id 3) exited with code 0]
[Thread 16092.0x3c40 (id 4) exited with code 0]
[Inferior 1 (process 16092) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lambda_runtime
[Thread 27068.0x3620 (id 2) exited with code 0]
[Thread 27068.0x5830 (id 3) exited with code 0]
[Thread 27068.0x3aec (id 4) exited with code 0]
[Inferior 1 (process 27068) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### composition_runtime
[Thread 9252.0xfcc (id 4) exited with code 0]
[Thread 9252.0x1cd0 (id 3) exited with code 0]
[Thread 9252.0x304c (id 2) exited with code 0]
[Inferior 1 (process 9252) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lambda_capture_runtime
[Thread 13760.0x26fc (id 4) exited with code 0]
[Thread 13760.0x5460 (id 2) exited with code 0]
[Thread 13760.0x168c (id 3) exited with code 0]
[Inferior 1 (process 13760) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lambda_nested_runtime
[Thread 7936.0x6aa0 (id 3) exited with code 0]
[Thread 7936.0x9d4 (id 4) exited with code 0]
[Thread 7936.0x3d88 (id 2) exited with code 0]
[Inferior 1 (process 7936) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### function_value_lifetime_runtime
[Thread 3568.0x509c (id 3) exited with code 0]
[Thread 3568.0x1990 (id 4) exited with code 0]
[Thread 3568.0x69cc (id 2) exited with code 0]
[Inferior 1 (process 3568) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### function_thread_lifetime_runtime
[Thread 26036.0x5e98 (id 5) exited with code 0]
[Thread 26036.0x2a44 (id 6) exited with code 0]
[Thread 26036.0x6120 (id 4) exited with code 0]
[Thread 26036.0x48a0 (id 3) exited with code 0]
[Thread 26036.0x521c (id 2) exited with code 0]
[Inferior 1 (process 26036) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### thread_result_runtime
[Thread 22012.0x6b98 (id 5) exited with code 0]
[Thread 22012.0x2ed4 (id 6) exited with code 0]
[Thread 22012.0xcec (id 7) exited with code 0]
[Thread 22012.0x3578 (id 9) exited with code 0]
[Thread 22012.0x4d7c (id 3) exited with code 0]
[Thread 22012.0x4e70 (id 4) exited with code 0]
[Thread 22012.0x3a10 (id 8) exited with code 0]
[Thread 22012.0x3584 (id 2) exited with code 0]
[Inferior 1 (process 22012) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### thread_await_runtime
[Thread 22404.0x1034 (id 7) exited with code 0]
[Thread 22404.0x6768 (id 5) exited with code 0]
[Thread 22404.0x4874 (id 2) exited with code 0]
[Thread 22404.0x69b4 (id 3) exited with code 0]
[Thread 22404.0x20b4 (id 4) exited with code 0]
[Thread 22404.0xad0 (id 6) exited with code 0]
[Inferior 1 (process 22404) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### float_precision_runtime
[Thread 25420.0x6a68 (id 4) exited with code 0]
[Thread 25420.0x3d08 (id 3) exited with code 0]
[Thread 25420.0x2a40 (id 2) exited with code 0]
[Inferior 1 (process 25420) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### call_arg_clobber_alone_runtime
[Thread 12420.0x1af8 (id 2) exited with code 0]
[Thread 12420.0x6954 (id 3) exited with code 0]
[Thread 12420.0xef4 (id 4) exited with code 0]
[Inferior 1 (process 12420) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### call_arg_clobber_shared_runtime
[Thread 16320.0x4de4 (id 4) exited with code 0]
[Thread 16320.0x17a4 (id 3) exited with code 0]
[Thread 16320.0x6af8 (id 2) exited with code 0]
[Inferior 1 (process 16320) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### list_set_off_by_one_runtime
[Thread 15448.0x1b20 (id 4) exited with code 0]
[Thread 15448.0x45e8 (id 3) exited with code 0]
[Thread 15448.0x670 (id 2) exited with code 0]
[Inferior 1 (process 15448) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### set_components_enumerable_runtime
[Thread 7824.0x6890 (id 2) exited with code 0]
[Thread 7824.0xf84 (id 4) exited with code 0]
[Thread 7824.0x2720 (id 3) exited with code 0]
[Inferior 1 (process 7824) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### set_float_lattice_runtime
[Thread 9720.0x2968 (id 3) exited with code 0]
[Thread 9720.0xc28 (id 4) exited with code 0]
[Thread 9720.0x1eb8 (id 2) exited with code 0]
[Inferior 1 (process 9720) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### unknown_builtin_runtime
[Thread 21056.0x6688 (id 4) exited with code 0]
[Thread 21056.0x3078 (id 3) exited with code 0]
[Thread 21056.0xb88 (id 2) exited with code 0]
[Inferior 1 (process 21056) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### contract_suite_runtime
[Thread 25148.0x1b78 (id 5) exited with code 0]
[Thread 25148.0xe20 (id 3) exited with code 0]
[Thread 25148.0x1ca0 (id 2) exited with code 0]
[Thread 25148.0x4cbc (id 4) exited with code 0]
[Inferior 1 (process 25148) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### set_enumeration_refusal_runtime
[Thread 25232.0x1940 (id 3) exited with code 0]
[Thread 25232.0x55a0 (id 4) exited with code 0]
[Thread 25232.0x3720 (id 2) exited with code 0]
[Inferior 1 (process 25232) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### sum_components_int64_runtime
[Thread 10212.0x5db8 (id 4) exited with code 0]
[Thread 10212.0x4c78 (id 3) exited with code 0]
[Thread 10212.0x1e94 (id 2) exited with code 0]
[Inferior 1 (process 10212) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### set_str_component_count_runtime
[Thread 25216.0x17c8 (id 2) exited with code 0]
[Thread 25216.0x2378 (id 3) exited with code 0]
[Thread 25216.0x1008 (id 4) exited with code 0]
[Inferior 1 (process 25216) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### truthiness_single_point_runtime
[Thread 14796.0x6404 (id 2) exited with code 0]
[Thread 14796.0x6050 (id 3) exited with code 0]
[Thread 14796.0x187c (id 4) exited with code 0]
[Inferior 1 (process 14796) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### union_member_tag_runtime
[Thread 7968.0x68e4 (id 4) exited with code 0]
[Thread 7968.0x388 (id 2) exited with code 0]
[Thread 7968.0x2b50 (id 3) exited with code 0]
[Inferior 1 (process 7968) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### concat_left_operand_runtime
[Thread 5440.0x30ac (id 4) exited with code 0]
[Thread 5440.0x2b0 (id 3) exited with code 0]
[Thread 5440.0x69b0 (id 2) exited with code 0]
[Inferior 1 (process 5440) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### atomic_int64_width_runtime
[Thread 10632.0x3eac (id 4) exited with code 0]
[Thread 10632.0x4c9c (id 2) exited with code 0]
[Thread 10632.0x6a8c (id 3) exited with code 0]
[Inferior 1 (process 10632) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### order_requires_orderable_runtime
[Thread 22580.0x5620 (id 3) exited with code 0]
[Thread 22580.0x3b58 (id 4) exited with code 0]
[Thread 22580.0x613c (id 2) exited with code 0]
[Inferior 1 (process 22580) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### predicate_result_type_runtime
[Thread 13168.0x3dc0 (id 4) exited with code 0]
[Thread 13168.0x33c4 (id 3) exited with code 0]
[Thread 13168.0x6858 (id 2) exited with code 0]
[Inferior 1 (process 13168) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### divergent_builtin_contract_runtime
[Thread 19708.0x1894 (id 3) exited with code 0]
[Thread 19708.0x288 (id 4) exited with code 0]
[Thread 19708.0x4de8 (id 2) exited with code 0]
[Inferior 1 (process 19708) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### round_nonnumber_contract_runtime
[Thread 12328.0x3454 (id 4) exited with code 1]
[Thread 12328.0x2cc8 (id 2) exited with code 1]
[Thread 12328.0x6af0 (id 3) exited with code 1]
[Inferior 1 (process 12328) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### params_relative_path_runtime
error: cannot read script 'D:\inim-rel\d27413a-src\build-n\vtest\params_sub\relative_script.im'
[Thread 22404.0x1f38 (id 3) exited with code 1]
[Thread 22404.0x6694 (id 4) exited with code 1]
[Thread 22404.0x1078 (id 2) exited with code 1]
[Inferior 1 (process 22404) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### params_missing_explicit_runtime
error: cannot read script 'D:\inim-rel\d27413a-src\build-n\vtest\params_sub\relative_script.im'
[Thread 4220.0x221c (id 4) exited with code 1]
[Thread 4220.0x63f4 (id 2) exited with code 1]
[Thread 4220.0x1ce4 (id 3) exited with code 1]
[Inferior 1 (process 4220) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### params_precompiled_runtime
error: cannot load bytecode 'D:\inim-rel\d27413a-src\build-n\vtest\params_precompiled_v06.inim' (old format? recompile with buildc)
[Thread 17412.0x65b0 (id 4) exited with code 1]
[Thread 17412.0x6520 (id 2) exited with code 1]
[Thread 17412.0x22dc (id 3) exited with code 1]
[Inferior 1 (process 17412) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### spi_caps_contract_runtime
[Thread 6600.0x2858 (id 3) exited with code 0]
[Thread 6600.0x3d68 (id 2) exited with code 0]
[Thread 6600.0x5074 (id 4) exited with code 0]
[Inferior 1 (process 6600) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### atomic_slot_type_contract_runtime
[Thread 8960.0x6984 (id 3) exited with code 0]
[Thread 8960.0x3cd8 (id 4) exited with code 0]
[Thread 8960.0x4fcc (id 2) exited with code 0]
[Inferior 1 (process 8960) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### random_bounded_contract_runtime
[Thread 10948.0x4994 (id 4) exited with code 0]
[Thread 10948.0x394 (id 3) exited with code 0]
[Thread 10948.0xd80 (id 2) exited with code 0]
[Inferior 1 (process 10948) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_missing_default_runtime
[Thread 8112.0x366c (id 3) exited with code 1]
[Thread 8112.0x4f14 (id 2) exited with code 1]
[Thread 8112.0x6080 (id 4) exited with code 1]
[Inferior 1 (process 8112) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### lint_case_exhaustive_runtime
[Thread 10112.0x1c60 (id 4) exited with code 1]
[Thread 10112.0x14f4 (id 3) exited with code 1]
[Thread 10112.0x1d8c (id 2) exited with code 1]
[Inferior 1 (process 10112) exited with code 01]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=1 cspawn=0 clist=0 hl_init=0 hl_loop=0
### substr_boundary_runtime
[Thread 26544.0x2ae8 (id 4) exited with code 0]
[Thread 26544.0x2dc8 (id 3) exited with code 0]
[Thread 26544.0x1674 (id 2) exited with code 0]
[Inferior 1 (process 26544) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### count_builtin_runtime
Thread 1 received signal SIGSEGV, Segmentation fault.
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
### match_long_pattern_runtime
[Thread 6896.0x48c4 (id 4) exited with code 0]
[Thread 6896.0x4198 (id 3) exited with code 0]
[Thread 6896.0x6afc (id 2) exited with code 0]
[Inferior 1 (process 6896) exited normally]
HITS gui=1 io=1 ident=1 social=1 ai=1 net=1 server=1 say=1 rtreg=1 modload=0 cspawn=0 clist=0 hl_init=0 hl_loop=0
```


## 2. 十二个文件的归属

| 文件 | Windows 侧生产点 | POSIX 对应物 | 归属 | ③ 注册测试可达性 | 依据 |
|---|---|---|---|---|---|
| `src/runtime/runtime.c` | `CMakeLists.txt:429` | `src/runtime/runtime_posix.c`（`:438`） | **两平台**（各一份实现） | `runtime_register_builtins` **73/74**；`re_seq` 仅 3 条 | 互斥分支；`grep -c re_seq src/runtime/runtime_posix.c` = 0 |
| `src/child_proc.c` | `:429` | **无**（无对等文件、无空桩） | **仅 Windows（构建表所致）** | `child_proc_spawn` / `child_proc_list` **0/74** | `grep -n child_proc CMakeLists.txt` 只有 `:429` 一行；文件内 `grep -c _WIN32` = **0**，头文件 `src/child_proc.h:7` `#ifdef _WIN32` / `:10` `typedef uint64_t DWORD;` 明确写了非 Windows 的替身；注释首行 `/* Cross-platform child process registry. */` |
| `src/headless_server.c` | `:429` | `src/headless_server_posix.c`（`:442`） | **两平台**（各一份实现） | `headless_init` / `headless_loop` **0/74** | 互斥分支；Windows 侧唯一入口 `src/main.c:895`（`--headless`）→ `:1118 headless_init` |
| `src/mod/mod.c` | `:430` | `src/mod/mod_posix.c`（`:439`） | 两平台 | `mod_load_all` **57/74** | 互斥分支 |
| `src/mod/gui_mod.c` | `:431` | 空桩 `STUB_REG(gui_mod_register)`（`src/platform/posix_stubs.c:6`，列入 `:441`） | **仅 Windows（空桩）** | `gui_mod_register` **73/74** | POSIX 无实现 |
| `src/mod/io_mod.c` | `:431` | 空桩（`posix_stubs.c:6`） | **仅 Windows（空桩）** | `io_mod_register` **73/74** | 同上 |
| `src/mod/identity_mod.c` | `:433` | 空桩（`posix_stubs.c:7`） | **仅 Windows（空桩）** | `identity_mod_register` **73/74** | 同上 |
| `src/mod/social_mod.c` | `:433` | 空桩（`posix_stubs.c:8`） | **仅 Windows（空桩）** | `social_mod_register` **73/74** | 同上 |
| `src/mod/ai_mod.c` | `:434` | 空桩（`posix_stubs.c:8`） | **仅 Windows（空桩）** | `ai_mod_register` **73/74** | 同上 |
| `src/mod/net_mod.c` | `:431` | `src/mod/net_mod_posix.c`（`:439`） | 两平台 | `net_mod_register` **73/74** | 互斥分支 |
| `src/mod/server_mod.c` | `:433` | `src/mod/server_mod_posix.c`（`:439`） | 两平台 | `server_mod_register` **73/74** | 互斥分支 |
| `src/mod/say_mod_windows.c` | `:436` | `src/mod/say_mod_posix.c`（`:439`） | 两平台 | `say_mod_register` **73/74** | 互斥分支 |

分支结构（`CMakeLists.txt @ d27413a`）：`:426 if(WIN32)` → `:427-435` 的 `list(APPEND …)`（12 个文件分别落在 `:429`／`:430`／`:431`／`:433`／`:434`）→ `:436` 单独一行列 `say_mod_windows.c` → `:437 else()` → `:438-443` 的 POSIX 列表（对等实现与 `posix_stubs.c` 都在这里）→ `:444 endif()`。

十二个文件**全部编进了 Windows 引擎**：导出树的 `build-n/CMakeFiles/inimerse_engine.dir/<path>.obj` 十二个都在（`runtime.c` 53717 B、`gui_mod.c` 204681 B、`io_mod.c` 33413 B、`ai_mod.c` 17654 B、`net_mod.c` 13818 B、`server_mod.c` 13391 B、`identity_mod.c` 11885 B、`say_mod_windows.c` 11051 B、`headless_server.c` 6616 B、`social_mod.c` 6592 B、`child_proc.c` 4460 B、`mod.c` 3028 B），且符号在 `inimerse.exe` 里（该 exe 共 5987 个已定义符号，含 `0000000140069210 T child_proc_spawn`、`00000001400697c0 T headless_accept`、`00000001400633a0 t re_seq`）。

## 3. ③ 那一列是怎么量的

**不是读码。** 步骤：

1. 注册表：`main @ d27413a` 的 Windows 注册 = **137** 条，其中 **9 条 `(Disabled)`**；**74 条**的 COMMAND 直接是 `build-n/inimerse.exe`（其余是探针可执行文件、python 脚本与 node 脚本）。名单取自生成的 `build-n/CTestTestfile.cmake`，与 `ctest -N` 的 `Total Tests: 137` 一致。
2. 对每条跑一次 gdb，断点打在 14 个入口：`gui_mod_register`、`io_mod_register`、`identity_mod_register`、`social_mod_register`、`ai_mod_register`、`net_mod_register`、`server_mod_register`、`say_mod_register`、`runtime_register_builtins`、`mod_load_all`、`child_proc_spawn`、`child_proc_list`、`headless_init`、`headless_loop`。每个断点用 `commands` / `silent` / 计数器自增 / `continue`，程序结束（正常退出或信号）后一次性 `printf` 十四个计数。
3. 结果：**73/74** 命中那九个启动期注册函数；**57/74** 命中 `mod_load_all`；**0/74** 命中 `child_proc_*` 与 `headless_*`。唯一未命中启动的是 `version_runtime`（`inimerse --version`，不起 VM）。未命中 `mod_load_all` 的 17 条 = `version_runtime` + **16 条 `--no-mods` 测试**。

**这一列把 §1.71 说的「结构性失明」从形容词变成了可复现的机制**：八个 `*_mod_register` 由 `src/main.c:1067` 与 `:1097` 的 `register_core_modules(&vm)` **无条件调用**，`--no-mods` 挡不住它们 —— `src/main.c:1000` 的 `--no-mods` 只把 `load_mods` 置 0，而 `load_mods` 只挡 `src/main.c:1068`／`:1084`／`:1098`／`:1099` 的 `register_world_modules` 与 `mod_load_all`。所以「引擎起得来」与「这八个模块被调用」是同一件事。

## 4. 两个工具陷阱

**陷阱一：`nm.exe` / `objdump.exe` 是 Windows 程序，必须给 `D:/…` 形式的路径。** 给 `/mnt/d/…` 会得到 `nm.exe: '/mnt/d/…': No such file`，而 `nm --defined-only` 因此**只输出 1 行** —— 看上去与「符号不存在」一模一样。我第一遍就中了这个，据此差点写下「`child_proc_*` / `headless_*` 不在 exe 里」；换成 `D:/inim-rel/d27413a-src/build-n/inimerse.exe` 后同一命令输出 5987 个已定义符号，两者都在。⇒ **「工具说没有」与「确实没有」之间，隔着一次「我给它的路径形式它读不读得懂」的检查。**

**陷阱二：该导出树的对象文件后缀是 `.obj` 不是 `.o`。** 用 `find build-n -name '*.o'` 去找，十二个文件**全部报缺失**，而它们十二个都在 `CMakeFiles/inimerse_engine.dir/<path>.obj`。⇒ 同一个入口的第二次：**一次筛选取不到东西，先怀疑筛选条件，再怀疑世界。**

## 5. 未测项（写「没测到」，不写「不存在」）

- `child_proc.c` 与 `headless_server.c` 在 **Windows 侧注册表里 0 条测试可达**。但 `headless_probe` 在 Windows **根本没注册**（137 条里没有这个名字，它在 Windows 的 11 条未注册名单里），`src/headless_server_probe.c:10` 的 `headless_init(18130)` 因此在 Windows 上跑不到。这是**没测到**，不是**不存在**。
- 只覆盖了 **74 条直接以 `inimerse.exe` 为 COMMAND 的测试**。python／node 类测试若 fork 引擎会走同一条启动路径，但 gdb 不跟子进程，**未覆盖**。
- `child_proc.c` **是否能编进 POSIX 分支 —— 未测**。文件里没有一行 `_WIN32`、头文件为非 Windows 备了 `DWORD` typedef、它调用的 `im_process_*` 来自 `src/platform/process.c`（两个分支都列），这些都只是**读码候选**，不构成「可以」。
- `child_proc_init` 没有外部调用者：它由 `src/child_proc.c:12` 的 `child_proc_spawn` 惰性调用（`if(!g_inited)child_proc_init();`）。所以「有没有测试碰 `child_proc.c`」等价于「有没有测试碰 `child_proc_spawn` / `child_proc_list`」，本表量的是后者。

## 6. 「仅 Windows」有两义，不要混写

- **仅 Windows（空桩）**：`gui_mod.c`、`io_mod.c`、`identity_mod.c`、`social_mod.c`、`ai_mod.c`。POSIX 侧由 `src/platform/posix_stubs.c:6-8` 的 `STUB_REG(...)` 顶成空函数（宏在 `:5`），功能确实只在 Windows。这些模块在 POSIX 上是**静默缺席**：`:4` 的 `unsupported()`（会打印 `inimerse: capability '%s' is not available on this POSIX build yet`）只被 `:9` 的 `build_project_impl` 调用，六个 `STUB_REG` 展开成 `void name(VM *vm) { (void)vm; }`，什么都不打印。
- **仅 Windows（构建表所致）**：`child_proc.c`。文件本身是跨平台的（注释这么写、代码这么写、头文件还专门给了非 Windows 的 `DWORD`），**是 `CMakeLists.txt` 只在 `if(WIN32)` 里列它**。

**这两义混在一列里，表就把一个「有人决定不编」说成了一个「文件只能这样」。** 本表因此把它们分开写。

## 7. 这份文档的边界

- 它是**记录**，不是断言：表中每一格都指到 `d27413a` 的某个文件某一行，或指到一次可重跑的 gdb 计数。换 ref 请重量，不要搬运。
- 它不修改 `docs/AUDIT.md` §1.71；§1.71 末尾的一行指针由协调者加。
- 它不裁定 `child_proc.c` 该不该编进 POSIX 分支 —— 那是人类/协调者的决定，本表只把它现在的状态量清楚。


## 8. 2026-10-22 本机复核（重算原始件；`honest-island`，base `290b5e4`）

**这不是重取，是重算。** 本节从**仍存在的原始件**逐条重算，目的是复核 ③ 那一列的每一个数，并说明第二份原始件是死的。**同 ref 的 gdb 重跑另见 §9。**

复核者：`honest-island`（`session-7546de99-9b6e-4d4b-9a4b-4009464e76cb`），分支 `stream/win-source-attribution-evidence`，base `290b5e49d69e0b46fe52d7335a76e855e161ce2b`（`docs/streams/win-source-attribution.md` 在 `caac5fa`→`290b5e4` 之间未变）。读数全部取在**本机（Linux/WSL）**，`/mnt/d` 挂载可读。

### 8.1 注册表那一半（`CTestTestfile.cmake`）

文件 `/mnt/d/inim-rel/d27413a-src/build-n/CTestTestfile.cmake`（67554 B，2026-10-06 13:01）——即 §3 步骤 1 说「名单取自」的那份：

- `add_test(` 出现 **137** 次，唯一名 **137** ⇒ 与「注册 = 137 条」一致。
- `DISABLED` 出现 **9** 次 ⇒ 与「9 条 `(Disabled)`」一致。
- `add_test` 的**第二个 token（COMMAND）就是 `build-n/inimerse.exe`** 的 = **74** ⇒ 与「74 条的 COMMAND 直接是 inimerse.exe」一致。

### 8.2 gdb 输出那一半（`hits-all.txt`）

从 §1.1 的全文重解析：测试段 **74**、`HITS` 行 **74**、断点恰好 **14**；`rtreg` 命中 **73**（唯一未命中 = `version_runtime`）、`modload` 命中 **57**、`cspawn` / `clist` / `hl_init` / `hl_loop` 命中 **0**。未命中 `modload` 的恰 **17** 条 = `version_runtime` + 16 条 `--no-mods` 测试，与 §3 步骤 3 的分解逐字相符。

### 8.3 两件原始件的交叉（本节最硬的一条）

把 8.1 里「COMMAND 直接是 `inimerse.exe`」的 74 个名字，与 8.2 里 74 个段名取集合：**两边差集皆空，集合相等**。⇒ 注册表与 gdb 输出描述的是**同一组 74 条测试**，不是两组各自 74 条。§3 步骤 2 说的「对每条跑一次 gdb」，其「每条」由此被钉死在同一个名单上。

### 8.4 边界与两条方法论

**「缺证据」和「证据不存在」是两回事。** 本件更正的是前者：`hits-all.txt` 一直在盘上，本件的每一个数都能从它重算出来，缺的只是**把它装进本文**（§1.1）——**位置缺失**不等于**证据不存在**。所以判据 ① 的原文（「四项回报齐」）判「不齐」成立（第四项 `:14` 此前只列了文件名与路径、fence 数 = 0），但把它读成「第四项**全文不存在**」，就把「缺内容」升级成了「无证据」——
那是更重的判词，本件实测不支持。

- **重取不在本节**：上面的每一个数都是**重算**，不是**重测**；「原始件可复核」与「测量可重取」是两句话。重取见 §9。
- `/mnt/d/inim-rel/probe/runhits.log` **0 字节**：它被 §1 第四项引作「驱动日志」，但 0 字节的文件对任何读数都不是见证。本次复核没有用到它。
- **一条我自己踩的字面量陷阱**：`CTestTestfile.cmake` 里写的是 `DISABLED`，`(Disabled)` 是 `ctest -N` 的**渲染**。拿 `(Disabled)` 去 grep 生成文件会得 **0** —— 那不是「没有禁用测试」，是「查错了字面量」。与 §4 那两个陷阱同族。

## 9. 2026-10-22 同 ref 重取（重跑 gdb）

**这是重测，不是重算。** §8 把 ③ 那一列从原始件重算了一遍；本节在**同一棵导出树、同一个 ref** 上把 gdb 测量**重跑**了一遍，用来把 ② 从「由原始件蕴含」抬到「同 ref 独立重取」。

### 9.1 五个坐标

- **机器**：`LAPTOP-ALLQECIL`（Windows 主机；`gdb.exe` 经 WSL interop 调用 —— 同一台机器的同一个 gdb）。
- **目录**：`/mnt/d/inim-rel/d27413a-src`（既有导出树，**未重新生成**）；`cwd` = 其 `build-n`（有测试用 `tests/_mini.im` 这类相对路径）。
- **ref**：`d27413a`。身份钉法 = 该树 `CMakeLists.txt` 的 `grep -c 'add_test('` = **148**（与 §3 步骤 3 同一枚钉子）。
- **时刻**：`2026-10-22T18:23:32+08:00` 起，`18:24:21+08:00` 止（墙钟 **49.3 s**；预算 1200 s）。
- **命令**（逐字，复现 `runhits.sh` 的调用形状）：`gdb.exe -batch -x D:/inim-rel/probe/mods.gdb --args <exe> <args...>  2>&1 | grep -E "^HITS|SIGSEGV|exited|error:"`。`<exe>` 与 `<args...>` 逐条取自 `build-n/CTestTestfile.cmake` 中 COMMAND 为 `build-n/inimerse.exe` 的那 74 条；`gdb.exe` = `/mnt/c/msys64/ucrt64/bin/gdb.exe`（GNU gdb 18.1）。

### 9.2 结果：74/74，逐条零差异

| 项 | 记录（`d27413a`，2026-10-06） | 重取（2026-10-22） |
|---|---|---|
| 有 `HITS` 行的测试 | 74 / 74 | **74 / 74** |
| `gui` `io` `ident` `social` `ai` `net` `server` `say` `rtreg` | 各 73 | **各 73** |
| `modload` | 57 | **57** |
| `cspawn` `clist` `hl_init` `hl_loop` | 各 0 | **各 0** |

逐条对表（不是逐项合计）：74 个测试名集合相等，**14 个计数器 × 74 条 = 1036 个格子，差异 0 个**。⇒ ③ 那一列的每一个数在同一个 ref 上可重取，`--no-mods` 那 16 条的口径也一并复现。

### 9.3 新读数与旧记录并列

- 重取原始件：`.scratch/island/retake/retake.txt`（27286 B、536 行、`sha256` = `b294337f52cdba4863069998f369954498c3a8a67128d5e133ad45d63a7396ca`），驱动 `.scratch/island/retake/driver.py`。
- **`hits-all.txt` 未被写入一个字节**：重取只读它、只与它对表。旧的是记录、新的是今天的重取，本节不覆盖 §1.1。

### 9.4 边界

- 同一台机器、同一棵树、同一 ref：**不是**「另一台机器上的独立复现」——它与记录共享同一个硬件与工具链前提。
- 预算 20 分钟，实际 **49.3 s** 跑完 74/74 ⇒ 本次重取是**完整的**，不是部分结果。先试通 3 条（3.5 s、`rc` 全 0、`HITS` 与记录一致）才跑全量。
- 驱动是按 `runhits.sh` 的调用形状**复写**的（原脚本用 MSYS 路径，不能在 WSL 下逐字跑）。