#!/usr/bin/env bash
# gate.sh — the acceptance gate every Inimerse/Infiverse stream must pass before
# its branch may be merged into main.  See docs/BOARD.md §3.
#
#   tools/gate.sh              full gate: configure + build + all suites
#   tools/gate.sh --fast       skip configure/build, reuse the existing build/
#   tools/gate.sh --only links run a single stage
#                              (build|ctest|fuzz|economy|node|plugin|oauth-loop|
#                               ignored-credentials|links|doc-paths|text-integrity|
#                               orphan-fixtures|orphan-targets|line-refs|
#                               release-tags|obs-markers|fences)
#                              `orphan-targets` asks whether a target is run, not
#                              whether it was compiled here: a target can be run
#                              by a CTest and still be named only inside a
#                              platform branch (docs/AUDIT.md 1.71), and then the
#                              PASS says nothing about the file it names.
#                              `line-refs` asserts the denominators of the sweep
#                              over the line numbers docs/ cites in
#                              CMakeLists.txt and tools/gate.sh -- how many
#                              citations are written in each of the four forms,
#                              and how many have nothing holding them.  It does
#                              not assert that any number is right; a number
#                              still there that now points at something else is
#                              not caught.  See docs/AUDIT.md 1.72 and the
#                              checker's own docstring.
#                              `release-tags` asks whether the sha a document
#                              records for a tag is still the sha that tag points
#                              at.  A tag is a moving pointer, and moving one
#                              leaves no trace locally -- `git reflog show <tag>`
#                              prints nothing -- so a number that was true when
#                              it was written and is false now cannot be caught
#                              by reading.  See docs/AUDIT.md 1.74 E.
#   tools/gate.sh --jobs 4     parallel job count for the build
#   tools/gate.sh --required-for <base>..<head>|staged|worktree|<rev>|<path>
#                              print the stages that change forces, one selector
#                              per line, then `required: N stage(s)` and, in
#                              brackets, which question was answered.  It runs
#                              nothing, and it exits 0 whenever it can answer:
#                              "which stages are required" and "the required
#                              stages ran" are two questions, and this answers
#                              only the first.  One argument is handed to git
#                              as it stands, and git reads three different
#                              things out of it: `<base>..<head>` is committed
#                              history and does not read the working tree at
#                              all, a single rev is that commit against the
#                              working tree and does, and a bare path is the
#                              unstaged changes to that path and reads neither
#                              history nor the index.  `staged` and `worktree`
#                              name the two questions git's own syntax has no
#                              spelling for.  All five print the same
#                              `required: 0 stage(s)` when nothing changed, so
#                              the count line says which one it was: a mistyped
#                              path is a call that answers a question nobody
#                              asked, and the bracket is where that shows.
#
#                              It exits 2 when it cannot answer: an argument
#                              git cannot resolve, a missing argument, a
#                              selector no stage answers to, or a registered
#                              stage no rule can reach.  See docs/AUDIT.md
#                              §1.72: a rule used to excuse work, that no
#                              command can run, is a rule applied by feel.
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
#                              The two lists watch each other, and each
#                              direction has a command that goes red: a scopes
#                              row naming a stage STAGE_SPECS does not declare
#                              exits 2 ("a rule names stage 'nosuchstage', but
#                              no stage answers to it."), and a STAGE_SPECS row
#                              no scopes row and no `all` row can reach also
#                              exits 2 ("stage 'build' is registered, but no
#                              rule can reach it.").  A reader does not have to
#                              take the second table's word for agreeing with
#                              the first; he can run it.
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
# Whether the flag was given at all.  An empty REQUIRED_FOR used to mean both
# "the flag is absent" and "the flag was given no argument", so
# `--required-for` on its own ran the full gate instead of refusing: the same
# shape as the other refusals below, one flag earlier.
REQUIRED_FOR_SET=0

while [ $# -gt 0 ]; do
  case "$1" in
    --fast) FAST=1 ;;
    --only) ONLY="${2:-}"; shift ;;
    --jobs) JOBS="${2:-4}"; shift ;;
    --required-for) REQUIRED_FOR="${2:-}"; REQUIRED_FOR_SET=1; shift ;;
    -h|--help)
      # The header comment is the help text, and its last line is whatever sits
      # just above `set -uo pipefail`.  It used to be a fixed number here, which
      # meant every added line of help was also an edit here -- and the edit that
      # gets forgotten is the one in the file nobody reads while changing the
      # other (docs/SYNTAX.md H4.1: a drifting value is not fixed by a new
      # number, only by a different way of writing it).
      sed -n "2,$(( $(grep -n '^set -uo pipefail' "$0" | head -1 | cut -d: -f1) - 1 ))p" "$0"
      exit 0 ;;
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
# Which stages failed, in the order they ran, and where each one's log is.  Kept
# apart from STAGE_RESULTS because the verdict line has to name its own subjects:
# a reader who pastes "gate: FAILED" has nothing to act on, and the table that
# would have told them is exactly what a paste throws away.
FAILED_STAGE_NAMES=()
FAILED_STAGE_LOGS=()

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
EXP_CTEST="${EXP_CTEST:-184}"   # recounted off the merged tree by the commit below, not inherited from either side; +1 when the serializer-cycle registration was added (150 -> 151), +1 for the error-domain width guard (153 -> 154), +1 for the neg-bool unary-minus regression (155 -> 156), +1 when websocket_probe stopped being built-but-unrun (156 -> 157), +1 when check_frozen_identity.test.py got the CTest it needed (157 -> 158), +1 when the CLI harness got its own CTest (158 -> 159, added by 699a88c), +2 when the self-hosted interpreter got its first running test and the pool-growth shape test came with it (159 -> 161, added by the commit that rebased inline_buf after arrays_big's realloc, +1 when the self-hosted compiler got the corpus equivalence test (161 -> 162, added by the commit that rebased a compiled program's relative paths onto the target's directory), +1 when the self-hosted compiler got its fixed point measured by a registered test (162 -> 163, added by the commit that added tools/dec_selfcompile_probe.py), +1 when the bootstrap's own INIMBC outlet got the test that the image reproduces itself (163 -> 164, added by the commit that added tools/dec_inimbc_outlet_probe.py, +1 when the three routes that run the same program got a registered comparison (164 -> 165, added by the commit that added tools/dec_mirror_behavior_probe.py), +1 when the two writes past the register window got a witness built from a bytecode file (165 -> 166, added by the commit that added tools/vm_frame_bounds.test.py), +1 when a 256-byte identifier stopped being silently truncated to 255 and started being refused (166 -> 167, added by the commit that added tools/identifier_truncation.test.py), +1 when the writer half of the INIMBC cycle became a registered test, with its refusal path measured instead of promised (167 -> 168, added by the commit that added tools/check_inimbc_writer_half.py), +1 when the prover pin self-check got a registered test that refuses with exit 2 instead of skipping (168 -> 169, added by the commit that added tools/z3bounds_selfcheck_pins.py), +1 when the extent of the reference reading became a measurement with a control in both directions (169 -> 170, added by the commit that added tools/checker_scope.test.py), +1 when a section that is deleted stopped being invisible to every ceiling (170 -> 171, added by the commit that added tools/docs_headings.test.py), +1 when the holds suite was registered without its expectation (173 -> 174, added by the commit that added tools/holds.test.py -- the registration and the number it implies belong in one commit, or everyone else’s gate goes red for a reason that names neither), +1 when the IR opcode table got a test that measures it against the tree instead of remembering it (174 -> 175, added by the commit that added tools/ir_spec.py, +1 when the interpreter opcode census got the controls that say what it cannot see (175 -> 176, added by the commit that added tools/op_hot.test.py), +1 when one program started being run on all three backends and compared, with the refusal surface pinned rather than excused (176 -> 177, added by the commit that registered tools/three_way_parity.test.py), +1 when the P1 relabel-only gate got a registered test that refuses to call an unmeasured relabel a relabel (178 -> 179, added by the commit that registered tools/mir_wired.test.py), +1 when the interpreter\'s opcode table started answering every fetch beside the switch, so a table that drifts from the dispatch is caught while the dispatch still exists (179 -> 180, added by the commit that registered tools/ir_table_same.test.py), +1 when the native path stopped being judged by whether it compiles and started being judged by whether it computes the same answers as the interpreter over the programs it accepts, with the numbers pinned to a tree and recomputed (180 -> 181, added by the commit that registered tools/aot_cover.test.py)
#   CTest that would fail the day the MIR layer starts deciding something (177 -> 178,
#   added by the commit that registered tools/mir_roundtrip.test.py, +1 when the P1 wiring got a registered test that compiles the whole corpus twice and compares the bytes (178 -> 179, added by the commit that registered tools/mir_wired.test.py -- the harness builds its own baseline from the recorded commit, so the gate needs no second checkout, +1 when the refusal histogram's decoder got a self-check that runs on recorded messages and no build (181 -> 182, added by the commit that added --self-check to tools/aot_cover.test.py, +1 when the MIR expressiveness got measured instead of remembered -- field by offsetof, copy by planting a value and running the regeneration (182 -> 183, added by the commit that added tools/mir_express.test.py), +1 when the guard against shared temporary paths got a self-test that measures the detector instead of trusting it (183 -> 184, added by the commit that registered tools/check_shared_tmp.py --self-test)))))

# The JS suite count, asserted for the same reason as EXP_CTEST: a suite dropped
# from tools/node_suites/run_all.js SUITES must not leave a green stage behind.
EXP_NODE="${EXP_NODE:-12}"

# Where the panels suite's jsdom lives.  It is installed outside the repository,
# at the location this stage's own failure text has always printed, so whether
# the stage could see it used to be a property of the CALLER's environment and
# not of the tree: one tree, one gate.sh, two verdicts, and the same label over
# both.  Probe that one location here, so the stage label can say which of the
# three paths was taken.  An explicit NODE_PATH still wins -- that is the caller
# saying where, and it is also the only way left to reach the skip path on
# purpose (point it at a directory that has no jsdom).
NODE_SUITES_JSDOM="none -- no jsdom at $HOME/.local/inimerse-jsdom/node_modules"
if [ -n "${NODE_PATH:-}" ]; then NODE_SUITES_JSDOM="$NODE_PATH (from NODE_PATH)"
elif [ -f "$HOME/.local/inimerse-jsdom/node_modules/jsdom/package.json" ]; then
  export NODE_PATH="$HOME/.local/inimerse-jsdom/node_modules"
  NODE_SUITES_JSDOM="$NODE_PATH (probed)"
fi

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
  # The stage log lives in BUILD_DIR, not in /tmp: a path named by the verdict
  # line has to survive the run, and /tmp collides across worktrees whose stages
  # have the same name.  A passing stage's log is deleted; a failing one is moved
  # to a name derived from the stage, so the verdict line can point at it.
  local log
  log="$(mktemp "$BUILD_DIR/.gate-stage-XXXXXX.log")" || {
    echo "gate: cannot create a stage log in $BUILD_DIR" >&2; exit 2; }
  "$@" 2>&1 | tee "$log"
  local rc="${PIPESTATUS[0]}"
  local slug
  slug="$(printf '%s' "$name" | tr 'A-Z' 'a-z' | sed 's/[^a-z0-9]\+/-/g; s/^-//; s/-$//')"
  if [ "$rc" -eq 0 ]; then
    STAGE_NAMES+=("$name"); STAGE_RESULTS+=("PASS"); STAGE_NOTES+=("")
    echo "✔ $name"
    rm -f "$log"
  else
    local kept="$BUILD_DIR/.gate-stage-$slug.log"
    mv -f "$log" "$kept"
    STAGE_NAMES+=("$name"); STAGE_RESULTS+=("FAIL"); STAGE_NOTES+=("exit $rc")
    FAILED_STAGE_NAMES+=("$name"); FAILED_STAGE_LOGS+=("$kept")
    echo "✘ $name (exit $rc)"
    echo "  log kept at $kept"
    FAILED=1
  fi
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

stage_line_refs() {
  # A number cited in docs/ is held by something, or it is not.  docs/ cites
  # line numbers in CMakeLists.txt and in tools/gate.sh; CMakeLists.txt's moved
  # eight times in one merge series by eight different offsets (+43 +34 +15 +34
  # +23 +14 +11 +11) and again by +67, so no single correction repairs them, and
  # 13 citations of tools/gate.sh moved at once when this file grew by 35 lines.
  # This stage asserts the sweep's DENOMINATORS, not its anchor rate: that the
  # input set is non-empty, that each of the four writings is still present (a
  # checker that has quietly become a prefix scan reports that nothing was
  # missed while missing the shape it exists to find), that the explicit-form
  # count has not shrunk, and that the unanchored count has not grown -- 501 is
  # a known quantity today and 520 tomorrow is an unannounced regression.
  #
  # It does not assert that any number is right.  A number that is still there
  # and now points at something else is not caught here.  See docs/AUDIT.md
  # 1.72 and the checker's own docstring.
  python3 "$REPO_ROOT/tools/check_line_refs.py"
}

stage_release_tags() {
  # A tag is a moving pointer, and this is the fourth kind of failure: the
  # number was right when it was written, and then somebody moved the thing
  # being measured.  docs/STATUS.md's release-point cell once read
  # `v0.5.2` -> `0ebd68d`, which was true; the tag was moved twice and the same
  # cell became false with no edit to it.  It is also invisible locally: moving
  # a tag leaves no trace, so `git reflog show v0.5.2` prints nothing and there
  # is no history to read.  A number that was true and is now false, with no
  # edit and no log entry, has to be checked by running something.
  #
  # The checker reads the tag names out of `git tag -l`, so the next release
  # adds one without anyone editing it -- a checker with `v0.5.2` written in it
  # would need editing at exactly the moment it is most likely to be forgotten.
  # See docs/AUDIT.md 1.74 E.
  python3 "$REPO_ROOT/tools/check_release_tags.py"
}

stage_obs_markers() {
  # A citation that carries an observation marker claims that the number beside
  # it was read on the tree the marker names.  That claim is checkable, and this
  # is the only place it is checked: a marker is otherwise prose, and prose
  # cannot be run.
  #
  # It is registered as a stage while its own ceiling is a debt.  The pin is
  # still an equality -- pointers may grow by at most EXP_OBS_POINTERS_DELTA_MAX
  # against EXP_OBS_BASE, and the checker prints the ceiling beside the reading
  # -- but the ceiling was set, in the commit that registered this stage, to the
  # value measured there rather than to zero.  A ceiling of zero while the
  # pointers are unaccounted for is a rule that is red on the day it lands; a
  # ceiling set to the measured value is a debt with a number on it, and paying
  # it down means lowering the ceiling in the same commit that removes the
  # pointers.  See docs/AUDIT.md 1.78.
  python3 "$REPO_ROOT/tools/check_obs_markers.py"
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
  "node|node protocol suites (expect ${EXP_NODE} registered; jsdom ${NODE_SUITES_JSDOM})|stage_node"
  "plugin|dsh-inimerse plugin (offline + live)|stage_plugin"
  "oauth-loop|oauth_loop crate (expect 75/75)|stage_oauth_loop"
  "ignored-credentials|userdata ignore rules (default deny)|stage_ignored_credentials"
  "links|docs relative links|stage_links"
  "doc-paths|docs backtick paths (expect 0 broken)|stage_doc_paths"
  "text-integrity|tracked text files carry no NUL byte (expect 0)|stage_text_integrity"
  "orphan-fixtures|test inputs that no CTest runs (expect 0)|stage_orphan_fixtures"
  "orphan-targets|executables that no CTest runs (expect 0)|stage_orphan_targets"
  "line-refs|line numbers docs/ cites in CMakeLists.txt / gate.sh (pins held)|stage_line_refs"
  "release-tags|sha a document records for a tag, against the tag today|stage_release_tags"
  "obs-markers|every citation names the tree it was read on|stage_obs_markers" "fences|fences (verdict=PASS expected, odd=0 swallowed=0)|stage_fences" "shared-tmp|artifacts built at a path every process shares (expect 0)|stage_shared_tmp"
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
  #
  # Every row says why it is written rather than derived.  "The scopes are
  # handwritten" is only defensible if each row names what a call site could not
  # have said, so each one does -- and there are two kinds of reason, the second
  # being the one to watch: a reader exists but does not name the subtree, or
  # there is no reader in this file at all.
  #
  # docs/* -- reader named, subtree not.  stage_links runs
  #   tools/check_links.py, and that line passes it no directory: the file the
  #   call site names says nothing about which tree the checker walks.
  #   stage_line_refs runs tools/check_line_refs.py, which globs `docs/**/*.md`
  #   for itself and is pointed at no path either.
  #   stage_release_tags runs tools/check_release_tags.py, which walks docs/ the
  #   same way; and what it compares those documents against is `git tag -l`,
  #   which is not a path at all, so no call site could have named it.
  "docs/*|links doc-paths line-refs release-tags obs-markers fences"
  # src/* -- no reader.  src/ is read by the compiler, and a compiler is not a
  #   path named in this file.  fuzz and economy read a built engine, plugin the
  #   shipped plugin, ctest the binaries; none of them names a source file.
  "src/*|build ctest fuzz economy plugin"
  # vtest/* -- no reader.  stage_ctest runs ctest, which is handed no fixture
  #   list, and check_orphan_fixtures.py builds its own set by globbing
  #   vtest/*.im rather than being pointed at one.
  "vtest/*|ctest orphan-fixtures"
  # tools/*.test.py, tools/*.test.js -- same shape as vtest/*: ctest names no
  #   path, and the orphan checker globs the pattern itself.
  "tools/*.test.py|ctest orphan-fixtures shared-tmp"
  "tools/*.test.js|ctest orphan-fixtures"
  # tools/node_suites/* -- reader named, subtree not.  stage_node runs
  #   tools/node_suites/run_all.js, and no line in this file lists the suites
  #   that runner discovers for itself.
  "tools/node_suites/*|node"
  # tools/dsh-inimerse/* -- reader named, subtree not.  stage_plugin runs
  #   tools/dsh-inimerse/verify.mjs, which loads the rest of that directory.
  "tools/dsh-inimerse/*|plugin"
  # tools/gate.sh -- no reader.  No stage_ function names this file; every stage
  #   depends on it because every stage is run by it, and "is run by" is not a
  #   call site.
  "tools/gate.sh|all"
  # CMakeLists.txt -- no reader.  cmake reads it and cmake is not a path named
  #   here; stage_build names the source directory, not this file.  A change to
  #   the build graph invalidates every stage's premise, so nothing is excused.
  "CMakeLists.txt|all"
  # .github/* -- no reader and no stage: CI is CI's own business.
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

# Print the stages a change forces, in registry order, then the count, then
# which question was answered.
#
# The bracket on the count line is not decoration.  Git reads three different
# questions out of one argument -- a range is committed history, a single rev is
# that commit against the working tree, a bare path is the unstaged changes to
# that path -- and all three print `required: 0 stage(s)` when nothing changed.
# A mistyped path therefore gets a confident, well-formed answer to a question
# nobody asked.  Refusing the path would be worse: the single-rev form is the
# only one that reads the working tree at all, and a rule demanding `..` would
# forbid it.  So the answer says what it inspected instead.
required_for() {
  local range="$1" changed label

  if [ -z "$range" ]; then
    echo "gate: --required-for needs an argument: '<base>..<head>', 'staged', 'worktree'," >&2
    echo "gate: a single rev, or a path." >&2
    echo "gate: with no argument it ran the full gate instead of refusing, which is this same" >&2
    echo "gate: shape one flag earlier: a question never asked, reporting a result." >&2
    exit 2
  fi

  case "$range" in
    staged)
      label="staged: the index against HEAD -- the working tree is not read"
      if ! changed="$(git -C "$REPO_ROOT" diff --cached --name-only 2>&1)"; then
        echo "gate: --required-for staged: git could not diff the index:" >&2
        printf '%s\n' "$changed" >&2
        exit 2
      fi
      ;;
    worktree)
      label="worktree: the worktree and index against HEAD, plus untracked files"
      if ! changed="$(git -C "$REPO_ROOT" diff HEAD --name-only 2>&1
                       git -C "$REPO_ROOT" ls-files --others --exclude-standard 2>&1)"; then
        echo "gate: --required-for worktree: git could not read the worktree:" >&2
        printf '%s\n' "$changed" >&2
        exit 2
      fi
      ;;
    *)
      if ! changed="$(git -C "$REPO_ROOT" diff --name-only "$range" 2>&1)"; then
        echo "gate: --required-for: git cannot resolve '$range' against anything:" >&2
        printf '%s\n' "$changed" >&2
        echo "gate: refusing to answer a question about an argument that does not exist." >&2
        exit 2
      fi
      case "$range" in
        *..*)
          label="range $range: committed only -- the working tree is not read"
          ;;
        *)
          if [ -n "$(git -C "$REPO_ROOT" rev-parse --verify --quiet "${range}^{commit}" 2>/dev/null || true)" ]; then
            label="single rev $range: against the working tree -- uncommitted tracked changes are read"
          else
            label="path $range: unstaged changes to that path only -- history and the index are not read"
          fi
          ;;
      esac
      ;;
  esac

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
  # "Reachable" is not enough, and the gap is exactly one rule wide.  The two
  # `all` rules fire only for tools/gate.sh and CMakeLists.txt, so a stage that
  # nothing but `all` reaches is reachable on paper and excused in practice for
  # every other change there is -- which is the defect this loop exists to name,
  # wearing the answer as a disguise.  So each stage must be reached by a rule
  # OF ITS OWN; `all` alone is exit 2.  Measured, not assumed: with a stage
  # registered and only `all` reaching it, --required-for answers a docs-only
  # change without it and exits 0.
  local reach own
  for path in "${STAGE_WANTED[@]}"; do
    reach=0
    own=0
    for sel in "${REQUIRED_SCOPES[@]}"; do
      sels="${sel#*|}"
      [ "$sels" = "-" ] && continue
      if [ "$sels" = "all" ]; then reach=1; continue; fi
      for s in $sels; do
        if [ "$s" = "$path" ]; then reach=1; own=1; break; fi
      done
    done
    if [ "$own" -eq 0 ]; then
      while IFS=$'\t' read -r s _p; do
        [ "$s" = "$path" ] && { reach=1; own=1; break; }
      done <<<"$DERIVED_EDGES"
    fi
    [ "$path" = "text-integrity" ] && { reach=1; own=1; }
    if [ "$reach" -eq 0 ]; then
      echo "gate: --required-for: stage '$path' is registered, but no rule can reach it." >&2
      echo "gate: a stage no rule can require is a stage every change silently excuses." >&2
      exit 2
    fi
    if [ "$own" -eq 0 ]; then
      echo "gate: --required-for: stage '$path' is registered, but only the 'all' rules reach it." >&2
      echo "gate: an 'all' rule fires only for tools/gate.sh and CMakeLists.txt, so every other" >&2
      echo "gate: change excuses this stage in silence; name a subtree its subject reads." >&2
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
  echo "required: $n stage(s)  [$label]"
}

# --required-for answers a question; it does not run the gate, so it returns
# before the first stage starts.  The flag having been given is what counts, not
# the answer being non-empty: `--required-for` with no argument must refuse, not
# fall through to a full gate run.
if [ "$REQUIRED_FOR_SET" -eq 1 ]; then
  required_for "$REQUIRED_FOR"
  exit 0
fi

# `build/` is a shared writable resource, not a parameter.  Two gate.sh runs
# share one build tree, one CTest registry, and one set of stage logs, so one
# run's ledger can be edited by another: measured here, a second run deletes the
# first run's kept log -- the file its verdict line names -- and a stage that
# binds a fixed port has been red because a server on this machine already held
# it.  A red like that says nothing about the tree it claims to measure, so
# refuse instead of racing: the refusal names the directory, and two callers can
# see they asked for the same one.
GATE_LOCK="$BUILD_DIR/.gate.lock"
mkdir -p "$BUILD_DIR" || { echo "gate: cannot create BUILD_DIR $BUILD_DIR" >&2; exit 2; }
if ! command -v flock >/dev/null 2>&1; then
  echo "gate: flock is not on PATH; refusing to measure $BUILD_DIR with no lock." >&2
  echo "gate: install util-linux, or point BUILD_DIR at a directory only this run uses." >&2
  exit 2
fi
exec 9>"$GATE_LOCK" || { echo "gate: cannot open $GATE_LOCK" >&2; exit 2; }
if ! flock -n 9; then
  echo "gate: another gate is running against $BUILD_DIR; refusing to measure a shared directory." >&2
  exit 2
fi
# The stage logs are the ledger of THIS run: a path printed by a judgment line
# has to name a file this run wrote, so the previous run's leftovers are removed
# here rather than left to be mistaken for this run's evidence.  A run that fails
# leaves its logs behind; the next run against this BUILD_DIR replaces them.
rm -f "$BUILD_DIR"/.gate-stage-*.log

# The work tree is a shared thing this script cannot lock: in a check-out several
# lanes can reach, HEAD is a shared variable, and a lane that runs
# `git checkout --detach origin/main` mid-run leaves the working tree CLEAN -- so
# `git status` cannot see what happened, and a twelve-minute run comes out with no
# subject at all.  Measured: a gate was in flight when another lane checked out a
# different commit in that same tree; `git push origin HEAD:main` then answered
# "Everything up-to-date" (HEAD no longer pointed at the commit being pushed, and
# it was recovered from the reflog), and the run's numbers belonged to no tree
# that anyone could name.  The lock answers "two readers share this directory";
# this answers "the tree I am measuring was swapped under me".  Different subject,
# different sentence, different exit -- so the run stamps the tree it starts on.
stage_fences() {
  # The first stage in this file whose verdict comes from a WORD the tool printed
  # rather than from its exit status.  tools/check_md_fences.py's rc counts
  # findings of any kind -- including the escaped backticks that are reported and
  # are not defects -- so on a clean tree it exited 1 while its own verdict said
  # PASS.  Reading rc here would re-introduce exactly that: a stage that is red
  # about a tree the checker called clean.  The word had to become its own name
  # before it got a reader, which is why the tool landed first (a3f6914) and this
  # stage second.
  #
  # Both components are printed whether the stage passes or fails, and one at a
  # time: a log that says "fences failed" makes the reader open the tool; a log
  # that says "odd=0 swallowed=1" does not.  The escaped counters are named in the
  # failure text only to say they are NOT the criterion.
  local out rc vline verdict odd swallowed
  rc=0
  # GATE_FENCES_ROOT exists so the two directions can be shown without editing the
  # tree this stage is about: a copy carrying only escaped backticks must be green,
  # and the same copy plus one unclosed fence must be red with odd=1.
  out="$(python3 "$REPO_ROOT/tools/check_md_fences.py" --root "${GATE_FENCES_ROOT:-$REPO_ROOT}" 2>&1)" || rc=$?
  printf '%s\n' "$out"
  vline="$(printf '%s\n' "$out" | grep '^FENCES-VERDICT|' | tail -1)"
  if [ -z "$vline" ]; then
    echo "gate: fences: the tool printed no FENCES-VERDICT line (rc=$rc)." >&2
    echo "gate: fences: there is no word to read, so this stage refuses instead of reading the status." >&2
    return 2
  fi
  verdict="$(printf '%s\n' "$vline" | sed -n 's/.*|verdict=\([A-Za-z]*\)$/\1/p')"
  odd="$(printf '%s\n' "$vline" | sed -n 's/.*|odd=\([0-9]*\)|.*/\1/p')"
  swallowed="$(printf '%s\n' "$vline" | sed -n 's/.*|swallowed=\([0-9]*\)|.*/\1/p')"
  if [ "$verdict" != "PASS" ]; then
    echo "gate: fences: verdict=$verdict odd=$odd swallowed=$swallowed" >&2
    echo "gate: fences: the verdict line was: $vline" >&2
    echo "gate: fences: odd and swallowed are the criterion; escaped backtick occurrences are reported and do not fail this stage." >&2
    echo "gate: fences: the tool's exit status was rc=$rc; it was read and printed here, and it did not decide this stage." >&2
    return 1
  fi
  echo "gate: fences: verdict=PASS odd=$odd swallowed=$swallowed; decided by the verdict word, not by the exit status (rc=$rc was read and printed)."
}

stage_shared_tmp() {
  # The sibling of stage_test_ports: a suite that binds a reserved port races
  # another suite; a suite that builds an artifact at one path shared by every
  # process races everything, including itself.  Measured on 2026-10-30 with
  # three worktrees at once: one run died with PermissionError [Errno 13] on
  # the shared harness, another's corpus control silently passed on the
  # overwritten binary.
  #
  # Like stage_fences, this stage reads a WORD the tool printed, not the
  # tool's exit status.  The status is read and printed, but it does not
  # decide: the tool's rc says "there is something to report", and the report
  # is what says whether the something is a defect.  A stage that decided on
  # rc would be red about a tree the checker called clean -- exactly what
  # stage_fences stopped doing.
  #
  # GATE_SHARED_TMP_ROOT exists so the two directions can be shown without
  # editing the tree this stage is about: this tree must be green, and a copy
  # carrying one shared path must be red with failures=1.
  local out rc vline verdict scanned failures
  rc=0
  out="$(python3 "$REPO_ROOT/tools/check_shared_tmp.py" --root "${GATE_SHARED_TMP_ROOT:-$REPO_ROOT}" 2>&1)" || rc=$?
  printf '%s\n' "$out"
  vline="$(printf '%s\n' "$out" | grep '^SHARED-TMP-VERDICT|' | tail -1)"
  if [ -z "$vline" ]; then
    echo "gate: shared-tmp: the tool printed no SHARED-TMP-VERDICT line (rc=$rc)." >&2
    echo "gate: shared-tmp: there is no word to read, so this stage refuses instead of reading the status." >&2
    return 2
  fi
  verdict="$(printf '%s\n' "$vline" | sed -n 's/.*|verdict=\([A-Za-z]*\)$/\1/p')"
  scanned="$(printf '%s\n' "$vline" | sed -n 's/.*|scanned=\([0-9]*\)|.*/\1/p')"
  failures="$(printf '%s\n' "$vline" | sed -n 's/.*|failures=\([0-9]*\)|.*/\1/p')"
  if [ "$verdict" != "PASS" ]; then
    echo "gate: shared-tmp: verdict=$verdict scanned=$scanned failures=$failures" >&2
    echo "gate: shared-tmp: the verdict line was: $vline" >&2
    echo "gate: shared-tmp: every artifact a suite builds must land in a directory that suite created (tempfile.mkdtemp)." >&2
    echo "gate: shared-tmp: the tool's exit status was rc=$rc; it was read and printed here, and it did not decide this stage." >&2
    return 1
  fi
  echo "gate: shared-tmp: verdict=PASS scanned=$scanned failures=$failures; decided by the verdict word, not by the exit status (rc=$rc was read and printed)."
}

GATE_START_HEAD="$(git rev-parse HEAD 2>/dev/null || true)"
GATE_WORKTREE="$(git rev-parse --show-toplevel 2>/dev/null || true)"
# The stamp carries the moment as well as the subject, because "was this run
# before or after that window" is a question a sha cannot answer: a reader would
# have to leave the log and run date -u and git reflog themselves.  UTC, to the
# second, with a Z -- two stamps then compare as strings, and no reader has to
# convert a time zone in their head.  The moment is NOT a criterion: start vs end
# is still decided by the sha alone.  Judging anything on the clock would let a
# system clock jump become a new intermittent red, and intermittent red is exactly
# what the sha comparison was added to end.
GATE_START_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "GATE|start|head=${GATE_START_HEAD:-<not a git tree>}|worktree=${GATE_WORKTREE:-<unknown>}|at=$GATE_START_AT"
# The ctest row s label is written here, not declared in STAGE_SPECS: registered
# is a property of the build tree, readable only after configure/build ran, and it
# must sit on the same line as expect -- a reader has to be able to see the two
# counts disagree without leaving the log.  The skip count is absent on purpose:
# it is a property of the run, so stage_ctest reads it from its own output and
# prints "gate: ctest reported N skipped test(s)"; a literal 0 here would look
# like the two measurements beside it and reach nothing.  stage_ctest asserts the
# summary count too; this is the other source, printed not asserted, because a
# doc quotes that assert comment verbatim and prose wins over a count.
for _spec in "${STAGE_SPECS[@]}"; do
  _sel="${_spec%%|*}"; _rest="${_spec#*|}"
  _label="${_rest%%|*}"
  if [ "$_sel" = "ctest" ]; then
    CTEST_REGISTERED="$(ctest --test-dir "$BUILD_DIR" -N 2>/dev/null | sed -n 's/^ *Total Tests: *//p' | tail -1)"
    _label="ctest (expect ${EXP_CTEST}, registered ${CTEST_REGISTERED:-<unreadable>}, skipped n/a)"
  fi
  run_stage "$_label" "$_sel" "${_rest#*|}"
done

# The contention margin of the tests RUN_SERIAL protects.  Printed, never asserted:
# tools/gate_attr_readings.py recomputes it from the TIMEOUT this build tree declares
# and from tools/gate_attrs.recorded.json, so raising the limit moves the margin
# instead of leaving a number behind that reads as designed slack.  The recorded
# file names the load and the tree each time was measured on (the same test alone
# takes 4.90s, which is the point of printing the load beside the margin).
python3 "$REPO_ROOT/tools/gate_attr_readings.py" --test-dir "$BUILD_DIR"

# Re-read the same subject at the end.  A mismatch is not a failing stage and not
# a test result: it says the numbers above are about more than one tree, so there
# is nothing to attribute and no stage conclusion is printed at all.  Refusing to
# re-run on its own is deliberate -- whether to spend another twelve minutes is a
# human decision, and a gate that silently re-measured would hide how often this
# happens.
#
# What the two moments can and cannot settle.  They place this run in time, so a
# reader holding an accident window can say "this reading began after it" without
# leaving the log.  They add no criterion, and they do not turn the log into a
# proof: git reflog records HEAD moving, so it holds no line for
# git checkout -- <path>, which rewrites files and leaves HEAD where it was.  A
# clean reflog therefore covers exactly one kind of accident -- the tree HEAD
# swapped under a run -- and says nothing about a file edited in place; for that
# kind the evidence is worktree= plus a before/after digest, not the reflog.  A
# reader who takes a clean reflog to mean "the reading is clean" is reading it one
# accident too broadly.
GATE_END_HEAD="$(git rev-parse HEAD 2>/dev/null || true)"
GATE_END_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "GATE|end|head=${GATE_END_HEAD:-<not a git tree>}|at=$GATE_END_AT"
if [ -n "$GATE_START_HEAD" ] && [ "$GATE_START_HEAD" != "$GATE_END_HEAD" ]; then
  echo "gate: FAILED — the tree changed while this run was measuring it (start $GATE_START_HEAD -> end $GATE_END_HEAD); nothing here is attributable."
  echo "gate: no stage result is printed: a stage result names a tree, and this run measured more than one."
  echo "gate: not re-running automatically -- whether to measure again is a human decision."
  exit 2
fi

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
  # The verdict names its own subjects.  FAILED is a boolean and the table above
  # is what a paste cuts off, so a run that says only "FAILED" hands the reader a
  # question ("which stage?") that was already answered on disk.  An empty list
  # here is the ledger disagreeing with the verdict -- refuse rather than print a
  # sentence with a hole in it.
  if [ "${#FAILED_STAGE_NAMES[@]}" -eq 0 ]; then
    echo "gate: internal: FAILED is set but no stage is named; the ledger and the verdict disagree." >&2
    exit 2
  fi
  _names=""
  for _n in "${FAILED_STAGE_NAMES[@]}"; do _names="${_names:+$_names, }$_n"; done
  echo "gate: failed stage(s): $_names"
  echo "gate: each failed stage's log is kept at $BUILD_DIR/.gate-stage-<name>.log"
  _kept=""
  for _k in "${FAILED_STAGE_LOGS[@]}"; do _kept="${_kept:+$_kept }$_k"; done
  echo "gate: kept: $_kept"
  exit 1
fi
if [ -n "$ONLY" ]; then
  echo "gate: OK — the selected stage passed (--only $ONLY)."
  echo "gate: this was NOT the full gate: ${#STAGE_WANTED[@]} stages are registered and only this one ran."
else
  echo "gate: OK — every stage passed (${#STAGE_NAMES[@]}/${#STAGE_WANTED[@]} stages ran)."
fi
exit 0
