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

WHY THE GENERATOR NOW DECLARES ITS OWN FAMILIES (§10.156)

§10.154 found TWO DEAD FAMILIES by a person reading this file against the
route's arm list: a SETOPS table no branch ever read, and no leaf returning a
bare string, so `say "a" + 1` could not be generated at all.  Both were found by
READING, not by a run going red -- 2500 generated programs stood behind them
reporting PASS, because a generator's silence about a shape is
indistinguishable from that shape being correct when you are standing inside
the generator.

So the generator now says which families it HAS, registers each family it
actually EMITS while generating, prints a census, and goes RED when a declared
family emitted zero programs:

    FAMILIES                 the declaration, in this file, above gen_program
    emitted.add(name)        the registration, at each family's own block
    IR-AOT-MIR-FUZZ-FAMILY|name=<n>|programs=<k>|of=<count>     the census
    FFAMILY-SILENT|family=<n>|programs=0|of=<count>            the red
    IR-AOT-MIR-FUZZ-FAMILIES|declared=|emitted=|silent=        the roll-up

`--suppress-family <name>` forces one declared family's dice to zero, which is
how the red is REPRODUCED by a reader instead of being taken on the word of a
commit message.  It suppresses by skipping the family's block, so the run is a
genuinely different run -- the point is not that the rest of the run is
untouched, it is that the census of a family that produced nothing is a red.

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
                                  [--suppress-family NAME] [--self-check]
Exit: 0 = no DIVERGE, no THREW, and no declared family at zero programs;
      1 = at least one of those.
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


# --- the declaration: the families this generator BELIEVES it has ------------
#
# This tuple is the tool's claim about itself, written where a reviewer can
# read it beside the arms it claims to reach.  Before it existed the family
# list lived only in the code paths, so a block that was never reached was
# INVISIBLE: §10.154 found two of them (SETOPS declared and never read; no
# string leaf in value_expr) by reading this file against the route's arm list,
# with 2500 generated programs reporting PASS behind them.
#
# `gen_program` registers a family here-in-declared only where it actually
# emits that family, main() counts the registrations, and `audit_families`
# turns a declared family with zero programs into a red line.  A family added
# to the code but not declared here is a red too (FFAMILY-UNDECLARED), because
# a declaration that can go stale is the same silence one level up.
FAMILIES = (
    "container-len",   # c = <arr|dict|set|str>; say len(c)
    "index-side",      # say c[0] / say c[k] on an arr, a str or a dict
    "growth",          # gw[k] = v past the end; the gap fills with nil
    "nest-alias",      # n[0][0], p/q aliasing, e["k"][1]
    "closure",         # func mk(n) { return x -> x + n }
    "loop",            # while i < k { acc = ... }
    "try-catch",       # try { say ... } catch (e) { ... }
    "set-ops",         # s = 1, 2, 3; say s - t   (SETOPS, all seven)
    "str-concat",      # say sp + 1               (§10.147's shape)
    "scalar-tail",     # the trailing `say <value_expr>` (unconditional)
)


def _hit(rng, p, name, suppressed):
    """One family's dice roll, with the suppression lever.

    rng.random() is drawn FIRST and unconditionally, so `--suppress-family`
    changes which block runs without turning the lever into a second, unnamed
    change to the roll order.  Suppressing a family still reshuffles the
    programs after it (its body draws its own randoms), and that is stated in
    the docstring rather than papered over: the reading this lever exists for
    is "a declared family produced zero programs is RED", not "the rest of the
    run is byte-identical".
    """
    return rng.random() < p and name not in suppressed


def audit_families(counts):
    """The declaration checked against the census: (silent, undeclared).

    `silent` is a declared family that produced no program at all -- the shape
    §10.154 had two of and nothing could see.  `undeclared` is a family the
    generator registered that FAMILIES does not name: a block added to the
    generator without being added to the declaration, which makes the claim
    above incomplete rather than wrong.
    """
    silent = [f for f in FAMILIES if counts.get(f, 0) == 0]
    undeclared = sorted(n for n in counts if n not in FAMILIES)
    return silent, undeclared


def verdict_of(diverge, threw, silent, undeclared):
    """One place decides PASS/FAIL, so the self-check measures the real rule."""
    if diverge or threw or silent or undeclared:
        return "FAIL"
    return "PASS"


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


def gen_program(rng, suppressed=()):
    """One program, plus the set of families this call actually EMITTED.

    The second return value is the registration the run counts.  A family is
    registered at its own block, not inferred from the text afterwards: if the
    block stops emitting, the registration stops with it, which is the whole
    point (see FAMILIES above).
    """
    lines, names = [], []
    emitted = set()
    # sometimes a container whose length is read back
    if _hit(rng, 0.5, "container-len", suppressed):
        emitted.add("container-len")
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
        if kind != "set":
            emitted.add("index-side")   # arr, str and dict each read a subscript
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
    # GROWTH, the shape a RETIRED refusal used to make unreachable (§10.151):
    # a write past the end grows the array (src/vm/vm.c:3721-3724) and the gap
    # fills with nil.  While `prescan_constant_subscripts` refused these shapes
    # at translate time the generator could not say them, which is exactly the
    # silence this fuzzer exists to break -- so the family is written down and
    # given its own seed rather than left to the dice.
    if _hit(rng, 0.4, "growth", suppressed):
        emitted.add("growth")
        k = rng.randint(1, 5)
        lines.append("gw = [" + ", ".join(rng.choice(INTS) for _ in range(rng.randint(1, 3))) + "]")
        lines.append("gw[%d] = %s" % (k, rng.choice(INTS)))
        lines.append("say gw")
        lines.append("say len(gw)")
        lines.append("say gw[%d]" % k)
        lines.append("gx = gw")
        lines.append("say gx[%d]" % k)
        lines.append("say gw[%d]" % (k + rng.randint(0, 2)))
    # NESTED CONTAINERS AND ALIASES.  Both are places where a route that models
    # a container as a REGION IN A POOL can part company with a runtime that
    # models it as an OBJECT: a nested write has to reach THROUGH the outer
    # region, and a write through an alias has to be visible through the original
    # name.  Measured reachable in both runtimes: `n = [[1, 2], [3]]` /
    # `say n[1][0]`, `b = a` / `b[0] = 9` / `say a[0]` (both answer 9).
    if _hit(rng, 0.35, "nest-alias", suppressed):
        emitted.add("nest-alias")
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
    if _hit(rng, 0.35, "closure", suppressed):
        emitted.add("closure")
        lines.append("func mk(n) { return x -> x + n }")
        lines.append("f = mk(%s)" % rng.choice(INTS))
        lines.append("say f(%s)" % rng.choice(INTS))
    # a loop, so the answer depends on iteration
    if _hit(rng, 0.5, "loop", suppressed):
        emitted.add("loop")
        lines.append("acc = " + rng.choice(INTS))
        names.append("acc")
        lines.append("i = 0")
        names.append("i")
        lines.append("while i < %d {" % rng.randint(1, 6))
        lines.append("    acc = " + value_expr(rng, names))
        lines.append("    i = i + 1")
        lines.append("}")
    # a try/catch around an expression that may or may not throw
    if _hit(rng, 0.3, "try-catch", suppressed):
        emitted.add("try-catch")
        lines.append("try {")
        lines.append("    say " + value_expr(rng, names))
        lines.append("} catch (e) {")
        lines.append('    say "caught"')
        lines.append("}")
    # THE SET OPERATOR FAMILY, AND A STRING THAT REACHES `+`.  Both were
    # BLIND SPOTS of this generator rather than of the route: SETOPS was
    # declared here and never used, so not one program in 2500 ever put an
    # operator on a set; and value_expr has no leaf that returns a bare string,
    # so `say "a" + 1` -- the exact shape section 10.147 was written for --
    # could not be generated either.  Measured examples of the set family:
    # `s = 1, 2, 3` / `t = 2, 3` / `say s - t` prints set(1) (set_diff),
    # `say s * t` prints set(2) (set_intersect), and the interpreter answers
    # nil for a set beside a scalar.
    # SETOPS was declared and never used: no program this generator produced
    # ever put an operator on a set.  The route has arms for + - * and the
    # comparisons, so the family is legal; measured examples: `s = 1, 2, 3`
    # / `t = 2, 3` / `say s - t` prints set(1) (the interpreter's set_diff),
    # and `say s * t` prints set(2).
    if _hit(rng, 0.5, "set-ops", suppressed):
        emitted.add("set-ops")
        lines.append("sw = " + set_expr(rng, names))
        lines.append("sv = " + set_expr(rng, names))
        op = rng.choice(SETOPS)
        if op == "in":
            lines.append("say " + rng.choice(INTS) + " in sw")
        else:
            lines.append("say sw %s sv" % op)
        lines.append("say len(sw %s sv)" % op if op in ("+", "-", "*") else "say " + rng.choice(INTS))
    # A STRING REACHING +.  value_expr has no leaf that returns a bare string,
    # so `say "a" + 1` -- the shape section 10.147 was written for -- could
    # never appear in 2500 generated programs.
    if _hit(rng, 0.5, "str-concat", suppressed):
        emitted.add("str-concat")
        lines.append("sp = " + rng.choice(STRINGS))
        lines.append("say sp + " + rng.choice(INTS))
        lines.append("say " + rng.choice(INTS) + " + sp")
        lines.append("say sp + " + rng.choice(STRINGS))
    # UNCONDITIONAL, and that is what makes it the census's own canary: every
    # program carries a trailing scalar say, so `scalar-tail` reading 0 in the
    # census means the REGISTRATION stopped happening, not that the dice were
    # unlucky.  A family that can never be silent is worth one line precisely
    # because it tells the red for the other nine apart from a broken counter.
    emitted.add("scalar-tail")
    lines.append("say " + value_expr(rng, names))
    return "\n".join(lines) + "\n", emitted


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
    ap.add_argument("--suppress-family", action="append", default=[], metavar="NAME")
    ap.add_argument("--self-check", action="store_true")
    args = ap.parse_args()
    if args.self_check:
        return self_check()

    # A SUPPRESSION THAT NAMES NOTHING IS REFUSED, because the whole point of
    # the lever is that a reader can trust its red: `--suppress-family gwoth`
    # (typo) would otherwise suppress nothing and print a green run, which reads
    # exactly like "the control does not work".  The key set is closed here for
    # the same reason check_reconciliation.py closes its token keys.
    suppressed = set(args.suppress_family)
    unknown = sorted(suppressed - set(FAMILIES))
    if unknown:
        for name in unknown:
            print("FFAMILY-SUPPRESS-UNKNOWN|name=%s|why=not-in-FAMILIES|declared=%d"
                  % (name, len(FAMILIES)))
        print("IR-AOT-MIR-FUZZ-FAMILY-DECLARED|names=%s" % ",".join(FAMILIES))
        return 1

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
    family_counts = {name: 0 for name in FAMILIES}
    for n in range(args.count):
        src, emitted = gen_program(rng, suppressed)
        for name in emitted:
            family_counts[name] = family_counts.get(name, 0) + 1
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

    # THE FAMILY CENSUS, and the red it exists for.  A declared family at zero
    # programs is the silence §10.154 could only find by reading the generator
    # against the route's arm list: the run itself said PASS for 2500 programs
    # while two families were dead.  It says RED now, and it says it in a line
    # a machine can grep.
    silent, undeclared = audit_families(family_counts)
    for name in silent:
        findings.append("FFAMILY-SILENT|family=%s|programs=0|of=%d" % (name, args.count))
    for name in undeclared:
        findings.append("FFAMILY-UNDECLARED|family=%s|programs=%d|why=not-in-FAMILIES"
                        % (name, family_counts[name]))
    for f in findings:
        print(f)
    for name in FAMILIES:
        print("IR-AOT-MIR-FUZZ-FAMILY|name=%s|programs=%d|of=%d|%s"
              % (name, family_counts[name], args.count,
                 "emitted" if family_counts[name] else "SILENT"))
    print("IR-AOT-MIR-FUZZ-FAMILIES|declared=%d|emitted=%d|silent=%d|undeclared=%d|suppressed=%s"
          % (len(FAMILIES), len(FAMILIES) - len(silent), len(silent), len(undeclared),
             ",".join(sorted(suppressed)) if suppressed else "-"))
    print("IR-AOT-MIR-FUZZ|seed=%d|count=%d|ran=%d|diverge=%d|threw=%d|unsound=%d"
          "|refused-by-name=%d|dir=%s"
          % (args.seed, args.count, ran, diverge, threw, unsound,
             skipped_refusals, work))
    verdict = verdict_of(diverge, threw, silent, undeclared)
    print("IR-AOT-MIR-FUZZ-VERDICT|%s" % verdict)
    return 0 if verdict == "PASS" else 1


def self_check():
    """The comparator's own controls: it must be able to say DIFFER.

    A fuzzer whose comparator never reports a difference passes on any tree.
    Three cells: identical payloads are clean; one byte apart is a difference;
    and the engine's own notices are filtered (a line starting with '[' is not
    the program's output).

    THEN THE FAMILY AUDIT'S OWN CONTROL, in BOTH directions, driven through the
    real generator rather than a hand-made counts dict -- a census that only
    ever gets to say "all ten emitted" is the silence §10.156 exists to remove,
    so the second half of the control suppresses one family and requires the
    audit to name exactly that one.  `checks` therefore counts comparator cells
    and family cells together.
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

    def census(count, suppressed):
        r = random.Random(7)
        c = {f: 0 for f in FAMILIES}
        for _ in range(count):
            _src, emitted = gen_program(r, suppressed)
            for name in emitted:
                c[name] = c.get(name, 0) + 1
        return c

    clean = census(20, set())
    silent_clean, undeclared_clean = audit_families(clean)
    silenced = census(20, {"growth"})
    silent_silenced, _u = audit_families(silenced)
    fam_cells = [
        ("families-all-emitted", silent_clean == [] and undeclared_clean == []
         and verdict_of(0, 0, silent_clean, undeclared_clean) == "PASS",
         "silent=%s|undeclared=%s" % (silent_clean, undeclared_clean)),
        ("family-silenced-is-named", silent_silenced == ["growth"],
         "silent=%s" % (silent_silenced,)),
        ("silent-family-is-red", verdict_of(0, 0, ["growth"], []) == "FAIL",
         "verdict=%s" % verdict_of(0, 0, ["growth"], [])),
        ("undeclared-family-is-red", verdict_of(0, 0, [], ["ghost"]) == "FAIL",
         "verdict=%s" % verdict_of(0, 0, [], ["ghost"])),
        ("scalar-tail-is-the-canary", clean["scalar-tail"] == 20,
         "scalar-tail=%d|of=20" % clean["scalar-tail"]),
    ]
    for what, hit, detail in fam_cells:
        print("IR-AOT-MIR-FUZZ-SELFCHECK|what=%s|%s|%s"
              % (what, detail, "ok" if hit else "WRONG"))
        ok &= hit
    print("IR-AOT-MIR-FUZZ-SELFCHECK|declared=%s" % ",".join(FAMILIES))
    print("IR-AOT-MIR-FUZZ-SELFCHECK|checks=%d|verdict=%s"
          % (len(cells) + len(fam_cells), "PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
