"""CLI + incremental compilation regression (v0.5 roadmap §1.2 / §4).

Exercises the `compile/buildc/run/profile/symbols` command surface and the
`--incremental` dependency-trailer flow: second build is skipped, touching any
imported source triggers a rebuild, `--force` always rebuilds, and
`--abi-version` mismatches fail at build time.
"""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def find_engine():
    candidates = []
    env = os.environ.get("INIMERSE_BIN")
    if env:
        candidates.append(Path(env))
    for _dir in ("build", "build-local", "build-windows-gcc", "build-py"):
        for _name in ("inimerse", "inimerse.exe"):
            candidates.append(Path(_dir) / _name)
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand.resolve()
    return None


def run(engine, *args, cwd):
    return subprocess.run([str(engine), *args], cwd=cwd, capture_output=True, timeout=60)


def main():
    engine = find_engine()
    if not (engine): raise AssertionError("inimerse engine binary not found (set INIMERSE_BIN to override)")
    with tempfile.TemporaryDirectory(prefix="inimerse-cli-test-") as td:
        root = Path(td)
        (root / "lib.im").write_text('func lib_hello() {\n    return "hello from lib"\n}\n', encoding="utf-8")
        (root / "app.im").write_text(
            'import "lib.im"\n\nfunc add(a, b) {\n    return a + b\n}\n\n'
            'say lib_hello()\nsay "sum=" + str(add(1, 2))\n',
            encoding="utf-8",
        )

        # buildc + run round-trip (source and bytecode entry points)
        rc = run(engine, "buildc", "app.im", "app.inim", cwd=root)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode(errors="replace"))
        rc = run(engine, "run", "app.inim", cwd=root)
        if not (rc.returncode == 0 and b"hello from lib" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)
        rc = run(engine, "run", "app.im", cwd=root)
        if not (rc.returncode == 0 and b"sum=3" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)

        # symbols export
        rc = run(engine, "symbols", "app.im", cwd=root)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode(errors="replace"))
        symbols = (root / "app.im.symbols").read_text(encoding="utf-8", errors="replace")
        if not ("add" in symbols and "lib_hello" in symbols): raise AssertionError(symbols)

        # profile writes a .prof report with per-function stats
        rc = run(engine, "profile", "app.im", cwd=root)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode(errors="replace"))
        prof = (root / "app.im.prof").read_text(encoding="utf-8")
        if not ("add" in prof and "lib_hello" in prof): raise AssertionError(prof)

        # incremental: rebuild is skipped when nothing changed
        rc = run(engine, "buildc", "--incremental", "app.im", "app.inim", cwd=root)
        if not (rc.returncode == 0 and b"up to date" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)

        # ...but any imported source change triggers a rebuild
        time.sleep(0.02)
        (root / "lib.im").write_text(
            'func lib_hello() {\n    return "hello from lib v2"\n}\n', encoding="utf-8"
        )
        rc = run(engine, "buildc", "--incremental", "app.im", "app.inim", cwd=root)
        if not (rc.returncode == 0 and b"compiled:" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)
        rc = run(engine, "run", "app.inim", cwd=root)
        if not (b"hello from lib v2" in rc.stdout): raise AssertionError(rc.stdout)

        # main source change also triggers a rebuild
        time.sleep(0.02)
        (root / "app.im").write_text(
            'import "lib.im"\n\nfunc add(a, b) {\n    return a + b\n}\n\n'
            'say lib_hello()\nsay "sum=" + str(add(2, 3))\n',
            encoding="utf-8",
        )
        rc = run(engine, "buildc", "--incremental", "app.im", "app.inim", cwd=root)
        if not (b"compiled:" in rc.stdout): raise AssertionError(rc.stdout)

        # --force rebuilds even when up to date
        rc = run(engine, "buildc", "--incremental", "--force", "app.im", "app.inim", cwd=root)
        if not (rc.returncode == 0 and b"compiled:" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)

        # ABI version mismatch fails at build time; matching version passes
        rc = run(engine, "buildc", "--abi-version", "99", "app.im", "abi.inim", cwd=root)
        if not (rc.returncode != 0 and b"ABI version mismatch" in rc.stderr): raise AssertionError(rc.stdout + rc.stderr)
        rc = run(engine, "buildc", "--abi-version", "2", "app.im", "abi.inim", cwd=root)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode(errors="replace"))

        # debug info: --debug-info writes a text sidecar + a DWARF5 line program
        rc = run(engine, "buildc", "--debug-info", "app.im", "dbg.inim", cwd=root)
        if not (rc.returncode == 0 and b"debug-info:" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)
        dbg = (root / "dbg.inim.dbg").read_text(encoding="utf-8", errors="replace")
        if not ("# block: main" in dbg and "# symbols" in dbg): raise AssertionError(dbg)
        dl = (root / "dbg.inim.debug_line").read_bytes()
        if not (dl[4:6] == bytes([5, 0]) and len(dl) > 20): raise AssertionError(dl[:8])  # DWARF version 5

        # --aot produces a self-executing native executable (packaging channel)
        rc = run(engine, "compile", "--aot", "app.im", "app_aot.exe", cwd=root)
        if not (rc.returncode == 0 and b"aot:" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)
        aot = root / "app_aot.exe"
        if not (aot.is_file() and os.access(aot, os.X_OK)): raise AssertionError("aot output missing or not executable")
        rc = subprocess.run([str(aot)], cwd=root, capture_output=True, timeout=60)
        if not (rc.returncode == 0 and b"hello from lib" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)

        # unstable backends are rejected explicitly
        rc = run(engine, "run", "--aot", "app.im", cwd=root)
        if not (rc.returncode != 0 and b"compile --aot" in rc.stderr): raise AssertionError(rc.stdout + rc.stderr)
        rc = run(engine, "run", "--abi-target", "wasm", "app.im", cwd=root)
        if not (rc.returncode != 0 and b"stable channel" in rc.stderr): raise AssertionError(rc.stdout + rc.stderr)

        # reproducible builds: identical project layouts in different trees
        # (and different build times) produce identical bytecode + record
        import hashlib
        import json
        for tree in ("reproA", "reproB"):
            t = root / tree
            t.mkdir()
            (t / "lib.im").write_text('func lib_hello() {\n    return "hello from lib"\n}\n', encoding="utf-8")
            (t / "app.im").write_text(
                'import "lib.im"\n\nfunc add(a, b) {\n    return a + b\n}\n\n'
                'say lib_hello()\nsay "sum=" + str(add(1, 2))\n',
                encoding="utf-8",
            )
            rc = run(engine, "buildc", "--reproducible", "app.im", "app.inim", cwd=t)
            if not (rc.returncode == 0 and b"reproducible: ok" in rc.stdout): raise AssertionError(rc.stdout + rc.stderr)
        hA = hashlib.sha256((root / "reproA/app.inim").read_bytes()).hexdigest()
        hB = hashlib.sha256((root / "reproB/app.inim").read_bytes()).hexdigest()
        if not (hA == hB): raise AssertionError(f"bytecode not reproducible across trees: {hA} vs {hB}")
        recA = json.loads((root / "reproA/app.inim.build.json").read_text(encoding="utf-8"))
        recB = json.loads((root / "reproB/app.inim.build.json").read_text(encoding="utf-8"))
        if not (recA["bytecode_sha256"] == recB["bytecode_sha256"] == hA): raise AssertionError(recA)
        if not ([d["path"] for d in recA["dependencies"]] == ["app.im", "lib.im"]): raise AssertionError(recA)
        if not (recA["dependencies"][1]["sha256"] == hashlib.sha256(
            (root / "reproA/lib.im").read_bytes()).hexdigest()): raise AssertionError(recA)

        # compile from outside the script dir: imports must resolve and the
        # output hash must match the in-dir build (location independence)
        rc = run(engine, "buildc", "--reproducible", str(root / "reproA/app.im"),
                 str(root / "reproA/out.inim"), cwd=root)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode(errors="replace"))
        hOut = hashlib.sha256((root / "reproA/out.inim").read_bytes()).hexdigest()
        # out.inim sits next to app.inim, so the trailer relative paths (and
        # therefore the hash) match the in-dir build exactly
        if not (hOut == hA): raise AssertionError(f"out-of-dir build differs: {hOut} vs {hA}")
    print("cli incremental: ok")


if __name__ == "__main__":
    main()
