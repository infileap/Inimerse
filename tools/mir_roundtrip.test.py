#!/usr/bin/env python3
"""P1 gate: mir_from_bytecode -> mir_to_bytecode must reproduce the container byte for byte.

P1 is RELABEL-ONLY.  The criterion below is therefore not "the program still
runs": it is "the bytes that come out are the bytes that went in".  A P1 prototype
that folded a constant, reordered blocks or renumbered an operand would change
those bytes, and this file is supposed to go red when it does.

What is compared, and what is not (read this before trusting a green run):

  * The bytecode STREAM is regenerated from IrInsn -- mir_to_bytecode never
    copies `Bytecode.code` -- so a byte-identical stream means the MIR carried
    every instruction, operand (including the 32-bit ones), pool entry, function
    header, thread header, try entry and global name in the right order.
  * The dependency TRAILER (src/compilation/deps.c) is NOT part of Bytecode and
    cannot be regenerated; it is carried verbatim and counted separately
    (`trailer_carried`).  The stream length is printed next to the file length so
    identity is reported at the layer it holds.
  * The NEGATIVE CONTROL is what makes the positive reading mean anything: two
    MIR-layer transformations that are NOT relabels (renumber an operand, swap
    two instructions) must each produce a DIFFERENT file.  If one still
    round-trips identically, the writer is copying rather than rebuilding and the
    whole green run is void -- that is why a missed control exits 1.

Usage: tools/mir_roundtrip.test.py [--keep DIR]
Exit: 0 all identical and every control red, 1 any mismatch or a green control,
      2 harness build failure.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = os.path.join(tempfile.gettempdir(), "mir_roundtrip_harness")
SRC = ["src/compiler/mir.c", "src/compiler/bytecode.c",
       "src/compilation/deps.c", "src/platform/platform.c"]
INCLUDES = ["-Isrc/compiler", "-Isrc/common", "-Isrc", "-Isrc/platform", "-Isrc/compilation"]
# MIR_ROUNDTRIP_MAIN adds a main() to mir.c, which is why the repo keeps three
# files instead of four: the test compiles the harness it needs.
FLAGS = ["-std=gnu11", "-O1", "-g", "-DMIR_ROUNDTRIP_MAIN", "-Wall", "-Wextra"]


def build_harness():
    cmd = ["gcc"] + FLAGS + INCLUDES + ["-o", HARNESS] + SRC
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout + p.stderr)
        print("MIR-HARNESS|built=0")
        return False
    print("MIR-HARNESS|built=1|path=%s" % HARNESS)
    return True


def parse(stdout):
    """(tag -> first record), (tag -> list of records)."""
    first, many = {}, {}
    for line in stdout.splitlines():
        parts = line.split("|")
        if not parts:
            continue
        tag, kv = parts[0], {}
        for item in parts[1:]:
            if "=" in item:
                k, v = item.split("=", 1)
                kv[k] = v
        first.setdefault(tag, kv)
        many.setdefault(tag, []).append(kv)
    return first, many


def run_one(inim, workdir, tag, extra=None):
    out = os.path.join(workdir, tag + ".rt.inim")
    cmd = [HARNESS, inim, out] + (extra or [])
    p = subprocess.run(cmd, capture_output=True, text=True)
    first, many = parse(p.stdout)
    return p.returncode, first, many, p.stdout + p.stderr


def compile_im(im, workdir):
    out = os.path.join(workdir, os.path.basename(im)[:-3] + ".inim")
    p = subprocess.run([os.path.join(ROOT, "build", "inimerse"), "compile", im, out],
                       cwd=ROOT, capture_output=True, text=True)
    return out if p.returncode == 0 and os.path.exists(out) else None


def report_group(name, rows):
    """rows: list of (label, rc, first, raw). Returns (n, bytes, differ, skipped)."""
    n = total_bytes = differ = skipped = 0
    for label, rc, first, raw in rows:
        ident = first.get("ORIG-IDENTICAL", {})
        if "read failed" in raw:
            skipped += 1
            print("MIR-SKIP-UNREADABLE|file=%s" % label)
            continue
        n += 1
        total_bytes += int(ident.get("bytes_orig", "0") or 0)
        if rc == 0 and ident.get("differ") == "0":
            continue
        differ += 1
        print("MIR-MISMATCH|file=%s|rc=%d|differ=%s" % (label, rc, ident.get("differ", "?")))
        for line in raw.splitlines():
            if line.startswith(("ORIG-", "IR-SIZE")):
                print("  " + line)
    print("MIR-IDENTICAL|set=%s|programs=%d|bytes=%d|differ=%d|skip_unreadable=%d"
          % (name, n, total_bytes, differ, skipped))
    return n, total_bytes, differ, skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", default=None, help="keep the working directory here")
    args = ap.parse_args()

    if not build_harness():
        return 2

    workdir = args.keep or tempfile.mkdtemp(prefix="mir-rt-")
    os.makedirs(workdir, exist_ok=True)

    # Group A: .im sources compiled by this run (AST -> compile, today's path).
    compiled_rows = []
    for im in sorted(glob.glob(os.path.join(ROOT, "vtest", "*.im")) +
                     glob.glob(os.path.join(ROOT, "examples", "*.im"))):
        label = os.path.relpath(im, ROOT)
        built = compile_im(label, workdir)
        if not built:
            print("MIR-SKIP-COMPILE|file=%s" % label)
            continue
        rc, first, many, raw = run_one(built, workdir, "a%d" % len(compiled_rows))
        compiled_rows.append((label, rc, first, raw))

    # Group B: .inim already in the tree -- nothing in this run built them.
    precompiled_rows = []
    seen = set()
    for inim in sorted(glob.glob(os.path.join(ROOT, "**", "*.inim"), recursive=True)):
        # compare the path RELATIVE to ROOT: when this runs inside a worktree the
        # absolute path itself contains "/.worktrees/", so filtering on the
        # absolute path would silently drop every artifact and report programs=0.
        rel = os.path.relpath(inim, ROOT)
        if rel.startswith(".worktrees/") or rel.startswith(".scratch/") or rel.startswith("build/"):
            continue
        if inim in seen:
            continue
        seen.add(inim)
        label = rel
        rc, first, many, raw = run_one(inim, workdir, "b%d" % len(precompiled_rows))
        precompiled_rows.append((label, rc, first, raw, many))

    n_a, b_a, d_a, s_a = report_group("compiled", compiled_rows)
    n_b, b_b, d_b, s_b = report_group("precompiled", [(r[0], r[1], r[2], r[3]) for r in precompiled_rows])

    carried = 0
    for row in compiled_rows + [(r[0], r[1], r[2], r[3]) for r in precompiled_rows]:
        if row[2].get("ORIG-TRAILER", {}).get("bytes", "0") not in ("0", None):
            carried += 1
    print("MIR-ROUNDTRIP|files=%d|identical=%d|differ=%d|skip_unreadable=%d|trailer_carried=%d"
          % (n_a + n_b, (n_a - d_a) + (n_b - d_b), d_a + d_b, s_a + s_b, carried))

    # ---- negative control: P1 is relabel-only, so a non-relabel must go red --
    # Pick a program and a function big enough to mutate, from the harness's own
    # per-function report rather than by assuming function 0 is large.
    target = None
    for label, rc, first, raw, many in precompiled_rows:
        funcs = [f for f in many.get("IR-FUNC", []) if int(f.get("ninsns", "0") or 0) >= 4]
        if funcs:
            fid = int(funcs[0]["id"])
            target = (label, None, fid)
            break
    if target is None:
        for row in compiled_rows:
            label = row[0]
            path = os.path.join(workdir, os.path.basename(label)[:-3] + ".inim")
            rc, first, many, raw = run_one(path, workdir, "probe")
            funcs = [f for f in many.get("IR-FUNC", []) if int(f.get("ninsns", "0") or 0) >= 4]
            if funcs:
                target = (label, path, int(funcs[0]["id"]))
                break

    controls = []   # (case, want, got, differ)
    if target:
        label, path, fid = target
        if path is None:
            path = os.path.join(ROOT, label)
        # (a) renumber one operand: an operand change is not a relabel
        rc, first, many, raw = run_one(path, workdir, "neg-renum",
                                       ["--perturb", str(fid), "1", "0", "25"])
        d = first.get("ORIG-IDENTICAL", {}).get("differ", "0")
        controls.append(("renumber-op", "red", "red" if d not in ("0", None) else "green", d))
        # (b) perturb a destination register
        rc, first, many, raw = run_one(path, workdir, "neg-dst",
                                       ["--perturb", str(fid), "1", "1", "99"])
        d = first.get("ORIG-IDENTICAL", {}).get("differ", "0")
        controls.append(("renumber-dst", "red", "red" if d not in ("0", None) else "green", d))
        # (c) swap two instructions: the clearest form of "not a relabel"
        rc, first, many, raw = run_one(path, workdir, "neg-swap",
                                       ["--swap", str(fid), "1", "2"])
        d = first.get("ORIG-IDENTICAL", {}).get("differ", "0")
        controls.append(("swap-1-2", "red", "red" if d not in ("0", None) else "green", d))
    for case, want, got, d in controls:
        print("MIR-NEGCTL|case=%s|want=%s|got=%s|differ=%s" % (case, want, got, d))
    red = sum(1 for c in controls if c[2] == "red")
    print("MIR-NEGCTL|controls=%d|red=%d|green=%d|target=%s"
          % (len(controls), red, len(controls) - red, target[0] if target else "none"))

    if args.keep is None:
        shutil.rmtree(workdir, ignore_errors=True)

    ok = (d_a == 0 and d_b == 0 and n_a > 0 and len(controls) == 3 and red == 3)
    print("MIR-VERDICT|%s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
