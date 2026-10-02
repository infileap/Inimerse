#!/usr/bin/env python3
"""Does the route-A native backend compute what the interpreter computes?

Two properties are checked, and they are different properties:

  EQUIVALENCE  For programs inside the numeric subset, the native executable's
               stdout must equal the interpreter's stdout, byte for byte.  The
               interpreter is the oracle; there are no hand-written expected
               values to drift out of date.

  REFUSAL      For programs outside the subset the translator must FAIL with a
               message rather than emit code that quietly disagrees.  A wrong
               native binary is worse than no native binary, so every
               out-of-subset case here asserts a non-zero exit from
               `aot-native translate`.

Prerequisites:
    cmake --build build -j4            # builds build/inimerse and build/aot-native

Usage:
    python3 tools/aot_native.test.py [--build build] [-v]
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# ------------------------------------------------------------------ corpus
#
# Int/bool printing only.  The interpreter's float formatter is not
# reproducible (it prints 1e-20 as "0." and mixes 6- and 7-digit precision),
# so printed floats are a documented divergence and stay out of this corpus
# rather than being asserted around.
EQUIVALENCE = [
    # -- integer arithmetic and precedence
    ("int_add",            "say 1 + 2\n", "3\n"),
    ("int_precedence",     "say 1 + 2 * 3\n", "7\n"),
    ("int_paren",          "say (1 + 2) * 3\n", "9\n"),
    ("int_sub_negative",   "say 5 - 10\n", "-5\n"),
    ("int_unary_minus",    "say -3 + 1\n", "-2\n"),
    ("int_div_exact",      "say 4 / 2\n", "2\n"),
    ("int_mod",            "say 7 % 3\n", "1\n"),
    ("int_mod_negative",   "say -7 % 3\n", "-1\n"),
    # First step of the classic LCG.  The literal 2147483648 exceeds INT32_MAX
    # and the interpreter promotes it to float (warning on stderr, which this
    # harness ignores), but the first step still fits 2^53 exactly, so both
    # sides agree.  The SECOND step is where they part — see DIVERGENCE.
    ("int_lcg_first_step",
     "x = 1\nx = (x*1103515245+12345) % 2147483648\nsay x\n", "1103527590\n"),
    # -- comparisons produce bools, printed as words
    ("cmp_lt",             "say 1 < 2\n", "true\n"),
    ("cmp_gt_false",       "say 3 > 4\n", "false\n"),
    ("cmp_le",             "say 2 <= 2\n", "true\n"),
    ("cmp_ge",             "say 2 >= 3\n", "false\n"),
    ("cmp_eq",             "say 2 == 2\n", "true\n"),
    ("cmp_ne",             "say 2 != 2\n", "false\n"),
    # -- chained comparison: a < b < c means (a<b) and (b<c)
    ("chain_true",         "say 1 < 2 < 3\n", "true\n"),
    ("chain_false",        "say 1 < 3 < 2\n", "false\n"),
    # -- logic, including short-circuit through C's && and ||
    ("bool_and",           "say true and false\n", "false\n"),
    ("bool_or",            "say true or false\n", "true\n"),
    ("bool_not",           "say not true\n", "false\n"),
    ("bool_literal",       "say true\n", "true\n"),
    # -- variables, reassignment
    ("assign_rebind",      "x = 1\nx = x + 5\nsay x\n", "6\n"),
    ("assign_from_expr",   "a = 3\nb = a * a\nsay b\n", "9\n"),
    # -- if / else
    ("if_taken",           "if 1 < 2 { say 10 }\n", "10\n"),
    ("if_skipped",         "if 2 < 1 { say 10 }\n", ""),
    ("if_else_then",       "if 1 < 2 { say 1 } else { say 2 }\n", "1\n"),
    ("if_else_else",       "if 2 < 1 { say 1 } else { say 2 }\n", "2\n"),
    ("if_nested",          "x = 5\nif x > 0 { if x > 3 { say 99 } }\n", "99\n"),
    # -- while
    ("while_count",        "i = 0\nwhile i < 5 { i = i + 1 }\nsay i\n", "5\n"),
    ("while_sum",          "s = 0\ni = 1\nwhile i <= 10 { s = s + i\ni = i + 1 }\nsay s\n", "55\n"),
    ("while_false_at_once", "i = 0\nwhile i < 0 { i = i + 1 }\nsay i\n", "0\n"),
    ("while_break",        "i = 0\nwhile i < 10 { if i == 3 { break }\ni = i + 1 }\nsay i\n", "3\n"),
    # -- repeat
    ("repeat_count",       "n = 0\nrepeat 4 { n = n + 1 }\nsay n\n", "4\n"),
    ("repeat_zero",        "n = 7\nrepeat 0 { n = n + 1 }\nsay n\n", "7\n"),
    ("repeat_break",       "n = 0\nrepeat 10 { if n == 2 { break }\nn = n + 1 }\nsay n\n", "2\n"),
    # -- functions
    ("func_noargs",        "func five() { return 5 }\nsay five()\n", "5\n"),
    ("func_one_arg",       "func dbl(x) { return x * 2 }\nsay dbl(21)\n", "42\n"),
    ("func_two_args",      "func add(a, b) { return a + b }\nsay add(3, 4)\n", "7\n"),
    ("func_closure_free",  "func sq(x) { return x * x }\nsay sq(sq(2))\n", "16\n"),
    ("func_mutual_order",  "func first(x) { return second(x) + 1 }\nfunc second(x) { return x * 2 }\nsay first(10)\n", "21\n"),
    ("func_recursion",     "func fact(n) { if n <= 1 { return 1 }\nreturn n * fact(n - 1) }\nsay fact(10)\n", "3628800\n"),
    ("func_loop_body",     "func total(n) { s = 0\ni = 0\nwhile i < n { s = s + i\ni = i + 1 }\nreturn s }\nsay total(100)\n", "4950\n"),
    # -- globals ("global" is a bare declaration; the value is assigned after)
    ("global_decl",        "global g\ng = 3\nsay g\n", "3\n"),
    ("global_assigned",    "global g\ng = 8\nsay g\n", "8\n"),
    ("global_read_from_func", "global g\ng = 2\nfunc peek() { return g }\nsay peek()\n", "2\n"),
    # -- multi-line program with several statements
    ("mixed_program",      "func f(n) { if n % 2 == 0 { return n / 2 }\nreturn n * 3 + 1 }\ni = 1\nwhile i <= 4 { say f(i)\ni = i + 1 }\n",
                           "4\n1\n10\n2\n"),
]

# ------------------------------------------------------------------ known divergences
#
# Programs the backend translates but does NOT reproduce.  These are pinned,
# not hidden: both the interpreter's output and the native output are asserted
# exactly, so the gap cannot silently widen and *fixing* one of them makes this
# test fail loudly and demand a promotion into EQUIVALENCE.
DIVERGENCE = [
    # The interpreter returns nil when a function falls off the end; the tagged
    # NV value has no nil tag, so the codegen returns a zeroed NV.
    ("func_no_return",
     "func nothing() { x = 1 }\nsay nothing()\n",
     "nil\n", "0\n"),

    # Scoping: reading a global from inside a function sees the global, but
    # ASSIGNING to it inside a function does not write the global in the
    # interpreter (the name becomes local, so `g = g + 5` reads nil, and
    # nil + 5 is 5).  The codegen resolves the declared global and writes it.
    ("global_write_from_func",
     "global g\ng = 2\nfunc bump() { g = g + 5\nreturn g }\nsay bump()\nsay g\n",
     "5\n2\n", "7\n7\n"),

    # Literal promotion, once it actually bites.  `2147483648` is promoted to
    # float by the interpreter, so the LCG's `x*1103515245+12345` intermediate
    # is evaluated in double: from the second step on it exceeds 2^53, the low
    # bits are gone, and the modulo collapses to 0 (or 12345, depending on the
    # step).  The codegen keeps int64 and stays exact.  The first step agrees
    # and lives in EQUIVALENCE as `int_lcg_first_step`.
    ("lcg_float_promotion",
     "x = 1\nrepeat 2 { x = (x*1103515245+12345) % 2147483648 }\nsay x\n",
     "0\n", "377401575\n"),
]

# Each must be REFUSED by the translator, with a message, and must not leave a
# .c file behind.
REFUSAL = [
    ("string_value",       'say "hello"\n'),
    ("string_concat",      'say "a" + "b"\n'),
    ("list_literal",       "say [1, 2, 3]\n"),
    ("dict_literal",       "say {1: 2}\n"),
    ("for_range",          "for i = 1 to 3 { say i }\n"),
    ("member_access",      "say a.b\n"),
    ("index_access",       "say a[0]\n"),
    ("lambda",             "f = (x) => x + 1\nsay f(1)\n"),
    ("builtin_call",       "say len([1, 2])\n"),
    ("assign_to_member",   "a.b = 1\n"),
]


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(REPO / "build"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    build = Path(args.build)
    engine = build / "inimerse"
    translator = build / "aot-native"
    for p in (engine, translator):
        if not p.is_file():
            print(f"error: {p} not found — configure and build the project "
                  f"(cmake --build build), which now builds aot-native as a "
                  f"normal target", file=sys.stderr)
            return 2

    checks = 0
    failures = []

    with tempfile.TemporaryDirectory(prefix="aot-native-test-") as tmp:
        tmp = Path(tmp)

        # ---------------------------------------------------- equivalence
        for name, source, expect in EQUIVALENCE:
            src = tmp / f"{name}.im"
            src.write_text(source, encoding="utf-8")

            got = run([str(engine), "--no-mods", str(src)])
            if got.returncode != 0:
                failures.append(f"{name}: interpreter failed rc={got.returncode} "
                                f"{got.stderr.strip()}")
                continue
            if expect is not None and got.stdout != expect:
                failures.append(f"{name}: corpus expectation wrong — interpreter "
                                f"printed {got.stdout!r}, corpus says {expect!r}")
                continue

            cfile = tmp / f"{name}.c"
            tr = run([str(translator), "translate", str(src), str(cfile)])
            if tr.returncode != 0:
                failures.append(f"{name}: translator refused an in-subset program: "
                                f"{tr.stderr.strip()}")
                continue

            exe = tmp / f"{name}.native"
            cc = run([args.cc, "-O2", "-o", str(exe), str(cfile)])
            if cc.returncode != 0:
                failures.append(f"{name}: cc failed: {cc.stderr.strip()[:400]}")
                continue

            nat = run([str(exe)])
            if nat.returncode != 0:
                failures.append(f"{name}: native binary exited {nat.returncode}")
                continue

            checks += 1
            if nat.stdout != got.stdout:
                failures.append(f"{name}: OUTPUT DIFFERS\n"
                                f"    interpreter: {got.stdout!r}\n"
                                f"    native     : {nat.stdout!r}\n"
                                f"    source     : {source!r}")

        # ------------------------------------------- pinned known divergences
        for name, source, want_interp, want_native in DIVERGENCE:
            src = tmp / f"{name}.im"
            src.write_text(source, encoding="utf-8")
            cfile = tmp / f"{name}.c"
            exe = tmp / f"{name}.native"

            got = run([str(engine), "--no-mods", str(src)])
            tr = run([str(translator), "translate", str(src), str(cfile)])
            if tr.returncode != 0:
                failures.append(f"{name}: translator refused a divergence case: "
                                f"{tr.stderr.strip()}")
                continue
            cc = run([args.cc, "-O2", "-o", str(exe), str(cfile)])
            if cc.returncode != 0:
                failures.append(f"{name}: cc failed: {cc.stderr.strip()[:400]}")
                continue
            nat = run([str(exe)])

            checks += 1
            if got.stdout != want_interp:
                failures.append(f"{name}: interpreter moved from {want_interp!r} "
                                f"to {got.stdout!r} — re-check this divergence")
            if nat.stdout != want_native:
                failures.append(f"{name}: native moved from {want_native!r} to "
                                f"{nat.stdout!r} — if it now matches the "
                                f"interpreter, promote this case to EQUIVALENCE")

        # -------------------------------------------------------- refusal
        for name, source in REFUSAL:
            src = tmp / f"neg_{name}.im"
            src.write_text(source, encoding="utf-8")
            cfile = tmp / f"neg_{name}.c"
            tr = run([str(translator), "translate", str(src), str(cfile)])
            checks += 1
            if tr.returncode == 0:
                failures.append(f"{name}: translator ACCEPTED an out-of-subset "
                                f"program ({source!r}) — it must refuse")
            elif cfile.exists():
                failures.append(f"{name}: translator refused but still wrote {cfile}")
            elif not tr.stderr.strip():
                failures.append(f"{name}: refused without a message on stderr")

    total = len(EQUIVALENCE) + len(DIVERGENCE) + len(REFUSAL)
    for f in failures:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"aot_native.test: {total} cases ({len(EQUIVALENCE)} equivalence, "
          f"{len(DIVERGENCE)} pinned divergences, {len(REFUSAL)} refusal), "
          f"{len(failures)} failures")
    if args.verbose:
        print(f"  successful comparisons: {checks - len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
