#!/usr/bin/env bash
# stream.sh — give every DSH conversation its own working tree.
#
# Several sessions share this repository, and a single shared working tree means
# one session's `git add -A` sweeps up another session's unfinished edits.  A
# git worktree gives each stream its own directory, its own branch and its own
# build/ directory, so they cannot collide.  See docs/BOARD.md §2.
#
#   tools/stream.sh new <slug>          start a stream at .worktrees/<slug>
#   tools/stream.sh list                list streams and their branch/dirty state
#   tools/stream.sh cd <slug>           print the stream's path (for cd "$(…)")
#   tools/stream.sh sync <slug>         merge current main into the stream
#   tools/stream.sh rm <slug>           remove a stream (refuses if dirty)
#   tools/stream.sh prune               drop worktrees whose branch was merged
#
# Slugs are lower-case, dash-separated: `verse-upp`, `econ-crp`, `docs-audit`.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WT_ROOT="$REPO_ROOT/.worktrees"
BRANCH_PREFIX="stream/"

die() { echo "stream: $*" >&2; exit 1; }

valid_slug() { [[ "$1" =~ ^[a-z0-9]+(-[a-z0-9]+)*$ ]]; }

wt_path() { echo "$WT_ROOT/$1"; }

require_slug() {
  [ -n "${1:-}" ] || die "missing <slug>"
  valid_slug "$1" || die "bad slug '$1' (use lower-case words joined by dashes)"
}

cmd_new() {
  require_slug "${1:-}"
  local slug="$1" path branch base
  path="$(wt_path "$slug")"
  branch="${BRANCH_PREFIX}${slug}"
  base="${2:-main}"
  [ -e "$path" ] && die "$path already exists"
  git -C "$REPO_ROOT" rev-parse --verify --quiet "$base" >/dev/null \
    || die "base '$base' is not a known revision"
  git -C "$REPO_ROOT" rev-parse --verify --quiet "$branch" >/dev/null \
    && die "branch '$branch' already exists"
  mkdir -p "$WT_ROOT"
  git -C "$REPO_ROOT" worktree add -b "$branch" "$path" "$base" || die "worktree add failed"

  # Point the new stream at its row on the board, if the slug is already there.
  local board="$path/docs/BOARD.md"
  if [ -f "$board" ] && grep -q "\`$slug\`" "$board"; then
    echo "stream: docs/BOARD.md already lists '$slug' -- mark the row 进行中"
  else
    echo "stream: '$slug' is not a row in docs/BOARD.md yet -- add one, or ask the coordinator"
  fi

  echo
  echo "stream '$slug' ready"
  echo "  path    $path"
  echo "  branch  $branch  (base $base)"
  echo
  echo "  cd $path"
  echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\"\$(nproc)\""
  echo "  tools/gate.sh            # must pass before this branch may be merged"
}

cmd_list() {
  echo "worktrees:"
  git -C "$REPO_ROOT" worktree list --porcelain | awk '
    /^worktree /   { p = substr($0, 10) }
    /^branch /     { b = substr($0, 8); sub("refs/heads/", "", b) }
    /^detached/    { b = "(detached)" }
    /^$/           { if (p != "") { printf "  %-46s %s\n", p, b; p = ""; b = "" } }
    END            { if (p != "") printf "  %-46s %s\n", p, b }'
  echo
  echo "streams (branch, working tree state):"
  local found=0
  for d in "$WT_ROOT"/*/; do
    [ -d "$d" ] || continue
    found=1
    local slug; slug="$(basename "$d")"
    local branch dirty ahead
    branch="$(git -C "$d" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
    if [ -z "$(git -C "$d" status --porcelain 2>/dev/null)" ]; then
      dirty="clean"
    else
      dirty="$(git -C "$d" status --porcelain 2>/dev/null | wc -l) changed"
    fi
    ahead="$(git -C "$d" rev-list --count main..HEAD 2>/dev/null || echo '?')"
    printf '  %-20s %-28s %-12s %s commit(s) ahead of main\n' "$slug" "$branch" "$dirty" "$ahead"
  done
  [ "$found" -eq 1 ] || echo "  (none -- start one with: tools/stream.sh new <slug>)"

  echo
  echo "unmerged stream branches:"
  local any=0
  while IFS= read -r b; do
    [ -n "$b" ] || continue
    any=1
    printf '  %s\n' "$b"
  done < <(git -C "$REPO_ROOT" for-each-ref --format='%(refname:short)' "refs/heads/$BRANCH_PREFIX*")
  [ "$any" -eq 1 ] || echo "  (none)"
}

cmd_cd() {
  require_slug "${1:-}"
  local path; path="$(wt_path "$1")"
  [ -d "$path" ] || die "no such stream '$1'"
  echo "$path"
}

cmd_sync() {
  require_slug "${1:-}"
  local path; path="$(wt_path "$1")"
  [ -d "$path" ] || die "no such stream '$1'"
  [ -z "$(git -C "$path" status --porcelain)" ] \
    || die "stream '$1' has uncommitted changes; commit or stash them first"
  git -C "$path" merge --no-edit main || die "merge conflict -- resolve it inside $path"
  echo "stream '$1' synced with main"
}

cmd_rm() {
  require_slug "${1:-}"
  local path; path="$(wt_path "$1")"
  [ -d "$path" ] || die "no such stream '$1'"
  if [ -n "$(git -C "$path" status --porcelain)" ] && [ "${2:-}" != "--force" ]; then
    git -C "$path" status --short >&2
    die "stream '$1' has uncommitted changes (re-run with --force to discard)"
  fi
  git -C "$REPO_ROOT" worktree remove --force "$path" || die "worktree remove failed"
  git -C "$REPO_ROOT" branch -D "${BRANCH_PREFIX}$1" >/dev/null 2>&1 \
    && echo "stream '$1' removed (branch ${BRANCH_PREFIX}$1 deleted)"
}

cmd_prune() {
  git -C "$REPO_ROOT" worktree prune
  local merged
  merged="$(git -C "$REPO_ROOT" branch --merged main --format='%(refname:short)' \
            | grep "^$BRANCH_PREFIX" || true)"
  if [ -z "$merged" ]; then
    echo "stream: no merged stream branches to prune"
    return 0
  fi
  while IFS= read -r b; do
    [ -n "$b" ] || continue
    git -C "$REPO_ROOT" branch -d "$b" >/dev/null 2>&1 && echo "pruned $b"
  done <<< "$merged"
}

case "${1:-}" in
  new)   shift; cmd_new "${1:-}" "${2:-main}" ;;
  list|ls) cmd_list ;;
  cd)    shift; cmd_cd "${1:-}" ;;
  sync)  shift; cmd_sync "${1:-}" ;;
  rm)    shift; cmd_rm "${1:-}" "${2:-}" ;;
  prune) cmd_prune ;;
  ""|-h|--help) sed -n '2,20p' "$0" ;;
  *) die "unknown command '$1' (try: new | list | cd | sync | rm | prune)" ;;
esac
