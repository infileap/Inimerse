# 集合变换性能审计

`tools/collection_perf_audit.py` 固化 V0.4 的集合变换基准，覆盖：

- 集合推导筛选：`{ x in source | x > 0 }`；
- 集合到列表变换：`list(selected)`；
- `--jit off/template/optimized` 三种模式的相对比较。

示例：

```bash
python3 tools/collection_perf_audit.py -e ./build/inimerse --size 10000 --iterations 10 --json
```

脚本记录进程耗时、标准输出 SHA-256，并在 Linux/WSL 有 `/usr/bin/time` 时记录每次子进程峰值 RSS（KiB）；结果正确性由 `collection_comprehension_runtime` 等语言回归测试负责。
最终 V0.4 审计应在同一机器、同一构建配置下重复至少三次，记录均值、标准差、峰值内存和结果哈希，并以 `--jit off` 为基线设置回归阈值。当前脚本输出是测量工具，不代表最终发布基线。

## 预审计样本（2026-08-30）

WSL/Ubuntu 构建、输入规模 1000、每模式 3 次；仅用于确认工具和场景可运行：

| 场景 | off 均值 | template 均值 | optimized 均值 |
|---|---:|---:|---:|
| comprehension_filter | 179.41 ms | 187.22 ms | 178.89 ms |
| set_to_list | 171.67 ms | 172.84 ms | 174.20 ms |

当前非 `off` 模式仍是解释器安全回退，因此不能据此宣称 JIT 加速。
