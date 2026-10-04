#!/usr/bin/env python3
"""Differential fuzzer: the interpreter versus the AOT backend.

Hand-written probes found the divergences in docs/AUDIT.md.  Hand-written
probes only find what the author already suspected, so this tool generates
programs instead and reports every pair where the two backends disagree.

Both backends are given the same source.  The interpreter runs it directly;
the AOT path goes through `aot-native translate` and `cc -O2`, exactly as a
user would.  Only the AOT-supported subset is generated (int/float/bool,
+ - * / %, < <= > >= == !=, and/or/not, if/else, while, repeat, break,
assignment, user functions, globals, say), so a failure to translate is
reported as a generator bug rather than counted as a divergence.

Two classes of finding, and they are different things:

  DIVERGE   both backends produced a value, and the values differ.
  THREW     one backend refused (an uncaught exception) and the other did not.

`%` and `/` by a literal zero are avoided when generating, because a thrown
division_by_zero is a documented behaviour rather than a divergence; a zero
that arises from *evaluation* is still possible and is reported, because the
two backends disagree about what to do with it.
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

# Constants worth reaching for.  The interesting ones are not "42": they are
# the boundaries where int32 and double stop agreeing.
CONSTANTS = [
    "0", "1", "2", "3", "7", "10", "31",
    "2147483646", "2147483647", "2147483648", "2147483649",
    "4294967295", "4294967296",
    "9007199254740991", "9007199254740992", "9007199254740993",
    "1000000007", "3037000499", "3037000500",
]
# Divisors, none of them zero.
DIVISORS = ["1", "2", "3", "7", "10", "31", "1000003", "2147483647"]
BINOPS = ["+", "-", "*", "/", "%"]
CMPOPS = ["<", "<=", ">", ">=", "==", "!="]


def gen_expr(rng, depth, names):
    """A random expression, never dividing by a literal zero."""
    if depth <= 0 or rng.random() < 0.25:
        if names and rng.random() < 0.5:
            return rng.choice(names)
        return rng.choice(CONSTANTS)
    if rng.random() < 0.15:
        return f"not ({gen_expr(rng, depth - 1, names)})"
    if rng.random() < 0.2:
        a, b = gen_expr(rng, depth - 1, names), gen_expr(rng, depth - 1, names)
        return f"({a} {rng.choice(CMPOPS)} {b})"
    if rng.random() < 0.15:
        a, b = gen_expr(rng, depth - 1, names), gen_expr(rng, depth - 1, names)
        return f"({a} {rng.choice(['and', 'or'])} {b})"
    op = rng.choice(BINOPS)
    a = gen_expr(rng, depth - 1, names)
    b = rng.choice(DIVISORS) if op in "/%" and rng.random() < 0.6 \
        else gen_expr(rng, depth - 1, names)
    return f"({a} {op} {b})"


def gen_program(rng):
    """One program, ending in a single `say` of a value."""
    lines, names = [], []
    if rng.random() < 0.5:
        # A function whose result the main body uses.
        params = ["a", "b"][: rng.randint(1, 2)]
        body = gen_expr(rng, 3, params)
        lines.append(f"func f({' , '.join(params)}) {{ return {body} }}")
        # The call must match the arity.  The AOT backend checks arity and
        # refuses to translate a mismatch, so a generator that always passes
        # two arguments would report its own bug as "outside the subset" --
        # and would bury the real observation that the interpreter does NOT
        # check arity (a documented hazard) while the AOT does.
        args = ", ".join(rng.choice(CONSTANTS) for _ in params)
        lines.append(f"g = f({args})")
        names.append("g")

    # A loop, so the answer depends on iteration rather than one evaluation.
    if rng.random() < 0.6:
        names.append("acc")
        names.append("i")
        step = gen_expr(rng, 2, names)
        lines.append("acc = " + rng.choice(CONSTANTS))
        lines.append("i = 0")
        lines.append(f"while i < {rng.randint(1, 40)} {{")
        lines.append(f"    acc = ({step})")
        lines.append("    i = i + 1")
        lines.append("}")
    elif rng.random() < 0.4:
        lines.append("acc = " + gen_expr(rng, 3, names))
        names.append("acc")

    final = gen_expr(rng, 3, names) if names else gen_expr(rng, 3, [])
    lines.append(f"say {final}")
    return "\n".join(lines) + "\n"


def run(argv, cwd, timeout=30):
    """(rc, last non-empty stdout line).  The engine echoes literals, so the
    answer is the last line that is not an engine notice."""
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "<timeout>"
    last = ""
    for line in p.stdout.splitlines():
        if line.startswith(("[", "warning:", "error:")) or not line.strip():
            continue
        last = line.strip()
    return p.returncode, last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(REPO / "build" / "inimerse"))
    ap.add_argument("--translator", default=str(REPO / "build" / ("aot-native.exe" if os.name == "nt" else "aot-native")))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--count", type=int, default=120)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep", default=None,
                    help="directory to keep failing programs in")
    args = ap.parse_args()

    for p, what in ((Path(args.engine), "engine"),
                    (Path(args.translator), "aot-native translator")):
        if not p.exists():
            print(f"error: {what} not found at {p} (build it: cmake --build build)",
                  file=sys.stderr)
            return 2

    rng = random.Random(args.seed)
    diverged, threw, untranslated, agreed = [], [], [], 0

    with tempfile.TemporaryDirectory(prefix="im-diff-fuzz-") as td:
        tmp = Path(td)
        for n in range(args.count):
            src = gen_program(rng)
            im = tmp / f"f{n}.im"
            im.write_text(src, encoding="utf-8")

            rc_im, out_im = run([args.engine, str(im)], tmp)

            gen_c = tmp / f"f{n}.c"
            r = subprocess.run([args.translator, "translate", str(im), str(gen_c)],
                               capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
            if r.returncode != 0:
                untranslated.append((src, r.stderr.strip().splitlines()[:1]))
                continue
            exe = tmp / f"f{n}.bin"
            r = subprocess.run([args.cc, "-O2", "-o", str(exe), str(gen_c)],
                               capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
            if r.returncode != 0:
                untranslated.append((src, r.stderr.strip().splitlines()[:1]))
                continue
            rc_aot, out_aot = run([str(exe)], tmp)

            if rc_im != 0 and rc_aot == 0:
                threw.append((src, out_im, out_aot))
            elif rc_im == 0 and rc_aot != 0:
                threw.append((src, out_im, out_aot))
            elif out_im != out_aot:
                diverged.append((src, out_im, out_aot))
            else:
                agreed += 1

    print(f"seed {args.seed}, {args.count} programs")
    print(f"  agreed          {agreed}")
    print(f"  DIVERGE         {len(diverged)}")
    print(f"  THREW           {len(threw)}")
    print(f"  not translated  {len(untranslated)}")
    print()

    if diverged:
        print("=== DIVERGE: both produced a value, and the values differ ===")
        for src, a, b in diverged[:8]:
            print(f"--- interpreter={a!r}  aot={b!r}")
            print(src)
    if threw:
        print("=== THREW: one backend refused and the other did not ===")
        for src, a, b in threw[:8]:
            print(f"--- interpreter={a!r}  aot={b!r}")
            print(src)
    if untranslated:
        print("=== NOT TRANSLATED (generator produced something outside the "
              "AOT subset, or the subset is narrower than documented) ===")
        for src, err in untranslated[:5]:
            print(f"--- {err}")
            print(src)

    if args.keep and (diverged or threw or untranslated):
        keep = Path(args.keep)
        keep.mkdir(parents=True, exist_ok=True)
        for i, (src, *_) in enumerate(diverged + threw + untranslated):
            (keep / f"case{i}.im").write_text(src, encoding="utf-8")
        print(f"kept {len(diverged) + len(threw) + len(untranslated)} cases in {keep}")

    return 1 if (diverged or threw or untranslated) else 0


if __name__ == "__main__":
    sys.exit(main())
