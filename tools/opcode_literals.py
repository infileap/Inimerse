#!/usr/bin/env python3
"""Census: which opcode numbers in this tree are written as literals.

The criteria are written here, before the numbers are taken, so that a later
reader can disagree with the ruler instead of only with the count.

WHY THIS EXISTS
---------------
`src/compiler/bytecode.h` numbers its members implicitly: `OP_MOV,
OP_LOADK_INT, ...` with no `=`.  In that form the member *name* is a comment
and the *number* is the interface, and two things follow that a reader does
not see:

  * the number is written into every serialized bytecode image
    (`bytecode_write` fwrite()s `RegInstruction` whole), so removing a member
    in the middle re-points every image already written;
  * any *test* that spells an opcode number as a literal has borrowed its
    correctness from the enum as it stood the day the line was written, and
    nothing re-checks the loan.

This tool finds the second kind, because it is the kind a person can fix.

THE RULER, AND TWO THINGS IT REFUSES TO COUNT AS EVIDENCE
---------------------------------------------------------
A mention is not a read.  A file that says `src/compiler/bytecode.h` in a
comment has not obtained anything from that header; only a file that opens it
and reads it at run time has.  Both spellings look the same to a grep, which
is why the first draft of this ruler called thirteen literal positions
"derived" -- every one of them sitting in a file that merely cites the header.

A name is not a registration.  `docs/AUDIT.md` appears in `CMakeLists.txt` in
several `docs/` checks; that does not make it a test, and it does not make the
opcode numbers quoted inside it run.  Only a path that appears as an argument
of an `add_test(...)` command is executed by CTest.

THE DENOMINATORS
----------------
D1  the *written* form: every tracked line matching `OP_<NAME> = <int>` or
    `OP_<NAME> (<int>):` -- how a borrowed value is spelled once somebody
    copies it out of the header into prose or into a table.
D2  the *runnable* form: every tracked test-shaped file that hands a bytecode
    image to the engine -- it names `vm_exec` AND carries a `{"code": [[...
    ]]}` shape.
D3  the *positions*: inside a D2 file, each quadruple in the `code` array is
    one instruction and its first field is one opcode number.  D3 counts
    positions, not files, because one file can borrow twice.

THE VERDICT
-----------
  borrowed  the opcode number is a literal in the source.  Every position this
            ruler can enumerate is one of these, and the reason is structural:
            a number that comes from the header at run time leaves no digits
            in the source to find.  So `borrowed` is not a judgment the ruler
            passes on a position -- it is the only thing the ruler can see.
  derived   the *file* opens `src/compiler/bytecode.h` and reads it at run
            time, so it has no constant of its own to go stale.  This is
            established from the file's I/O, never from a position, and it is
            why a fixed file reports zero positions instead of reporting
            positions as derived.

Every reported position prints BOTH numbers: the literal, and what the enum
gives the name it claims today.  Equal today means the loan is currently good;
it does not mean anything is watching it.

Exit status: 0 when no CTest-registered file borrows an opcode number, 1 when
one does, 2 when the header cannot be read (an unmeasurable tree is not a
passing one).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HEADER = 'src/compiler/bytecode.h'

WRITTEN = re.compile(r'\b(OP_[A-Z0-9_]+)\s*(?:=|:|\()\s*(\d{1,3})\b')
QUAD = re.compile(r'\[\s*(-?\d{1,3})\s*,\s*(-?\d{1,3})\s*,\s*(-?\d{1,3})\s*,\s*(-?\d{1,3})\s*\]')
CODE_SHAPE = re.compile(r'"code"\s*:\s*\[|\[\[\s*-?\d{1,3}\s*,')

# Reading the header means opening it, not naming it.  These are the call
# shapes that actually consume it; `bytecode.h` may still appear in a comment.
READS_HEADER = re.compile(
    r'(?:read_text|readFileSync|read\(|open\(|cat\b|grep\b)[^\n]{0,80}bytecode\.h'
    r'|bytecode\.h[^\n]{0,40}(?:read_text|readFileSync|\.read\()')

# The weaker spelling: the path is held in a name on one line and consumed on
# another.  This is a fallback, not the primary signal -- a file that derives
# its numbers leaves no digits behind, and that is what the D3 scan measures.
HEADER_CONST = re.compile(r'''=\s*['"][^'"]*bytecode\.h['"]''')
READS_SOMETHING = re.compile(
    r'read_text\(|readFileSync\(|[^\w]open\(|subprocess\.\w+\(|\bread\(\)')


def parse_enum(text):
    """The enum as the C compiler reads it: explicit values win, and a gap in
    the numbering is a hole that no name occupies."""
    m = re.search(r'enum\s*\{(.*?\bOP_MOV\b.*?)\}', text, re.S)
    if m is None:
        return None
    src = re.sub(r'/\*.*?\*/', ' ', m.group(1), flags=re.S)
    src = re.sub(r'//[^\n]*', ' ', src)
    name_to_value, value_to_name, nxt = {}, {}, 0
    for item in src.split(','):
        item = item.strip()
        if not item:
            continue
        mm = re.fullmatch(r'(OP_[A-Z0-9_]+)\s*(?:=\s*(-?\d+))?', item)
        if not mm:
            continue
        name, explicit = mm.group(1), mm.group(2)
        if explicit is not None:
            nxt = int(explicit)
        name_to_value[name] = nxt
        value_to_name.setdefault(nxt, name)
        nxt += 1
    return name_to_value, value_to_name


def tracked(root):
    return subprocess.run(['git', 'ls-files'], cwd=root, capture_output=True,
                          text=True, check=True).stdout.split()


def read(root, rel):
    try:
        return (root / rel).read_text(encoding='utf-8', errors='replace')
    except (OSError, IsADirectoryError):
        return None


def ctest_file_arguments(root):
    """Paths that are actually arguments of an add_test( ) command.  A name
    that merely appears in CMakeLists.txt is not a registration."""
    cm = read(root, 'CMakeLists.txt') or ''
    out = set()
    for m in re.finditer(r'add_test\s*\((.*?)\)\s*\n', cm, re.S):
        for tok in re.findall(r'[\w./$<>{}:-]*[\w.]+\.(?:py|js|im|sh|exe)\b',
                              m.group(1)):
            out.add(Path(tok).name)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', default='.')
    ap.add_argument('--written', action='store_true')
    ap.add_argument('--only', action='append', default=None,
                    help='classify only these paths (repeatable); use to run '
                         'the two negative controls against the same header')
    args = ap.parse_args()

    root = Path(args.root).resolve()
    header_text = read(root, HEADER)
    if header_text is None:
        print(f'opcode_literals: cannot read {HEADER} under {root}', file=sys.stderr)
        return 2
    parsed = parse_enum(header_text)
    if parsed is None:
        print(f'opcode_literals: {HEADER} does not parse as an opcode enum', file=sys.stderr)
        return 2
    name_to_value, value_to_name = parsed

    files = args.only if args.only else tracked(root)
    registered = ctest_file_arguments(root)

    written = []
    for rel in files:
        text = read(root, rel)
        if text is None:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            for name, lit in WRITTEN.findall(line):
                written.append({'file': rel, 'line': lineno, 'name': name,
                                'literal': int(lit), 'test': 'test' in rel})

    positions, carriers = [], set()
    for rel in files:
        text = read(root, rel)
        if text is None or 'vm_exec' not in text or not CODE_SHAPE.search(text):
            continue
        carriers.add(rel)
        reads = bool(READS_HEADER.search(text)) or (
            bool(HEADER_CONST.search(text)) and bool(READS_SOMETHING.search(text)))
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in QUAD.finditer(line):
                positions.append({'file': rel, 'line': lineno,
                                  'literal': int(m.group(1)),
                                  'registered': Path(rel).name in registered,
                                  'reads_header': reads})

    print(f'opcode_literals: header {HEADER} -> {len(name_to_value)} member(s)')
    print()

    print(f'D1  written form: {len(written)} position(s) in '
          f'{len({r["file"] for r in written})} file(s)'
          f'  ({"--written to list" if not args.written else "listing"})')
    mism = [r for r in written if name_to_value.get(r['name']) != r['literal']]
    print(f'    of those, {len(mism)} disagree with the enum today')
    for r in mism:
        print(f'    MISMATCH {r["file"]}:{r["line"]}  {r["name"]} = {r["literal"]} '
              f'(enum says {name_to_value.get(r["name"])})')
    tests = [r for r in written if r['test']]
    print(f'    {len(tests)} of them are inside a file whose path contains "test"')
    if args.written:
        for r in sorted(written, key=lambda r: (r['file'], r['line'])):
            print(f'      {r["file"]}:{r["line"]}  {r["name"]} = {r["literal"]}')
    print()

    print(f'D2  files handing a bytecode image to the engine: {len(carriers)}')
    for f in sorted(carriers):
        text = read(root, f) or ''
        reads = bool(READS_HEADER.search(text)) or (
            bool(HEADER_CONST.search(text)) and bool(READS_SOMETHING.search(text)))
        print(f'    {f}')
        print(f'        add_test( ) argument: {Path(f).name in registered}'
              f'   reads {HEADER} at run time: {reads}')
    print()

    print(f'D3  opcode positions the ruler can see: {len(positions)} '
          f'-- all of them are borrowed by construction (a number read from '
          f'the header at run time leaves no digits to find)')
    for r in sorted(positions, key=lambda r: (r['file'], r['line'], r['literal'])):
        name = value_to_name.get(r['literal'], '<not a member today>')
        print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]:>3}'
              f'  -> {name} = {r["literal"]}   borrowed'
              f'   file reads header: {r["reads_header"]}'
              f'{"  [CTest-registered]" if r["registered"] else ""}')
    print()

    borrowed_all = [r for r in positions if not r['reads_header']]
    running = [r for r in borrowed_all if r['registered']]
    print(f'borrowed positions: {len(borrowed_all)}  '
          f'(of them, registered: {len(running)})')
    derived_files = sorted(f for f in carriers
                           if (READS_HEADER.search(read(root, f) or '')
                               or (HEADER_CONST.search(read(root, f) or '')
                                   and READS_SOMETHING.search(read(root, f) or ''))))
    print(f'derived: {len(derived_files)} file(s) read {HEADER} at run time '
          f'and so report no positions at all')
    for f in derived_files:
        print(f'    {f}')
    print(f'verdict: {len(running)} borrowed opcode number(s) inside an '
          f'add_test( )-registered file.')
    if running:
        print('  These are the loans: a deletion in the middle of the enum that')
        print('  does not pin the numbering changes what they mean, silently.')
        for r in running:
            print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]}')
    return 1 if running else 0


if __name__ == '__main__':
    sys.exit(main())
