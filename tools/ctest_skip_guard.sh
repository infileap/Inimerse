#!/usr/bin/env bash
#
# Run one registered CTest case and refuse a skip.
#
# Why a script instead of a `grep` in the workflow: a test that exits with its
# SKIP_RETURN_CODE (77 today) is reported by ctest as `***Skipped`, the
# per-test exit code stays 0, and the job summary still says the suite passed.
# The loop in .github/workflows/linux-build.yml therefore cannot see it: it
# branches on the exit code, and a skip keeps that code at 0.
#
# The direction that matters in CI is the opposite of the local one.  Locally,
# tools/gate.sh counts `***Skipped` in the ctest stage and treats a non-zero
# count as a failure.  In CI the count was never taken at all, so a test that
# stopped running was indistinguishable from a test that kept passing.
#
# Usage:
#   bash tools/ctest_skip_guard.sh <build-dir> <test-name>
#       0  the test ran and passed, or skipped with a declared exemption
#       1  the test ran and failed (its output is printed)
#       2  the test skipped and is not declared in tools/known-skips.txt
#
#   bash tools/ctest_skip_guard.sh --check-allowlist <build-dir>
#       0  every declared exemption names a registered test and gives a reason
#       1  one does not -- an exemption for a test that no longer exists, or
#          one declared without a reason, is not an exemption but a licence
#          nobody audits
#
# Override the exemption list with INIMERSE_KNOWN_SKIPS=<path> (the only reason
# that knob exists is so the two branches below can be shown to be reachable).
set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
allowlist="${INIMERSE_KNOWN_SKIPS:-${script_dir}/known-skips.txt}"

# Print `<name> <reason>` pairs, one per line; refuse entries malformed enough
# that the caller could not tell which test was meant.
declared() {
  if [ ! -f "$allowlist" ]; then
    printf 'exemption list %s does not exist\n' "$allowlist" >&2
    return 1
  fi
  awk '
    { sub(/\r$/, "") }
    /^[[:space:]]*$/ { next }
    /^[[:space:]]*#/ { next }
    {
      line = $0
      hash = index(line, "#")
      if (hash > 0) { name = substr(line, 1, hash - 1); reason = substr(line, hash + 1) }
      else          { name = line;                      reason = "" }
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", name)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", reason)
      printf "%s\t%s\n", name, reason
    }
  ' "$allowlist"
}

check_allowlist() {
  local build_dir="$1" names_file failure=0 entries=0
  names_file="$(mktemp)"
  trap 'rm -f "$names_file"' RETURN
  if ! bash "${script_dir}/ctest_enumerate.sh" "$build_dir" > "$names_file"; then
    printf 'cannot check the exemption list: enumeration failed\n' >&2
    return 1
  fi
  while IFS=$'\t' read -r name reason; do
    [ -n "$name" ] || continue
    entries=$((entries + 1))
    if ! grep -qxF "$name" "$names_file"; then
      printf 'exemption for %s: no such registered test\n' "$name" >&2
      failure=1
    fi
    if [ -z "$reason" ]; then
      printf 'exemption for %s: no reason given\n' "$name" >&2
      failure=1
    fi
  done < <(declared) || failure=1
  if [ "$failure" -ne 0 ]; then
    printf 'refusing the exemption list: %s entr(ies) checked in %s\n' "$entries" "$allowlist" >&2
    return 1
  fi
  printf 'exemption list: %s declared entr(ies), all registered and justified\n' "$entries"
  return 0
}

if [ "${1:-}" = "--check-allowlist" ]; then
  check_allowlist "${2:-build}"
  exit $?
fi

build_dir="${1:-build}"
test_name="${2:-}"
if [ -z "$test_name" ]; then
  printf 'usage: bash tools/ctest_skip_guard.sh <build-dir> <test-name>\n' >&2
  printf '       bash tools/ctest_skip_guard.sh --check-allowlist <build-dir>\n' >&2
  exit 1
fi

out="$(ctest --test-dir "$build_dir" -R "^${test_name}$" --output-on-failure 2>&1)"
rc=$?
printf '%s\n' "$out"

if [ "$rc" -ne 0 ]; then
  printf 'test %s exited %s\n' "$test_name" "$rc" >&2
  exit 1
fi

# `***Skipped` appears on the per-test line only when the test did not run.
# Read the exit code first (above) so a genuine failure is never mistaken for
# a skip, then treat any skip marker as a skip -- one test per invocation means
# there is no other test whose marker this could be.
if printf '%s\n' "$out" | grep -qF '***Skipped'; then
  skip_line="$(printf '%s\n' "$out" | grep -F '***Skipped' | head -1)"
  while IFS=$'\t' read -r name reason; do
    if [ "$name" = "$test_name" ]; then
      printf 'test %s skipped, declared: %s\n' "$test_name" "$reason"
      exit 0
    fi
  done < <(declared)
  printf 'test %s skipped and is not declared in %s\n' "$test_name" "$allowlist" >&2
  printf '  %s\n' "$skip_line" >&2
  printf 'a test that does not run is not a test that passes: fix the guard, or declare the\n' >&2
  printf 'skip in %s with a reason saying what would make the entry unnecessary\n' "$allowlist" >&2
  exit 2
fi

exit 0
