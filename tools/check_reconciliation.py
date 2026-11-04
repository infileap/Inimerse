#!/usr/bin/env python3
"""The reconciliation checklist, made checkable.

docs/BOARD.md §14 lists six commands whose expected readings are written in
prose.  Prose expectations are not expectations: measured on 2026-11-04, every
number in that table could be changed to a wrong value and all seven docs
checkers still exited 0.  A checklist whose entries nothing can contradict is a
document, not a gate.

So the numbers that can be measured get a token: `recon.<key>=<value>`, and this
tool resolves every token against the tree and compares.  Three properties are
the point:

  * a token whose key has no resolver FAILS -- the key set is closed, so a typo
    in a key name is a red line rather than a silently unchecked word (the
    checklist's own §14 paragraph says a symbol anchor has no teeth; this is
    where one of them gets them);
  * a token whose value disagrees with the tree FAILS;
  * two tokens for the same key with different values FAIL.

The unresolved keys that remain are listed at the end of the run, not omitted:
`EXIT|unresolved=<n>` -- a checklist that only prints what it could check reads
as if it checked everything.

Usage:
    python3 tools/check_reconciliation.py            # the checklist
    python3 tools/check_reconciliation.py --self-check
"""

import ast
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARD = ROOT / "docs" / "BOARD.md"
SECTION = "## 14. 合并前后的对账清单"
TOKEN = re.compile(r"recon\.([a-z0-9-]+)=([0-9]+)")

# How each key is measured.  Every resolver returns (ok, actual, why).  `why` is
# printed either way: a passing line that does not say where its number came
# from is the same shape as a failing line that does not say why it failed.
def _run(cmd):
    p = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def key_ctest_count():
    n_add = sum(
        1 for line in (ROOT / "CMakeLists.txt").read_text().splitlines()
        if "add_test(" in line
    )
    rc, out = _run(["ctest", "--test-dir", str(ROOT / "build"), "-N"])
    m = re.search(r"Total Tests: (\d+)", out)
    n_ctest = int(m.group(1)) if m else -1
    gate = (ROOT / "tools" / "gate.sh").read_text()
    m = re.search(r'EXP_CTEST="\$\{EXP_CTEST:-(\d+)\}"', gate)
    n_exp = int(m.group(1)) if m else -1
    return (
        n_add == n_ctest == n_exp,
        n_add,
        "add_test=%d|ctest-N=%d|EXP_CTEST=%d%s"
        % (n_add, n_ctest, n_exp, "" if n_add == n_ctest == n_exp else "|three-numbers-disagree"),
    )


def _parse(pattern, text):
    m = re.search(pattern, text)
    return int(m.group(1)) if m else -1


def key_word_refs_baseline():
    rc, out = _run([sys.executable, "tools/check_word_refs.py"])
    n = _parse(r"baseline=(\d+)", out)
    return rc == 0 and n >= 0, n, "rc=%d|checker=check_word_refs.py" % rc


def key_headings_docs():
    rc, out = _run([sys.executable, "tools/check_docs_headings.py"])
    n = _parse(r"HEADINGS\|pin\|.*?docs=(\d+)", out)
    total = _parse(r"headings=(\d+)", out)
    return rc == 0 and n >= 0, n, "rc=%d|headings=%d" % (rc, total)


def key_citations_cases():
    rc, out = _run([sys.executable, "tools/citations.test.py"])
    n = _parse(r"cases=(\d+)", out)
    return rc == 0 and n >= 0, n, "rc=%d|checker=citations.test.py" % rc


def key_explicit_delta():
    rc, out = _run([sys.executable, "tools/check_line_refs.py"])
    n = _parse(r"explicit delta \+(\d+)", out)
    return rc == 0 and n >= 0, n, "rc=%d|checker=check_line_refs.py" % rc


# The registry holds NAMES, not function objects.  A rename of a resolver used
# to crash this file at import time, before anything was measured: the reading
# then did not exist at all, which is not the same as "this key cannot be
# computed" and is not what a gate should have to guess.  Looking the name up
# when the key is judged turns that into a booked resolver-failed.
KEYS = {
    "ctest-count": "key_ctest_count",
    "word-refs-baseline": "key_word_refs_baseline",
    "headings-docs": "key_headings_docs",
    "citations-cases": "key_citations_cases",
    "explicit-delta": "key_explicit_delta",
}


# Every place this file takes element N of something, booked by hand.  A
# positional read can be empty, so each one needs a reason here and a reviewer
# reads that reason in the diff.
#
# THE RULE FOR A REASON: a reason may assert structure only if an assertion
# backs it.  "It is the 5-tuple's [3]" is a claim about arity, so there is a
# cell that measures arity and prints the first answer whose length disagrees.
# A reason with no such backing must say so in the words "unverified: relies
# on the construction site" -- ugly and honest beats pretty and false (this
# file already carried "[1] is the value half" about a 3-tuple for a day).  The book is the assertion: a new read
# anywhere in the file -- helper included -- changes it and the change is visible.
POSITIONAL_READS = {
    "xs[0]": 1,      # one(): guarded by the ternary's own test, "xs[0] if xs else ''"
    "[][0]": 1,      # guarded(): proves a crash becomes a named sentence, not a traceback
    "book[text]": 1, # positional_reads' accumulator: book.get writes the key on the same line
    "r[3]": 1,       # judge() appends 5-tuples; [3] is the verdict slot (arity is a cell)
    "a[3]": 1,       # the same 5-tuples in main(), indexed inside a comprehension
    "key_ctest_count()[1]": 3,  # returns (agree, count, text); [1] is the count (arity is a cell)

}


def positional_reads(src):
    """The multiset of `name[int]` reads in a source text, from its syntax tree."""
    book = {}
    for node in ast.walk(ast.parse(src)):
        if not isinstance(node, ast.Subscript):
            continue
        sl = node.slice
        if isinstance(sl, ast.Slice):
            continue                      # a read of a range, not of element N
        if isinstance(sl, ast.Constant) and isinstance(sl.value, str):
            continue                      # a named lookup (books["tokens"]), not positional
        text = re.sub(r"\s+", " ", ast.get_source_segment(src, node) or "")
        book[text] = book.get(text, 0) + 1
    return book


def one(xs):
    """First element, or "" -- a self-check cell must report a named failure
    even when the thing it looked at is empty.  Indexing one(p) directly turned
    a regression into an IndexError traceback: red, but with no name."""
    return xs[0] if xs else ""


def scan_tokens(body):
    """Every recon token in `body`, as (key, value, line-number)."""
    out = []
    for lineno, line in enumerate(body.splitlines(), 1):
        for key, value in TOKEN.findall(line):
            out.append((key, int(value), lineno))
    return out


def tokens_in(text):
    """Every recon token in §14 of `text`, as (key, value, line-number)."""
    start = text.find(SECTION)
    if start < 0:
        return []
    return scan_tokens(text[start:])


def judge(tokens):
    """(assertions, problems, measured) -- the whole check, no printing."""
    assertions, problems, measured = [], [], []
    books = {"tokens": 0, "unknown": 0, "resolver-failed": 0}
    seen = {}
    for key, value, lineno in tokens:
        books["tokens"] += 1
        # The record is a named pair, not a 2-tuple read by position: the
        # construction now says what [0] used to mean, and a reader (or a
        # later edit) cannot swap the slots without the name disagreeing.
        prior = seen.setdefault(key, {"value": value, "line": lineno})
        if prior["value"] != value:
            problems.append(
                "§14 line %d: two tokens for %shave different values (%d and %d)"
                % (lineno, key + " ", prior["value"], value)
            )
        resolve = KEYS.get(key)
        if resolve is None:
            problems.append(
                "§14 line %d: 'recon.%s' has no resolver in this tool; the key "
                "set is closed so a typo cannot pass as an unchecked word"
                % (lineno, key)
            )
            books["unknown"] += 1
            continue
        resolve = (resolve if callable(resolve)
                   else globals().get(resolve))
        if resolve is None:
            books["resolver-failed"] += 1
            problems.append(
                "§14 line %d: 'recon.%s=%d' -- the key is registered here but "
                "the resolver it names does not exist; a missing resolver is a "
                "number nobody computed, not a key nobody declared"
                % (lineno, key, value)
            )
            continue
        try:
            measured_ok, actual, why = resolve()
        except Exception as exc:                      # noqa: BLE001 -- a resolver
            books["resolver-failed"] += 1             # that throws is a number
            problems.append(                          # this tool cannot read, and
                "§14 line %d: 'recon.%s=%d' -- the resolver for this key threw "
                "%s: %s, so this number is not measured at all"
                % (lineno, key, value, type(exc).__name__, exc)
            )
            continue
        agree = value == actual
        assertions.append((key, value, actual, measured_ok and agree, why))
        measured.append(key)
        if not measured_ok:
            books["resolver-failed"] += 1
            problems.append(
                "§14 line %d: 'recon.%s=%d' -- the resolver for this key failed "
                "(%s), so this number is not measured at all"
                % (lineno, key, value, why)
            )
        elif not agree:
            problems.append(
                "§14 line %d: 'recon.%s=%d' but the tree says %d (%s)"
                % (lineno, key, value, actual, why)
            )
    return assertions, problems, measured, books


def self_check():
    """The cells that make the tool itself falsifiable."""
    # Checked BEFORE any cell runs.  A cell that indexes a list directly turns
    # its own regression into an IndexError traceback: non-zero exit, no
    # defendant.  Red is not the same as named.  Order matters -- if this runs
    # after the cells, the crashing cell gets there first and the shape check
    # never reports.
    # Positional element reads are BOOKED, not pattern-matched.  The previous
    # check grepped one shape (p[0]) and missed _p[0], row[0], row[1], row[-1]
    # and every helper outside this function -- calm-forge measured all four
    # variants as green, which means the check named its own last mistake
    # rather than this class.  The book below is taken from the syntax tree of
    # the whole file, so a new positional read anywhere (helper included)
    # changes the book and the book has to be edited in the same diff.  The
    # question the check answers is not "does it look like p[0]" but "did
    # anyone write down every place we take element N of something".
    _live = positional_reads(Path(__file__).read_text())
    if _live != POSITIONAL_READS:
        _diff = sorted(
            "%s=%d here/%s booked" % (k, _live.get(k, 0), POSITIONAL_READS.get(k, "-"))
            for k in set(_live) | set(POSITIONAL_READS)
            if _live.get(k, 0) != POSITIONAL_READS.get(k, 0)
        )
        print("FAIL -- positional reads changed and nobody booked them: %s; a "
              "positional read can be empty, so it belongs on POSITIONAL_READS "
              "with a reason or behind one()" % "; ".join(_diff))
        return 1

    checks = []

    def expect(name, got, want):
        checks.append((name, got == want, got, want))
        LAST_CELL["cell"] = name

    sample = (
        "%s counts\n| 6 | cmd | `recon.ctest-count=%d` | — |\n"
        % (SECTION, key_ctest_count()[1])
    )
    toks = tokens_in(sample)
    expect("a token in §14 is found", len(toks), 1)
    expect("and it carries key and value", one(toks)[:2], ("ctest-count", key_ctest_count()[1]))
    # The book must see the class, not one shape: a helper (not the slice the
    # old check used), an underscore name, and an index other than 0.
    expect("a helper's positional read is booked",
           positional_reads("def helper():\n    row = ['a']\n    return row[0]\n"),
           {"row[0]": 1})
    expect("an underscore name is booked, not skipped by a word boundary",
           positional_reads("x = _p[0]\n"), {"_p[0]": 1})
    expect("an index other than zero is booked",
           positional_reads("x = row[-1]\n"), {"row[-1]": 1})
    expect("slices are reads of a different kind and stay out of the book",
           positional_reads("x = toks[1:3]\n"), {})


    toks = tokens_in("| `recon.ctest-count=999` |")
    expect("a token outside §14 is not found", len(toks), 0)
    toks = scan_tokens("| `recon.no-such-key=1` |")
    a, p, _, books = judge(toks)
    expect("an unknown key is a problem", len(p), 1)
    expect("and it is not counted as a measurement", len(a), 0)
    expect("and it is booked as unknown, so the summary can close", books["unknown"], 1)
    expect("and names the unknown key, not a failing resolver",
           "has no resolver in this tool" in one(p)
           and "the resolver it names does not exist" not in one(p), True)
    t_unknown = one(p)
    toks = scan_tokens(
        "| `recon.ctest-count=193` |\n| `recon.ctest-count=194` |\n"
    )
    a, p, _, _ = judge(toks)
    expect("two values for one key is two problems (duplicate, mismatch)", len(p), 2)
    toks = scan_tokens("| `recon.ctest-count=%d` |" % key_ctest_count()[1])
    a, p, _, _ = judge(toks)
    expect("the tree's own value passes", (len(p), one([r[3] for r in a])), (0, True))
    # The book believes [3] is a verdict and [1] is a count.  Those are claims
    # about arity and about slots, so they get their own cells -- and the red
    # line prints the first offender rather than a set that only says "not 5".
    _bad = [row for row in a if len(row) != 5]
    expect("every answer is the 5-tuple the book indexes, and the first one that "
           "is not prints itself",
           (len(_bad), repr(one(_bad))[:80] if _bad else ""), (0, ""))
    _kc = key_ctest_count()
    expect("the ctest resolver is (agree, count, text), and the slots hold what "
           "the book names them",
           (len(_kc), type(one(_kc[0:1])).__name__, type(one(_kc[1:2])).__name__,
            type(one(_kc[2:3])).__name__),
           (3, "bool", "int", "str"))

    toks = scan_tokens("| `recon.ctest-count=1` |")
    a, p, _, _ = judge(toks)
    expect("a wrong value fails", len(p), 1)
    expect("and names both numbers", "but the tree says" in one(p), True)
    KEYS["zz-missing-resolver"] = "zz_no_such_function"
    toks = scan_tokens("| `recon.zz-missing-resolver=0` |")
    a, p, _, books = judge(toks)
    del KEYS["zz-missing-resolver"]
    expect("a renamed resolver is booked, not crashed", books["resolver-failed"], 1)
    expect("and is not confused with an undeclared key", books["unknown"], 0)
    expect("and says the resolver it names does not exist",
           "the resolver it names does not exist" in one(p), True)
    t_renamed = one(p)

    KEYS["zz-raises"] = lambda: 1 // 0
    toks = scan_tokens("| `recon.zz-raises=0` |")
    a, p, _, books = judge(toks)
    del KEYS["zz-raises"]
    expect("a resolver that throws is one problem", len(p), 1)
    expect("and is booked as resolver-failed, not unknown",
           (books["resolver-failed"], books["unknown"]), (1, 0))
    expect("and says it threw rather than that the key is missing",
           "threw ZeroDivisionError" in one(p) and "has no resolver" not in one(p), True)
    t_raises = one(p)
    KEYS["zz-broken"] = lambda: (False, 0, "rc=1|checker=pretends-to-run")
    toks = scan_tokens("| `recon.zz-broken=0` |")
    a, p, _, books = judge(toks)
    del KEYS["zz-broken"]
    expect("a failing resolver is a problem", len(p), 1)
    expect("and says the number is not measured", "not measured at all" in one(p), True)
    expect("and it is booked as resolver-failed", books["resolver-failed"], 1)
    t_broken = one(p)
    # Same bucket, different sentences: downstream distinguishes these only by
    # their words, and words with no assertion drift.  So the four sentences
    # must be four, and each must name the line it was read on -- "there is a
    # PROBLEM" is not the property being claimed.
    expect("four ways a number goes missing print four different sentences",
           len({t_unknown, t_renamed, t_raises, t_broken}), 4)
    expect("and each sentence names the §14 line it was read on",
           all("§14 line " in x for x in
               (t_unknown, t_renamed, t_raises, t_broken)), True)

    # The harness proves the property it depends on: a raising cell becomes a
    # named sentence rather than a traceback.
    def guarded(name, thunk):
        try:
            thunk()
        except Exception as exc:                      # noqa: BLE001
            return "builtin.%s: %s" % (type(exc).__name__, exc)
        return "ok"

    expect("a crashing cell is reported by name and by its exception",
           guarded("old-style", lambda: [][0]),
           "builtin.IndexError: list index out of range")

    failed = []
    for name, passed, got, want in checks:
        if not passed:
            failed.append(name)
            print("FAIL -- %s: want %r got %r" % (name, want, got))
    if failed:
        # printing "ok (N checks)" next to a named FAIL told the reader the
        # harness passed while exit was 1 -- one of the two had to go
        print("FAIL -- %d of %d checks failed" % (len(failed), len(checks)))
        return 1
    print("check_reconciliation self-check: ok (%d checks)" % len(checks))
    return 0


LAST_CELL = {"cell": ""}


def main():
    if "--self-check" in sys.argv:
        # A cell that raises prints a traceback: the same exit code as a named
        # FAIL, with none of the information.  This turns every escape into the
        # one sentence it can still earn -- plus the last cell that did report,
        # which is where in the list the crash happened.
        try:
            return self_check()
        except Exception as exc:  # noqa: BLE001 -- that is the point of the net
            print("FAIL -- the self-check crashed while reading the cell after "
                  "%r: %s: %s" % (LAST_CELL["cell"] or "(before the first cell)",
                                  type(exc).__name__, exc))
            return 1
    text = BOARD.read_text()
    toks = tokens_in(text)
    assertions, problems, measured, books = judge(toks)
    for key, expected, actual, ok, why in assertions:
        print(
            "RECON|key=%s|expected=%d|actual=%d|ok=%s|%s"
            % (key, expected, actual, "yes" if ok else "no", why)
        )
    for p in problems:
        print("RECON-PROBLEM|" + p)
    unresolved = [k for k in KEYS if k not in measured]
    read_back = books["tokens"] - books["unknown"] - books["resolver-failed"]
    close = books["tokens"] == read_back + books["unknown"] + books["resolver-failed"]
    print(
        "RECON|tokens=%d|tokens-read-back=%d|tokens-unknown=%d|tokens-resolver-failed=%d"
        "|keys-measured=%d|keys-unresolved=%d|closed=%s"
        "|equation=tokens=tokens-read-back+tokens-unknown+tokens-resolver-failed"
        "|basis=every-number-§14-claims-is-read-back-from-the-tree"
        % (books["tokens"], read_back, books["unknown"], books["resolver-failed"],
           len({k for k in measured}), len(unresolved), "yes" if close else "no")
    )
    if unresolved:
        print("RECON-NOT-YET|no §14 entry to check for: %s" % ",".join(sorted(unresolved)))
    if not close:
        print("RECON-PROBLEM|the summary line does not close: %d token(s) are "
              "neither read back, nor unknown, nor resolver-failed"
              % (books["tokens"] - read_back - books["unknown"] - books["resolver-failed"]))
        problems.append("summary-line-not-closed")
    if assertions and all(a[3] for a in assertions) and not problems:
        print("VERDICT|PASS|measured=%d" % len(assertions))
        return 0
    print("VERDICT|FAIL|problems=%d" % len(problems))
    return 1


if __name__ == "__main__":
    sys.exit(main())
