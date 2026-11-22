#!/usr/bin/env python3
"""check_python_asserts.py -- an assert a flag can delete is not a check.

Why this exists (docs/STATUS.md 10.241):

The suite's Python tests state their expectations with `assert`.  The
interpreter is allowed to delete those statements:

    $ python3 -c 'import sys; print(sys.flags.optimize)'          -> 0
    $ PYTHONOPTIMIZE=1 python3 -c 'import sys; print(sys.flags.optimize)' -> 1

Under optimize >= 1 the compiler drops every `assert` statement: the
condition is never evaluated and the message never raised.  A test whose only
way to fail is an assert therefore reports success -- exit 0, and a green
CTest line -- without having checked anything.  The defect was demonstrated,
not argued:

    tools/scan_tools.test.py, `raise AssertionError("INJECTED: ...")` added at
    the top of main(), which is the shape the rewrite below produces:

    -> python3             tools/scan_tools.test.py  -> rc=1
    -> PYTHONOPTIMIZE=1    tools/scan_tools.test.py  -> rc=1
    -> PYTHONOPTIMIZE=1 ctest -R scan_tools_regression -> Failed

and, before the rewrite, the same injection in an `assert` statement:

    -> PYTHONOPTIMIZE=1 ctest -R scan_tools_regression -> Passed

The ruler, and why it is the ruler:

The tempting check is "does the test set PYTHONOPTIMIZE=0 / unset it?".  That
asks about the environment, and the environment is not the property under
audit: `python3 -O` deletes the same statements with no environment variable
involved, and a test that runs the script through a shell, a subprocess or a
different interpreter is not covered by a CTest property at all.  The property
that actually holds or fails is a property of the *source*: does this file
signal failure with a construct the compiler can remove?  So this checker
reads the source, with `ast`, and asks that one question.

Which sources: every `.py` under `tools/`, plus everything those files import
from this repository.  The universe is deliberately wider than "the scripts a
CTest line names", because the suite does not only name scripts -- it runs
them as subprocesses.  `tools/scan_tools.test.py` executes `tools/cpp_scan.py`
through `subprocess.run`; an assert in that file is exactly as strippable as
one in the script that spawns it, and neither a command line nor an import
closure contains it.  The control that proved this: an assert appended to
`tools/cpp_scan.py` was reported as clean by a version of this checker that
followed only imports.  An assert that no interpreter flag can remove costs
nothing, so the resolution is to make the whole tree uniform rather than to
narrow the question until the answer is empty.

Exit codes: 0 clean, 1 a driven script still asserts, 2 cannot read the input
(a tree with no `src/`, or a CTest listing that will not parse).  "No build to
read" is a different answer from "no defect found", and collapsing them is
how an unconfigured tree comes to look clean.
"""

from __future__ import annotations

import ast
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A python invocation in a shell script: `python3 x.py`, `"$PY" y.py`.
#
# The interpreter name must be followed by something other than a word
# character or `-`: without that lookahead `python3?` also matches the bare
# word "python", and `tools/gate.sh` says "python" in prose -- its own
# EXP_CTEST comment is a 8,299-character English sentence about commits that
# mention 29 `.py` file names, and the first version of this reader took the
# whole sentence for a command line and called every name on it a driven
# script.  The reading was safe in direction (it widened the set, it never
# narrowed it) and wrong anyway: 45 scripts against the 22 the gate really
# runs.  `python3 -O x.py` still matches -- the lookahead rejects a word, and
# `-` is not one.
_PY_CALL = re.compile(r"""\bpython3?(?:\.\d+)?(?![-\w])([^|;&\n]*)""")
_PY_ARG = re.compile(r"""([\w./$-]+\.py)\b""")
# The interpreter name inside single or double quotes, for `"$PY" y.py`: a
# variable holding an interpreter is not the word "python" and the regex above
# does not see it, so the caller's quoting is stripped before the match.
_PY_QUOTED = re.compile(r"""(["'])(\$?[A-Za-z_]\w*)\1""")


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def _parse(path: str):
    """The AST of a file, or None when it does not parse.

    A file that does not parse cannot be driven by a working test either, but
    it is not this checker's business to say so -- returning None keeps a
    syntax error in an unrelated script from being reported as an assert.
    """
    try:
        return ast.parse(_read(path), filename=path)
    except (SyntaxError, ValueError):
        return None


def ctest_scripts(build_dir: str) -> set[str]:
    """Every `.py` a CTest test executes, as absolute paths.

    Read from the test listing rather than from CMakeLists.txt: the listing is
    what CTest will actually run, with the variables already expanded.
    """
    listing = subprocess.run(
        ["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
        capture_output=True,
        text=True,
    )
    if listing.returncode != 0 or not listing.stdout.strip():
        raise RuntimeError("ctest --show-only=json-v1 produced nothing")
    data = json.loads(listing.stdout)
    found: set[str] = set()
    for test in data.get("tests", []):
        for token in test.get("command") or []:
            if not isinstance(token, str) or not token.endswith(".py"):
                continue
            path = os.path.abspath(token)
            if os.path.isfile(path):
                found.add(path)
    return found


def _shell_code(line: str) -> str:
    """The executable part of a shell line: everything before an unquoted `#`.

    `tools/gate.sh` documents itself in comments, and a comment is the one
    place a `.py` name appears without anything running it.  Quoting is
    tracked so that a `#` inside a string stays code.
    """
    quote = ""
    index = 0
    while index < len(line):
        char = line[index]
        if quote:
            if char == quote:
                quote = ""
        elif char in "\"'":
            quote = char
        elif char == "#" and (index == 0 or line[index - 1] in " \t"):
            return line[:index]
        index += 1
    return line


def gate_scripts(repo_root: str) -> set[str]:
    """Every `.py` tools/gate.sh invokes directly.

    The gate runs checkers that CTest does not, so leaving them out would
    measure the suite and call it the tree.
    """
    gate = os.path.join(repo_root, "tools", "gate.sh")
    if not os.path.isfile(gate):
        return set()
    found: set[str] = set()
    for line in _read(gate).splitlines():
        code = _shell_code(line)
        # Only the span after the interpreter name is searched: a `.py` earlier
        # on the line is not an argument to this invocation.
        for match in _PY_CALL.finditer(code):
            for token in _PY_ARG.findall(match.group(1)):
                token = token.replace("$REPO_ROOT/", "").replace("${REPO_ROOT}/", "")
                for candidate in (os.path.join(repo_root, token), os.path.abspath(token)):
                    if os.path.isfile(candidate):
                        found.add(os.path.abspath(candidate))
                        break
    return found


def repo_module_for(name: str, repo_root: str, importer: str) -> str | None:
    """Resolve an imported name to a file in this repository, if it is one.

    Only intra-repository imports are followed: the standard library and
    site-packages are not ours to change, and reporting them would make the
    checker's output unactionable.
    """
    tail = name.replace(".", os.sep)
    roots = [
        os.path.dirname(importer),
        os.path.join(repo_root, "tools"),
        repo_root,
    ]
    for root in roots:
        for candidate in (
            os.path.join(root, tail + ".py"),
            os.path.join(root, tail, "__init__.py"),
        ):
            if os.path.isfile(candidate):
                return os.path.abspath(candidate)
    return None


def imported_modules(path: str, repo_root: str) -> set[str]:
    """The repository files a script imports, transitively."""
    seen: set[str] = set()
    queue = [path]
    while queue:
        current = queue.pop()
        if current in seen:
            continue
        seen.add(current)
        tree = _parse(current)
        if tree is None:
            continue
        for node in ast.walk(tree):
            names: list[str] = []
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                names = [node.module]
                names += ["%s.%s" % (node.module, a.name) for a in node.names]
            for name in names:
                resolved = repo_module_for(name, repo_root, current)
                if resolved and resolved not in seen:
                    queue.append(resolved)
    seen.discard(path)
    return seen


# A file that does not parse can still be SCANNED, and the scan says whether the
# file would have had something to report.  It is a text test and it is wrong in
# both directions (an `assert` in a string, an `assert` split across lines), so it
# decides only whether to NAME the file -- never whether it has a defect.
_ASSERT_SHAPED = re.compile(r"(?m)^[ \t]*assert[ \t(]")


def assert_shaped_text(path: str) -> bool:
    """Does this file LOOK like it carries an assert statement?

    Used only for files that do not parse, where the AST is unavailable and the
    choice is between naming the file and dropping it silently.
    """
    try:
        return bool(_ASSERT_SHAPED.search(_read(path)))
    except OSError:
        return False


def assert_lines(path: str) -> list[int]:
    """Line numbers of every `assert` statement in a file.

    `ast`, not a regex: the word appears in docstrings (this file's own
    docstring says "assert" a dozen times), in comments, and in string
    literals, and none of those can be deleted by a flag while all of them
    would be reported by a text search.
    """
    tree = _parse(path)
    if tree is None:
        return []
    return sorted(node.lineno for node in ast.walk(tree) if isinstance(node, ast.Assert))


def audit(repo_root: str, build_dir: str) -> tuple[list[dict], list[str], dict]:
    """(defects, notes, stats) for every driven script and the modules it imports."""
    driven = ctest_scripts(build_dir) | gate_scripts(repo_root)
    if not driven:
        raise RuntimeError("no Python script is driven by CTest or tools/gate.sh")

    universe = sorted(
        os.path.abspath(path)
        for path in glob.glob(os.path.join(repo_root, "tools", "**", "*.py"), recursive=True)
    )
    universe += sorted(driven - set(universe))

    seen: set[str] = set()
    defects: list[dict] = []
    notes: list[str] = []
    unreadable: list[str] = []
    for script in universe:
        for path in [script] + sorted(imported_modules(script, repo_root)):
            if path in seen:
                continue
            seen.add(path)
            # A FILE THAT DOES NOT PARSE IS NOT A FILE WITH NO ASSERTS.
            # `assert_lines` returns [] for both, so a syntax error silently
            # removed the file from the audit -- measured: a helper containing
            # `assert False` behind a syntax error was read, reported as having
            # no asserts, and the checker stayed green.  It is counted apart and
            # named, because "cannot read" and "nothing wrong" are the two
            # answers this whole file exists to keep separate.
            if _parse(path) is None and assert_shaped_text(path):
                unreadable.append(os.path.relpath(path, repo_root))
                continue
            lines = assert_lines(path)
            if not lines:
                continue
            if path == script:
                kind = "under tools/"
            else:
                kind = "imported by %s" % os.path.relpath(script, repo_root)
            defects.append(
                {
                    "source": os.path.relpath(path, repo_root),
                    "lines": lines,
                    "kind": kind,
                }
            )
    stats = {"driven": len(driven), "tools": len(universe), "read": len(seen)}
    notes.append(
        "%d script(s) driven by the suite, %d file(s) under tools/ and the "
        "scripts outside it they reach, %d file(s) read including imports"
        % (stats["driven"], stats["tools"], stats["read"])
    )
    stats["unreadable"] = sorted(unreadable)
    return defects, notes, stats


def self_test() -> int:
    """Run the checker's own decision procedure over sources whose answer is known.

    Each case is a source file plus the verdict it must produce.  A checker
    that cannot be made to say "defect" on a source that has one is not a
    checker; the interesting cases are the two the naive text search gets
    wrong.
    """
    cases = [
        (
            "assert at module level",
            "x = 1\nassert x == 1, 'one'\n",
            True,
        ),
        (
            "assert inside a function",
            "def f():\n    y = 2\n    assert y == 2\n",
            True,
        ),
        (
            "the rewrite the repository now uses",
            "x = 1\nif not (x == 1): raise AssertionError('one')\n",
            False,
        ),
        (
            "the word assert in a docstring",
            'def f():\n    """This must assert the thing."""\n    return 1\n',
            False,
        ),
        (
            "the word assert in a comment",
            "x = 1\n# assert x == 1\n",
            False,
        ),
        (
            "the word assert in a string literal",
            "msg = 'assert x == 1'\n",
            False,
        ),
        (
            "an assert in a module the script imports",
            "import helper\nhelper.run()\n",
            True,
        ),
        (
            "an assert in a sibling script that is executed, not imported",
            "import subprocess, sys\nsubprocess.run([sys.executable, 'sibling.py'])\n",
            True,
        ),
    ]

    temp = tempfile.mkdtemp(prefix="check_python_asserts_")
    failures = 0
    for index, (name, source, expect_defect) in enumerate(cases):
        root = os.path.join(temp, "case%d" % index)
        os.makedirs(os.path.join(root, "tools"))
        script = os.path.join(root, "tools", "case.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(source)
        if index == len(cases) - 2:
            # The imported module: reached through `import helper`.
            with open(os.path.join(root, "tools", "helper.py"), "w", encoding="utf-8") as handle:
                handle.write("def run():\n    assert True\n")
        if index == len(cases) - 1:
            # The executed sibling: never imported, so only the tools/ sweep
            # can see it.  This is the control that caught the narrow version.
            with open(os.path.join(root, "tools", "sibling.py"), "w", encoding="utf-8") as handle:
                handle.write("x = 1\nassert x == 1\n")
        found = bool(assert_lines(script))
        if index >= len(cases) - 2:
            found = found or any(
                assert_lines(module) for module in imported_modules(script, root)
            )
        if index == len(cases) - 1:
            found = found or any(
                assert_lines(path)
                for path in glob.glob(os.path.join(root, "tools", "**", "*.py"), recursive=True)
            )
        verdict = "catches" if found else "clean"
        wanted = "catches" if expect_defect else "clean"
        ok = found == expect_defect
        if not ok:
            failures += 1
        print(
            "PROBE-PYASSERT-SELFTEST|case=%d|%-42s|verdict=%s|want=%s|%s"
            % (index + 1, name, verdict, wanted, "PASS" if ok else "FAIL")
        )

    import shutil

    shutil.rmtree(temp, ignore_errors=True)

    # The gate reader's own controls.  These do not go through `audit` -- they
    # are about what `gate_scripts` calls an invocation, which is the reading
    # that was wrong.  Case A is the defect measured on the real tree: an
    # 8,299-character comment in tools/gate.sh names 29 `.py` files and runs
    # none of them, and the first version of this reader called all 29 driven.
    gate_cases = [
        (
            "a .py named in a shell comment is not driven",
            "#   a note: python3 tools/not_run.py was once considered\n"
            "python3 tools/real.py\n",
            {"tools/real.py"},
            {"tools/not_run.py"},
        ),
        (
            "a .py on the line before the interpreter is not its argument",
            "grep tools/earlier.py \"$f\" && python3 tools/later.py\n",
            {"tools/later.py"},
            {"tools/earlier.py"},
        ),
        (
            "`python3 -O x.py` is still an invocation",
            "python3 -O tools/underflag.py\n",
            {"tools/underflag.py"},
            set(),
        ),
        (
            "`python3-config --libs x.py` is not one (the name is glued to a dash)",
            "python3-config --ldflags tools/glued.py\n",
            set(),
            {"tools/glued.py"},
        ),
    ]
    for index, (name, text, wanted, decoys) in enumerate(gate_cases):
        root = os.path.join(temp, "gate%d" % index)
        os.makedirs(os.path.join(root, "tools"))
        with open(os.path.join(root, "tools", "gate.sh"), "w", encoding="utf-8") as handle:
            handle.write(text)
        # The decoys must exist as real files.  A control whose bait is missing
        # is a control that a broken reader walks straight past: the first
        # version of these cases named `tools/not_run.py` without creating it,
        # and the `os.path.isfile` filter downstream swallowed exactly the
        # defect the case was written to catch -- all three mutations below
        # stayed green.  The measurement caught the control, not the reader.
        for path in re.findall(r"""([\w./$-]+\.py)""", text):
            if path in wanted or path in decoys:
                with open(os.path.join(root, path), "w", encoding="utf-8") as handle:
                    handle.write("x = 1\n")
        found = {os.path.relpath(path, root) for path in gate_scripts(root)}
        ok = found == wanted
        if not ok:
            failures += 1
        print(
            "PROBE-PYASSERT-SELFTEST|gate=%d|%-60s|found=%s|want=%s|%s"
            % (
                index + 1,
                name,
                ",".join(sorted(found)) or "-",
                ",".join(sorted(wanted)) or "-",
                "PASS" if ok else "FAIL",
            )
        )
    shutil.rmtree(temp, ignore_errors=True)

    # A FILE THAT DOES NOT PARSE IS NAMED, not counted clean (§10.269).  Both
    # directions: the text scan has to see an assert in a broken file, and has
    # to stay quiet on a broken file that has none.
    broken_with = 'def f(:\n    assert False\n'
    broken_without = 'def f(:\n    return 1\n'
    scratch = tempfile.mkdtemp(prefix="check_python_asserts_unreadable_")
    try:
        p1 = os.path.join(scratch, "with.py")
        p2 = os.path.join(scratch, "without.py")
        open(p1, "w", encoding="utf-8").write(broken_with)
        open(p2, "w", encoding="utf-8").write(broken_without)
        if _parse(p1) is not None:
            failures.append("the fixture must not parse, or it tests nothing")
        if not assert_shaped_text(p1):
            failures.append("an assert in a broken file must be seen by the text scan")
        if assert_shaped_text(p2):
            failures.append("a broken file with no assert must not be named")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    total = len(cases) + len(gate_cases) + 3
    print(
        "PROBE-PYASSERT-SELFTEST-VERDICT|cases=%d|red=%d|verdict=%s"
        % (total, failures, "PASS" if failures == 0 else "FAIL")
    )
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    build_dir = os.path.join(REPO_ROOT, "build")
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])
        if token == "--build-dir" and index + 1 < len(argv):
            build_dir = os.path.abspath(argv[index + 1])

    if not os.path.isdir(os.path.join(repo_root, "tools")):
        print(
            "check_python_asserts: %s does not look like the repository "
            "(no tools/ under it)" % repo_root
        )
        return 2

    try:
        defects, notes, stats = audit(repo_root, build_dir)
    except (RuntimeError, OSError, ValueError) as problem:
        print("check_python_asserts: cannot read the input: %s" % problem)
        return 2

    for rel in stats.get("unreadable", []):
        print(
            "UNREADABLE %s: does not parse, and its text looks like it carries an "
            "assert -- this checker cannot say whether that assert is strippable, "
            "so it names the file instead of counting it as clean" % rel
        )
    for defect in defects:
        print(
            "WEAK %s: %d assert statement(s) -- a failure here would still be a "
            "green test under PYTHONOPTIMIZE=1" % (defect["source"], len(defect["lines"]))
        )
        print("       lines: %s" % ", ".join(str(n) for n in defect["lines"]))
        print("       found %s" % defect["kind"])
        print(
            "       state the expectation with `if not (<cond>): raise "
            "AssertionError(<msg>)`, which no interpreter flag removes"
        )
    for note in notes:
        print("NOTE %s" % note)

    print(
        "check_python_asserts: %d script(s) driven by the suite, %d file(s) "
        "under tools/, %d file(s) read including imports, %d still asserting."
        % (stats["driven"], stats["tools"], stats["read"], len(defects))
    )
    # A file this checker could not read is an answer of its own, and the exit
    # code has to carry it: "0 still asserting" next to an UNREADABLE line is the
    # same conflation the docstring separates for "no build to read".
    unreadable = stats.get("unreadable", [])
    if unreadable:
        print(
            "check_python_asserts: %d file(s) could not be parsed and look like they carry "
            "an assert; that is not a clean answer" % len(unreadable)
        )
    return 1 if (defects or unreadable) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
