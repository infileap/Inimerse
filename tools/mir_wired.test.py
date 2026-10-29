#!/usr/bin/env python3
"""P1 of docs/IR_DESIGN.md: compiler.c builds MIR first, then bytecode.

What this measures, and why each set exists:

  set=compiled    -- every tracked .im is compiled twice: once by a baseline
                     binary built before the wiring (INIMERSE_BASE) and once by
                     the wired one (INIMERSE_BIN).  The .inim files must be
                     byte-identical.  This is the assertion P1 is judged on:
                     emit() now appends an IrInsn and mir_to_bytecode builds the
                     code stream, so any operand or offset that shifts shows up
                     here as a differing byte.

  set=precompiled -- every tracked .inim is read back through mir_from_bytecode
                     and written out by mir_to_bytecode.  The harness is the
                     same gcc build tools/mir_roundtrip.test.py uses.  This set
                     needs no baseline binary, so it can be registered on its own.

Why the baseline is a separate binary instead of a flag: after the wiring the
old emit() path no longer exists, so "unwired" cannot be toggled at run time
without writing a second implementation of it -- and comparing two new things
proves nothing.  INIMERSE_BASE must therefore be a build of fdb55bb (the commit
this branch is based on); when it is missing the script refuses (exit 2) rather
than printing differ=0 for a comparison it never ran.

Readouts (peer ivory-zephyr format, m05378):
  MIR-WIRED|set=compiled|programs=<n>|bytes=<b>|differ=0
  MIR-WIRED|set=precompiled|files=<k>|differ=0
  MIR-WIRED-CORPUS|expected=<n>|measured=<n>|missing=0
and the negative control, which must go red:
  INIMERSE_MIR_PERTURB=id:idx:field:value flips one operand of one IrInsn
  inside wire_finish() and names it on stderr; the compiled output must then
  differ from the baseline and the differing program must be named.
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("INIMERSE_BIN") or os.path.join(ROOT, "build", "inimerse")
BASE = os.environ.get("INIMERSE_BASE")
HARNESS = os.path.join(tempfile.gettempdir(), "mir_wired_harness")
SRC = ["src/compiler/mir.c", "src/compiler/bytecode.c",
       "src/compilation/deps.c", "src/platform/platform.c"]
INCLUDES = ["-Isrc/compiler", "-Isrc/common", "-Isrc", "-Isrc/platform", "-Isrc/compilation"]
FLAGS = ["-std=gnu11", "-O1", "-g", "-DMIR_ROUNDTRIP_MAIN", "-Wall", "-Wextra"]

IM_PATTERNS = ["vtest/*.im", "examples/**/*.im"]
INIM_PATTERNS = ["*.inim", "projects/*.inim", "vtest/*.inim", "examples/**/*.inim"]

# Same two declared sets as tools/mir_roundtrip.test.py: a program that fails to
# compile must be on this list (a failure outside it is a corpus gap and is
# named), and an .inim the engine reader refuses must be on the other.
EXPECTED_UNCOMPILABLE = {
    "vtest/be_removed_assign_v04.im",
    "vtest/be_removed_bare_v04.im",
    "vtest/be_removed_decl_v04.im",
    "vtest/finally_continue_control_flow_probe_v04.im",
    "vtest/finally_control_flow_probe_v04.im",
    "vtest/finally_goto_control_flow_probe_v04.im",
    "vtest/member_on_nonname_v06.im",
    "vtest/range_metadata_probe_v06.im",
}
EXPECTED_UNREADABLE_INIM = {
    "nst2.inim",
    "projects/exc_test.inim",
    "projects/set_comp_test.inim",
    "projects/set_test.inim",
    "projects/t1.inim",
    "projects/tt4.inim",
    "projects/tt6.inim",
    "projects/tt7.inim",
}

# The negative control fixed perturbation: instruction 0 of module 0, its
# operand b, set to a value no real operand uses.  0:0:3:32767 is written down
# rather than searched for -- a control that hunts for a perturbation producing
# a difference would pass by finding one, not by having a hook that bites.
PERTURB = "0:0:3:32767"


def parse(stdout):
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


def tracked(patterns):
    p = subprocess.run(["git", "ls-files", "--"] + patterns, cwd=ROOT,
                       capture_output=True, text=True)
    if p.returncode != 0:
        return []
    return sorted(set(x for x in p.stdout.splitlines() if x))


def compile_im(binary, rel, out, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run([binary, "compile", rel, out], cwd=ROOT, env=e,
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def read_bytes(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def build_harness():
    cmd = ["gcc"] + FLAGS + INCLUDES + ["-o", HARNESS] + SRC
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout + p.stderr)
        return None
    return HARNESS


def corpus_compiled(workdir):
    """Every compilable .im compiled by both binaries; bytes compared."""
    ims = tracked(IM_PATTERNS)
    rows, declared, gaps = [], [], []
    total_bytes = 0
    differ = 0
    for rel in ims:
        tag = os.path.basename(rel)[:-3] + "_" + str(abs(hash(rel)) % 100000)
        base_out = os.path.join(workdir, tag + ".base.inim")
        wire_out = os.path.join(workdir, tag + ".wire.inim")
        brc, blog = compile_im(BASE, rel, base_out)
        if brc != 0:
            if rel in EXPECTED_UNCOMPILABLE:
                declared.append(rel)
                continue
            gaps.append((rel, "baseline-compile-failed"))
            continue
        if rel in EXPECTED_UNCOMPILABLE:
            # The declaration is stale: the baseline compiles it, so "supposed
            # not to compile" is no longer a fact about this program.
            rows.append({"label": rel, "differ": "stale-declaration", "bytes": 0})
            differ += 1
            print("MIR-MISMATCH|file=%s|why=stale-uncompilable-declaration" % rel)
            continue
        wrc, wlog = compile_im(BIN, rel, wire_out)
        if wrc != 0:
            gaps.append((rel, "wired-compile-failed"))
            differ += 1
            print("MIR-MISMATCH|file=%s|why=wired-compile-failed" % rel)
            continue
        a, b = read_bytes(base_out), read_bytes(wire_out)
        if a is None or b is None:
            gaps.append((rel, "no-output"))
            differ += 1
            continue
        total_bytes += len(b)
        if a != b:
            differ += 1
            rows.append({"label": rel, "differ": "1", "bytes": len(b)})
            print("MIR-MISMATCH|file=%s|why=bytes-differ|base=%d|wired=%d"
                  % (rel, len(a), len(b)))
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print("  first-differing-byte|offset=%d|base=%d|wired=%d" % (i, x, y))
                    break
        else:
            rows.append({"label": rel, "differ": "0", "bytes": len(b)})
    return rows, declared, gaps, total_bytes, differ


def corpus_precompiled(workdir):
    """Every tracked .inim read through mir_from_bytecode and written back."""
    harness = build_harness()
    if harness is None:
        return None
    inims = tracked(INIM_PATTERNS)
    rows, declared, gaps = [], [], []
    differ = 0
    for rel in inims:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            gaps.append((rel, "not-on-disk"))
            continue
        out = os.path.join(workdir, "rt%d.inim" % len(rows))
        p = subprocess.run([harness, path, out], cwd=ROOT, capture_output=True, text=True)
        raw = p.stdout + p.stderr
        first, _ = parse(p.stdout)
        if "read failed" in raw:
            if rel in EXPECTED_UNREADABLE_INIM:
                declared.append(rel)
                continue
            gaps.append((rel, "unreadable"))
            differ += 1
            print("MIR-MISMATCH|file=%s|why=reader-refused-it" % rel)
            continue
        if rel in EXPECTED_UNREADABLE_INIM:
            differ += 1
            print("MIR-MISMATCH|file=%s|why=stale-unreadable-declaration" % rel)
            rows.append({"label": rel, "differ": "stale-declaration"})
            continue
        ident = first.get("ORIG-IDENTICAL", {})
        d = ident.get("differ", "?")
        rows.append({"label": rel, "differ": d})
        if not (p.returncode == 0 and d == "0"):
            differ += 1
            print("MIR-MISMATCH|file=%s|rc=%d|differ=%s" % (rel, p.returncode, d))
            for line in raw.splitlines():
                if line.startswith(("ORIG-", "IR-SIZE")):
                    print("  " + line)
    return rows, declared, gaps, differ


def negative_control(workdir, sample):
    """The perturbation hook must change the bytes, and it must name them."""
    base_out = os.path.join(workdir, "neg.base.inim")
    brc, _ = compile_im(BASE, sample, base_out)
    if brc != 0:
        return None, "baseline-could-not-compile-%s" % sample
    perturbed = os.path.join(workdir, "neg.wire.inim")
    wrc, wlog = compile_im(BIN, sample, perturbed, env={"INIMERSE_MIR_PERTURB": PERTURB})
    if wrc != 0:
        return None, "wired-compile-failed-under-perturbation"
    if "MIR-WIRED-PERTURB" not in wlog:
        return None, "hook-silent"
    a, b = read_bytes(base_out), read_bytes(perturbed)
    if a is None or b is None:
        return None, "no-output"
    if a == b:
        return None, "bytes-identical-despite-perturbation"
    return {"program": sample, "base": len(a), "wired": len(b)}, None


def main():
    if not os.path.exists(BIN):
        print("mir_wired: cases=0 red=1 green=0")
        print("MIR-WIRED|set=compiled|differ=na|why=no-wired-binary|path=%s" % BIN)
        return 2
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                          capture_output=True, text=True).stdout.strip()
    print("MIR-WIRED|head=%s" % head)
    anc = subprocess.run(["git", "merge-base", "--is-ancestor", "fdb55bb", "HEAD"],
                         cwd=ROOT, capture_output=True, text=True)
    print("MIR-WIRED|base-commit-is-ancestor=%s" % ("yes" if anc.returncode == 0 else "no"))
    if not BASE or not os.path.exists(BASE):
        print("mir_wired: cases=0 red=1 green=0")
        print("MIR-WIRED|set=compiled|differ=na|why=no-baseline-binary"
              "|hint=INIMERSE_BASE must point at a build of fdb55bb, the commit this branch is based on")
        return 2
    print("MIR-WIRED|baseline=%s" % BASE)

    workdir = tempfile.mkdtemp(prefix="mir-wired-")
    ims = tracked(IM_PATTERNS)
    inims = tracked(INIM_PATTERNS)

    rows, declared, gaps, total_bytes, differ = corpus_compiled(workdir)
    print("MIR-WIRED|set=compiled|programs=%d|bytes=%d|differ=%d"
          % (len(rows), total_bytes, differ))
    for rel, why in gaps:
        print("MIR-GAP|set=compiled|file=%s|why=%s" % (rel, why))
    compiled_cell = {"name": "compiled-bytes-identical", "red": differ, "green": len(rows) - differ}

    pre = corpus_precompiled(workdir)
    precompiled_cell = {"name": "precompiled-bytes-identical",
                        "red": 1 if pre is None else pre[3],
                        "green": 0 if pre is None else len(pre[0])}
    pre_rows, pre_declared, pre_gaps = ([], [], []) if pre is None else pre[:3]
    if pre is not None:
        print("MIR-WIRED|set=precompiled|files=%d|differ=%d" % (len(pre_rows), pre[3]))
        for rel, why in pre_gaps:
            print("MIR-GAP|set=precompiled|file=%s|why=%s" % (rel, why))
    else:
        print("MIR-WIRED|set=precompiled|files=0|differ=na|why=harness-not-built")

    measured = len(rows) + len(declared) + len(pre_rows) + len(pre_declared)
    expected = len(ims) + len(inims)
    missing = [rel for rel, _ in list(gaps) + list(pre_gaps)]
    print("MIR-WIRED-CORPUS|expected=%d|measured=%d|missing=%d"
          % (expected, measured, len(missing)))
    corpus_cell = {"name": "every-tracked-file-is-accounted-for",
                   "red": len(missing) + (1 if measured != expected else 0),
                   "green": 1 if (measured == expected and not missing) else 0}

    sample = rows[0]["label"] if rows else None
    neg, neg_why = (None, "no-program-to-perturb") if not sample else negative_control(workdir, sample)
    if neg:
        print("MIR-WIRED-NEGCTRL|program=%s|perturb=%s|base=%d|wired=%d|went-red=yes"
              % (neg["program"], PERTURB, neg["base"], neg["wired"]))
        negctrl_cell = {"name": "a-perturbed-operand-changes-the-bytes", "red": 0, "green": 1}
    else:
        print("MIR-WIRED-NEGCTRL|perturb=%s|went-red=no|why=%s" % (PERTURB, neg_why))
        negctrl_cell = {"name": "a-perturbed-operand-changes-the-bytes", "red": 1, "green": 0}

    cells = [compiled_cell, precompiled_cell, corpus_cell, negctrl_cell]
    red = sum(1 for c in cells if c["red"])
    green = len(cells) - red
    print("mir_wired: cases=%d red=%d green=%d" % (len(cells), red, green))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())
