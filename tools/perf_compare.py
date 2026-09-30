#!/usr/bin/env python3
"""Performance comparison across the three v0.5 execution channels
(roadmap §2.3 acceptance: performance data, not theoretical claims).

Measures a numeric workload under:
  - interpreter      (`inimerse run`)
  - wasm MVP         (`node tools/wasm_run.js`, includes node startup)
  - aot package      (engine copy + embedded bytecode, same interpreter)

Each channel is calibrated against its own empty-script baseline so the
reported delta is workload compute time.  Honest expectation: the AOT
*package* channel reuses the C interpreter (parity, not speedup); the wasm
MVP backend is a straightforward translation without optimization — the
>=2x target belongs to the optimizing AOT backend (v0.5 -> v0.6 follow-up).

Results are appended to docs/SELFHOST_BENCHMARK.md with --write-docs.
"""
import argparse
import datetime
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
WORKLOAD = ('func sum(n) {\n'
            '    s = 0\n'
            '    i = 0\n'
            '    while i < n {\n'
            '        s = s + i\n'
            '        i = i + 1\n'
            '    }\n'
            '    return s\n'
            '}\n'
            'say sum(N_ITERS)\n').replace('N_ITERS', '{n}')
EMPTY = 'say 1\n'


def find_engine():
    import os
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    cands += [REPO / "build" / "inimerse", REPO / "build-local" / "inimerse"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def timeit(cmd, runs, cwd):
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        rc = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=300)
        dt = time.perf_counter() - t0
        if rc.returncode != 0:
            raise SystemExit(f"{cmd} failed: {rc.stderr.decode(errors='replace')}")
        samples.append(dt)
    return samples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--n", type=int, default=2000000, help="loop iterations")
    ap.add_argument("--write-docs", action="store_true")
    args = ap.parse_args()
    import shutil
    engine = find_engine()
    node = shutil.which("node")
    assert node, "node required"

    with tempfile.TemporaryDirectory(prefix="inimerse-perf-") as td:
        root = Path(td)
        (root / "work.im").write_text(WORKLOAD.replace('{n}', str(args.n)), encoding="utf-8")
        (root / "empty.im").write_text(EMPTY, encoding="utf-8")

        # interpreter
        base = statistics.median(timeit([str(engine), "run", "empty.im"], args.runs, root))
        full = statistics.median(timeit([str(engine), "run", "work.im"], args.runs, root))
        interp = full - base

        # wasm MVP
        rc = subprocess.run([str(engine), "compile", "--abi-target", "wasm", "work.im", "work.wasm"],
                            cwd=root, capture_output=True)
        assert rc.returncode == 0, rc.stderr.decode()
        wbase = statistics.median(timeit([node, str(HERE / "wasm_run.js"), "empty.wasm"], args.runs, root)
                                  ) if (root / "empty.wasm").exists() else 0
        if wbase == 0:
            subprocess.run([str(engine), "compile", "--abi-target", "wasm", "empty.im", "empty.wasm"],
                           cwd=root, capture_output=True)
            wbase = statistics.median(timeit([node, str(HERE / "wasm_run.js"), "empty.wasm"], args.runs, root))
        wfull = statistics.median(timeit([node, str(HERE / "wasm_run.js"), "work.wasm"], args.runs, root))
        wasm = wfull - wbase

        # aot package
        rc = subprocess.run([str(engine), "compile", "--aot", "work.im", "work_aot.exe"], cwd=root, capture_output=True)
        assert rc.returncode == 0, rc.stderr.decode()
        subprocess.run([str(engine), "compile", "--aot", "empty.im", "empty_aot.exe"], cwd=root, capture_output=True)
        abase = statistics.median(timeit([str(root / "empty_aot.exe")], args.runs, root))
        afull = statistics.median(timeit([str(root / "work_aot.exe")], args.runs, root))
        aot = afull - abase

    def ms(v): return f"{v * 1000:.0f} ms"
    lines = [
        "## 通道性能对比（roadmap §2.3 验收：以测量数据为依据）",
        "",
        f"- 引擎：`{subprocess.run([str(engine), '--version'], capture_output=True, text=True).stdout.strip()}`，"
        f"主机 `{platform.system()} {platform.machine()}`，工作负载 `sum(1..{args.n})`，每通道 {args.runs} 次取中位数",
        f"- 校准：各通道用自己的空脚本基线扣除启动开销（Node 启动约数百 ms）",
        "",
        "| 通道 | 计算耗时（中位） | 相对解释器 |",
        "|---|---|---|",
        f"| 解释器 | {ms(interp)} | 1.00x |",
        f"| Wasm MVP（Node 宿主） | {ms(wasm)} | {interp / wasm if wasm > 0 else float('nan'):.2f}x |",
        f"| AOT 打包通道 | {ms(aot)} | {interp / aot if aot > 0 else float('nan'):.2f}x |",
        "",
        "解读（诚实记录）：",
        "- AOT 打包通道复用同一个 C 解释器，性能与解释器等同（打包是分发手段，不是优化）；",
        "- Wasm MVP 是无优化的直接翻译，且宿主为 JS（Node 启动开销已扣除）——不满足 ≥2x 目标；",
        "- ≥2x 目标属于优化型 AOT 后端（原生代码生成），为 v0.5→v0.6 后续迭代。",
        "",
    ]
    report = "\n".join(lines)
    print(report)
    if args.write_docs:
        doc = REPO / "docs" / "SELFHOST_BENCHMARK.md"
        with open(doc, "a", encoding="utf-8") as f:
            f.write("\n" + report)
        print(f"appended: {doc}")


if __name__ == "__main__":
    main()
