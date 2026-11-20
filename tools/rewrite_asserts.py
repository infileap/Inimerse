#!/usr/bin/env python3
"""rewrite_asserts.py -- turn `assert` statements into raises that survive -O.

Why this exists (docs/STATUS.md 10.241):

Python's `assert` statement is removed entirely when the interpreter runs
with `-O` or with `PYTHONOPTIMIZE` set.  It is not a cheaper assertion; it
is a different one that does not exist in that mode.  Four test suites in
this repository use `assert` as their ONLY way to fail:

    tools/cli_incremental.test.py   (30 asserts)
    tools/inim.test.py              (22 asserts)
    tools/release_verify.test.py    (10 asserts)
    tools/scan_tools.test.py        (15 asserts)

The defect was demonstrated, not argued.  `tools/scan_tools.test.py` with
`assert False, "INJECTED"` added to its main():

    $ python3 tools/scan_tools.test.py
    AssertionError                       rc=1   (the test fails, as it must)
    $ PYTHONOPTIMIZE=1 python3 tools/scan_tools.test.py
    scan tools: ok                      rc=0   (the test reports success)

and through CTest, where the same script is the whole of the
`scan_tools_regression` test:

    $ ctest -R scan_tools_regression                 -> Failed
    $ PYTHONOPTIMIZE=1 ctest -R scan_tools_regression -> Passed

A green CTest line that an environment variable can turn on and off is not a
green CTest line.

The rewrite is mechanical and the behaviour is identical: `assert cond, msg`
raises `AssertionError(msg)` when `cond` is false, and does nothing when it
is true -- which is exactly what

    if not cond:
        raise AssertionError(msg)

does, in every mode, optimized or not.  The difference is that the second
one is still there when the interpreter is told to skip assertions.

What this file does NOT do: it does not touch `assert` inside a docstring, in
a comment, or in a string literal, and it does not rewrite `assert` used as a
bare statement with no message beyond adding the guard.  It reports what it
changed and what it refused to change.  A comment trailing the statement stays
on the rewritten line: the rewrite replaces the whole physical line, and what
it did not mention is not its to delete.

Exit codes: 0 nothing left to rewrite (or --check found nothing wrong),
1 a rewrite was still needed under --check, 2 the file could not be read.
`--self-test` runs this module's own claims -- the shape of the rewrite, what
it leaves alone, and that the rewritten form still fails under `-O`.
"""

from __future__ import annotations

import argparse
import ast
import os
import re
import subprocess
import sys

# An assert statement, in the shape it actually takes in source: `assert
# <expr>` or `assert <expr>, <expr>`.  The expression part is matched by
# nesting-aware scanning, not by a regex over the whole line, because an
# assert can span several lines and can contain commas inside brackets.
ASSERT_RE = re.compile(r"^(?P<indent>[ \t]*)assert(?![ \t]*=)(?P<rest>.*)$")


def _expression_source(source: str, node: ast.AST) -> str:
    """The source text of an expression, taken from the AST's own positions.

    `ast.get_source_segment` returns exactly the expression -- no trailing
    comment, no stray comma -- which is what the rewrite needs.  It needs
    `padded=False` so that a multi-line expression keeps its own line breaks
    rather than being indented into one.
    """
    segment = ast.get_source_segment(source, node, padded=False)
    if segment is None:
        return ""
    return segment.strip()


def _split_top_level(text: str) -> tuple[str, str | None]:
    """Split `expr, message` at the first comma that is not inside brackets."""
    depth = 0
    in_string: str | None = None
    index = 0
    while index < len(text):
        char = text[index]
        if in_string:
            if char == "\\":
                index += 2
                continue
            if char == in_string:
                in_string = None
            index += 1
            continue
        if char in "\"'":
            in_string = char
            index += 1
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            return text[:index], text[index + 1 :]
        index += 1
    return text, None


def _logical_lines(source: str) -> list[tuple[int, str]]:
    """The physical lines of the source that are not inside a string.

    A docstring or a triple-quoted block is replaced by empty lines so that
    the line numbers of everything after it stay correct.
    """
    lines = source.splitlines()
    cleaned: list[str] = []
    in_triple: str | None = None
    for line in lines:
        stripped = line.strip()
        if in_triple:
            cleaned.append("")
            if in_triple in line:
                in_triple = None
            continue
        for quote in ('"""', "'''"):
            if quote in line:
                # A triple quote opens on this line; the rest of the line is
                # still code only if the quote closes on the same line.
                if line.count(quote) % 2 == 0:
                    cleaned.append(line)
                    break
                in_triple = quote
                cleaned.append("")
                break
        else:
            cleaned.append(line)
    return list(enumerate(cleaned, start=1))


def rewrite_source(source: str) -> tuple[str, list[tuple[int, str]], list[tuple[int, str]]]:
    """Rewrite the assert statements in `source`.

    Returns (new_source, changed, refused).  `changed` and `refused` are
    lists of (line_number, description).
    """
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return source, [], []

    lines = source.splitlines(keepends=True)
    # Collect the asserts, with the source segment of each.
    targets: list[ast.Assert] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Assert):
            targets.append(node)
    if not targets:
        return source, [], []

    # Work from the bottom up so that earlier offsets stay valid.
    changed: list[tuple[int, str]] = []
    refused: list[tuple[int, str]] = []
    for node in sorted(targets, key=lambda n: n.lineno, reverse=True):
        start = node.lineno - 1
        # The statement may span lines; AST knows where it ends.
        end = node.end_lineno - 1
        segment = "".join(lines[start : end + 1])
        # The indentation is the whitespace before the `assert` keyword on its
        # own line.  The segment may span lines, so take the first physical
        # line and measure its leading whitespace -- a multi-line assert's
        # continuation lines are not part of the indentation.
        first_line = segment.split("\n", 1)[0]
        indent = first_line[: len(first_line) - len(first_line.lstrip())]
        # The condition and the message are taken from the AST, not from the
        # text.  The text is not enough: a trailing comment after the message
        # ("..., dl[:8]  # DWARF version 5") would be swept into the message
        # and the rewritten line would not parse.
        condition = _expression_source(source, node.test)
        message = _expression_source(source, node.msg) if node.msg is not None else None
        if not condition:
            refused.append((node.lineno, "assert with no condition"))
            continue
        # A single line, so that the rewrite does not move any line number in
        # the file: docs/ cites these scripts by line, and a rewrite that
        # shifted them would break every one of those citations.
        if message is None:
            replacement = "%sif not (%s): raise AssertionError" % (indent, condition)
        else:
            replacement = "%sif not (%s): raise AssertionError(%s)" % (
                indent,
                condition,
                message,
            )
        # A trailing comment after the statement is not part of the AST, so
        # it sits outside `end_col_offset` on the statement's last physical
        # line.  The rewrite replaces the whole physical line, and a
        # mechanical rewrite must not delete what it did not mention: the
        # rewritten line keeps the comment verbatim.
        trailing = lines[end][node.end_col_offset :].rstrip("\n")
        if trailing.lstrip().startswith("#"):
            replacement += trailing
        lines[start : end + 1] = [(replacement + "\n")]
        changed.append((node.lineno, condition[:60]))

    changed.reverse()
    refused.reverse()
    return "".join(lines), changed, refused


def count_asserts(source: str) -> int:
    """How many assert statements the source has."""
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return len(re.findall(r"^\s*assert\s", source, re.M))
    return sum(1 for node in ast.walk(tree) if isinstance(node, ast.Assert))


# The mechanical shape, for both assert forms and for the case the rewrite
# was nearly silent about: a comment after the statement survives it.
SELFTEST_SHAPES: list[tuple[str, str]] = [
    ("assert x\n", "if not (x): raise AssertionError\n"),
    ("assert x, y\n", "if not (x): raise AssertionError(y)\n"),
    (
        "def f():\n    assert x, y  # a note\n    return 1\n",
        "def f():\n    if not (x): raise AssertionError(y)  # a note\n    return 1\n",
    ),
    (
        "x = 1  # not on an assert\nassert x\n",
        "x = 1  # not on an assert\nif not (x): raise AssertionError\n",
    ),
    (
        "def f():\n    assert x and (\n        y\n    ), 'm'  # a note\n    return 1\n",
        "def f():\n    if not (x and (\n        y\n    )): raise AssertionError('m')  # a note\n"
        "    return 1\n",
    ),
]

# The word `assert` in places that are not statements.  None of these may be
# rewritten, and the count that a text search would report on them is the
# reason this tool walks the AST rather than the lines.
SELFTEST_UNTOUCHED: list[str] = [
    '"""assert is not a statement here."""\n',
    "# assert is not a statement here\n",
    's = "assert is not a statement here"\n',
]

# The property the whole file is about: the rewritten form must still fail
# when the interpreter is told to drop assertions.  Run for real, in a
# subprocess with -O, so the claim is measured rather than argued.
SELFTEST_FAILS_UNDER_O: list[str] = [
    "assert 1 == 2, 'boom'\n",
    "def f():\n    assert 1 == 2, 'boom'\n    return 1\nf()\n",
]


def self_test() -> int:
    """Exercise the claims this module makes.  Returns 0 if every one held.

    The claims are the ones a reader has to be able to check without
    re-deriving them: the shape of the rewrite, what it leaves alone, and the
    one property that makes it worth doing at all -- that the rewritten form
    still fails under `-O`, where the original did not.
    """
    failures = 0

    for source, expected in SELFTEST_SHAPES:
        new, changed, refused = rewrite_source(source)
        if new != expected:
            print("SELFTEST shape: got %r, wanted %r" % (new, expected))
            failures += 1
        if not changed or refused:
            print("SELFTEST shape: rewrote %d, refused %d -- wanted 1 and 0"
                  % (len(changed), len(refused)))
            failures += 1

    for source in SELFTEST_UNTOUCHED:
        if count_asserts(source) != 0:
            print("SELFTEST untouched: counted an assert in %r" % source)
            failures += 1
        new, changed, refused = rewrite_source(source)
        if new != source or changed or refused:
            print("SELFTEST untouched: rewrote %r" % source)
            failures += 1

    for source in SELFTEST_FAILS_UNDER_O:
        # The rewritten form has to fail under -O.  The original has to pass
        # there -- that gap is the defect, and if the rewrite ever stops
        # closing it, this case is what says so.
        stripped = rewrite_source(source)[0]
        for label, text in (("original", source), ("rewritten", stripped)):
            proc = subprocess.run(
                [sys.executable, "-O", "-c", text],
                capture_output=True,
            )
            if label == "original":
                if proc.returncode != 0:
                    print("SELFTEST -O: the original already fails under -O")
                    failures += 1
            elif proc.returncode == 0:
                print("SELFTEST -O: the rewritten form passed under -O")
                failures += 1
        try:
            compile(stripped, "<selftest>", "exec")
        except SyntaxError as error:
            print("SELFTEST -O: the rewrite does not compile: %s" % error)
            failures += 1

    print(
        "REWRITE_ASSERTS-SELFTEST-VERDICT|cases=%d|shapes=%d|untouched=%d|under_o=%d|red=%d|verdict=%s"
        % (
            len(SELFTEST_SHAPES) + len(SELFTEST_UNTOUCHED) + len(SELFTEST_FAILS_UNDER_O),
            len(SELFTEST_SHAPES),
            len(SELFTEST_UNTOUCHED),
            len(SELFTEST_FAILS_UNDER_O),
            failures,
            "PASS" if not failures else "FAIL",
        )
    )
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", help="python files to rewrite")
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit 1 if any file still has assert statements",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the checks this module makes about itself, and exit",
    )
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.paths:
        parser.error("no files to rewrite, and --self-test was not given")

    failures = 0
    for path in args.paths:
        try:
            with open(path, encoding="utf-8") as handle:
                source = handle.read()
        except OSError as error:
            print("rewrite_asserts: cannot read %s: %s" % (path, error))
            return 2

        new_source, changed, refused = rewrite_source(source)
        for line, reason in refused:
            print("REFUSED %s:%d: %s" % (path, line, reason))
        if not changed:
            print("OK %s: no assert statements" % path)
            continue
        if args.check:
            print(
                "NEEDS-REWRITE %s: %d assert statement(s) at line(s) %s"
                % (path, len(changed), ", ".join(str(l) for l, _ in changed))
            )
            failures += 1
            continue
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(new_source)
        print(
            "REWRITTEN %s: %d assert statement(s) at line(s) %s"
            % (path, len(changed), ", ".join(str(l) for l, _ in changed))
        )
    if args.check:
        return 1 if failures else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
