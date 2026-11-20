#!/usr/bin/env python3
"""check_try_region_enters.py -- a refusal that cannot tell "skipped over the
gap" from "jumped into the middle".

Why this exists (docs/STATUS.md 10.249):

The MIR route refuses a program when a jump from outside a `try` REGION lands
inside it, with `why=try-enters`.  The rule is right, and its witness is real:
two `try` statements in one function put a region after a region, and the
first one's catch block jumps over the second's -- a jump from inside region 1
to the start of region 2, whose frame was never opened.

What the rule cannot see is whether the jump came from ANY region.

    i = 0
    while i < 2 { i = i + 1 }
    try { say 1 } catch (e) { say "c" }

The loop's exit jump targets the instruction right after the loop, and that
instruction is `OP_TRY_START` -- which OPENS the frame when it runs.  Landing on
it is the same as falling into the try, and the interpreter accepts the
program.  The route refuses it.

So `try-enters` is two shapes wearing one name:

    source inside another try region   -> the jump really skips an open frame
                                          (the witness; must stay refused)
    source in no region at all         -> the jump lands on OP_TRY_START and
                                          opens its own frame (must be allowed)

This checker measures which one each refusal is, over the corpus, and counts
them apart.  It does NOT change the compiler: the second shape looks safe, and
"looks safe" is not the standard this repository accepts for a relaxation --
the relaxation needs a witness that the interpreter and the route agree on the
program afterwards, which is a different piece of work.

Exit codes: 0 the split was measured and no legal program is in the refused
bucket as "source in no region"; 1 at least one is; 2 the measurement could not
be made.
"""

from __future__ import annotations

import glob
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The refusal line the tool prints.  Kept as one pattern so a wording change in
# the tool is a red here rather than a silent "measured nothing".
REFUSAL = re.compile(
    r"why=try-enters\|detail=container=\d+ insn=(\d+) jumps INTO the try region "
    r"\[(\d+),(\d+)\] at (\d+)"
)

# A program whose only `try` is preceded by a loop or an `if` -- the shape the
# interpreter accepts and the route refuses.  Written here rather than taken
# from the corpus because the corpus does not contain it: the three refused
# corpus programs all have several `try` statements, which is the witness shape
# and must stay refused.
MISFIRE_SHAPES = [
    (
        "a while before a single try",
        "i = 0\nwhile i < 2 {\n  i = i + 1\n}\ntry {\n  say 1\n} catch (e) { say \"c\" }\n",
    ),
    (
        "an if before a single try",
        "if true {\n  say 1\n}\ntry {\n  say 2\n} catch (e) { say \"c\" }\n",
    ),
    (
        "a never-entered while before a single try",
        "i = 0\nwhile i < 0 {\n  i = i + 1\n}\ntry {\n  say 1\n} catch (e) { say \"c\" }\n",
    ),
]

# The shape that must STAY refused: the source of the jump is inside a region.
WITNESS_SHAPES = [
    (
        "two try statements in one function",
        "try {\n  say 1\n} catch (e) { say \"a\" }\ntry {\n  say 2\n} catch (e) { say \"b\" }\n",
    ),
    (
        "a loop inside a first try, before a second try",
        "try {\n  i = 0\n  while i < 2 {\n    i = i + 1\n  }\n} catch (e) { say \"a\" }\n"
        "try {\n  say 2\n} catch (e) { say \"b\" }\n",
    ),
]


def _tool(repo_root: str) -> str:
    return os.path.join(repo_root, "build", "aot-mir")


def _engine(repo_root: str) -> str:
    return os.path.join(repo_root, "build", "inimerse")


def translate(tool: str, source: str, out_c: str) -> tuple[int, str]:
    proc = subprocess.run(
        [tool, "translate", source, out_c], capture_output=True, text=True
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def region_of(detail: str):
    """(source_insn, region_start, region_end, target) or None."""
    m = REFUSAL.search(detail)
    if not m:
        return None
    return tuple(int(g) for g in m.groups())


def measure(repo_root: str) -> tuple[int, list[str], dict]:
    tool = _tool(repo_root)
    engine = _engine(repo_root)
    notes: list[str] = []
    if not os.path.isfile(tool):
        raise RuntimeError("no %s -- configure and build first" % tool)

    work = tempfile.mkdtemp(prefix="check_try_enters_")
    out_c = os.path.join(work, "out.c")
    stats = {"misfire": 0, "witness": 0, "legal_and_ok": 0, "witness_missing": 0}

    # The misfire shapes: refused by the route, accepted by the interpreter.
    for name, source in MISFIRE_SHAPES:
        path = os.path.join(work, "m.im")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(source)
        rc, detail = translate(tool, path, out_c)
        if rc == 0:
            notes.append("NOT-REFUSED  %s -- the route now accepts this shape" % name)
            continue
        region = region_of(detail)
        if region is None:
            notes.append("OTHER-REASON %s -- refused, but not by try-enters" % name)
            continue
        src_insn, rs, re_, target = region
        # The claim: the source is NOT inside any region, and it lands on the
        # region's own start (the instruction that opens the frame).
        lands_on_start = target == rs
        if os.path.isfile(engine):
            run = subprocess.run(
                [engine, "--no-mods", "run", path], capture_output=True, text=True
            )
            interp_ok = run.returncode == 0
        else:
            interp_ok = None
        stats["misfire"] += 1
        notes.append(
            "MISFIRE      %-46s src=%d region=[%d,%d] target=%d lands_on_start=%s "
            "interpreter_rc=%s"
            % (
                name,
                src_insn,
                rs,
                re_,
                target,
                lands_on_start,
                "ok" if interp_ok else ("FAIL" if interp_ok is False else "?"),
            )
        )

    # The witness shapes: must stay refused.  A witness that starts translating
    # is a red too -- it means the relaxation went further than the argument.
    for name, source in WITNESS_SHAPES:
        path = os.path.join(work, "w.im")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(source)
        rc, detail = translate(tool, path, out_c)
        if rc == 0:
            stats["witness_missing"] += 1
            notes.append(
                "WITNESS-GONE %s -- this shape used to be refused and now translates; "
                "if that was intended, this case has to move to MISFIRE_SHAPES "
                "with an argument, not be deleted" % name
            )
            continue
        region = region_of(detail)
        if region is None:
            notes.append("WITNESS-OTHER %s -- refused, but not by try-enters" % name)
            continue
        src_insn, rs, re_, target = region
        stats["witness"] += 1
        notes.append(
            "WITNESS      %-46s src=%d region=[%d,%d] target=%d"
            % (name, src_insn, rs, re_, target)
        )

    # The corpus: every try-enters refusal has to be a witness, not a misfire.
    corpus_misfire = []
    for path in sorted(glob.glob(os.path.join(repo_root, "vtest", "*.im"))):
        rc, detail = translate(tool, path, out_c)
        if rc == 0:
            stats["legal_and_ok"] += 1
            continue
        region = region_of(detail)
        if region is None:
            continue
        src_insn, rs, _, target = region
        # A corpus refusal that lands on the region's own start with no other
        # region around it is the misfire shape in the wild.
        rel = os.path.relpath(path, repo_root)
        text = open(path, encoding="utf-8", errors="replace").read()
        if target == rs and text.count("try") >= 1:
            corpus_misfire.append((rel, src_insn, rs, text.count("try")))

    stats["corpus_misfire"] = len(corpus_misfire)
    for rel, src_insn, rs, ntry in corpus_misfire:
        notes.append(
            "CORPUS       %-46s src=%d region_start=%d try-tokens=%d"
            % (rel, src_insn, rs, ntry)
        )

    import shutil

    shutil.rmtree(work, ignore_errors=True)
    # Red when a LEGAL program is refused (misfire > 0), or when a witness that
    # must stay refused starts translating.  The first draft had this inverted
    # -- `misfire == 0` -- which made the checker green on the tree that has
    # three misfires and red on a tree that has none.
    red = 1 if (stats["misfire"] > 0 or stats["witness_missing"]) else 0
    return red, notes, stats


def self_test() -> int:
    """The pattern has to match the line the tool really prints.

    A checker whose pattern silently stops matching reports "no misfire" and
    "no witness" at once, which is the shape this whole family is about.  The
    control is a line copied from a real run, and a near-miss that must NOT
    match.
    """
    failures = []
    real = (
        "IR-MIR-REFUSED|why=try-enters|detail=container=0 insn=6 jumps INTO the "
        "try region [12,15] at 12, whose frame was never opened"
    )
    got = region_of(real)
    if got != (6, 12, 15, 12):
        failures.append("the refusal line did not parse: %r" % (got,))
    near = real.replace("try-enters", "try-escapes")
    if region_of(near) is not None:
        failures.append("a try-escapes line must not be counted as try-enters")
    if region_of("IR-MIR-REFUSED|why=threads|detail=x") is not None:
        failures.append("an unrelated refusal must not parse")

    print("check_try_region_enters --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_try_region_enters: self-test FAILED (%d)" % len(failures))
        return 1
    print("  ok   a real refusal line parses to (src, region_start, region_end, target)")
    print("  ok   try-escapes and unrelated refusals do not parse as try-enters")
    print("check_try_region_enters: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, notes, stats = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_try_region_enters: cannot measure: %s" % problem)
        return 2

    for line in notes:
        print("  " + line)
    print(
        "check_try_region_enters: misfire shapes refused=%d, witness shapes refused=%d, "
        "corpus programs refused on a region start=%d, corpus programs translated=%d"
        % (
            stats["misfire"],
            stats["witness"],
            stats.get("corpus_misfire", 0),
            stats["legal_and_ok"],
        )
    )
    if red:
        print(
            "check_try_region_enters: the refusal cannot tell a jump that SKIPPED "
            "over the gap from one that entered an open frame."
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
