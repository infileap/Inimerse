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


def run(engine, source, td, name):
    script = Path(td) / name
    script.write_text(source, encoding='utf-8')
    return subprocess.run([engine, '--no-mods', str(script)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)


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
    print('vm bad fidx: ok (out-of-range call index refused, control call works)')


if __name__ == '__main__':
    main()
