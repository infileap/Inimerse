#!/bin/bash
set -u
for d in sv-ref-prev sv-ref-be sv-ref-count; do
  wd="/home/sakiko/inimerse/.worktrees/$d"
  log="/home/sakiko/inimerse/.worktrees/surgery-verify/ctest_$d.log"
  echo "===== $d @ $(git -C "$wd" rev-parse --short HEAD)"
  ctest --test-dir "$wd/build" -j"$(nproc)" > "$log" 2>&1
  echo "  ctest_exit=$?"
  grep -E 'tests passed|tests failed|Total Test time' "$log" | tail -4
done
echo CTESTALLDONE
