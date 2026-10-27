#!/usr/bin/env python3
"""identifier-length: names longer than the compiler's 255-byte copy must be refused, not truncated.

The compiler copies every identifier through a 256-byte buffer.  A 256-byte name is cut to 255,
so the last byte disappears and two different globals collapse into one.  The engine answers rc=0
and a wrong value: a silent wrong answer, which is the worst failure shape there is.

The first case is the one that was red before the fix, and it states both halves of the red:
rc!=0 (the compiler refused) and the text "identifier too long" on stderr (it said why).  The
second case pins the boundary from the other side: 255 bytes still compiles, and the two names
still print 1 and 2 -- not 2 and 2.  The remaining cases cover the other copy sites (thread
name, function name, local stores and loads), because a test that only exercised assignment
would pass while five sites still truncated.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def run_engine(engine, src_path):
    return subprocess.run([engine, "--no-mods", src_path],
                          capture_output=True, text=True, timeout=120)


def case_globals(n):
    a = "g" * (n - 1) + "A"
    b = "g" * (n - 1) + "B"
    assert len(a) == n
    return ("%s = 1\n%s = 2\nsay \"A=\" + str(%s)\nsay \"B=\" + str(%s)\n" % (a, b, a, b))


def case_locals(n):
    a = "L" * (n - 1) + "A"
    b = "L" * (n - 1) + "B"
    return ("func f() {\n  %s = 1\n  %s = 2\n  say \"A=\" + str(%s)\n  say \"B=\" + str(%s)\n}\nf()\n"
            % (a, b, a, b))


def case_funcname(n):
    name = "F" * (n - 1) + "A"
    return "func %s() {\n  say 1\n}\nsay \"compiled\"\n" % name


def case_threadname(n):
    name = "T" * (n - 1) + "A"
    return "thread %s {\n  say 1\n}\nsay \"compiled\"\n" % name


CASES = [
    # The red-first pair.  Order matters: the refusal is the assertion this test
    # exists for, and the 255 case is the boundary that must not have moved.
    ("global-256", case_globals, 256, {"rc_nonzero": True, "err_has": "identifier too long"}),
    ("global-255", case_globals, 255, {"rc": 0, "out_has": ["A=1", "B=2"]}),
    ("global-300", case_globals, 300, {"rc_nonzero": True, "err_has": "identifier too long"}),
    ("local-256", case_locals, 256, {"rc_nonzero": True, "err_has": "identifier too long"}),
    ("local-255", case_locals, 255, {"rc": 0, "out_has": ["A=1", "B=2"]}),
    ("funcname-256", case_funcname, 256, {"rc_nonzero": True, "err_has": "identifier too long"}),
    ("threadname-256", case_threadname, 256, {"rc_nonzero": True, "err_has": "identifier too long"}),
]


def check(label, src, expect, engine, workdir):
    path = os.path.join(workdir, label + ".im")
    with open(path, "w") as f:
        f.write(src)
    proc = run_engine(engine, path)
    out = proc.stdout
    err = proc.stderr
    problems = []
    if expect.get("rc_nonzero"):
        if proc.returncode == 0:
            problems.append("rc=0 but the compiler should have refused the name")
    elif expect.get("rc") is not None:
        if proc.returncode != expect["rc"]:
            problems.append("rc=%d, expected %d" % (proc.returncode, expect["rc"]))
    for needle in expect.get("out_has", []):
        if needle not in out:
            problems.append("stdout is missing %r" % needle)
    if expect.get("err_has") and expect["err_has"] not in err:
        problems.append("stderr does not say %r (it said %r)" % (expect["err_has"], err.strip()[:120]))
    # the silent wrong answer, named explicitly: both names answered 2
    if expect.get("rc_nonzero") and proc.returncode == 0 and "A=2" in out:
        problems.append("silent wrong answer: the two names collapsed (A=2, B=2)")
    if problems:
        return False, "; ".join(problems)
    return True, "ok (rc=%d)" % proc.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True, help="engine executable")
    args = ap.parse_args()
    engine = os.path.abspath(args.bin)
    if not os.path.exists(engine):
        print("SKIP(no engine at %s)" % engine)
        return 2
    red = []
    green = []
    with tempfile.TemporaryDirectory(prefix="idlen-") as workdir:
        for label, factory, n, expect in CASES:
            ok, note = check(label, factory(n), expect, engine, workdir)
            (green if ok else red).append(label)
            state = "green" if ok else "RED"
            print("identifier-length: %-14s len=%-3d -- %s: %s" % (label, n, state, note))
    print("identifier-length: cases=%d red=%d green=%d%s"
          % (len(CASES), len(red), len(green), (" -- " + ",".join(red)) if red else ""))
    return 0 if not red else 1


if __name__ == "__main__":
    sys.exit(main())
