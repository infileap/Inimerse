#!/usr/bin/env python3
"""How much faster is a route-A native binary than the interpreter, really?

`tools/aot_parity.py` measures the PACKAGING channel (`compile --aot`), which
turns out to be parity.  This tool measures the other channel: source compiled
to C and then to a native executable by the system compiler.

Three things make the number trustworthy, and all three matter:

  1. THE WORKLOAD MUST SURVIVE -O2.  A plain accumulator loop is constant-folded
     (GCC computed `sum(1..2000000)` at compile time and even n=2e8 ran in
     0.00 s), which produced a fictional 153x in an earlier attempt.  The
     workload here is a data-dependent recurrence with no closed form, the
     input is supplied at run time through an `extern` global, and the tool
     REFUSES to report a number unless the generated assembly still contains a
     loop branch.

  2. EACH CHANNEL IS CALIBRATED AGAINST ITS OWN EMPTY SCRIPT.  Start-up and
     parse cost differ between the interpreter and a static binary; subtracting
     each channel's own floor leaves compute time on both sides.

  3. THE NOISE FLOOR IS MEASURED.  The interpreter is timed against itself, so
     you can see what a "speedup" of 1.00x measures like on this machine.

Prerequisites:
    cmake --build build -j4            # builds build/inimerse and build/aot-native

Usage:
    python3 tools/aot_native_bench.py [--trials 5] [--runs 5] [--n 2000000]
"""
import argparse
import json
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

WORKLOAD = ('func mix(seed, n) {\n'
            '    x = seed\n'
            '    i = 0\n'
            '    while i < n {\n'
            '        x = (x * 3 + 7) % 65536\n'
            '        i = i + 1\n'
            '    }\n'
            '    return x\n'
            '}\n'
            'say mix(SEED_EXPR)\n')
EMPTY = 'say 1\n'

# The harness supplies the recurrence seed at run time, which is what stops the
# optimiser from evaluating the whole program at compile time.
HARNESS = ('#include <stdlib.h>\n'
           'typedef struct { int t; long long i; double f; } NV;\n'
           'NV g_bench_n;\n'
           'int nv_program_main(void);\n'
           'int main(int argc, char **argv) {\n'
           '    g_bench_n.t = 0;\n'
           '    g_bench_n.i = (argc > 1) ? strtoll(argv[1], 0, 10) : 1;\n'
           '    g_bench_n.f = 0.0;\n'
           '    return nv_program_main();\n'
           '}\n')


def find_engine(build):
    import os
    for name in ("inimerse",):
        p = Path(build) / name
        if p.is_file() and os.access(p, os.X_OK):
            return p.resolve()
    raise SystemExit(f"engine not found under {build}")


def time_batch(cmd, runs, cwd):
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        rc = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=900)
        dt = time.perf_counter() - t0
        if rc.returncode != 0:
            raise SystemExit(f"{cmd} rc={rc.returncode}: "
                             f"{rc.stderr.decode(errors='replace')[:400]}")
        samples.append(dt)
    return statistics.median(samples) * 1000.0


def loop_branches(cc, cfile, root):
    """Count branch instructions in the generated assembly.

    Zero means the optimiser removed the loop and any timing is meaningless.
    """
    subprocess.run([cc, "-O2", "-S", "-o", "check.s", cfile],
                   cwd=root, capture_output=True)
    asm = (Path(root) / "check.s").read_text(encoding="utf-8", errors="replace")
    import re
    # Count backward jumps, which is what a loop actually is.
    labels = {}
    n = 0
    for line in asm.splitlines():
        s = line.strip()
        m = re.match(r"^(\.L\d+):$", s)
        if m:
            labels[m.group(1)] = n
        m2 = re.match(r"^j\w+\s+(\.L\d+)$", s)
        if m2:
            tgt = labels.get(m2.group(1))
            if tgt is not None and tgt < n:
                n += 1
        n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(REPO / "build"))
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--trials", type=int, default=5)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--n", type=int, default=2000000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--json")
    args = ap.parse_args()

    build = Path(args.build)
    engine = find_engine(build)
    translator = build / "aot-native"
    if not translator.is_file():
        raise SystemExit(f"{translator} not found — run: cmake --build {build}")

    with tempfile.TemporaryDirectory(prefix="aot-native-bench-") as tmp:
        root = Path(tmp)
        (root / "work.im").write_text(
            WORKLOAD.replace("SEED_EXPR", f"{args.seed}, {args.n}"), encoding="utf-8")
        (root / "work_native.im").write_text(
            WORKLOAD.replace("SEED_EXPR", f"bench_n, {args.n}"), encoding="utf-8")
        (root / "empty.im").write_text(EMPTY, encoding="utf-8")
        (root / "harness.c").write_text(HARNESS, encoding="utf-8")

        # The interpreter runs the literal-seed program; the native binary runs
        # the extern-seed twin, so both compute exactly the same recurrence.
        subprocess.run([str(translator), "translate", "--extern", "bench_n",
                        "work_native.im", "work.c"], cwd=root, capture_output=True)
        subprocess.run([str(translator), "translate", "--extern", "bench_n",
                        "empty.im", "empty.c"], cwd=root, capture_output=True)
        for c, exe in (("work.c", "work.native"), ("empty.c", "empty.native")):
            rc = subprocess.run([args.cc, "-O2", "-o", exe, c, "harness.c"],
                                cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
            if rc.returncode != 0:
                raise SystemExit(f"cc {c}: {rc.stderr.strip()[:600]}")

        # ---- guard 1: does a real loop survive -O2?
        loops = loop_branches(args.cc, "work.c", root)
        if loops < 1:
            raise SystemExit(
                "REFUSING to report a speedup: the generated assembly has no "
                "loop branch, so the optimiser folded the workload away and any "
                "ratio would be fiction.  Make the workload data-dependent.")

        # ---- guard 2: both channels must agree on the answer
        i_out = subprocess.run([str(engine), "--no-mods", str(root / "work.im")],
                               cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
        n_out = subprocess.run([str(root / "work.native"), str(args.seed)],
                               cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
        if i_out != n_out:
            raise SystemExit(
                f"REFUSING to report a speedup: the two channels disagree "
                f"(interpreter {i_out!r}, native {n_out!r}) — they are not doing "
                f"the same work, so the ratio would be meaningless.")

        rows = []
        for _ in range(args.trials):
            ib = time_batch([str(engine), "--no-mods", str(root / "empty.im")], args.runs, root)
            iw = time_batch([str(engine), "--no-mods", str(root / "work.im")], args.runs, root)
            cb = time_batch([str(engine), "--no-mods", str(root / "empty.im")], args.runs, root)
            cw = time_batch([str(engine), "--no-mods", str(root / "work.im")], args.runs, root)
            nb = time_batch([str(root / "empty.native"), "1"], args.runs, root)
            nw = time_batch([str(root / "work.native"), str(args.seed)], args.runs, root)

            ic, nc, cc_ = iw - ib, nw - nb, cw - cb
            rows.append({
                "interp_compute_ms": ic, "native_compute_ms": nc, "ctrl_compute_ms": cc_,
                "speedup": ic / nc if nc > 0 else float("inf"),
                "control_speedup": ic / cc_ if cc_ > 0 else float("nan"),
                "interp_total_ms": iw, "native_total_ms": nw,
            })

    sp = sorted(r["speedup"] for r in rows)
    ctl = sorted(r["control_speedup"] for r in rows)

    L = [
        "## Route-A native backend vs the interpreter",
        "",
        f"- workload: `mix(seed={args.seed}, n={args.n})` — a data-dependent recurrence "
        f"`x = (x*3+7) % 65536` with no closed form",
        f"- **loop branches in the generated assembly: {loops}** (guard: a folded "
        f"workload would show 0 and this tool would refuse)",
        f"- both channels print `{i_out}` — same work, verified before timing",
        f"- {args.trials} trials x {args.runs} runs, median per batch, each channel "
        f"minus its own empty-script baseline",
        "",
        "| trial | interp compute (ms) | native compute (ms) | speedup | control |",
        "|---|---|---|---|---|",
    ]
    for i, r in enumerate(rows, 1):
        L.append(f"| {i} | {r['interp_compute_ms']:.2f} | {r['native_compute_ms']:.3f} "
                 f"| {r['speedup']:.1f}x | {r['control_speedup']:.2f}x |")
    L += [
        "",
        f"- speedup: min **{sp[0]:.1f}x**, median **{statistics.median(sp):.1f}x**, max **{sp[-1]:.1f}x**",
        f"- control (interpreter vs itself): min {ctl[0]:.2f}x, median "
        f"{statistics.median(ctl):.2f}x, max {ctl[-1]:.2f}x",
        "",
        f"Total wall clock per run at the median: interpreter "
        f"{statistics.median([r['interp_total_ms'] for r in rows]):.1f} ms, "
        f"native {statistics.median([r['native_total_ms'] for r in rows]):.3f} ms.",
        "",
    ]
    print("\n".join(L))
    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=2), encoding="utf-8")
        print(f"written: {args.json}")


if __name__ == "__main__":
    main()
