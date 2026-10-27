#!/usr/bin/env python3
"""The outlet closed: does a compiler the self-hosted compiler produced itself
produce the same bytes for that compiler?

The dec-artifact v1 text format (docs/streams/dec-inimbc-outlet.md) has no
loader in this tree, so a second generation of the compiler was unreachable.
--emit-inimbc adds the other outlet: compiler.im writes INIMBC itself, and the
engine loads what it writes.  This probe measures the fixed point that path
makes reachable:

  gen1   <bin> selfhost/compiler.im --emit-inimbc <abs>/selfhost/compiler.im a
         the engine running the compiler source (the compiler as the tree
         builds it), writing an INIMBC image
  gen2   <bin> a --emit-inimbc <abs>/selfhost/compiler.im b
         the image compiling itself; no source compiler involved
  verdict gen1 == gen2, byte for byte, and both carry the INIMBC header

Two properties make this different from dec_selfcompile_probe.py rather than a
duplicate of it: there the second stage was the C compiler's product and the
compiler's own product was unloadable; here the compiler's own product is the
thing that runs, so the loop is closed on the bootstrap's own output.

Usage:
  python3 tools/dec_inimbc_outlet_probe.py [--bin build/inimerse] [--timeout 300]

Exit codes:
  0  the image reproduces itself byte for byte
  1  it does not, or a stage failed (the report names sha256 / bytes / the
     first differing line)
  2  the engine binary is missing or not executable -- NOT a silent skip: a
     fixed-point test that quietly passes when it cannot run certifies
     nothing, so it prints SKIP(reason) and returns non-zero.

Known gap this probe reads around rather than hides: the self-hosted front end
treats `stop all` as thread control on a thread named "all" (selfhost/parser.im
against src/parser/parser.c, which matches TOK_ALL into STMT_STOP), so a
self-produced image does not exit its --emit-inimbc branch and prints
"无法读取文件: --emit-inimbc" after writing the file.  The artifact is written
before that, and this probe compares artifacts, not exit paths -- but the line
is kept visible in the failure message if a run stops emitting.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

BANNER_PREFIXES = ("[mod", "[infiverse", "[verse_dist")


def digest(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_text(path: str) -> str:
    with open(path, "rb") as fh:
        return fh.read().decode("utf-8", "replace")


def first_difference(left_path: str, right_path: str) -> str:
    left = read_text(left_path).splitlines()
    right = read_text(right_path).splitlines()
    for index in range(max(len(left), len(right))):
        a = left[index] if index < len(left) else "<no such line>"
        b = right[index] if index < len(right) else "<no such line>"
        if a != b:
            return (
                f"first differing line {index + 1}: "
                f"left={a[:90]!r} right={b[:90]!r}"
            )
    return "no differing line, yet the bytes differ (line endings?)"


def run(cmd: list[str], timeout: int, cwd: str) -> tuple[int, str, str]:
    proc = subprocess.run(
        cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=timeout, cwd=cwd,
    )
    return proc.returncode, proc.stdout, proc.stderr


def strip_banners(text: str) -> str:
    return "\n".join(
        line for line in text.splitlines()
        if not line.startswith(BANNER_PREFIXES)
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=os.path.join("build", "inimerse"))
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--keep", action="store_true",
                        help="print the scratch directory instead of deleting it")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    engine = os.path.abspath(args.bin)
    source = os.path.join(root, "selfhost", "compiler.im")

    if not os.path.isfile(engine) or not os.access(engine, os.X_OK):
        print(f"dec-inimbc: SKIP(no executable engine at {engine})")
        print(
            "dec-inimbc: FAILED -- the subject is absent, so this run measured "
            "nothing; a green here would certify a measurement that did not "
            "happen."
        )
        return 2
    if not os.path.isfile(source):
        print(f"dec-inimbc: SKIP(no source at {source})")
        print("dec-inimbc: FAILED -- see above.")
        return 2

    scratch = tempfile.mkdtemp(prefix="dec-inimbc-")
    gen1 = os.path.join(scratch, "gen1.inim")
    gen2 = os.path.join(scratch, "gen2.inim")
    c_image = os.path.join(scratch, "c.inim")
    failures: list[str] = []

    # The engine runs both compilers: the source for gen1, the INIMBC image for
    # gen2.  The image carries no execute bit (it is data the engine loads), so
    # both stages are "<engine> <program> <args...>".
    def emit(program: str, target_out: str, label: str) -> None:
        rc, out, err = run(
            [engine, program, "--emit-inimbc", source, target_out],
            args.timeout, scratch,
        )
        clean = strip_banners(out)
        if "emitted-inimbc " not in clean or not os.path.isfile(target_out):
            failures.append(
                f"{label} failed: rc={rc} stdout={clean.strip()[:200]!r} "
                f"stderr={err.strip()[:200]!r}"
            )
        elif not open(target_out, "rb").read(7).startswith(b"INIMBC"):
            failures.append(f"{label} wrote {target_out} without the INIMBC header")

    emit(source, gen1, "gen1 (engine runs the source)")
    if not os.path.isfile(gen1):
        for failure in failures:
            print(f"dec-inimbc: FAILED -- {failure}")
        return 1
    emit(gen1, gen2, "gen2 (the image runs itself)")

    build_rc, build_out, build_err = run(
        [engine, "compile", source, c_image], args.timeout, scratch
    )
    if build_rc != 0 or not os.path.isfile(c_image):
        failures.append(
            f"c-image compile failed: rc={build_rc} "
            f"stdout={strip_banners(build_out).strip()[:200]!r} "
            f"stderr={build_err.strip()[:200]!r}"
        )

    if failures:
        for failure in failures:
            print(f"dec-inimbc: FAILED -- {failure}")
        return 1

    left_digest, right_digest = digest(gen1), digest(gen2)
    left_bytes, right_bytes = os.path.getsize(gen1), os.path.getsize(gen2)
    if left_digest == right_digest:
        print(
            f"gen1-vs-gen2: same sha256={left_digest} bytes={left_bytes} "
            f"(the image reproduces itself)"
        )
    else:
        failures.append(
            f"gen1-vs-gen2: DIFFER sha256_left={left_digest} "
            f"sha256_right={right_digest}, bytes {left_bytes} vs {right_bytes}; "
            f"{first_difference(gen1, gen2)}"
        )
        print(f"gen1-vs-gen2: DIFFER -- {failures[-1]}")

    print(
        f"artifact-format: selfhost=INIMBC {left_bytes} bytes, "
        f"c=INIMBC {os.path.getsize(c_image)} bytes"
    )

    if args.keep:
        print(f"dec-inimbc: scratch kept at {scratch}")
    else:
        for path in (gen1, gen2, c_image):
            try:
                os.unlink(path)
            except OSError:
                pass
        try:
            os.rmdir(scratch)
        except OSError:
            pass

    if failures:
        for failure in failures:
            print(f"dec-inimbc: FAILED -- {failure}")
        return 1
    print(
        "dec-inimbc: outlet fixed point OK -- the compiler the bootstrap "
        f"produces reproduces itself ({left_digest})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
