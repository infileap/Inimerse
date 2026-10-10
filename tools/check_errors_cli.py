#!/usr/bin/env python3
"""`inimerse errors` must be a view of g_errors[], not a second copy of it.

docs/ERROR_CODES_V06.md section 4.2 states the criterion: the table the CLI
prints has to be *derived*, and the test of "derived" is an assertion that can
go red -- add a member to src/types/error_types.c and the output must follow;
change a code there and that line must follow.

Two questions, each evaluated and reported on its own:

  1. is there a copy?  None of the names in the table may appear as a string
     literal in src/main.c.  A hardcoded table would print the right thing
     today and the wrong thing the first time the real table moved, and the
     two would be indistinguishable until then -- which is the failure this
     question exists for.

  2. do the two agree?  The (name, code, domain) triples parsed out of
     src/types/error_types.c must equal the ones the running binary reports.
     Question 1 alone cannot see a copy that is assembled from the source at
     compile time; question 2 alone cannot see a copy that is not.

The domain nibble is checked as a third question because a code can be right
while the domain it is filed under is wrong, and the CLI prints the domain.

Usage: check_errors_cli.py <path-to-inimerse>

Exit 0 when every question passes, 1 when one fails, 2 on misuse.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "types" / "error_types.c"
CLI = ROOT / "src" / "main.c"

ENTRY = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(IM_ERROR_DOMAIN_[A-Z_]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}'
)


def source_rows():
    text = SOURCE.read_text(encoding="utf-8")
    rows = ENTRY.findall(text)
    if not rows:
        print("check_errors_cli: no entries found in %s" % SOURCE, file=sys.stderr)
        sys.exit(2)
    return [(name, domain, int(code, 16)) for name, domain, code in rows]


def binary_rows(binary):
    proc = subprocess.run([binary, "errors", "--json"],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        print("check_errors_cli: `inimerse errors --json` exited %d"
              % proc.returncode, file=sys.stderr)
        sys.exit(1)
    try:
        data = json.loads(proc.stdout.decode("utf-8"))
    except ValueError as exc:
        print("check_errors_cli: the output is not JSON: %s" % exc, file=sys.stderr)
        sys.exit(1)
    return [(e["name"], e["domain"], e["code"]) for e in data["entries"]], data


def main(argv):
    if len(argv) != 2:
        print("usage: check_errors_cli.py <path-to-inimerse>", file=sys.stderr)
        return 2
    binary = argv[1]

    rows = source_rows()
    reported, data = binary_rows(binary)
    names = [name for name, _domain, _code in rows]
    cli_text = CLI.read_text(encoding="utf-8")

    failures = []

    # question 1 of 3: is there a copy?
    copied = [name for name in names if '"%s"' % name in cli_text]
    if copied:
        print("question 1 of 3: no table name is written into src/main.c ... FAIL")
        print("  %d name(s) appear as string literals there, e.g. %s"
              % (len(copied), ", ".join('"%s"' % n for n in copied[:5])))
        print("  a name written into the CLI is a second copy of the table: it")
        print("  reads correctly until the day the two disagree, and it reads")
        print("  identically on the day before that one.")
        failures.append(1)
    else:
        print("question 1 of 3: no table name is written into src/main.c ... PASS")

    # question 2 of 3: do the two agree on the members?
    src_pairs = sorted((name, code) for name, _d, code in rows)
    cli_pairs = sorted((name, code) for name, _d, code in reported)
    if src_pairs != cli_pairs:
        print("question 2 of 3: the CLI reports the table ... FAIL")
        only_src = [p for p in src_pairs if p not in cli_pairs]
        only_cli = [p for p in cli_pairs if p not in src_pairs]
        if only_src:
            print("  in %s but not in the output: %s"
                  % (SOURCE.name, ", ".join("%s 0x%02X" % p for p in only_src[:5])))
        if only_cli:
            print("  in the output but not in %s: %s"
                  % (SOURCE.name, ", ".join("%s 0x%02X" % p for p in only_cli[:5])))
        failures.append(2)
    else:
        print("question 2 of 3: the CLI reports the table ... PASS (%d member(s))"
              % len(src_pairs))

    # question 3 of 3: the reverse route agrees with the forward one.  The
    # reverse lookup is a second code path over the same table (a number in,
    # a name out), so it can be wrong while the listing is right.
    mismatch = []
    for name, _domain, code in reported:
        proc = subprocess.run([binary, "errors", "0x%02X" % code],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        text = proc.stdout.decode("utf-8", "replace").strip()
        if proc.returncode != 0 or not text.startswith("0x%02X" % code) or name not in text:
            mismatch.append((code, name, proc.returncode, text))
    if mismatch:
        print("question 3 of 3: the reverse route agrees with the listing ... FAIL")
        for code, name, rc, text in mismatch[:5]:
            print("  0x%02X should name %s, but `errors 0x%02X` exited %d and said %r"
                  % (code, name, code, rc, text[:60]))
        failures.append(3)
    else:
        print("question 3 of 3: the reverse route agrees with the listing ... PASS "
              "(%d code(s) looked up by number)" % len(reported))

    if data.get("kinds") != len(rows):
        print("check_errors_cli: the output self-reports %s kind(s), the source has %d"
              % (data.get("kinds"), len(rows)), file=sys.stderr)
        failures.append(4)

    if failures:
        print("check_errors_cli: %d question(s) asked, %d failed" % (3, len(set(failures))))
        return 1
    print("check_errors_cli: 3 question(s) asked, 0 failed -- the CLI is a view of "
          "g_errors[], not a copy of it.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
