#!/usr/bin/env python3
"""Does the route-A native backend compute what the interpreter computes?

Three properties are checked, and they are different properties:

  EQUIVALENCE  For programs inside the numeric subset, the native executable's
               stdout must equal the interpreter's stdout, byte for byte.  The
               interpreter is the oracle; there are no hand-written expected
               values to drift out of date.

  REFUSAL      For programs outside the subset the translator must FAIL with a
               message rather than emit code that quietly disagrees.  A wrong
               native binary is worse than no native binary, so every
               out-of-subset case here asserts a non-zero exit from
               `aot-native translate`.

  RUNTIME_ERROR  For programs that ARE inside the subset but must fail when they
               run, both sides must exit non-zero and name the SAME error kind.
               An integer that leaves int64, or an integer division by zero, is
               an ArithmeticVMError -- not a silent wraparound, and not a float
               infinity.

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
# Int, bool and float printing.  The backend used to print floats with plain
# %g, a lossy format that rounds differently, so printed floats stayed out of
# this corpus as a documented divergence.  nv_fmt_double now ports the
# interpreter's vts_double (src/vm/vm.c) exactly, and the float cases below
# pin that agreement — including the surprising ones (1e-20 prints "0",
# 0.9999999 carries to "1", and a negative value that rounds away prints "-0").
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
    # First step of the classic LCG.  The literal 2147483648 exceeds INT32_MAX;
    # it used to be promoted to float with a warning, and the SECOND step then
    # left 2^53 and stopped agreeing -- that pair was pinned in DIVERGENCE until
    # v3.1 kept the whole expression in int64.  Both steps agree now.
    ("int_lcg_first_step",
     "x = 1\nx = (x*1103515245+12345) % 2147483648\nsay x\n", "1103527590\n"),
    # -- float printing: vts_double prints an integral float as an integer, an
    # exactly representable value with all its digits, and anything else with up
    # to six decimals.  Each row below is a case a naive %g, a naive 6-decimal
    # truncation, or a formatter that drops the sign of a small negative gets
    # wrong.
    ("float_third",             "say 1.0 / 3.0\n", "0.333333\n"),
    ("float_two_thirds",        "say 2.0 / 3.0\n", "0.666667\n"),
    ("float_more_digits_kept",  "say 123456789.125\n", "123456789.125\n"),
    ("float_whole_prints_int",  "say 1.0\n", "1\n"),
    ("float_round_then_trim",   "say 0.1 + 0.2\n", "0.3\n"),
    ("float_mid_keeps_digits",  "say 2147483648.5\n", "2147483648.5\n"),
    ("float_place_value_kept",  "say 0.000001\n", "0.000001\n"),
    ("float_rounds_to_zero",    "say 1e-20\n", "0\n"),
    ("float_carry_into_int",    "say 0.9999999\n", "1\n"),
    ("float_negative_sign",     "say 0.0 - 0.5\n", "-0.5\n"),
    ("float_negative_tiny",     "say 0.0 - 1e-20\n", "-0\n"),
    ("float_small_negative",    "say 0.0 - 1.0 / 3.0\n", "-0.333333\n"),
    ("float_big_scientific",    "say 1e20\n", "1e+20\n"),
    ("float_big_17_digits",
     "say 12345678901234567890.0\n", "1.2345678901234567e+19\n"),
    # -- the %.17g region: a tie rounds half-to-even, not to the larger n
    ("float_g17_tie",        "say 1.0000000000000002e15\n", "1000000000000000.2\n"),
    ("float_g17_tie2",       "say 2000000000000000.25\n", "2000000000000000.2\n"),
    ("float_g17_tie_neg",    "say 0.0 - 1.0000000000000002e15\n", "-1000000000000000.2\n"),
    ("float_g17_no_tie",     "say 1500000000000000.5\n", "1500000000000000.5\n"),
    ("float_g17_integral",   "say 1.5e15\n", "1500000000000000\n"),
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
    # -- v3.1 integer width (docs/AUDIT.md §1.14)
    #
    # Value's integer slot is int64 and an integer expression no longer degrades
    # to double when it leaves int32.  Every row below disagreed with the native
    # backend before that change: they are the 9 fuzz divergences, reduced to
    # their minimal forms.
    ("int_lcg_second_step",
     "x = 1\nrepeat 2 { x = (x*1103515245+12345) % 2147483648 }\nsay x\n", "377401575\n"),
    ("i64_literal",        "say 2147483648\n", "2147483648\n"),
    ("i64_literal_2p53",   "say 9007199254740993\n", "9007199254740993\n"),
    ("i64_add",            "say 9007199254740993 + 1\n", "9007199254740994\n"),
    ("i64_sub",            "say 9007199254740993 - 1\n", "9007199254740992\n"),
    ("i64_mul",            "say 4294967296 * 3\n", "12884901888\n"),
    ("i64_mul_near_max",   "say 3037000499 * 3037000499\n", "9223372030926249001\n"),
    ("i64_mod",            "say 9007199254740993 % 31\n", "9\n"),
    # An exact integer division stays an integer; only a remainder makes it float.
    # The interpreter never implemented that rule, so the AOT backend was the odd
    # one out until both were made to mirror each other.
    ("i64_div_exact",      "say 9007199254740993 / 1\n", "9007199254740993\n"),
    ("i64_div_remainder",  "say 7 / 2\n", "3.5\n"),
    # A bool is an integer-valued operand: it must not drag the other side onto
    # the double path (`true * 9007199254740993` used to print ...992 here).
    ("i64_bool_add",       "say true + 9007199254740993\n", "9007199254740994\n"),
    ("i64_bool_mul",       "say 9007199254740993 * true\n", "9007199254740993\n"),
    # Comparisons are exact inside the integer domain and promote only across
    # types; the AOT backend used to compare two ints through double, so
    # 9007199254740993 == 9007199254740992 answered true there.
    ("i64_cmp_gt",         "say 9007199254740993 > 9007199254740992\n", "true\n"),
    ("i64_cmp_eq_nbr",     "say 9007199254740993 == 9007199254740992\n", "false\n"),
    ("i64_cmp_eq_mixed",   "say 9007199254740993 == 9007199254740993.0\n", "true\n"),
    ("i64_cmp_bool_float", "say true == 1.5\n", "false\n"),
    # int64 arguments survive the call boundary (push_int took an `int`).
    ("i64_arg_return",     "func f(a) { return a }\nsay f(9007199254740993)\n", "9007199254740993\n"),
    ("i64_arg_arith",      "func f(a) { return a + 0 }\nsay f(9007199254740993)\n", "9007199254740993\n"),
    # A logical operator's result register must survive the enclosing
    # expression's right operand.  alloc_reg() hands out next_register, so taking
    # the release watermark before allocating `result` freed `result` itself and
    # the right operand reused it: `(a or false) % 31` became MOD r2, r2, r2 and
    # printed 31 % 31 = 0.  Only a variable left operand hit it.
    ("or_result_not_clobbered",
     "func f(a) { return ((a or false) + 31) }\nsay f(5)\n", "32\n"),
    ("or_result_mod",      "func f(a) { return ((a or false) % 31) }\nsay f(5)\n", "1\n"),
    ("and_result_mod",     "func f(a) { return ((a and true) % 31) }\nsay f(5)\n", "1\n"),
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

    # O2 -- "an integer silently degrades to double" -- is CLOSED as of v3.1: the
    # interpreter keeps int64 now, so the LCG's second step agrees with the codegen
    # and moved into EQUIVALENCE as `int_lcg_second_step`.  It lived here as
    # `lcg_float_promotion` (interpreter 377401600, native 377401575) and the
    # harness flagged the promotion exactly as this list is designed to.
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

# Each must FAIL AT RUNTIME, identically on both sides.  An integer that leaves
# int64, or an integer division by zero, is an ArithmeticVMError
# (docs/archive/ROADMAP_CASE_TYPES_V04.md:44) -- not a silent wraparound, and not
# a float infinity.  Both backends must exit non-zero and name the same kind.
RUNTIME_ERROR = [
    ("i64_add_overflow",  "say 9223372036854775807 + 1\n",             "numeric_overflow"),
    ("i64_sub_overflow",  "x = 9223372036854775807\nsay 0 - x - 2\n",  "numeric_overflow"),
    ("i64_mul_overflow",  "say 9223372036854775807 * 2\n",             "numeric_overflow"),
    ("i64_neg_overflow",  "x = 0 - 9223372036854775807 - 1\nsay 0 - x\n", "numeric_overflow"),
    # Past int32 the operand used to be demoted to float, which skipped the
    # integer zero guard entirely and answered `inf`.
    ("i64_div_by_zero",   "say 2147483648 / 0\n",                      "division_by_zero"),
    ("i64_mod_by_zero",   "say 2147483648 % 0\n",                      "division_by_zero"),
]


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(REPO / "build"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    build = Path(args.build)
    engine = build / ("inimerse.exe" if os.name == "nt" else "inimerse")
    translator = build / ("aot-native.exe" if os.name == "nt" else "aot-native")
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

        # ----------------------------------------------------- runtime errors
        for name, source, kind in RUNTIME_ERROR:
            src = tmp / f"err_{name}.im"
            src.write_text(source, encoding="utf-8")
            cfile = tmp / f"err_{name}.c"
            exe = tmp / f"err_{name}.native"

            got = run([str(engine), "--no-mods", str(src)])
            tr = run([str(translator), "translate", str(src), str(cfile)])
            if tr.returncode != 0:
                failures.append(f"{name}: translator refused an in-subset program: "
                                f"{tr.stderr.strip()}")
                continue
            cc = run([args.cc, "-O2", "-o", str(exe), str(cfile)])
            if cc.returncode != 0:
                failures.append(f"{name}: cc failed: {cc.stderr.strip()[:400]}")
                continue
            nat = run([str(exe)])

            checks += 1
            want = f"uncaught: {kind}"
            for who, r in (("interpreter", got), ("native", nat)):
                if r.returncode == 0:
                    failures.append(f"{name}: {who} did NOT fail — expected {kind}, "
                                    f"printed {r.stdout!r}")
                elif want not in r.stderr:
                    failures.append(f"{name}: {who} failed with the wrong kind — "
                                    f"expected {want!r}, stderr was {r.stderr.strip()!r}")

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

    total = len(EQUIVALENCE) + len(DIVERGENCE) + len(RUNTIME_ERROR) + len(REFUSAL)
    for f in failures:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"aot_native.test: {total} cases ({len(EQUIVALENCE)} equivalence, "
          f"{len(DIVERGENCE)} pinned divergences, {len(RUNTIME_ERROR)} runtime errors, "
          f"{len(REFUSAL)} refusal), {len(failures)} failures")
    if args.verbose:
        print(f"  successful comparisons: {checks - len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
