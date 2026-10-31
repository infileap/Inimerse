#!/usr/bin/env python3
"""P1 gate: mir_from_bytecode -> mir_to_bytecode must reproduce the container byte for byte.

P1 is RELABEL-ONLY.  The criterion is therefore not "the program still runs": it
is "the bytes that come out are the bytes that went in".  A P1 prototype that
folded a constant, reordered blocks or renumbered an operand would change those
bytes, and this file is supposed to go red when it does.

What is compared, and what is not (read this before trusting a green run):

  * The bytecode STREAM is regenerated from IrInsn -- mir_to_bytecode never
    copies `Bytecode.code` -- so a byte-identical stream means the MIR carried
    every instruction, operand (including the 32-bit ones), pool entry, function
    header, thread header, try entry and global name in the right order.
  * The dependency TRAILER (src/compilation/deps.c) is NOT part of Bytecode and
    cannot be regenerated; it is carried verbatim and counted separately
    (`trailer_carried`).  A whole-file `cmp` is therefore the conjunction
    "stream rebuilt correctly AND trailer copied verbatim"; the stream length is
    printed next to the file length so identity is reported at the layer it holds.
  * The CORPUS is printed program by program (`MIR-CORPUS|...`).  Two totals one
    apart differ by a digit to a reader, while "one program stopped being run"
    and "one program that should not run started running" are different
    accidents.  The expected list comes from `git ls-files`, not from the working
    tree, so a program deleted on disk is still expected and is named as missing.
  * The NEGATIVE CONTROL is what makes the positive reading mean anything: two
    MIR-layer transformations that are NOT relabels (renumber an operand, swap
    two instructions) must each produce a DIFFERENT file, and two corpus
    accidents (a program removed, a program made uncompilable) must each be
    NAMED and exit non-zero.  A control that stays green exits 1 -- an unproven
    control voids the reading above it instead of becoming a footnote.

Usage: tools/mir_roundtrip.test.py [--quiet] [--no-selfcheck]
       [--simulate-missing REL] [--simulate-broken REL]     (test hooks)
Exit: 0 all identical and every control red, 1 any mismatch / any green control,
      2 harness build failure.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# One directory per process: the shared /tmp path was overwritten by
# concurrent worktrees and by this suite's own subprocess re-entry, and the
# loser died with PermissionError [Errno 13] (see tools/check_shared_tmp.py).
HARNESS = os.path.join(tempfile.mkdtemp(prefix="mir_roundtrip_harness-"), "mir_roundtrip_harness")
SRC = ["src/compiler/mir.c", "src/compiler/bytecode.c",
       "src/compilation/deps.c", "src/platform/platform.c"]
INCLUDES = ["-Isrc/compiler", "-Isrc/common", "-Isrc", "-Isrc/platform", "-Isrc/compilation"]
# MIR_ROUNDTRIP_MAIN adds a main() to mir.c, which is why the repo keeps three
# files instead of four: the test compiles the harness it needs.
FLAGS = ["-std=gnu11", "-O1", "-g", "-DMIR_ROUNDTRIP_MAIN", "-Wall", "-Wextra"]

IM_PATTERNS = ["vtest/*.im", "examples/**/*.im"]
INIM_PATTERNS = ["*.inim", "projects/*.inim", "vtest/*.inim", "examples/**/*.inim"]

# Programs that are SUPPOSED not to compile: negative frontend tests, where the
# language is meant to reject them.  Declared here instead of tolerated
# silently -- a compile failure outside this set is a corpus gap and is named.
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

# .inim files the engine's own reader refuses (older format version).  Declared
# for the same reason: "unreadable" must be an asserted fact, not a bucket that
# quietly absorbs whatever fails.
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


def run_one(inim, out_dir, tag, extra=None):
    out = os.path.join(out_dir, tag + ".rt.inim")
    p = subprocess.run([HARNESS, inim, out] + (extra or []), capture_output=True, text=True)
    first, many = parse(p.stdout)
    return p.returncode, first, many, p.stdout + p.stderr


def compile_im(rel, workdir):
    out = os.path.join(workdir, os.path.basename(rel)[:-3] + ".inim")
    p = subprocess.run([os.path.join(ROOT, "build", "inimerse"), "compile", rel, out],
                       cwd=ROOT, capture_output=True, text=True)
    return out if p.returncode == 0 and os.path.exists(out) else None


def tracked(patterns):
    p = subprocess.run(["git", "ls-files", "--"] + patterns, cwd=ROOT,
                       capture_output=True, text=True)
    if p.returncode != 0:
        return []
    return sorted(set(x for x in p.stdout.splitlines() if x))


def discover():
    return tracked(IM_PATTERNS), tracked(INIM_PATTERNS)


def collect(sim_missing=None, sim_broken=None):
    workdir = tempfile.mkdtemp(prefix="mir-rt-")
    ims, inims = discover()
    rows, declared, gaps = [], set(), []

    for rel in ims:
        if rel == sim_missing:
            gaps.append((rel, "not-in-run"))            # hook: pretend deleted
            continue
        if rel in EXPECTED_UNCOMPILABLE:
            declared.add(rel)
            continue
        if rel == sim_broken:
            gaps.append((rel, "compile-failed"))       # hook: pretend broken
            continue
        built = compile_im(rel, workdir)
        if not built:
            gaps.append((rel, "compile-failed"))
            continue
        rc, first, many, raw = run_one(built, workdir, "a%d" % len(rows))
        rows.append({"label": rel, "set": "compiled", "path": built,
                     "rc": rc, "first": first, "many": many, "raw": raw})

    for rel in inims:
        if rel == sim_missing:
            gaps.append((rel, "not-in-run"))
            continue
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            gaps.append((rel, "not-on-disk"))
            continue
        rc, first, many, raw = run_one(path, workdir, "b%d" % len(rows))
        if "read failed" in raw:
            if rel in EXPECTED_UNREADABLE_INIM:
                declared.add(rel)
                continue
            gaps.append((rel, "unreadable"))
            continue
        rows.append({"label": rel, "set": "precompiled", "path": path,
                     "rc": rc, "first": first, "many": many, "raw": raw})

    return rows, declared, gaps, len(ims) + len(inims), workdir


def report(rows, declared, gaps, expected):
    stats = {}
    for row in rows:
        ident = row["first"].get("ORIG-IDENTICAL", {})
        grp = row["set"]
        st = stats.setdefault(grp, {"n": 0, "bytes": 0, "differ": 0, "carried": 0})
        st["n"] += 1
        st["bytes"] += int(ident.get("bytes_orig", "0") or 0)
        if row["first"].get("ORIG-TRAILER", {}).get("bytes", "0") not in ("0", None):
            st["carried"] += 1
        differ = ident.get("differ", "?")
        print("MIR-CORPUS|program=%s|bytes=%s|differ=%s|set=%s"
              % (row["label"], ident.get("bytes_orig", "0"), differ, grp))
        if not (row["rc"] == 0 and differ == "0"):
            st["differ"] += 1
            print("MIR-MISMATCH|file=%s|rc=%d|differ=%s" % (row["label"], row["rc"], differ))
            for line in row["raw"].splitlines():
                if line.startswith(("ORIG-", "IR-SIZE")):
                    print("  " + line)

    for grp in ("compiled", "precompiled"):
        st = stats.get(grp, {"n": 0, "bytes": 0, "differ": 0, "carried": 0})
        print("MIR-IDENTICAL|set=%s|programs=%d|bytes=%d|differ=%d"
              % (grp, st["n"], st["bytes"], st["differ"]))
    tot = {k: sum(st.get(k, 0) for st in stats.values())
           for k in ("n", "bytes", "differ", "carried")}
    print("MIR-ROUNDTRIP|files=%d|identical=%d|differ=%d|trailer_carried=%d"
          % (tot["n"], tot["n"] - tot["differ"], tot["differ"], tot["carried"]))

    for rel, why in gaps:
        print("MIR-CORPUS-MISSING|program=%s|why=%s" % (rel, why))
    for rel in sorted(declared):
        print("MIR-CORPUS-DECLARED|program=%s" % rel)
    print("MIR-CORPUS-CHECK|expected=%d|measured=%d|declared=%d|missing=%d"
          % (expected, tot["n"], len(declared), len(gaps)))
    return tot["differ"], tot["n"], len(gaps)


def mir_controls(rows):
    controls, target = [], None
    for row in rows:
        funcs = [f for f in row["many"].get("IR-FUNC", [])
                 if int(f.get("ninsns", "0") or 0) >= 4]
        if funcs:
            target = (row, str(funcs[0]["id"]))
            break
    if not target:
        return controls
    row, fid = target
    outdir = tempfile.mkdtemp(prefix="mir-neg-")
    for name, extra in (("renumber-op", ["--perturb", fid, "1", "0", "25"]),
                        ("renumber-dst", ["--perturb", fid, "1", "1", "99"]),
                        ("swap-1-2", ["--swap", fid, "1", "2"])):
        rc, first, many, raw = run_one(row["path"], outdir, name, extra)
        d = first.get("ORIG-IDENTICAL", {}).get("differ", "0")
        controls.append((name, "red", "red" if d not in ("0", None) else "green", d))
    shutil.rmtree(outdir, ignore_errors=True)
    return controls


def corpus_controls(victim):
    out = []
    for hook, why in (("--simulate-missing", "not-in-run"),
                      ("--simulate-broken", "compile-failed")):
        p = subprocess.run([sys.executable, os.path.abspath(__file__), hook, victim,
                            "--no-selfcheck", "--quiet"],
                           cwd=ROOT, capture_output=True, text=True)
        named = ("MIR-CORPUS-MISSING|program=%s|why=%s" % (victim, why)) in p.stdout
        ok = (p.returncode != 0) and named
        out.append((hook.lstrip("-"), "red", "red" if ok else "green",
                    "rc=%d|named=%d" % (p.returncode, 1 if named else 0)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--no-selfcheck", action="store_true")
    ap.add_argument("--simulate-missing", default=None, metavar="REL")
    ap.add_argument("--simulate-broken", default=None, metavar="REL")
    args = ap.parse_args()

    if not build_harness():
        return 2

    rows, declared, gaps, expected, workdir = collect(args.simulate_missing, args.simulate_broken)
    differ, measured, nmissing = report(rows, declared, gaps, expected)

    controls = mir_controls(rows)
    for case, want, got, d in controls:
        print("MIR-NEGCTL|case=%s|want=%s|got=%s|differ=%s" % (case, want, got, d))
    red = sum(1 for c in controls if c[2] == "red")
    print("MIR-NEGCTL|controls=%d|red=%d|green=%d" % (len(controls), red, len(controls) - red))

    ctl = []
    if not args.no_selfcheck and rows:
        victim = sorted(r["label"] for r in rows)[0]
        ctl = corpus_controls(victim)
        for case, want, got, detail in ctl:
            print("MIR-CORPUSCTL|case=%s|want=%s|got=%s|%s" % (case, want, got, detail))
        cred = sum(1 for c in ctl if c[2] == "red")
        print("MIR-CORPUSCTL|controls=%d|red=%d|green=%d|victim=%s"
              % (len(ctl), cred, len(ctl) - cred, victim))

    shutil.rmtree(workdir, ignore_errors=True)
    ok = (differ == 0 and measured > 0 and nmissing == 0
          and len(controls) == 3 and red == 3
          and (args.no_selfcheck or (len(ctl) == 2 and all(c[2] == "red" for c in ctl))))
    print("MIR-VERDICT|%s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
