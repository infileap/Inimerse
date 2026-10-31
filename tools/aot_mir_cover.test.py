#!/usr/bin/env python3
"""Coverage of the MIR route, measured against the AST route's own denominator.

aot_cover pins what the first front end's corpus makes the interpreter execute.
This script asks the matching question of the second front end -- the C that
aot_mir lowers out of the MIR -- on the corpus the first one accepts, and it
refuses to answer with a number it did not measure:

  ACCEPTED      every inline case translates (aot-mir translate, exit 0).  A
                refusal is printed by name (IR-AOT-MIR-REFUSED carries the
                tool's own why= and its not-yet count) and makes the run red,
                because a silent refusal reads as coverage.
  EQUIVALENT    the C the MIR route emitted compiles and prints the same bytes
                and exits the same way as the interpreter on the same program.
  SUPERSET      for every case, the opcodes the interpreter executed (its own
                OP-HOT census, one run in a cwd with no params.params) are a
                subset of the opcodes present in the MIR stream that was
                lowered.  A program whose execution leaves the stream that was
                lowered is the one failure the equality above cannot see: it
                compares answers, not the work behind them.
  DENOMINATOR   the denominator is not written down here.  It is read from
                tools/aot_cover.pin.json:covered_ops -- the first front end's
                own answer -- so the two cannot drift apart silently, and the
                two pins are never added together (coverage-claim=none).

Measurement basis: an opcode counts as covered by the MIR route when some
translated program's MIR stream contains it (the tool's own generated=(...)
list).  That is a statement about this corpus, not about the lowering: the
lowering's emittable set is asked of the tool separately, so an opcode missing
from the corpus is named with which of the two it is.

Readings:
  IR-AOT-MIR|accepted=N|refused=0|same-as-interpreter=N|of=M|corpus=...
  IR-AOT-MIR-COVER|covered=N|of=M|denominator=...|basis=...|coverage-claim=none
  IR-AOT-MIR-OPS|covered=(OP_A,OP_B,...)
  IR-AOT-MIR-MISSING|op=OP_X|why=not-emittable-by-this-lowering|emittable-but-not-exercised-by-the-corpus
  IR-AOT-MIR-REFUSED|prog=NAME|why=...|n=K
  IR-AOT-MIR-DYN-MISS|prog=NAME|op=OP_X|executed=N|in-mir-stream=no
  IR-AOT-MIR-EXEC-DIFF|prog=NAME|interp-rc=..|mir-rc=..|...
  IR-AOT-PRELUDE|file=params.params|found-at=cwd|contributed=OP_LOADK_STRING|excluded=yes|no
  IR-AOT-PRELUDE-CONTROL|cwd=with|executed=N|cwd=without|executed=M|delta=(..)|exact=yes|no
  IR-AOT-MIR-PIN|tree=..|head=..|of=..|covered=..|pinned-covered=..|match=yes|tree-moved=yes
  IR-AOT-MIR-VERDICT|PASS|FAIL|...

Usage:
    python3 tools/aot_mir_cover.test.py [--build build] [--pin] [--check-pin]
                                       [--limit N] [--self-check] [-v]
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# "Can a reader look this sha up?" is asked by every tool that pins a tree, and
# this file's own copy of the answer is now only used by --pin/--check-pin; the
# self-check asks the shared module, because a second copy of a predicate is a
# second thing to keep true.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pin_tree  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tools" / "aot_native.test.py"
AOT_PIN = ROOT / "tools" / "aot_cover.pin.json"
PIN = ROOT / "tools" / "aot_mir_cover.pin.json"

# The prelude text, and only the text: the control's expected opcode set is
# measured by running this program on its own (see prelude_control), because a
# set copied in here would stay "right" after the prelude changed and could not
# tell a prelude that grew from a delta that shrank.
PRELUDE_TEXT = 'speed = 42\ntitle = "hello"\n'

CODEGEN = re.compile(r"^IR-MIR-CODEGEN[|](.*)$")
CONSERVE = re.compile(r"^IR-MIR-CONSERVE[|](.*)$")
NOTYET = re.compile(r"^IR-MIR-NOT-YET[|]op=(OP_[A-Z0-9_]+)[|]n=(\d+)$")
REFUSED = re.compile(r"^IR-MIR-REFUSED[|]why=([^|]*)[|]detail=(.*)$")
SUBSET = re.compile(r"^IR-MIR-SUBSET[|]emits=(\d+)[|]ops=\((.*)\)$")
CASE_OP = re.compile(r"case (OP_[A-Z0-9_]+):")
# The real census line carries more fields than the two that matter here
# (op=, interp=, then aot=/wasm=/why=), so the tail is optional -- but the
# opcode and the count are not.  Validated against a captured line below.
HOT = re.compile(r"^OP-HOT[|]op=(OP_[A-Z0-9_]+)[|]interp=(\d+)(?:[|].*)?$")


def declared_ops():
    """The opcodes the encoding declares, read from the enum that declares them.

    Not a list kept here: tools/ir_spec.py already parses `typedef enum { ... }
    OpCode;` and counts 68 entries / 67 distinct names, and a second parser would
    be a second answer to the same question."""
    spec = importlib.util.spec_from_file_location("ir_spec", ROOT / "tools" / "ir_spec.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {name for name, _code in mod.declared_ops()}


def load_cases():
    spec = importlib.util.spec_from_file_location("aot_cases", CASES)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(mod)
    return mod


def head_sha():
    p = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                       capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else "<not a git tree>"


def run(cmd, cwd, env=None, timeout=300):
    try:
        d = subprocess.run([str(c) for c in cmd], cwd=str(cwd), env=env,
                           capture_output=True, text=True, timeout=timeout)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def parse_kv(line, pattern):
    m = pattern.match(line.strip())
    if not m:
        return None
    body = m.group(1) if m.groups() else ""
    return dict(kv.split("=", 1) for kv in body.split("|") if "=" in kv)


def parse_ops(text):
    """(OP_A,OP_B) -> ['OP_A', 'OP_B']."""
    text = text.strip()
    if text.startswith("(") and text.endswith(")"):
        text = text[1:-1]
    return [o for o in (p.strip() for p in text.split(",")) if o]


def hot(output):
    """The interpreter's own census, opcodes that ran at least once."""
    got = {}
    for raw in output.splitlines():
        m = HOT.match(raw.strip())
        if m:
            n = int(m.group(2))
            if n > 0:
                got[m.group(1)] = n
    return got


def ledger(of_ops, covered, missing):
    """The arithmetic every coverage number in this repository carries: the two
    buckets partition the denominator, reuse no opcode and lose none."""
    problems = []
    both = set(covered) & set(missing)
    if both:
        problems.append("opcode in both buckets: %s" % ",".join(sorted(both)))
    if len(set(covered)) != len(covered) or len(set(missing)) != len(missing):
        problems.append("duplicate opcode inside a bucket")
    if set(covered) | set(missing) != set(of_ops):
        problems.append("buckets do not reconstruct the denominator")
    if len(covered) + len(missing) != len(set(of_ops)):
        problems.append("covered + missing != of")
    return problems


def prelude_delta(with_foot, without_foot):
    return frozenset(op for op, n in with_foot.items() if n > without_foot.get(op, 0))


def function_body(text, signature):
    """The brace-delimited body of one function, by signature.

    Scoped on purpose: `case OP_*` also appears in the emitter and in this file,
    so a grep over the translation unit would compare a set with itself and call
    it agreement.  The two sets that matter are in two named bodies.
    """
    i = text.find(signature)
    if i < 0:
        return None
    j = text.find("{", i)
    if j < 0:
        return None
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return None


def case_ops(body):
    """The distinct opcodes a switch body names in `case OP_X:` labels."""
    return {m.group(1) for m in CASE_OP.finditer(body or "")}


def lowering_problems(table_ops, emitter_ops, of_ops, reported):
    """The two questions the lowering's own tables have to agree on.

    `table` is which opcodes the register-model switch claims to lower (op_regs),
    `emitter` is which ones the code generator actually has a body for, and
    `reported` is what the tool prints as its subset.  A case added to the emitter
    and not to the table makes the reading call a real lowering unable to emit, and
    the coverage ledger stays balanced while it happens -- which is precisely the
    failure this repository has already met twice.
    """
    problems = []
    if table_ops != emitter_ops:
        problems.append("the read table and the emitter disagree: table-only=%s emitter-only=%s"
                        % (",".join(sorted(table_ops - emitter_ops)) or "none",
                           ",".join(sorted(emitter_ops - table_ops)) or "none"))
    if reported != table_ops:
        problems.append("the tool reports %d opcodes but the table names %d"
                        % (len(reported), len(table_ops)))
    outside = table_ops - set(of_ops)
    if outside:
        problems.append("the table names opcodes the encoding does not declare: %s"
                        % ",".join(sorted(outside)))
    return problems



def self_check():
    """The ledger arithmetic and the two parsers, on inputs whose answer is
    known -- including inputs the ledger has to reject."""
    checks = [0]
    bad = []

    def expect(name, got, want):
        checks[0] += 1
        ok = got == want
        if not ok:
            bad.append("%s: want %r got %r" % (name, want, got))
        print("aot_mir_cover self-check: %s %s" % (name, "ok" if ok else "FAIL"))

    expect("ledger-clean", ledger(["OP_A", "OP_B"], ["OP_A"], ["OP_B"]), [])
    expect("ledger-overlap", len(ledger(["OP_A", "OP_B"], ["OP_A", "OP_B"], ["OP_B"])), 2)
    expect("ledger-sum", ledger(["OP_A", "OP_B"], ["OP_A"], []) != [], True)
    expect("ledger-foreign", ledger(["OP_A", "OP_B"], ["OP_A", "OP_Z"], ["OP_B"]) != [], True)
    # `case OP_*` is also in the emitter and in this file, so the two sets are
    # taken from named function bodies; a whole-file scan would compare a set
    # with itself and call that agreement.
    two = ("static int op_regs(int op) { switch (op) { case OP_A: return 1; "
           "case OP_B: return 1; default: return -1; } }\n"
           "static int emit_one(void) { switch (op) { case OP_A: return 0; "
           "case OP_B: return 0; default: return 1; } }")
    expect("the two sets are read out of their own bodies",
           (sorted(case_ops(function_body(two, "static int op_regs(int op)") )),
            sorted(case_ops(function_body(two, "static int emit_one(")))),
           (["OP_A", "OP_B"], ["OP_A", "OP_B"]))
    expect("agreement is silent",
           lowering_problems({"OP_A", "OP_B"}, {"OP_A", "OP_B"},
                             ["OP_A", "OP_B"], {"OP_A", "OP_B"}), [])
    expect("a case added to the emitter and not to the table is caught",
           len(lowering_problems({"OP_A", "OP_B"}, {"OP_A", "OP_B", "OP_C"},
                                 ["OP_A", "OP_B", "OP_C"], {"OP_A", "OP_B"})), 1)
    expect("a table entry the emitter never serves is caught",
           len(lowering_problems({"OP_A", "OP_B"}, {"OP_A"}, ["OP_A", "OP_B"],
                                 {"OP_A", "OP_B"})), 1)
    expect("a table entry the encoding does not declare is caught",
           len(lowering_problems({"OP_A", "OP_Z"}, {"OP_A", "OP_Z"},
                                 ["OP_A", "OP_Z"], {"OP_A"})), 1)
    expect("parse-ops", parse_ops("(OP_A,OP_B)"), ["OP_A", "OP_B"])
    real = ("OP-HOT|op=OP_EQ|interp=3|aot=na|wasm=na|why=aot flattens each op into inline C at "
            "build time, so a counter there would be counting a modified object; wasm executes "
            "inside the host engines process and this counter is not in it")
    expect("hot-real-line", hot(real), {"OP_EQ": 3})
    expect("hot-filters-zero",
           hot("OP-HOT|op=OP_A|interp=3\nOP-HOT|op=OP_B|interp=0\n"),
           {"OP_A": 3})
    expect("hot-ignores-total",
           hot("OP-HOT-TOTAL|fetches=48|rows=48|rows-match-fetches=yes\n"),
           {})
    expect("prelude-delta",
           sorted(prelude_delta({"OP_A": 2, "OP_B": 1}, {"OP_A": 1})), ["OP_A", "OP_B"])
    expect("prelude-delta-empty", list(prelude_delta({"OP_A": 1}, {"OP_A": 1})), [])
    expect("the prelude's opcode set is measured, not copied into this file",
           [ln for ln in Path(__file__).read_text().splitlines()
            if ln.startswith("PRELUDE_OPS")], [])
    head = head_sha()
    expect("a sha nobody can look up is not a pin", len(pin_tree.problems("0" * 40, ROOT)), 1)
    # The two cells below used to assert this about this checkout's own HEAD, and
    # that is an assertion about where the file happens to be running rather than
    # about the mechanism.  Between `git commit` and `git push` -- and in any
    # worktree left on a detached HEAD -- the commit a pin would record is held by
    # no ref yet, which is the ordinary state of the tree a gate is run on, so the
    # cells went red there and the gate could only go green *after* the push it was
    # supposed to gate (measured on 81ae27d: refs=0, both failed).  The fixture
    # builds the two states the assertion is actually about, in a repository of
    # its own: one commit a ref holds, one commit nothing holds.
    fx = pin_tree.fixture()
    expect("a sha a ref holds is a pin", pin_tree.problems(fx["held"], fx["root"]), [])
    expect("...and a sha no ref holds is not, however resolvable it is",
           len(pin_tree.problems(fx["dangling"], fx["root"])), 1)
    if bad:
        print("aot_mir_cover self-check: FAIL -- " + "; ".join(bad))
        return 1
    print("aot_mir_cover self-check: ok (%d checks)" % checks[0])
    return 0


def prelude_control(interp, verbose):
    """Same program, two cwds, one with the prelude file and one without.  The
    delta must be exactly the set of opcodes the prelude executes when it is run
    as a program of its own -- measured here, not copied into this file.  That is
    what makes 'excluded=yes' below a measurement instead of an assumption."""
    with_dir = Path(tempfile.mkdtemp(prefix="aot_mir_prelude_"))
    no_dir = Path(tempfile.mkdtemp(prefix="aot_mir_noprelude_"))
    (with_dir / "params.params").write_text(PRELUDE_TEXT)
    for d in (with_dir, no_dir):
        (d / "ctrl.im").write_text("say 1\n")
    # The same text in the clean directory, where nothing preloads anything:
    # this census is the prelude alone.
    (no_dir / "prelude.im").write_text(PRELUDE_TEXT)
    env = dict(os.environ, INIMERSE_HOT_OPS="1")
    env.pop("INIMERSE_IR_TABLE", None)
    _rc1, out1, _e1 = run([interp, "--no-mods", "run", with_dir / "ctrl.im"], with_dir, env)
    _rc2, out2, _e2 = run([interp, "--no-mods", "run", no_dir / "ctrl.im"], no_dir, env)
    _rc3, out3, _e3 = run([interp, "--no-mods", "run", no_dir / "prelude.im"], no_dir, env)
    f1, f2, alone = hot(out1), hot(out2), hot(out3)
    delta = prelude_delta(f1, f2)
    expected = frozenset(alone)
    exact = delta == expected
    print("IR-AOT-PRELUDE-CONTROL|cwd=with|executed=%d|cwd=without|executed=%d|delta=(%s)"
          "|expected-from=the-prelude-run-as-a-program|expected=(%s)|prelude-executed=%d|exact=%s"
          % (sum(f1.values()), sum(f2.values()), ",".join(sorted(delta)),
             ",".join(sorted(expected)), sum(alone.values()), "yes" if exact else "no"))
    if verbose:
        print("IR-AOT-PRELUDE-CONTROL-OPS|with=(%s)|without=(%s)|prelude-alone=(%s)"
              % (",".join("%s:%d" % (k, v) for k, v in sorted(f1.items())),
                 ",".join("%s:%d" % (k, v) for k, v in sorted(f2.items())),
                 ",".join("%s:%d" % (k, v) for k, v in sorted(alone.items()))))
    return exact


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(ROOT / "build"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--pin", action="store_true", help="write tools/aot_mir_cover.pin.json")
    ap.add_argument("--check-pin", action="store_true",
                    help="require the numbers to equal the pinned ones")
    ap.add_argument("--limit", type=int, default=0, help="stop after N cases (0 = all)")
    ap.add_argument("--self-check", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_check:
        return self_check()

    # --build may arrive relative (a hand run from the repository root, or a
    # test that passes "build"), and the corpus runs in a private cwd: resolving
    # it there would make every tool look missing.  Resolve it here, where ROOT
    # is known.
    build = (ROOT / args.build) if not os.path.isabs(args.build) else Path(args.build)
    interp = build / "inimerse"
    mir_tool = build / "aot-mir"
    for tool in (interp, mir_tool):
        if not tool.exists():
            print("aot_mir_cover: REFUSED|missing %s -- build it first" % tool, file=sys.stderr)
            return 2
    if not AOT_PIN.exists():
        print("aot_mir_cover: REFUSED|missing %s -- the denominator is read from it"
              % AOT_PIN, file=sys.stderr)
        return 2

    aot = json.loads(AOT_PIN.read_text())
    aot_covered = list(aot["covered_ops"])

    rc, out, err = run([mir_tool, "subset"], ROOT)
    emittable = None
    for raw in out.splitlines():
        m = SUBSET.match(raw.strip())
        if m:
            emittable = set(parse_ops(m.group(2)))
    if rc != 0 or emittable is None:
        print("aot_mir_cover: REFUSED|the tool does not report its emittable set "
              "(IR-MIR-SUBSET): %s" % (err or out).strip()[:200], file=sys.stderr)
        return 2

    mod = load_cases()
    cases = [(n, src) for n, src, _want in mod.EQUIVALENCE]
    if args.limit:
        cases = cases[:args.limit]

    workdir = Path(tempfile.mkdtemp(prefix="aot_mir_cover_"))
    prelude_excluded = not (workdir / "params.params").exists()
    control_exact = prelude_control(interp, args.verbose)
    print("IR-AOT-PRELUDE|file=params.params|found-at=cwd|contributed=OP_LOADK_STRING|excluded=%s"
          % ("yes" if (prelude_excluded and control_exact) else "no"))

    # Two interpreter runs per case, deliberately: the census writes its rows to
    # stdout, so the oracle has to be a run that was not asked for one.  Filtering
    # the rows out would be comparing a program against a modified program.
    clean = dict(os.environ)
    clean.pop("INIMERSE_HOT_OPS", None)
    clean.pop("INIMERSE_IR_TABLE", None)
    env = dict(clean, INIMERSE_HOT_OPS="1")

    accepted = refused = equivalent = 0
    corpus_ops = set()
    problems = []
    for name, src in cases:
        s = workdir / (name + ".im")
        s.write_text(src)
        cfile = workdir / (name + ".c")
        exe = workdir / (name + ".exe")
        rc_i, out_i, _e = run([interp, "--no-mods", "run", s], workdir, clean)
        _rc_h, out_h, _e_h = run([interp, "--no-mods", "run", s], workdir, env)
        dyn = hot(out_h)
        rc_t, out_t, err_t = run([mir_tool, "translate", s, cfile], workdir)
        if rc_t != 0:
            refused += 1
            why = "-"
            n = 0
            for raw in out_t.splitlines():
                mr = REFUSED.match(raw.strip())
                if mr:
                    why = mr.group(1)
                mn = NOTYET.match(raw.strip())
                if mn:
                    n += int(mn.group(2))
            print("IR-AOT-MIR-REFUSED|prog=%s|why=%s|n=%d" % (name, why, n))
            continue
        accepted += 1
        gen = None
        for raw in out_t.splitlines():
            kv = parse_kv(raw, CODEGEN)
            if kv:
                gen = kv
        if not gen:
            problems.append("%s: translate succeeded without IR-MIR-CODEGEN" % name)
            continue
        emitted = set(parse_ops(gen.get("generated", "()")))
        corpus_ops |= emitted
        for op, count in sorted(dyn.items()):
            if op not in emitted:
                problems.append("%s: the interpreter executed %s %d time(s) but the MIR stream "
                                "that was lowered does not contain it" % (name, op, count))
                print("IR-AOT-MIR-DYN-MISS|prog=%s|op=%s|executed=%d|in-mir-stream=no" % (name, op, count))
        cons = None
        for raw in out_t.splitlines():
            cons = parse_kv(raw, CONSERVE) or cons
        if not cons or cons.get("holds") != "yes":
            problems.append("%s: IR-MIR-CONSERVE does not hold: %s" % (name, cons))
        rc_c, _o, ec = run([args.cc, "-O2", "-o", exe, cfile], workdir)
        if rc_c != 0:
            problems.append("%s: the generated C did not compile: %s" % (name, (ec or "")[:200]))
            continue
        rc_r, out_r, _e2 = run([exe], workdir)
        if out_r == out_i and rc_r == rc_i:
            equivalent += 1
        else:
            problems.append("%s: answers differ (interp rc=%s out=%r / mir rc=%s out=%r)"
                            % (name, rc_i, out_i, rc_r, out_r))
            print("IR-AOT-MIR-EXEC-DIFF|prog=%s|interp-rc=%s|mir-rc=%s|interp-out=%r|mir-out=%r"
                  % (name, rc_i, rc_r, out_i, out_r))
        if args.verbose:
            print("IR-AOT-MIR-CASE|prog=%s|accepted=yes|dyn=%d|emitted=%d|superset=yes"
                  % (name, len(dyn), len(emitted)))

    covered = sorted(o for o in aot_covered if o in corpus_ops)
    missing = sorted(o for o in aot_covered if o not in corpus_ops)
    for op in missing:
        why = ("not-emittable-by-this-lowering" if op not in emittable
               else "emittable-but-not-exercised-by-the-corpus")
        print("IR-AOT-MIR-MISSING|op=%s|why=%s" % (op, why))
    problems.extend(ledger(aot_covered, covered, missing))


    print("IR-AOT-MIR|accepted=%d|refused=%d|same-as-interpreter=%d|of=%d|emittable=%d"
          "|corpus=inline-cases:%d|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none"
          % (accepted, refused, equivalent, len(aot_covered), len(emittable), len(cases)))
    print("IR-AOT-MIR-COVER|covered=%d|of=%d|denominator=tools/aot_cover.pin.json:covered_ops"
          "|coverage-claim=none" % (len(covered), len(aot_covered)))

    # `of` above is the DYNAMIC denominator borrowed from another question: the
    # opcodes this corpus executed.  The static one -- of all 67 opcodes the
    # encoding declares, how many this lowering can lower at all -- is a different
    # number, and printing only the first reads as 100% of the second.  Both are
    # printed, each with its basis, and the two tables that must agree are asked.
    src = (ROOT / "src" / "compilation" / "aot_mir.c").read_text()
    table_ops = case_ops(function_body(src, "static int op_regs(int op)"))
    emitter_ops = case_ops(function_body(src, "static int emit_one("))
    of_ops = declared_ops()
    lowering = lowering_problems(table_ops, emitter_ops, of_ops, emittable)
    problems.extend(lowering)
    print("IR-AOT-MIR-LOWERING|handles=%d|of=%d|unknown=%d"
          "|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one"
          "|table=%d|emitter=%d|reported=%d|agree=%s"
          % (len(table_ops), len(of_ops), len(of_ops) - len(table_ops),
             len(table_ops), len(emitter_ops), len(emittable),
             "yes" if not lowering else "no"))
    # `missing` is scoped to the pin's covered set, and after a corpus change it
    # can legitimately be empty while the lowering still cannot emit most of the
    # encoding.  An empty list printed alone reads as "nothing is missing"; the
    # denominators ride beside it so it cannot be read that way.
    print("IR-AOT-MIR-MISSING-SCOPE|of=%d|scope=the-opcodes-tools/aot_cover.pin.json"
          "-says-the-AST-route-executed|not-the-%d-the-encoding-declares"
          "|lowering-unknown=%d"
          % (len(aot_covered), len(of_ops), len(of_ops) - len(table_ops)))
    print("IR-AOT-MIR-OPS|covered=(%s)" % ",".join(covered))

    live = {
        "tree": head_sha(),
        "covered": len(covered),
        "of": len(aot_covered),
        "of-source": "tools/aot_cover.pin.json:covered_ops (read, not copied: the two routes "
                     "must not drift apart, and the two pins are never added)",
        "corpus": {"inline_cases": len(cases), "accepted": accepted, "refused": refused,
                   "same-stdout-and-rc-as-interpreter": equivalent},
        "covered_ops": covered,
        "missing_ops": missing,
        "coverage-claim": "none",
        "basis": "mir-emitted-ops-union-over-the-corpus",
        "note": [
            "regenerate with tools/aot_mir_cover.test.py --pin.  The fraction a",
            "reader sees in IR-AOT-MIR is this file.  coverage-claim=none says",
            "what the number is not: covering the corpus is not covering the",
            "lowering, which is why emittable= and the missing reasons are",
            "recorded next to it, and why the denominator is read from the AST",
            "route's pin instead of being written down twice.",
        ],
    }

    # A pin written from a red run would freeze the defect as the expectation.
    green = (accepted == len(cases) and refused == 0 and equivalent == accepted
             and not problems and prelude_excluded and control_exact
             and len(covered) + len(missing) == len(aot_covered))
    pin_ok = True
    if args.pin:
        if not green:
            pin_ok = False
            print("IR-AOT-MIR-PIN|wrote=no|why=the run is not green, and a pin written from a red "
                  "run would freeze the defect as the expectation")
        else:
            PIN.write_text(json.dumps(live, indent=2) + "\n")
            print("IR-AOT-MIR-PIN|wrote=%s|covered=%d|of=%d" % (PIN.relative_to(ROOT), len(covered),
                                                               len(aot_covered)))
            wrote_problems = pin_tree.problems(live["tree"], ROOT)
            problems.extend(wrote_problems)
            if wrote_problems:
                pin_ok = False
            print(pin_tree.reading("IR-AOT-MIR-PIN-TREE", live["tree"], ROOT, head=live["tree"]))
    elif PIN.exists():
        old = json.loads(PIN.read_text())
        same = (old.get("covered") == live["covered"] and old.get("of") == live["of"]
                and old.get("covered_ops") == live["covered_ops"]
                and old.get("missing_ops") == live["missing_ops"])
        pin_ok = same or not args.check_pin
        print("IR-AOT-MIR-PIN|tree=%s|head=%s|of=%s|covered=%s|pinned-covered=%s|match=%s|tree-moved=%s"
              % (old.get("tree"), live["tree"], live["of"], live["covered"], old.get("covered"),
                 "yes" if same else "no",
                 "yes" if old.get("tree") != live["tree"] else "no"))
        print(pin_tree.reading("IR-AOT-MIR-PIN-TREE", old.get("tree"), ROOT, head=live["tree"]))
        tree_problems = pin_tree.problems(old.get("tree"), ROOT)
        problems.extend(tree_problems)
        if tree_problems:
            # A pin a reader cannot look up is not a holding pin, whatever the
            # numbers in it say.  --check-pin is the mode that promises the pin
            # is usable, so this fails it there rather than printing a note.
            pin_ok = False
        if not same:
            problems.append("the live coverage does not match %s -- re-pin deliberately with --pin "
                            "if the change is intended" % PIN.relative_to(ROOT))
    else:
        print("IR-AOT-MIR-PIN|tree=-|head=%s|of=%d|covered=%d|pinned-covered=-|match=-|tree-moved=-"
              % (live["tree"], live["of"], live["covered"]))
        if args.check_pin:
            pin_ok = False
            problems.append("%s is missing and --check-pin was asked for: an absent expectation "
                            "is not a passing one" % PIN.relative_to(ROOT))

    for line in problems:
        print("IR-AOT-MIR-PROBLEM|" + line)
    ok = green and pin_ok
    print("IR-AOT-MIR-VERDICT|%s|tree=%s|accepted=%d|of=%d|covered=%d|missing=%d|refused=%d"
          "|same-as-interpreter=%d|prelude-excluded=%s"
          % ("PASS" if ok else "FAIL", live["tree"], accepted, live["of"], len(covered),
             len(missing), refused, equivalent, "yes" if prelude_excluded else "no"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
