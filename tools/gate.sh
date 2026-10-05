#!/usr/bin/env bash
# gate.sh — the acceptance gate every Inimerse/Infiverse stream must pass before
# its branch may be merged into main.  See docs/BOARD.md §3.
#
#   tools/gate.sh              full gate: configure + build + all suites
#   tools/gate.sh --fast       skip configure/build, reuse the existing build/
#   tools/gate.sh --only links run a single stage
#                              (build|ctest|fuzz|economy|node|plugin|oauth-loop|
#                               ignored-credentials|links|doc-paths|text-integrity|
#                               orphan-fixtures|orphan-targets)
#                              `orphan-targets` asks whether a target is run, not
#                              whether it was compiled here: a target can be run
#                              by a CTest and still be named only inside a
#                              platform branch (docs/AUDIT.md 1.71), and then the
#                              PASS says nothing about the file it names.
#   tools/gate.sh --jobs 4     parallel job count for the build
#   tools/gate.sh --required-for <base>..<head>
#                              print the stages that range forces, one selector
#                              per line, then `required: N stage(s)`.  It runs
#                              nothing, and it exits 0 whenever it can answer:
#                              "which stages are required" and "the required
#                              stages ran" are two questions, and this answers
#                              only the first.  It exits 2 when it cannot
#                              answer -- an unresolvable range, a selector no
#                              stage answers to, or a registered stage no rule
#                              can reach.  See docs/AUDIT.md §1.72: a rule used
#                              to excuse work, that no command can run, is a
#                              rule applied by feel.
#
#                              The stage list is STAGE_SPECS, the same registry
#                              --only reads, so the two cannot drift.  Which
#                              stage a changed path forces comes from three
#                              places, and it is worth knowing which: the call
#                              sites, read out of each stage_ function in this
#                              file at run time (a name-shaped `tools/check_*`
#                              glob misses tools/im_diff_fuzz.py, node_suites,
#                              dsh-inimerse and Infiverse_standard/oauth_loop --
#                              the four stages whose subject is not a checker);
#                              the scopes, written down below because no call
#                              site says which subtree a subject reads; and
#                              text-integrity's own TEXT_SUFFIXES/TEXT_NAMES,
#                              read out of tools/check_text_integrity.py.
#
# Exit code 0 only when every stage passed.  Each stage prints PASS/FAIL/SKIP,
# and a summary table is printed last so a failing run is readable at a glance.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# This file's own path, resolved now: --required-for reads the call sites out of
# it, and $0 is not reliable once a stage has changed directory.
GATE_SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
cd "$REPO_ROOT" || exit 2

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
JOBS="$(nproc 2>/dev/null || echo 4)"
FAST=0
ONLY=""
REQUIRED_FOR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --fast) FAST=1 ;;
    --only) ONLY="${2:-}"; shift ;;
    --jobs) JOBS="${2:-4}"; shift ;;
    --required-for) REQUIRED_FOR="${2:-}"; shift ;;
    -h|--help) sed -n '2,44p' "$0"; exit 0 ;;
    *) echo "gate: unknown argument '$1'" >&2; exit 2 ;;
  esac
  shift
done

STAGE_NAMES=()
STAGE_RESULTS=()
STAGE_NOTES=()
# Every registered selector, in registration order.  Filled from STAGE_SPECS at
# the bottom of this file, never from a second handwritten list: the legal
# --only values, the stage count, and the paths --required-for maps to stages
# are all derived from that one array, so no two of them can drift apart.
STAGE_WANTED=()
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
EXP_CTEST="${EXP_CTEST:-148}"   # recounted off the merged tree by the commit below, not inherited from either side

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
  # A builtin name registered twice is dead code that reads as live: the name
  # resolves to whichever handler landed first on the probe chain, and the second
  # entry is unreachable.  vm_register_builtin now refuses the duplicate and says
  # so on stderr, so a suite that prints this line is a suite whose engine carries
  # one name with two answers.  Asserted here because it is a property of the
  # suites this stage just ran, and because no single test file can see it.
  if printf '%s\n' "$out" | grep -qF "is already registered"; then
    echo "gate: a builtin name was registered twice:" >&2
    printf '%s\n' "$out" | grep -F "is already registered" | sort -u >&2
    echo "gate: one name, one handler -- delete the second registration." >&2
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
# The program count stays fixed, so the finding set is deterministic and a regression
# is reproducible.  The counts are still asserted EXACTLY rather than as a ceiling,
# because the direction of the surprise matters:
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
#
# Two things are pinned here that the finding counts alone cannot express:
#
#   * the seed *set*, not one seed.  A single seed pinned the corpus to a single RNG
#     stream, so "the generator's range was explored" and "one stream was replayed"
#     read the same in the log -- the wider sweeps that confirmed the 0/0 pin
#     (seeds 2-10, docs/AUDIT.md §1.13) were manual runs this stage never repeated;
#   * the *denominator*.  `DIVERGE 0 / THREW 0` is also what a run that produced
#     nothing prints, so the stage asserts that the tool reported exactly
#     EXP_FUZZ_COUNT programs per seed AND that agreed+DIVERGE+THREW+untranslated
#     accounts for every one of them.  Without this the generator could stop
#     producing programs -- or the tool could be replaced by `echo` -- and the stage
#     would still pass, the same shape as the release workflow that ran 24 of the
#     123 cases it claimed (docs/AUDIT.md §1.13 「分母」, §1.42).
#
# Cost: ~45-50 s per seed.  Lower EXP_FUZZ_SEEDS only by saying so in docs/BOARD.md 3
# -- the set is part of the claim.
EXP_FUZZ_COUNT="${EXP_FUZZ_COUNT:-120}"
EXP_FUZZ_SEEDS="${EXP_FUZZ_SEEDS:-1 2 3}"
EXP_FUZZ_DIVERGE="${EXP_FUZZ_DIVERGE:-0}"
EXP_FUZZ_THREW="${EXP_FUZZ_THREW:-0}"

stage_fuzz() {
  local seed out rc div threw untr agreed prog
  local ran=0 total_agreed=0 total_div=0 total_threw=0 total_untr=0
  for seed in $EXP_FUZZ_SEEDS; do
    out="$(python3 "$REPO_ROOT/tools/im_diff_fuzz.py" \
             --count "$EXP_FUZZ_COUNT" --seed "$seed" 2>&1)"
    rc=$?
    printf '%s\n' "$out"
    if [ "$rc" -eq 2 ]; then
      echo "gate: the fuzzer could not find its engine or its translator." >&2
      return 1
    fi
    # The tool exits 1 whenever it finds anything -- the right default for interactive
    # use, but the pin below is what decides here, so the exit code is not the verdict.
    prog="$(printf '%s\n' "$out" | sed -n 's/^seed [0-9][0-9]*, \([0-9][0-9]*\) programs$/\1/p')"
    agreed="$(printf '%s\n' "$out" | sed -n 's/^  agreed *\([0-9]*\)$/\1/p')"
    div="$(printf '%s\n' "$out" | sed -n 's/^  DIVERGE *\([0-9]*\)$/\1/p')"
    threw="$(printf '%s\n' "$out" | sed -n 's/^  THREW *\([0-9]*\)$/\1/p')"
    untr="$(printf '%s\n' "$out" | sed -n 's/^  not translated *\([0-9]*\)$/\1/p')"
    if [ -z "$prog" ] || [ -z "$agreed" ] || [ -z "$div" ] || [ -z "$threw" ] || [ -z "$untr" ]; then
      echo "gate: could not read the fuzzer's counts (seed $seed) from its output." >&2
      return 1
    fi
    # The denominator.  The finding counts cannot tell "120 programs ran and all
    # agreed" from "no program ran at all", so assert both the run's size and that
    # every program landed in exactly one bucket.
    if [ "$prog" -ne "$EXP_FUZZ_COUNT" ]; then
      echo "gate: seed $seed ran $prog program(s), pinned $EXP_FUZZ_COUNT." >&2
      echo "gate: an empty or shrunken run must not pass as 'no findings'." >&2
      return 1
    fi
    if [ "$((agreed + div + threw + untr))" -ne "$prog" ]; then
      echo "gate: seed $seed accounted for $((agreed + div + threw + untr)) of $prog program(s)" >&2
      echo "gate: (agreed $agreed + DIVERGE $div + THREW $threw + not translated $untr)." >&2
      return 1
    fi
    total_agreed=$((total_agreed + agreed))
    total_div=$((total_div + div))
    total_threw=$((total_threw + threw))
    total_untr=$((total_untr + untr))
    ran=$((ran + 1))
  done
  if [ "$ran" -eq 0 ]; then
    echo "gate: EXP_FUZZ_SEEDS is empty ('$EXP_FUZZ_SEEDS') -- nothing ran, so nothing was verified." >&2
    return 1
  fi
  if [ "$total_untr" -ne 0 ]; then
    echo "gate: the generator produced $total_untr program(s) the AOT backend cannot translate." >&2
    echo "gate: that is a generator bug, not a divergence -- narrow the generator." >&2
    return 1
  fi
  if [ "$total_div" -ne "$EXP_FUZZ_DIVERGE" ] || [ "$total_threw" -ne "$EXP_FUZZ_THREW" ]; then
    echo "gate: differential fuzz found $total_div DIVERGE / $total_threw THREW, pinned $EXP_FUZZ_DIVERGE / $EXP_FUZZ_THREW" >&2
    echo "gate: ($ran seed(s) x $EXP_FUZZ_COUNT programs: $EXP_FUZZ_SEEDS)." >&2
    echo "gate: MORE findings is a new interpreter/AOT divergence -- do not paper over it." >&2
    echo "gate: FEWER findings means one was fixed -- promote that case into" >&2
    echo "gate: tools/aot_native.test.py, then bump EXP_FUZZ_DIVERGE/EXP_FUZZ_THREW" >&2
    echo "gate: here and the counts in docs/BOARD.md 3." >&2
    return 1
  fi
  echo "gate: fuzz findings match the pin over $ran seed(s) x $EXP_FUZZ_COUNT programs ($EXP_FUZZ_SEEDS): $total_div DIVERGE, $total_threw THREW, 0 untranslated, $total_agreed agreed."
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

stage_orphan_fixtures() {
  # docs/API.md's "evidence (CTest)" column claimed a CTest covered
  # vtest/lint_case_missing_default_v04.im and listed
  # vtest/lint_case_exhaustive_v04.im among four real CTest names.  Neither had
  # a registration line: the five lint_case_* tests are hand-listed, so the
  # sixth and seventh were never added, and both fixtures ran green by hand
  # while nothing compared their output.  This stage compares the set of test
  # inputs against the set that actually runs.  See docs/AUDIT.md §1.59.
  python3 "$REPO_ROOT/tools/check_orphan_fixtures.py"
}

stage_orphan_targets() {
  # The stage above compares the set of test *inputs* against the set that
  # actually runs.  It cannot see the same defect one level down: an executable
  # that is built and run by nothing.  `src/platform/vfs.c`'s `..` guard read
  # uninitialised memory and was accepted 40 times out of 40, and the one
  # program that asserts the correct behaviour, `src/platform/vfs_probe.c`, was
  # built while nothing ever ran it -- `tools/check_orphan_fixtures.py` globs
  # `vtest/`, `tools/*.test.py` and `tools/*.test.js`, so a probe in `src/` is
  # outside its denominator by construction (`grep -c 'src/'` on it is 0).
  # This stage compares the `add_executable( )` set against the targets some
  # `add_test( )` actually names.  See docs/AUDIT.md §1.67.
  python3 "$REPO_ROOT/tools/check_orphan_targets.py"
}

# ── the stage registry ──────────────────────────────────────────────────────
#
# One entry per stage: selector|label|function.  Every consumer reads this
# array -- the runner below, the --only legality check, and --required-for --
# so the legal selectors, the stage count and the path table cannot drift from
# one another.  The count is written down nowhere: `all` is resolved from this
# array and the summary counts its entries, so adding a stage is this array and
# nothing else.  The gate grew a thirteenth stage while the table below was
# being written; a hardcoded twelve would have been wrong on the day it was
# typed.
STAGE_SPECS=(
  "build|build (Release, $( [ "$FAST" -eq 1 ] && echo incremental || echo configure+incremental ), -j$JOBS)|stage_build"
  "ctest|ctest (expect ${EXP_CTEST}/${EXP_CTEST}, 0 skipped)|stage_ctest"
  "fuzz|differential fuzz (interp vs AOT, expect 0 findings)|stage_fuzz"
  "economy|economy migration (§43.5, expect 39/39)|stage_economy"
  "node|node protocol suites (expect ${EXP_NODE} registered)|stage_node"
  "plugin|dsh-inimerse plugin (offline + live)|stage_plugin"
  "oauth-loop|oauth_loop crate (expect 75/75)|stage_oauth_loop"
  "ignored-credentials|userdata ignore rules (default deny)|stage_ignored_credentials"
  "links|docs relative links|stage_links"
  "doc-paths|docs backtick paths (expect 0 broken)|stage_doc_paths"
  "text-integrity|tracked text files carry no NUL byte (expect 0)|stage_text_integrity"
  "orphan-fixtures|test inputs that no CTest runs (expect 0)|stage_orphan_fixtures"
  "orphan-targets|executables that no CTest runs (expect 0)|stage_orphan_targets"
)
for _spec in "${STAGE_SPECS[@]}"; do
  STAGE_WANTED+=("${_spec%%|*}")
done

# ── which stages a change forces ────────────────────────────────────────────
#
# docs/AUDIT.md §1.72 writes the rule down: a change under docs/ does not need
# the build, a change under src/ does.  It was left without an executable form
# -- the mapping from a changed path to the stages it forces lived only as
# prose in a table, and no command returned 0 or 1.  A judgement used to excuse
# work, that cannot be run, is a judgement applied by feel.
#
# The stage list is not written down here.  It is STAGE_SPECS, the registry
# --only already reads, and this command reads it for the same reason --only
# does: a list nothing consumes only constrains the moment it was written.
# Adding a stage there, and its stage_ function, is enough for both commands.
#
# Where the mapping comes from is two parts, and the split is the point:
#
#   (1) The call sites.  For every registry entry, the repo paths its stage_
#       function actually executes are read out of this file at run time.  A
#       name-shaped glob gets this wrong, and the four stages it gets wrong on
#       are the four whose subject is not called check_*: tools/im_diff_fuzz.py
#       backs fuzz, tools/node_suites/run_all.js backs node,
#       tools/dsh-inimerse/verify.mjs backs plugin, and
#       Infiverse_standard/oauth_loop backs oauth-loop.
#   (2) The scopes.  Which subtree a stage's subject covers.  A call site
#       cannot state this: `stage_links` runs tools/check_links.py, and nothing
#       in that line says the checker reads docs/.  They are written down
#       below, and every name in them is resolved against the registry on every
#       run -- an unknown name is exit 2, not a row that quietly stops excusing
#       anything.
#
# text-integrity is a third kind of case, and it is read rather than written:
# its two entry points are TEXT_SUFFIXES and TEXT_NAMES in its own source, so a
# change to LICENSE, Makefile, Dockerfile, or any .c/.md/.yml/.json file forces
# it whatever directory the file is in.  Writing that list here as well would be
# a copy free to drift from the file it describes.
#
# Three properties are deliberate, and none of them is obvious:
#
#   * A path that matches nothing forces EVERY stage, not none.  A table that
#     exists to excuse work is worse than no table when it excuses too much, so
#     the unknown case falls on the expensive side -- and says so on stderr,
#     because a silent fallback would hide how incomplete the table is.
#   * `all` is resolved from STAGE_SPECS, never written out.  See above.
#   * A rule naming a stage no stage answers to is exit 2, and so is a
#     registered stage no rule can reach.  Those are one defect seen from two
#     sides -- a table and a registry that disagree -- and either side silently
#     becomes an excused stage.
REQUIRED_SCOPES=(
  # changed subtree                     stages whose subject reads it
  "docs/*|links doc-paths"
  "src/*|build ctest fuzz economy plugin"
  "vtest/*|ctest orphan-fixtures"
  "tools/*.test.py|ctest orphan-fixtures"
  "tools/*.test.js|ctest orphan-fixtures"
  "tools/node_suites/*|node"
  "tools/dsh-inimerse/*|plugin"
  # A change to the gate itself, or to the build, invalidates every stage's
  # premise.  Nothing is excused.
  "tools/gate.sh|all"
  "CMakeLists.txt|all"
  # CI is CI's own business and needs no stage here.
  ".github/*|-"
)

# The call sites: for each stage function in this file, the repo paths it
# executes.  Comments and messages are skipped -- a path named in a comment is a
# path nobody runs, and mapping a change to a stage because a comment mentions
# the file is how a table starts lying.  Measured: without the two skips,
# docs/BOARD.md is named in five stage bodies and would force five stages.
derived_edges() {
  awk '
    /^stage_[a-z_]+\(\) *\{/ {
      fn = $1; sub(/\(\).*/, "", fn); next
    }
    /^\}/ { fn = "" }
    fn != "" {
      l = $0; sub(/^[ \t]+/, "", l)
      if (l ~ /^#/) next
      if (l ~ /echo|printf/) next
      line = $0
      while (match(line, /(tools|src|vtest|docs|Infiverse_standard|packages|mods|selfhost|scripts|examples|future|rooms|universe|userdata)\/[A-Za-z0-9_.\/-]*[A-Za-z0-9_-]/)) {
        print fn "\t" substr(line, RSTART, RLENGTH)
        line = substr(line, RSTART + RLENGTH)
      }
      while (match(line, /CMakeLists\.txt/)) {
        print fn "\t" substr(line, RSTART, RLENGTH)
        line = substr(line, RSTART + RLENGTH)
      }
    }' "$GATE_SELF" | sort -u
}

# text-integrity's two entry points, read out of the checker that owns them.
# TEXT_SUFFIXES is a multi-line literal and TEXT_NAMES a one-line one; both end
# in a line containing ')', which is what closes the block.
derived_text_globs() {
  awk '
    /^TEXT_SUFFIXES *= *frozenset\(/ { block = 1 }
    /^TEXT_NAMES *= *frozenset\(/ { block = 1 }
    block {
      line = $0
      while (match(line, /"[.A-Za-z0-9_-]+"/)) {
        s = substr(line, RSTART + 1, RLENGTH - 2)
        if (s ~ /^\./) print "*" s; else print s
        line = substr(line, RSTART + RLENGTH)
      }
      if (/\)/) block = 0
    }' "$REPO_ROOT/tools/check_text_integrity.py" | sort -u
}

# The registry's selector for a stage function.  This is the one place the two
# names are tied together: derived_edges keys on the function it read out of
# this file, and STAGE_SPECS says which selector that function answers to.  A
# function no registry entry names is exit 2 -- it is a stage that exists and
# cannot be required, and its edges would otherwise point at a name nothing
# answers to.
selector_for_function() {
  local want="$1" spec
  for spec in "${STAGE_SPECS[@]}"; do
    if [ "${spec##*|}" = "$want" ]; then echo "${spec%%|*}"; return 0; fi
  done
  return 1
}

# Which stages a single changed path forces.  Prints selectors, in no order.
# A named file matches exactly; a named directory matches everything under it.
stages_for_path() {
  local path="$1" rule pattern sels sel g
  for rule in "${REQUIRED_SCOPES[@]}"; do
    pattern="${rule%%|*}"; sels="${rule#*|}"
    case "$path" in $pattern) ;; *) continue ;; esac
    [ "$sels" = "-" ] && continue
    if [ "$sels" = "all" ]; then
      for sel in "${STAGE_WANTED[@]}"; do echo "$sel"; done
    else
      for sel in $sels; do echo "$sel"; done
    fi
  done
  while IFS=$'\t' read -r sel pattern; do
    [ -n "$sel" ] || continue
    case "$path" in
      "$pattern") echo "$sel" ;;
      "$pattern"/*) echo "$sel" ;;
    esac
  done <<<"$DERIVED_EDGES"
  for g in ${TEXT_GLOBS[@]+"${TEXT_GLOBS[@]}"}; do
    case "$path" in $g) echo "text-integrity"; break ;; esac
  done
}

# Filled in once per --required-for run, before any question is answered.
DERIVED_EDGES=""
TEXT_GLOBS=()

# Print the stages a range forces, in registry order, then the count.
required_for() {
  local range="$1" changed
  if ! changed="$(git -C "$REPO_ROOT" diff --name-only "$range" 2>&1)"; then
    echo "gate: --required-for: git cannot resolve '$range' as a range:" >&2
    printf '%s\n' "$changed" >&2
    echo "gate: refusing to answer a question about a range that does not exist." >&2
    exit 2
  fi

  # The derivation, once, before anything is answered.
  local -a edges=()
  local fn
  while IFS=$'\t' read -r fn _path; do
    [ -n "$fn" ] || continue
    if ! sel="$(selector_for_function "$fn")"; then
      echo "gate: --required-for: $GATE_SELF defines $fn, but no registry entry names it." >&2
      echo "gate: a stage function the registry does not answer to cannot be required by anything." >&2
      exit 2
    fi
    edges+=("$sel"$'\t'"$_path")
  done <<<"$(derived_edges)"
  DERIVED_EDGES="$(printf '%s\n' ${edges[@]+"${edges[@]}"})"
  TEXT_GLOBS=()
  while IFS= read -r _g; do [ -n "$_g" ] && TEXT_GLOBS+=("$_g"); done < <(derived_text_globs)
  if [ "${#TEXT_GLOBS[@]}" -eq 0 ]; then
    echo "gate: --required-for: could not read TEXT_SUFFIXES/TEXT_NAMES out of tools/check_text_integrity.py." >&2
    echo "gate: text-integrity's scope is read, not written down here; an empty read is not a rule." >&2
    exit 2
  fi
  if [ -z "$DERIVED_EDGES" ]; then
    echo "gate: --required-for: read no call site out of any stage_ function in $GATE_SELF." >&2
    echo "gate: an empty call-site table would excuse every stage; refusing to answer." >&2
    exit 2
  fi

  local sel pattern path sels s out joined hit

  # Every derived edge must force its own stage.  This is the derivation
  # checking itself: if the matching below cannot reach the stage a call site
  # belongs to, the table is being read and not consumed, which is the defect
  # --required-for exists to make visible.
  while IFS=$'\t' read -r sel path; do
    [ -n "$sel" ] || continue
    joined="$(stages_for_path "$path" | sort -u | tr '\n' ' ')"
    case " $joined " in
      *" $sel "*) ;;
      *)
        echo "gate: --required-for: the call site in stage_${sel//-/_} names '$path'," >&2
        echo "gate: but a change to '$path' does not force '$sel'.  The derivation is not" >&2
        echo "gate: reaching the stage it came from; refusing to answer." >&2
        exit 2
        ;;
    esac
  done <<<"$DERIVED_EDGES"

  local -a picked=()
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    out="$(stages_for_path "$path")"
    if [ -z "$out" ]; then
      echo "gate: --required-for: no rule matches '$path'; requiring every stage." >&2
      for sel in "${STAGE_WANTED[@]}"; do picked+=("$sel"); done
    else
      while IFS= read -r sel; do [ -n "$sel" ] && picked+=("$sel"); done <<<"$out"
    fi
  done <<<"$changed"

  # A rule that names a stage nobody answers to has quietly stopped excusing
  # anything, or quietly started excusing the wrong thing.
  if [ "${#picked[@]}" -gt 0 ]; then
    for sel in "${picked[@]}"; do
      hit=0
      for path in "${STAGE_WANTED[@]}"; do
        [ "$path" = "$sel" ] && hit=1 && break
      done
      if [ "$hit" -eq 0 ]; then
        echo "gate: --required-for: a rule names stage '$sel', but no stage answers to it." >&2
        echo "gate: the path table and the stage registry disagree; refusing to answer." >&2
        exit 2
      fi
    done
  fi

  # And the other direction: a registered stage no rule can reach is a stage
  # this command would excuse for every change there is.  Reachability counts
  # all three sources -- the scopes, the call sites, and text-integrity's own
  # entry points, which is the one stage no path rule has to name.
  local reach
  for path in "${STAGE_WANTED[@]}"; do
    reach=0
    for sel in "${REQUIRED_SCOPES[@]}"; do
      sels="${sel#*|}"
      [ "$sels" = "-" ] && continue
      if [ "$sels" = "all" ]; then reach=1; break; fi
      for s in $sels; do
        if [ "$s" = "$path" ]; then reach=1; break; fi
      done
      [ "$reach" -eq 1 ] && break
    done
    if [ "$reach" -eq 0 ]; then
      while IFS=$'\t' read -r s _p; do
        [ "$s" = "$path" ] && { reach=1; break; }
      done <<<"$DERIVED_EDGES"
    fi
    [ "$path" = "text-integrity" ] && reach=1
    if [ "$reach" -eq 0 ]; then
      echo "gate: --required-for: stage '$path' is registered, but no rule can reach it." >&2
      echo "gate: a stage no rule can require is a stage every change silently excuses." >&2
      exit 2
    fi
  done

  local n=0
  for path in "${STAGE_WANTED[@]}"; do
    hit=0
    if [ "${#picked[@]}" -gt 0 ]; then
      for sel in "${picked[@]}"; do
        if [ "$sel" = "$path" ]; then hit=1; break; fi
      done
    fi
    [ "$hit" -eq 1 ] || continue
    echo "$path"
    n=$((n + 1))
  done
  echo "required: $n stage(s)"
}

# --required-for answers a question; it does not run the gate, so it returns
# before the first stage starts.
if [ -n "$REQUIRED_FOR" ]; then
  required_for "$REQUIRED_FOR"
  exit 0
fi

for _spec in "${STAGE_SPECS[@]}"; do
  _sel="${_spec%%|*}"; _rest="${_spec#*|}"
  run_stage "${_rest%%|*}" "$_sel" "${_rest#*|}"
done

# An --only value that matches no stage used to skip every stage, print
# "gate: OK -- every stage passed." and exit 0: a green light from a run that
# executed nothing.  A skip is not a pass, and skipping all of them is the most
# complete form of that mistake -- a CI step with a misspelled stage name would
# have been silently green.  The legal values are derived from STAGE_WANTED,
# which run_stage filled in above, so this check cannot go stale on its own.
if [ -n "$ONLY" ]; then
  matched=0
  for w in "${STAGE_WANTED[@]}"; do
    [ "$w" = "$ONLY" ] && matched=1
  done
  if [ "$matched" -eq 0 ]; then
    echo "gate: --only '$ONLY' matches no stage; nothing was run." >&2
    echo "gate: legal values: ${STAGE_WANTED[*]}" >&2
    echo "gate: refusing to report a pass for a run that executed nothing." >&2
    exit 2
  fi
fi

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
if [ -n "$ONLY" ]; then
  echo "gate: OK — the selected stage passed (--only $ONLY)."
  echo "gate: this was NOT the full gate: ${#STAGE_WANTED[@]} stages are registered and only this one ran."
else
  echo "gate: OK — every stage passed (${#STAGE_NAMES[@]}/${#STAGE_WANTED[@]} stages ran)."
fi
exit 0
