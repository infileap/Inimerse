#!/usr/bin/env bash
# gate.sh — the acceptance gate every Inimerse/Infiverse stream must pass before
# its branch may be merged into main.  See docs/BOARD.md §3.
#
#   tools/gate.sh              full gate: configure + build + all suites
#   tools/gate.sh --fast       skip configure/build, reuse the existing build/
#   tools/gate.sh --only links run a single stage
#                              (build|ctest|economy|node|plugin|links|doc-paths)
#   tools/gate.sh --jobs 4     parallel job count for the build
#
# Exit code 0 only when every stage passed.  Each stage prints PASS/FAIL/SKIP,
# and a summary table is printed last so a failing run is readable at a glance.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT" || exit 2

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
JOBS="$(nproc 2>/dev/null || echo 4)"
FAST=0
ONLY=""

while [ $# -gt 0 ]; do
  case "$1" in
    --fast) FAST=1 ;;
    --only) ONLY="${2:-}"; shift ;;
    --jobs) JOBS="${2:-4}"; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "gate: unknown argument '$1'" >&2; exit 2 ;;
  esac
  shift
done

STAGE_NAMES=()
STAGE_RESULTS=()
STAGE_NOTES=()
FAILED=0

# The number of registered CTest cases this gate expects.  It is *asserted*
# below, not merely printed in the stage label: ctest exits 0 as long as no
# test FAILS, so a suite that silently stopped being registered would otherwise
# still show a green gate.  Bump this (and docs/BOARD.md §3) when you add one.
#
# The skipped count is asserted too.  A test that exits 77 (SKIP_RETURN_CODE,
# used by the xlang bridge suites when no toolchain prefix is present) is
# reported by ctest as "***Skipped" while the summary still reads "100% tests
# passed, 0 tests failed out of N" -- so without this check the gate could go
# green having verified nothing about the bridge.
EXP_CTEST="${EXP_CTEST:-101}"

run_stage() {
  local name="$1" wanted="$2"; shift 2
  if [ -n "$ONLY" ] && [ "$ONLY" != "$wanted" ]; then
    STAGE_NAMES+=("$name"); STAGE_RESULTS+=("SKIP"); STAGE_NOTES+=("--only $ONLY")
    return 0
  fi
  echo
  echo "──────────────────────────────────────────────────────────────"
  echo "▶ $name"
  echo "──────────────────────────────────────────────────────────────"
  local log; log="$(mktemp)"
  "$@" 2>&1 | tee "$log"
  local rc="${PIPESTATUS[0]}"
  if [ "$rc" -eq 0 ]; then
    STAGE_NAMES+=("$name"); STAGE_RESULTS+=("PASS"); STAGE_NOTES+=("")
    echo "✔ $name"
  else
    STAGE_NAMES+=("$name"); STAGE_RESULTS+=("FAIL"); STAGE_NOTES+=("exit $rc")
    echo "✘ $name (exit $rc)"
    FAILED=1
  fi
  rm -f "$log"
  return 0
}

stage_build() {
  if [ "$FAST" -eq 0 ]; then
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release || return 1
  fi
  cmake --build "$BUILD_DIR" -j"$JOBS" || return 1

  # Tally warnings/errors.  `grep -c` exits 1 when it counts zero, and
  # `set -o pipefail` (line 12) turns that into a stage failure for a perfectly
  # clean build, so capture the log once and count with awk instead.
  local log; log="$(mktemp)"
  cmake --build "$BUILD_DIR" -j"$JOBS" >"$log" 2>&1
  echo "-- warning/error tally --"
  awk '/warning:/ { w++ } /error:/ { e++ } END {
        printf "warnings: %d\nerrors: %d\n", w + 0, e + 0 }' "$log"
  rm -f "$log"
  return 0
}

stage_ctest() {
  local out rc skipped
  out="$(ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$JOBS" 2>&1)"
  rc=$?
  printf '%s\n' "$out"
  # Skipped tests are invisible in ctest's pass/fail summary, so count them.
  skipped="$(printf '%s\n' "$out" | grep -cF '***Skipped' || true)"
  echo "gate: ctest reported ${skipped} skipped test(s)"
  [ "$rc" -eq 0 ] || return "$rc"
  if [ "$skipped" -ne 0 ]; then
    echo "gate: ${skipped} test(s) exited 77 (skipped); a skip is not a pass." >&2
    printf '%s\n' "$out" | sed -n '/The following tests did not run:/,$p' >&2
    echo "gate: install the bridge toolchain (examples/BUILDING_BRIDGES.md) and re-run." >&2
    return 1
  fi
  # A green ctest only means "nothing failed".  Assert the count too, so that a
  # dropped add_test( ) cannot pass silently.
  if ! printf '%s\n' "$out" | grep -q "0 tests failed out of $EXP_CTEST"; then
    echo "gate: ctest did not report '0 tests failed out of $EXP_CTEST'." >&2
    echo "gate: a test may have stopped being registered, or the count moved." >&2
    echo "gate: bump EXP_CTEST in tools/gate.sh and docs/BOARD.md 3 if that was intended." >&2
    return 1
  fi
  return 0
}

stage_economy() {
  # §43.5 acceptance gate: 39 checks over the migration import path.
  python3 "$REPO_ROOT/tools/economy_migration.test.py"
}

stage_node() {
  # The UPP / CRP / .vverse JS suites.  They passed for months while nothing
  # invoked them (ctest registers only Python suites), so a regression in the
  # reference protocol code could not have failed any gate.  Now it can.
  if ! command -v node >/dev/null 2>&1; then
    echo "node not found -- cannot run the JS protocol suites"; return 1
  fi
  node "$REPO_ROOT/tools/node_suites/run_all.js"
}

stage_plugin() {
  # The DSH bridge plugin: offline checks plus a live round trip through the
  # real inim-server / inim-client binaries.
  if ! command -v node >/dev/null 2>&1; then
    echo "node not found -- cannot run the plugin suite"; return 1
  fi
  node "$REPO_ROOT/tools/dsh-inimerse/verify.mjs" --live
}

stage_links() {
  python3 "$REPO_ROOT/tools/check_links.py"
}

stage_doc_paths() {
  # Complement to check_links.py, not a replacement.  check_links.py must strip
  # inline code spans (otherwise a span like `object["m"](...)` is parsed as a
  # link), and that makes it blind to the references this repository actually
  # writes in backticks.  22 `docs/<name>.md` references in
  # docs/REQUIREMENTS_ANALYSIS.md pointed at files moved into docs/archive/
  # while check_links.py kept reporting 0 broken.  See docs/BOARD.md §3.
  python3 "$REPO_ROOT/tools/check_doc_paths.py"
}

run_stage "build (Release, $( [ "$FAST" -eq 1 ] && echo incremental || echo configure+incremental ), -j$JOBS)" build stage_build
run_stage "ctest (expect ${EXP_CTEST}/${EXP_CTEST}, 0 skipped)" ctest stage_ctest
run_stage "economy migration (§43.5, expect 39/39)" economy stage_economy
run_stage "node protocol suites (expect 11/11)" node stage_node
run_stage "dsh-inimerse plugin (offline + live)" plugin stage_plugin
run_stage "docs relative links" links stage_links
run_stage "docs backtick paths (expect 0 broken)" doc-paths stage_doc_paths

echo
echo "══════════════════════════════════════════════════════════════"
printf '%-58s %s\n' "STAGE" "RESULT"
echo "──────────────────────────────────────────────────────────────"
for i in "${!STAGE_NAMES[@]}"; do
  printf '%-58s %s %s\n' "${STAGE_NAMES[$i]}" "${STAGE_RESULTS[$i]}" "${STAGE_NOTES[$i]}"
done
echo "══════════════════════════════════════════════════════════════"

if [ "$FAILED" -ne 0 ]; then
  echo "gate: FAILED — do not merge."
  exit 1
fi
echo "gate: OK — every stage passed."
exit 0
