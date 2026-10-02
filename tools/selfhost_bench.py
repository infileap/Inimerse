"""Self-host benchmark suite (v0.5 roadmap §1.3).

Measures representative workloads — collection transforms, `case try`
dispatch, VFS round-trips and the compiler itself — over N runs and reports
median / P95 wall time.  Used as the release gate for bootstrap claims:
see docs/archive/SELFHOST_BENCHMARK.md (regenerate with `--write-docs`).

Usage:
    python3 tools/selfhost_bench.py [--runs N] [--write-docs] [--json PATH]
"""
import argparse
import datetime
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BENCH = Path(__file__).resolve().parent / "bench"

SUITES = [
    ("collections", "bench_collections.im", "集合变换：list 构造/遍历、dict 访问"),
    ("case-try", "bench_case_try.im", "`case try` 模式分发（1500 轮）"),
    ("vfs", "bench_vfs.im", "VFS 文件读写往返（400 次）"),
    ("selfhost", "bench_compile.im", "编译器自身：多函数/分支编译 + 执行"),
]


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    candidates = [Path(env)] if env else []
    candidates += [REPO / "build" / "inimerse", REPO / "build-local" / "inimerse"]
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def time_runs(cmd, runs, cwd):
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        rc = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=120)
        dt = time.perf_counter() - t0
        if rc.returncode != 0:
            raise SystemExit(f"{cmd} failed rc={rc.returncode}: {rc.stderr.decode(errors='replace')}")
        samples.append(dt * 1000.0)
    return samples


def p95(samples):
    return statistics.quantiles(samples, n=20)[-1] if len(samples) > 1 else samples[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5, help="samples per suite (default 5)")
    ap.add_argument("--write-docs", action="store_true", help="update docs/archive/SELFHOST_BENCHMARK.md")
    ap.add_argument("--json", help="also dump raw samples as JSON")
    args = ap.parse_args()

    engine = find_engine()
    version = subprocess.run([str(engine), "--version"], capture_output=True, text=True).stdout.strip()

    workdir = Path(os.environ.get("INIMERSE_BENCH_TMP", Path(BENCH)))
    results = []
    for name, script, desc in SUITES:
        src = BENCH / script
        out = workdir / (script + ".inim")
        compile_samples = time_runs([str(engine), "buildc", str(src), str(out)], args.runs, cwd=workdir)
        run_samples = time_runs([str(engine), "run", str(out)], args.runs, cwd=workdir)
        bc_hash = hashlib.sha256(out.read_bytes()).hexdigest()
        results.append({
            "suite": name, "script": script, "desc": desc,
            "compile_median_ms": statistics.median(compile_samples),
            "compile_p95_ms": p95(compile_samples),
            "run_median_ms": statistics.median(run_samples),
            "run_p95_ms": p95(run_samples),
            "bytecode_sha256": bc_hash,
        })
        out.unlink(missing_ok=True)
        tmp = workdir / "bench_vfs.tmp"
        tmp.unlink(missing_ok=True)

    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = [
        "# Self-Host Benchmark (v0.5)",
        "",
        f"- 引擎：`{version}`",
        f"- 主机：`{platform.system()} {platform.machine()}`，Python {platform.python_version()}",
        f"- 采样：每套例 {args.runs} 次，取中位数与 P95",
        f"- 生成时间：{now}",
        "",
        "| 套例 | 编译中位 (ms) | 编译 P95 (ms) | 运行中位 (ms) | 运行 P95 (ms) | 规范化字节码哈希 |",
        "|---|---|---|---|---|---|",
    ]
    for r in results:
        lines.append(
            f"| {r['suite']} | {r['compile_median_ms']:.1f} | {r['compile_p95_ms']:.1f} "
            f"| {r['run_median_ms']:.1f} | {r['run_p95_ms']:.1f} | `{r['bytecode_sha256'][:16]}…` |")
    lines += [
        "",
        "## 方法",
        "",
        "- 套例脚本位于 `tools/bench/`，运行器为 `tools/selfhost_bench.py`。",
        "- 编译阶段计时 `inimerse buildc <script> <out>`；运行阶段计时 `inimerse run <out>`。",
        "- 规范化字节码哈希为产物 `.inim`（含依赖尾块）的 SHA-256；相同源码与选项跨宿主必须一致（可复现构建）。",
        "- 回归判定：与上一份报告相比，任一套例运行中位数劣化超过 20% 时不得宣称发布；需附超阈值原因。",
        "",
        "复现命令：`python3 tools/selfhost_bench.py --runs 5 --write-docs`",
        "",
    ]
    report = "\n".join(lines)
    print(report)
    if args.write_docs:
        (REPO / "docs" / "archive" / "SELFHOST_BENCHMARK.md").write_text(report + "\n", encoding="utf-8")
        print(f"written: docs/archive/SELFHOST_BENCHMARK.md")
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
