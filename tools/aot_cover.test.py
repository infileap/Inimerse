#!/usr/bin/env python3
"""How much of the opcode set has the AOT path been *shown* to compute?

The question is not "what does aot_native.c implement".  A list read out of the
translator source is a claim, and a claim can be right about a construct no
program ever exercises.  The question is: over programs the translator accepts,
which opcodes did the interpreter actually execute, and did the native binary
print the same bytes?

Two filters, in this order:

  1. the translator accepted the program -- a refusal is not coverage, nothing ran;
  2. native stdout AND exit status equal the interpreter byte for byte.

Only then is the footprint counted, and the footprint comes from INIMERSE_HOT_OPS
on the interpreter, which counts what executed rather than what could execute.
A construct the translator can lower but no accepted program runs lands in
IR-AOT-MISSING.  OP_OR is why that matters: "say (a > 0) or (b > 0)" translates
fine today and is still missing from the 86 in-tree cases.

The corpus is printed with both of its halves.  No repository program is inside
the subset right now (every tracked .im and .inim is refused), so a fraction
printed alone would read as coverage of the repository.  Why each one was
declined is not a sentence written down in this file: the histogram beside the
fraction is decoded from the translator's own stderr, one bucket per distinct
cause, and the buckets are asserted to account for every refusal.  A cause that
belongs to the frontend is counted under its own stage -- a program the parser
refused never reached the backend, and printing it as a numeric-subset refusal
would credit the backend with a decision it did not make.  The MIR route, when
it becomes a route of its own, gets its own reading (IR-AOT-MIR): it is a second
translator, not another bucket of this one.

"Over programs the translator accepts" is the whole basis, and the interpreter
has a prelude: it loads params.params out of the *current directory* before it
runs the program it was asked to run (src/main.c:286, then src/vm/vm.c:5129
compiles that file and vm_run()s it), in the same VM, sharing the opcode census.
The repository root has a tracked params.params, so a census taken there
answered "what did this directory's files execute".  It reported 30 opcodes over
the 86 inline cases; the same corpus in a directory with no params.params
reports 29, and the one that differed, OP_LOADK_STRING, cannot come from an
accepted program at all (the accepted subset is int/float/bool, so a program
with a string literal is refused).  The interpreter therefore runs in a private
directory that holds no params.params, and IR-AOT-PRELUDE states what was
excluded while IR-AOT-PRELUDE-CTL measures that the exclusion happened -- an
exclusion asserted instead of measured is the same defect one layer up.

Readings:
  IR-AOT-COVER|corpus=inline-cases:N,repo:A/B|repo-rejected=R
              |repo-rejected-stages=frontend-parse:F,numeric-subset:B
              |repo-rejected-causes=K|repo-rejected-unparsed=U
              |accepted=N|refused=N|differ=N
              |declared-diverge=K
              |covered=C|of=67|basis=opcodes-executed-by-the-interpreter-over-programs-the-translator-accepts
  IR-AOT-PRELUDE|file=params.params|present=yes|no|own-opcodes=...|n=N
                |excluded=yes|no|cwd=private
  IR-AOT-PRELUDE-CTL|probe=PROGRAM|at-root=...|at-private=...|diff=...
                    |own=...|verdict=PASS|FAIL
  IR-AOT-OPCODE|op=OP_X|seen-in=N|verdict=covered|missing
  IR-AOT-MISSING|ops=...|count=N|because=no-accepted-program-executes-them
  IR-AOT-DIFF|case=NAME|first-diff=stdout|exit|cc|footprint|detail=...
  IR-AOT-REFUSED|case=NAME|why=...
  IR-AOT-REPO-REJECTED|n=N|stage=frontend|numeric-subset|unparsed|why=cause

Exit 0 when every accepted program prints the same bytes; 1 when one differs
(named, because a silent divergence is the one thing this path must never ship);
2 when a prerequisite is missing -- including a "private" directory that turned
out to be running a prelude after all.  A refusal is never a failure: it is the
translator declining, counted and reported.

--self-check runs the decoder and the completeness assertion against messages
the translator does and does not emit, where the second kind is the point of
running it at all; it needs no build and measures no corpus.
"""
import argparse
import atexit
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tools" / "aot_native.test.py"
PIN = ROOT / "tools" / "aot_cover.pin.json"
AST_H = ROOT / "src" / "parser" / "ast.h"
OP_HOT = re.compile(r"^OP-HOT[|]op=(OP_[A-Z0-9_]+)[|]interp=([0-9]+)[|]")

OPCODES = """OP_MOV OP_LOADK_INT OP_LOADK_FLOAT OP_LOADK_STRING OP_LOADK_BOOL
OP_ADD OP_SUB OP_MUL OP_DIV OP_NEG OP_EQ OP_NEQ OP_LT OP_GT OP_LE OP_GE
OP_AND OP_OR OP_NOT OP_NEW_ARRAY OP_INDEX_GET OP_INDEX_SET OP_LOAD_GLOBAL
OP_STORE_GLOBAL OP_JUMP OP_JUMP_IF_FALSE OP_JUMP_IF_TRUE OP_CALL_BUILTIN
OP_PUSH_REG OP_SAY OP_WAIT OP_STOP OP_HALT OP_CALL_FUNC OP_RETURN OP_IS_NIL
OP_THREAD_START OP_THREAD_CTRL OP_THREAD_JOIN OP_THREAD_WAIT OP_THREAD_STATE
OP_LOCK OP_SEND OP_RECV OP_NEW_DICT OP_EQK OP_NEQK OP_DECLARE OP_RECORD OP_MOD
OP_NEW_SET OP_SET_INTERVAL OP_IN OP_MIN OP_MAX OP_BIND OP_TRY_START OP_THROW
OP_SET_ADD OP_THREAD_GOTO OP_CONCAT OP_YIELD OP_MAKE_FUNC OP_TRY_END
OP_CALL_VALUE OP_LOAD_CAPTURE OP_LOADK_I64""".split()


def refuse(why):
    print("aot_cover: REFUSED|" + why, file=sys.stderr)
    print("aot_cover: nothing was measured", file=sys.stderr)
    sys.exit(2)


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
        d = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True,
                           env=env, timeout=timeout)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


_CWD = None


def private_cwd():
    """A directory that holds no params.params, so only the program runs.

    The interpreter loads params.params from the current directory before the
    program it was asked to run (src/main.c:286), compiles it and vm_run()s it
    in the same VM (src/vm/vm.c:5129), so its opcodes share the same census.
    The repository root has a tracked params.params; running there measures the
    directory, not the program.
    """
    global _CWD
    if _CWD is None:
        _CWD = tempfile.mkdtemp(prefix="aot_cover.cwd.")
        atexit.register(shutil.rmtree, _CWD, True)
    return _CWD


def footprint(interp, script, cwd=None):
    env = dict(os.environ, INIMERSE_HOT_OPS="1")
    env.pop("INIMERSE_IR_TABLE", None)
    if cwd is None:
        cwd = private_cwd()
    rc, out, _err = run([str(interp), "--no-mods", "run", str(script)], cwd, env)
    ops = {}
    for line in out.splitlines():
        m = OP_HOT.match(line)
        if m and int(m.group(2)) > 0:
            ops[m.group(1)] = int(m.group(2))
    return rc, ops


PROBE_PROGRAM = "say 1\n"


def prelude_report(interp):
    """What the prelude would have added, and the control that it no longer does.

    Both halves are measured rather than asserted: the opcode set of
    params.params itself, run as an ordinary program from the private directory
    (where it is not also the prelude), and the difference between a census of
    one tiny program taken at the repository root and one taken privately.  That
    difference is the control.  If params.params is there and the difference is
    empty, the private directory is not private, which means the fraction
    printed below is the contaminated one -- so that case refuses instead of
    printing a number under a basis it does not have.
    """
    pf = ROOT / "params.params"
    present = pf.is_file()
    own = {}
    if present:
        _rc, own = footprint(interp, pf)
    probe = Path(tempfile.mkdtemp(prefix="aot_cover.probe.")) / "probe.im"
    probe.write_text(PROBE_PROGRAM)
    _rc, at_root = footprint(interp, probe, cwd=ROOT)
    _rc, at_private = footprint(interp, probe, cwd=private_cwd())
    diff = sorted(set(at_root) - set(at_private))
    if present and not diff:
        print("IR-AOT-PRELUDE|file=params.params|present=yes|own-opcodes=%s|n=%d"
              "|excluded=no|cwd=private" % (",".join(sorted(own)) or "none", len(own)))
        print("IR-AOT-PRELUDE-CTL|probe=%s|at-root=%s|at-private=%s|diff=none"
              "|own=%s|verdict=FAIL"
              % (PROBE_PROGRAM.strip(), ",".join(sorted(at_root)),
                 ",".join(sorted(at_private)), ",".join(sorted(own)) or "none"))
        refuse("params.params is in the tree and the private directory did not"
               " exclude it: the probe census is the same in both (%d opcodes),"
               " so the census below is not the census it says it is"
               % len(at_root))
    if not present and diff:
        print("IR-AOT-PRELUDE|file=params.params|present=no|own-opcodes=none|n=0"
              "|excluded=yes|cwd=private")
        print("IR-AOT-PRELUDE-CTL|probe=%s|at-root=%s|at-private=%s|diff=%s"
              "|own=none|verdict=FAIL"
              % (PROBE_PROGRAM.strip(), ",".join(sorted(at_root)),
                 ",".join(sorted(at_private)), ",".join(diff)))
        refuse("there is no params.params in the tree, yet the two directories"
               " disagree by %s: something else in the current directory is being"
               " executed, and it is not named here" % ",".join(diff))
    print("IR-AOT-PRELUDE|file=params.params|present=%s|own-opcodes=%s|n=%d"
          "|excluded=yes|cwd=private"
          % ("yes" if present else "no", ",".join(sorted(own)) or "none", len(own)))
    print("IR-AOT-PRELUDE-CTL|probe=%s|at-root=%s|at-private=%s|diff=%s|own=%s"
          "|verdict=PASS"
          % (PROBE_PROGRAM.strip(), ",".join(sorted(at_root)),
             ",".join(sorted(at_private)), ",".join(diff) or "none",
             ",".join(sorted(own)) or "none"))


def enum_members(name, text=None):
    """The members of a `typedef enum { ... } name;` in src/parser/ast.h, by value.

    The histogram names a kind as expr:EXPR_STRING, which is only a reading
    while the number is resolved against the declaration that gives it its
    meaning.  A table copied into this file would be one more claim about
    another file's source, and it would keep printing after the header changed,
    so the declaration is read here -- and when it has been edited into a shape
    this does not understand, the caller refuses instead of guessing.
    """
    if text is None:
        text = AST_H.read_text() if AST_H.is_file() else ""
    # [^{}]* and not .*?: an unanchored non-greedy body would start at the FIRST
    # typedef enum in the header and swallow the structs after it, turning a
    # successful search into a body this refuses -- the wrong answer twice over.
    m = re.search(r"typedef\s+enum\s*\{([^{}]*)\}\s*" + re.escape(name) + r"\s*;",
                  text, re.S)
    if not m:
        return None
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    toks = [t.strip() for t in body.split(",")]
    while toks and not toks[-1]:
        toks.pop()
    if not toks or not all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", t) for t in toks):
        return None
    return {i: t for i, t in enumerate(toks)}


def normalized(msg):
    """The message with the parts that vary from case to case blanked out.

    A name or a line number differs between two programs that hit the same
    wall; keeping them would turn one cause into as many buckets as there are
    names, which reads as a longer list of problems than the corpus has.  What
    was blanked is printed as <name> or <N>, so a bucket still says that these
    were the translator's own words and not a summary written here.
    """
    m = re.sub(r"'[^']*'", "<name>", msg)
    m = re.sub(r'"[^"]*"', "<name>", m)
    m = re.sub(r"\d+", "<N>", m)
    return m.strip()


def classify(last_line, exprs, stmts):
    """(stage, cause) for one refusal, decoded from the message itself.

    The two kind numbers are the message's own, resolved against the header; a
    number outside the declaration's range is named unknown-kind-N rather than
    dropped, because a decoder that invents a name for it would report a
    construct that does not exist.  Anything not recognised here goes to
    unparsed: rather than into a stage that would then be overstating itself.
    """
    m = re.match(r"aot_native: expression kind (\d+) is outside the numeric subset",
                 last_line)
    if m:
        n = int(m.group(1))
        return "numeric-subset", "expr:" + exprs.get(n, "unknown-kind-%d" % n)
    m = re.match(r"aot_native: statement kind (\d+) is outside the numeric subset",
                 last_line)
    if m:
        n = int(m.group(1))
        return "numeric-subset", "stat:" + stmts.get(n, "unknown-kind-%d" % n)
    if re.match(r"Error\b", last_line):
        return "frontend", "parse-error"
    if last_line.startswith("aot_native:"):
        return "numeric-subset", normalized(last_line[len("aot_native:"):].strip())
    return "unparsed", normalized(last_line)[:90] or "<no message>"


def histogram_is_complete(hist, rejected):
    """True when the buckets account for every refusal, not for most of them.

    A histogram that quietly drops the messages it could not place reads as a
    smaller and cleaner set of causes than the corpus has, and the fraction it
    sits beside would then be explained by a list that does not add up, so the
    sum is checked rather than assumed.
    """
    total = sum(hist.values())
    if total == rejected:
        return True, "buckets sum to %d == repo-rejected" % rejected
    return False, "buckets sum to %d but repo-rejected is %d" % (total, rejected)


def self_check():
    """Exercise the decoder where it can be seen failing.

    Nothing here needs a build: the decoder and the completeness assertion are
    fed messages the translator does emit and messages it must not pretend to
    understand, including a kind number outside the header's range.  Exit 0 when
    every check held, 1 with the failing check on stderr.
    """
    checks = [0]

    def check(what, got, want):
        checks[0] += 1
        if got != want:
            print("aot_cover self-check: FAIL -- %s\n  want %r\n  got  %r"
                  % (what, want, got), file=sys.stderr)
            sys.exit(1)

    exprs = enum_members("ExprType")
    stmts = enum_members("StmtType")
    check("ExprType readable from src/parser/ast.h", exprs is not None, True)
    check("StmtType readable from src/parser/ast.h", stmts is not None, True)
    check("EXPR_STRING is kind 2", exprs.get(2), "EXPR_STRING")
    check("EXPR_LIST is kind 10", exprs.get(10), "EXPR_LIST")
    check("EXPR_LAMBDA is kind 17", exprs.get(17), "EXPR_LAMBDA")
    check("STMT_THREAD_DEF is kind 6", stmts.get(6), "STMT_THREAD_DEF")
    check("STMT_TYPE is kind 39", stmts.get(39), "STMT_TYPE")
    check("STMT_TRY is kind 45", stmts.get(45), "STMT_TRY")

    samples = [
        ("aot_native: expression kind 2 is outside the numeric subset",
         ("numeric-subset", "expr:EXPR_STRING")),
        ("aot_native: statement kind 39 is outside the numeric subset",
         ("numeric-subset", "stat:STMT_TYPE")),
        ("aot_native: expression kind 999 is outside the numeric subset",
         ("numeric-subset", "expr:unknown-kind-999")),
        ("Error at line 5: `be` declarations were removed (docs/SYNTAX.md 6.3)",
         ("frontend", "parse-error")),
        ("aot_native: call to 'atomic_set' is not a user-defined function in this program",
         ("numeric-subset",
          "call to <name> is not a user-defined function in this program")),
        ("aot_native: variable 'nil' is used but never assigned in this program",
         ("numeric-subset", "variable <name> is used but never assigned in this program")),
        ("a line this decoder has never seen",
         ("unparsed", "a line this decoder has never seen")),
        ("", ("unparsed", "<no message>")),
    ]
    for line, want in samples:
        check("classify(%r, header)" % (line[:48],), classify(line, exprs, stmts), want)

    check("a declaration edited into a shape this cannot read is refused",
          enum_members("ExprType", "typedef enum { EXPR_A = 1, EXPR_B } ExprType;"), None)
    check("a header that declares another enum is refused, not answered from memory",
          enum_members("ExprType", "typedef enum { EXPR_A, EXPR_B } StmtType;"), None)

    ok, _why = histogram_is_complete({"numeric-subset:expr:EXPR_STRING": 3}, 4)
    check("a histogram that drops one refusal is refused", ok, False)
    ok, _why = histogram_is_complete({"numeric-subset:expr:EXPR_STRING": 4}, 4)
    check("a histogram that accounts for every refusal passes", ok, True)

    print("aot_cover self-check: ok (%d checks)" % checks[0])
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", default=str(ROOT / "build"))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--pin", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-repo", action="store_true")
    ap.add_argument("--check-pin", action="store_true",
                    help="re-run the measurement and compare it with tools/aot_cover.pin.json;"
                         " a pin nobody re-runs is a constant, not a reading")
    ap.add_argument("--self-check", action="store_true",
                    help="check the refusal decoder against messages it must and must not"
                         " recognise; needs no build and measures no corpus")
    args = ap.parse_args()

    # Before the prerequisites below, on purpose: the decoder is this file's own
    # code and it has to be checkable in a tree where nothing has been built.
    if args.self_check:
        return self_check()

    build = Path(args.build)
    interp = build / "inimerse"
    aot = build / "aot-native"
    if not interp.is_file() or not aot.is_file():
        refuse("no inimerse or aot-native under %s (build them first)" % build)
    if not CASES.is_file():
        refuse("no case list at %s" % CASES)

    # Before the corpus, on purpose: it costs three runs, and if the private
    # directory is not private then every number below is about to be measured
    # in an environment this file does not claim to measure in.
    prelude_report(interp)

    mod = load_cases()
    cases = [(n, src) for n, src, _want in mod.EQUIVALENCE]
    if args.limit:
        cases = cases[:args.limit]
    if not cases:
        refuse("the inline case list is empty: a corpus of zero is not a reading")

    tmp = Path(tempfile.mkdtemp(prefix="aot_cover."))
    accepted, refused, differ = [], [], []
    seen = {}
    for name, src in cases:
        safe = re.sub(r"[^A-Za-z0-9_]", "_", name)
        script = tmp / (safe + ".im")
        script.write_text(src)
        cfile = tmp / (safe + ".c")
        exe = tmp / (safe + ".bin")
        trc, _o, terr = run([str(aot), "translate", str(script), str(cfile)], ROOT)
        if trc != 0:
            msg = (terr or "").strip().splitlines()
            refused.append((name, msg[-1][:120] if msg else "<no message>"))
            continue
        crc, _o, cerr = run([args.cc, "-O2", "-o", str(exe), str(cfile)], ROOT)
        if crc != 0:
            differ.append((name, "cc", (cerr or "").strip()[-120:]))
            continue
        irc, iout, _e = run([str(interp), "--no-mods", "run", str(script)],
                            private_cwd())
        nrc, nout, _e = run([str(exe)], ROOT)
        if irc != nrc or iout != nout:
            where = "exit" if irc != nrc else "stdout"
            differ.append((name, where, "interp=%s native=%s" % (irc, nrc)))
            continue
        _prc, ops = footprint(interp, script)
        if not ops:
            differ.append((name, "footprint", "accepted and equal, but no opcode was counted"))
            continue
        accepted.append(name)
        for op in ops:
            seen.setdefault(op, set()).add(name)

    # The repository corpus is taken from git, not from the filesystem, for the
    # same reason tools/mir_wired.test.py does: a program deleted on disk is
    # still part of the corpus this fraction is about, and a stray file that was
    # never committed is not.  The two globs are the ones the wiring gate uses,
    # so the denominators agree without a second opinion about what "the corpus"
    # means.
    repo_progs, repo_accepted = [], 0
    repo_hist = {}
    if not args.no_repo:
        exprs = enum_members("ExprType")
        stmts = enum_members("StmtType")
        if exprs is None or stmts is None:
            refuse("src/parser/ast.h does not declare ExprType/StmtType in a shape this"
                   " reads; the histogram names kinds from that declaration, and a name"
                   " guessed instead of read is not a reading")
        tracked = subprocess.run(
            ["git", "ls-files", "--", "vtest/*.im", "examples/**/*.im"],
            cwd=str(ROOT), capture_output=True, text=True)
        repo_progs = [ROOT / rel for rel in tracked.stdout.split() if rel]
        for p in repo_progs:
            cfile = tmp / ("repo_" + re.sub(r"[^A-Za-z0-9_]", "_", p.name) + ".c")
            trc, _o, terrs = run([str(aot), "translate", str(p), str(cfile)], ROOT)
            if trc == 0:
                repo_accepted += 1
                continue
            # stderr was thrown away here before this commit; the last line of it
            # is the only place the reason exists, and a reason nobody reads is
            # the sentence this reading used to print from memory instead.
            tail = (terrs or "").strip().splitlines()
            stage, cause = classify(tail[-1] if tail else "", exprs, stmts)
            key = "%s:%s" % (stage, cause)
            repo_hist[key] = repo_hist.get(key, 0) + 1

    def divergence_report():
        # Defined above the summary so the summary can say how many of the
        # declared cases are still diverging; the lines themselves are printed
        # below, where they have always been.
        div_lines = []
        # The divergence list is the other half of the same question: a program the
        # translator accepts and the native binary gets WRONG, with the expected
        # difference written down.  It is not a coverage cell -- nothing there is
        # counted as covered -- but a reader of the fraction deserves to know how
        # many accepted programs are known to disagree and where.  A case that
        # starts AGREEING is also reported: the list has then gone stale, and a
        # stale divergence list quietly excuses a difference that no longer exists.
        for name, src, want_im, want_native in getattr(mod, "DIVERGENCE", []):
            safe = "div_" + re.sub(r"[^A-Za-z0-9_]", "_", name)
            script = tmp / (safe + ".im")
            script.write_text(src)
            cfile = tmp / (safe + ".c")
            exe = tmp / (safe + ".bin")
            trc, _o, _e = run([str(aot), "translate", str(script), str(cfile)], ROOT)
            if trc != 0:
                div_lines.append(
                    "IR-AOT-DIVERGENCE|case=%s|state=translator-refused-it-now|known=declared"
                    % name)
                continue
            crc, _o, _e = run([args.cc, "-O2", "-o", str(exe), str(cfile)], ROOT)
            if crc != 0:
                div_lines.append("IR-AOT-DIVERGENCE|case=%s|state=cc-failed|known=declared" % name)
                continue
            irc, iout, _e = run([str(interp), "--no-mods", "run", str(script)],
                                private_cwd())
            nrc, nout, _e = run([str(exe)], ROOT)
            state = ("still-diverges" if (iout != nout or irc != nrc)
                     else "now-agrees-so-the-list-is-stale")
            div_lines.append(
                "IR-AOT-DIVERGENCE|case=%s|state=%s|interp-out=%s|native-out=%s"
                "|declared-interp=%s|declared-native=%s"
                % (name, state, repr(iout)[-24:], repr(nout)[-24:],
                   repr(want_im)[-24:], repr(want_native)[-24:]))
        diverging = sum(1 for l in div_lines if "|state=still-diverges|" in l)
        return div_lines, diverging
    div_lines, declared_diverge = divergence_report()

    covered = sorted(op for op in OPCODES if op in seen)
    missing = sorted(op for op in OPCODES if op not in seen)
    repo_rejected = len(repo_progs) - repo_accepted
    # The reasons are counted, so they have to add up to the count they explain:
    # a bucket that was never printed is not one this line can be missing.
    complete, complete_why = histogram_is_complete(repo_hist, repo_rejected)
    if not complete:
        refuse("the refusal histogram does not account for the corpus: " + complete_why)

    def stage_count(stage):
        return sum(n for k, n in repo_hist.items() if k.split(":", 1)[0] == stage)

    # differ= counts only the differences that are NOT written down; the declared
    # list is the other half of the same question, so how many of its cases still
    # diverge rides on this line rather than on a reader memory of it.
    print("IR-AOT-COVER|corpus=inline-cases:%d,repo:%d/%d|repo-rejected=%d"
          "|repo-rejected-stages=frontend-parse:%d,numeric-subset:%d"
          "|repo-rejected-causes=%d|repo-rejected-unparsed=%d"
          "|accepted=%d|refused=%d|differ=%d|declared-diverge=%d|covered=%d|of=%d"
          "|basis=opcodes-executed-by-the-interpreter-over-programs-the-translator-accepts"
          "|not-a-capability-count=yes"
          % (len(cases), repo_accepted, len(repo_progs), repo_rejected,
             stage_count("frontend"), stage_count("numeric-subset"),
             len(repo_hist), stage_count("unparsed"),
             len(accepted), len(refused), len(differ), declared_diverge,
             len(covered), len(OPCODES)))
    for op in covered:
        print("IR-AOT-OPCODE|op=%s|seen-in=%d|verdict=covered" % (op, len(seen[op])))
    for op in missing:
        print("IR-AOT-OPCODE|op=%s|seen-in=0|verdict=missing" % op)
    print("IR-AOT-MISSING|ops=%s|count=%d|because=no-accepted-program-executes-them"
          % (",".join(missing) or "none", len(missing)))
    for name, why, detail in differ:
        print("IR-AOT-DIFF|case=%s|first-diff=%s|detail=%s" % (name, why, detail))
    for name, msg in refused:
        print("IR-AOT-REFUSED|case=%s|why=%s" % (name, msg))
    # One line per cause, most common first: a reader of the fraction above is
    # owed the reasons themselves and not only their number, and these keys are
    # printed verbatim because they are the ones the assertion just summed.
    for key in sorted(repo_hist, key=lambda k: (-repo_hist[k], k)):
        stage, cause = key.split(":", 1)
        print("IR-AOT-REPO-REJECTED|n=%d|stage=%s|why=%s" % (repo_hist[key], stage, cause))

    for _div_line in div_lines:
        print(_div_line)
    print("aot_cover: cases=%d accepted=%d refused=%d differ=%d declared-diverge=%d"
          " repo-rejected-buckets=%d/%d"
          % (len(cases), len(accepted), len(refused), len(differ), declared_diverge,
             sum(repo_hist.values()), repo_rejected))

    if args.pin:
        PIN.write_text(json.dumps({
            "tree": head_sha(),
            "covered": len(covered),
            "of": len(OPCODES),
            "corpus": {"inline_cases": len(cases),
                       "repo_accepted": repo_accepted,
                       "repo_total": len(repo_progs)},
            "covered_ops": covered,
            "missing_ops": missing,
            "note": ("regenerate with tools/aot_cover.test.py --pin. The fraction a",
                     "reader sees in IR-COVER is this file, and this file names the",
                     "tree it was measured on so the checker can go back and re-run",
                     "the same measurement instead of trusting the number."),
        }, indent=2) + "\n")
        print("aot_cover: pin written to %s (tree=%s)" % (PIN, head_sha()))

    if args.check_pin:
        if not PIN.is_file():
            print("IR-AOT-PIN|tree=<no pin>|match=no|why=no-pin-file-to-recompute")
            return 1
        stored = json.loads(PIN.read_text())
        here = head_sha()
        # Two questions, two fields -- they are not the same question and they
        # cannot share a verdict.  What a reader quotes is `covered`/`of`; the
        # tree is where those numbers were measured.  A pin cannot name the
        # commit that contains it (the sha is a function of the content), so
        # requiring tree == HEAD makes `match=no` the permanent state of every
        # clean tree, and a red that is always on is a red nobody reads.
        # The verdict therefore follows the numbers, and tree drift is reported
        # as its own token -- the same split docs_headings.pin.json needed
        # (HEADINGS|pin-tree|mismatch= vs HEADINGS|stale|net=).
        numbers_match = (stored.get("covered") == len(covered)
                         and stored.get("of") == len(OPCODES))
        tree_moved = stored.get("tree") != here
        print("IR-AOT-PIN|tree=%s|head=%s|covered=%d|recomputed=%d|of=%d"
              "|corpus-pin=%d/%d|corpus-now=%d/%d"
              "|numbers-match=%s|tree-moved=%s|basis=the-quoted-number-is-compared"
              "-and-the-tree-is-reported-separately"
              % (stored.get("tree"), here, stored.get("covered", -1), len(covered),
                 len(OPCODES),
                 (stored.get("corpus") or {}).get("repo_accepted", -1),
                 (stored.get("corpus") or {}).get("repo_total", -1),
                 repo_accepted, len(repo_progs),
                 "yes" if numbers_match else "no",
                 "yes" if tree_moved else "no"))
        if tree_moved and numbers_match:
            print("IR-AOT-PIN-DRIFT|effect=the-pin-names-an-earlier-tree-and-its-numbers"
                  "-still-describe-this-one|note=a-commit-cannot-name-itself|"
                  "strict=--check-pin-tree")
        if not numbers_match:
            print("IR-AOT-PIN-STALE|effect=the-numbers-a-reader-would-quote-no-longer-"
                  "describe-this-tree|fix=rerun-with---pin")
            return 1

    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
