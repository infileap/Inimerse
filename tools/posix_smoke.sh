#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -j2 >/dev/null
version="$(./inimerse --version)"
path="$(./inimerse where)"
case "$version" in inimerse\ *) ;; *) echo "unexpected version: $version" >&2; exit 1;; esac
test -x "$path"
test "$(printf 'X\n' | ./inimerse --version >/dev/null; echo ok)" = ok
echo "POSIX smoke: ok ($version)"
make clean >/dev/null
