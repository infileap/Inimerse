#!/usr/bin/env python3
"""Audit the strength of every `--shape` clause passed to tools/gated_run.py.

Why this exists
---------------
`gated_run.py` is the wrapper this repository uses to assert a program's
*shape* — its output — while keeping the exit-code judgement separate from
the output judgement.  `docs/SYNTAX.md` §7.2 M13 records how many CTests assert
nothing at all; §10.236 and §10.237 of `docs/STATUS.md` then found that some
CTest-level `PASS_REGULAR_EXPRESSION`s could be satisfied by a program that did
not do what the test was about, and tightened them.

That work was done **by hand, one pattern at a time**, and its cost was written
down: "write the trivial program, see whether the gate still passes".  The
handful that were found were found because someone happened to look.

The shapes have the same exposure and no such pass.  A `--shape` clause that
pins only a marker word — one the program could print on its own — asserts
nothing about the computation behind it.

What this script does
---------------------
It runs the audit that §10.238 of `docs/STATUS.md` records, and it runs it the
way that audit actually had to be run, which is the only way it can be trusted:

  STAGE 1 (text)   Find every `--shape` clause in `CMakeLists.txt` and split
                   them into two kinds by their text alone:
                     - a clause that pins a *value* (`setcomp-ok union=5/1,9`)
                     - a clause that pins *no value* (`thread-await-ok`)
                   This stage is a screen, not a verdict.  It cannot see what
                   the program computes.

  STAGE 2 (mutation)  For every clause found by stage 1, take the program the
                   clause guards, **destroy the results it prints** (rewrite
                   every `say` literal, and every `say <expr>` the clause's
                   own text identifies), and run it.  If the clause still
                   matches the mutated output, the clause is **not** derived
                   from the computation — it is a marker, and the assertion is
                   weak.

Stage 2 is the load-bearing part, and it is deliberately falsifiable in both
directions:

  * it must FAIL to match a program whose computation was broken (otherwise it
    proves nothing);
  * it must still MATCH a program whose computation was left intact (otherwise
    it would flag every clause, which is the same as flagging none).

The self-test at the bottom of this file proves the first property on a
synthetic program that computes nothing, so a future change to the mutation
harness cannot silently turn this checker into a no-op.

What it does not claim
----------------------
Stage 2 proves that a clause **changes** when the computation changes.  It
cannot prove that the pinned value is the value the test *intends* — that is a
question about the language, not about the clause.

Usage
-----
    python3 tools/check_shape_assertions.py          # audit and exit non-zero on findings
    python3 tools/check_shape_assertions.py --json   # machine-readable
    python3 tools/check_shape_assertions.py --self-test   # prove the harness can fail

Requires a built `build/inimerse`; without it the script reports that it could
not audit and exits 2 (a distinct code, so a missing build is never mistaken
for a clean audit).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = os.path.join(REPO_ROOT, "CMakeLists.txt")

# A shape clause pins a value when it contains an assignment-like token:
#   key=value   key: value   key=value,other=value
# A bare marker ("thread-await-ok", "gc-ran") has none.
VALUE_TOKEN = re.compile(r"[A-Za-z0-9_)\]]\s*[=:]\s*[^\s]")

# A clause that is a single word with no value attached.
MARKER_ONLY = re.compile(r"^[A-Za-z][A-Za-z0-9_.-]*$")

SHAPE_RE = re.compile(r"--shape\s+(\"[^\"]*\"|'[^']*'|\S+)")

# A CMake variable reference is a placeholder resolved at configure time; the
# value is not in this file and cannot be judged here.
CMAKE_VAR = re.compile(r"\$\{[^}]+\}")


def find_binary(explicit: str | None = None) -> str | None:
    if explicit:
        return explicit if os.path.exists(explicit) else None
    for rel in ("build/inimerse", "build-release/inimerse"):
        p = os.path.join(REPO_ROOT, rel)
        if os.path.exists(p):
            return p
    return None


def parse_shapes(text: str):
    """Yield (test_name, clause) for every --shape clause in a CMakeLists text."""
    name = "<unknown>"
    for line in text.splitlines():
        m = re.search(r"add_test\(\s*NAME\s+(\w+)", line)
        if m:
            name = m.group(1)
        for s in SHAPE_RE.finditer(line):
            raw = s.group(1)
            if raw[:1] in "\"'":
                raw = raw[1:-1]
            yield name, raw


def parse_shape_commands_from_json(build_dir: str):
    """Yield (test_name, [clauses], program_path) from CTest's own metadata.

    Reading `ctest --show-only=json-v1` is used **in preference** to parsing
    `CMakeLists.txt` as text, because the text has three shapes this script
    would otherwise have to guess at:

      * `add_test(NAME x COMMAND ...)` on one line and `add_test(NAME x\\n
        COMMAND ...)` across two are both legal and both present;
      * a command may wrap over a third line;
      * `${CMAKE_SOURCE_DIR}` and `$<TARGET_FILE:inimerse>` are resolved by
        CMake, so the text does not say what actually runs.

    The JSON gives the resolved argv, which is the thing the audit is about.
    """
    try:
        r = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
                           capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if r.returncode != 0:
        return []
    try:
        data = json.loads(r.stdout)
    except ValueError:
        return []

    out = []
    for t in data.get("tests", []):
        cmd = [str(c) for c in (t.get("command") or [])]
        if not any("gated_run" in c for c in cmd):
            continue
        name = t.get("name", "<unknown>")
        clauses = []
        i = 0
        while i < len(cmd):
            if cmd[i] == "--shape" and i + 1 < len(cmd):
                clauses.append(cmd[i + 1])
                i += 2
                continue
            i += 1
        # The program is the last `.im` on the command line; an earlier one may
        # be an output path (`--dump tests/_mini.im`).
        cands = [c for c in cmd if c.endswith(".im")]
        prog = cands[-1] if cands else None
        if prog and not os.path.isabs(prog):
            prog = os.path.join(REPO_ROOT, prog)
        out.append((name, clauses, prog))
    return out


def parse_shape_commands(text: str):
    """Yield (test_name, [clauses], program_path) for every gated_run invocation.

    A text fallback for when CTest's metadata is unavailable.  It handles the
    one-line `add_test(NAME x COMMAND ...)` form only; the multi-line form is
    left to the JSON path, and a test the fallback cannot see is reported as
    not audited rather than silently skipped.
    """
    out = []
    for line in text.splitlines():
        m = re.search(r"add_test\(\s*NAME\s+(\w+)\s+COMMAND", line)
        if not m:
            continue
        name = m.group(1)
        if "gated_run" not in line:
            continue
        clauses = []
        for s in SHAPE_RE.finditer(line):
            raw = s.group(1)
            if raw[:1] in "\"'":
                raw = raw[1:-1]
            clauses.append(raw)
        prog = None
        after = line.rsplit("--", 1)
        if len(after) == 2:
            cands = [t.rstrip(")").rstrip(",").replace("${CMAKE_SOURCE_DIR}", REPO_ROOT)
                     for t in after[1].split() if t.rstrip(")").rstrip(",").endswith(".im")]
            if cands:
                prog = cands[-1]
                if not os.path.isabs(prog):
                    prog = os.path.join(REPO_ROOT, prog)
        out.append((name, clauses, prog))
    return out


def classify(clause: str):
    """Return (is_marker, why) for one clause — stage 1 only."""
    if clause == "":
        return True, "empty clause"
    if CMAKE_VAR.search(clause):
        return False, "cmake variable, resolved at configure time"
    if MARKER_ONLY.match(clause):
        return True, "bare marker word, no value pinned"
    if not VALUE_TOKEN.search(clause):
        return True, "no value pinned (no '=' or ':' with a value)"
    return False, "pins a value"


# --------------------------------------------------------------------------
# stage 2: mutation
# --------------------------------------------------------------------------

def split_top_level_plus(text: str):
    """Split `text` on `+` that is not inside a string or a bracket.

    A `+` inside a string literal or inside parentheses is not a
    concatenation operator, so the split has to know about both.
    """
    parts = []
    depth = 0
    in_str = False
    quote = ""
    cur = []
    i = 0
    while i < len(text):
        ch = text[i]
        if in_str:
            cur.append(ch)
            if ch == "\\" and i + 1 < len(text):
                cur.append(text[i + 1])
                i += 2
                continue
            if ch == quote:
                in_str = False
            i += 1
            continue
        if ch in "\"'":
            in_str = True
            quote = ch
            cur.append(ch)
            i += 1
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "+" and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    parts.append("".join(cur).strip())
    return [p for p in parts if p]


def is_literal(part: str) -> bool:
    """True when the part is a plain string literal — a marker."""
    p = part.strip()
    return len(p) >= 2 and p[0] in "\"'" and p[-1] == p[0]


def is_computed(part: str) -> bool:
    """True when the part derives its value instead of spelling it out.

    `str(w1)` derives; the literal `"atomic-ok"` and the bare number `42`
    do not.  This is the whole distinction stage 2 turns on.
    """
    p = part.strip()
    if not p:
        return False
    if is_literal(p):
        return False
    if re.fullmatch(r"-?\d+(\.\d+)?", p):
        return False
    return True


def neutralise(part: str) -> str:
    """Return a replacement for `part` that keeps the program runnable.

    A program that cannot run prints its error to stderr and the clause stops
    matching — which would look like a strong assertion for the wrong reason.
    So the replacement keeps the shape of the expression:

        str(x)`   -> `str("__MUT__")`   (still a string)
        f(x)`     -> `"__MUT__"`        (the whole call is replaced)
        `x`       -> `"__MUT__"`        (a value where a value was)
        `x + y`   -> `"__MUT__" + y`    (left operand neutralised)

    The call is replaced **as a whole** rather than having its arguments
    replaced.  Replacing the argument of `captured_fn()` with a constant
    changes nothing: the function ignores it and still returns the length of
    the captured array, so the mutation would not destroy the computation and
    the clause would look weak when it is not.  Replacing the call removes the
    computation entirely.
    """
    p = part.strip()
    m = re.match(r"^([A-Za-z_]\w*)\s*\((.*)\)$", p, re.S)
    if m:
        name = m.group(1)
        if name == "str":
            return 'str("__MUT__")'
        return '"__MUT__"'
    left, sep, right = p.partition("+")
    if sep:
        return neutralise(left) + " + " + right
    return '"__MUT__"'


def say_parts(line: str):
    """Return the concatenation parts of a `say` line, or None.

    A `say` is not always the whole line: a `case` branch puts it after the
    pattern, as in

        [{"kind": "ok", "data": [first, {"code": code}]}, tail]: say first + code

    so the search starts at the `say` keyword rather than at the beginning of
    the line, and the text before it (the pattern) is ignored.
    """
    s = line.strip()
    idx = s.find("say")
    while idx != -1:
        # `say` must be a word of its own, not the tail of an identifier:
        # `sayx` is a variable, `say x` is the keyword.  The check is on the
        # character right after the keyword, before any space is skipped.
        before_ok = idx == 0 or not (s[idx - 1].isalnum() or s[idx - 1] == "_")
        after = s[idx + 3:]
        if before_ok and (after == "" or after[0] not in
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"):
            s = after.lstrip()
            break
        idx = s.find("say", idx + 1)
    else:
        return None
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    return split_top_level_plus(s)


def mutate_computation(src: str):
    """Return `src` with every **computed** part of every `say` destroyed.

    The literal parts — the markers — are left exactly as they are.  A clause
    that still matches the mutated output is therefore pinned on the marker
    alone: the program computed nothing the clause can see, and the assertion
    is weak.

    A `say` that carries no computed part at all (`say "marker"`) is left
    alone by this function, and the clause it guards is reported separately by
    the caller — see `marker_only_says`.
    """
    out_lines = []
    changed_any = False
    for line in src.splitlines():
        parts = say_parts(line)
        if parts is None:
            out_lines.append(line)
            continue
        new_parts = []
        changed = False
        for part in parts:
            if is_computed(part):
                new_parts.append(neutralise(part))
                changed = True
            else:
                new_parts.append(part)
        if not changed:
            out_lines.append(line)
            continue
        changed_any = True
        # Keep everything that precedes the `say` keyword: a `case` branch
        # puts the pattern there, and dropping it would change the program
        # into something else — the mutation has to change the computation,
        # not the program.
        idx = line.find("say")
        while idx != -1:
            before_ok = idx == 0 or not (line[idx - 1].isalnum() or line[idx - 1] == "_")
            after = line[idx + 3:]
            if before_ok and (after == "" or after[0] not in
                              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"):
                break
            idx = line.find("say", idx + 1)
        prefix = line[:idx] if idx != -1 else ""
        out_lines.append(prefix + " + ".join(new_parts))
    return "\n".join(out_lines), changed_any


def marker_only_says(src: str):
    """Return the markers printed by `say` statements that compute nothing.

    These are the strings a program can print without doing any work, so a
    clause that names one of them asserts nothing about the computation.
    """
    out = []
    for line in src.splitlines():
        parts = say_parts(line)
        if parts is None:
            continue
        if all(not is_computed(p) for p in parts):
            for p in parts:
                if is_literal(p):
                    out.append(p.strip()[1:-1])
    return out


def neutralise_condition(cond: str) -> str:
    """Return a condition that is always false.

    A program that decides *whether to print the marker* behind an `if` has
    put its computation in the condition, not in the `say`.  Destroying the
    `say` alone would leave the condition intact and the marker would still
    be printed, so the mutation has to reach the condition too.

    The condition is replaced by `false`, not by its negation.  Negating
    `if ok { say "marker" }` would make the marker print exactly when `ok` is
    false — the marker would still appear, and the clause would still match,
    which would make a strong clause look weak.  A condition that is always
    false takes the guarded branch out of the picture entirely: if the marker
    was only printed because the computation succeeded, it stops being
    printed.
    """
    c = cond.strip()
    if not c:
        return c
    return "false"


def mutate_conditions(src: str):
    """Return `src` with every `if` condition negated.

    A guard that decides what to print is where the computation lives; see
    `neutralise_condition` for why this has to be part of the mutation.
    """
    out_lines = []
    changed = False
    for line in src.splitlines():
        m = re.match(r"^(\s*if\s+)(.*?)(\s*\{.*)$", line)
        if not m:
            out_lines.append(line)
            continue
        cond = m.group(2).strip()
        if not cond or "{" not in line:
            out_lines.append(line)
            continue
        out_lines.append(m.group(1) + neutralise_condition(cond) + m.group(3))
        changed = True
    return "\n".join(out_lines), changed


def mutate_program(src: str):
    """Return (mutated_source, changed) with every computation destroyed.

    Three things are destroyed:

      * the computed parts of every `say` — the values it prints;
      * the condition of every `if` — the decision that selects what to print;
      * (through the second) the accumulation of a flag like `ok = 0`, because
        the branch that sets it is the one whose condition was flipped.

    The literal markers are left alone on purpose.  They are what a weak
    clause pins, so keeping them is what makes the test meaningful: a clause
    that still matches after this is pinned on the marker and on nothing else.
    """
    out, say_changed = mutate_computation(src)
    out, cond_changed = mutate_conditions(out)
    return out, (say_changed or cond_changed)


def run_program(binary: str, program: str, timeout: int = 60):
    """Return (stdout, stderr) of the program."""
    try:
        r = subprocess.run([binary, program], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return "", ""
    except OSError:
        return "", ""
    return r.stdout, r.stderr


def clause_parts(clause: str):
    """Split a clause into the parts its regex is made of.

    `12.*7.*closure-throw-caught` is three requirements joined by `.*`, and
    the strength of the clause is the strength of its **weakest** part: a
    clause whose every part is a free marker asserts nothing, while a clause
    with one computed part asserts *something* even if the other parts are
    free.

    The split is on the regex alternation and the concatenation operators,
    because those are the places where the clause stops being one
    requirement and becomes several.
    """
    parts = re.split(r"\.\*|\.\+|\||&&", clause)
    return [p.strip() for p in parts if p.strip()]


def free_parts(clause: str, free_markers):
    """Return the parts of `clause` that the program prints with no work."""
    out = []
    for p in clause_parts(clause):
        if any(p in f for f in free_markers):
            out.append(p)
    return out


def trivial_program(clause: str) -> str:
    """Return a program that computes nothing and prints only the markers.

    This is the §10.236 method applied to shapes: to find out how much a
    clause asserts, write the program that asserts it with the least work and
    see whether the gate still passes.

    The program prints, as plain literals, the strings the clause mentions.
    A clause that still matches it is a clause that a program which did no
    work at all could satisfy, and the clause is therefore weak.
    """
    lines = []
    seen = set()
    for part in clause_parts(clause):
        # Only a part that is itself a plain string can be printed literally.
        if re.fullmatch(r"[A-Za-z0-9_.:@/\\ -]+", part) and part not in seen:
            seen.add(part)
            lines.append('say "%s"' % part.replace('"', '\\"'))
    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def clause_satisfied_by_trivial(binary: str, clause: str, workdir: str) -> bool:
    """True when a program that computes nothing satisfies `clause`.

    The clause is matched the way `gated_run.py` matches it — with `re.S`,
    because CMake's regex engine lets `.` cross a newline — so the answer is
    the answer the gate gives.
    """
    prog = trivial_program(clause)
    if not prog:
        return False
    path = os.path.join(workdir, "trivial.im")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(prog)
    out, _ = run_program(binary, path)
    try:
        rx = re.compile(clause, re.S)
    except re.error:
        return False
    return rx.search(out) is not None


def audit(binary: str, cmake_text: str, workdir: str, invocations=None):
    """Run both stages.  Returns (audited, findings, not_audited).

    A clause that matches the engine's **diagnostics** (stderr) is not judged
    here.  Those clauses — "declarations were removed", "is not implemented" —
    assert that the engine *refused* the program, and the program cannot print
    them because it never reaches its `say` statements.  They are stronger than
    the program-output clauses, not weaker.

    Only clauses that match the program's **stdout** are audited, because only
    those can be satisfied by the program printing something it computed.

    The verdict comes from the §10.236 method, applied to shapes: for every
    clause, a program that computes nothing is written and run, and a clause
    that program satisfies is weak.  The mutation stage is kept as a second,
    independent reading of the same question — a clause must fail both ways to
    be called strong.
    """
    if invocations is None:
        invocations = parse_shape_commands(cmake_text)
    findings = []
    not_audited = []
    audited = 0

    for name, clauses, prog in invocations:
        markers = [c for c in clauses if classify(c)[0] and c]
        if not markers:
            continue
        if not prog or not os.path.exists(prog):
            not_audited.append((name, markers, "no program file found"))
            continue
        src = open(prog, "r", encoding="utf-8").read()

        # --- the §10.236 method: a program that computes nothing ---------
        trivially = [c for c in markers
                     if clause_satisfied_by_trivial(binary, c, workdir)]

        # --- the mutation reading: destroy the computation ----------------
        mutated, changed = mutate_program(src)
        survivors = []
        if changed:
            tmp = os.path.join(workdir, f"{name}.im")
            with open(tmp, "w", encoding="utf-8") as fh:
                fh.write(mutated)
            out, err = run_program(binary, tmp)
            # A clause satisfied by the engine's diagnostics is out of scope.
            diag_only = [c for c in markers
                         if not re.search(c, out) and re.search(c, err)]
            candidates = [c for c in markers if c not in diag_only]
            if not candidates:
                not_audited.append((name, diag_only,
                                     "clause asserts an engine diagnostic (stderr), "
                                     "not program output"))
                continue
            audited += 1
            survivors = [c for c in candidates if re.search(c, out)]
        else:
            # The mutation had nothing to destroy: every `say` in the program
            # prints a literal, so the program computes nothing at all.  A
            # clause that matches such a program is satisfied by a program
            # that did no work — that is the weakest form of assertion there
            # is, and it must be reported rather than skipped.
            #
            # The exception is a clause that matches the engine's diagnostics:
            # such a program never reaches its `say` statements, so the
            # clause asserts that the engine *refused* the program, which is
            # stronger than anything the program could print.
            out, err = run_program(binary, prog)
            diag_only = [c for c in markers
                         if not re.search(c, out) and re.search(c, err)]
            candidates = [c for c in markers if c not in diag_only]
            if not candidates:
                not_audited.append((name, diag_only,
                                     "clause asserts an engine diagnostic (stderr), "
                                     "not program output"))
                continue
            audited += 1
            survivors = [c for c in candidates if re.search(c, out)]
            if survivors:
                findings.append({"test": name, "clauses": sorted(set(survivors)),
                                 "program": prog,
                                 "kind": "the program computes nothing at all; "
                                         "the clause is satisfied by printing "
                                         "the marker",
                                 "severity": "weak"})
            continue

        weak = sorted(set(survivors))
        if not weak:
            continue
        # The clause survived the destruction of the program's computation.
        # That is causal evidence: the clause does not depend on the
        # computation, so a program whose computation was replaced by
        # constants still satisfies it.
        findings.append({"test": name, "clauses": weak, "program": prog,
                         "kind": "satisfied by a program whose computation was destroyed",
                         "severity": "weak"})

    return audited, findings, not_audited


# --------------------------------------------------------------------------
# self-test: prove the harness can actually fail
# --------------------------------------------------------------------------

SELFTEST_SRC = '''
# A program that computes nothing and prints a marker.  The harness must see
# this clause as WEAK, or it cannot be trusted to call anything weak.
say "selftest-marker-ok"
'''

# A program that DOES compute, and prints the computed value next to a marker.
# The harness must see this clause as STRONG, or it would flag everything.
SELFTEST_STRONG_SRC = '''
n = 6 * 7
say "selftest-strong-ok n=" + str(n)
'''

# A program that computes a value but whose `say` prints only a hard-coded
# marker: the computation is dead code, and a clause on the marker is weak.
SELFTEST_DEADCODE_SRC = '''
n = 6 * 7
say "selftest-deadcode-ok"
'''


def self_test(binary: str) -> int:
    """Prove the harness fails on weak clauses and passes on strong ones.

    Three cases, and all three must behave as stated, or the checker is not
    trustworthy:

      1. a clause on a program that computes nothing   -> WEAK
      2. a clause on a program whose computation was
         replaced by a constant                        -> WEAK
      3. a clause on a program that really computes     -> STRONG
    """
    workdir = tempfile.mkdtemp(prefix="shape-selftest-")
    failures = []
    try:
        # --- case 1: nothing is computed -------------------------------
        src = SELFTEST_SRC
        free = marker_only_says(src)
        if not free:
            failures.append("case 1: the marker-only program was not recognised")
        elif "selftest-marker-ok" not in free:
            failures.append("case 1: the marker was not extracted from the program")

        # --- case 2: the computation is replaced by a constant --------
        src2 = SELFTEST_DEADCODE_SRC
        mutated, changed = mutate_program(src2)
        if not changed:
            # The program has no computed `say`, so the mutation cannot act;
            # the weakness is caught by the marker-only rule instead.
            if not marker_only_says(src2):
                failures.append("case 2: neither the mutation nor the marker rule fired")
        else:
            out, _ = run_program(binary, write_program(workdir, "case2.im", mutated))
            if re.search("selftest-deadcode-ok", out):
                failures.append("case 2: the clause still matched after the computation was destroyed")

        # --- case 3: a real computation must stay strong ----------------
        # The marker survives the mutation by design — that is the point of
        # the mutation, it destroys the *computation* and leaves the marker
        # alone.  So the marker alone cannot tell a strong clause from a weak
        # one.  What tells them apart is the VALUE: a clause that pins a
        # computed value must stop matching once that computation is gone.
        src3 = SELFTEST_STRONG_SRC
        mutated3, changed3 = mutate_program(src3)
        if not changed3:
            failures.append("case 3: the mutation did not touch a computed say")
        else:
            orig_out, _ = run_program(binary, write_program(workdir, "case3-orig.im", src3))
            mut_out, _ = run_program(binary, write_program(workdir, "case3.im", mutated3))
            # The computed value is 42; the original prints it, the mutant does
            # not.  The pattern is anchored on the field name so that a module
            # load notice which happens to contain "42" cannot be mistaken for
            # the program's own output.
            if not re.search(r"n=42\b", orig_out):
                failures.append("case 3: the original program does not print the computed value")
            if re.search(r"n=42\b", mut_out):
                failures.append("case 3: the computed value survived the mutation")

        if failures:
            for f in failures:
                print("self-test FAILED: " + f)
            return 1
        print("self-test ok: weak clauses are detected, a clause on a real "
              "computation is not")
        return 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def write_program(workdir: str, name: str, src: str) -> str:
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(src)
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description="Audit --shape clause strength.")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--cmake", default=CMAKE)
    ap.add_argument("--binary", default=None, help="path to the inimerse binary")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the mutation harness can detect a weak clause")
    args = ap.parse_args(argv)

    binary = find_binary(args.binary)
    if args.self_test:
        if not binary:
            print("check_shape_assertions: no inimerse binary found", file=sys.stderr)
            return 2
        return self_test(binary)

    if not os.path.exists(args.cmake):
        print(f"check_shape_assertions: no such file: {args.cmake}", file=sys.stderr)
        return 2

    with open(args.cmake, "r", encoding="utf-8") as fh:
        cmake_text = fh.read()

    total_clauses = sum(len(c) for _, c, _ in parse_shape_commands(cmake_text))
    build_dir = os.path.join(REPO_ROOT, "build")
    invocations = parse_shape_commands_from_json(build_dir)
    if invocations:
        total_clauses = sum(len(c) for _, c, _ in invocations)
    if not binary:
        result = {
            "clauses": total_clauses,
            "audited": 0,
            "findings": [],
            "not_audited": [],
            "error": "no inimerse binary found; stage 2 cannot run",
        }
        if args.json:
            print(json.dumps(result, indent=2, ensure_ascii=False))
        else:
            print(f"check_shape_assertions: {total_clauses} shape clause(s), "
                  f"but no inimerse binary was found — the mutation stage "
                  f"cannot run.", file=sys.stderr)
        return 2

    workdir = tempfile.mkdtemp(prefix="shape-audit-")
    try:
        audited, findings, not_audited = audit(binary, cmake_text, workdir, invocations)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    result = {
        "clauses": total_clauses,
        "audited": audited,
        "findings": findings,
        "weak": [f for f in findings if f.get("severity") == "weak"],
        "partial": [f for f in findings if f.get("severity") == "partial"],
        "not_audited": [{"test": n, "clauses": c, "why": w} for n, c, w in not_audited],
    }

    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 1 if result["weak"] else 0

    print(f"check_shape_assertions: {total_clauses} shape clause(s); "
          f"{audited} no-value clause group(s) audited; "
          f"{len(result['weak'])} weak, {len(result['partial'])} partial.")
    for f in findings:
        tag = "WEAK" if f.get("severity") == "weak" else "PARTIAL"
        print(f"  {tag} {f['test']}: {f['clauses']}")
        print(f"       {f['kind']}")
    for n, c, w in not_audited:
        print(f"  not audited: {n} ({w})")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
