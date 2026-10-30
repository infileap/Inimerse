#!/usr/bin/env python3
"""How much of the opcode set has the AOT path been *shown* to compute?

The question is not "what does aot_native.c implement".  A list read out of the
translator source is a claim, and a claim can be right about a construct no
program ever exercises.  The question is: over programs the translator accepts,
which opcodes did the interpreter actually execute, and did the native binary
print the same bytes?

Two filters, in this order:

  1. the translator accepted the program -- a refusal is not coverage, nothing ran;
  2. native stdout AND exit status equal the interpreter byte for byte.

Only then is the footprint counted, and the footprint comes from INIMERSE_HOT_OPS
on the interpreter, which counts what executed rather than what could execute.
A construct the translator can lower but no accepted program runs lands in
IR-AOT-MISSING.  OP_OR is why that matters: "say (a > 0) or (b > 0)" translates
fine today and is still missing from the 86 in-tree cases.

The corpus is printed with both of its halves.  No repository program is inside
the subset right now (every tracked .im and .inim is refused), so a fraction
printed alone would read as coverage of the repository; repo-rejected-why says
in words that the corpus was measured and everything in it was declined.

Readings:
  IR-AOT-COVER|corpus=inline-cases:N,repo:A/B|repo-rejected=R
              |repo-rejected-why=...|accepted=N|refused=N|differ=N
              |covered=C|of=67|basis=opcodes-executed-by-the-interpreter-over-programs-the-translator-accepts
  IR-AOT-OPCODE|op=OP_X|seen-in=N|verdict=covered|missing
  IR-AOT-MISSING|ops=...|count=N|because=no-accepted-program-executes-them
  IR-AOT-DIFF|case=NAME|first-diff=stdout|exit|cc|footprint|detail=...
  IR-AOT-REFUSED|case=NAME|why=...

Exit 0 when every accepted program prints the same bytes; 1 when one differs
(named, because a silent divergence is the one thing this path must never ship);
2 when a prerequisite is missing.  A refusal is never a failure: it is the
translator declining, counted and reported.
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tools" / "aot_native.test.py"
PIN = ROOT / "tools" / "aot_cover.pin.json"
OP_HOT = re.compile(r"^OP-HOT[|]op=(OP_[A-Z0-9_]+)[|]interp=([0-9]+)[|]")

OPCODES = """OP_MOV OP_LOADK_INT OP_LOADK_FLOAT OP_LOADK_STRING OP_LOADK_BOOL
OP_ADD OP_SUB OP_MUL OP_DIV OP_NEG OP_EQ OP_NEQ OP_LT OP_GT OP_LE OP_GE
OP_AND OP_OR OP_NOT OP_NEW_ARRAY OP_INDEX_GET OP_INDEX_SET OP_LOAD_GLOBAL
OP_STORE_GLOBAL OP_JUMP OP_JUMP_IF_FALSE OP_JUMP_IF_TRUE OP_CALL_BUILTIN
OP_PUSH_REG OP_SAY OP_WAIT OP_STOP OP_HALT OP_CALL_FUNC OP_RETURN OP_IS_NIL
OP_THREAD_START OP_THREAD_CTRL OP_THREAD_JOIN OP_THREAD_WAIT OP_THREAD_STATE
OP_LOCK OP_SEND OP_RECV OP_NEW_DICT OP_EQK OP_NEQK OP_DECLARE OP_RECORD OP_MOD
OP_NEW_SET OP_SET_INTERVAL OP_IN OP_MIN OP_MAX OP_BIND OP_TRY_START OP_THROW
OP_SET_ADD OP_THREAD_GOTO OP_CONCAT OP_YIELD OP_MAKE_FUNC OP_TRY_END
OP_CALL_VALUE OP_LOAD_CAPTURE OP_LOADK_I64""".split()


def refuse(why):
    print("aot_cover: REFUSED|" + why, file=sys.stderr)
    print("aot_cover: nothing was measured", file=sys.stderr)
    sys.exit(2)


def load_cases():
    spec = importlib.util.spec_from_file_location("aot_cases", CASES)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(mod)
    return mod


def head_sha():
    p = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                       capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else "<not a git tree>"


def run(cmd, cwd, env=None, timeout=300):
    try:
        d = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True,
                           env=env, timeout=timeout)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def footprint(interp, script):
    env = dict(os.environ, INIMERSE_HOT_OPS="1")
    env.pop("INIMERSE_IR_TABLE", None)
    rc, out, _err = run([str(interp), "--no-mods", "run", str(script)], ROOT, env)
    ops = {}
    for line in out.splitlines():
        m = OP_HOT.match(line)
        if m and int(m.group(2)) > 0:
            ops[m.group(1)] = int(m.group(2))
    return rc, ops


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", default=str(ROOT / "build"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--pin", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-repo", action="store_true")
    ap.add_argument("--check-pin", action="store_true",
                    help="re-run the measurement and compare it with tools/aot_cover.pin.json;"
                         " a pin nobody re-runs is a constant, not a reading")
    args = ap.parse_args()

    build = Path(args.build)
    interp = build / "inimerse"
    aot = build / "aot-native"
    if not interp.is_file() or not aot.is_file():
        refuse("no inimerse or aot-native under %s (build them first)" % build)
    if not CASES.is_file():
        refuse("no case list at %s" % CASES)

    mod = load_cases()
    cases = [(n, src) for n, src, _want in mod.EQUIVALENCE]
    if args.limit:
        cases = cases[:args.limit]
    if not cases:
        refuse("the inline case list is empty: a corpus of zero is not a reading")

    tmp = Path(tempfile.mkdtemp(prefix="aot_cover."))
    accepted, refused, differ = [], [], []
    seen = {}
    for name, src in cases:
        safe = re.sub(r"[^A-Za-z0-9_]", "_", name)
        script = tmp / (safe + ".im")
        script.write_text(src)
        cfile = tmp / (safe + ".c")
        exe = tmp / (safe + ".bin")
        trc, _o, terr = run([str(aot), "translate", str(script), str(cfile)], ROOT)
        if trc != 0:
            msg = (terr or "").strip().splitlines()
            refused.append((name, msg[-1][:120] if msg else "<no message>"))
            continue
        crc, _o, cerr = run([args.cc, "-O2", "-o", str(exe), str(cfile)], ROOT)
        if crc != 0:
            differ.append((name, "cc", (cerr or "").strip()[-120:]))
            continue
        irc, iout, _e = run([str(interp), "--no-mods", "run", str(script)], ROOT)
        nrc, nout, _e = run([str(exe)], ROOT)
        if irc != nrc or iout != nout:
            where = "exit" if irc != nrc else "stdout"
            differ.append((name, where, "interp=%s native=%s" % (irc, nrc)))
            continue
        _prc, ops = footprint(interp, script)
        if not ops:
            differ.append((name, "footprint", "accepted and equal, but no opcode was counted"))
            continue
        accepted.append(name)
        for op in ops:
            seen.setdefault(op, set()).add(name)

    # The repository corpus is taken from git, not from the filesystem, for the
    # same reason tools/mir_wired.test.py does: a program deleted on disk is
    # still part of the corpus this fraction is about, and a stray file that was
    # never committed is not.  The two globs are the ones the wiring gate uses,
    # so the denominators agree without a second opinion about what "the corpus"
    # means.
    repo_progs, repo_accepted = [], 0
    if not args.no_repo:
        tracked = subprocess.run(
            ["git", "ls-files", "--", "vtest/*.im", "examples/**/*.im"],
            cwd=str(ROOT), capture_output=True, text=True)
        repo_progs = [ROOT / rel for rel in tracked.stdout.split() if rel]
        for p in repo_progs:
            cfile = tmp / ("repo_" + re.sub(r"[^A-Za-z0-9_]", "_", p.name) + ".c")
            trc, _o, _e = run([str(aot), "translate", str(p), str(cfile)], ROOT)
            if trc == 0:
                repo_accepted += 1

    covered = sorted(op for op in OPCODES if op in seen)
    missing = sorted(op for op in OPCODES if op not in seen)
    repo_rejected = len(repo_progs) - repo_accepted
    print("IR-AOT-COVER|corpus=inline-cases:%d,repo:%d/%d|repo-rejected=%d"
          "|repo-rejected-why=no-tracked-program-is-inside-the-numeric-subset"
          "|accepted=%d|refused=%d|differ=%d|covered=%d|of=%d"
          "|basis=opcodes-executed-by-the-interpreter-over-programs-the-translator-accepts"
          "|not-a-capability-count=yes"
          % (len(cases), repo_accepted, len(repo_progs), repo_rejected,
             len(accepted), len(refused), len(differ), len(covered), len(OPCODES)))
    for op in covered:
        print("IR-AOT-OPCODE|op=%s|seen-in=%d|verdict=covered" % (op, len(seen[op])))
    for op in missing:
        print("IR-AOT-OPCODE|op=%s|seen-in=0|verdict=missing" % op)
    print("IR-AOT-MISSING|ops=%s|count=%d|because=no-accepted-program-executes-them"
          % (",".join(missing) or "none", len(missing)))
    for name, why, detail in differ:
        print("IR-AOT-DIFF|case=%s|first-diff=%s|detail=%s" % (name, why, detail))
    for name, msg in refused:
        print("IR-AOT-REFUSED|case=%s|why=%s" % (name, msg))

    # The divergence list is the other half of the same question: a program the
    # translator accepts and the native binary gets WRONG, with the expected
    # difference written down.  It is not a coverage cell -- nothing there is
    # counted as covered -- but a reader of the fraction deserves to know how
    # many accepted programs are known to disagree and where.  A case that
    # starts AGREEING is also reported: the list has then gone stale, and a
    # stale divergence list quietly excuses a difference that no longer exists.
    for name, src, want_im, want_native in getattr(mod, "DIVERGENCE", []):
        safe = "div_" + re.sub(r"[^A-Za-z0-9_]", "_", name)
        script = tmp / (safe + ".im")
        script.write_text(src)
        cfile = tmp / (safe + ".c")
        exe = tmp / (safe + ".bin")
        trc, _o, _e = run([str(aot), "translate", str(script), str(cfile)], ROOT)
        if trc != 0:
            print("IR-AOT-DIVERGENCE|case=%s|state=translator-refused-it-now|known=declared"
                  % name)
            continue
        crc, _o, _e = run([args.cc, "-O2", "-o", str(exe), str(cfile)], ROOT)
        if crc != 0:
            print("IR-AOT-DIVERGENCE|case=%s|state=cc-failed|known=declared" % name)
            continue
        irc, iout, _e = run([str(interp), "--no-mods", "run", str(script)], ROOT)
        nrc, nout, _e = run([str(exe)], ROOT)
        state = ("still-diverges" if (iout != nout or irc != nrc)
                 else "now-agrees-so-the-list-is-stale")
        print("IR-AOT-DIVERGENCE|case=%s|state=%s|interp-out=%s|native-out=%s"
              "|declared-interp=%s|declared-native=%s"
              % (name, state, repr(iout)[-24:], repr(nout)[-24:],
                 repr(want_im)[-24:], repr(want_native)[-24:]))
    print("aot_cover: cases=%d accepted=%d refused=%d differ=%d"
          % (len(cases), len(accepted), len(refused), len(differ)))

    if args.pin:
        PIN.write_text(json.dumps({
            "tree": head_sha(),
            "covered": len(covered),
            "of": len(OPCODES),
            "corpus": {"inline_cases": len(cases),
                       "repo_accepted": repo_accepted,
                       "repo_total": len(repo_progs)},
            "covered_ops": covered,
            "missing_ops": missing,
            "note": ("regenerate with tools/aot_cover.test.py --pin. The fraction a",
                     "reader sees in IR-COVER is this file, and this file names the",
                     "tree it was measured on so the checker can go back and re-run",
                     "the same measurement instead of trusting the number."),
        }, indent=2) + "\n")
        print("aot_cover: pin written to %s (tree=%s)" % (PIN, head_sha()))

    if args.check_pin:
        if not PIN.is_file():
            print("IR-AOT-PIN|tree=<no pin>|match=no|why=no-pin-file-to-recompute")
            return 1
        stored = json.loads(PIN.read_text())
        here = head_sha()
        match = (stored.get("tree") == here
                 and stored.get("covered") == len(covered)
                 and stored.get("of") == len(OPCODES))
        print("IR-AOT-PIN|tree=%s|head=%s|covered=%d|recomputed=%d|of=%d"
              "|corpus-pin=%d/%d|corpus-now=%d/%d|match=%s"
              % (stored.get("tree"), here, stored.get("covered", -1), len(covered),
                 len(OPCODES),
                 (stored.get("corpus") or {}).get("repo_accepted", -1),
                 (stored.get("corpus") or {}).get("repo_total", -1),
                 repo_accepted, len(repo_progs),
                 "yes" if match else "no"))
        if not match:
            print("IR-AOT-PIN-STALE|effect=the-numbers-a-reader-would-quote-no-longer-"
                  "describe-this-tree|fix=rerun-with---pin")
            return 1

    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
