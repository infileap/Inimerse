#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -j2 >/dev/null
version="$(./inimerse --version)"
path="$(./inimerse where)"
caps="$(./inimerse capabilities)"
case "$version" in inimerse\ *) ;; *) echo "unexpected version: $version" >&2; exit 1;; esac
test -x "$path"
echo "$caps" | grep -qx 'threads'
echo "$caps" | grep -qx 'fiber'
echo "$caps" | grep -qx 'posix_fs'
test "$(printf 'X\n' | ./inimerse --version >/dev/null; echo ok)" = ok
echo "POSIX smoke: ok ($version)"
make clean >/dev/null
