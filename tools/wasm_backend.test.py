"""Wasm MVP backend regression (v0.5 roadmap §2.3).

For each subset script: compile with `inimerse compile --abi-target wasm`,
run it under the Node host (tools/wasm_run.js), and diff the output against
the interpreter (`inimerse run`).  Also asserts that constructs outside the
MVP subset are rejected with a clear compile-time error, and that the module
passes `wasm-validate` when wabt is installed.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

# (name, source). All scripts must be inside the documented MVP subset:
# int/float/bool, arithmetic, comparisons, if/while/repeat, user functions,
# globals. `say` takes one numeric/bool value.
CASES = [
    ("say_const", 'say 42\n'),
    ("globals", 'x = 3\nsay x\nx = x + 4\nsay x\n'),
    ("call", 'func f(a) {\n    return a + 1\n}\nsay f(41)\n'),
    ("fib", 'func fib(n) {\n    if n < 2 {\n        return n\n    }\n'
            '    return fib(n - 1) + fib(n - 2)\n}\n'
            'i = 0\nwhile i < 15 {\n    say fib(i)\n    i = i + 1\n}\n'),
    ("while", 'x = 0\nwhile x < 5 {\n    say x\n    x = x + 1\n}\n'),
    ("repeat", 'i = 0\nrepeat 4 {\n    say i * 10\n    i = i + 1\n}\n'),
    ("float_div", 'say 7 / 2\nsay 1.5 + 2\nsay 0.5 * 4\n'),
    ("int_overflow", 'say 2147483647 + 1\nsay 2000000000 + 2000000000\n'),
    ("negative", 'x = 10\nsay -x\nsay 0 - 2147483647 - 2\n'),
    ("mod", 'say 17 % 5\nsay 18 % 6\n'),
    ("nested_calls", 'func twice(x) {\n    return x * 2\n}\n'
                     'func four(x) {\n    return twice(twice(x))\n}\nsay four(3)\n'),
    ("bools", 'say true\nsay 1 == 1\nsay 2 > 3\nsay 2 <= 2\n'),
    ("mixed_num", 'i = 1\nwhile i < 5 {\n    say i / 2\n    i = i + 1\n}\n'),
]

REJECT_CASES = [
    ("strings", 'say "hello"\n', "strings are not supported"),
    ("unknown_fn", 'say nosuch(1)\n', "function 'nosuch' not found"),
    ("and_outside", 'x = true and false\n', "'and'/'or' outside a condition"),
    ("array", 'a = [1, 2]\nsay 1\n', "expression type"),
]


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    cands += [HERE.parent / "build" / "inimerse", HERE.parent / "build-local" / "inimerse"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def run(cmd, cwd, timeout=30):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)


def main():
    engine = find_engine()
    node = shutil.which("node")
    assert node, "node is required for the wasm host runner"
    with tempfile.TemporaryDirectory(prefix="inimerse-wasm-") as td:
        root = Path(td)
        for name, src in CASES:
            im = root / f"{name}.im"
            im.write_text(src, encoding="utf-8")
            rc = run([str(engine), "compile", "--abi-target", "wasm", im.name, f"{name}.wasm"], root)
            assert rc.returncode == 0, f"{name}: compile failed: {rc.stderr.decode(errors='replace')}"

            w = run([node, str(HERE / "wasm_run.js"), f"{name}.wasm"], root)
            i = run([str(engine), "run", im.name], root)
            assert i.returncode == 0, f"{name}: interpreter failed: {i.stderr.decode(errors='replace')}"
            assert w.returncode == 0, f"{name}: wasm failed: {w.stderr.decode(errors='replace')}"
            assert w.stdout == i.stdout, (
                f"{name}: output mismatch\n wasm: {w.stdout!r}\n interp: {i.stdout!r}")

        for name, src, needle in REJECT_CASES:
            im = root / f"bad_{name}.im"
            im.write_text(src, encoding="utf-8")
            rc = run([str(engine), "compile", "--abi-target", "wasm", im.name, f"bad_{name}.wasm"], root)
            assert rc.returncode != 0, f"{name}: expected compile-time rejection"
            assert needle in rc.stderr.decode(errors="replace"), (
                f"{name}: unexpected error: {rc.stderr.decode(errors='replace')}")

        if shutil.which("wasm-validate"):
            rc = run(["wasm-validate", "fib.wasm"], root)
            assert rc.returncode == 0, "wasm-validate rejected the module"
    print(f"wasm backend: ok ({len(CASES)} equivalence cases, {len(REJECT_CASES)} rejections)")


if __name__ == "__main__":
    main()
