#!/usr/bin/env python3
"""Fixed point of the self-hosted compiler: does the engine and the compiled
compiler produce the same bytes?

The measurement, in the form stage 3 can actually hold today:

  stage1    <bin> selfhost/compiler.im --emit <abs>/selfhost/compiler.im a.txt
            the compiler as the engine runs it (zero recursions)
  stage1'   the same command again, to a second file; must be byte identical
            to stage1 -- one right answer and one wrong answer is not a fixed
            point, and a measurement that cannot repeat cannot be cited
  stage2    <bin> compile selfhost/compiler.im c.bin     (the C artifact)
            <bin> c.bin --emit <abs>/selfhost/compiler.im b.txt
            the compiled compiler running as a program (one recursion)
  verdict   stage1 == stage1-again == stage2, byte for byte

Why one recursion and not two: the compiler's own product is the dec-artifact
v1 text format, and nothing in this tree loads or executes that format --
src/, tools/ and CMakeLists.txt contain no reader (measured on 2a165b5).  The
C artifact is INIMBC and the engine eats it.  So the second generation is
reachable today only through the C artifact, and this probe prints that
boundary as a reading instead of hiding it behind a skip:

  artifact-format: c=INIMBC <bytes>, bootstrap=dec-artifact v1 <bytes>

Usage:
  python3 tools/dec_selfcompile_probe.py [--bin build/inimerse] [--timeout 300]

Exit codes:
  0  every comparison identical
  1  a comparison differed (the report names sha256 / lines / bytes and the
     first differing line of the pair that differed)
  2  the engine binary is missing or not executable -- NOT a silent skip: a
     fixed-point test that quietly passes when it cannot run certifies
     nothing, so it prints SKIP(reason) and returns non-zero.

Absolute paths throughout: the compiled compiler resolves a relative path
against the compiler's own directory, not the target's (docs/streams/
dec-p3-observe.md), so a relative target would read a different file than the
one this probe means.
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
    """The first differing line: its number, and both sides, clipped."""
    left = read_text(left_path).splitlines()
    right = read_text(right_path).splitlines()
    for index in range(max(len(left), len(right))):
        a = left[index] if index < len(left) else "<no such line>"
        b = right[index] if index < len(right) else "<no such line>"
        if a != b:
            clip = 90
            return (
                f"first differing line {index + 1}: "
                f"left={a[:clip]!r} right={b[:clip]!r}"
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
        print(f"dec-selfcompile: SKIP(no executable engine at {engine})")
        print(
            "dec-selfcompile: FAILED -- the subject is absent, so this run "
            "measured nothing; a green here would certify a measurement that "
            "did not happen."
        )
        return 2
    if not os.path.isfile(source):
        print(f"dec-selfcompile: SKIP(no source at {source})")
        print("dec-selfcompile: FAILED -- see above.")
        return 2

    scratch = tempfile.mkdtemp(prefix="dec-selfcompile-")
    stage1 = os.path.join(scratch, "stage1.txt")
    stage1_again = os.path.join(scratch, "stage1-again.txt")
    stage2 = os.path.join(scratch, "stage2.txt")
    compiled = os.path.join(scratch, "compiler.inim")
    failures: list[str] = []

    def emit(target_out: str, compiler: str) -> None:
        rc, out, err = run(
            [compiler, source, "--emit", source, target_out], args.timeout, scratch
        )
        if rc != 0 or not os.path.isfile(target_out):
            failures.append(
                f"emit failed: {os.path.basename(compiler)} --emit rc={rc} "
                f"stdout={strip_banners(out).strip()[:200]!r} "
                f"stderr={err.strip()[:200]!r}"
            )

    emit(stage1, engine)
    emit(stage1_again, engine)

    build_rc, build_out, build_err = run(
        [engine, "compile", source, compiled], args.timeout, scratch
    )
    if build_rc != 0 or not os.path.isfile(compiled):
        failures.append(
            f"compile failed: rc={build_rc} "
            f"stdout={strip_banners(build_out).strip()[:200]!r} "
            f"stderr={build_err.strip()[:200]!r}"
        )
    else:
        # The artifact is INIMBC data, not an executable: the engine is the
        # thing that runs it, exactly as it runs a .im script.
        rc, out, err = run(
            [engine, compiled, "--emit", source, stage2], args.timeout, scratch
        )
        if rc != 0 or not os.path.isfile(stage2):
            failures.append(
                f"stage2 emit failed: engine could not run the compiled "
                f"compiler, rc={rc} stdout={strip_banners(out).strip()[:200]!r} "
                f"stderr={err.strip()[:200]!r}"
            )

    if failures:
        for failure in failures:
            print(f"dec-selfcompile: FAILED -- {failure}")
        return 1

    def compare(left: str, right: str, label: str) -> None:
        left_digest, right_digest = digest(left), digest(right)
        left_bytes, right_bytes = os.path.getsize(left), os.path.getsize(right)
        left_lines = len(read_text(left).splitlines())
        right_lines = len(read_text(right).splitlines())
        if left_digest == right_digest:
            print(
                f"{label}: same sha256={left_digest} "
                f"lines={left_lines} bytes={left_bytes}"
            )
            return
        failures.append(
            f"{label}: DIFFER sha256_left={left_digest} "
            f"sha256_right={right_digest}, lines {left_lines} vs {right_lines}, "
            f"bytes {left_bytes} vs {right_bytes}; "
            f"{first_difference(left, right)}"
        )
        print(f"{label}: DIFFER -- {failures[-1]}")

    compare(stage1, stage1_again, "stage1-rerun")
    compare(stage1, stage2, "stage1-vs-stage2")
    # Read before the scratch tree is removed: the summary line names the bytes
    # it compared, and a digest taken after the files are gone is a guess.
    stage1_digest = digest(stage1)

    with open(compiled, "rb") as fh:
        c_head = fh.read(7)
    c_format = "INIMBC" if c_head.startswith(b"INIMBC") else "unknown"
    first_line = read_text(stage1).splitlines()[0] if os.path.getsize(stage1) else ""
    bootstrap_format = first_line if first_line.strip() else "unknown"
    print(
        f"artifact-format: c={c_format} {os.path.getsize(compiled)} bytes, "
        f"bootstrap={bootstrap_format} {os.path.getsize(stage1)} bytes"
    )

    if args.keep:
        print(f"dec-selfcompile: scratch kept at {scratch}")
    else:
        for path in (stage1, stage1_again, stage2, compiled):
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
            print(f"dec-selfcompile: FAILED -- {failure}")
        return 1
    print(
        "dec-selfcompile: fixed point OK -- engine and compiled compiler "
        f"produce the same bytes ({stage1_digest})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
