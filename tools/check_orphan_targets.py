#!/usr/bin/env python3
"""check_orphan_targets.py -- an executable no CTest runs is not a test.

Why this exists (docs/AUDIT.md 1.67, and the sibling check_orphan_fixtures.py):

`src/platform/vfs.c`'s `..` guard searched for a terminator that was never
written, so it read uninitialised memory: `im_vfs_normalize("os:/../escape")`
was accepted 40 times out of 40.  The one program that asserts the correct
behaviour is `src/platform/vfs_probe.c` -- and it had been built for as long as
anyone could remember while nothing ever ran it.  It was found by a person
reading code, not by a checker.

`tools/check_orphan_fixtures.py` is the checker that exists for exactly this
question ("is every test input run by something?") and it *structurally cannot*
find that one: it globs `vtest/*.im`, `tools/*.test.py` and `tools/*.test.js`.
`grep -c 'src/' tools/check_orphan_fixtures.py` is 0.  A probe that lives in
`src/platform/` is outside its denominator, so the orphan it should have caught
was invisible to it -- the same shape as the rest of this family, one level up:
the set is named, and the set that actually runs is never compared against it.

This file is the second half of that comparison, and it closes the hole: the
named set is the `add_executable( )` targets, wherever their sources live, and
the running set is the targets that some `add_test( )` actually names.

The ruler matters, and the obvious ruler is wrong:

    An earlier hand pass at this question matched the *test name* against the
    target name -- `add_test(NAME <target> COMMAND <target>)` -- and reported
    three orphans: `literal_resolve_probe`, `resolve_timeout_probe` and
    `websocket_probe`.  Two of those were false.  A test's NAME is chosen by
    whoever writes the registration and need not resemble the target it runs:
    the tests are `literal_resolve_runtime` and `resolve_timeout_runtime`, and
    both targets are run.  Only `websocket_probe` was real.  So this checker
    matches the target that is *used* -- the first token after `COMMAND`, and
    any `$<TARGET_FILE:...>` generator expression -- never the test name.

Honest bounds:
  - `COMMAND`'s first token is read textually.  A target reached through a
    CMake variable (`COMMAND ${SOME_TARGET}`) would not be recognised; none
    exist today, and this file says so rather than guessing.
  - "Run by a CTest" is not "asserted".  A registration with no PASS and no
    FAIL regex can pass on any exit code -- see docs/SYNTAX.md 7.2 (M13).  This
    checker answers "does anything run it", not "does running it prove
    anything".
  - A target named by something other than a test (an install rule, a custom
    command) is still reported: being copied is not being run.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Targets that are deliberately not run by any CTest, each with the thing that
# runs instead.  Read the reason, not the name.
ALLOWED = {
    "websocket_probe": (
        "not an omission but a state that is already written down: the WebSocket "
        "frame layer is not implemented, so the probe has nothing to assert. "
        "docs/API.md:481 lists it among the built binaries that are not CTest "
        "registrations, and docs/API.md:622 marks the WebSocket row as "
        "'reserved / protocol frames not implemented'.  Register it the day "
        "there is a frame to parse."
    ),
}


def cmake_blocks(text, command):
    """Yield the argument text of every `command( ... )`, paren-balanced.

    Registrations and target declarations both span several lines here
    (CMakeLists.txt:626-628, :670-672), so a line-oriented read is not enough.
    """
    out = []
    for m in re.finditer(r"\b" + command + r"\s*\(", text):
        i = m.end()
        depth = 1
        while i < len(text) and depth:
            c = text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        out.append(text[m.end():i - 1])
    return out


def declared_targets(text):
    targets = []
    for body in cmake_blocks(text, "add_executable"):
        m = re.match(r"\s*([A-Za-z0-9_.\-]+)", body)
        if m:
            targets.append(m.group(1))
    return targets


def targets_run_by_tests(text):
    """Targets some add_test( ) names -- by COMMAND or by $<TARGET_FILE:...>.

    Deliberately not the test name: see the header.
    """
    used = set()
    for body in cmake_blocks(text, "add_test"):
        for tm in re.finditer(r"\$<TARGET_FILE:([^>]+)>", body):
            used.add(tm.group(1).strip())
        cm = re.search(r"\bCOMMAND\b(.*)", body, re.S)
        if not cm:
            continue
        tokens = cm.group(1).split()
        if tokens:
            used.add(tokens[0].strip().strip('"'))
    return used


def main() -> int:
    cmake_path = ROOT / "CMakeLists.txt"
    if not cmake_path.is_file():
        print(f"check_orphan_targets: no {cmake_path}", file=sys.stderr)
        return 2
    cmake = cmake_path.read_text(encoding="utf-8", errors="surrogateescape")

    targets = sorted(set(declared_targets(cmake)))
    tests = cmake_blocks(cmake, "add_test")
    used = targets_run_by_tests(cmake)

    if not targets or not tests:
        print(
            f"check_orphan_targets: parsed {len(targets)} add_executable( ) and "
            f"{len(tests)} add_test( ) -- one of those is zero, so the parse "
            f"failed and this check would report a pass it did not earn.",
            file=sys.stderr,
        )
        return 2

    problems = []
    for name in targets:
        if name in used or name in ALLOWED:
            continue
        elsewhere = len(re.findall(r"\b" + re.escape(name) + r"\b", cmake)) - 1
        problems.append(
            f"{name}: built, but no add_test( ) runs it (it is named "
            f"{elsewhere} other time(s) in CMakeLists.txt). Either register a "
            f"test that runs it, or add it to ALLOWED in this file with the "
            f"thing that runs instead."
        )

    if problems:
        print(
            f"check_orphan_targets: {len(problems)} target(s) out of "
            f"{len(targets)} checked are run by no CTest:",
            file=sys.stderr,
        )
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        print(
            "check_orphan_targets: a program nothing runs cannot fail, and "
            "cannot pass either -- it is not evidence.",
            file=sys.stderr,
        )
        return 1

    print(
        f"check_orphan_targets: {len(targets)} add_executable( ) target(s) "
        f"checked against {len(tests)} add_test( ) registration(s); "
        f"{len(targets) - len(ALLOWED)} run by at least one CTest, "
        f"{len(ALLOWED)} allowed with a stated reason."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
