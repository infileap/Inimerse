"""Wasm MVP backend regression (v0.5 roadmap §2.3).

For each subset script: compile with `inimerse compile --abi-target wasm`,
run it under the Node host (tools/wasm_run.js), and diff the output against
the interpreter (`inimerse run`).  Also asserts that constructs outside the
MVP subset are rejected with a clear compile-time error, that constructs the
wasm backend supports only on a best-effort basis fail *explicitly* (never
silently), and that the module passes `wasm-validate` when wabt is installed.

The array/heap cases are the point of the v0.5.0 follow-up: arrays live in a
real linear-memory heap (src/compilation/wasm_backend.c), the refcounted
ownership model reclaims them deterministically, and exhaustion traps with
im_error(4) instead of silently continuing.  See docs/WASM.md.
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
    # nil printing: the baseline backend wrapped the print dispatch in
    # `if (tag != 0)`, so tag 0 (nil) printed nothing at all
    ("nil_print", 'func f() {\n}\nsay f()\nx = 0\nif x > 1 {\n    say 1\n}\nsay x\n'),
    # ---- arrays + linear-memory heap ----
    ("array_basic", 'a = [1, 2, 3]\nsay a[0]\nsay a[2]\nsay len(a)\n'
                    'a[1] = 20\nsay a[1]\n'),
    ("array_loop", 'a = [5, 10, 15, 20]\ns = 0\ni = 0\n'
                   'while i < len(a) {\n    s = s + a[i]\n    i = i + 1\n}\nsay s\n'),
    ("array_alias", 'a = [1, 2, 3]\nb = a\nb[0] = 99\nsay a[0]\nsay b[0]\n'),
    ("array_nested", 'a = [[1, 2], [3, 4]]\nsay a[1][0]\nsay len(a[0])\n'
                     'a[0][1] = 7\nsay a[0][1]\n'),
    ("array_func", 'func f(x) {\n    return x[0] + x[1]\n}\n'
                   'func g() {\n    return [7, 8]\n}\n'
                   'a = [10, 20, 30]\nsay f(a)\nsay g()[1]\nsay len(a)\n'),
    ("array_oob_read", 'a = [1, 2, 3]\nsay a[5]\nsay a[-1]\nsay len(a)\n'),
    ("array_write_elem_arr", 'a = [1, 2, 3]\na[0] = [9, 8]\nsay a[0][1]\nsay len(a)\n'),
    # 200k allocations would need ~12 MB; the arena is ~4 MB, so this only
    # finishes if the refcounted ownership model really reclaims blocks.
    ("array_churn", 's = 0\nrepeat 200000 {\n    b = [1, 2, 3]\n    s = s + b[0]\n}\nsay s\n'),
    # `and`/`or` in value position used to be a hard refusal here while the
    # interpreter returned the deciding operand and AOT returned a boolean --
    # one operator, three answers.  All three now produce the truth-value
    # (docs/AUDIT.md §1.0, §5 O0).  These assert agreement with the
    # interpreter rather than a hardcoded string, so the two cannot drift
    # apart again.
    ("and_or_value", 'say true and false\nsay true or false\nsay 0 or 7\nsay 7 and 0\n'),
    ("not_value", 'say not 0\nsay not 1\nsay not (0 or 0)\n'),
]

REJECT_CASES = [
    ("strings", 'say "hello"\n', "strings are not supported"),
    ("unknown_fn", 'say nosuch(1)\n', "function 'nosuch' not found"),
]

# Cases the interpreter accepts but the wasm subset refuses on purpose: the
# program must exit non-zero and name the reason, never differ silently.
EXPLICIT_FAIL_CASES = [
    # the interpreter grows an out-of-range write; the wasm subset refuses
    ("oob_write", 'a = [1, 2, 3]\na[5] = 9\nsay len(a)\n', "array_index_out_of_range"),
    # the interpreter prints [1, 2, 3]; the wasm subset has no formatter
    ("say_array", 'a = [1, 2, 3]\nsay a\n', "array_op_unsupported"),
    # ~70k live 64-byte arrays exceed the ~4 MB arena: explicit exhaustion,
    # never a silent wrap-around or a corrupt pointer
    ("heap_exhausted", 'a = [1, 2, 3]\nrepeat 70000 {\n    b = [a, 2, 3]\n    a = b\n}\nsay len(a)\n',
     "heap_exhausted"),
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

        for name, src, needle in EXPLICIT_FAIL_CASES:
            im = root / f"one_{name}.im"
            im.write_text(src, encoding="utf-8")
            rc = run([str(engine), "compile", "--abi-target", "wasm", im.name, f"one_{name}.wasm"], root)
            assert rc.returncode == 0, (
                f"{name}: expected the wasm backend to accept this program: "
                f"{rc.stderr.decode(errors='replace')}")
            w = run([node, str(HERE / "wasm_run.js"), f"one_{name}.wasm"], root, timeout=120)
            i = run([str(engine), "run", im.name], root, timeout=120)
            assert i.returncode == 0, (
                f"{name}: the interpreter must accept this program: "
                f"{i.stderr.decode(errors='replace')}")
            assert w.returncode != 0, (
                f"{name}: the wasm subset must refuse this program, it exited 0 with "
                f"{w.stdout!r}")
            assert needle in w.stderr.decode(errors="replace"), (
                f"{name}: expected '{needle}' in stderr, got "
                f"{w.stderr.decode(errors='replace')}")

        # v128 path: the exported scalar/simd loops must run and agree
        bench = root / "bench.im"
        bench.write_text("say 1\n", encoding="utf-8")
        rc = run([str(engine), "compile", "--abi-target", "wasm", bench.name, "bench.wasm"], root)
        assert rc.returncode == 0, f"bench: compile failed: {rc.stderr.decode(errors='replace')}"
        b = run([node, str(HERE / "wasm_run.js"), "--bench", "bench.wasm", "1000000"], root, timeout=120)
        assert b.returncode == 0, f"bench: failed: {b.stderr.decode(errors='replace')}"
        assert b"results equal" in b.stdout, f"bench: scalar/simd disagree: {b.stdout!r}"

        if shutil.which("wasm-validate"):
            rc = run(["wasm-validate", "fib.wasm"], root)
            assert rc.returncode == 0, "wasm-validate rejected the module"
            rc = run(["wasm-validate", "one_heap_exhausted.wasm"], root)
            assert rc.returncode == 0, "wasm-validate rejected the array module"
    print(f"wasm backend: ok ({len(CASES)} equivalence cases, {len(REJECT_CASES)} rejections, "
          f"{len(EXPLICIT_FAIL_CASES)} explicit failures, simd bench equal)")


if __name__ == "__main__":
    main()
