#!/usr/bin/env bash
# run_bounds_proof.sh -- the one command behind docs/streams/dec-formal-bounds.md.
#
# What it does, in order:
#   1. checks every pinned source line against the tree (a drifted line aborts,
#      so a stale model can never keep proving things about moved code);
#   2. generates the self-contained z3 script (tools/z3bounds/gen_bounds_proof.py);
#   3. runs that script on the interpreter that has z3 (the WSL one when it is
#      installed, else the Windows CPython), and prints one RESULT line per
#      variant plus PASS/FAIL against the expected reading.
#
# Exit status: 0 = every variant matched its expected reading (unsat proofs,
# sat counterexamples, red mutants, green candidate fixes); non-zero otherwise.
#
# Defaults: the WSL interpreter if it carries z3 (a POSIX run stays inside WSL
# and crosses no process boundary); otherwise the Windows CPython 3.14.7 with
# z3 5.3.2 at /mnt/c/Python314/python.exe.  The Windows path is slower and the
# host firewall reports each launch as a blocked process, so the local z3 is
# the preferred default and the exe is the fallback.
# Override with PY_WIN=... / WORK_WSL=... when the host moves.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [ -n "${PY_WIN:-}" ]; then
    :
elif python3 -c 'import z3' >/dev/null 2>&1; then
    PY_WIN=$(command -v python3)
else
    PY_WIN=/mnt/c/Python314/python.exe
fi
if [ -n "${WORK_WSL:-}" ]; then
    :
elif [ "${PY_WIN%.exe}" = "$PY_WIN" ] && [ "$(basename "$PY_WIN")" != "python.exe" ]; then
    WORK_WSL=${TMPDIR:-/tmp}/z3probe
else
    WORK_WSL=/mnt/c/Users/Lenovo/AppData/Local/Temp/z3probe
fi
SCRIPT_NAME=bounds_proof.py

if [ ! -x "$PY_WIN" ]; then
    echo "run_bounds_proof: no interpreter at $PY_WIN" >&2
    echo "run_bounds_proof: install z3 on the WSL side (pip install z3-solver) or put the z3-carrying CPython path in PY_WIN=..." >&2
    exit 2
fi
if [ "${PY_WIN%.exe}" = "$PY_WIN" ] && [ "$(basename "$PY_WIN")" != "python.exe" ]; then
    : # POSIX interpreter: no wslpath, no boundary crossing
elif ! command -v wslpath >/dev/null 2>&1; then
    echo "run_bounds_proof: wslpath is missing, cannot hand a Windows path to the interpreter" >&2
    exit 2
fi

mkdir -p "$WORK_WSL"
GEN="$ROOT/tools/z3bounds/gen_bounds_proof.py"
OUT="$WORK_WSL/$SCRIPT_NAME"

echo "== pin check + generate =========================================="
python3 "$GEN" --root "$ROOT" --out "$OUT"
echo
echo "== z3 run ($(basename "$PY_WIN")) =================================="
echo "interpreter: $PY_WIN"
echo "script:      $OUT"
echo "tree:        $ROOT ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'no git'))"
echo
if [ "${PY_WIN%.exe}" = "$PY_WIN" ] && [ "$(basename "$PY_WIN")" != "python.exe" ]; then
    "$PY_WIN" "$OUT"
else
    "$PY_WIN" "$(wslpath -w "$OUT")"
fi
