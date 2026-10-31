#!/usr/bin/env python3
"""Does the second front end -- C generated from IrInsn -- agree with the first?

aot_native lowers an AST.  aot_mir lowers the MIR (src/compiler/mir.h) that the
compiler already builds and then throws away.  Both end in a C translation unit
that embeds the same value runtime, so the interesting question is not "does it
compile" but "on the programs the AST route accepts, does the MIR route reach
the same answers, and if the two files differ, can every difference be named?"

Four properties, checked in this order, because each one is the premise of the
next:

  ACCEPTED     aot-native accepted it, so the program is inside the numeric
               subset.  If aot-mir refuses such a program, that is reported by
               name (IR-AOT-MIR-REFUSED carries mir's own why= and n=), never
               swallowed: a silent refusal reads as coverage.
  EXECUTED     both native binaries print the same bytes and exit the same way
               as the interpreter.  The interpreter is the oracle; no expected
               output is written down here.
  CONSERVED    every file aot-mir wrote carries IR-MIR-CONSERVE with holds=yes:
               emitted + not-yet == insns, counted from the module's own
               instruction count and the emitter's own appends.  A file whose
               equation is red is a failure even if it compiled.
  NAMED DIFF   the two C files are compared byte for byte.  A difference must
               fall inside this closed set, and each reason is backed by a
               mechanical check on the files themselves:

                 identical                                no difference
                 mir-register-shape                       the byte-identical
                     prefix runs through the whole shared value runtime, and the
                     first difference lies in how containers and instructions
                     are spelled: the AST route has named locals and structured
                     statements, the MIR route has a flat NV v[] and labels.
                 mir-register-shape+mir-internal-globals  the same, and the two
                     global declaration sets differ, because the compiler keeps
                     globals of its own (a repeat counter, say) that no AST ever
                     names and the MIR faithfully carries.

               A difference that cannot be put in one of those buckets is a
               failure, not a finding: --self-check exercises exactly that path
               on synthetic pairs, including one whose shared prefix stops
               before the value runtime.

The negative control is the perturbed operand.  aot_mir dump lists the module's
instructions; the test picks LOADK_INT instructions in main, changes one operand
through mir_perturb(), and requires that the generated C text disappears from
the old bytes AND that at least one such change moves the program's stdout.  A
generator that ignores its operands passes every other check in this file.

Readings:
  IR-MIR-C|case=NAME|ast=accepted|mir=accepted|cc=ok|exec=same
  IR-MIR-C-SAME|case=NAME|bytes=N|reason=identical
  IR-MIR-C-DIFF|case=NAME|len=N|first-diff=line N|reason=...
  IR-MIR-REFUSED|case=NAME|why=...|not-yet=OP_X:count,...
  IR-MIR-EXEC-DIFF|case=NAME|side=mir|ast|interp|rc=..|stdout=...
  IR-MIR-PERTURB|case=NAME|func=F|idx=I|field=2|was=V|now=W|c-changed=yes|stdout-changed=yes
  IR-MIR-COVER|corpus=inline-cases:N|accepted=N|refused=N|differ=N|conserved=N
              |c-identical=N|c-diff=N|reasons=identical:N,mir-register-shape:N,...
  IR-MIR-VERDICT|PASS|FAIL|...

Exit 0 when every accepted program agrees, every generated file conserves, every
difference is in the closed set and the control fired; 1 when any of those is
false (each one named); 2 when a prerequisite is missing.

Usage:
    python3 tools/aot_mir.test.py [--build build] [--max N] [--all] [--self-check] [-v]
"""
import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tools" / "aot_native.test.py"

# The closed set.  Difference classification may return nothing else.
DIFF_REASONS = ("identical", "mir-register-shape", "mir-register-shape+mir-internal-globals")

# A byte-identical prefix only proves the two files share a runtime if it
# actually contains that runtime.  These markers are lines of kPreamble, handed
# to aot_mir through aot_native_preamble() so there is one copy of it.
PREAMBLE_MARKERS = ("nv_die_division_by_zero", "nv_fmt_double", "static void nv_say(NV a)")

GLOBAL_DECL = re.compile(r"^static NV (g_\w+) = \{ NV_INT, 0, 0 \};$")
CODEGEN = re.compile(r"^IR-MIR-CODEGEN[|](.*)$")
CONSERVE = re.compile(r"^IR-MIR-CONSERVE[|](.*)$")
NOTYET = re.compile(r"^IR-MIR-NOT-YET[|]op=(OP_[A-Z0-9_]+)[|]n=(\d+)$")
REFUSED = re.compile(r"^IR-MIR-REFUSED[|]why=([^|]*)[|]detail=(.*)$")
INSN = re.compile(r"^IR-MIR-INSN[|]func=(\d+)[|]idx=(\d+)[|]op=(OP_[A-Z0-9_]+)"
                  r"[|]r1=(-?\d+)[|]r2=(-?\d+)[|]r3=(-?\d+)[|]argc=(\d+)$")


class Unclassifiable(Exception):
    """A byte difference the closed set does not cover.  Never caught silently."""


def load_cases():
    spec = importlib.util.spec_from_file_location("aot_cases", CASES)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(mod)
    return mod


def head_sha():
    p = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                       capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else "<not a git tree>"


def run(cmd, cwd, timeout=300):
    try:
        d = subprocess.run([str(c) for c in cmd], cwd=str(cwd), capture_output=True,
                           text=True, timeout=timeout)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def first_diff_line(a, b):
    """1-based line number of the first line that differs, or 0 when the files
    are equal; when one is a prefix of the other, the first missing line."""
    la, lb = a.splitlines(), b.splitlines()
    n = 0
    while n < len(la) and n < len(lb) and la[n] == lb[n]:
        n += 1
    return n + 1 if (n < len(la) or n < len(lb)) else 0


def classify(ast_text, mir_text):
    """Return (reason, first-diff-line).  Raises Unclassifiable when the
    difference is not one of the closed set's reasons."""
    if ast_text == mir_text:
        return "identical", 0
    line = first_diff_line(ast_text, mir_text)
    prefix = "\n".join(ast_text.splitlines()[:line - 1])
    for marker in PREAMBLE_MARKERS:
        if marker not in prefix:
            raise Unclassifiable("the byte-identical prefix ends at line %d, before the shared "
                                 "runtime marker %r, so a difference there is not lowering shape"
                                 % (line, marker))
    ga = {GLOBAL_DECL.match(l).group(1) for l in ast_text.splitlines() if GLOBAL_DECL.match(l)}
    gb = {GLOBAL_DECL.match(l).group(1) for l in mir_text.splitlines() if GLOBAL_DECL.match(l)}
    if ga != gb:
        return "mir-register-shape+mir-internal-globals", line
    return "mir-register-shape", line


def parse_mir_out(text):
    got = {"codegen": None, "conserve": None, "notyet": [], "refused": None}
    for raw in text.splitlines():
        line = raw.strip()
        m = CODEGEN.match(line)
        if m:
            got["codegen"] = dict(kv.split("=", 1) for kv in m.group(1).split("|") if "=" in kv)
            continue
        m = CONSERVE.match(line)
        if m:
            got["conserve"] = dict(kv.split("=", 1) for kv in m.group(1).split("|") if "=" in kv)
            continue
        m = NOTYET.match(line)
        if m:
            got["notyet"].append((m.group(1), int(m.group(2))))
            continue
        m = REFUSED.match(line)
        if m:
            got["refused"] = (m.group(1), m.group(2))
    return got


def self_check(verbose):
    """The classifier, on pairs whose answer is known -- including one it must
    refuse to classify.  No build, no corpus."""
    head = "static void nv_die_division_by_zero(void) { }\nstatic void nv_fmt_double(void) { }\n"
    tail = "static void nv_say(NV a) { }\n"
    run = sys.stdout.write
    checks = 0

    def expect(name, a, b, want):
        nonlocal checks
        checks += 1
        try:
            got, _line = classify(a, b)
        except Unclassifiable as exc:
            got = "unclassifiable:" + str(exc)
        ok = got == want
        print("IR-MIR-SELFCHECK|what=%s|got=%s|want=%s|%s" % (name, got, want, "ok" if ok else "WRONG"))
        return ok

    ok = True
    ok &= expect("equal-files", head + tail + "int main(void) { return 0; }\n",
                 head + tail + "int main(void) { return 0; }\n", "identical")
    ok &= expect("body-shape", head + tail + "int main(void) { NV v_x = nv_int(1); }\n",
                 head + tail + "int main(void) { NV v[2]; NV v; }\n", "mir-register-shape")
    ok &= expect("extra-global",
                 head + tail + "static NV g_a = { NV_INT, 0, 0 };\nint main(void) { return 0; }\n",
                 head + tail + "static NV g_a = { NV_INT, 0, 0 };\nstatic NV g_b = { NV_INT, 0, 0 };\n"
                 + "int main(void) { return 0; }\n",
                 "mir-register-shape+mir-internal-globals")
    checks += 1
    try:
        classify("int main(void) { return 0; }\n", "int main(void) { return 0; }\n\n",
                 )
        print("IR-MIR-SELFCHECK|what=no-shared-runtime|got=classified|want=unclassifiable|WRONG")
        ok = False
    except Unclassifiable:
        print("IR-MIR-SELFCHECK|what=no-shared-runtime|got=unclassifiable|want=unclassifiable|ok")
    checks += 1
    try:
        classify(head + "static void nv_say(NV a) { x }\n",
                 head + "static void nv_say(NV a) { y }\n")
        print("IR-MIR-SELFCHECK|what=runtime-varies|got=classified|want=unclassifiable|WRONG")
        ok = False
    except Unclassifiable:
        print("IR-MIR-SELFCHECK|what=runtime-varies|got=unclassifiable|want=unclassifiable|ok")
    print("IR-MIR-SELFCHECK|checks=%d|verdict=%s" % (checks, "PASS" if ok else "FAIL"))
    return 0 if ok else 1


def perturb_case(mir_tool, workdir, name, src_path, base_c, verbose):
    """Change one LOADK_INT operand in main and require the generated C to move.

    Returns (tried, c_changed_failures, stdout_changes)."""
    rc, out, err = run([mir_tool, "dump", src_path], workdir)
    if rc != 0:
        return 0, 0, 0, ["dump failed for %s: %s" % (name, (err or out).strip()[:200])]
    picks = []
    for raw in out.splitlines():
        m = INSN.match(raw.strip())
        if not m:
            continue
        f, idx, op, r1, r2, r3, argc = (int(m.group(1)), int(m.group(2)), m.group(3),
                                        int(m.group(4)), int(m.group(5)), int(m.group(6)),
                                        int(m.group(7)))
        if f == 0 and op == "OP_LOADK_INT" and r2 != 2147483647:
            picks.append((idx, r2))
        if len(picks) >= 3:
            break
    tried = 0
    c_fail = 0
    stdout_changes = 0
    problems = []
    for idx, was in picks:
        tried += 1
        now = was + 1
        out_c = Path(workdir) / ("%s.p%d.c" % (name, idx))
        rc2, out2, err2 = run([mir_tool, "translate", "--perturb", "0", str(idx), "2", str(now),
                               src_path, out_c], workdir)
        if rc2 != 0:
            problems.append("perturb %s idx=%d was refused: %s" % (name, idx, (err2 or out2).strip()[:200]))
            continue
        try:
            new_c = out_c.read_text()
        except OSError as exc:
            problems.append("perturb %s idx=%d wrote no file: %s" % (name, idx, exc))
            continue
        c_changed = new_c != base_c
        if not c_changed:
            c_fail += 1
        exe = Path(workdir) / ("%s.p%d.exe" % (name, idx))
        rc3, _o3, err3 = run(["cc", "-O2", "-o", exe, out_c], workdir)
        stdout_changed = "no-file"
        if rc3 == 0:
            rc4, o4, _e4 = run([exe], workdir)
            stdout_changed = "yes" if o4 != BASE_STDOUT[0] else "no"
            if stdout_changed == "yes":
                stdout_changes += 1
        else:
            problems.append("perturb %s idx=%d did not compile: %s" % (name, idx, (err3 or "")[:200]))
        print("IR-MIR-PERTURB|case=%s|func=0|idx=%d|field=2|was=%d|now=%d|c-changed=%s|stdout-changed=%s"
              % (name, idx, was, now, "yes" if c_changed else "no", stdout_changed))
    return tried, c_fail, stdout_changes, problems


BASE_STDOUT = [None]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--tool", default="",
                    help="the aot-mir executable; the CTest registration passes "
                         "$<TARGET_FILE:aot-mir>, so the registration names the target it runs")
    ap.add_argument("--max", type=int, default=0, help="stop after N cases (0 = all)")
    ap.add_argument("--all", action="store_true", help="run the whole corpus (the default)")
    ap.add_argument("--perturb-cases", type=int, default=3,
                    help="how many accepted cases get the perturbed-operand control")
    ap.add_argument("--self-check", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_check:
        return self_check(args.verbose)

    build = (ROOT / args.build) if not os.path.isabs(args.build) else Path(args.build)
    interp = build / "inimerse"
    ast_tool = build / "aot-native"
    # --tool is what the CTest registration passes ($<TARGET_FILE:aot-mir>, an
    # absolute path).  A relative one is resolved the same way --build is, because
    # the cases run in a temp cwd of their own.
    if args.tool:
        mir_tool = ROOT / args.tool if not os.path.isabs(args.tool) else Path(args.tool)
    else:
        mir_tool = build / "aot-mir"
    for tool in (interp, ast_tool, mir_tool):
        if not tool.exists():
            print("aot_mir: REFUSED|missing %s -- build it first" % tool, file=sys.stderr)
            return 2

    mod = load_cases()
    cases = [(n, src) for n, src, _want in mod.EQUIVALENCE]
    if args.max:
        cases = cases[:args.max]

    workdir = tempfile.mkdtemp(prefix="aot_mir_")
    accepted = refused = differ = conserved = 0
    c_identical = c_diff = 0
    reasons = {r: 0 for r in DIFF_REASONS}
    problems = []
    unclassifiable = []
    perturbed = perturb_fail = stdout_changes = 0

    for name, src in cases:
        s = Path(workdir) / (name + ".im")
        s.write_text(src)
        ast_c = Path(workdir) / (name + ".ast.c")
        mir_c = Path(workdir) / (name + ".mir.c")

        rc_a, _oa, ea = run([ast_tool, "translate", s, ast_c], workdir)
        rc_m, om, em = run([mir_tool, "translate", s, mir_c], workdir)
        got = parse_mir_out(om)
        if rc_a != 0:
            problems.append("%s: the AST route refused a case from its own equivalence corpus: %s"
                            % (name, (ea or "").strip()[:200]))
            continue
        if rc_m != 0:
            refused += 1
            why = got["refused"][0] if got["refused"] else "no IR-MIR-REFUSED line"
            detail = got["refused"][1] if got["refused"] else (em or "").strip()[:120]
            notyet = ",".join("%s:%d" % (op, n) for op, n in got["notyet"])
            print("IR-MIR-REFUSED|case=%s|why=%s|detail=%s|not-yet=%s" % (name, why, detail, notyet or "-"))
            continue
        accepted += 1

        cons = got["conserve"] or {}
        if cons.get("holds") == "yes":
            conserved += 1
        else:
            problems.append("%s: IR-MIR-CONSERVE does not hold: %s" % (name, cons))

        rc_i, out_i, _ei = run([interp, "--no-mods", "run", s], workdir)
        BASE_STDOUT[0] = out_i
        exe_m = Path(workdir) / (name + ".mir.exe")
        exe_a = Path(workdir) / (name + ".ast.exe")
        rc_cm, _o, ecm = run(["cc", "-O2", "-o", exe_m, mir_c], workdir)
        rc_ca, _o2, eca = run(["cc", "-O2", "-o", exe_a, ast_c], workdir)
        if rc_cm != 0 or rc_ca != 0:
            problems.append("%s: cc failed (mir=%s ast=%s): %s %s"
                            % (name, rc_cm, rc_ca, (ecm or "")[:200], (eca or "")[:200]))
            continue
        rc_rm, out_m, _e = run([exe_m], workdir)
        rc_ra, out_a, _e2 = run([exe_a], workdir)
        same_exec = (out_m == out_i == out_a) and (rc_rm == rc_i == rc_ra)
        if not same_exec:
            differ += 1
            print("IR-MIR-EXEC-DIFF|case=%s|interp-rc=%s|mir-rc=%s|ast-rc=%s|interp-out=%r|mir-out=%r|ast-out=%r"
                  % (name, rc_i, rc_rm, rc_ra, out_i, out_m, out_a))
        print("IR-MIR-C|case=%s|ast=accepted|mir=accepted|cc=ok|exec=%s"
              % (name, "same" if same_exec else "diff"))

        ast_text = ast_c.read_text()
        mir_text = mir_c.read_text()
        try:
            reason, line = classify(ast_text, mir_text)
        except Unclassifiable as exc:
            unclassifiable.append("%s: %s" % (name, exc))
            continue
        reasons[reason] += 1
        if reason == "identical":
            c_identical += 1
            print("IR-MIR-C-SAME|case=%s|bytes=%d|reason=identical" % (name, len(mir_text)))
        else:
            c_diff += 1
            print("IR-MIR-C-DIFF|case=%s|len=%d|first-diff=line %d|reason=%s"
                  % (name, len(mir_text), line, reason))

        if perturbed < args.perturb_cases:
            t, cf, sc, probs = perturb_case(mir_tool, workdir, name, s, mir_text, args.verbose)
            perturbed += t
            perturb_fail += cf
            stdout_changes += sc
            problems.extend(probs)

    print("IR-MIR-COVER|corpus=inline-cases:%d|accepted=%d|refused=%d|differ=%d|conserved=%d"
          "|c-identical=%d|c-diff=%d|reasons=%s|perturbed=%d|perturb-c-changed-failures=%d"
          "|perturb-stdout-changes=%d"
          % (len(cases), accepted, refused, differ, conserved, c_identical, c_diff,
             ",".join("%s:%d" % (r, reasons[r]) for r in DIFF_REASONS),
             perturbed, perturb_fail, stdout_changes))
    for line in unclassifiable:
        print("IR-MIR-C-UNCLASSIFIED|" + line)
    for line in problems:
        print("IR-MIR-PROBLEM|" + line)

    ok = (differ == 0 and not problems and not unclassifiable and conserved == accepted
          and perturb_fail == 0 and stdout_changes > 0 and perturbed > 0)
    if c_identical == 0 and c_diff > 0:
        ok = ok  # a corpus where every file differs is not itself a failure
    print("IR-MIR-VERDICT|%s|tree=%s|accepted=%d|refused=%d|differ=%d|c-identical=%d|c-diff=%d"
          "|perturb-stdout-changes=%d"
          % ("PASS" if ok else "FAIL", head_sha(), accepted, refused, differ, c_identical, c_diff,
             stdout_changes))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
