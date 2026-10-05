#!/bin/bash
# build the three frozen refs, sequentially, logs in my own worktree
set -u
for d in sv-ref-prev sv-ref-be sv-ref-count; do
  wd="/home/sakiko/inimerse/.worktrees/$d"
  log="/home/sakiko/inimerse/.worktrees/surgery-verify/build_$d.log"
  echo "===== $d @ $(git -C "$wd" rev-parse --short HEAD)"
  cmake -S "$wd" -B "$wd/build" -DCMAKE_BUILD_TYPE=Release > "$log" 2>&1
  echo "  configure_exit=$?"
  cmake --build "$wd/build" -j"$(nproc)" >> "$log" 2>&1
  echo "  build_exit=$?"
  if [ -x "$wd/build/inimerse" ]; then
    echo "  BIN_OK $(sha256sum "$wd/build/inimerse" | cut -c1-16)"
  else
    echo "  BIN_MISSING"; tail -20 "$log"
  fi
done
echo "ALLDONE"
