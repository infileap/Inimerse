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

WHAT IT DID NOT REACH UNTIL THE SECOND CELL, and how that was found: the first
version built containers and asked for their LENGTH -- it never indexed one.
`say c[0]`, `c[i] = v`, a dict read at a key that is not there, a string indexed
by a character: each of those has its own helper with its own note, and none was
generated.  The agent that noticed was a person reading the generator against
the route's arm list, not a run going red -- 190 programs across five seeds came
back clean before and after, which is exactly what a blind spot looks like from
the inside.  The lesson is in the tool's own history rather than in a bug: a
generator's silence about a shape is indistinguishable from that shape being
correct.

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
    # `min(<set>)` is deliberately NOT generated: min takes one argument and its
    # answer for a non-set operand is nil, so composing it invites the
    # `min(a, b)` syntax error that flooded an earlier run with UNSOUND cases
    # that were the GENERATOR's fault, not this route's.  A set literal read
    # through len() is the shape that is actually answerable here.
    return "len([%s])" % set_literal(rng)   # brackets: len takes ONE argument


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
    # A SET LITERAL IS A STATEMENT SHAPE, NOT AN EXPRESSION: `1, 2` is how a set
    # is BOUND (`s = 1, 2`), so pasting it into `len(...)` produced
    # `len(42, 100, 1, 2)` -- four arguments -- and the run reported that as an
    # UNSOUND case, i.e. the generator's own bug wearing a finding's clothes
    # (measured on seed 101, case 0).  Wrap it in brackets, which IS an
    # expression and takes one argument.
    return "[" + set_literal(rng) + "]"


# NON-NEGATIVE ONLY, and that is a measured limit of the LANGUAGE, not of this
# route: `c = -1, 2` and `c = 1, -1, 2` both answer "Error: expected
# 'expression'", because the set-literal lookahead (src/parser/parser.c's
# looks_like_set_start) does not accept a leading '-' and the retry then sees a
# comma.  The interpreter and the MIR route refuse identically, so it is not a
# divergence -- but a generator that emits it reports a syntax error as a
# finding, which is the generator's bug wearing a finding's clothes.
SET_INTS = ["0", "1", "2", "3", "7", "42", "100"]


def set_literal(rng):
    """A set literal: a bare comma list, which is the ONLY set source the
    language has (src/parser/parser.c's looks_like_set_start)."""
    n = rng.randint(1, 4)
    return ", ".join(rng.choice(SET_INTS) for _ in range(n))


def set_expr(rng, names):
    """A set bound to a NAME, never a bare comma list in an expression.

    `c = 1, 2` parses; `0.5 * (1, 2)` does not -- not in this route and not in
    the interpreter, which answers "Error: expected 'expression', but got ','".
    The bare list is a STATEMENT shape (src/parser/parser.c's bind path), so a
    generator that pastes one into an expression produces a syntax error and
    calls it a finding.  Bindings are what these helpers are for.
    """
    return set_literal(rng)


def set_or_array(rng, names):
    """A CONTAINER operand for `in`: an array literal, or a bound name.

    Never a bare set list -- see set_expr's note.  `c` is in `names` because the
    generator binds it before composing; an unbound name reads nil, which is a
    legal operand and not a syntax error.
    """
    if rng.random() < 0.6:
        return "[" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(0, 4))) + "]"
    if names and rng.random() < 0.5:
        return rng.choice(names)
    return "[" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(0, 3))) + "]"


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
        # NOT appended to `names`: `c` may be a SET or a DICT, and pasting a
        # bare set list into an expression does not parse in EITHER runtime
        # (measured: "Error: expected 'expression', but got ','").  It is read
        # through len() above, which is the shape the language allows.
        lines.append("say len(c)")
        # THE INDEX SIDE, which the generator did not reach until now: a read at
        # an in-range subscript, a read past the end (the interpreter answers nil
        # rather than throwing, src/vm/vm.c:1002), and a WRITE that the array arm
        # of OP_INDEX_SET has to carry.  This route has a separate helper for
        # each of those and each has its own note; a generator that only builds
        # containers and asks for their length never exercises any of them.
        if kind == "arr":
            lines.append("say c[0]")
            lines.append("say c[%d]" % rng.randint(0, 3))
            lines.append("c[%d] = %s" % (rng.randint(0, 2), rng.choice(INTS)))
            lines.append("say c[0]")
        elif kind == "str":
            lines.append("say c[0]")            # a one-character string, or nil
        elif kind == "dict":
            lines.append('say c["a"]')
            lines.append('say c["zz"]')         # a key that is not there
            lines.append('c["b"] = %s' % rng.choice(INTS))
            lines.append('say c["b"]')
    # NESTED CONTAINERS AND ALIASES.  Both are places where a route that models
    # a container as a REGION IN A POOL can part company with a runtime that
    # models it as an OBJECT: a nested write has to reach THROUGH the outer
    # region, and a write through an alias has to be visible through the original
    # name.  Measured reachable in both runtimes: `n = [[1, 2], [3]]` /
    # `say n[1][0]`, `b = a` / `b[0] = 9` / `say a[0]` (both answer 9).
    if rng.random() < 0.35:
        role = rng.choice(["alias", "nested", "dictvalue"])
        if role == "alias":
            lines.append("p = [%s]" % ", ".join(rng.choice(INTS) for _ in range(rng.randint(1, 3))))
            lines.append("q = p")
            lines.append("q[0] = %s" % rng.choice(INTS))
            lines.append("say p[0]")
            lines.append("say q[0]")
            lines.append("say len(p)")
        elif role == "nested":
            lines.append("n = [[%s], [%s]]" % (rng.choice(INTS), rng.choice(INTS)))
            lines.append("say n[0][0]")
            lines.append("x = n[1]")
            lines.append("say x[0]")
            lines.append("say len(n)")
        else:
            lines.append('e = {"k": [%s, %s]}' % (rng.choice(INTS), rng.choice(INTS)))
            lines.append('say e["k"][0]')
            lines.append('say len(e["k"])')
            lines.append('e["k"][1] = %s' % rng.choice(INTS))
            lines.append('say e["k"][1]')
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
    skipped_refusals = 0
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
            why = ""
            for line in out_t.decode("utf-8", "replace").splitlines():
                if line.startswith("IR-MIR-REFUSED|"):
                    why = line.strip()
            # A NAMED refusal is this route working as designed, not a finding:
            # the boundary table (tools/aot_mir_surface.py) argues for every one
            # of them, and counting them as UNSOUND would make a run that
            # suddenly covered LESS look the same as a run that found a bug.
            # A PARSE error is the generator's own fault and is counted apart.
            named = ("|why=threads|", "|why=try-enters|", "|why=try-escapes|",
                     "|why=set-interval-domain|", "|why=lock-unpaired|",
                     "|why=index-out-of-range-constant|")
            text = why or (err_t or b"").decode("utf-8", "replace").strip()[:120]
            if any(k in text for k in named):
                skipped_refusals += 1
                continue
            unsound += 1
            findings.append("FUNSOUND|case=%d|why=%s" % (n, text))
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
    print("IR-AOT-MIR-FUZZ|seed=%d|count=%d|ran=%d|diverge=%d|threw=%d|unsound=%d"
          "|refused-by-name=%d|dir=%s"
          % (args.seed, args.count, ran, diverge, threw, unsound,
             skipped_refusals, work))
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
