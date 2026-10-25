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
    sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9][0-9]*:[[:space:]]*//p' |
    # Windows writes CRLF into a pipe.  A `\r` survives command substitution --
    # it is stripped only at the end of the *last* line -- so every name comes
    # out as `hub_dist_regression\r` and compares unequal to the same name
    # without it.  That is not hypothetical: the first run of the exemption
    # check on the Windows job reported all eleven declared entries as `no such
    # registered test` while `ctest -N` printed every one of them.
    cat  # DRILL: CR stripping removed on purpose |
    # `ctest -N` marks a disabled test in the listing itself:
    # `Test #3: hub_dist_regression (Disabled)`.  The name is what precedes the
    # marker; leaving the marker on would make nine of the eleven Windows
    # exemptions name tests that "do not exist", and -- worse -- the exemption
    # check would then be comparing against a string no run ever prints.
    # Note the missing `-n`: this substitution must print every line, and `-n`
    # would print only the lines it changed -- one name out of three.
    sed 's/[[:space:]]*(Disabled)$//'
)
registered="$(printf '%s\n' "$listing" | cat  # DRILL: CR stripping removed on purpose | sed -n 's/^[[:space:]]*Total Tests:[[:space:]]*//p')"

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

# Print the shape of what was read, on the success path as well as on failure.
# An empty or short enumeration has two causes -- a tree with fewer tests, or a
# ruler that read the listing wrongly -- and the two look identical in a log:
# both print a smaller number, and the short one is usually *not* a mismatch, so
# the failure-only message never fires.  `%q` is what makes a `\r` or a trailing
# space visible; a count alone would let `hub_dist_regression\r` read as a name.
cr_lines="$(printf '%s\n' "$listing" | grep -c $'\r' || true)"
if [ "${cr_lines:-0}" -gt 0 ]; then
  cr_note="CR on ${cr_lines} line(s), stripped"
else
  cr_note="no CR in listing"
fi
printf 'ctest_enumerate: %s name(s), first %q, last %q, %s\n' \
  "${#names[@]}" "${names[0]}" "${names[-1]}" "$cr_note" >&2

printf '%s\n' "${names[@]}"
