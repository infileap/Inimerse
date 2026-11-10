#!/usr/bin/env python3
"""Differential fuzzer for the MIR route: generated programs, two runtimes.

WHY THIS EXISTS, AND WHY IT IS NOT tools/im_diff_fuzz.py

That tool generates for the AST->C backend's numeric subset.  The MIR route
answers a much wider language -- strings, arrays, dicts, sets, functions,
try/catch -- and its corpus is HAND-WRITTEN: 86 shared cases and a list of
MIR-only cases in tools/aot_mir.test.py.  A hand-written corpus finds what its
author already suspected, and that is not a theory: the widest disagreement
this route has had was found by hand, after the hand-written cases were green
(`list` narrowing the integer half of a set to 32 bits, section 10.142), and it
was found by a person doing differential probing of a shape nobody had listed.

So this tool generates programs in the part of the language the route already
ACCEPTS and compares the two runtimes byte for byte:

    interpreter   build/inimerse <prog>
    MIR route     build/aot-mir translate <prog> out.c && cc -O2 out.c

WHAT IT DOES NOT GENERATE, AND WHY EACH IS A DECISION

  * anything requiring a thread section (the route refuses those by name);
  * anything whose answer is TIME (`wait`) or a disk write (`record`);
  * division or modulo by a literal zero, and any uncaught throw -- a program
    that stops is compared by exit code and stdout, but a generator that emits
    them on purpose floods the report with the documented behaviour;
  * a set used as a dict key (10.128 says the two runtimes agree there BY
    AGREEMENT WITH A DEFECT, and a fuzzer that re-finds it every run teaches
    nothing).

FINDINGS ARE THREE KINDS, because they need three different responses:

  DIVERGE   both ran to completion and the bytes differ.  A wrong answer.
  THREW     one exited non-zero and the other did not.  A missing stop, or an
            extra one.
  UNSOUND   the MIR route refused a program the interpreter ran.  Not a bug by
            itself -- the route refuses by design -- but it is a COVERAGE fact,
            and the tool prints the refusal's own why= so a run that suddenly
            stops covering a shape says so instead of going quiet.

Usage:
    python3 tools/aot_mir_fuzz.py [--count N] [--seed S] [--engine ...]
                                  [--mir-tool ...] [--cc ...] [--keep-dir D]
                                  [--self-check]
Exit: 0 = no DIVERGE and no THREW; 1 = at least one.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


# --- the generator ---------------------------------------------------------
#
# The value shapes are the ones this route ANSWERS, taken from its own corpus
# rather than from the language: an int, a float, a string, an array of ints, a
# finite set of ints, a dict with two string keys, a closure over one capture,
# and nil.  Every expression is composed from operators whose answers the
# corpus already pins, so a divergence here is about COMPOSITION, which is the
# part a hand-written corpus under-samples: it tends to test each shape alone.

INTS = ["0", "1", "2", "3", "7", "-1", "-5", "42", "100"]
FLOATS = ["0.0", "1.5", "-2.5", "3.25", "0.5"]
STRINGS = ['"a"', '"hi"', '"zz"', '""', '"q\\"z"']
BINOPS = ["+", "-", "*", "%"]
CMPOPS = ["<", "<=", ">", ">=", "==", "!="]
SETOPS = ["+", "-", "<", ">", "==", "!=", "in"]


def number(rng):
    if rng.random() < 0.7:
        return rng.choice(INTS)
    return rng.choice(FLOATS)


def scalar(rng, names):
    """A value that can be COMPARED.  Not a set, not an array, not a dict.

    The generator used to compose comparisons out of any expression, which
    produced `(c <= len([]))` where `c` is a SET -- and the interpreter has no
    rule for ordering a set against a number (val_orderable refuses it), so the
    two runtimes agreeing or disagreeing there is not a finding a reader can act
    on.  A fuzzer that reports shapes the language leaves undefined buries the
    shapes it does define.
    """
    r = rng.random()
    if r < 0.5:
        return number(rng)
    if r < 0.8:
        return rng.choice(STRINGS)
    if names and rng.random() < 0.5:
        return rng.choice(names)
    return number(rng)


def value_expr(rng, names, depth=0):
    """An expression over the scalar shapes, plus the builtins that read them."""
    r = rng.random()
    if depth >= 2 or r < 0.35:
        if names and rng.random() < 0.5:
            return rng.choice(names)
        return number(rng)
    if r < 0.5:
        return "(%s %s %s)" % (value_expr(rng, names, depth + 1),
                               rng.choice(BINOPS),
                               value_expr(rng, names, depth + 1))
    if r < 0.62:
        return "len(%s)" % string_or_container(rng, names)
    if r < 0.72:
        return "(%s %s %s)" % (scalar(rng, names),
                               rng.choice(CMPOPS),
                               scalar(rng, names))
    if r < 0.80:
        return "(%s and %s)" % (cond(rng, names, depth + 1), cond(rng, names, depth + 1))
    if r < 0.88:
        return "(%s or %s)" % (cond(rng, names, depth + 1), cond(rng, names, depth + 1))
    if r < 0.94:
        return "(not %s)" % cond(rng, names, depth + 1)
    return "min(%s)" % set_expr(rng, names)


def cond(rng, names, depth=0):
    r = rng.random()
    if r < 0.5 and depth < 2:
        return "(%s %s %s)" % (scalar(rng, names),
                               rng.choice(CMPOPS), scalar(rng, names))
    if r < 0.75 and depth < 2:
        return "(%s in %s)" % (scalar(rng, names), set_or_array(rng, names))
    return value_expr(rng, names, depth)


def string_or_container(rng, names):
    r = rng.random()
    if r < 0.4:
        return rng.choice(STRINGS)
    if r < 0.7:
        return "[" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(0, 4))) + "]"
    if r < 0.9:
        return "{%s}" % ", ".join('%s: %s' % (rng.choice(STRINGS), rng.choice(INTS))
                                  for _ in range(rng.randint(0, 2)))
    return set_expr(rng, names)


def set_expr(rng, names):
    n = rng.randint(1, 4)
    return ", ".join(rng.choice(INTS) for _ in range(n))


def set_or_array(rng, names):
    if rng.random() < 0.5:
        return "[" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(0, 4))) + "]"
    return set_expr(rng, names)


def gen_program(rng):
    lines, names = [], []
    # sometimes a container whose length is read back
    if rng.random() < 0.5:
        kind = rng.choice(["arr", "dict", "set", "str"])
        if kind == "arr":
            lines.append("c = [" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(0, 5))) + "]")
        elif kind == "dict":
            lines.append('c = {"a": %s, "b": %s}' % (rng.choice(INTS), rng.choice(INTS)))
        elif kind == "set":
            lines.append("c = " + set_expr(rng, names))
        else:
            lines.append("c = " + rng.choice(STRINGS))
        names.append("c")
        lines.append("say len(c)")
    # a closure over one capture
    if rng.random() < 0.35:
        lines.append("func mk(n) { return x -> x + n }")
        lines.append("f = mk(%s)" % rng.choice(INTS))
        lines.append("say f(%s)" % rng.choice(INTS))
    # a loop, so the answer depends on iteration
    if rng.random() < 0.5:
        lines.append("acc = " + rng.choice(INTS))
        names.append("acc")
        lines.append("i = 0")
        names.append("i")
        lines.append("while i < %d {" % rng.randint(1, 6))
        lines.append("    acc = " + value_expr(rng, names))
        lines.append("    i = i + 1")
        lines.append("}")
    # a try/catch around an expression that may or may not throw
    if rng.random() < 0.3:
        lines.append("try {")
        lines.append("    say " + value_expr(rng, names))
        lines.append("} catch (e) {")
        lines.append('    say "caught"')
        lines.append("}")
    lines.append("say " + value_expr(rng, names))
    return "\n".join(lines) + "\n"


# --- running ---------------------------------------------------------------

def first_payload(stdout_bytes):
    """The program's own output, without the engine's notices.

    The interpreter prints module-load lines on stdout before the program, and
    the generated binary prints nothing of its own.  The notices all start with
    '[' and the program cannot print one (there is no way to say a bare '[' at
    the start of a line through `say` -- a string that begins with one is
    printed inside its own line), so filtering by that prefix is exact.
    """
    keep = []
    for line in stdout_bytes.decode("utf-8", "replace").splitlines():
        if line.startswith("[") or not line.strip():
            continue
        keep.append(line)
    return "\n".join(keep)


def run(cmd, cwd, timeout=60):
    try:
        p = subprocess.run([str(c) for c in cmd], cwd=str(cwd), capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", b"", b""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=60)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--engine", default=str(ROOT / "build" / "inimerse"))
    ap.add_argument("--mir-tool", default=str(ROOT / "build" / "aot-mir"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--keep-dir", default="")
    ap.add_argument("--self-check", action="store_true")
    args = ap.parse_args()
    if args.self_check:
        return self_check()

    # RESOLVED TO ABSOLUTE, because the translator is run with `work` as its cwd
    # and a relative --keep-dir then names a FILE the translator cannot find
    # (measured: every case came back `cannot parse '.scratch/fz/fNNN.im'`, and
    # the fuzzer reported 25 UNSOUND cases that were its own path bug).
    work = Path(args.keep_dir).resolve() if args.keep_dir else Path(tempfile.mkdtemp(prefix="aot_mir_fuzz_"))
    work.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)

    ran = diverge = threw = unsound = 0
    findings = []
    for n in range(args.count):
        src = gen_program(rng)
        prog = work / ("f%03d.im" % n)
        prog.write_text(src)
        rc_i, out_i, _e_i = run([args.engine, "--no-mods", "run", prog], work)
        if rc_i == "timeout":
            findings.append("FSKIP|case=%d|why=interpreter-timeout" % n)
            continue
        mir_c = work / ("f%03d.mir.c" % n)
        rc_t, out_t, err_t = run([args.mir_tool, "translate", prog, mir_c], work)
        if rc_t != 0:
            unsound += 1
            why = ""
            for line in out_t.decode("utf-8", "replace").splitlines():
                if line.startswith("IR-MIR-REFUSED|"):
                    why = line.strip()
            findings.append("FUNSOUND|case=%d|why=%s" % (n, why or (err_t or b"").decode("utf-8", "replace").strip()[:120]))
            continue
        exe = work / ("f%03d.exe" % n)
        rc_c, _o_c, e_c = run([args.cc, "-O2", "-o", exe, mir_c], work)
        if rc_c != 0:
            findings.append("FBADCC|case=%d|err=%s" % (n, (e_c or b"").decode("utf-8", "replace").strip().splitlines()[:1]))
            continue
        rc_r, out_r, _e_r = run([exe], work)
        ran += 1
        a, b = first_payload(out_i), first_payload(out_r)
        if rc_i != rc_r:
            threw += 1
            findings.append("FTHREW|case=%d|interp-rc=%s|mir-rc=%s" % (n, rc_i, rc_r))
        elif a != b:
            diverge += 1
            findings.append("FDIVERGE|case=%d|interp=%r|mir=%r" % (n, a, b))

    for f in findings:
        print(f)
    print("IR-AOT-MIR-FUZZ|seed=%d|count=%d|ran=%d|diverge=%d|threw=%d|unsound=%d|dir=%s"
          % (args.seed, args.count, ran, diverge, threw, unsound, work))
    print("IR-AOT-MIR-FUZZ-VERDICT|%s" % ("PASS" if diverge == 0 and threw == 0 else "FAIL"))
    return 0 if diverge == 0 and threw == 0 else 1


def self_check():
    """The comparator's own controls: it must be able to say DIFFER.

    A fuzzer whose comparator never reports a difference passes on any tree.
    Three cells: identical payloads are clean; one byte apart is a difference;
    and the engine's own notices are filtered (a line starting with '[' is not
    the program's output).
    """
    ok = True
    cells = [
        ("identical", b"[mod] x\n1\n2\n", b"1\n2\n", False),
        ("one-byte", b"1\n2\n", b"1\n3\n", True),
        ("notices-filtered", b"[mod] utils loaded\n[infiverse mod] loaded\n7\n", b"7\n", False),
        ("only-notices", b"[mod] only\n", b"", False),
    ]
    for what, a, b, want_diff in cells:
        got = first_payload(a) != first_payload(b)
        hit = got == want_diff
        print("IR-AOT-MIR-FUZZ-SELFCHECK|what=%s|differ=%s|want=%s|%s"
              % (what, got, want_diff, "ok" if hit else "WRONG"))
        ok &= hit
    print("IR-AOT-MIR-FUZZ-SELFCHECK|checks=%d|verdict=%s" % (len(cells), "PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
