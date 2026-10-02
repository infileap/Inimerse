#!/usr/bin/env python3
"""Is the AOT *package* channel actually faster than the interpreter?

`tools/perf_compare.py` reports ONE ratio per invocation (interpreter compute
time / AOT-package compute time).  The v0.5 release claim ("outperform the
interpreter by at least 2x") and the archived "1.09x" figure both rest on a
single such ratio.  A single ratio cannot separate a real speedup from
run-to-run noise, so this tool answers the question the claim actually needs:

  1. repeat the whole comparison `--trials` times and print the ratio
     DISTRIBUTION, not one number;
  2. run the SAME channel against itself as a control.  That control measures
     the noise floor of the rig: any "speedup" that sits inside the control's
     spread is not a speedup, it is the measurement wobbling;
  3. print raw absolute times, so a reader can judge whether each sample is
     long enough to resolve the difference being claimed at all.

Channels and calibration are identical to `tools/perf_compare.py` (same
workload, same empty-script baseline per channel) so the numbers remain
comparable with the archived report.  Nothing here invokes `--aot` expecting
speed: the AOT channel is the packaging channel and reuses the same C
interpreter.

Usage:
    python3 tools/aot_parity.py [--trials 9] [--runs 5] [--n 2000000]
    python3 tools/aot_parity.py --json /tmp/parity.json
"""
import argparse
import json
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

# Same workload as tools/perf_compare.py so results are comparable.
WORKLOAD = ('func sum(n) {\n'
            '    s = 0\n'
            '    i = 0\n'
            '    while i < n {\n'
            '        s = s + i\n'
            '        i = i + 1\n'
            '    }\n'
            '    return s\n'
            '}\n'
            'say sum(N_ITERS)\n')
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


def time_batch(cmd, runs, cwd):
    """Return (median_seconds, all_samples_seconds) for `runs` process runs."""
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        rc = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=300)
        dt = time.perf_counter() - t0
        if rc.returncode != 0:
            raise SystemExit(f"{cmd} failed rc={rc.returncode}: "
                             f"{rc.stderr.decode(errors='replace')}")
        samples.append(dt)
    return statistics.median(samples), samples


def trial(engine, work_im, empty_im, work_aot, empty_aot, runs, cwd):
    """One full comparison. Returns a dict of calibrated compute times in ms.

    Each channel gets its own empty-script baseline subtracted, which is what
    perf_compare.py does; `interp_ctrl` is a second, independent measurement of
    the interpreter channel used purely to size the noise floor.
    """
    ib, _ = time_batch([str(engine), "run", empty_im], runs, cwd)
    iw, _ = time_batch([str(engine), "run", work_im], runs, cwd)
    cb, _ = time_batch([str(engine), "run", empty_im], runs, cwd)
    cw, _ = time_batch([str(engine), "run", work_im], runs, cwd)
    ab, _ = time_batch([str(Path(cwd) / empty_aot)], runs, cwd)
    aw, _ = time_batch([str(Path(cwd) / work_aot)], runs, cwd)

    def ms(sec):
        return sec * 1000.0

    interp = ms(iw - ib)
    ctrl = ms(cw - cb)
    aot = ms(aw - ab)
    return {
        "interp_ms": interp,
        "interp_ctrl_ms": ctrl,
        "aot_ms": aot,
        "aot_ratio": (interp / aot) if aot > 0 else float("nan"),
        "control_ratio": (interp / ctrl) if ctrl > 0 else float("nan"),
        "raw": {
            "interp_empty_ms": ms(ib), "interp_work_ms": ms(iw),
            "ctrl_empty_ms": ms(cb), "ctrl_work_ms": ms(cw),
            "aot_empty_ms": ms(ab), "aot_work_ms": ms(aw),
        },
    }


def describe(name, values):
    vs = sorted(values)
    return {
        "name": name,
        "n": len(vs),
        "min": vs[0],
        "median": statistics.median(vs),
        "max": vs[-1],
        "stdev": statistics.stdev(vs) if len(vs) > 1 else 0.0,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=9,
                    help="independent repetitions of the whole comparison (default 9)")
    ap.add_argument("--runs", type=int, default=5,
                    help="process runs per timing batch (default 5, as perf_compare.py)")
    ap.add_argument("--n", type=int, default=2000000,
                    help="loop iterations (default 2000000, the archived workload)")
    ap.add_argument("--claim", type=float, default=2.0,
                    help="speedup asserted by the release notes (default 2.0)")
    ap.add_argument("--json", help="also dump every trial as JSON")
    args = ap.parse_args()

    engine = find_engine()
    version = subprocess.run([str(engine), "--version"], capture_output=True,
                             text=True).stdout.strip()

    with tempfile.TemporaryDirectory(prefix="inim-parity-") as td:
        root = Path(td)
        (root / "work.im").write_text(WORKLOAD.replace('N_ITERS', str(args.n)),
                                      encoding="utf-8")
        (root / "empty.im").write_text(EMPTY, encoding="utf-8")

        for src, out in (("work.im", "work_aot.exe"), ("empty.im", "empty_aot.exe")):
            rc = subprocess.run([str(engine), "compile", "--aot", src, out],
                                cwd=root, capture_output=True)
            if rc.returncode != 0:
                raise SystemExit(f"--aot {src} failed: {rc.stderr.decode(errors='replace')}")

        rows = []
        for t in range(args.trials):
            r = trial(engine, "work.im", "empty.im", "work_aot.exe", "empty_aot.exe",
                      args.runs, root)
            r["trial"] = t + 1
            rows.append(r)

        # bonus: the packaged exe is byte-identical to the engine plus a tail,
        # so its compute delta should be indistinguishable from the interpreter.
        sizes = {n: (root / n).stat().st_size for n in ("work_aot.exe", "empty_aot.exe")}

    aot_ratios = [r["aot_ratio"] for r in rows]
    ctrl_ratios = [r["control_ratio"] for r in rows]
    a = describe("AOT package / interpreter", aot_ratios)
    c = describe("interpreter control (same channel twice)", ctrl_ratios)
    assert a["name"] != c["name"]

    lines = [
        "## AOT package channel: is the speedup real?",
        "",
        f"- engine: `{version}`", 
        f"- workload: `sum(1..{args.n})`; empty-script baseline per channel, "
        f"{args.runs} process runs per batch, {args.trials} independent trials",
        f"- packaged exe sizes: work_aot.exe {sizes['work_aot.exe']} B, "
        f"empty_aot.exe {sizes['empty_aot.exe']} B (the engine binary + a bytecode tail)",
        "",
        "| trial | interp (ms) | AOT (ms) | AOT ratio | control ratio |",
        "|---|---|---|---|---|",
    ]
    for r in rows:
        lines.append(f"| {r['trial']} | {r['interp_ms']:.1f} | {r['aot_ms']:.1f} "
                     f"| {r['aot_ratio']:.2f}x | {r['control_ratio']:.2f}x |")

    lines += [
        "",
        "| measurement | min | median | max | stdev |",
        "|---|---|---|---|---|",
        f"| AOT package / interpreter | {a['min']:.2f}x | {a['median']:.2f}x "
        f"| {a['max']:.2f}x | {a['stdev']:.2f} |",
        f"| control (interpreter vs itself) | {c['min']:.2f}x | {c['median']:.2f}x "
        f"| {c['max']:.2f}x | {c['stdev']:.2f} |",
        "",
    ]

    # verdict
    inside = sum(1 for v in aot_ratios if c["min"] <= v <= c["max"])
    lines += [
        "### Verdict",
        "",
        f"- The assertion under test is **>= {args.claim:.2f}x**. "
        f"Observed AOT ratios span **{a['min']:.2f}x .. {a['max']:.2f}x** "
        f"(median {a['median']:.2f}x).",
        f"- The control — the interpreter measured against itself — spans "
        f"**{c['min']:.2f}x .. {c['max']:.2f}x** (median {c['median']:.2f}x).",
        f"- {inside}/{len(aot_ratios)} AOT trials fall inside the control's own "
        f"range, i.e. are indistinguishable from measurement noise.",
        f"- Trials at or above {args.claim:.2f}x: "
        f"**{sum(1 for v in aot_ratios if v >= args.claim)}/{len(aot_ratios)}**.",
        "",
        "The packaged `.exe` is the engine binary with the bytecode appended, so "
        "it executes through the *same* C interpreter; parity is the expected "
        "result, and any ratio above 1.00x here is noise, not code generation.",
        "",
    ]

    report = "\n".join(lines)
    print(report)
    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=2), encoding="utf-8")
        print(f"written: {args.json}")


if __name__ == "__main__":
    main()
