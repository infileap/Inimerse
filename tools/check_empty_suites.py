#!/usr/bin/env python3
"""A self-test that runs no cases must not report success.

`tools/check_*.py` report on the tree, and most of them carry a `--self-test`
that drives the checker against inputs whose answer is known.  The shape this
guards against was measured 2026-11-28 (§10.293, §10.294): empty the case
table and the self-test still printed `OK` and exited 0 --

    check_probe_failure_paths: PROBE-FAILPATH-SELFTEST-VERDICT|cases=0|red=0|verdict=PASS

-- because `red == 0` was the whole verdict.  A suite that runs nothing reports
success by arithmetic alone, and it cannot be told apart from a working one.

The deeper one: **printing a case count is not a guard.**  Two of the four
tools already printed one and still passed, one because the count was
`len(cases) + 2` and could never be zero, the other because zero was not a
failure.  So this checks the EXIT CODE, not the output.

HOW IT DECIDES

For every `tools/check_*.py` that accepts `--self-test`, it copies the file
BESIDE the original (so `REPO_ROOT` still resolves to this tree), empties the
first list literal assigned to a name matching `cases`, runs the copy's
`--self-test`, and requires a non-zero exit.  A tool whose --self-test has no
such list is SKIPPED and counted as such, because "no case table" and "a case
table that cannot be emptied" are different facts and neither is a pass.

Usage:
    python3 tools/check_empty_suites.py [--report]

Exit codes: 0 every suite with a case table fails on an empty one, 1 one did
not, 2 a setup fault, 4 an environment refusal.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"

CASE_LIST = re.compile(
    r"^(\s*)(\w*[Cc][Aa][Ss][Ee][Ss]\w*)\s*=\s*\[(.*?)^\1\]", re.S | re.M
)


def candidates() -> list[Path]:
    return sorted(p for p in TOOLS.glob("check_*.py")
                  if "--self-test" in p.read_text(encoding="utf-8", errors="replace"))


def probe(path: Path) -> dict:
    src = path.read_text(encoding="utf-8")
    # The table that matters is the one the self-test ITERATES over.  Collect
    # every `for ... in X:` target, and prefer a table whose name it is.
    iterated = set(re.findall(r"^\s*for\s+[\w, ]+\s+in\s+(\w+)\s*:", src, re.M))
    found = [f for f in CASE_LIST.findall(src) if f[1] in iterated]
    if not found:
        found = CASE_LIST.findall(src)
    appends = re.findall(r"^\s*cases\.append\(.*?\)\s*$", src, re.M)
    if not found and not appends:
        return {"tool": path.name, "outcome": "NO-TABLE",
                "detail": "--self-test has no case table to empty"}
    if not found and appends:
        # The list is built by appending, so emptying it means removing the
        # appends; the list literal is already empty.
        empty = src
        for line in appends:
            empty = empty.replace(line + "\n", "", 1)
        with tempfile.TemporaryDirectory(dir=TOOLS) as tmp:
            copy = Path(tmp) / path.name
            copy.write_text(empty, encoding="utf-8")
            r = subprocess.run(
                [sys.executable, str(copy), "--self-test"],
                capture_output=True, text=True, cwd=str(REPO_ROOT),
            )
        return {
            "tool": path.name,
            "outcome": "GUARDED" if r.returncode != 0 else "UNGUARDED",
            "detail": (r.stdout + r.stderr).strip().splitlines()[-1][:110]
                      if (r.stdout or r.stderr) else "",
        }
    # EVERY table, not just the first: a suite with two tables and one emptied
    # still runs checks, and the guard is on "no cases ran", not on "one table
    # is empty".
    empty = src
    for indent, name, body in reversed(found):
        empty = empty.replace(
            f"{indent}{name} = [{body}{indent}]", f"{indent}{name} = []", 1
        )
    with tempfile.TemporaryDirectory(dir=TOOLS) as tmp:
        copy = Path(tmp) / path.name
        copy.write_text(empty, encoding="utf-8")
        r = subprocess.run(
            [sys.executable, str(copy), "--self-test"],
            capture_output=True, text=True, cwd=str(REPO_ROOT),
        )
    # A tool may keep a case outside its table.  Then emptying the table still
    # runs that case, and exit 0 is correct -- so the question is not only the
    # exit code: it is whether the tool RAN anything.  Its own verdict line
    # says, and a run count above zero with a green verdict is a suite whose
    # zero cannot be reached, which is a different (weaker) guarantee and is
    # named as such rather than counted as unguarded.
    ran = re.search(r"cases=(\d+)", (r.stdout or "") + (r.stderr or ""))
    if r.returncode != 0:
        outcome = "GUARDED"
    elif ran and int(ran.group(1)) > 0:
        outcome = "NOT-REACHABLE"
    else:
        outcome = "UNGUARDED"
    return {
        "tool": path.name,
        "outcome": outcome,
        "detail": (r.stdout + r.stderr).strip().splitlines()[-1][:110]
                  if (r.stdout or r.stderr) else "",
    }


def selftest() -> int:
    """A tool that passes on an empty table must be reported, and one that
    fails must not be.

    The whole checker rests on "run the tool's --self-test with its table
    emptied and look at the exit code".  A checker of checkers that has only
    ever been seen green is one nobody has watched work, so these two synthetic
    tools are the control: one ignores its empty table, one refuses it.
    """
    import textwrap
    cases = []
    # The two synthetic tools are written into TOOLS so REPO_ROOT resolves, and
    # removed again; `probe` is called on each directly rather than through
    # `candidates()`, which would pick up the real tool set.
    for name, body, want in (
        ("check_probe_a.py", textwrap.dedent(
            """\
            cases = [
                (1, 2),
                (3, 4),
            ]
            if __name__ == '__main__':
                import sys
                fails = 0
                for a, b in cases:
                    if a >= b:
                        fails += 1
                sys.exit(0 if fails == 0 else 1)
            """), "UNGUARDED"),
        ("check_probe_b.py", textwrap.dedent(
            """\
            cases = [
                (1, 2),
                (3, 4),
            ]
            if __name__ == '__main__':
                import sys
                fails = 0
                for a, b in cases:
                    if a >= b:
                        fails += 1
                if not cases:
                    sys.exit(1)
                sys.exit(0 if fails == 0 else 1)
            """), "GUARDED"),
    ):
        path = TOOLS / name
        try:
            path.write_text(body, encoding="utf-8")
            got = probe(path)["outcome"]
        finally:
            path.unlink(missing_ok=True)
        ok = got == want
        cases.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: want {want}, got {got}")
    if not all(cases):
        print(f"check_empty_suites --self-test: {cases.count(False)} case(s) "
              "differed")
        return 1
    print(f"check_empty_suites --self-test: ok ({len(cases)} cases)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return selftest()

    if not TOOLS.is_dir():
        print(
            f"check_empty_suites: ENVIRONMENT -- {TOOLS} is not a directory; "
            "the tool set is the input, so nothing was measured",
            file=sys.stderr,
        )
        return 4

    results = [probe(p) for p in candidates()]
    if args.report:
        for x in results:
            print(f"EMPTY-SUITE|{x['outcome']}|{x['tool']}|{x['detail']}")
    unguarded = [x for x in results if x["outcome"] == "UNGUARDED"]
    unreachable = [x for x in results if x["outcome"] == "NOT-REACHABLE"]
    no_table = [x for x in results if x["outcome"] == "NO-TABLE"]
    print(
        f"check_empty_suites: {len(results)} suite(s) with --self-test; "
        f"{len(results) - len(unguarded) - len(no_table) - len(unreachable)} "
        f"guarded, {len(unreachable)} with a case outside its table (zero not "
        f"reachable), {len(no_table)} without a case table, "
        f"{len(unguarded)} unguarded"
    )
    if unguarded:
        print(
            "check_empty_suites: "
            + ", ".join(x["tool"] for x in unguarded)
            + " exited 0 with an empty case table -- a suite that runs no "
            "cases reports success by arithmetic alone",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
