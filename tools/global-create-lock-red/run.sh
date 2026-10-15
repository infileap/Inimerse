#!/usr/bin/env bash
# global-create-lock-red/run.sh -- run the ABBA probe and its discrimination control.
#
#   usage: run.sh <path-to-inimerse> [timeout-seconds]
#
# The timeout must be ABOVE the green runtime, or a red reading is just the
# timeout measuring itself.  Green here is ~4 s; the default is 25 s.
set -u
EXE=${1:?usage: run.sh <path-to-inimerse> [timeout-seconds]}
TMO=${2:-25}
DIR=$(cd "$(dirname "$0")" && pwd)
echo "interpreter: $EXE"
echo "timeout:     ${TMO}s"
for p in probe.im probe_int.im; do
    out=$(mktemp)
    timeout "$TMO" "$EXE" "$DIR/$p" >"$out" 2>/dev/null
    rc=$?
    last=$(tail -1 "$out" 2>/dev/null | tr -d '\r')
    case $rc in
        0)   verdict="GREEN" ;;
        124) verdict="RED (hang)" ;;
        *)   verdict="OTHER" ;;
    esac
    printf '  %-13s rc=%-4s %-10s last-line=%s\n' "$p" "$rc" "$verdict" "${last:-<none>}"
    rm -f "$out"
done
echo
echo "expected: probe.im = RED on a tree whose two create paths take different locks,"
echo "          probe_int.im = GREEN on any tree (it has no intern edge)."
