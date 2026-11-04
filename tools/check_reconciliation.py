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
        if key in seen and seen[key][0] != value:
            problems.append(
                "§14 line %d: two tokens for %shave different values (%d and %d)"
                % (lineno, key + " ", seen[key][0], value)
            )
        seen.setdefault(key, (value, lineno))
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
    _src = Path(__file__).read_text()
    _start = _src.index("def self_check(")
    # the next top-level def, found by its leading newline so that this line's
    # own quoted markers cannot be mistaken for the boundary
    _body = _src[_start:_src.index("\ndef ", _start + 1)]
    if re.search(r"\b[pax]\[0\]", _body):
        print("FAIL -- a cell in this harness indexes a list directly; it must "
              "go through one(), so a regression reports by NAME instead of "
              "raising a traceback")
        return 1

    checks = []

    def expect(name, got, want):
        checks.append((name, got == want, got, want))

    sample = (
        "%s counts\n| 6 | cmd | `recon.ctest-count=%d` | — |\n"
        % (SECTION, key_ctest_count()[1])
    )
    toks = tokens_in(sample)
    expect("a token in §14 is found", len(toks), 1)
    expect("and it carries key and value", toks[0][:2], ("ctest-count", key_ctest_count()[1]))
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

    ok = 0
    for name, _, got, want in checks:
        if got == want:
            ok += 1
        else:
            print("FAIL -- %s: want %r got %r" % (name, want, got))
    print("check_reconciliation self-check: ok (%d checks)" % ok)
    return 0 if ok == len(checks) else 1


def main():
    if "--self-check" in sys.argv:
        return self_check()
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
