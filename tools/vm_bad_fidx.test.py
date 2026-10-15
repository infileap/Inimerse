#!/usr/bin/env python3
"""Regression for the out-of-range call index in src/vm/vm.c (docs/AUDIT.md §1.3).

`L_CALL_FUNC`'s guard against an invalid function index used to be the *body* of
a stray `if (... strncmp(root->func_names[fidx], "h", 1) == 0)`, so it only ran
for functions whose name began with 'h'.  Every other out-of-range fidx fell
through to `root->func_argc[fidx]` and read out of bounds; the probe below
segfaulted (exit 139) on every run before the fix.

The trigger needs no malformed bytecode file.  `vm_exec` accepts a bytecode
image built at run time (src/runtime/vm_exec_builtin.c, `bc_from_data`), so a
two-line `.im` program constructs the bad call directly.

A passing run must (a) not crash and (b) exit non-zero, because the program is
supposed to die reporting the invalid index.  Exit 0 would mean the call
silently succeeded, and a signal would mean the guard is decorative again.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from opcode_literals import HEADER, parse_enum  # noqa: E402


def opcode(name):
    """The number `src/compiler/bytecode.h` gives this member *now*.

    This used to be two literals with their values in a comment, which meant
    the probe borrowed its meaning from the enum as it stood the day the
    comment was written.  Nothing re-checked the loan: deleting a member in
    the middle of that enum moves every number after it, and this probe then
    pointed at a different instruction -- so the test that exists to prove
    `L_CALL_FUNC` refuses an out-of-range index stopped calling `L_CALL_FUNC`
    at all, and reported the guard as broken instead of itself as stale.

    Reading the header here is the whole guard.  If the header cannot be read
    the run fails rather than falling back to a constant: a probe that cannot
    see what it is probing is not a passing probe.
    """
    header = Path(__file__).resolve().parent.parent / HEADER
    if not header.exists():
        raise SystemExit(f'vm bad fidx: cannot read {HEADER}; refusing to fall '
                         f'back to a hard-coded opcode number (an unmeasurable '
                         f'tree is not a passing one)')
    parsed = parse_enum(header.read_text(encoding='utf-8', errors='replace'))
    if parsed is None:
        raise SystemExit(f'vm bad fidx: {HEADER} does not parse as an opcode enum')
    table = parsed[0]
    if name not in table:
        raise SystemExit(f'vm bad fidx: {HEADER} no longer declares {name}, so '
                         f'this probe has nothing to point at')
    return table[name]


# `funcs` is empty, so fidx = 999 is out of range on every axis.
PROBE = ('bc = {{"code": [[{}, 999, 0, 0], [{}, 0, 0, 0]], '
         '"strings": [], "floats": [], "funcs": []}}\n'
         'say vm_exec(bc)\n').format(opcode('OP_CALL_FUNC'), opcode('OP_HALT'))

# The same shape with a real function: it must keep working, so that "the VM
# refuses everything" cannot pass this test.
CONTROL = 'func f() { return 7 }\nsay f()\n'

# The tree this probe's numbers were authored on.  `opcode()` above stops this
# file from CHOOSING its numbers; this asks whether the numbers it ends up with
# still NAME the two instructions the probe was written to exercise.  Those are
# two questions.  Deriving the number follows the enum wherever the enum moves,
# so the guard above cannot go stale -- and that is exactly why it cannot notice
# the day the number it derives means something else than the test means.
#
# Measured 2026-10-12: across all nine revisions that have touched the header,
# 33 has always been OP_HALT and 34 has always been OP_CALL_FUNC, so this
# tripwire reads 0 today.  A guard that is zero today is not the same as no
# guard: this one is non-zero on the first revision that renumbers the middle
# of the enum without pinning the numbering, which is the mistake it exists to
# catch.  (That mistake has been made once -- deleting OP_POP_REG moved every
# number from 30 up -- and it was caught, by four failing CTest cases, only
# after the patch was built.)
#
# An EMPTY value turns the phase off and prints nothing, so a caller can ask for
# this file's output to be byte-identical to the run before the phase existed
# and measure with `cmp` that it got it.  Unset is not the same as empty: unset
# takes the default and the phase runs.
REF_TREE = os.environ.get('VM_BAD_FIDX_REF',
                          '17b3ec5fc38ef7e9b153b013686401c8357f3a32')


def run(engine, source, td, name):
    script = Path(td) / name
    script.write_text(source, encoding='utf-8')
    return subprocess.run([engine, '--no-mods', str(script)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)


def check_ref_tree(repo):
    """Do the numbers this probe hands the engine still name what it names?

    `git show REF_TREE:HEADER` gives the value->name table the numbers were read
    off.  Each number the probe uses is looked up there and here; if the two
    disagree, the probe is standing on a tree it was not written for, and the
    instruction it exercises is not the one it was written to exercise.
    """
    if not REF_TREE:
        return
    show = subprocess.run(['git', 'show', f'{REF_TREE}:{HEADER}'], cwd=repo,
                          capture_output=True, text=True, encoding='utf-8',
                          errors='replace', check=False)
    if show.returncode != 0:
        raise SystemExit(
            f'vm bad fidx: {REF_TREE} has no {HEADER}, so the tree this probe '
            f'was authored on cannot be read -- an unmeasurable tree is not a '
            f'passing one')
    parsed = parse_enum(show.stdout)
    if parsed is None:
        raise SystemExit(
            f'vm bad fidx: {HEADER} on {REF_TREE} does not parse as an opcode '
            f'enum')
    then = parsed[1]

    here = parse_enum((repo / HEADER).read_text(encoding='utf-8',
                                                errors='replace'))[1]
    moved = []
    for name in ('OP_CALL_FUNC', 'OP_HALT'):
        value = opcode(name)
        if then.get(value) != here.get(value):
            moved.append((name, value, then.get(value), here.get(value)))
    if moved:
        lines = [f'    {name} is {value}: {there} on {REF_TREE[:9]}, '
                 f'{now} here' for name, value, there, now in moved]
        raise SystemExit(
            'vm bad fidx: this probe hands the engine a number that no longer '
            'names the instruction it was written to exercise, because the '
            'numbering moved under it:\n' + '\n'.join(lines))
    print(f'vm bad fidx: the two numbers still name OP_CALL_FUNC and OP_HALT '
          f'on {REF_TREE[:9]} -- the enum has not been renumbered under this '
          f'probe')


def main():
    engine = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix='vm-bad-fidx-') as td:
        r = run(engine, PROBE, td, 'probe.im')
        if r.returncode < 0:
            raise SystemExit(
                f'the out-of-range call index crashed the VM (signal '
                f'{-r.returncode}); the L_CALL_FUNC guard is not running. '
                f'stderr tail: {r.stderr.strip()[-200:]}')
        if r.returncode == 0:
            raise SystemExit(
                'the out-of-range call index exited 0: the VM accepted an '
                f'invalid function index. stdout={r.stdout.strip()!r}')
        if '999' not in r.stderr:
            raise SystemExit(
                'the VM refused the call but did not name the index; '
                f'stderr={r.stderr.strip()[-200:]!r}')

        c = run(engine, CONTROL, td, 'control.im')
        if c.returncode != 0 or '7' not in c.stdout:
            raise SystemExit(
                f'the control program (a real function call) stopped working: '
                f'rc={c.returncode} stdout={c.stdout.strip()!r} '
                f'stderr={c.stderr.strip()[-200:]!r}')

    # The fix above removes one loan.  This keeps the next one from being
    # written unnoticed: the census is run here, inside a test CTest already
    # executes, so it costs no new stage, no line in tools/gate.sh and no moved
    # anchor.  A check wired into the gate instead would make every future edit
    # to it pay the whole repository's line-anchor bill.
    repo = Path(__file__).resolve().parent.parent
    census = subprocess.run(
        [sys.executable, str(repo / 'tools' / 'opcode_literals.py'),
         '--root', str(repo)],
        capture_output=True, text=True, encoding='utf-8', errors='replace',
        check=False)
    if census.returncode == 2:
        raise SystemExit('vm bad fidx: the opcode census could not read the '
                         f'header -- an unmeasurable tree is not a passing '
                         f'one\n{census.stdout.strip()[-400:]}')
    if census.returncode != 0:
        loans = [ln.strip() for ln in census.stdout.splitlines()
                 if 'borrowed' in ln and 'CTest-registered' in ln]
        raise SystemExit(
            'a file CTest runs spells an opcode number as a literal, so its '
            'meaning is borrowed from the enum and nothing re-checks it.  '
            'Read the header at run time instead (see opcode() above).\n  '
            + '\n  '.join(loans))
    check_ref_tree(repo)
    print('vm bad fidx: ok (out-of-range call index refused, control call '
          'works, no CTest-registered file borrows an opcode number)')


if __name__ == '__main__':
    main()
