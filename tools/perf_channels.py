#!/usr/bin/env python3
"""Compare the execution channels Inimerse actually has, against C++ and Rust.

Channels measured for each workload in `tools/bench/channels/`:

  interpreter  `inimerse <workload>.im <N>`   -- bytecode VM
  aot          `aot-native translate` -> C -> `cc -O2`  -- the native backend
  wasm         `inimerse compile --abi-target wasm` -> `node tools/wasm_run.js`
  c++          `g++ -O2`                      -- baseline, static types
  rust         `rustc -O`                     -- baseline, static types

The wasm channel was added after the first audit, which had only read the wasm
backend's emitter and therefore could say only that it *looked* consistent with
AOT.  Reading is not measuring, so it is a channel here.

## What this does NOT measure, and why

`--jit=template|optimized` is not a channel.  `im_jit_mode` is written by
`src/main.c` and read by nothing on the execution path (`grep -rn im_jit_mode
src/` hits only `src/main.c`, `src/vm/jit_mode.c` and its probe), and the flag
changes neither the bytecode nor the output.  `--jit` is passed to the
interpreter channel here purely so the claim stays testable rather than
asserted: `--jit-probe` runs the same workload under all three values and
checks that the bytecode hash and the output are identical.

## The correctness gate

A timing is only reported if every channel prints the same value for the same
input.  A faster channel that computes something else is not a faster channel.
The C++/Rust baselines run the same algorithm with static 64-bit integers; the
`.im` side is dynamically typed, and that difference is the thing being
measured rather than an accident of the translation.

## The scaling gate

Agreeing on the answer is not enough, because a compiler can agree by not
looping at all.  Each channel is therefore also run at 2N, and its time must
grow by at least `SCALING_MIN`.  A flat channel is reported as "optimized
away" and its timing is dropped rather than published.  `arith` -- a plain
accumulation -- is kept in the workload set precisely as the negative control
for this gate: its Rust channel is expected to be refused.

## Why the AOT channel must use --extern

`aot-native translate --extern N` declares the trip count as an extern global
instead of a literal.  Without it the host C compiler sees a constant workload,
evaluates the whole loop at compile time, and the resulting binary measures
nothing -- a fiction the tool refuses to produce: it asserts the generated C
reads the harness-supplied global.

## Why the wasm channel bakes N in instead of passing it

The wasm host (`tools/wasm_run.js`) takes no argv -- it calls
`inimerse_run(0)` -- and the MVP subset has no `args()`, so the template's
`N = int(args()[0])` is rejected at compile time ("function 'int' not found
(builtins are not in the wasm MVP subset)").  The wasm channel therefore
substitutes the literal trip count for the bind line and compiles a fresh
module, including a second one at 2N for the scaling gate; that is why every
channel here is a *function of the trip count* rather than a fixed argv.

Baking N in is not the same mistake as the AOT constant-workload trap: the
wasm backend emits a real loop and has no optimiser that could fold it.  But
that is a claim about code someone else wrote, so the scaling gate recompiles
at 2N and checks it instead of trusting it.

Peak RSS for this channel is the Node.js host plus the module, so it is not
comparable with the native channels' RSS and is reported as-is.

## Methodology

Wall time and peak RSS are read from `os.wait4()` for that one child.  The
obvious alternative, `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss`, is a
high-water mark across every child ever reaped, so the second run would
silently inherit the first run's peak.  Each channel gets a warmup run, then
`--reps` measured runs; the median is reported, with the spread, because a
single run on a shared machine is not a measurement.
"""

import argparse
import hashlib
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
CHANNELS = HERE / "bench" / "channels"

# Trip counts chosen so the *interpreter* -- always the slowest channel -- runs
# for roughly a second.  `fib` is exponential, so its N is a recursion depth,
# not an iteration count.
PARAMS = {
    "arith": 20_000_000,
    "branch": 10_000_000,
    "fib": 33,
    "lcg": 20_000_000,
    "nested": 3_400,
}

# The scaling gate.  Every workload's time must grow when N doubles; a channel
# whose time is flat has had its loop replaced by a closed form, and its
# "speed" is the speed of printing a number.  This is not hypothetical: with a
# plain accumulation loop, `rustc -O` computed the sum in closed form and the
# Rust channel sat at 0.74ms for N=10M, 50M, 100M and 200M alike -- a flat line
# that would have been reported as a 3522x speedup over the interpreter.
SCALING_MIN = 1.4


def scale_n(name, n):
    """How far to raise N for the scaling gate.

    Doubling is right for a loop, but `fib` is exponential: doubling its
    recursion depth squares the work, so it is compared against N+1 instead
    (the golden ratio, ~1.62, already clears SCALING_MIN).  A naive 2N here
    sends fib(66) off to run for geological time.
    """
    return n + 1 if name == "fib" else 2 * n

BIND_LINE = "N = int(args()[0])"

# The AOT preamble's value layout (src/compilation/aot_native.c kPreamble).
# The harness is a separate translation unit, so it redeclares the type rather
# than including the generated file; the layouts must agree, and this comment
# is the contract.
AOT_HARNESS = """/* Generated by tools/perf_channels.py.  Defines the trip count the translated
 * program reads through its `extern NV g_N;` declaration. */
#include <stdlib.h>

typedef struct { int t; long long i; double f; } NV;

NV g_N;
int nv_program_main(void);

int main(int argc, char **argv) {
    NV v;
    v.t = 0;            /* NV_INT */
    v.i = atoll(argv[1]);
    v.f = 0;
    g_N = v;
    return nv_program_main();
}
"""


def sh(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=600)


def last_int(stdout):
    """The program's answer: the last line that parses as an integer."""
    for line in reversed(stdout.splitlines()):
        line = line.strip()
        if not line:
            continue
        try:
            return int(line)
        except ValueError:
            continue
    return None


def measure(argv, reps, warmup=1):
    """Run argv; return median wall seconds and peak RSS KiB, from wait4."""
    for _ in range(warmup):
        p = subprocess.Popen(argv, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL,
                             stdin=subprocess.DEVNULL)
        os.wait4(p.pid, 0)

    walls, rss, out = [], [], ""
    for _ in range(reps):
        t0 = time.perf_counter()
        p = subprocess.Popen(argv, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL,
                             stdin=subprocess.DEVNULL)
        buf = p.stdout.read()
        p.stdout.close()
        _, status, ru = os.wait4(p.pid, 0)
        walls.append(time.perf_counter() - t0)
        rss.append(ru.ru_maxrss)
        if os.waitstatus_to_exitcode(status) != 0:
            return None, None, None, f"exit {os.waitstatus_to_exitcode(status)}"
        out = buf.decode(errors="replace")

    return (statistics.median(walls), max(rss), out,
            statistics.stdev(walls) if len(walls) > 1 else 0.0)


def build_interpreter(engine, im, n, tmp):
    return [str(engine), str(im), str(n)]


def build_aot(translator, im, cc, tmp):
    """Translate, compile, and prove the trip count is not a constant."""
    src = im.read_text(encoding="utf-8")
    stripped = [ln for ln in src.splitlines() if ln.strip() != BIND_LINE]
    if len(stripped) != len(src.splitlines()) - 1:
        return None, (f"{im.name}: expected exactly one {BIND_LINE!r} line to "
                      f"strip for the AOT variant; the template and this tool "
                      f"have drifted apart")
    aot_im = tmp / (im.stem + ".aot.im")
    aot_im.write_text("\n".join(stripped) + "\n", encoding="utf-8")

    gen = tmp / (im.stem + ".gen.c")
    r = sh([str(translator), "translate", "--extern", "N", str(aot_im), str(gen)])
    if r.returncode != 0:
        return None, f"translate failed: {r.stderr.strip()[:300]}"

    text = gen.read_text(encoding="utf-8")
    if "extern NV g_N;" not in text:
        return None, "generated C does not read the harness-supplied global"
    if "nv_program_main" not in text:
        return None, "generated C has no nv_program_main entry point"

    harness = tmp / (im.stem + ".harness.c")
    harness.write_text(AOT_HARNESS, encoding="utf-8")
    exe = tmp / (im.stem + ".aot")
    r = sh([cc, "-O2", "-o", str(exe), str(harness), str(gen)])
    if r.returncode != 0:
        return None, f"cc failed: {r.stderr.strip()[:300]}"
    return exe, None


def build_wasm(engine, node, runner, im, n, tmp):
    """Compile the workload to wasm with the trip count as a literal.

    Returns (argv, None) or (None, reason).  The bind line is *replaced*, not
    stripped: the wasm host passes no argv and the MVP subset has no `args()`,
    so the literal is the only way in.  Each trip count gets its own module, so
    the scaling gate calling this again at 2N is the intended use.
    """
    src = im.read_text(encoding="utf-8")
    if src.count(BIND_LINE) != 1:
        return None, (f"{im.name}: expected exactly one {BIND_LINE!r} line to "
                      f"replace for the wasm variant; the template and this "
                      f"tool have drifted apart")
    wasm_im = tmp / f"{im.stem}.n{n}.im"
    wasm_im.write_text(src.replace(BIND_LINE, f"N = {n}"), encoding="utf-8")
    out = tmp / f"{im.stem}.n{n}.wasm"
    r = sh([str(engine), "compile", "--abi-target", "wasm",
            str(wasm_im), str(out)])
    if r.returncode != 0:
        detail = (r.stderr.strip() or r.stdout.strip()).splitlines()
        return None, (f"wasm compile failed: "
                      f"{detail[-1][:300] if detail else 'no output'}")
    if not out.exists():
        return None, "wasm compile reported success but wrote no module"
    return [str(node), str(runner), str(out)], None


def build_cpp(src, cxx, tmp):
    exe = tmp / (src.stem + ".cpp.bin")
    r = sh([cxx, "-O2", "-o", str(exe), str(src)])
    if r.returncode != 0:
        return None, f"{cxx} failed: {r.stderr.strip()[:300]}"
    return exe, None


def build_rust(src, rustc, tmp):
    exe = tmp / (src.stem + ".rs.bin")
    r = sh([rustc, "-O", "-o", str(exe), str(src)])
    if r.returncode != 0:
        return None, f"rustc failed: {r.stderr.strip()[:300]}"
    return exe, None


def jit_probe(engine, im, n, reps):
    """Show that --jit does not change the bytecode or the answer."""
    hashes, answers = {}, {}
    for mode in ("off", "template", "optimized"):
        r = sh([str(engine), "--jit", mode, "bytecode", str(im)])
        hashes[mode] = hashlib.sha256(r.stdout.encode()).hexdigest()[:16]
        w, _, out, _ = measure([str(engine), "--jit", mode, str(im), str(n)],
                               reps=reps, warmup=1)
        answers[mode] = (last_int(out or ""), w)
    return hashes, answers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(REPO / "build" / "inimerse"))
    ap.add_argument("--translator", default=str(REPO / "build" / "aot-native"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    ap.add_argument("--rustc", default="rustc")
    ap.add_argument("--node", default="node")
    ap.add_argument("--wasm-runner", default=str(REPO / "tools" / "wasm_run.js"))
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--only", default=None, help="comma-separated workload names")
    ap.add_argument("--jit-probe", action="store_true")
    ap.add_argument("--no-scaling-gate", dest="scaling_gate",
                    action="store_false",
                    help="measure at N only; for debugging this tool, never for "
                         "reporting numbers -- a flat channel is a folded loop")
    args = ap.parse_args()

    engine, translator = Path(args.engine), Path(args.translator)
    for p, what in ((engine, "engine"), (translator, "aot-native translator")):
        if not p.exists():
            print(f"error: {what} not found at {p} "
                  f"(build it: cmake --build build)", file=sys.stderr)
            return 2

    runner = Path(args.wasm_runner)
    if not runner.exists():
        print(f"error: wasm runner not found at {runner}", file=sys.stderr)
        return 2
    node = args.node

    names = sorted(p.stem for p in CHANNELS.glob("*.im"))
    if args.only:
        want = set(args.only.split(","))
        names = [n for n in names if n in want]
    if not names:
        print("error: no workloads found in tools/bench/channels/", file=sys.stderr)
        return 2

    print(f"engine      {engine}")
    print(f"translator  {translator}")
    print(f"cc          {sh([args.cc, '--version']).stdout.splitlines()[0]}")
    print(f"cxx         {sh([args.cxx, '--version']).stdout.splitlines()[0]}")
    rustc_v = sh([args.rustc, "--version"])
    print(f"rustc       {rustc_v.stdout.strip() or rustc_v.stderr.strip()[:60]}")
    print(f"reps        {args.reps} (median, after 1 warmup)")
    print()

    rows, problems = [], []
    with tempfile.TemporaryDirectory(prefix="perf-channels-") as td:
        tmp = Path(td)
        for name in names:
            im = CHANNELS / f"{name}.im"
            n = PARAMS.get(name)
            if n is None:
                problems.append(f"{name}: no trip count in PARAMS; add one")
                continue

            # Every channel is a *function of the trip count*: the wasm channel
            # has to recompile for the scaling gate, while the others only need
            # a different argv.  `rebuild[chan](m)` -> (argv, error).
            exes, rebuild = {}, {}
            exes["interpreter"] = build_interpreter(engine, im, n, tmp)
            rebuild["interpreter"] = lambda m: ([str(engine), str(im), str(m)], None)

            for chan, builder in (
                ("aot", lambda: build_aot(translator, im, args.cc, tmp)),
                ("wasm", lambda: build_wasm(engine, node, runner, im, n, tmp)),
                ("c++", lambda: build_cpp(CHANNELS / f"{name}.cpp", args.cxx, tmp)),
                ("rust", lambda: build_rust(CHANNELS / f"{name}.rs", args.rustc, tmp)),
            ):
                built, err = builder()
                if err:
                    problems.append(f"{name}/{chan}: {err}")
                    exes[chan] = None
                elif chan == "wasm":
                    exes[chan] = built
                    rebuild[chan] = lambda m: build_wasm(engine, node, runner,
                                                         im, m, tmp)
                else:
                    exes[chan] = [str(built), str(n)]
                    rebuild[chan] = lambda m, built=built: ([str(built), str(m)], None)

            if any(v is None for v in exes.values()):
                continue

            results, answers = {}, {}
            for chan, argv in exes.items():
                w, rss, out, spread = measure(argv, reps=args.reps)
                if w is None:
                    problems.append(f"{name}/{chan}: {spread}")
                    continue

                if args.scaling_gate:
                    n2 = scale_n(name, n)
                    # Built, not patched: the wasm channel's trip count lives in
                    # the module, so raising N means recompiling.  (The earlier
                    # form, argv[:-1] + [n2], assumed the trip count was the
                    # last argv element for every channel; it is for the others,
                    # but there is no such element for wasm.)
                    argv2, err2 = rebuild[chan](n2)
                    if argv2 is None:
                        problems.append(f"{name}/{chan}: at N={n2}: {err2}")
                        continue
                    w2, _, _, spread2 = measure(argv2, reps=max(3, args.reps // 2))
                    if w2 is None:
                        problems.append(f"{name}/{chan}: at N={n2}: {spread2}")
                        continue
                    ratio = w2 / w if w else 0.0
                    if ratio < SCALING_MIN:
                        problems.append(
                            f"{name}/{chan}: OPTIMIZED AWAY -- {w:.4f}s at N={n} "
                            f"vs {w2:.4f}s at N={n2} (ratio {ratio:.2f}x, need "
                            f">= {SCALING_MIN}); the loop was folded, so this "
                            f"channel gets no timing")
                        continue

                answers[chan] = last_int(out)
                results[chan] = (w, rss, spread)

            if len(results) < 2:
                problems.append(f"{name}: fewer than two channels survived the "
                                f"gates, so nothing is comparable")
                continue

            distinct = set(answers.values())
            if len(distinct) != 1:
                problems.append(
                    f"{name}: channels disagree on the answer for N={n}: "
                    + ", ".join(f"{c}={answers[c]}" for c in sorted(answers)))
                continue

            rows.append((name, n, distinct.pop(), results))

    if problems:
        print("PROBLEMS (these workloads produced no timing):")
        for p in problems:
            print(f"  - {p}")
        print()

    if not rows:
        print("no workload produced a comparable result", file=sys.stderr)
        return 1

    print(f"{'workload':<10}{'N':>11}  {'channel':<12}{'median':>9}{'sd':>7}"
          f"{'peak RSS':>11}{'vs interp':>11}")
    print("-" * 73)
    for name, n, answer, results in rows:
        base = results.get("interpreter", (None,))[0]
        for chan in ("interpreter", "aot", "wasm", "c++", "rust"):
            if chan not in results:
                continue
            w, rss, sd = results[chan]
            ratio = (base / w) if (base and w) else 0.0
            label = f"{ratio:>10.2f}x" if ratio else f"{'-':>11}"
            print(f"{name:<10}{n:>11}  {chan:<12}{w:>8.3f}s{sd:>7.3f}"
                  f"{rss / 1024:>9.1f}MB{label}")
        print(f"{'':<10}{'':>11}  answer={answer}")
        print()

    if args.jit_probe:
        print("--jit probe (the flag must change nothing):")
        for name in names:
            hashes, answers = jit_probe(engine, CHANNELS / f"{name}.im",
                                        PARAMS[name], reps=3)
            same_hash = len(set(hashes.values())) == 1
            same_ans = len({a for a, _ in answers.values()}) == 1
            times = "  ".join(f"{m}={w:.3f}s" for m, (_, w) in answers.items())
            print(f"  {name:<10} bytecode={'identical' if same_hash else 'DIFFERS'}"
                  f"  answer={'identical' if same_ans else 'DIFFERS'}  {times}")
        print()

    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
