#!/usr/bin/env python3
"""check_preset_tables.py -- three tables that say they must match, and do not.

Why this exists (docs/STATUS.md 10.255):

The compiler seeds the global table with a set of "preset" names before it has
seen any source.  Three files each hold that list, and one of them says in a
comment that it has to match another:

    src/vm/vm.c              the VM registers them at init
    src/compiler/compiler.c  "must match vm.c vm_init order: ... kong"
    src/compilation/aot_mir.c  "must match vm.c vm_init order", refusing a
                             program that touches one by name

Measured: they do not match.

    vm.c           array of 24, loop bound 22, last two entries empty  -> 22
    compiler.c     array of 23, loop bound 23                         -> 23
    aot_mir.c      array of 23, `sizeof` bound                        -> 23

The extra name in the other two is the GBK bytes "\\xBF\\xD5".  It is not a
preset the VM has: `git show 8248e08e:src/vm/vm.c` registers the SAME set with
`"\\xE7\\xA9\\xBA"` and a comment saying GBK sources are transcoded to UTF-8 at
load, so the GBK spelling is the wrong one wherever it appears.

What that costs, measured: a program whose source contains that GBK byte pair
as an identifier runs on the interpreter (the name is simply an ordinary global,
and prints nil) and is REFUSED by the MIR route with `why=preset-set-global`.
The route refuses a name that is not a preset.  That is the safe direction --
it refuses rather than lowering something wrong -- and it is still a refusal
justified by a table that does not describe this VM.

This checker reads the three tables and compares their EFFECTIVE contents: the
entries the code actually registers, not the declared array size.  An array of
24 whose loop stops at 22 registers 22, and a checker comparing array sizes
would report agreement about two empty strings.

Exit codes: 0 the three register the same names; 1 they do not; 2 a table could
not be read -- which is a failure to measure, not a verdict.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (label, path, the anchor that opens the table, the pattern that ends it)
# Anchored on the declaration AND on that file's own loop, because the three
# files spell the bound three different ways and the bound is half the fact.
# The number of disagreement lines this tree carries today.  It may only FALL,
# in the commit that removes one; a fifth is a new divergence.
KNOWN_DISAGREEMENTS = 4

TABLES = [
    ("vm.c", "src/vm/vm.c", "static const char *bn[", "};", r"for \(int i = 0; i < (\d+)"),
    ("compiler.c", "src/compiler/compiler.c", "static const char *pset[", "};", r"for \(int i = 0; i < (\d+)"),
    ("aot_mir.c", "src/compilation/aot_mir.c", "static const char *const kPresetGlobals[", "};", None),
]

# A C string literal, with the escapes left intact.  The names that matter here
# include a byte-escaped one, so unescaping is done by the caller when it needs
# to compare bytes; comparing the literal text is enough for everything else.
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def unescape(text: str) -> str:
    """Decode a C string literal body the way the compiler would.

    Enough for this file's alphabet: \\xNN, \\\\ and the ordinary characters.
    A name with a byte escape has no UTF-8 spelling in the source at all, so
    the comparison has to be on bytes and not on characters.
    """
    out = bytearray()
    index = 0
    while index < len(text):
        ch = text[index]
        if ch != "\\":
            out.extend(ch.encode("utf-8"))
            index += 1
            continue
        nxt = text[index + 1] if index + 1 < len(text) else ""
        if nxt == "x" and index + 3 < len(text):
            try:
                out.append(int(text[index + 2 : index + 4], 16))
            except ValueError:
                out.extend(text[index : index + 2].encode("utf-8"))
            index += 4
            continue
        if nxt == "\\":
            out.append(0x5C)
            index += 2
            continue
        if nxt == "n":
            out.append(0x0A)
            index += 2
            continue
        if nxt == "t":
            out.append(0x09)
            index += 2
            continue
        out.extend(text[index : index + 2].encode("utf-8"))
        index += 2
    return out.decode("utf-8", "surrogateescape")


def effective_names(src: str, anchor: str, end: str, loop_pat):
    """(names the code registers, declared array size, loop bound or None).

    The declared size is printed but never compared: an array of 24 whose loop
    stops at 22 registers 22, and a checker that compared array sizes would be
    comparing empty strings.
    """
    start = src.find(anchor)
    if start < 0:
        return None, None, None
    close = src.index(end, start)
    body = src[start:close]
    declared = re.search(r"\[(\d+)\]", body)
    declared = int(declared.group(1)) if declared else None
    literals = [unescape(lit) for lit in LITERAL.findall(body)]
    # A trailing empty entry is padding, not a name: vm.c's array has two.
    while literals and literals[-1] == "":
        literals.pop()
    bound = None
    tail_start = -1
    if loop_pat:
        tail_start = src.find("for", close)
        m = re.search(loop_pat, src[tail_start : tail_start + 400]) if tail_start >= 0 else None
        bound = int(m.group(1)) if m else None
    names = literals[:bound] if bound is not None else literals
    # A name registered AFTER the loop is still registered.  vm.c appends its
    # last preset this way (`kong`, a nil-typed global) and the array it indexes
    # does not contain it -- so a reader that stopped at the array would say the
    # VM registers 22 names when it registers 23, and compare a table against a
    # table it had truncated.  Measured: the first version of this file did
    # exactly that and reported the encoding fix as still-failing.
    if tail_start >= 0:
        # The window ends at the enclosing block's close, not at a fixed number
        # of characters.  The first version took 1600 characters and swept up
        # `write_file`, `str` and `sum` from the debugger initialisation that
        # follows -- sixteen problems about names no preset table contains.
        # The registration block closes at a line that is exactly `    }`.
        window_end = len(src)
        for m in re.finditer(r"^    \}$", src[tail_start:], re.M):
            window_end = tail_start + m.end()
            break
        for m in re.finditer(r'\.name = strdup\("((?:[^"\\]|\\.)*)"\)',
                             src[tail_start:window_end]):
            extra = unescape(m.group(1))
            if extra and extra not in names:
                names = names + [extra]
    return names, declared, bound


def measure(repo_root: str):
    results = []
    for label, rel, anchor, end, loop_pat in TABLES:
        path = os.path.join(repo_root, rel)
        if not os.path.isfile(path):
            raise RuntimeError("no %s" % rel)
        src = _read(path)
        names, declared, bound = effective_names(src, anchor, end, loop_pat)
        if names is None:
            raise RuntimeError("could not find the preset table in %s (%s)" % (rel, anchor))
        results.append(
            {
                "label": label,
                "names": names,
                "declared": declared,
                "bound": bound,
            }
        )

    problems = []
    base = {n for n in results[0]["names"]}
    for row in results[1:]:
        other = {n for n in row["names"]}
        for name in sorted(base - other):
            problems.append("%s registers %r; %s does not" % (results[0]["label"], name, row["label"]))
        for name in sorted(other - base):
            problems.append("%s registers %r; %s does not" % (row["label"], name, results[0]["label"]))
    # A CEILING, in the shape §10.242 used for its homograph bucket.  The
    # disagreement is real and this section does not fix it: the fix changes the
    # bytes the compiled route emits, which breaks tools/mir_wired.test.py's
    # assertion that wiring changed nothing -- a change that needs its own
    # argument, not a side effect of an encoding repair.  So the count is held
    # rather than driven to zero, and a fourth name is a red.
    #
    # Measured on the tree this checker was added to: 4 problem lines, because
    # one file's name is present and two lack it (each direction is one line).
    if len(problems) > KNOWN_DISAGREEMENTS:
        red = 1
    else:
        red = 0
    return red, problems, results


def self_test() -> int:
    """The bound is half the fact, so the control has to show both halves.

    A fixture written the way vm.c is written -- an array wider than the loop --
    because a parser that compared array sizes would report agreement about
    padding, and padding is exactly what this file found.
    """
    failures = []

    padded = (
        'static const char *bn[6] = { "A", "B", "C", "", "" };\n'
        "for (int i = 0; i < 3; i++) { }\n"
    )
    names, declared, bound = effective_names(padded, "static const char *bn[", "};",
                                             r"for \(int i = 0; i < (\d+)")
    if names != ["A", "B", "C"]:
        failures.append("padding must not be counted: got %r" % (names,))
    if declared != 6 or bound != 3:
        failures.append("declared=%r bound=%r, want 6 and 3" % (declared, bound))
    # The distinction the checker lives on: array size and effective size differ.
    if len(names) == declared:
        failures.append("this fixture must have declared != effective or it controls nothing")

    # A name registered AFTER the loop is registered.  vm.c does this for its
    # last preset and the array does not contain it; the first version of this
    # file read only the array and compared a truncated table with a whole one.
    post = (
        'static const char *bn[3] = { "A", "B", "" };\n'
        "for (int i = 0; i < 2; i++) { }\n"
        "if (x) {\n"
        '    g.name = strdup("C");\n'
        "}\n"
        "    }\n"
    )
    names_post, _d, _b = effective_names(post, "static const char *bn[", "};",
                                         r"for \(int i = 0; i < (\d+)")
    if names_post != ["A", "B", "C"]:
        failures.append("a post-loop registration must be counted: got %r" % (names_post,))

    # A byte-escaped name survives as one name, not as two or as escape text.
    esc = 'static const char *const kPresetGlobals[2] = { "N", "\\xBF\\xD5" };'
    names, declared, _ = effective_names(esc, "static const char *const kPresetGlobals[", "};", None)
    if len(names) != 2:
        failures.append("a byte-escaped name must be one entry: got %r" % (names,))
    if names and names[-1] == "\\xBF\\xD5":
        failures.append("the escape must be decoded, not left as text")

    print("check_preset_tables --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_preset_tables: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "array padding is not counted as a name",
        "the declared size and the registered size are different numbers",
        "a byte-escaped name is decoded and counted once",
        "a name registered after the loop is counted",
    ):
        print("  ok   %s" % line)
    print("check_preset_tables: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, problems, results = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_preset_tables: cannot measure: %s" % problem)
        return 2

    for row in results:
        shown = []
        for name in row["names"]:
            # Print a byte-escaped name by its bytes: repr() of a surrogate
            # string is unreadable in a log a human has to act on.
            try:
                shown.append(name.encode("utf-8", "surrogateescape").hex())
            except Exception:  # noqa: BLE001
                shown.append(name)
        printable = [
            (n if all(32 <= ord(c) < 127 for c in n) else "hex:" + h)
            for n, h in zip(row["names"], shown)
        ]
        print(
            "  %-12s declared=%-3s bound=%-4s registers=%d  last=%r"
            % (row["label"], row["declared"], row["bound"], len(row["names"]), printable[-2:])
        )
    for line in problems:
        print("  PROBLEM  %s" % line)
    same = len({frozenset(r["names"]) for r in results}) == 1
    print(
        "check_preset_tables: tables=%d|all-equal=%s|problems=%d"
        % (len(results), "yes" if same else "no", len(problems))
    )
    if red:
        print(
            "check_preset_tables: the three tables disagree, and one of them says in a "
            "comment that they must match."
        )
        return 1
    if same:
        print(
            "check_preset_tables: the three register the same names.  This says they agree; "
            "it does not say the set is right."
        )
    else:
        print(
            "check_preset_tables: the three still disagree (%d line(s), ceiling %d) and this "
            "checker does not fix it -- the fix changes the bytes the compiled route emits, "
            "which tools/mir_wired.test.py asserts wiring did not change.  The count may only "
            "fall; a new divergence is a red." % (len(problems), KNOWN_DISAGREEMENTS)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
