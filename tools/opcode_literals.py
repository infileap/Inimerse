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

An equal number is not a loan.  The first draft counted thirteen positions and
three of them borrowed nothing: `[1, 2, 3, 4]` in a prose table is a list, and
`1.` opening a bullet is an ordinal -- both match a quadruple's shape and both
happen to equal an enum value.  A number written where an opcode goes is a
loan; a number that happens to equal one is a coincidence.  The `"code":` key
is the difference the ruler can see, and requiring it is why the count is eight
and not thirteen.

A mention is not a carrier either.  This ruler spells `'vm_exec'` and `"code":`
in its own patterns and docstring, so the D2 test selects it -- it describes
the shape it hunts, and hands the engine nothing.

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

    A quadruple only counts when it sits on a line that also carries the
    `"code":` key.  Without that, the first draft of this ruler counted the
    list display `[1, 2, 3, 4]` in docs/AUDIT.md and docs/STATUS.md as two
    borrowed opcode positions, and the ordered-list marker `1.` as a third.
    A number that equals a member is not a loan; a number written where an
    opcode goes is.  Requiring the `"code"` key is what tells them apart.

D4  the *tree*: whether the line carrying a position names the tree it was
    read on -- the family rule `tools/check_obs_markers.py` enforces on
    citations, applied here to positions.  A record says which tree it was
    read on; a pointer does not, and is read against HEAD.  A borrowed opcode
    number that also fails to name its tree is a loan with no observation
    point: nobody can re-read it, because nobody wrote down what to re-read.
    This is reported as a reading, never as a failure -- see the honest
    boundary below.

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

HONEST BOUNDARIES
-----------------
  * The `derived` verdict rests on the ABSENCE of digits, not on the presence
    of a read.  A position read from the header at run time leaves nothing to
    find, so the ruler cannot enumerate it; the read detector is a weaker,
    secondary signal and it misses the indirect spelling (a file that does
    `from opcode_literals import HEADER` reads the header without naming the
    path on any line the pattern sees).  The strong evidence is that there is
    no number left.
  * D4 is a reading, never a failure.  It reports a position whose line names
    no tree; it does not say the line is wrong.  `docs/` prose is allowed to
    point at HEAD -- that is what a pointer is -- and this tool does not decide
    which pointers deserve a marker.  `tools/check_obs_markers.py` counts those
    separately and is the authority on the marker's grammar, which is reused
    here rather than reimplemented.
  * D4 looks only at the line.  A tree named in a section heading, or within
    thirty lines above, is not seen.  For the positions measured here that
    changes nothing -- none of the three documents names a tree in any of those
    places either -- but a reader applying this reading elsewhere should widen
    it deliberately rather than assume it.
  * The census counts positions, not files: one file can borrow twice, and
    `docs/AUDIT.md` does.

Exit status: 0 when no CTest-registered file borrows an opcode number, 1 when
one does, 2 when the header cannot be read (an unmeasurable tree is not a
passing one).

--ref resolves the enum against a named revision instead of the working tree and
reports the positions whose literal means a different member there.  It is off
by default, and without it every line below is unchanged.
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
CODE_ON_LINE = re.compile(r'''["']code["']\s*:''')

# The tree-naming test is check_obs_markers.py's, reused rather than reinvented:
# a revision token the operand of an `[obs: ...]` marker, and it resolves.
OBS_MARKER = re.compile(r'\[obs:\s*([^\]]*)\]')

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


def names_tree(line, root):
    """Does this line say which tree its number was read on?  The criterion is
    check_obs_markers.py's, not a tense word: the operand of an `[obs: ...]`
    marker must resolve to a commit in this repository.  `[obs:]`, `[obs: ...]`
    and `[obs: some prose]` all fail it -- a marker with no resolvable operand
    is a switch that proves itself."""
    for m in OBS_MARKER.finditer(line):
        for tok in re.split(r'[\s,@]+|->', m.group(1)):
            tok = tok.strip('`,()（）*')
            if not tok:
                continue
            r = subprocess.run(['git', 'rev-parse', '--verify', '--quiet',
                                tok + '^{commit}'], cwd=root,
                               capture_output=True, text=True)
            if r.returncode == 0:
                return True
    return False


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
    ap.add_argument('--ref', default=None, metavar='REV',
                    help='resolve the enum against REV instead of the working '
                         'tree, and report the positions whose literal means a '
                         'different member there (default: off, output '
                         'unchanged)')
    args = ap.parse_args()

    root = Path(args.root).resolve()
    head_text = read(root, HEADER)
    if head_text is None:
        print(f'opcode_literals: cannot read {HEADER} under {root}', file=sys.stderr)
        return 2
    head_parsed = parse_enum(head_text)
    if head_parsed is None:
        print(f'opcode_literals: {HEADER} does not parse as an opcode enum', file=sys.stderr)
        return 2
    _head_name_to_value, head_value_to_name = head_parsed

    # --ref only moves where the enum is read from.  The corpus is still the
    # working tree: the question is what a number written here means there.
    if args.ref:
        r = subprocess.run(['git', 'show', f'{args.ref}:{HEADER}'], cwd=root,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f'opcode_literals: {args.ref} has no {HEADER} '
                  f'(an unmeasurable tree is not a passing one)', file=sys.stderr)
            return 2
        header_text = r.stdout
    else:
        header_text = head_text
    parsed = parse_enum(header_text)
    if parsed is None:
        print(f'opcode_literals: {HEADER} does not parse as an opcode enum '
              f'on {args.ref}', file=sys.stderr)
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
        # This ruler spells the shape it hunts -- `'vm_exec'` and `"code":` sit
        # in its own patterns and docstring -- so it would otherwise be its own
        # first carrier.  That is a mention, not an image: it hands the engine
        # nothing.  The exemption is live and this is the measurement that says
        # so (D2 reads 7 with it and 8 without).
        if Path(rel).name == 'opcode_literals.py':
            continue
        carriers.add(rel)
        reads = bool(READS_HEADER.search(text)) or (
            bool(HEADER_CONST.search(text)) and bool(READS_SOMETHING.search(text)))
        for lineno, line in enumerate(text.splitlines(), 1):
            # A quadruple counts as a position only where an opcode goes.  The
            # `"code"` key is what says so: `[1, 2, 3, 4]` in a prose table is
            # a list, and `1.` opening a bullet is an ordinal, and both match
            # QUAD's shape while borrowing nothing from the enum.
            if not CODE_ON_LINE.search(line):
                continue
            for m in QUAD.finditer(line):
                positions.append({'file': rel, 'line': lineno,
                                  'literal': int(m.group(1)),
                                  'names_tree': names_tree(line, root),
                                  'registered': Path(rel).name in registered,
                                  'reads_header': reads})

    print(f'opcode_literals: header {HEADER} -> {len(name_to_value)} member(s)'
          f'{f"  (read from {args.ref})" if args.ref else ""}')
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
              f'   reads {HEADER} at run time: {reads}'
              f' (direct spelling only -- the indirect one, an import that '
              f'holds the path in a name, is invisible here)')
    print()

    print(f'D3  opcode positions the ruler can see: {len(positions)} '
          f'-- all of them are borrowed by construction (a number read from '
          f'the header at run time leaves no digits to find)')
    for r in sorted(positions, key=lambda r: (r['file'], r['line'], r['literal'])):
        name = value_to_name.get(r['literal'], '<not a member today>')
        print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]:>3}'
              f'  -> {name} = {r["literal"]}   borrowed'
              f'   file reads header: {r["reads_header"]}'
              f'   names its tree: {r["names_tree"]}'
              f'{"  [CTest-registered]" if r["registered"] else ""}')
    print()

    borrowed_all = [r for r in positions if not r['reads_header']]
    running = [r for r in borrowed_all if r['registered']]

    print(f'D4  positions that name the tree they were read on: '
          f'{sum(1 for r in positions if r["names_tree"])} of {len(positions)}')
    unplaced = [r for r in positions if not r['names_tree']]
    by_file = {}
    for r in unplaced:
        by_file.setdefault(r['file'], []).append(r)
    for f in sorted(by_file):
        rows = by_file[f]
        print(f'    {f}: {len(rows)} position(s), none of them on a line '
              f'carrying a resolvable [obs: <ref>]')
    print()
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
    moved = []
    if args.ref:
        moved = [r for r in positions
                 if value_to_name.get(r['literal'])
                 != head_value_to_name.get(r['literal'])]
        print(f'D5  positions whose literal means a different member on '
              f'{args.ref}: {len(moved)} of {len(positions)}')
        for r in sorted(moved, key=lambda r: (r['file'], r['line'], r['literal'])):
            print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]:>3}'
                  f'  -> {value_to_name.get(r["literal"], "<not a member>")} '
                  f'on {args.ref}'
                  f'   -> {head_value_to_name.get(r["literal"], "<not a member>")}'
                  f' on the working tree'
                  f'{"  [CTest-registered]" if r["registered"] else ""}')
        print()

    print(f'verdict: {len(running)} borrowed opcode number(s) inside an '
          f'add_test( )-registered file.')
    if running:
        print('  These are the loans: a deletion in the middle of the enum that')
        print('  does not pin the numbering changes what they mean, silently.')
        for r in running:
            print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]}')
    if moved:
        print(f'  And {len(moved)} of them mean a different member on {args.ref}:')
        print('  a number read off one tree and used as if it were this one.')
        for r in sorted(moved, key=lambda r: (r['file'], r['line'], r['literal'])):
            print(f'    {r["file"]}:{r["line"]}  literal {r["literal"]}')
    return 1 if (running or moved) else 0


if __name__ == '__main__':
    sys.exit(main())
