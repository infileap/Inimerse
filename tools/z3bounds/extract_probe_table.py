#!/usr/bin/env python3
"""T3 probe table: regenerate the construct list from its source of truth.

The table is a READING, not a list: it is produced by parsing
tools/dec_construct_matrix.py (never by importing it -- the module must not be
able to hand us a table it does not contain), taking PROBES and LIBX out
literally with ast, then running both front ends exactly the way that tool
does.  Anything the tool cannot derive is printed as unexplained, not guessed.

Exit codes: 0 readings consistent / 1 self-evidence mismatch (a number disagreed
with the pinned baseline) / 2 the tree does not look like the one described
(source table or binary missing).
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

BANNER_PREFIXES = ("[mod", "[infiverse", "[verse_dist")
SOURCE_REL = os.path.join("tools", "dec_construct_matrix.py")
COMPILER_REL = os.path.join("selfhost", "compiler.im")

# The baseline is a PINNED READING taken on the tree named in the page that
# quotes it, not a value re-derived here.  A mismatch exits 1 and says so.
BASELINE = {"total": 30, "same": 28, "differ": 2}

# A non-same case must carry a why.  The two entries below come from the source
# comment quoted in the page; a case that differs for a reason not written here
# is printed as unexplained=yes and counted, never silently absorbed.
WHY = {
    "repeat-count": "selfhost parse_statement has no 'repeat <n> { }' statement form: the body is never parsed, the run still exits 0 and prints MARK=0 -- a silent wrong answer, not a refusal",
    "do-until": "selfhost parse_statement has no 'do { } until <cond>' statement form: the body is never parsed, the run still exits 0 and prints MARK=0 -- a silent wrong answer, not a refusal",
}


def sha8(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:8]


def sha8_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def load_table(repo: str) -> tuple[list[tuple[str, str, object]], str, int]:
    path = os.path.join(repo, SOURCE_REL)
    if not os.path.isfile(path):
        raise SystemExit(f"TABLE-MISSING|{SOURCE_REL}")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    tree = ast.parse(text, filename=path)
    probes = None
    libx = None
    line = 0
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name == "PROBES":
                probes = ast.literal_eval(node.value)
                line = node.lineno
            elif name == "LIBX":
                libx = ast.literal_eval(node.value)
    if probes is None or libx is None:
        raise SystemExit(f"TABLE-MISSING|{SOURCE_REL}:{'PROBES' if probes is None else 'LIBX'}")
    if not isinstance(probes, list) or not probes:
        raise SystemExit(f"TABLE-MALFORMED|{SOURCE_REL}:{line}")
    return probes, libx, line


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
        ln for ln in p.stdout.splitlines() if not ln.startswith(BANNER_PREFIXES)
    )
    return p.returncode, out


def mark_of(out: str) -> str:
    """The reading of one run: the MARK= payload, or a compact failure shape."""
    m = re.search(r"MARK=([^\n]*)", out)
    if m:
        return "MARK=" + m.group(1).strip()
    flat = out.strip().replace("\n", " / ")
    return "OUT=" + (flat[:60] if flat else "<empty>")


def classify(rc_c: int, out_c: str, rc_s: int, out_s: str) -> str:
    if "MARK=" not in out_c:
        return "c-fails"
    if rc_s == 124:
        return "sh-fails"
    if rc_c == rc_s and out_c == out_s:
        return "same"
    if rc_c != rc_s or "MARK=" not in out_s:
        return "sh-fails"
    return "differ"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--bin", default="/tmp/z3build/inimerse")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--dump", default="/tmp/z3work/probe_scripts", help="where the 30 probe sources are written, with a sha manifest")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    repo = os.path.abspath(args.root)
    probes, libx, table_line = load_table(repo)
    binary = args.bin if os.path.isabs(args.bin) else os.path.join(repo, args.bin)
    compiler = os.path.join(repo, COMPILER_REL)
    if not os.path.isfile(binary):
        print(f"BIN-MISSING|{binary}|build it out of tree: cmake -S {repo} -B /tmp/z3build && cmake --build /tmp/z3build -j")
        return 2
    if not os.path.isfile(compiler):
        print(f"SELFHOST-MISSING|{COMPILER_REL}")
        return 2

    print(f"SOURCE|{SOURCE_REL}:{table_line}|table=PROBES|libx_sha256={sha8(libx)}")
    print(f"BIN|path={binary}|sha256={sha8_file(binary)}|size={os.path.getsize(binary)}")
    print(f"SELFHOST|{COMPILER_REL}|sha256={sha8_file(compiler)}")
    os.makedirs(args.dump, exist_ok=True)

    counts = {"same": 0, "differ": 0, "c-fails": 0, "sh-fails": 0}
    manifest = []
    rows = []
    with tempfile.TemporaryDirectory(prefix="t3-probe-") as tmp:
        with open(os.path.join(tmp, "libx.im"), "w", encoding="utf-8") as fh:
            fh.write(libx)
        for name, src, _note in probes:
            path = os.path.join(tmp, name.replace(" ", "_") + ".im")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(src)
            dump_path = os.path.join(args.dump, name.replace(" ", "_") + ".im")
            with open(dump_path, "w", encoding="utf-8") as fh:
                fh.write(src)
            s_sha = sha8(src)
            manifest.append({"case": name, "script_sha256": s_sha, "bytes": len(src.encode("utf-8"))})
            rc_c, out_c = run([binary, path], tmp, args.timeout)
            rc_s, out_s = run([binary, compiler, path], tmp, args.timeout)
            verdict = classify(rc_c, out_c, rc_s, out_s)
            counts[verdict] += 1
            why = WHY.get(name) if verdict != "same" else None
            ev = next((ln.strip() for ln in out_s.splitlines() if "parse error" in ln), "")
            row = {
                "case": name,
                "script_sha256": s_sha,
                "c": f"{rc_c}:{mark_of(out_c)}",
                "sh": f"{rc_s}:{mark_of(out_s)}",
                "verdict": verdict,
                "why": why or ("-" if verdict == "same" else "unexplained"),
                "evidence": ev,
            }
            rows.append(row)
            if not args.quiet:
                print(
                    f"case={row['case']}|script_sha256={row['script_sha256']}|c={row['c']}|sh={row['sh']}|verdict={row['verdict']}|why={row['why']}"
                )

    total = len(rows)
    unexplained = [r for r in rows if r["why"] == "unexplained"]
    for r in rows:
        if r["verdict"] != "same" and r["why"] not in ("unexplained",):
            print(f"WHY|{r['case']}|{r['why']}|sh_first_error={r['evidence'] or '<none>'}")
    print(f"VERDICTS|same={counts['same']}|differ={counts['differ']}|c-fails={counts['c-fails']}|sh-fails={counts['sh-fails']}|skip=0")
    print(f"CASES|total={total}|same={counts['same']}|differ={counts['differ']}|from_source={SOURCE_REL}:{table_line}")
    print(f"UNEXPLAINED|n={len(unexplained)}|" + ",".join(r["case"] for r in unexplained))
    manifest_path = os.path.join(args.dump, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump({"source": f"{SOURCE_REL}:{table_line}", "libx_sha256": sha8(libx), "cases": manifest}, fh, indent=1)
    print(f"SCRIPTS|dir={args.dump}|n={len(manifest)}|manifest={manifest_path}")

    mismatch = total != BASELINE["total"] or counts["same"] != BASELINE["same"] or counts["differ"] != BASELINE["differ"]
    print(
        f"EXPECT|total={BASELINE['total']}|same={BASELINE['same']}|differ={BASELINE['differ']}|status={'match' if not mismatch else 'MISMATCH'}"
        f"|source=pinned-baseline|note=the baseline is a pinned reading,derived numbers above are measured"
    )
    if mismatch:
        print(f"EXPECT-FAIL|measured total={total} same={counts['same']} differ={counts['differ']} != pinned {BASELINE['total']}/{BASELINE['same']}/{BASELINE['differ']}")
    if len(unexplained):
        print("UNEXPLAINED-FAIL|a non-same case carries no why; write one before quoting this table")
        return 1
    return 1 if mismatch else 0


if __name__ == "__main__":
    sys.exit(main())
