#!/usr/bin/env python3
"""check_orphan_fixtures.py -- a test input that nothing runs is not a test.

Why this exists (docs/AUDIT.md §1.59):

`docs/API.md:114` said the `case value { _: ... }` default-branch diagnostic was
covered by "`lint_case_missing_default_v04.im` 相关 CTest", and `:115` listed
`lint_case_exhaustive_v04.im` among four real CTest names in a column headed
"evidence (CTest)".  Neither fixture had a registration line.  Both run green by
hand:

    $ ./build/inimerse --lint vtest/lint_case_missing_default_v04.im
    [lint] line 1 [WARN] case has no wildcard '_'/'else' branch; ...
    $ ./build/inimerse --lint vtest/lint_case_exhaustive_v04.im
    [lint] line 3 [WARN] case branch is unreachable: wildcard '_'/'else' ...

so the capability was real, the fixture was real, and the claim was false --
because the five `lint_case_*` tests are *hand-listed* in CMakeLists.txt and the
sixth and seventh were never added.  Nothing in the repository compared the set
of fixtures against the set of registrations, so a fixture could be written,
documented, quoted in a table as evidence, and run by nobody.

This is the same shape as the rest of this family: a set is named, and its
membership is never checked against the set that actually runs.  `--only` with a
misspelled stage ran nothing and reported a pass; the fuzz stage's denominator
is written in prose and read by nobody; the Linux CI job built its test list
with a `sed` that silently matched 35 of 134 tests.  Here the named set is the
`vtest/` directory and the running set is CMakeLists.txt.

What is checked, and why each set:

  1. `vtest/*.im`      -- a fixture must be named in CMakeLists.txt, or be in
                          ALLOWED below with a reason.  This is the set that
                          caught the two lint fixtures.
  2. `tools/*.test.py` -- a harness must be named in CMakeLists.txt.  Zero
                          orphans today; the check is here so the next one is
                          visible the day it lands.
  3. `tools/*.test.js` -- a harness must be named in CMakeLists.txt or in
                          tools/node_suites/run_all.js, which is how the Node
                          suites are actually invoked.  Zero orphans today.

A file in ALLOWED is not exempt from the question; it is an answer to it.  Every
entry names the thing that *does* run instead, so the entry can be checked by
reading it.  An allowlist of bare filenames would be the same defect one level
up.

Honest bounds:
  - "Named in CMakeLists.txt" is a substring test, not a parse.  A fixture whose
    name appears only inside a comment would pass.  That is deliberate: the
    alternative is a CMake parser, and a comment that names a fixture is at
    least a reader who can be found.
  - This says nothing about whether a registered test *asserts* anything.  A
    registration with no PASS regex and no FAIL regex can pass on any exit code
    -- see docs/SYNTAX.md §7.2 (M13).
  - Only `vtest/*.im` is checked, not `vtest/*.params`, `*.inim` or `*.txt`.
    Those are inputs to registered tests rather than tests themselves, and
    `params_precompiled_v06.inim` is named by the registration that runs it.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Fixtures that are deliberately not registered, each with the thing that runs
# instead.  Read the reason, not the name.
ALLOWED = {
    "params_precompiled_v06.im": (
        "the *source* of vtest/params_precompiled_v06.inim, which is what "
        "`params_precompiled_runtime` (CMakeLists.txt) actually runs -- the "
        "test exercises the serialised program, so the source is an input to "
        "the build step and not a test input"
    ),
    "eidos_object_probe_v04.im": (
        "a sample quoted in docs/API.md:201 to show a factory function "
        "returning a dict of closures; the feature is asserted by "
        "tools/eidos_runtime.test.py, which carries its own inline scripts"
    ),
    "say_pair_probe_v06.im": (
        "an ad-hoc probe whose output is recorded verbatim in "
        "docs/AUDIT.md:1811-1812; it was run by hand while comparing two engines "
        "and was never meant to be a regression input"
    ),
    "range_metadata_probe_v06.im": (
        "a recording, not a test: every line is an observation with no "
        "assertion, and its header names the binary sha256 and the commit it "
        "was taken on.  Registering it would add a green stage that cannot "
        "fail -- see docs/SYNTAX.md 7.4 H4 on one observation versus a "
        "property.  It is kept as evidence for the open .range question"
    ),
}


def named_in_cmake(name: str, cmake: str) -> bool:
    return name in cmake


def main() -> int:
    cmake_path = ROOT / "CMakeLists.txt"
    if not cmake_path.is_file():
        print(f"check_orphan_fixtures: no {cmake_path}", file=sys.stderr)
        return 2
    cmake = cmake_path.read_text(encoding="utf-8", errors="surrogateescape")

    node_runner = ROOT / "tools" / "node_suites" / "run_all.js"
    node_text = ""
    if node_runner.is_file():
        node_text = node_runner.read_text(encoding="utf-8", errors="surrogateescape")

    problems = []
    checked = 0

    # 1. vtest fixtures
    fixtures = sorted(p.name for p in (ROOT / "vtest").glob("*.im"))
    for name in fixtures:
        checked += 1
        if named_in_cmake(name, cmake):
            continue
        if name in ALLOWED:
            continue
        problems.append(
            f"vtest/{name}: not named in CMakeLists.txt and not in ALLOWED. "
            f"Either register it (add_test NAME ... COMMAND inimerse --lint "
            f"${{CMAKE_SOURCE_DIR}}/vtest/{name}) or add it to ALLOWED in this "
            f"file with the thing that runs instead."
        )

    # 2. python harnesses
    py_tests = sorted(p.name for p in (ROOT / "tools").glob("*.test.py"))
    for name in py_tests:
        checked += 1
        if not named_in_cmake(name, cmake):
            problems.append(
                f"tools/{name}: no CTest runs it. A harness that nothing "
                f"invokes reports nothing."
            )

    # 3. node harnesses
    js_tests = sorted(p.name for p in (ROOT / "tools").glob("*.test.js"))
    for name in js_tests:
        checked += 1
        if not (named_in_cmake(name, cmake) or name in node_text):
            problems.append(
                f"tools/{name}: named neither in CMakeLists.txt nor in "
                f"tools/node_suites/run_all.js, so no suite runs it."
            )

    if problems:
        print(
            f"check_orphan_fixtures: {len(problems)} orphaned input(s) out of "
            f"{checked} checked:",
            file=sys.stderr,
        )
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        print(
            "check_orphan_fixtures: an input nothing runs cannot fail, and "
            "cannot pass either -- it is not evidence.",
            file=sys.stderr,
        )
        return 1

    print(
        f"check_orphan_fixtures: {checked} input(s) checked "
        f"({len(fixtures)} vtest fixtures, {len(py_tests)} python harnesses, "
        f"{len(js_tests)} node harnesses); "
        f"{len(fixtures) - sum(1 for n in fixtures if n in ALLOWED)} fixtures "
        f"registered, {len(ALLOWED)} allowed with a stated reason."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
