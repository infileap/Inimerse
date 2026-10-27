#!/usr/bin/env bash
# run_bounds_proof.sh -- the one command behind docs/streams/dec-formal-bounds.md.
#
# What it does, in order:
#   1. checks every pinned source line against the tree (a drifted line aborts,
#      so a stale model can never keep proving things about moved code);
#   2. generates the self-contained z3 script (tools/z3bounds/gen_bounds_proof.py);
#   3. runs that script on the Windows interpreter that has z3, and prints one
#      RESULT line per variant plus PASS/FAIL against the expected reading.
#
# Exit status: 0 = every variant matched its expected reading (unsat proofs,
# sat counterexamples, red mutants, green candidate fixes); non-zero otherwise.
#
# Defaults are the measured facts of this box: Windows CPython 3.14.7 with
# z3 5.3.2 at /mnt/c/Python314/python.exe.  The WSL side has no z3 module.
# Override with PY_WIN=... / WORK_WSL=... when the host moves.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PY_WIN=${PY_WIN:-/mnt/c/Python314/python.exe}
WORK_WSL=${WORK_WSL:-/mnt/c/Users/Lenovo/AppData/Local/Temp/z3probe}
SCRIPT_NAME=bounds_proof.py

if [ ! -x "$PY_WIN" ]; then
    echo "run_bounds_proof: no interpreter at $PY_WIN" >&2
    echo "run_bounds_proof: put the z3-carrying CPython path in PY_WIN=..." >&2
    exit 2
fi
if ! command -v wslpath >/dev/null 2>&1; then
    echo "run_bounds_proof: wslpath is missing, cannot hand a Windows path to the interpreter" >&2
    exit 2
fi

mkdir -p "$WORK_WSL"
GEN="$ROOT/tools/z3bounds/gen_bounds_proof.py"
OUT="$WORK_WSL/$SCRIPT_NAME"

echo "== pin check + generate =========================================="
python3 "$GEN" --root "$ROOT" --out "$OUT"
echo
echo "== z3 run (Windows interpreter) =================================="
echo "interpreter: $PY_WIN"
echo "script:      $OUT"
echo "tree:        $ROOT ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'no git'))"
echo
"$PY_WIN" "$(wslpath -w "$OUT")"
