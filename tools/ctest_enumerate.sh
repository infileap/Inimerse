#!/usr/bin/env bash
#
# Print one registered CTest name per line for a build directory.
#
# Why this is a script and not a copy of the same `sed` in two workflows:
# `ctest -N` right-aligns the test number, so the gap between "Test" and "#1"
# is wider than the one before "#100".  A pattern that demanded exactly one
# space therefore collected only the three-digit tests, and the loop that
# consumed it looked like it had covered the whole suite:
#
#   * the 0.5.0 release ran 24 of 123 tests (fixed in 29402f8),
#   * .github/workflows/linux-build.yml ran 35 of 134 (this script).
#
# The sed pattern is the symptom; the disease is that nothing compared the
# number of tests collected with the number ctest says are registered.  So the
# comparison lives here, once, and a broken extraction fails the job instead of
# quietly shrinking it.
#
# Usage: bash tools/ctest_enumerate.sh [build-dir]   (default: build)
#   stdout: test names, one per line
#   stderr: why the enumeration was refused; exit 1
set -uo pipefail

build_dir="${1:-build}"

if ! listing="$(ctest --test-dir "$build_dir" -N 2>&1)"; then
  printf 'ctest -N failed for build directory %s\n%s\n' "$build_dir" "$listing" >&2
  exit 1
fi

mapfile -t names < <(
  printf '%s\n' "$listing" |
    sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9][0-9]*:[[:space:]]*//p'
)
registered="$(printf '%s\n' "$listing" | sed -n 's/^[[:space:]]*Total Tests:[[:space:]]*//p')"

if [ "${#names[@]}" -eq 0 ]; then
  printf 'no tests collected from %s (ctest -N printed %s registered)\n' \
    "$build_dir" "${registered:-?}" >&2
  exit 1
fi
if [ "${#names[@]}" -ne "${registered:-0}" ]; then
  printf 'collected %s of %s registered tests from %s\n' \
    "${#names[@]}" "${registered:-?}" "$build_dir" >&2
  exit 1
fi

printf '%s\n' "${names[@]}"
