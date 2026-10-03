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

# OP_CALL_FUNC = 34, OP_HALT = 33 (src/compiler/bytecode.h:8-23).  `funcs` is
# empty, so fidx = 999 is out of range on every axis.
PROBE = ('bc = {"code": [[34, 999, 0, 0], [33, 0, 0, 0]], '
         '"strings": [], "floats": [], "funcs": []}\n'
         'say vm_exec(bc)\n')

# The same shape with a real function: it must keep working, so that "the VM
# refuses everything" cannot pass this test.
CONTROL = 'func f() { return 7 }\nsay f()\n'


def run(engine, source, td, name):
    script = Path(td) / name
    script.write_text(source, encoding='utf-8')
    return subprocess.run([engine, '--no-mods', str(script)],
                          capture_output=True, text=True, check=False)


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
