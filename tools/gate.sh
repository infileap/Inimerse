#!/usr/bin/env bash
# gate.sh — the acceptance gate every Inimerse/Infiverse stream must pass before
# its branch may be merged into main.  See docs/BOARD.md §3.
#
#   tools/gate.sh              full gate: configure + build + all suites
#   tools/gate.sh --fast       skip configure/build, reuse the existing build/
#   tools/gate.sh --only links run a single stage
#                              (build|ctest|fuzz|economy|node|plugin|oauth-loop|
#                               ignored-credentials|links|doc-paths)
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
EXP_CTEST="${EXP_CTEST:-134}"

# The JS suite count, asserted for the same reason as EXP_CTEST: a suite dropped
# from tools/node_suites/run_all.js SUITES must not leave a green stage behind.
EXP_NODE="${EXP_NODE:-12}"

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

  # Tally warnings/errors from a build that actually compiles.
  #
  # This used to run `cmake --build` once and then run it AGAIN into the tally
  # log.  On a warm tree the second run is a no-op, so the tally could only ever
  # print `warnings: 0` no matter what the real build said -- it read 0 while the
  # tree carried 34 warnings.  Tallying the first build fixes that only when the
  # tree happened to be dirty, so the tally build is now `--clean-first`: an
  # incremental rebuild that recompiles nothing is not a measurement.
  # `grep -c` also exits 1 when it counts zero and `set -o pipefail` (line 12)
  # would turn that into a stage failure for a perfectly clean build, hence the
  # log plus awk.  See docs/AUDIT.md 1.21.
  local log; log="$(mktemp)"
  cmake --build "$BUILD_DIR" --clean-first -j"$JOBS" >"$log" 2>&1 || { cat "$log"; rm -f "$log"; return 1; }
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
  # The suites themselves have to be safe to run concurrently: a server bound
  # to a reserved port number loses a release-to-bind race under `-j` and dies
  # before its first assertion (tools/node_discovery.test.py did exactly that,
  # 4/120 runs at 32-way load).  Checked here rather than in its own stage
  # because it is a property of the suites this stage just ran.
  python3 "$REPO_ROOT/tools/check_test_ports.py" || return 1
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

# The differential fuzzer (tools/im_diff_fuzz.py) is an expect-zero stage now.
#
# It deliberately generates constants at the int32/double boundary, which is exactly
# where the interpreter and the AOT backend used to disagree: Value carried a 32-bit
# `ival` plus a double while the codegen's NV.i was 64-bit, so a number above int32
# became a double in the VM (low bits gone, integer zero guards skipped) and stayed an
# exact int64 in AOT.  v3.1 widened Value's integer slot to int64 behind an anonymous
# union that keeps sizeof(Value) at 32 bytes, so the two agree: 0 findings across
# seeds 1-6 at 120 programs each and seed 1 at 400.  See docs/AUDIT.md §1.14.
#
# The seed and the program count stay fixed, so the finding set is deterministic and a
# regression is reproducible.  The counts are still asserted EXACTLY rather than as a
# ceiling, because the direction of the surprise matters:
#
#   * MORE findings is a new interpreter/AOT divergence -- do not paper over it;
#   * FEWER findings means one was fixed -- promote that case into
#     tools/aot_native.test.py, then lower the pin here and in docs/BOARD.md 3.
#
# That is the same convention as the pinned DIVERGENCE list in
# tools/aot_native.test.py, so the gap can neither widen nor close without someone
# saying so.  `not translated` must be 0 either way -- the tool reports a program the
# AOT backend cannot translate as a generator bug rather than as a divergence, and a
# generator bug is never a finding.  See docs/AUDIT.md §1.2 (整数提升) and §1.13.
EXP_FUZZ_COUNT="${EXP_FUZZ_COUNT:-120}"
EXP_FUZZ_SEED="${EXP_FUZZ_SEED:-1}"
EXP_FUZZ_DIVERGE="${EXP_FUZZ_DIVERGE:-0}"
EXP_FUZZ_THREW="${EXP_FUZZ_THREW:-0}"

stage_fuzz() {
  local out rc div threw untr
  out="$(python3 "$REPO_ROOT/tools/im_diff_fuzz.py" \
           --count "$EXP_FUZZ_COUNT" --seed "$EXP_FUZZ_SEED" 2>&1)"
  rc=$?
  printf '%s\n' "$out"
  if [ "$rc" -eq 2 ]; then
    echo "gate: the fuzzer could not find its engine or its translator." >&2
    return 1
  fi
  # The tool exits 1 whenever it finds anything -- the right default for interactive
  # use, but the pin below is what decides here, so the exit code is not the verdict.
  div="$(printf '%s\n' "$out" | sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p')"
  threw="$(printf '%s\n' "$out" | sed -n 's/^  THREW *\([0-9]*\)$/\1/p')"
  untr="$(printf '%s\n' "$out" | sed -n 's/^  not translated *\([0-9]*\)$/\1/p')"
  if [ -z "$div" ] || [ -z "$threw" ] || [ -z "$untr" ]; then
    echo "gate: could not read the fuzzer's finding counts from its output." >&2
    return 1
  fi
  if [ "$untr" -ne 0 ]; then
    echo "gate: the generator produced $untr program(s) the AOT backend cannot translate." >&2
    echo "gate: that is a generator bug, not a divergence -- narrow the generator." >&2
    return 1
  fi
  if [ "$div" -ne "$EXP_FUZZ_DIVERGE" ] || [ "$threw" -ne "$EXP_FUZZ_THREW" ]; then
    echo "gate: differential fuzz found $div DIVERGE / $threw THREW, pinned $EXP_FUZZ_DIVERGE / $EXP_FUZZ_THREW" >&2
    echo "gate: (seed $EXP_FUZZ_SEED, $EXP_FUZZ_COUNT programs)." >&2
    echo "gate: MORE findings is a new interpreter/AOT divergence -- do not paper over it." >&2
    echo "gate: FEWER findings means one was fixed -- promote that case into" >&2
    echo "gate: tools/aot_native.test.py, then bump EXP_FUZZ_DIVERGE/EXP_FUZZ_THREW" >&2
    echo "gate: here and the counts in docs/BOARD.md 3." >&2
    return 1
  fi
  echo "gate: fuzz findings match the pin ($EXP_FUZZ_DIVERGE DIVERGE, $EXP_FUZZ_THREW THREW, 0 untranslated)."
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
  # Assert the registered suite COUNT (passed + skipped), not the pass count --
  # the same reason stage_ctest asserts a count.  Two suites self-skip on a
  # missing optional toolchain (wasm_host without a built wasm module,
  # infiverse_panels without jsdom), so "N/N passed" is configuration-dependent
  # and would be a flaky assertion.  What must be stable is the denominator: a
  # suite dropped from run_all.js SUITES still fails this stage.
  #
  # Skips are counted and reported separately, and this stage FAILS on any skip
  # -- matching examples/BUILDING_BRIDGES.md, whose rule is "a gate can never go
  # green while claiming evidence it did not collect".  Before this, the runner
  # counted a skipped suite as a pass, so a jsdom-less machine would have shown a
  # green node stage having asserted nothing at all about the UI.
  local out rc ran skipped
  out="$(node "$REPO_ROOT/tools/node_suites/run_all.js" 2>&1)"
  rc=$?
  printf '%s\n' "$out"
  [ "$rc" -eq 0 ] || return "$rc"
  ran="$(printf '%s\n' "$out" | sed -n 's/^node protocol suites: [0-9]*\/\([0-9]*\) passed$/\1/p')"
  if [ -z "$ran" ]; then
    echo "gate: could not read the suite count from the node runner output." >&2
    return 1
  fi
  if [ "$ran" -ne "$EXP_NODE" ]; then
    echo "gate: node runner covered $ran suite(s), expected $EXP_NODE." >&2
    echo "gate: a suite may have been dropped from tools/node_suites/run_all.js SUITES." >&2
    echo "gate: bump EXP_NODE in tools/gate.sh and docs/BOARD.md 3 if that was intended." >&2
    return 1
  fi
  skipped="$(printf '%s\n' "$out" | sed -n 's/^skipped (NOT passes): //p')"
  if [ -n "$skipped" ]; then
    echo "gate: ${skipped}" >&2
    echo "gate: a skip is not a pass; this stage fails so the gate cannot go green" >&2
    echo "gate: while claiming UI or wasm evidence it did not collect." >&2
    echo "gate: for the panels suite install jsdom outside the repo, then re-run:" >&2
    echo "gate:   npm install --prefix \$HOME/.local/inimerse-jsdom jsdom" >&2
    echo "gate:   NODE_PATH=\$HOME/.local/inimerse-jsdom/node_modules bash tools/gate.sh" >&2
    echo "gate: for wasm_host build the wasm module first." >&2
    return 1
  fi
  return 0
}

stage_plugin() {
  # The DSH bridge plugin: offline checks plus a live round trip through the
  # real inim-server / inim-client binaries.
  if ! command -v node >/dev/null 2>&1; then
    echo "node not found -- cannot run the plugin suite"; return 1
  fi
  node "$REPO_ROOT/tools/dsh-inimerse/verify.mjs" --live
}

stage_oauth_loop() {
  # The Tauri-free OAuth loop-back crate (BOARD row 100).  It exists precisely
  # so this layer can be exercised on a machine where the Tauri shell cannot be
  # built, so it is worth nothing unless something runs it -- a crate nobody
  # invokes is the same failure mode as the JS protocol suites that passed for
  # months unregistered (see stage_node above).
  #
  # Rust is NOT required to build Inimerse.  A missing cargo is therefore a
  # SKIP with a loud note, not a failure: turning it into a failure would make
  # the gate unrunnable on any machine without a Rust toolchain, and the engine
  # must stay buildable with nothing but a C compiler.
  local crate="$REPO_ROOT/Infiverse_standard/oauth_loop"
  if [ ! -d "$crate" ]; then
    echo "gate: $crate is missing -- it is the row 100 artifact." >&2
    return 1
  fi
  if ! command -v cargo >/dev/null 2>&1; then
    echo "cargo not found -- skipping the oauth_loop crate (set PATH=\$HOME/.cargo/bin:\$PATH)."
    echo "NOTE: this skip is not a pass; the loop-back tests did not run."
    return 0
  fi
  # Keep cargo's target dir and network use inside the repo scratch area, and
  # never let a stale lock from a parallel run block the stage.
  #
  # Assert the count, exactly as stage_ctest does for ctest: "cargo test exits
  # 0" does not distinguish 36 passing tests from 1 passing test and 35 deleted
  # ones.  The audit for this row deleted the slot write in start_callback() and
  # the suite stayed green, so a count alone is weak -- but it at least catches
  # the crate being emptied out.
  #
  # The count is 75: 36 from the PKCE work (BOARD row 101), 10 from the
  # redirect_uri binding, 3 pinning the authorize URL encoding -- the defect that
  # made a real GitHub round trip fail with a misleading
  # incorrect_client_credentials -- 6 from the client_secret work, which is the
  # same defect one layer down: GitHub's exchange documents client_secret as
  # Required with no "unless PKCE" clause, so a correct verifier alone is refused,
  # and 14 pinning the device flow, the only GitHub path that needs no secret,
  # plus 3 refusing a saved secret that is too short to be one, and 3 for
  # the environment lookup the shell falls back to -- the route recommended for
  # keeping the secret off disk -- which had a doc comment claiming a test covered
  # it and no test at all; the first version of that whitespace test was vacuous
  # (a wrong variable name also returns None) and was rewritten to prove the name
  # it reads.  The stale
  # 7-character value that made every exchange fail with an error blaming the
  # client_id while the panel's field sat empty.
  # Bump this whenever the crate's suite grows on purpose.
  local out rc
  out="$( cd "$crate" && cargo test --offline --locked 2>&1 )"
  rc=$?
  printf '%s\n' "$out"
  [ "$rc" -eq 0 ] || return "$rc"
  if ! printf '%s\n' "$out" | grep -qE 'test result: ok\. 75 passed; 0 failed'; then
    echo "gate: oauth_loop did not report 'test result: ok. 75 passed; 0 failed'." >&2
    echo "gate: update the expected count here and in docs/BOARD.md if that was intended." >&2
    return 1
  fi
  # A synchronous #[tauri::command] runs on the thread that drives the webview,
  # so a command that shells out to curl with --max-time 30 freezes the window
  # for the whole timeout when the host is unreachable.  No DOM assertion can
  # see that, so assert the declaration instead.
  if ! python3 "$REPO_ROOT/tools/check_async_commands.py"; then
    echo "gate: a network-touching Tauri command is not async — it will block the UI thread." >&2
    return 1
  fi
  return 0
}

stage_ignored_credentials() {
  # The userdata/ ignore rules are default-deny, so a runtime artifact -- a
  # client secret, a live access token -- is protected before anyone knows it
  # exists.  This asserts that default with a filename no run has ever written,
  # rather than re-checking the three names someone already remembered; the
  # previous per-file patches passed the narrower version of this test while the
  # defect stayed live, and two credentials nearly shipped.  See docs/STATUS.md
  # §10.37 and docs/BOARD.md row 106.
  python3 "$REPO_ROOT/tools/check_ignored_credentials.py"
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

stage_text_integrity() {
  # A NUL byte in a text file is inert to the compiler and loud to grep: GNU
  # grep calls the file binary, lists only the matches it found *before* that
  # byte, puts "binary file matches" on stderr, and exits 0 -- a truncated
  # answer that looks complete.  In src/mod/gui_mod.c the swallowed lines were
  # the two that prove gui_fullscreen is registered twice, and the duplicate is
  # a live defect (builtin_fullscreen is unreachable).  See docs/AUDIT.md §1.55.
  python3 "$REPO_ROOT/tools/check_text_integrity.py"
}

run_stage "build (Release, $( [ "$FAST" -eq 1 ] && echo incremental || echo configure+incremental ), -j$JOBS)" build stage_build
run_stage "ctest (expect ${EXP_CTEST}/${EXP_CTEST}, 0 skipped)" ctest stage_ctest
run_stage "differential fuzz (interp vs AOT, expect 0 findings)" fuzz stage_fuzz
run_stage "economy migration (§43.5, expect 39/39)" economy stage_economy
run_stage "node protocol suites (expect ${EXP_NODE} registered)" node stage_node
run_stage "dsh-inimerse plugin (offline + live)" plugin stage_plugin
run_stage "oauth_loop crate (expect 75/75)" oauth-loop stage_oauth_loop
run_stage "userdata ignore rules (default deny)" ignored-credentials stage_ignored_credentials
run_stage "docs relative links" links stage_links
run_stage "docs backtick paths (expect 0 broken)" doc-paths stage_doc_paths
run_stage "tracked text files carry no NUL byte (expect 0)" text-integrity stage_text_integrity

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
