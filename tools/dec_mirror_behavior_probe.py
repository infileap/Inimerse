#!/usr/bin/env python3
"""Three ways to run the same program: do they print the same bytes and exit the
same way?

dec_inimbc_outlet_probe.py compares artifacts ("this probe compares artifacts,
not exit paths").  That leaves the other half unmeasured: a program run by the
engine, the same program carried through the C compiler's INIMBC image, and the
same program carried through the self-hosted compiler's own INIMBC image can
disagree on stdout, on stderr, and on the exit code while every artifact stays
byte identical.  "prints two lines instead of one, rc 0" is the failure shape
this project keeps guarding against, so it needs a machine reading.

Routes for a program script S:
  src      <bin> S                                  the engine runs the source
  cimage   <bin> compile S x.inim ; <bin> x.inim    the C compiler's image runs
  bimage   <bin> selfhost/compiler.im --emit-inimbc <abs> S y.inim
           <bin> y.inim                             the bootstrap's image runs

Routes for the compiler's own command line on a target T (cases "emit-cli" and
"float-pool"):
  src      <bin> selfhost/compiler.im --emit-inimbc T out
  cimage   <bin> compile selfhost/compiler.im c.inim ; <bin> c.inim --emit-inimbc T out
  bimage   <bin> g.inim --emit-inimbc T out         g.inim = bootstrap image

Comparison is BYTE-level: a route's stdout is compared as bytes, so "40 lines
each but one byte apart" is red; line numbers appear only to say where the first
difference is.  Each case also prints the three routes' rc / stdout bytes /
stderr bytes plus the first three stdout lines and the first stderr line, so a
later reader can check a verdict without rerunning the case.

Self-check: the comparator is exercised on two deliberately injected differences
every run ("mutation"): one extra line, and the same number of lines with
different bytes.  A comparator that never fires on the second kind is
indistinguishable from one that compares lines only.

Usage:
  python3 tools/dec_mirror_behavior_probe.py [--bin build/inimerse] [--timeout 300]

Exit codes:
  0  three routes agree on every case
  1  at least one case diverges (the report names the route, the line, the bytes)
  2  the engine binary is missing or the comparator failed its own mutation test
     -- NOT a silent skip.
"""

from __future__ import annotations

import argparse
import difflib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# (name, source text).  The source text is what a user would write; \n inside it
# is the two characters a string literal in the source shows.
CASES = [
    ("stop-all", 'say "before"\nstop all\nsay "after"\n'),
    ("escaped-newline", 'say "a\\nb"\n'),
    ("escaped-tab", 'say "x\\ty"\n'),
    ("escaped-backslash", 'say "p\\\\q"\n'),
    ("escaped-quote", 'say "s\\"t"\n'),
    ("escaped-nul", 'say "u\\0v"\n'),
    ("escape-unsupported", 'say "w\\z"\n'),
]

# (name, target source text) for the compiler's own command line.  emit-cli runs
# it on the compiler itself; float-pool runs it on a program the bootstrap cannot
# encode yet (no double pool), which must fail loudly on every route and print
# the same line doing it.
CLI_CASES = [
    ("emit-cli", None),
    ("float-pool", 'say 3.5\n'),
]


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, b"", b"<timeout>"


def decode(b):
    return b.decode("utf-8", "backslashreplace")


def first_divergence(a, b):
    """1-based line number of the first differing line, 0 when equal."""
    la, lb = decode(a).splitlines(), decode(b).splitlines()
    for i in range(max(len(la), len(lb))):
        if i >= len(la) or i >= len(lb) or la[i] != lb[i]:
            return i + 1
    return 0


def compare(label, routes):
    """routes: list of (name, (rc, out, err)).  Returns a list of findings."""
    findings = []
    base_name, (base_rc, base_out, base_err) = routes[0]
    for name, (rc, out, err) in routes[1:]:
        if rc != base_rc:
            findings.append("%s: rc %d, %s: rc %d" % (base_name, base_rc, name, rc))
        if out != base_out:
            n = first_divergence(base_out, out)
            findings.append(
                "%s: stdout differs at line %d (%d vs %d lines, %d vs %d bytes)"
                % (name, n, len(decode(base_out).splitlines()),
                   len(decode(out).splitlines()), len(base_out), len(out)))
            for line in difflib.unified_diff(
                    decode(base_out).splitlines(), decode(out).splitlines(),
                    fromfile=base_name, tofile=name, lineterm="", n=1):
                findings.append("  " + line)
        if err != base_err:
            findings.append("%s: stderr differs (%d vs %d bytes, first line %r vs %r)"
                            % (name, len(base_err), len(err),
                               decode(base_err).splitlines()[:1],
                               decode(err).splitlines()[:1]))
    return findings


def detail(routes):
    """One line per route: rc, byte counts, first three stdout lines, first
    stderr line.  This is what a later reader checks instead of rerunning."""
    out = []
    for name, (rc, so, se) in routes:
        head = decode(so).splitlines()[:3]
        err = decode(se).splitlines()[:1]
        out.append("  %-6s rc=%-3d stdout=%dB stderr=%dB head=%r err=%r"
                   % (name, rc, len(so), len(se), head, err))
    return out


def program_routes(bin_abs, compiler_abs, src_abs, scratch, timeout):
    """(name, result) for the three routes that run a program script."""
    x = os.path.join(scratch, "c.inim")
    y = os.path.join(scratch, "b.inim")
    src = run([bin_abs, src_abs], timeout)
    rc_c, out_c, err_c = run([bin_abs, "compile", src_abs, x], timeout)
    cimage = run([bin_abs, x], timeout) if rc_c == 0 else (rc_c, out_c, err_c)
    rc_b, out_b, err_b = run(
        [bin_abs, compiler_abs, "--emit-inimbc", src_abs, y], timeout)
    bimage = run([bin_abs, y], timeout) if rc_b == 0 else (rc_b, out_b, err_b)
    return [("src", src), ("cimage", cimage), ("bimage", bimage)]


def cli_routes(bin_abs, compiler_abs, target_abs, scratch, timeout):
    """(name, result) for the compiler's own command line on --emit-inimbc."""
    out = os.path.join(scratch, "out.inim")  # one path for every route: the
    # printed path is part of stdout, and three different paths would read as a
    # divergence that is the probe's own doing.
    src = run([bin_abs, compiler_abs, "--emit-inimbc", target_abs, out], timeout)
    c = os.path.join(scratch, "cc.inim")
    rc_c, out_c, err_c = run([bin_abs, "compile", compiler_abs, c], timeout)
    if rc_c == 0:
        cimage = run([bin_abs, c, "--emit-inimbc", target_abs, out], timeout)
    else:
        cimage = (rc_c, out_c, err_c)
    g = os.path.join(scratch, "gen1.inim")
    rc_g, out_g, err_g = run(
        [bin_abs, compiler_abs, "--emit-inimbc", compiler_abs, g], timeout)
    if rc_g == 0:
        bimage = run([bin_abs, g, "--emit-inimbc", target_abs, out], timeout)
    else:
        bimage = (rc_g, out_g, err_g)
    return [("src", src), ("cimage", cimage), ("bimage", bimage)]


def mutation_selfcheck():
    """The comparator must fire on injected, known differences -- both an extra
    line and the same line count with different bytes."""
    a = (0, b"one\n", b"")
    extra = (0, b"one\ntwo\n", b"")
    findings = compare("mutation", [("left", a), ("right", extra)])
    if not findings:
        print("mirror: comparator failed its own mutation test (an extra line "
              "went unreported)")
        return False
    if "stdout differs at line 2" not in " ".join(findings):
        print("mirror: mutation test fired but named the wrong line: %r" % findings)
        return False
    same_shape = (0, b"one\n", b"")  # identical, must stay silent
    if compare("mutation", [("left", a), ("right", same_shape)]):
        print("mirror: comparator reported equal bytes as unequal")
        return False
    bytewise = (0, b"ONe\n", b"")
    findings = compare("mutation", [("left", a), ("right", bytewise)])
    if not findings or "bytes" not in " ".join(findings):
        print("mirror: comparator compares lines, not bytes: %r" % findings)
        return False
    print("mirror: mutation ok -- extra line flagged at line 2, same line count "
          "with different bytes flagged too")
    return True


def report(name, routes):
    """Print the verdict and the per-route detail.  Returns True when red."""
    findings = compare(name, routes)
    if findings:
        print("case %s: DIVERGE" % name)
        for f in findings:
            print("  " + f)
        for line in detail(routes):
            print(line)
        return True
    print("case %s: agree (rc %d, %d bytes stdout, byte-identical on all routes)"
          % (name, routes[0][1][0], len(routes[0][1][1])))
    for line in detail(routes):
        print(line)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "inimerse"))
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    bin_abs = os.path.abspath(args.bin)
    if not os.access(bin_abs, os.X_OK):
        print("SKIP(no executable engine at %s) -- three routes need a runner"
              % bin_abs)
        return 2

    if not mutation_selfcheck():
        return 2

    compiler_abs = os.path.join(ROOT, "selfhost", "compiler.im")
    scratch = tempfile.mkdtemp(prefix="mirror-")
    red = 0
    total = 0
    try:
        for name, text in CASES:
            src_abs = os.path.join(scratch, name + ".im")
            with open(src_abs, "w", encoding="utf-8") as f:
                f.write(text)
            routes = program_routes(bin_abs, compiler_abs, src_abs, scratch,
                                    args.timeout)
            total += 1
            if report(name, routes):
                red += 1

        for name, text in CLI_CASES:
            target = compiler_abs
            if text is not None:
                target = os.path.join(scratch, name + ".im")
                with open(target, "w", encoding="utf-8") as f:
                    f.write(text)
            routes = cli_routes(bin_abs, compiler_abs, target, scratch,
                                args.timeout)
            total += 1
            if report(name, routes):
                red += 1
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print("mirror: cases=%d red=%d green=%d" % (total, red, total - red))
    return 0 if red == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
