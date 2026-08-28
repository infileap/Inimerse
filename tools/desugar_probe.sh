#!/usr/bin/env bash
set -eu
bin="${1:-./inimerse}"
tmp_in=$(mktemp)
tmp_out=$(mktemp)
trap 'rm -f "$tmp_in" "$tmp_out"' EXIT
printf 'say@chat "hello"\nsay@ai "trace";\nprint "plain"\n' >"$tmp_in"
"$bin" --desugar "$tmp_in" "$tmp_out"
grep -F 'say_target("chat", "hello")' "$tmp_out" >/dev/null || { cat "$tmp_out"; exit 1; }
grep -F 'say_target("ai", "trace")' "$tmp_out" >/dev/null || { cat "$tmp_out"; exit 1; }
grep -F 'say "plain"' "$tmp_out" >/dev/null || { cat "$tmp_out"; exit 1; }
echo 'desugar probe: ok'
