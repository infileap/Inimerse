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
#       0  the test ran and passed, or did not run with a declared exemption
#       1  the test ran and failed (its output is printed)
#       2  the test did not run (skipped, or DISABLED) and is not declared in
#          tools/known-skips.txt
#       3  `-R '^name$'` does not match exactly one test -- the name is wrong,
#          not the test.  ctest exits 0 in this case and prints the same
#          `No tests were found!!!` a disabled test prints, so without this
#          check a renamed test would read as a green step.
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

# `ctest -R` matching nothing is silent in the worst possible way: no `Test #N:`
# line, exit code 0, and the same `No tests were found!!!` on stderr that
# `DISABLED TRUE` produces.  A renamed or misspelled test therefore reads exactly
# like a test that ran, which is the failure this whole script exists to refuse.
# Count first: a guard that does not report must still be able to show it saw
# something, so "matched" is measured, never assumed.
selected="$(ctest --test-dir "$build_dir" -N -R "^${test_name}$" 2>/dev/null \
  | grep -cE '^[[:space:]]*Test +#[0-9]+' )"
if [ "$selected" -ne 1 ]; then
  printf 'guard: matched=0 executed=0 skipped=0 disabled=0 failed=0 result=no-such-test\n' >&2
  printf 'guard: -R "^%s$" selects %s test(s), not 1 -- the name is wrong, not the test\n' \
    "$test_name" "$selected" >&2
  exit 3
fi

if [ "$rc" -ne 0 ]; then
  printf 'guard: matched=1 executed=1 skipped=0 disabled=0 failed=1 result=failed\n' >&2
  printf 'test %s exited %s\n' "$test_name" "$rc" >&2
  exit 1
fi

# Two markers mean "this test did not run", and ctest counts them differently:
#
#   ***Skipped            the test ran and called its SKIP_RETURN_CODE; the
#                         per-test exit code is 0, and ctest keeps it inside the
#                         `out of N` denominator -- a skip is scored as a pass.
#   ***Not Run (Disabled) the test carries `DISABLED TRUE`; ctest removes it
#                         from the denominator entirely, so a suite of 147
#                         registered cases prints `out of 138` and the 9 that
#                         never ran are not even counted as missing.
#
# Both are the same claim -- it did not run -- and both need a declaration.  The
# distinction matters for the remedy only: a skip is usually a missing tool, a
# disabled test is usually a missing feature (the Windows engine has no POSIX
# hub, so eight hub regressions and http_large_package_regression are DISABLED
# there on purpose).  Either way the exit code above already ruled out a real
# failure, so a marker here cannot be hiding one.
marker=''          # result token: `skipped` or `disabled`
marker_text=''     # the same thing for prose
skip_line=''
if printf '%s\n' "$out" | grep -qF '***Skipped'; then
  marker='skipped'
  marker_text='skipped'
  skip_line="$(printf '%s\n' "$out" | grep -F '***Skipped' | head -1)"
elif printf '%s\n' "$out" | grep -qF '***Not Run'; then
  marker='disabled'
  marker_text='not run (disabled)'
  skip_line="$(printf '%s\n' "$out" | grep -F '***Not Run' | head -1)"
fi

if [ -n "$marker" ]; then
  skip_count=0
  disabled_count=0
  if [ "$marker" = 'skipped' ]; then skip_count=1; else disabled_count=1; fi
  while IFS=$'\t' read -r name reason; do
    if [ "$name" = "$test_name" ]; then
      printf 'guard: matched=1 executed=0 skipped=%s disabled=%s failed=0 result=%s-declared\n' \
        "$skip_count" "$disabled_count" "$marker"
      printf 'test %s %s, declared: %s\n' "$test_name" "$marker_text" "$reason"
      exit 0
    fi
  done < <(declared)
  printf 'guard: matched=1 executed=0 skipped=%s disabled=%s failed=0 result=%s-undeclared\n' \
    "$skip_count" "$disabled_count" "$marker" >&2
  printf 'test %s %s and is not declared in %s\n' "$test_name" "$marker_text" "$allowlist" >&2
  printf '  %s\n' "$skip_line" >&2
  printf 'a test that does not run is not a test that passes: fix the guard, or declare the\n' >&2
  printf 'marker in %s with a reason saying what would make the entry unnecessary\n' "$allowlist" >&2
  exit 2
fi

printf 'guard: matched=1 executed=1 skipped=0 disabled=0 failed=0 result=passed\n'
exit 0
