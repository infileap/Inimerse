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
#       1  the test ran and failed (its output is printed).  This is the number
#          a step summary shows most often, so it carries only this meaning.
#       2  the test did not run (skipped, or DISABLED) and is not declared in
#          tools/known-skips.txt
#       3  `-R '^name$'` does not match exactly one test -- the name is wrong,
#          not the test.  ctest exits 0 in this case and prints the same
#          `No tests were found!!!` a disabled test prints, so without this
#          check a renamed test would read as a green step.
#       20 the registry cannot be read at all: `ctest -N` failed, a wrong
#          build directory, no ctest, no CMake cache.  "I could not look" is
#          not "the name is wrong" (3) and not "the test failed" (1) -- a
#          reader told 3 goes and renames a test that exists.
#       21 the command line is wrong (no test name, or more arguments than the
#          mode reads).  The reader's repair is in the caller, not in this
#          repository.  Extra arguments used to be ignored, which made
#          `--check-junit r.xml --allowlist my-list.txt` pass on a list this
#          script never opened; the exemption list comes from
#          INIMERSE_KNOWN_SKIPS or the default path, never from argv, and the
#          first line on stderr now says which file was actually read.
#
#   bash tools/ctest_skip_guard.sh --check-allowlist <build-dir>
#       0   every declared exemption names a registered test and gives a reason
#       10  the exemption list itself is unusable -- an entry with no reason, or
#           the enumeration failed, so the check could not look at all
#       11  an entry names no registered test, and nothing nearby explains why:
#           a rename, a deletion, or a typo.  The list or CMakeLists.txt has to
#           change
#       12  an entry names no registered test but matches one as soon as CR, a
#           trailing `(Disabled)` suffix, whitespace and case are ignored.  The
#           name is right and the reading is wrong, so the repair is in the
#           parser
#       20  the list could not be read, or the registry could not be enumerated,
#           so nothing was checked.  Kept apart from 10: an entry without a
#           reason is a fault in the file, a missing file is a fault in how it
#           was invoked, and the two want different repairs
#
#   The allowlist mode deliberately does not reuse the per-test numbers.  Exit 3
#   already means "the name is wrong, the test is fine" in the per-test mode; a
#   second meaning ("the name is right, the reading is wrong") is the opposite
#   repair behind the same digit, and a reader -- or a log search -- cannot tell
#   the two apart.  One number, one debt: the failure paths also print
#   `result=<token>` so a CI log can be grepped without trusting the arithmetic.
#
#   That rule governs **process exits**, and this file contains two kinds of
#   number that a table must not put in one column.  A process exit is what a
#   caller can see -- it lands in a step summary or in `Process completed with
#   exit code N`.  A function return is internal.  `declared()` returns 1 when
#   the list is missing, and its call site turns that into 20 before the process
#   exits, so that 1 never reaches a log; a table built from
#   `grep -nE '^\s*(exit|return) [0-9]+'` -- 19 lines here, 12 returns and 7
#   exits -- that lists 1 twice (this one, and `result=failed`) re-creates "one
#   digit, two owners" in the very table that claims to have ended it.  Two
#   returns are process codes only because the dispatch converts them (`exit $?`
#   after `check_allowlist`, after `check_disabled`).
#   The reverse accident is a number documented with no code behind it: a dead
#   cell.  A grep cannot find one -- it answers "how often does this digit
#   appear", not "is the promise kept" -- so the check has to be pointed at the
#   code: `sed -n '<line>p'` on the line a table cites must print a statement,
#   not a comment.  This block documented 1 for `ctest -N` failing while the
#   path returned 20; one cause, two numbers, and one of them unreachable.
#   So: the nine process codes are 0 1 2 3 10 11 12 20 21, each one reachable,
#   and a number that is only ever returned stays with the code, next to its
#   call site.
#
#   bash tools/ctest_skip_guard.sh --check-disabled <build-dir>
#       0  every test the registry reports as `(Disabled)` is declared
#       20 `ctest -N` failed, the count line is missing, or the exemption list
#          could not be read -- the check could not look, which is not the same
#          finding as 2.  All
#          three causes carry this one number: this file has exactly one "I
#          could not look", so it has exactly one digit for it.  (An earlier revision
#          documented 1 for the `ctest -N` case as well; no path ever returned
#          it, so it was a dead cell promising a second number for the same
#          debt -- see the note on process exits above.)
#       2  a `DISABLED TRUE` test is not declared.  This is the mode the Windows
#          job uses: it runs the whole suite in one `ctest` invocation, so there
#          is no per-test step to hang a guard on, and a disabled property is
#          the quietest way a test can stop existing -- ctest drops it from the
#          denominator, so `147 registered` prints as `out of 138` and reads as
#          a complete run.
#
#   bash tools/ctest_skip_guard.sh --check-junit <report.xml>
#       Reads the JUnit report a full-suite `ctest --output-junit` run wrote and
#       refuses any test that did not run.  This is the second reader the Windows
#       job needs: one `ctest` invocation cannot hang a guard on a per-test step,
#       and a test that stops running makes the suite *smaller*, not redder --
#       nine disabled tests leave the denominator (`147 registered` prints as
#       `out of 138`) and two skipped ones stay in it counted as passing, so
#       `98% tests passed` can describe a suite that is short eleven tests.
#       0  every case marked `status="notrun"` or `status="disabled"` is declared
#       2  one of them is not declared with a reason (`result=junit-undeclared`)
#       20 the report cannot be read, its `tests="N"` header is missing, or the
#          header disagrees with what this parser found -- an XML dialect change
#          would otherwise shrink the parsed set to none and report "nothing was
#          skipped, all declared" about a run that skipped eleven.  Agreement
#          with the file's own footer is the same discipline as the `Total
#          Tests:` check above, applied to a different footer.
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
      # The list is `name # reason`.  A tab is accepted as the separator too,
      # but the two sides must be split *before* trimming: `<TAB>looks-like-
      # this` under a whole-line fallback would be trimmed into the name
      # `looks-like-this`, which is not a registered test, and the reader would
      # be sent to rename a test that is fine -- the real fault is that the
      # name column is empty.  So: split at `#`, else at the first tab, else the
      # whole line is a name with no reason.  An empty name side is reported as
      # such (third column `noname`) instead of being filled with whatever the
      # next column happens to hold -- a parser that repairs the data silently
      # and then blames the other file is worse than one that refuses.
      hash = index(line, "#")
      if (hash > 0) {
        left = substr(line, 1, hash - 1); reason = substr(line, hash + 1)
      } else {
        tab = index(line, "\t")
        if (tab > 0) { left = substr(line, 1, tab - 1); reason = substr(line, tab + 1) }
        else         { left = line;                      reason = "" }
      }
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", left)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", reason)
      # A leading empty field cannot mark this row: the shell strips
      # leading IFS whitespace when it reads, so `\t<text>` arrives as
      # name=<text> --
      # the same promotion this split exists to prevent, one layer down.  A
      # sentinel in the name column is unambiguous, and every consumer tests
      # the third column before it looks at the first.
      if (left == "") printf "@@noname@@\t%s\tnoname\n", reason
      else            printf "%s\t%s\t\n", left, reason
    }
  ' "$allowlist"
}

# Squash a name down to what a reader would call "the same name": line endings,
# a `(Disabled)` marker, whitespace and case all removed.  Two names that are
# equal here and unequal raw are a reading fault, not a naming fact -- and the
# repair for a reading fault is in this file, not in the exemption list.  Without
# this split the Windows job's first run reported eleven present tests as
# "no such registered test" and sent the reader to look for renames that had not
# happened.
normalize_name() {
  printf '%s' "$1" |
    tr -d '\r' |
    sed 's/[[:space:]]*(Disabled)$//' |
    tr -d '[:space:]' |
    tr '[:upper:]' '[:lower:]'
}

check_allowlist() {
  local build_dir="$1" names_file declared_file failure=0 entries=0 shown_sample=0
  local missing=0 reading_fault=0 unusable=0 nameless=0
  names_file="$(mktemp)"
  declared_file="$(mktemp)"
  trap 'rm -f "$names_file" "$declared_file"' RETURN
  # Read the list into a file and keep its exit code.  `done < <(declared)`
  # cannot do this: the `||` would bind to the `while` loop, and a loop that
  # runs zero times exits 0 -- so a missing list file produced
  # `0 declared entr(ies), all registered and justified` and a green step.
  # That is a false green in the one script whose purpose is to refuse them,
  # and its trigger is ordinary: a wrong INIMERSE_KNOWN_SKIPS, the wrong
  # working directory, a renamed file.
  if ! declared > "$declared_file"; then
    printf 'cannot read the exemption list %s -- nothing was checked; entries=0 result=allowlist-unreadable\n' \
      "$allowlist" >&2
    return 20
  fi
  # The denominator is a reading, not a detail of the sentence around it.
  # `2 declared` and `11 declared` used to carry the same token and the same
  # exit code, so a reader that greps `result=` or looks at rc could not tell
  # "checked two entries" from "checked eleven" -- and a check whose population
  # is invisible cannot be audited, only believed.  It is counted once, here,
  # and every line below carries it as `entries=<n>`.
  entries="$(grep -c . "$declared_file" || true)"
  if ! bash "${script_dir}/ctest_enumerate.sh" "$build_dir" > "$names_file"; then
    printf 'cannot check the exemption list: enumeration failed -- nothing was checked; entries=%s result=registry-unreadable\n' \
      "$entries" >&2
    return 20
  fi
  local -a registry=()
  mapfile -t registry < "$names_file"
  while IFS=$'\t' read -r name reason flag; do
    if [ "${flag:-}" = "noname" ]; then
      printf 'exemption list entry with no name: %q -- the name column is empty and\n' "$reason" >&2
      printf '  the text after the separator is not a test name.  Write `name # reason`.\n' >&2
      unusable=$((unusable + 1))
      nameless=$((nameless + 1))
      failure=1
      continue
    fi
    [ -n "$name" ] || continue
    if ! grep -qxF "$name" "$names_file"; then
      # Is this the same name read wrongly, or a name that is really gone?
      local normalized near_miss=''
      normalized="$(normalize_name "$name")"
      local candidate
      for candidate in "${registry[@]}"; do
        if [ "$(normalize_name "$candidate")" = "$normalized" ]; then
          near_miss="$candidate"
          break
        fi
      done
      if [ -n "$near_miss" ]; then
        reading_fault=$((reading_fault + 1))
        failure=1
        printf 'exemption for %s: the registry does contain %q, but not %q\n' \
          "$name" "$near_miss" "$name" >&2
        printf '  these differ only by CR, a (Disabled) marker, whitespace or case:\n' >&2
        printf '  the name is right and the reading is wrong.  Fix the parser that\n' >&2
        printf '  produced %q -- do not edit the exemption list to match the bug.\n' "$name" >&2
        continue
      fi
      missing=$((missing + 1))
      failure=1
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
    fi
    if [ -z "$reason" ]; then
      printf 'exemption for %s: no reason given\n' "$name" >&2
      unusable=$((unusable + 1))
      failure=1
    fi
  done < "$declared_file"
  # The exit code can only carry one class, and the order is arbitrary in the
  # sense that it had to be chosen: a misread (12) outranks a gap (11) because a
  # listing read wrongly can also make a real gap look like a mismatch, and both
  # outrank an unusable entry (10) because those two name something to repair in
  # the repository.  What the code cannot say is that a *second* class was also
  # seen in the same list -- so say it on its own line.  Without it a reader who
  # gets 11 never learns that an entry beside it also lacks a reason.
  report_others() {
    local skipped_class="$1"
    if [ "$skipped_class" != "misread" ] && [ "$reading_fault" -ne 0 ]; then
      printf '  also: %s entr(ies) are a reading fault (misread) -- entries=%s misread=%s\n' \
        "$reading_fault" "$entries" "$reading_fault" >&2
    fi
    if [ "$skipped_class" != "missing" ] && [ "$missing" -ne 0 ]; then
      printf '  also: %s entr(ies) name no registered test (missing) -- entries=%s missing=%s\n' \
        "$missing" "$entries" "$missing" >&2
    fi
    if [ "$skipped_class" != "unusable" ] && [ "$unusable" -ne 0 ]; then
      printf '  also: %s entr(ies) cannot be used (unusable, %s with no name) -- entries=%s unusable=%s\n' \
        "$unusable" "$nameless" "$entries" "$unusable" >&2
    fi
  }
  if [ "$failure" -ne 0 ]; then
    printf 'refusing the exemption list: %s entr(ies) checked in %s; entries=%s\n' "$entries" "$allowlist" "$entries" >&2
    if [ "$reading_fault" -ne 0 ]; then
      # Exit 12 wins when both faults are present: the reading fault is the one
      # that can hide the other, because a listing read wrongly can also make a
      # real gap look like a name mismatch.  (This comment said "Exit 3" for a
      # while after the allowlist numbers moved to 10/11/12; a comment is not a
      # return statement, so nothing that counts `return` lines could catch it.)
      printf 'exemption list: %s entr(ies) name a test this registry has but this\n' \
        "$reading_fault" >&2
      printf '  program did not see -- a reading fault, not a missing test; entries=%s misread=%s result=allowlist-misread\n' \
        "$entries" "$reading_fault" >&2
      report_others misread
      return 12
    fi
    if [ "$missing" -ne 0 ]; then
      printf 'exemption list: %s entr(ies) name no registered test; entries=%s missing=%s result=allowlist-missing\n' \
        "$missing" "$entries" "$missing" >&2
      report_others missing
      return 11
    fi
    # Nothing is missing and nothing was misread, so the list itself is the
    # problem: an entry without a reason is a licence nobody audits, and 1 is
    # the code that says "this file cannot be used", not "a test has gone".
    printf 'exemption list: %s entr(ies) cannot be used: %s give no reason in %s, %s have no\n' \
      "$unusable" "$((unusable - nameless))" "$allowlist" "$nameless" >&2
    printf '  name at all; entries=%s unusable=%s nameless=%s result=allowlist-unusable\n' \
      "$entries" "$unusable" "$nameless" >&2
    return 10
  fi
  if [ "$entries" -eq 0 ]; then
    # "Nothing is declared" and "everything declared is justified" are two
    # facts, and only the second one is good news.  A list that reads as empty
    # because the path is wrong would otherwise print the sentence a reader
    # wants to see -- the same shape of lie the disabled mode prints when it
    # sees no `(Disabled)` in a listing it read wrongly.
    printf 'exemption list: 0 entr(ies) read from %s -- this list declares nothing;\n' "$allowlist"
    printf '  entries=0 result=allowlist-empty.  If you expected exemptions here, the path is wrong.\n'
    return 0
  fi
  printf 'exemption list: %s declared entr(ies), all registered and justified; entries=%s result=allowlist-declared\n' \
    "$entries" "$entries"
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
    printf 'cannot check the disabled set: ctest -N failed for %s -- nothing was checked; result=registry-unreadable\n%s\n' \
      "$build_dir" "$listing" >&2
    return 20
  fi
  # Same reason as the allowlist mode: `done < <(declared)` swallows the exit
  # code of a list that could not be read, and every disabled test would then
  # be reported as undeclared -- a wrong answer with the right number (2).
  local declared_file drc=0
  declared_file="$(mktemp)"
  trap 'rm -f "$declared_file"' RETURN
  if ! declared > "$declared_file"; then
    printf 'cannot read the exemption list %s -- nothing was checked; entries=0 result=allowlist-unreadable\n' \
      "$allowlist" >&2
    return 20
  fi
  if grep -q $'\tnoname$' "$declared_file"; then
    printf 'the exemption list %s has entr(ies) with no name before the separator -- nothing was checked; entries=%s result=allowlist-unusable\n' \
      "$allowlist" "$(grep -c . "$declared_file" || true)" >&2
    return 10
  fi
  local -a declared_names=() declared_reasons=()
  local dname dreason
  while IFS=$'\t' read -r dname dreason; do
    declared_names+=("$dname")
    declared_reasons+=("$dreason")
  done < "$declared_file"
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
  local name found i
  local declared_count=0
  local undeclared_count=0
  for name in "${disabled[@]}"; do
    [ -n "$name" ] || continue
    found=''
    for i in "${!declared_names[@]}"; do
      if [ "${declared_names[$i]}" = "$name" ]; then
        found="${declared_reasons[$i]}"
        break
      fi
    done
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
  # The witness goes out on the success path too, and it has to: "nothing is
  # disabled" and "this program saw nothing" print the same working line, and the
  # second one is the failure this mode exists to catch.  A count with no shape
  # cannot tell them apart, so print what the ruler itself saw -- how many tests
  # the registry claims, and whether the listing arrived with CRs.
  local registered cr_lines cr_note
  registered="$(printf '%s\n' "$listing" | sed -n 's/^[[:space:]]*Total Tests:[[:space:]]*//p' | tr -d '\r' | head -1)"
  cr_lines="$(printf '%s\n' "$listing" | grep -c $'\r' || true)"
  if [ "${cr_lines:-0}" -gt 0 ]; then
    cr_note="CR on ${cr_lines} line(s), stripped"
  else
    cr_note="no CR in listing"
  fi
  # The witness is only a witness if the ruler could read its denominator.
  # `ctest -N` prints `Total Tests: N` on the last line; if a future ctest stops
  # printing it (or renames it), every count below still works and the check
  # stays green while the line that says "what I saw" degrades to `registry=?`.
  # "I could not read the denominator" is the same debt as "I could not read the
  # registry", so it takes the same number.
  if ! printf '%s' "${registered:-}" | grep -qE '^[0-9]+$'; then
    printf 'cannot read the registered count from %s: no `Total Tests:` line -- nothing was checked; result=registry-count-unreadable\n%s\n' \
      "$build_dir" "$listing" >&2
    return 20
  fi
  if [ "$undeclared_count" -ne 0 ]; then
    printf 'guard: disabled=%s declared=%s undeclared=%s result=disabled-undeclared\n' \
      "${#disabled[@]}" "$declared_count" "$undeclared_count" >&2
    return 2
  fi
  printf 'guard: registry=%s registered, %s, disabled=%s declared=%s result=disabled-declared\n' \
    "$registered" "$cr_note" "${#disabled[@]}" "$declared_count"
  return 0
}

check_junit() {
  # Why this mode exists: on Windows the whole suite runs in one `ctest`
  # invocation, so there is no per-test step to hang a guard on.  `ctest` prints
  # `98% tests passed, 3 tests failed out of 138` for the Windows tree while the
  # progress lines say 147 -- nine disabled tests leave the denominator and two
  # skipped tests stay in it counted as passing.  A test that stops running is
  # therefore invisible in both the exit code and the percentage: the run gets
  # *smaller* and stays green.  The JUnit report is the only structured record
  # of which tests did not run, and it survives a failing ctest, which is why
  # this mode reads the report instead of the log.
  local report="$1" declared_file cases_file
  local entries=0 tests_count='' skipped_count='' disabled_count=''
  local parsed_notrun=0 parsed_disabled=0 declared_count=0 undeclared=0
  declared_file="$(mktemp)"
  cases_file="$(mktemp)"
  trap 'rm -f "$declared_file" "$cases_file"' RETURN

  if ! declared > "$declared_file"; then
    printf 'cannot read the exemption list %s -- nothing was checked; entries=0 result=allowlist-unreadable\n' \
      "$allowlist" >&2
    return 20
  fi
  entries="$(grep -c . "$declared_file" || true)"
  if grep -q $'\tnoname$' "$declared_file"; then
    printf 'the exemption list %s has entr(ies) with no name before the separator -- nothing was checked; entries=%s result=allowlist-unusable\n' \
      "$allowlist" "$(grep -c . "$declared_file" || true)" >&2
    return 10
  fi
  if [ ! -r "$report" ]; then
    printf 'cannot read the test report %s -- nothing was checked; entries=%s result=junit-unreadable\n' \
      "$report" "$entries" >&2
    return 20
  fi

  # The report states its own population in the `<testsuite ...>` header:
  # `tests="3" failures="0" disabled="1" skipped="1"`.  Read it, and then
  # require the parser below to agree with it -- an XML dialect change would
  # otherwise shrink the set of parsed cases to zero and this mode would report
  # "nothing skipped, all declared" about a run that skipped eleven.
  tests_count="$(grep -oE '(^|[[:space:]])tests="[0-9]+"' "$report" | head -1 | grep -oE '[0-9]+' || true)"
  skipped_count="$(grep -oE '(^|[[:space:]])skipped="[0-9]+"' "$report" | head -1 | grep -oE '[0-9]+' || true)"
  disabled_count="$(grep -oE '(^|[[:space:]])disabled="[0-9]+"' "$report" | head -1 | grep -oE '[0-9]+' || true)"
  skipped_count="${skipped_count:-0}"
  disabled_count="${disabled_count:-0}"
  if ! printf '%s' "${tests_count:-}" | grep -qE '^[0-9]+$'; then
    printf 'cannot read the test count from %s: no `tests="N"` in the suite header -- nothing was checked; entries=%s result=junit-unreadable\n' \
      "$report" "$entries" >&2
    return 20
  fi

  # `ctest` writes `status="notrun"` with a `<skipped/>` child for a test that
  # returned SKIP_RETURN_CODE, and `status="disabled"` for a DISABLED property.
  # Both mean "did not run"; they are kept apart only so the reading line can
  # show why the suite is short.
  grep -oE '<testcase name="[^"]*"[^>]*status="(notrun|disabled)"' "$report" 2>/dev/null \
    | sed -E 's/^<testcase name="([^"]*)".*status="(notrun|disabled)".*$/\1\t\2/' > "$cases_file" || true
  parsed_notrun="$(cut -f2 "$cases_file" | grep -cx 'notrun' || true)"
  parsed_disabled="$(cut -f2 "$cases_file" | grep -cx 'disabled' || true)"
  parsed_notrun="${parsed_notrun:-0}"
  parsed_disabled="${parsed_disabled:-0}"

  if [ "$parsed_notrun" -ne "$skipped_count" ] || [ "$parsed_disabled" -ne "$disabled_count" ]; then
    printf 'the report says skipped=%s disabled=%s but this parser found notrun=%s disabled=%s -- the format changed, so nothing was checked; entries=%s result=junit-unreadable\n' \
      "$skipped_count" "$disabled_count" "$parsed_notrun" "$parsed_disabled" "$entries" >&2
    return 20
  fi

  local name status reason
  while IFS=$'\t' read -r name status; do
    [ -n "$name" ] || continue
    if grep -qxF "$name" <(cut -f1 "$declared_file"); then
      declared_count=$((declared_count + 1))
      reason="$(grep -F "${name}"$'\t' "$declared_file" | head -1 | cut -f2)"
      printf 'guard: %s test %s is declared: %s\n' \
        "$([ "$status" = "notrun" ] && printf skipped || printf disabled)" "$name" "$reason"
    else
      undeclared=$((undeclared + 1))
      printf 'guard: %s test %s is not declared in %s\n' \
        "$([ "$status" = "notrun" ] && printf skipped || printf disabled)" "$name" "$allowlist" >&2
    fi
  done < "$cases_file"

  if [ "$undeclared" -ne 0 ]; then
    printf 'guard: tests=%s notrun=%s disabled=%s declared=%s undeclared=%s result=junit-undeclared\n' \
      "$tests_count" "$parsed_notrun" "$parsed_disabled" "$declared_count" "$undeclared" >&2
    return 2
  fi
  printf 'guard: tests=%s notrun=%s disabled=%s declared=%s result=junit-declared\n' \
    "$tests_count" "$parsed_notrun" "$parsed_disabled" "$declared_count"
  return 0
}

# A parameter nobody reads is a parameter nobody was given.  Every mode takes
# the exemption list from INIMERSE_KNOWN_SKIPS or the default path and never
# from argv, so `--check-junit r.xml --allowlist my-list.txt` used to pass on a
# list the caller believed in and the script never opened: same rc 0, same
# reading line, different file.  Two cheap repairs, both here: refuse extra
# arguments instead of ignoring them (21), and say which file was actually read
# so "I passed a list" cannot stay indistinguishable from "it read my list".
case "${1:-}" in
  --check-junit|--check-disabled|--check-allowlist)
    if [ "$#" -gt 2 ]; then
      printf '%s reads one argument; %s were given and nothing would read the rest -- nothing was checked; result=usage\n' \
        "$1" "$#" >&2
      exit 21
    fi
    ;;
  *)
    if [ "$#" -gt 2 ]; then
      printf 'usage: bash tools/ctest_skip_guard.sh <build-dir> <test-name> -- %s argument(s) given; nothing was checked; result=usage\n' \
        "$#" >&2
      exit 21
    fi
    ;;
esac

printf 'guard: exemption list is %s\n' "$allowlist" >&2
if [ "${1:-}" = "--check-junit" ]; then
  check_junit "${2:?usage: --check-junit <report.xml>}"
  exit $?
fi
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
  # 21, not 1: this is the caller's mistake and every other 1 in this file is
  # about code that ran.  Before the split, a CI step showing 1 could mean "the
  # test failed" -- the most common reading -- or "the job passed no test name".
  printf 'guard: result=usage\n' >&2
  exit 21
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
selection="$(ctest --test-dir "$build_dir" -N -R "^${test_name}$" 2>&1)"
selection_rc=$?
if [ "$selection_rc" -ne 0 ]; then
  # A wrong build directory, a missing CMake cache or a missing ctest all land
  # here.  Counting what came back would read 0 matches and exit 3 -- "the name
  # is wrong, the test is fine" -- which sends the reader to rename a test that
  # exists.  The registry failing to open is a fact about the invocation.
  printf 'guard: the registry could not be read for %s (ctest -N exited %s) -- nothing was checked; result=registry-unreadable\n' \
    "$build_dir" "$selection_rc" >&2
  printf '%s\n' "$selection" >&2
  exit 20
fi
selected="$(printf '%s\n' "$selection" | grep -cE '^[[:space:]]*Test +#[0-9]+')"
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
