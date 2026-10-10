#!/usr/bin/env python3
"""Check the shape of the error table in `src/types/error_types.c`.

Three questions, each evaluated and reported on its own:

  1. does every domain hold at most 16 members?
  2. does every code's high nibble name its own domain?
  3. is every code distinct, non-zero, and below the reserved nibbles?

`docs/ERROR_CODES_V06.md` section 2 writes the layout as a byte: high nibble is
the domain, low nibble is the member inside it.  The table in
`src/types/error_types.c` is the thing that has to obey it, and this checker is
the guard that lives in the tree -- it sees "the table grew", which is the half
of the problem a runtime guard cannot see.  `im_enum_create` is the other half:
it sees a hand-built code on a path that runs, and it cannot see a table that
grew.  Neither guard replaces the other.

What this checker can see, and what it cannot
---------------------------------------------
It reads the C source text of `g_errors[]`.  So it sees the table and nothing
else: a code assembled by hand anywhere else in the engine is invisible here.
That is not a defect of this file -- it is why section 3.3 asks for two guards
whose red conditions differ.

Nothing here is a number copied from the design document.  Question 2 derives
the expected nibble from the `ImErrorDomain` order in `src/types/error_types.h`
(ordinal + 1), so adding a domain shifts the expectation with the enum rather
than with a constant somebody has to remember to edit.

Run with `--report` to print the per-domain table; the default output is one
line per question plus a verdict.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "types" / "error_types.c"
HEADER = ROOT / "src" / "types" / "error_types.h"

MAX_PER_DOMAIN = 16
RESERVED_FROM = 0x70  # nibbles 7..15 are reserved for future domains

ENTRY = re.compile(
    r'\{\s*"(?P<name>[^"]+)"\s*,\s*(?P<domain>IM_ERROR_DOMAIN_[A-Z_]+)\s*,\s*'
    r'(?P<code>0x[0-9A-Fa-f]+|\d+)\s*\}'
)
ENUM_ORDER = re.compile(r"typedef enum\s*\{(?P<body>.*?)\}\s*ImErrorDomain", re.S)
ORDINAL = re.compile(r"(IM_ERROR_DOMAIN_[A-Z_]+)")


def domain_order():
    """The ImErrorDomain ordinals, in declaration order, read from the header."""
    found = ENUM_ORDER.search(HEADER.read_text(encoding="utf-8"))
    if not found:
        sys.exit("error_domains: no `typedef enum { ... } ImErrorDomain` in "
                 f"{HEADER.relative_to(ROOT)}; nothing was checked.")
    names = ORDINAL.findall(found.group("body"))
    if not names:
        sys.exit("error_domains: ImErrorDomain declares no domain; "
                 "nothing was checked.")
    return names


def table():
    """(name, domain, code) for every entry of g_errors[], in table order."""
    text = SOURCE.read_text(encoding="utf-8")
    start = text.find("g_errors[]")
    if start < 0:
        sys.exit("error_domains: no `g_errors[]` in "
                 f"{SOURCE.relative_to(ROOT)}; nothing was checked.")
    end = text.find("};", start)
    rows = [
        (m.group("name"), m.group("domain"), int(m.group("code"), 0))
        for m in ENTRY.finditer(text[start:end])
    ]
    if not rows:
        sys.exit("error_domains: `g_errors[]` parsed to zero entries; "
                 "nothing was checked.")
    return rows


def question1(rows):
    counts = {}
    for _name, domain, _code in rows:
        counts[domain] = counts.get(domain, 0) + 1
    over = sorted((d, n) for d, n in counts.items() if n > MAX_PER_DOMAIN)
    if over:
        return ("question 1 of 3: every domain holds at most "
                f"{MAX_PER_DOMAIN} members ... FAIL "
                + "; ".join(f"{d} holds {n}" for d, n in over))
    worst = max(counts.values()) if counts else 0
    return (f"question 1 of 3: every domain holds at most {MAX_PER_DOMAIN} "
            f"members ... PASS (largest domain holds {worst} of {MAX_PER_DOMAIN})")


def question2(rows, order):
    nibble_of = {name: i + 1 for i, name in enumerate(order)}
    wrong = []
    for name, domain, code in rows:
        want = nibble_of.get(domain)
        if want is None:
            wrong.append(f"{name}: {domain} is not a member of ImErrorDomain")
        elif code >> 4 != want:
            wrong.append(f"{name}: 0x{code:02X} has nibble 0x{code >> 4:X}, "
                         f"but {domain} is nibble 0x{want:X}")
    if wrong:
        return ("question 2 of 3: every code's high nibble names its own "
                "domain ... FAIL " + "; ".join(wrong[:5]))
    used = {domain for _name, domain, _code in rows}
    return ("question 2 of 3: every code's high nibble names its own domain "
            f"... PASS ({len(rows)} code(s) over {len(used)} of "
            f"{len(order)} declared domain(s))")


def question3(rows):
    seen = {}
    bad = []
    for name, _domain, code in rows:
        if code in seen:
            bad.append(f"0x{code:02X} is claimed by both {seen[code]} and {name}")
        seen[code] = name
        if code == 0:
            bad.append(f"{name}: 0x00 is ok, not an error")
        if code >= RESERVED_FROM:
            bad.append(f"{name}: 0x{code:02X} sits in the reserved nibbles "
                       f"(0x{RESERVED_FROM:02X} and above)")
    if bad:
        return ("question 3 of 3: every code is distinct, non-zero and below "
                f"0x{RESERVED_FROM:02X} ... FAIL " + "; ".join(bad[:5]))
    return ("question 3 of 3: every code is distinct, non-zero and below "
            f"0x{RESERVED_FROM:02X} ... PASS ({len(seen)} distinct code(s))")


def main(argv):
    unknown = [a for a in argv[1:] if a != "--report"]
    if unknown:
        sys.exit(f"error_domains: unknown argument {unknown[0]!r}; "
                 "nothing was run.")
    report = "--report" in argv[1:]

    order = domain_order()
    rows = table()

    if report:
        print(f"{len(rows)} entr(ies) in g_errors[], read from "
              f"{SOURCE.relative_to(ROOT)}")
        for domain in order:
            members = [(n, c) for n, d, c in rows if d == domain]
            if not members:
                continue
            listed = ", ".join(f"0x{c:02X} {n}" for n, c in members)
            print(f"  {domain:<34} {len(members):>2} member(s)  {listed}")

    answers = [question1(rows), question2(rows, order), question3(rows)]
    for line in answers:
        print(line)

    failed = [a for a in answers if " ... FAIL " in a]
    if failed:
        print(f"error_domains: 3 question(s) asked, {len(failed)} failed -- the "
              "table in src/types/error_types.c does not obey the byte layout "
              "in docs/ERROR_CODES_V06.md section 2.", file=sys.stderr)
        return 1
    print("error_domains: 3 question(s) asked, 0 failed -- the table obeys the "
          "byte layout: high nibble is the domain, low nibble is the member.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
