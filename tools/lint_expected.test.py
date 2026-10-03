#!/usr/bin/env python3
"""Assert what --lint actually reports, per fixture.

The five `vtest/lint_case_*_v04.im` fixtures used to be registered as

    add_test(NAME lint_case_try_runtime COMMAND inimerse --lint <fixture>)

and that registration asserted nothing at all: `--lint` returned 0
unconditionally, so a fixture could gain or lose every finding it has and the
test stayed green.  Four of the five fixtures do report findings (4, 1, 2 and 4
lines) and still exited 0 -- see docs/SYNTAX.md M11.

The exit code now carries the verdict (1 = at least one finding, 2 = the file
could not be read, 0 = clean).  This driver pins the exit code, the exact number
of `[lint]` lines and the required text of each finding, so both a missed
finding and newly introduced noise fail the test.

It also pins one honest boundary: `--lint` is still NOT a parse predicate.
`lint_scan()` is a line-based scanner, so `x = = 5` produces zero findings and
exits 0 while really running it exits 1.  That limitation is recorded as a
PINNED case below rather than hidden, so that if anyone ever teaches lint_scan
to parse, this test goes red and says the documentation needs updating.

Usage:
    lint_expected.test.py <inimerse> [--fixture NAME]

Without --fixture every fixture in the table is checked.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VTEST = REPO / "vtest"

# fixture stem -> (expected exit code, expected count of "[lint]" lines, required substrings)
EXPECT = {
    "lint_case_try_v04": (1, 4, [
        "case has no wildcard '_'/'else' branch",
        "case try is missing Result branch(es): err",
        "case try guarded err coverage does not prove complete err coverage",
        "case try finite err type 'FileError' is missing members: permission_denied, disk_full",
    ]),
    "lint_case_enum_v04": (1, 1, [
        "finite case type 'Direction' is missing members: E, W",
    ]),
    "lint_case_membership_v04": (1, 2, [
        "case branch is unreachable: a finite-set membership branch already covers the subject",
    ]),
    "lint_case_try_members_v04": (1, 4, [
        "case has no wildcard '_'/'else' branch",
        "case try is missing Result branch(es): err",
        "case try guarded err coverage does not prove complete err coverage",
        "case try finite err type 'FileError' is missing members: permission_denied, disk_full",
    ]),
    "lint_case_try_alias_v04": (0, 0, []),
}

# PINNED: a syntax error is invisible to the line-based scanner, so --lint still
# exits 0 on it.  The real interpreter exits 1 on the same file.  This is the
# remaining half of M11 and it is deliberate, not an oversight.
PINNED_SYNTAX_ERROR = ("x = = 5\n", 0, 1)


def run_lint(inimerse, path):
    proc = subprocess.run(
        [str(inimerse), "--lint", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(REPO),
    )
    out = proc.stdout.decode("utf-8", "replace") + proc.stderr.decode("utf-8", "replace")
    findings = [ln for ln in out.splitlines() if "[lint]" in ln]
    return proc.returncode, findings


def run_script(inimerse, path):
    proc = subprocess.run(
        [str(inimerse), "--no-mods", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(REPO),
    )
    return proc.returncode


def check_fixture(inimerse, stem, failures):
    want_rc, want_n, want_text = EXPECT[stem]
    path = VTEST / (stem + ".im")
    if not path.exists():
        failures.append("%s: fixture missing at %s" % (stem, path))
        return

    rc, findings = run_lint(inimerse, path)

    if rc != want_rc:
        failures.append(
            "%s: exit code %d, expected %d "
            "(0 = clean, 1 = findings, 2 = unreadable)" % (stem, rc, want_rc))
    if len(findings) != want_n:
        failures.append(
            "%s: %d lint line(s), expected %d\n    got:\n      %s"
            % (stem, len(findings), want_n, "\n      ".join(findings) or "(none)"))
    for text in want_text:
        if not any(text in ln for ln in findings):
            failures.append("%s: no finding containing %r" % (stem, text))

    # A clean fixture must be genuinely clean, not merely exit-code-clean.
    if want_rc == 0 and findings:
        failures.append("%s: expected a clean scan but got %d finding(s)" % (stem, len(findings)))


def check_pinned(inimerse, failures):
    src, want_rc, run_rc = PINNED_SYNTAX_ERROR
    tmpdir = tempfile.mkdtemp(prefix="lint_expected_")
    tmp = Path(tmpdir) / "syntax_error.im"
    tmp.write_text(src, encoding="utf-8")
    try:
        rc, findings = run_lint(inimerse, tmp)
        real = run_script(inimerse, tmp)
    finally:
        try:
            os.unlink(str(tmp))
            os.rmdir(tmpdir)
        except OSError:
            pass

    if rc != want_rc:
        failures.append(
            "PINNED syntax-error case: --lint exit %d, expected %d. If lint_scan "
            "learned to parse, update docs/SYNTAX.md M11 and this table."
            % (rc, want_rc))
    if findings:
        failures.append(
            "PINNED syntax-error case: expected zero findings from the line-based "
            "scanner, got %d" % len(findings))
    if real != run_rc:
        failures.append(
            "PINNED syntax-error case: really running the file exited %d, expected %d"
            % (real, run_rc))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inimerse")
    ap.add_argument("--fixture", default=None,
                    help="check only this fixture stem (default: all)")
    args = ap.parse_args()

    inimerse = Path(args.inimerse).resolve()
    if not inimerse.exists():
        print("lint expected: FAIL - no interpreter at %s" % inimerse)
        return 1

    if args.fixture:
        if args.fixture not in EXPECT:
            print("lint expected: FAIL - unknown fixture %r" % args.fixture)
            return 1
        stems = [args.fixture]
    else:
        stems = sorted(EXPECT)

    failures = []
    for stem in stems:
        check_fixture(inimerse, stem, failures)
    if not args.fixture:
        check_pinned(inimerse, failures)

    if failures:
        print("lint expected: FAIL (%d problem(s))" % len(failures))
        for f in failures:
            print("  - %s" % f)
        return 1

    print("lint expected: ok (%d fixture(s) asserted: exit code + finding count + "
          "required text + pinned syntax-error limitation)" % len(stems))
    return 0


if __name__ == "__main__":
    sys.exit(main())
