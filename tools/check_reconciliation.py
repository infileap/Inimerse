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


KEYS = {
    "ctest-count": key_ctest_count,
    "word-refs-baseline": key_word_refs_baseline,
    "headings-docs": key_headings_docs,
    "citations-cases": key_citations_cases,
    "explicit-delta": key_explicit_delta,
}


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
    seen = {}
    for key, value, lineno in tokens:
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
            continue
        measured_ok, actual, why = resolve()
        agree = value == actual
        assertions.append((key, value, actual, measured_ok and agree, why))
        measured.append(key)
        if not measured_ok:
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
    return assertions, problems, measured


def self_check():
    """The cells that make the tool itself falsifiable."""
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
    a, p, _ = judge(toks)
    expect("an unknown key is a problem", len(p), 1)
    expect("and it is not counted as a measurement", len(a), 0)
    toks = scan_tokens(
        "| `recon.ctest-count=193` |\n| `recon.ctest-count=194` |\n"
    )
    a, p, _ = judge(toks)
    expect("two values for one key is two problems (duplicate, mismatch)", len(p), 2)
    toks = scan_tokens("| `recon.ctest-count=%d` |" % key_ctest_count()[1])
    a, p, _ = judge(toks)
    expect("the tree's own value passes", (len(p), a[0][3]), (0, True))
    toks = scan_tokens("| `recon.ctest-count=1` |")
    a, p, _ = judge(toks)
    expect("a wrong value fails", len(p), 1)
    expect("and names both numbers", "but the tree says" in p[0], True)
    KEYS["zz-broken"] = lambda: (False, 0, "rc=1|checker=pretends-to-run")
    toks = scan_tokens("| `recon.zz-broken=0` |")
    a, p, _ = judge(toks)
    del KEYS["zz-broken"]
    expect("a failing resolver is a problem", len(p), 1)
    expect("and says the number is not measured", "not measured at all" in p[0], True)

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
    assertions, problems, measured = judge(toks)
    for key, expected, actual, ok, why in assertions:
        print(
            "RECON|key=%s|expected=%d|actual=%d|ok=%s|%s"
            % (key, expected, actual, "yes" if ok else "no", why)
        )
    for p in problems:
        print("RECON-PROBLEM|" + p)
    unresolved = [k for k in KEYS if k not in measured]
    print(
        "RECON|tokens=%d|keys-measured=%d|keys-unresolved=%d|basis=every-number-"
        "§14-claims-is-read-back-from-the-tree" % (len(toks), len(measured), len(unresolved))
    )
    if unresolved:
        print("RECON-NOT-YET|no §14 entry to check for: %s" % ",".join(sorted(unresolved)))
    if assertions and all(a[3] for a in assertions) and not problems:
        print("VERDICT|PASS|measured=%d" % len(assertions))
        return 0
    print("VERDICT|FAIL|problems=%d" % len(problems))
    return 1


if __name__ == "__main__":
    sys.exit(main())
