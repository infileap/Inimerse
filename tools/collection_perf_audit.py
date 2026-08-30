#!/usr/bin/env python3
"""Reproducible V0.4 collection transformation audit.

The harness measures the same scenarios under each VM/JIT mode. It intentionally
discards program output and records only process timing; semantic correctness is
covered by the language regression tests.
"""
import argparse, json, statistics, subprocess, tempfile, time
from pathlib import Path

SCENARIOS = {
    "comprehension_filter": "source = [{items}]\nselected = {{ x in source | x > 0 }}\nsay len(selected)\n",
    "set_to_list": "source = [{items}]\nselected = {{ x in source | x > 0 }}\nout = list(selected)\nsay len(out)\n",
}

def measure(engine, mode, script, iterations):
    samples = []
    for _ in range(iterations):
        start = time.perf_counter()
        proc = subprocess.run([engine, "--jit", mode, str(script)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        samples.append((time.perf_counter() - start) * 1000.0)
        if proc.returncode:
            raise RuntimeError(proc.stderr.decode(errors="replace"))
    return {"mean_ms": statistics.mean(samples), "min_ms": min(samples),
            "max_ms": max(samples), "stdev_ms": statistics.stdev(samples) if len(samples) > 1 else 0.0}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-e", "--engine", default="inimerse")
    ap.add_argument("-n", "--iterations", type=int, default=7)
    ap.add_argument("--size", type=int, default=1000)
    ap.add_argument("--modes", nargs="+", default=["off", "template", "optimized"])
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    if args.size < 1 or args.iterations < 1:
        ap.error("--size and --iterations must be positive")
    items = ", ".join(str(i - args.size // 2) for i in range(args.size))
    with tempfile.TemporaryDirectory(prefix="inimerse-collection-audit-") as td:
        root = Path(td)
        results = []
        for name, template in SCENARIOS.items():
            script = root / (name + ".im")
            script.write_text(template.format(items=items), encoding="utf-8")
            for mode in args.modes:
                row = {"scenario": name, "jit": mode, "size": args.size,
                       "iterations": args.iterations}
                row.update(measure(args.engine, mode, script, args.iterations))
                results.append(row)
    payload = {"engine": args.engine, "results": results}
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        for row in results:
            print(f"{row['scenario']:24} {row['jit']:9} {row['mean_ms']:9.3f} ms "
                  f"(min {row['min_ms']:.3f}, max {row['max_ms']:.3f})")

if __name__ == "__main__":
    main()
