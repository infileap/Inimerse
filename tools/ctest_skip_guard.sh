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
#   bash tools/ctest_skip_guard.sh --check-disabled <build-dir>
#       0  every test the registry reports as `(Disabled)` is declared
#       1  `ctest -N` failed -- the check could not see the registry, so it
#          says nothing about the tests; this is not exit 2, because "I could
#          not look" and "I looked and found an undeclared one" are different
#          findings and want different repairs
#       2  a `DISABLED TRUE` test is not declared.  This is the mode the Windows
#          job uses: it runs the whole suite in one `ctest` invocation, so there
#          is no per-test step to hang a guard on, and a disabled property is
#          the quietest way a test can stop existing -- ctest drops it from the
#          denominator, so `147 registered` prints as `out of 138` and reads as
#          a complete run.
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
  local build_dir="$1" names_file failure=0 entries=0 shown_sample=0
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
      # Show what the registry did contain, once.  Without this the reader is
      # told only that a name they can see in CMakeLists.txt does not exist,
      # which sends them to look for a rename that did not happen; the Windows
      # job's first run failed exactly this way and the cause was a trailing
      # `\r`, invisible in both the message and the file.
      if [ "$shown_sample" -eq 0 ]; then
        shown_sample=1
        printf '  collected instead (first 3): %s\n' \
          "$(head -3 "$names_file" | tr '\r' '?' | paste -sd ', ' -)" >&2
        printf '  a name that looks identical and compares unequal is a trailing CR, a\n' >&2
        printf '  trailing space, or output read as CRLF\n' >&2
      fi
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

# Read the registry, not a run, and count the tests `(Disabled)`.  The Windows
# job needs this shape: it runs all 147 cases inside one `ctest` invocation, so
# the per-test branch above is never called there, and `DISABLED TRUE` is the
# quietest way a test can stop existing -- ctest removes it from the `out of N`
# denominator, so the 9 hub regressions read as a suite that ran 138 and left
# nothing out.  Checking before the run also keeps the two findings apart: a
# missing declaration is a fact about CMakeLists.txt and can be repaired without
# reading compiler output.
check_disabled() {
  local build_dir="$1" listing
  if ! listing="$(ctest --test-dir "$build_dir" -N 2>&1)"; then
    printf 'cannot check the disabled set: ctest -N failed for %s\n%s\n' \
      "$build_dir" "$listing" >&2
    return 1
  fi
  local -a disabled=()
  mapfile -t disabled < <(
    printf '%s\n' "$listing" |
      sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9][0-9]*:[[:space:]]*//p' |
      # CRLF first: on Windows every line arrives with a trailing `\r`, so the
      # `(Disabled)` suffix pattern below would not match at all and the count
      # would read 0 -- a check reporting "nothing disabled" on a suite with
      # nine disabled tests is the failure this mode exists to refuse.
      tr -d '\r' |
      sed -n 's/[[:space:]]*(Disabled)$//p'
  )
  local name reason found
  local declared_count=0
  local undeclared_count=0
  for name in "${disabled[@]}"; do
    [ -n "$name" ] || continue
    found=''
    while IFS=$'\t' read -r declared_name declared_reason; do
      if [ "$declared_name" = "$name" ]; then
        found="$declared_reason"
        break
      fi
    done < <(declared)
    if [ -n "$found" ]; then
      declared_count=$((declared_count + 1))
      printf 'guard: disabled test %s is declared: %s\n' "$name" "$found"
      continue
    fi
    undeclared_count=$((undeclared_count + 1))
    printf 'guard: disabled test %s is not declared in %s\n' "$name" "$allowlist" >&2
    printf '  DISABLED TRUE removes it from the `out of N` denominator: the suite\n' >&2
    printf '  prints a smaller N and reads as complete.  Make the feature it needs\n' >&2
    printf '  exist, or declare it with a reason saying what would make the entry\n' >&2
    printf '  unnecessary.\n' >&2
  done
  if [ "$undeclared_count" -ne 0 ]; then
    printf 'guard: disabled=%s declared=%s undeclared=%s result=disabled-undeclared\n' \
      "${#disabled[@]}" "$declared_count" "$undeclared_count" >&2
    return 2
  fi
  printf 'guard: disabled=%s declared=%s undeclared=0 result=disabled-declared\n' \
    "${#disabled[@]}" "$declared_count"
  return 0
}

if [ "${1:-}" = "--check-disabled" ]; then
  check_disabled "${2:-build}"
  exit $?
fi

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
# The two ways `selected` can miss 1 are not the same wrong, so they do not
# share a message or a token.  A reader told "the name is wrong" while the
# pattern is only too wide goes and renames a test that is registered and
# passing; a reader told "the pattern is not unique" while the name is misspelled
# goes and narrows a regex that would still select nothing.  One token, one fix.
if [ "$selected" -ne 1 ]; then
  if [ "$selected" -eq 0 ]; then
    # Reached by a misspelled name (that is the `probe_dsiabled` cell proved in
    # the throwaway project) and by a test that was renamed or deleted.  It is
    # here because ctest answers a pattern that matches nothing with exit 0 and
    # the same `No tests were found!!!` on stderr that a disabled test prints, so
    # the step used to read as green.  Deleting this branch reinstates a silent
    # pass -- the one thing this script exists to refuse.
    printf 'guard: matched=0 executed=0 skipped=0 disabled=0 failed=0 result=no-such-test\n' >&2
    printf 'guard: -R "^%s$" selects no test -- the name is wrong (misspelled, renamed or deleted), and no test ran\n' \
      "$test_name" >&2
  else
    # Reached by a family pattern (`^probe_` over a project holding three).
    # Every matching test exists; the step would run several of them under one
    # name, so the number is the wrong one to read as "the test".
    printf 'guard: matched=%s executed=0 skipped=0 disabled=0 failed=0 result=ambiguous-match\n' \
      "$selected" >&2
    printf 'guard: -R "^%s$" selects %s tests -- the name is not wrong, the pattern is not unique; this loop runs exactly one test per step, so tighten the pattern\n' \
      "$test_name" "$selected" >&2
  fi
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
