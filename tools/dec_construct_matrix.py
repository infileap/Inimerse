#!/usr/bin/env python3
"""Construct matrix for stage 3: does the self-hosted compiler handle the
language surface this tool chain actually uses?

For every construct we write one tiny program that exercises it and prints a
marker, then run the *same* source through two paths:

  C front end      : <bin> <abs>/probe.im                 (parse + interpret)
  self-hosted front: <bin> selfhost/compiler.im <abs>/probe.im  (parse + compile + vm_exec)

and compare (rc, stdout).  Absolute paths are used on purpose: the compiled
program's relative-path base is not the subject here (see
docs/streams/dec-progress.md, "第 3 级的第二道门").

Usage:
  python3 tools/dec_construct_matrix.py [--bin build/inimerse] [--timeout 20]

Output: one line per construct
  construct <name>: same | differ | c-fails | sh-fails | skip|<reason>
followed by a summary line
  dec-constructs: probes=N same=N differ=N c-fails=N sh-fails=N
Exit code 0 only when every probe is `same`.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

BANNER_PREFIXES = ("[mod", "[infiverse", "[verse_dist")

# (name, source, note).  Every probe must exit 0 and print MARK on the C path.
PROBES = [
    ("int-arithmetic", 'say "MARK=" + str(2 + 3 * 4 - 1)\n', None),
    ("string-concat", 'a = "x"\nb = "y"\nsay "MARK=" + a + b + str(len(a + b))\n', None),
    ("array-literal", 'xs = [1, 2, 3]\nsay "MARK=" + str(xs[1]) + "," + str(len(xs))\n', None),
    ("dict-literal", 'd = {"k": 7}\nsay "MARK=" + str(d["k"])\n', None),
    ("if-elif-else", 'n = 2\nif n == 1 {\n    say "MARK=one"\n} elif n == 2 {\n    say "MARK=two"\n} else {\n    say "MARK=many"\n}\n', None),
    ("while-loop", 'i = 0\ns = 0\nwhile i < 4 {\n    s = s + i\n    i = i + 1\n}\nsay "MARK=" + str(s)\n', None),
    ("for-loop", 's = 0\nfor x in [1, 2, 3] {\n    s = s + x\n}\nsay "MARK=" + str(s)\n', None),
    ("func-call", 'func twice(v) {\n    return v * 2\n}\nsay "MARK=" + str(twice(21))\n', None),
    ("recursion", 'func fib(n) {\n    if n < 2 {\n        return n\n    }\n    return fib(n - 1) + fib(n - 2)\n}\nsay "MARK=" + str(fib(10))\n', None),
    ("global", 'global g_n\ng_n = 5\nfunc bump() {\n    g_n = g_n + 1\n}\nbump()\nsay "MARK=" + str(g_n)\n', None),
    ("import", 'import "libx.im"\nsay "MARK=" + str(libx_value())\n', None),
    ("stop-all", 'say "MARK=before"\nstop all\n', None),
    ("elif-chain", 'x = 3\nif x == 1 {\n    say "MARK=a"\n} elif x == 2 {\n    say "MARK=b"\n} elif x == 3 {\n    say "MARK=c"\n} else {\n    say "MARK=d"\n}\n', None),
    ("nested-dict-array", 'd = {"a": [1, {"b": 2}]}\nsay "MARK=" + str(d["a"][1]["b"])\n', None),
    ("float-literal", 'f = 1.5 + 2.25\nsay "MARK=" + str(f)\n', None),
    ("bool-and-or", 'a = true\nb = false\nsay "MARK=" + str(a and not b)\n', None),
    ("range-call", 's = 0\nfor i in range(0, 4) {\n    s = s + i\n}\nsay "MARK=" + str(s)\n', None),
    ("push-pop", 'xs = []\npush(xs, 1)\npush(xs, 2)\nlast = pop(xs)\nsay "MARK=" + str(last) + "," + str(len(xs))\n', None),
    ("thread", 'thread worker:() {\n    say "MARK=thread"\n}\nstart worker\n', None),
    ("not-standalone", 'a = false\nsay "MARK=" + str(not a)\n', None),
    ("unary-minus", 'x = 5\nsay "MARK=" + str(-x + 1)\n', None),
    ("nil-compare", 'v = nil\nif v == nil {\n    say "MARK=nil"\n} else {\n    say "MARK=x"\n}\n', None),
    ("string-index", 's = "abc"\nsay "MARK=" + substr(s, 1, 1) + str(len(s))\n', None),
    ("dict-iterate", 'd = {"a": 1, "b": 2}\ns = 0\nfor k in keys(d) {\n    s = s + d[k]\n}\nsay "MARK=" + str(s)\n', None),
    ("break-loop", 's = 0\nfor i in [1, 2, 3, 4] {\n    if i == 3 {\n        break\n    }\n    s = s + i\n}\nsay "MARK=" + str(s)\n', None),
    ("return-void", 'func noop() {\n    return\n}\nnoop()\nsay "MARK=void"\n', None),
    ("member-type", 'v = [1]\nsay "MARK=" + v.type\n', None),
    # 这个方言里 repeat 是「计数循环」、do/until 才是「至少一次」——
    # `repeat { } until cond` 两种写法都不存在(C 前端报 expected '':'')。
    ("repeat-count", 'i = 0\nrepeat 3 {\n    i = i + 1\n}\nsay "MARK=" + str(i)\n', None),
    ("do-until", 'i = 0\ndo {\n    i = i + 1\n} until i == 3\nsay "MARK=" + str(i)\n', None),
    ("when-await", 'thread worker2:() {\n    say "MARK=w"\n}\nstart worker2\nstop all\n', None),
]

LIBX = 'func libx_value() {\n    return 41\n}\n'


def run(cmd: list[str], cwd: str, timeout: float) -> tuple[int, str]:
    try:
        p = subprocess.run(
            cmd,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="backslashreplace",
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return 124, "<timeout>"
    out = "\n".join(
        line for line in p.stdout.splitlines() if not line.startswith(BANNER_PREFIXES)
    )
    return p.returncode, out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="build/inimerse")
    ap.add_argument("--timeout", type=float, default=20.0)
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    binary = args.bin if os.path.isabs(args.bin) else os.path.join(repo, args.bin)
    compiler = os.path.join(repo, "selfhost", "compiler.im")

    counts = {"same": 0, "differ": 0, "c-fails": 0, "sh-fails": 0, "skip": 0}
    with tempfile.TemporaryDirectory(prefix="dec-constructs-") as tmp:
        with open(os.path.join(tmp, "libx.im"), "w", encoding="utf-8") as fh:
            fh.write(LIBX)
        for name, src, _note in PROBES:
            path = os.path.join(tmp, name.replace(" ", "_") + ".im")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(src)
            rc_c, out_c = run([binary, path], tmp, args.timeout)
            rc_s, out_s = run([binary, compiler, path], tmp, args.timeout)
            if "MARK=" not in out_c:
                verdict = "c-fails"
            elif rc_s == 124:
                verdict = "sh-fails"
            elif rc_c == rc_s and out_c == out_s:
                verdict = "same"
            elif rc_c != rc_s or "MARK=" not in out_s:
                verdict = "sh-fails"
            else:
                verdict = "differ"
            counts[verdict] += 1
            print(f"construct {name}: {verdict}")
            if verdict != "same":
                print(f"  C  rc={rc_c} out={out_c!r}")
                print(f"  SH rc={rc_s} out={out_s!r}")
        del tmp
    print(
        "dec-constructs: probes={} same={} differ={} c-fails={} sh-fails={}".format(
            len(PROBES), counts["same"], counts["differ"], counts["c-fails"], counts["sh-fails"]
        )
    )
    return 0 if counts["same"] == len(PROBES) else 1


if __name__ == "__main__":
    sys.exit(main())
