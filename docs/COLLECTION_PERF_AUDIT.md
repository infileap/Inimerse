# 集合变换性能审计

`tools/collection_perf_audit.py` 固化 V0.4 的集合变换基准，覆盖：

- 集合推导筛选：`{ x in source | x > 0 }`；
- 集合到列表变换：`list(selected)`；
- `--jit off/template/optimized` 三种模式的相对比较。

示例：

```bash
python3 tools/collection_perf_audit.py -e ./build/inimerse --size 10000 --iterations 10 --json
```

脚本在运行时逐项填充输入数组，避免大规模数组字面量触发编译器寄存器上限；记录进程耗时、标准输出 SHA-256，并在 Linux/WSL 有 `/usr/bin/time` 时记录每次子进程峰值 RSS（KiB）。结果正确性由 `collection_comprehension_runtime` 等语言回归测试负责。
最终 V0.4 审计应在同一机器、同一构建配置下重复至少三次，记录均值、标准差、峰值内存和结果哈希，并以 `--jit off` 为基线设置回归阈值。当前脚本输出是测量工具，不代表最终发布基线。

## 预审计样本（2026-08-30）

WSL/Ubuntu 构建、输入规模 1000、每模式 3 次；仅用于确认工具和场景可运行：

| 场景 | off 均值 | template 均值 | optimized 均值 |
|---|---:|---:|---:|
| comprehension_filter | 179.41 ms | 187.22 ms | 178.89 ms |
| set_to_list | 171.67 ms | 172.84 ms | 174.20 ms |

当前非 `off` 模式仍是解释器安全回退，因此不能据此宣称 JIT 加速。

## 当前工具回归样本（2026-09-05）

WSL/Ubuntu 构建、输入规模 1000、每模式 2 次；运行时数组填充路径验证成功。三种模式、两个场景的输出哈希均为
`bd17d3db1e8afeba31f9c6d5dd7d4839200332a39c75c2215aba3c3d3997e0df`，峰值 RSS 约 68.5 MiB。该样本用于验证审计工具本身，最终发布门禁仍需在固定构建目录上按要求重复采集。

## 工具回归样本（2026-08-30）

修复 `--jit=<mode>` 参数传递并加入结果哈希后，WSL/Ubuntu、规模 1000、每模式 3 次再次运行成功。所有场景输出哈希为
`bd17d3db1e8afeba31f9c6d5dd7d4839200332a39c75c2215aba3c3d3997e0df`；峰值 RSS 约 53 MiB。该数据仍是工具回归样本，最终发布门禁应在冻结版本上重新采集并设阈值。
