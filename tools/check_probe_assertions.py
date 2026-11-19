#!/usr/bin/env python3
"""check_probe_assertions.py -- a probe whose asserts are compiled out is green forever.

Why this exists (docs/STATUS.md 10.239):

`CMakeLists.txt` carries six probes that state their expectations with
`assert()`: `bytecode_capture_probe`, `closure_probe`, `typeset_probe`,
`enum_probe`, `error_types_probe` and `type_registry_probe`.  `assert()` is a
no-op under `-DNDEBUG`, and `CMAKE_C_FLAGS_RELEASE` is `-O3 -DNDEBUG` -- so a
probe compiled as-is exits 0 no matter what the engine does.  The repository
knows this: the six are listed in a `foreach` that appends `-UNDEBUG`, and the
comment above it says why.

That list is a list of names, and a list of names is only as good as the
discipline that keeps it current.  Nothing measures it.  Adding a probe that
uses `assert()` and forgetting the `foreach` entry produces a test that
cannot fail -- and it fails silently, in the most expensive way there is: it
prints its success line.

The defect was demonstrated, not argued:

    src/types/inject_probe.c:  int x = 41; assert(x == 42);
    add_executable(inject_probe src/types/inject_probe.c)
    add_test(NAME inject_probe COMMAND inject_probe)

    -> ./build/inject_probe prints "inject_probe: all good", rc=0
    -> ctest -R inject_probe reports "Passed"

The assertion could not have failed.  A green CTest line said it had.

The ruler, and why it is the ruler and not the obvious one:

The obvious check reads the source and asks "does it use assert()?".  That
answers a different question: it cannot tell a probe whose asserts were
compiled out from one whose asserts are live, and it says nothing about a
probe that states its expectations some other way.  So the question here is
narrower and mechanical:

    Does the target that CTest runs use assert(), and is NDEBUG in effect
    when it was compiled?

Both halves are read from the build tree, not from the source of truth we
hope is current:

  * the *effective* NDEBUG state comes from the compile flags the build
    actually used.  gcc applies the last of `-D`/`-U` for a macro, so
    `-O3 -DNDEBUG -std=gnu11 -UNDEBUG` leaves assertions ON, and the naive
    reading of "the flags contain -DNDEBUG" would report the six guarded
    probes as broken.  This file reads the order.
  * the *sources* of a target come from the object files the build produced
    (`build/CMakeFiles/<target>.dir/**/*.c.o`), because the target name in
    `CMakeLists.txt` need not resemble the file it compiles -- the defect
    above is exactly that mismatch: `registry_probe.c` is the target
    `type_registry_probe`.

Honest bounds:
  - A probe that states its expectations without `assert()` (a hand-written
    comparison, a non-zero return) is not this file's subject.  Those probes
    are safe under `-DNDEBUG` precisely because they do not use `assert()`,
    and this file says nothing about whether their comparison is correct.
  - The flags are read from the build tree that exists now.  A tree that was
    never configured has no flags to read, and this file reports that as a
    separate condition (exit 2) rather than as a clean run -- "no build to
    read" and "no defect found" are different answers.
  - Only targets a CTest actually runs are considered.  A probe nothing runs
    is `check_orphan_targets.py`'s business, not this file's.
  - This file measures whether an assertion can fire.  It does not measure
    whether the assertion is the RIGHT one -- a probe can assert something
    true and useless.  That is the other half of a test, and it is out of
    scope here.

Exit codes: 0 clean, 1 a defect was found, 2 the build tree could not be read.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BUILD_DIR = os.path.join(REPO_ROOT, "build")

# A macro defined with -D and undefined with -U: the last one wins.
_NDEBUG_DEF = re.compile(r"-DNDEBUG\b")
_NDEBUG_UNDEF = re.compile(r"-UNDEBUG\b")


def effective_ndebug(flags: str) -> bool:
    """Is NDEBUG defined after the whole command line is applied?

    gcc applies the last -D/-U for a macro, so the ORDER is the fact.  The
    six probes the repository guards carry `-O3 -DNDEBUG ... -UNDEBUG`, and
    reading the set of flags instead of their order would report them as
    broken -- which is the false positive this function exists to avoid.
    """
    defined = False
    seen = False
    for token in flags.split():
        if _NDEBUG_DEF.fullmatch(token):
            defined = True
            seen = True
        elif _NDEBUG_UNDEF.fullmatch(token):
            defined = False
            seen = True
    return defined if seen else False


def ctest_targets(build_dir: str) -> list[str]:
    """The build-tree executables some CTest actually runs.

    Matched on the executable that is EXECUTED, never on the test name: a
    test's NAME is chosen by whoever writes the registration and need not
    resemble the target it runs (check_orphan_targets.py says this at
    length).  A target that is run only under a platform branch is simply
    absent from this build, and absent is not a defect.
    """
    proc = subprocess.run(
        ["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "ctest --show-only=json-v1 failed in %s: %s"
            % (build_dir, (proc.stderr or proc.stdout).strip()[:400])
        )
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError("ctest did not print JSON for %s: %s" % (build_dir, exc))

    names: set[str] = set()
    build_real = os.path.realpath(build_dir)
    for test in data.get("tests", []):
        for token in test.get("command", []):
            token = str(token)
            if not token.startswith(build_real + os.sep):
                continue
            if not os.path.isfile(token) or not os.access(token, os.X_OK):
                continue
            names.add(os.path.basename(token))
    return sorted(names)


def target_flags(build_dir: str, target: str) -> str | None:
    """The C flags the build actually used for this target."""
    path = os.path.join(build_dir, "CMakeFiles", target + ".dir", "flags.make")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    match = re.search(r"^C_FLAGS = (.*)$", text, re.M)
    return match.group(1) if match else ""


def target_sources(build_dir: str, target: str) -> list[str]:
    """The sources the build actually compiled into this target.

    Read out of the object files, not out of `CMakeLists.txt`: the target
    name need not resemble the file it compiles.  `src/types/registry_probe.c`
    is the target `type_registry_probe`, and a checker that matched names
    would have missed it in both directions.
    """
    obj_dir = os.path.join(build_dir, "CMakeFiles", target + ".dir")
    sources: list[str] = []
    for root, _dirs, files in os.walk(obj_dir):
        for name in files:
            if not name.endswith(".c.o"):
                continue
            # The object path is inside the build tree, but the SOURCE it
            # was built from is in the repository: CMake keeps the source's
            # relative path under the target's object directory.
            rel = os.path.relpath(os.path.join(root, name)[: -len(".o")], obj_dir)
            for candidate in (
                os.path.join(REPO_ROOT, rel),
                os.path.join(build_dir, rel),
            ):
                if os.path.isfile(candidate):
                    sources.append(candidate)
                    break
    return sorted(set(sources))


def assert_count(path: str) -> int:
    """How many assert() calls the source really has.

    Comments are stripped first: a probe that merely MENTIONS assert() in a
    comment does not depend on it, and counting that would send the report
    at a file that is fine.
    """
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return len(re.findall(r"\bassert\s*\(", text))


def audit(build_dir: str) -> tuple[list[dict], list[str]]:
    """Return (defects, unreadable)."""
    defects: list[dict] = []
    unreadable: list[str] = []
    for target in ctest_targets(build_dir):
        flags = target_flags(build_dir, target)
        if flags is None:
            # No flags.make: the target was not compiled here (or was
            # compiled by a generator that does not write one).  Not a
            # defect -- this file is about asserts that cannot fire.
            continue
        if not effective_ndebug(flags):
            continue  # assertions are live here; nothing to report
        sources = target_sources(build_dir, target)
        if not sources:
            continue
        hits = [(src, assert_count(src)) for src in sources]
        total = sum(n for _src, n in hits)
        if total == 0:
            continue
        defects.append(
            {
                "target": target,
                "asserts": total,
                "sources": [src for src, n in hits if n],
                "flags": flags,
            }
        )
    return defects, unreadable


def self_test() -> int:
    """Three controls: the defect is found, and the guarded shape is not."""
    cases = [
        # (flags, expected NDEBUG effective)
        ("-O3 -DNDEBUG -std=gnu11", True),
        ("-O3 -DNDEBUG -std=gnu11 -UNDEBUG", False),
        ("-O3 -std=gnu11", False),
        ("-DNDEBUG -UNDEBUG -DNDEBUG", True),
    ]
    failures = 0
    for flags, expected in cases:
        actual = effective_ndebug(flags)
        if actual != expected:
            print("FAIL -- effective_ndebug(%r) is %s, expected %s"
                  % (flags, actual, expected))
            failures += 1

    # A source that mentions assert() only in a comment must not count.
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        commented = os.path.join(tmp, "commented.c")
        with open(commented, "w", encoding="utf-8") as handle:
            handle.write("/* we used to assert(x == 42) here */\nint main(void){return 0;}\n")
        real = os.path.join(tmp, "real.c")
        with open(real, "w", encoding="utf-8") as handle:
            handle.write("int main(void){ assert(1); return 0; }\n")
        if assert_count(commented) != 0:
            print("FAIL -- a commented assert() was counted")
            failures += 1
        if assert_count(real) != 1:
            print("FAIL -- a real assert() was not counted")
            failures += 1

    print("PROBE-ASSERT-SELFTEST-VERDICT|cases=%d|red=%d|verdict=%s"
          % (len(cases) + 2, failures, "PASS" if failures == 0 else "FAIL"))
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    build_dir = DEFAULT_BUILD_DIR
    for i, arg in enumerate(argv):
        if arg == "--build-dir" and i + 1 < len(argv):
            build_dir = argv[i + 1]
    if not os.path.isdir(os.path.join(build_dir, "CMakeFiles")):
        print("check_probe_assertions: no build tree at %s -- "
              "configure it first, or pass --build-dir" % build_dir)
        return 2
    try:
        defects, _unreadable = audit(build_dir)
    except RuntimeError as exc:
        print("check_probe_assertions: %s" % exc)
        return 2

    for defect in defects:
        print("WEAK %s: %d assert() call(s) compiled with NDEBUG in effect"
              % (defect["target"], defect["asserts"]))
        for src in defect["sources"]:
            print("       the assertion lives in %s" % os.path.relpath(src, REPO_ROOT))
        print("       flags: %s" % defect["flags"])
        print("       add it to the foreach(_asserting_probe ...) list in "
              "CMakeLists.txt, which appends -UNDEBUG")

    print("check_probe_assertions: %d target(s) run by CTest, %d with assert() "
          "compiled out." % (len(ctest_targets(build_dir)), len(defects)))
    return 1 if defects else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
