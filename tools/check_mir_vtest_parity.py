#!/usr/bin/env python3
"""check_mir_vtest_parity.py -- the corpus the MIR route never compared.

Why this exists (docs/STATUS.md 10.251):

`tools/aot_mir_cover.test.py` compares the MIR route against the interpreter
byte for byte, and it does it well: 403 accepted cases, 403 same, checked on
every gate run.  Its corpus is two lists of INLINE STRINGS -- `EQUIVALENCE`
from `aot_native.test.py` and `MIR_ONLY` from `aot_mir.test.py`.

The `vtest/` directory is not in either list.  It is 81 programs of which 41
translate through the MIR route, and **not one of them is compared by
anything**.  Measured: zero of the 81 have text that also appears as an inline
case, so this is not duplication -- it is a second corpus with no reader.

That is where §10.250's defect lived.  `x.declared` answered nil on this route
for every program, and one file in `vtest/` was the only program in the tree
that noticed.  It was found by a hand-written comparison run in a scratch
script, not by anything the gate runs.

What this checker does: for every `vtest/*.im` the route accepts, build the C,
compile it, run both, and compare stdout and exit status byte for byte -- the
same criterion `aot_mir_cover` applies to its inline cases, applied to the
files that were left out.

What it does NOT do: it does not compare the programs the route refuses.  A
refusal is not coverage (§10.129's rule), and 40 of the 81 are refused -- 21 of
them `builtin-call`, a declared boundary.  The refused set is printed so the
denominator is visible rather than implied.

Exit codes: 0 every translatable program agreed; 1 at least one disagreed;
2 the measurement could not be made.
"""

from __future__ import annotations

import glob
import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The engine prints its module-load notices on stdout before the program's own
# output.  `aot_mir_cover` strips them the same way; a comparison that did not
# would report every program as differing.
NOTICE_PREFIX = "["


def payload(stdout: str) -> list[str]:
    """The program's own lines, without the engine's load notices."""
    return [line for line in stdout.splitlines() if not line.startswith(NOTICE_PREFIX)]


def run(cmd, cwd, timeout=120):
    try:
        proc = subprocess.run(
            [str(c) for c in cmd], cwd=str(cwd), capture_output=True, timeout=timeout
        )
        return proc.returncode, proc.stdout.decode("utf-8", "replace"), proc.stderr.decode(
            "utf-8", "replace"
        )
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def measure(repo_root: str) -> tuple[int, list[str], dict]:
    build = os.path.join(repo_root, "build")
    interp = os.path.join(build, "inimerse")
    mir_tool = os.path.join(build, "aot-mir")
    for path in (interp, mir_tool):
        if not os.path.isfile(path):
            raise RuntimeError("no %s -- configure and build first" % path)

    programs = sorted(glob.glob(os.path.join(repo_root, "vtest", "*.im")))
    if not programs:
        raise RuntimeError("no vtest/*.im under %s" % repo_root)

    work = tempfile.mkdtemp(prefix="mir_vtest_parity_")
    notes: list[str] = []
    stats = {
        "total": len(programs),
        "refused": 0,
        "cc_failed": 0,
        "same": 0,
        "differ": 0,
        "refusals_by_reason": {},
        "not_parseable": 0,
    }
    differing: list[tuple[str, str]] = []

    cc = os.environ.get("CC", "cc")
    try:
        for source in programs:
            rel = os.path.relpath(source, repo_root)
            base = os.path.splitext(os.path.basename(source))[0]
            cfile = os.path.join(work, base + ".c")
            exe = os.path.join(work, base + ".exe")

            rc_t, out_t, err_t = run([mir_tool, "translate", source, cfile], work)
            if rc_t != 0:
                both = out_t + err_t
                # A refusal from the MIR ROUTE carries `why=`, and that is the
                # class this checker is about.  A program the COMPILER will not
                # parse (a removed `be` declaration, a syntax error) never
                # reaches the route at all: counting it as "refused by the MIR
                # route" would inflate the refusal set with programs the route
                # was never offered.  Measured: 8 of the 81 are this shape, and
                # the first version of this file reported them as `unknown`.
                if "why=" not in both:
                    stats["not_parseable"] += 1
                    continue
                stats["refused"] += 1
                reason = "unknown"
                for line in both.splitlines():
                    if "why=" in line:
                        reason = line.split("why=")[1].split("|")[0].split(":")[0].strip()
                        break
                stats["refusals_by_reason"][reason] = (
                    stats["refusals_by_reason"].get(reason, 0) + 1
                )
                continue

            # `-lm` for the same reason §10.248 recorded: the emitted C calls
            # floor/ceil/sqrt, and a link line without libm fails on programs
            # that would otherwise agree.
            rc_c, _o_c, err_c = run([cc, "-O2", "-o", exe, cfile, "-lm"], work)
            if rc_c != 0:
                stats["cc_failed"] += 1
                first = (err_c or "").strip().splitlines()
                notes.append(
                    "CC-FAILED   %-46s %s" % (rel, first[0][:90] if first else "")
                )
                continue

            rc_i, out_i, _e_i = run([interp, "--no-mods", "run", source], work)
            rc_r, out_r, _e_r = run([exe], work)

            same = rc_i == rc_r and payload(out_i) == payload(out_r)
            if same:
                stats["same"] += 1
            else:
                stats["differ"] += 1
                detail = "interp-rc=%s mir-rc=%s | interp=%r mir=%r" % (
                    rc_i,
                    rc_r,
                    payload(out_i)[-2:],
                    payload(out_r)[-2:],
                )
                differing.append((rel, detail))
                notes.append("DIFFER      %-46s %s" % (rel, detail[:150]))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    red = 1 if (stats["differ"] or stats["cc_failed"]) else 0
    return red, notes, stats


def self_test() -> int:
    """The payload filter has to be able to say DIFFER, and to say SAME.

    A comparison that filtered too much would report every program as agreeing;
    one that filtered too little would report every program as differing,
    because the engine prints its load notices on stdout.  Both directions are
    cheap to get wrong and impossible to notice from a green run.
    """
    failures = []
    a = "[mod] utils loaded\n[verse_dist mod] x\n1\n2\n"
    b = "[mod] utils loaded\n1\n2\n"
    if payload(a) != payload(b):
        failures.append("two programs with different notices must compare equal")
    if payload(a) != ["1", "2"]:
        failures.append("the notices must be stripped: %r" % (payload(a),))
    c = "[mod] utils loaded\n1\n3\n"
    if payload(a) == payload(c):
        failures.append("a real difference must survive the filter")
    if payload("") != []:
        failures.append("empty output must be empty")

    print("check_mir_vtest_parity --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_mir_vtest_parity: self-test FAILED (%d)" % len(failures))
        return 1
    print("  ok   load notices are stripped and a real difference survives")
    print("check_mir_vtest_parity: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, notes, stats = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_mir_vtest_parity: cannot measure: %s" % problem)
        return 2

    for line in notes:
        print("  " + line)
    reasons = ",".join(
        "%s:%d" % (k, v) for k, v in sorted(stats["refusals_by_reason"].items())
    ) or "-"
    print(
        "check_mir_vtest_parity: corpus=%d|translated=%d|same-as-interpreter=%d"
        "|differ=%d|cc-failed=%d|refused-by-mir=%d|not-parseable=%d|reasons=%s"
        % (
            stats["total"],
            stats["same"] + stats["differ"] + stats["cc_failed"],
            stats["same"],
            stats["differ"],
            stats["cc_failed"],
            stats["refused"],
            stats["not_parseable"],
            reasons,
        )
    )
    if red:
        print(
            "check_mir_vtest_parity: the MIR route and the interpreter disagree on a "
            "program in vtest/, which no inline case covers."
        )
        return 1
    print(
        "check_mir_vtest_parity: every vtest program the route translates agrees with "
        "the interpreter.  The refused set is not compared and is named above."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
