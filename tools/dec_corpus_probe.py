#!/usr/bin/env python3
"""Run the same .im program twice -- once through the C front-end, once through
the self-hosted compiler -- and require the two runs to agree.

Why this exists: `selfhost/compiler.im` parses a script, compiles it to the
host's bytecode and executes it with `vm_exec`.  That is the only place where
"the .im side can run the language" is actually observable, so it is also the
only place where a regression in that path is visible.  The first real defect
found here was a path-base mismatch: the C front-end resolves a script's
relative paths against the script's own directory, while a program compiled and
run by `compiler.im` resolved them against the compiler's directory, so the two
runs read different files and printed different lengths.

Three files in `selfhost/tests/` are deliberately skipped, each for a reason
that is about the file and not about the compiler:

  * `broadcast_test.im`, `cat_game.im` open with `stage 600, 400`: they are
    written in the stage DSL, which the C front-end routes through its GUI
    pipeline.  That pipeline has no bytecode form, so comparing the two runs
    there measures a missing feature, not a regression.
  * `hw_test.im` performs a live `http_get("http://example.com")`.  Measured:
    two runs of the same binary already differ in content while agreeing in
    byte count, so comparing two runs there measures the network.

The skip list is explicit and by name so that a new DSL or network file cannot
silently join it.

Output is one line, shaped so `tools/gated_run.py --shape` can hold it:
    dec-corpus: files=30 same=30 differ=0 skipped=3
Exit status is 0 only when every compared file agreed.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

BANNER = re.compile(r"^\[(mod|infiverse|verse_dist)")
SKIP = ("broadcast_test.im", "cat_game.im", "hw_test.im")


def filtered(text):
    return "\n".join(line for line in text.splitlines() if not BANNER.search(line))


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True,
                           errors="backslashreplace", timeout=timeout)
        return p.returncode, filtered(p.stdout or "")
    except subprocess.TimeoutExpired:
        return "timeout", ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=None, help="path to the inimerse binary")
    ap.add_argument("--pattern", default="selfhost/tests/*.im")
    ap.add_argument("--timeout", type=float, default=20.0)
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    binary = args.bin or os.path.join(root, "build", "inimerse")
    selfhost = os.path.join(root, "selfhost")
    compiler = os.path.join(selfhost, "compiler.im")
    if not os.path.exists(binary):
        print("dec-corpus: no binary at %s" % binary)
        return 2

    files = sorted(glob.glob(os.path.join(root, args.pattern)))
    compared = [p for p in files if os.path.basename(p) not in SKIP]
    same = 0
    differ = []
    for path in compared:
        name = os.path.basename(path)
        rel = "tests/" + name
        # Both sides get the same base: the C front-end changes into the
        # script's directory, so it is handed the path from `selfhost/`.
        rc_c, out_c = run([binary, rel], selfhost, args.timeout)
        rc_i, out_i = run([binary, compiler, rel], root, args.timeout)
        if rc_c == rc_i and out_c == out_i:
            same += 1
        else:
            differ.append((name, rc_c, rc_i, len(out_c), len(out_i)))
    for name, rc_c, rc_i, n_c, n_i in differ:
        print("dec-corpus: differ %s (C rc=%s %dB / im rc=%s %dB)"
              % (name, rc_c, n_c, rc_i, n_i))
    print("dec-corpus: files=%d same=%d differ=%d skipped=%d"
          % (len(compared), same, len(differ), len(files) - len(compared)))
    return 0 if not differ else 1


if __name__ == "__main__":
    sys.exit(main())
