#!/usr/bin/env python3
"""Assert that every `vtest/*.im` fixture can be parsed.

Why this exists
---------------
`vtest/lint_case_enum_v04.im` and `vtest/lint_case_membership_v04.im` both
contained `dir be Direction = "N"`.  `be` takes `:` for its initialiser
(`src/parser/parser.c:1383-1391`); `type X = ...` is a different statement and
takes `=` (`parse_type_stmt`, `src/parser/parser.c:1218-1227`).  So both
fixtures were parse errors:

    Error: expected 'expression', but got '=' (type 83)

and nothing noticed, because both are consumed only by `--lint`, and the lint
path does not print the parse error at all.  The CTest passed on the warning
text while the program the fixture describes had never been parsed.  That is
the same shape as the other findings in this family: an assertion whose
denominator -- "can this fixture be parsed at all" -- was never checked.

So this scans every fixture and asserts the count of unparseable ones is zero,
and it prints both numbers, because a check that cannot say how many things it
looked at is the defect it is meant to catch.

What counts as a parse error
----------------------------
The marker list is literal, taken from the only two files that report parse
failures (`src/parser/parser.c`, `src/lexer/lexer.c`).  It is a literal-prefix
test, not a classification: a new error message shape added to the parser
would not be recognised here.  That is a bound of this check, stated rather
than hidden.

Deliberately excluded: `Error at line %d: task/thread definitions inside a loop
are silently ineffective` (parser.c:1557) reports a construct that is accepted
and then ignored, not a program that failed to parse.

A fixture may be listed in ALLOWED, but every entry must say what it is
evidence for.  A bare filename would be the same defect one level up: an
exemption that nobody can audit.

Where the scan runs
-------------------
Against a copy of `vtest/`, not against `vtest/` itself.  The interpreter
chdirs to the directory of the script it is running (`chdir_to_script_dir`,
`src/main.c:520`), so a fixture that writes a file writes it next to itself,
and `vtest/say_pair_probe_v06.im:11` calls
`say_file("FILETEXT", "sayout.txt")`.  Every scan therefore appended
`FILETEXT\n` to `vtest/sayout.txt` -- an untracked file that `git status`
reported after every gate run, because this scan executes fixtures rather than
only parsing them.  A copy keeps the sibling-relative reads the fixtures rely
on (`params_relative_v06.params`, `params_sub/`) and puts every write where a
test is allowed to write.  The scan then asserts that `vtest/` gained and lost
nothing, so a fixture that finds another way into the tree fails here instead
of turning up in `git status`.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Literal parse-phase failure messages, from src/parser/parser.c and
# src/lexer/lexer.c.  `expected '` covers both shapes of parse_error_expected.
MARKERS = (
    "expected '",
    "Error: f-string interpolation only supports plain identifiers",
    "Error: case action expression runs into ':'",
    "Error: '++'/'--' currently only supported on simple variables",
    "Error: unterminated string literal",
)

# fixture name -> the reason it is allowed to be unparseable.
ALLOWED = {}

# A scan that found nothing must not pass: "0 of 0" is not a clean tree.
MIN_FIXTURES = 50

TIMEOUT = 20


def find_engine():
    if len(sys.argv) > 1:
        return Path(sys.argv[1]).resolve()
    candidates = []
    env = os.environ.get("INIMERSE_BIN")
    if env:
        candidates.append(Path(env))
    for _dir in ("build", "build-local", "build-windows-gcc", "build-py"):
        for _name in ("inimerse", "inimerse.exe"):
            candidates.append(Path(_dir) / _name)
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand.resolve()
    return None


def scan(engine, fixture):
    """Return the first parse-error marker seen, or None."""
    try:
        proc = subprocess.run(
            [str(engine), "--no-mods", str(fixture)],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        # A fixture that runs forever did parse (or at least did not fail to).
        return None
    out = (proc.stdout or "") + (proc.stderr or "")
    for marker in MARKERS:
        if marker in out:
            line = next(
                (ln for ln in out.splitlines() if marker in ln), marker
            )
            return line.strip()
    return None


def main():
    engine = find_engine()
    assert engine, (
        "inimerse engine binary not found (set INIMERSE_BIN to override, "
        "or pass it as argv[1])"
    )

    fixtures = sorted((ROOT / "vtest").glob("*.im"))
    assert len(fixtures) >= MIN_FIXTURES, (
        "only %d fixture(s) found under vtest/ -- expected at least %d; "
        "a scan that looks at nothing is not a passing scan"
        % (len(fixtures), MIN_FIXTURES)
    )

    source = ROOT / "vtest"
    before = sorted(p.name for p in source.iterdir())

    bad = []
    with tempfile.TemporaryDirectory(prefix="inimerse-fixture-parse-") as tmp:
        scratch = Path(tmp) / "vtest"
        shutil.copytree(source, scratch)
        for fixture in sorted(scratch.glob("*.im")):
            if fixture.name in ALLOWED:
                continue
            found = scan(engine, fixture)
            if found:
                bad.append((fixture.name, found))

    after = sorted(p.name for p in source.iterdir())
    if after != before:
        print(
            "fixture_parse: the scan changed the tree it was scanning -- "
            "vtest/ gained %s and lost %s"
            % (sorted(set(after) - set(before)), sorted(set(before) - set(after)))
        )
        print(
            "a fixture that writes next to itself must be run somewhere it is "
            "allowed to write, not in the source tree"
        )
        return 1

    print(
        "fixture_parse: %d fixture(s) scanned, %d emitted a parse error"
        % (len(fixtures), len(bad))
    )

    for name, line in bad:
        print("  %s: %s" % (name, line))

    if bad:
        print(
            "a fixture that cannot be parsed cannot be evidence for anything "
            "the engine does with it"
        )
        return 1

    if ALLOWED:
        print("allowed unparseable (%d):" % len(ALLOWED))
        for name in sorted(ALLOWED):
            print("  %s -- %s" % (name, ALLOWED[name]))

    print("fixture_parse tests: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
