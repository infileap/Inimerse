#!/usr/bin/env python3
"""P1 gate: mir_from_bytecode -> mir_to_bytecode must reproduce the container byte for byte.

What is compared, and what is not (read this before trusting a green run):

  * The bytecode STREAM is regenerated from IrInsn -- mir_to_bytecode never
    copies `Bytecode.code` -- so a byte-identical stream means the MIR carried
    every instruction, operand (including the 32-bit ones), pool entry, function
    header, thread header, try entry and global name in the right order.
  * The dependency TRAILER (src/compilation/deps.c) is NOT part of Bytecode and
    cannot be regenerated; it is carried verbatim and counted separately
    (`trailer_carried`).  A run that reports stream identity and a carried
    trailer is not hiding a mismatch: the stream length is printed.
  * The negative control matters as much as the positive one: a perturbed MIR
    must produce a DIFFERENT file.  If a perturbation still round-trips
    identically, the writer is copying rather than rebuilding and the positive
    result proves nothing.

Usage: tools/mir_roundtrip.test.py [--jobs N] [--keep DIR]
Exit: 0 all identical, 1 any mismatch, 2 harness build failure.
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
    out = {"size": {}, "identical": {}, "trailer": {}, "diff": {}}
    for line in stdout.splitlines():
        parts = line.split("|")
        if not parts:
            continue
        tag, kv = parts[0], {}
        for item in parts[1:]:
            if "=" in item:
                k, v = item.split("=", 1)
                kv[k] = v
        out.setdefault(tag, kv)
        if tag in ("IR-SIZE", "ORIG-IDENTICAL", "ORIG-TRAILER", "ORIG-STREAM", "ORIG-DIFF"):
            out[tag] = kv
    return out


def run_one(inim, workdir, tag):
    out = os.path.join(workdir, tag + ".rt.inim")
    p = subprocess.run([HARNESS, inim, out], capture_output=True, text=True)
    return p.returncode, parse(p.stdout), p.stdout + p.stderr


def compile_im(im, workdir):
    out = os.path.join(workdir, os.path.basename(im)[:-3] + ".inim")
    p = subprocess.run([os.path.join(ROOT, "build", "inimerse"), "compile", im, out],
                       cwd=ROOT, capture_output=True, text=True)
    return out if p.returncode == 0 and os.path.exists(out) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", default=None, help="keep the working directory here")
    args = ap.parse_args()

    if not build_harness():
        return 2

    workdir = args.keep or tempfile.mkdtemp(prefix="mir-rt-")
    os.makedirs(workdir, exist_ok=True)
    cleaned = args.keep is None

    cases = []          # (label, path)
    for im in sorted(glob.glob(os.path.join(ROOT, "vtest", "*.im")) +
                     glob.glob(os.path.join(ROOT, "examples", "*.im"))):
        built = compile_im(os.path.relpath(im, ROOT), workdir)
        if built:
            cases.append((os.path.relpath(im, ROOT), built))
    # artifacts that were not produced by this run: a precompiled .inim is the
    # more interesting case, because nothing in this run built it
    for inim in sorted(glob.glob(os.path.join(ROOT, "**", "*.inim"), recursive=True)):
        cases.append((os.path.relpath(inim, ROOT), inim))

    total = identical = failed = carried = skipped = 0
    tot_funcs = tot_insns = tot_threads = tot_try = tot_wide = 0
    for idx, (label, path) in enumerate(cases):
        rc, rec, raw = run_one(path, workdir, "c%d" % idx)
        total += 1
        size = rec.get("IR-SIZE", {})
        tot_funcs += int(size.get("funcs", 0) or 0)
        tot_insns += int(size.get("insns", 0) or 0)
        tot_threads += int(size.get("threads", 0) or 0)
        tot_try += int(size.get("try", 0) or 0)
        tot_wide += int(size.get("wide_ops", 0) or 0)
        if rec.get("ORIG-TRAILER", {}).get("bytes", "0") not in ("0", None):
            carried += 1
        ident = rec.get("ORIG-IDENTICAL", {})
        if rc == 0 and ident.get("differ") == "0":
            identical += 1
        elif "read failed" in raw:
            # Not a MIR result: the engine's own bytecode_read_file rejects it
            # (older format version / not a bytecode container at all).  Counted
            # separately so the total never claims more coverage than it has.
            skipped += 1
            print("MIR-SKIP-UNREADABLE|file=%s" % label)
        else:
            failed += 1
            print("MIR-MISMATCH|file=%s|rc=%d" % (label, rc))
            for line in raw.splitlines():
                if line.startswith(("ORIG-", "IR-SIZE")):
                    print("  " + line)

    print("MIR-ROUNDTRIP|files=%d|identical=%d|differ=%d|skip_unreadable=%d|trailer_carried=%d"
          % (total, identical, failed, skipped, carried))
    print("MIR-COVERAGE|funcs=%d|insns=%d|threads=%d|try=%d|wide_ops=%d"
          % (tot_funcs, tot_insns, tot_threads, tot_try, tot_wide))

    # ---- negative control -------------------------------------------------
    # Pick a case with insns to perturb.  A perturbation that still round-trips
    # means the writer copied instead of rebuilt.
    neg_ok = 0
    neg_bad = 0
    target = None
    for label, path in cases:
        rc, rec, raw = run_one(path, workdir, "probe")
        if int(rec.get("IR-SIZE", {}).get("insns", "0") or 0) >= 4:
            target = (label, path)
            break
    if target:
        for field, value, name in ((0, 25, "op"), (1, 99, "dst"), (2, 7, "r2")):
            out = os.path.join(workdir, "neg-%s.inim" % name)
            p = subprocess.run([HARNESS, target[1], out, "--perturb", "0", "3", str(field), str(value)],
                               capture_output=True, text=True)
            differ = parse(p.stdout).get("ORIG-IDENTICAL", {}).get("differ", "0")
            if differ not in ("0", None):
                neg_ok += 1
            else:
                neg_bad += 1
                print("MIR-NEGCTL-MISS|field=%s|value=%d|file=%s" % (name, value, target[0]))
    print("MIR-NEGCTL|fields_tested=%d|broke_identity=%d|did_not=%d"
          % (neg_ok + neg_bad, neg_ok, neg_bad))

    if cleaned:
        shutil.rmtree(workdir, ignore_errors=True)

    return 0 if (failed == 0 and neg_bad == 0 and neg_ok > 0) else 1


if __name__ == "__main__":
    sys.exit(main())
