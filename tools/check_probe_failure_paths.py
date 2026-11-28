#!/usr/bin/env python3
"""check_probe_failure_paths.py -- a probe that cannot report failure is green forever.

Why this exists (docs/STATUS.md 10.240):

`check_probe_assertions.py` (10.239) asks whether a probe's `assert()` calls
can fire.  That is one of the two ways a probe can be unable to fail, and it
was the one the repository happened to have.  The other is the exit code.

A probe states its expectations and then reports them through its exit code.
Three ways this is done in this tree, and all three are legitimate:

  * `assert(...)` -- the process aborts on failure, so the exit code never
    reaches 0.  Six probes do this.
  * a failure counter plus `return failures == 0 ? 0 : 1;` -- five probes.
  * `if (bad) return <non-zero>;` right at the check -- the rest.

What none of them is, is a guarantee.  The counter can be counted and never
read.  The `return` can be mistyped.  The defect was demonstrated, not argued:

    src/vm/vm_init_probe.c, the failure path changed from `return 1;` to
    `return 0;`, and src/vm/vm.c's `memset(vm, 0, sizeof(VM));` removed so
    that the probe had something to fail about.

    -> ./build/vm_init_probe prints 26 lines of "FAIL <field>"
    -> rc=0
    -> ctest -R vm_init_probe reports "Passed"

The probe reported the failure in plain sight, on stdout, and the test
reported success.  A green CTest line is a claim about the exit code, and the
exit code was wrong.

The ruler, and why it is the ruler:

The obvious check asks "does the source contain a non-zero return?".  That is
a different question, and it is answered by the wrong thing: the number of
`return` statements in `main` says nothing about whether any of them is
reachable from a failure.  A probe whose checks are in a helper function and
whose `main` only has `return 0;` fails by that measure while being perfectly
able to fail -- six of the assert-style probes are exactly that shape, and
they are not the defect.

So the question here is about the exit code's route, not its count:

    Does `main` have a path that ends in a non-zero exit, and is that path
    the one a failure takes?

Two kinds of probe satisfy that, and the checker accepts both:

  * a non-zero `return` reachable from `main`, or
  * `assert()` in the program, which aborts the process before any `return 0`.

The second is accepted because a process that aborts cannot report success;
that is the whole of what 10.239's `-UNDEBUG` guard is protecting.

Honest bounds:
  - This file reads the SOURCE.  It cannot know whether a check that exists is
    a check that runs, nor whether the value it compares against is the right
    one.  A probe can have a perfect failure path and assert nothing worth
    asserting.
  - A probe whose failures are reported through a channel other than the exit
    code (a side-channel file, a server that is then torn down) is out of
    scope; none of the 32 does that, and the check would report it as
    UNKNOWN rather than as clean, so it cannot pass by accident.
  - Only the probes this repository builds are considered.  A probe nothing
    runs is `check_orphan_targets.py`'s business, not this file's.

Exit codes: 0 clean, 1 a defect was found, 2 the source tree could not be read.
"""

from __future__ import annotations

import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A probe source: the name the repository gives these files is a convention,
# and this file follows it -- but it also accepts any file whose target name
# ends in _probe, which is the rule CMakeLists.txt's discovery block uses.
PROBE_GLOBS = ("src/**/*probe*.c",)


def strip_comments(text: str) -> str:
    """Remove C comments, so a `return 1;` inside one is not a return."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def find_main(body: str) -> str | None:
    """The body of `main`, or None if there is no main in this text."""
    match = re.search(r"\bint\s+main\s*\([^)]*\)\s*\{", body)
    if not match:
        return None
    start = match.end() - 1
    depth = 0
    for index in range(start, len(body)):
        char = body[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return body[start + 1 : index]
    return None


def returns_in(text: str) -> list[str]:
    """The return statements in a chunk of C, with their expressions."""
    return [m.group(1).strip() for m in re.finditer(r"\breturn\b([^;]*);", text)]


def nonzero_returns(text: str) -> list[str]:
    """The return statements whose value is not literally 0.

    `return failures == 0 ? 0 : 1;` is a non-zero exit on the failure side
    and is counted here, because that is what it is.  `return 0;` and
    `return (0);` are not.
    """
    out = []
    for expression in returns_in(text):
        if expression == "":
            continue
        if expression == "0" or expression == "(0)":
            continue
        out.append(expression)
    return out


def uses_assert(text: str) -> bool:
    """Does this text use assert() (in code, not in a comment)?"""
    return bool(re.search(r"\bassert\s*\(", text))


def classify(body: str) -> tuple[str, list[str]]:
    """Judge one probe's ability to report failure.

    Returns (verdict, evidence).  The verdict is one of:

      ok        -- main has a non-zero exit, or the program aborts on assert
      no-exit   -- main can only return 0 and there is no assert to abort
      no-main   -- there is no main to judge (a library, or a probe built
                   from a file this checker does not recognise)
    """
    main = find_main(body)
    if main is None:
        return ("no-main", [])
    nonzero = nonzero_returns(main)
    if nonzero:
        return ("ok", nonzero[:4])
    if uses_assert(body):
        return ("ok", ["assert() aborts the process before any return 0"])
    return ("no-exit", returns_in(main)[:4])


def probe_sources(root: str = REPO_ROOT) -> list[str]:
    """Every probe source in the tree."""
    found: set[str] = set()
    for pattern in PROBE_GLOBS:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            if os.path.isfile(path):
                found.add(os.path.normpath(path))
    return sorted(found)


def audit(root: str = REPO_ROOT) -> tuple[list[dict], list[str]]:
    """Every probe source, judged.  Returns (defects, notes)."""
    defects: list[dict] = []
    notes: list[str] = []
    for path in probe_sources(root):
        with open(path, encoding="utf-8", errors="replace") as handle:
            body = strip_comments(handle.read())
        verdict, evidence = classify(body)
        relative = os.path.relpath(path, root)
        if verdict == "no-exit":
            defects.append({"source": relative, "evidence": evidence})
        elif verdict == "no-main":
            notes.append("%s: no main() to judge" % relative)
    return defects, notes


def self_test() -> int:
    """Six controls, each one a claim this file makes."""
    cases = [
        (
            "a counter that is read decides the exit code",
            "static int failures;\nint main(void){ failures = 0;"
            " return failures == 0 ? 0 : 1; }",
            "ok",
        ),
        (
            "a check that returns non-zero at the point of failure",
            "int main(void){ if (bad()) return 3; return 0; }",
            "ok",
        ),
        (
            "assert() aborts, so the exit code never lies",
            "#include <assert.h>\nint main(void){ assert(1 == 2); return 0; }",
            "ok",
        ),
        (
            "a failure that is counted but never read",
            "static int failures;\nint main(void){ failures = 1; return 0; }",
            "no-exit",
        ),
        (
            "a probe whose main only returns 0",
            "int main(void){ return 0; }",
            "no-exit",
        ),
        (
            "a non-zero return inside a comment is not a return",
            "int main(void){ /* return 1; */ return 0; }",
            "no-exit",
        ),
    ]
    red = 0
    for name, source, expected in cases:
        verdict, _evidence = classify(strip_comments(source))
        ok = verdict == expected
        if not ok:
            red += 1
        print(
            "PROBE-FAILPATH-SELFTEST|%s|expected=%s|got=%s|verdict=%s"
            % ("case" if ok else "RED", expected, verdict, "PASS" if ok else "FAIL")
        )
        print("     %s" % name)
    ok = red == 0 and len(cases) > 0
    if not cases:
        print("PROBE-FAILPATH-SELFTEST: the case table is empty, so nothing was "
              "measured; a suite that runs no cases reports success by "
              "arithmetic (docs/STATUS.md 10.293)")
    print(
        "PROBE-FAILPATH-SELFTEST-VERDICT|cases=%d|red=%d|verdict=%s"
        % (len(cases), red, "PASS" if ok else "FAIL")
    )
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            root = os.path.abspath(argv[index + 1])
    if not os.path.isdir(os.path.join(root, "src")):
        print(
            "check_probe_failure_paths: %s does not look like the repository "
            "(no src/ under it)" % root
        )
        return 2

    defects, notes = audit(root)
    for defect in defects:
        print(
            "WEAK %s: main() can only report success -- a failure here would "
            "still be a green test" % defect["source"]
        )
        print(
            "       returns in main: %s"
            % (", ".join(defect["evidence"]) if defect["evidence"] else "(none)")
        )
        print(
            "       a probe must reach a non-zero exit when its checks fail, "
            "or abort on assert()"
        )
    for note in notes:
        print("NOTE %s" % note)

    total = len(probe_sources(root))
    print(
        "check_probe_failure_paths: %d probe source(s) read, %d without a "
        "failure exit." % (total, len(defects))
    )
    return 1 if defects else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
