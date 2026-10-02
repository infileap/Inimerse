#!/usr/bin/env python3
"""C-path vs self-host artifact comparison (BOARD row `selfhost-codegen-empty`).

Two ways to produce a program from the same `.im` source exist in this tree:

  C path        ./build/inimerse <target>
  self-host     ./build/inimerse selfhost/compiler.im <target>

Until this row was worked on, the second one silently produced *nothing*: the
engine's POSIX runtime registered `vm_exec` as an unsupported stub, and
`selfhost/compiler.im` never emitted an implicit `OP_RETURN` at the end of a
function body.  Both paths exit 0 either way, so no exit-code test could see it.

This tool records, per target, the pair

    (normalized bytecode hash, run-output hash)

for *both* paths, and the strict `--check` mode asserts that the two run
outputs are identical.  Hash pairs are recorded, not required to be equal: the
C compiler and the self-host compiler agree on behaviour but not on register
allocation, so their bytecode streams legitimately differ.  What must not
differ is what the program prints.

Normalization strips the engine's module-load preamble (`[TBP] …`,
`[infiverse mod] …`, `[verse_dist mod] …`) and the builtin-call echo line
(`[0]="str" [1]="len"`), which are host noise rather than program output.

Usage:
    tools/selfhost_compare.py [--engine PATH] [--json] [--check] [TARGET ...]

Exit codes: 0 = every compared target matched (or was skipped with a reason);
1 = at least one target's two paths disagreed.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILER_IM = os.path.join("selfhost", "compiler.im")

# Targets whose source does relative-path I/O (`read_file("tests/x.im")`,
# `import "lib.im"`) cannot be compared naively: the C path resolves those
# against the script's own directory, while a program run through `vm_exec`
# inherits the *outer* script's directory (`selfhost/`).  That is a runtime
# path-base difference, not a code-generation difference, so those targets are
# reported as `path-base` rather than as a parity failure.
PATH_IO = ("read_file(", "run_file(", "import ", "write_file(")

# The self-host front end (`selfhost/lexer.im` + `selfhost/parser.im`) implements
# the batch subset of the language only.  GUI/world scripts (`stage`, `sprite`,
# `when`, `on`, `forever`, `broadcast`, `wait`) are outside it, and the self-host
# compiler reports that as `parse error:` lines -- on *stdout*, with exit 0.
PARSE_ERROR = "parse error:"

# GUI/world scripts also *run forever* on the C path (they are game loops), so
# they must be recognised from the source and skipped before anything is spawned
# -- otherwise every one of them costs a full timeout.
GUI_STMT = re.compile(r"^\s*(stage|sprite|when|on|broadcast|forever|goto)\b", re.M)


def normalize(text):
    kept = []
    for line in text.splitlines():
        if line.startswith("["):
            continue
        kept.append(line.rstrip())
    return "\n".join(kept).strip()


def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def run(args, timeout=90):
    try:
        p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"


def ops_only(dump):
    """Normalized bytecode: the opcode column only.

    Register numbers and constant-pool indices are allocation decisions the two
    compilers are free to make differently; the sequence of operations is what
    they have to agree on.
    """
    return " ".join(
        line.split(",")[0] for line in dump.splitlines() if line and line[0].isdigit()
    )


# Live external effects make a target non-hermetic: `http_get` depends on the
# network, `exec` on a child process, `serial_open` on a device.  Their output is
# not reproducible run to run, so they cannot be parity targets -- hw_test.im
# calls `http_get("http://example.com")` and printed two different lengths across
# three consecutive self-host runs.
NON_HERMETIC = ("http_get(", "http_post(", "exec(", "serial_open(", "serial_write(")


def source_has_needle(path, needles):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            src = fh.read()
    except OSError:
        return False
    for line in src.splitlines():
        stripped = line.strip()
        if stripped.startswith("#") or stripped.startswith("//"):
            continue
        for needle in needles:
            if needle in line:
                return True
    return False


def source_has_path_io(path):
    return source_has_needle(path, PATH_IO)


def source_is_gui(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return bool(GUI_STMT.search(fh.read()))
    except OSError:
        return False


def probe(engine, target):
    """Run both paths on one target and collect the hash pairs."""
    rel = os.path.relpath(os.path.abspath(target), ROOT)
    abs_target = os.path.abspath(target)

    if source_is_gui(abs_target):
        return {"target": rel, "skip_reason": "gui-script", "c_bytes": 0, "sh_bytes": 0,
                "c_out_hash": "", "sh_out_hash": "", "c_bc_hash": "", "sh_bc_hash": "",
                "c_ops_hash": "", "sh_ops_hash": "", "output_match": False,
                "bytecode_match": False, "ops_match": False, "c_exit": 0, "sh_exit": 0}

    c_rc, c_out, _ = run([engine, rel])
    sh_rc, sh_out, _ = run([engine, COMPILER_IM, abs_target])

    c_bc_rc, c_bc, _ = run([engine, "bytecode", rel])
    sh_bc_rc, sh_bc, _ = run([engine, COMPILER_IM, "--dump", abs_target])

    c_norm, sh_norm = normalize(c_out), normalize(sh_out)
    c_dump = normalize(c_bc) if c_bc_rc == 0 else ""
    sh_dump = normalize(sh_bc) if sh_bc_rc == 0 else ""

    record = {
        "target": rel,
        "c_exit": c_rc,
        "sh_exit": sh_rc,
        "c_out_hash": sha(c_norm) if c_norm else "",
        "sh_out_hash": sha(sh_norm) if sh_norm else "",
        "c_bc_hash": sha(c_dump) if c_dump else "",
        "sh_bc_hash": sha(sh_dump) if sh_dump else "",
        "c_ops_hash": sha(ops_only(c_dump)) if c_dump else "",
        "sh_ops_hash": sha(ops_only(sh_dump)) if sh_dump else "",
        "output_match": c_norm == sh_norm,
        "bytecode_match": bool(c_dump) and c_dump == sh_dump,
        "ops_match": bool(c_dump and sh_dump) and ops_only(c_dump) == ops_only(sh_dump),
        "c_bytes": len(c_norm),
        "sh_bytes": len(sh_norm),
    }
    if source_has_path_io(abs_target):
        record["skip_reason"] = "path-base"
    elif source_has_needle(abs_target, NON_HERMETIC):
        record["skip_reason"] = "non-hermetic"
    elif PARSE_ERROR in sh_out:
        record["skip_reason"] = "unsupported-syntax"
    elif not c_norm and not sh_norm:
        # Library / data fixtures and scripts that need arguments print nothing
        # on either path; there is no observable behaviour to compare.
        record["skip_reason"] = "no-output"
    return record


def default_targets():
    targets = [os.path.join("selfhost", "test1.im")]
    tests_dir = os.path.join(ROOT, "selfhost", "tests")
    if os.path.isdir(tests_dir):
        for name in sorted(os.listdir(tests_dir)):
            if name.endswith(".im"):
                targets.append(os.path.join("selfhost", "tests", name))
    return targets


def check_mode(engine, targets):
    """Strict parity: the two paths must print the same thing, and not nothing.

    Targets outside the comparison's reach are skipped *with a printed reason*,
    never silently: relative-path I/O has a different base on each path, the
    self-host front end only implements the batch subset, and library fixtures
    print nothing at all.
    """
    reasons = {
        "path-base": "relative-path I/O: bases differ by design",
        "unsupported-syntax": "outside the self-host front end's subset",
        "no-output": "prints nothing on either path",
        "gui-script": "GUI/world script: runs forever on the C path",
        "non-hermetic": "live external effect (network/child process/device)",
    }
    failures = []
    compared = 0
    skipped = 0
    for target in targets:
        rec = probe(engine, target)
        reason = rec.get("skip_reason")
        if reason:
            skipped += 1
            print("skip   %-40s (%s)" % (rec["target"], reasons.get(reason, reason)))
            continue
        compared += 1
        status = "ok" if rec["output_match"] else "FAIL"
        print("%-6s %-40s c=%dB sh=%dB  c_out=%s sh_out=%s"
              % (status, rec["target"], rec["c_bytes"], rec["sh_bytes"],
                 rec["c_out_hash"][:12] or "-", rec["sh_out_hash"][:12] or "-"))
        if status == "FAIL":
            failures.append(rec["target"])
    if failures:
        print("\nselfhost parity FAILED for: %s" % ", ".join(failures), file=sys.stderr)
        print("The self-host path must print exactly what the C path prints.", file=sys.stderr)
        return 1
    print("\nselfhost parity OK: %d target(s) byte-identical, %d skipped" % (compared, skipped))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", default=os.path.join(ROOT, "build", "inimerse"))
    ap.add_argument("--json", action="store_true", help="emit the pair records as JSON")
    ap.add_argument("--check", action="store_true",
                    help="strict parity mode (used by CTest): nonzero exit on any mismatch")
    ap.add_argument("targets", nargs="*")
    args = ap.parse_args()

    engine = args.engine
    if not os.path.exists(engine):
        print("error: engine not found at %s (build first, or pass --engine)" % engine,
              file=sys.stderr)
        return 2

    targets = args.targets or default_targets()
    if args.check:
        return check_mode(engine, targets)

    records = [probe(engine, t) for t in targets]
    if args.json:
        print(json.dumps(records, indent=2))
        return 0

    print("%-34s %-8s %-8s %-10s %-10s %s"
          % ("target", "c_out", "sh_out", "c_bc", "sh_bc", "verdict"))
    matched = skipped = 0
    for r in records:
        if r.get("skip_reason"):
            verdict = "skip:" + r["skip_reason"]
            skipped += 1
        elif r["output_match"]:
            verdict = "ok"
            matched += 1
        else:
            verdict = "OUTPUT-DIFFERS"
        print("%-34s %-8s %-8s %-10s %-10s %s"
              % (r["target"],
                 r["c_out_hash"][:8] or "-", r["sh_out_hash"][:8] or "-",
                 r["c_bc_hash"][:8] or "-", r["sh_bc_hash"][:8] or "-",
                 verdict))
    print("\nmatched=%d skipped=%d total=%d" % (matched, skipped, len(records)))
    print("hash pairs are recorded, not required to be equal: the two compilers agree on")
    print("behaviour, not on register allocation.  Only the run outputs must match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
