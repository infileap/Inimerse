#!/usr/bin/env python3
"""Controls for a held reference that has to name what holds it.

The checker already answers "is this number held?".  These cases ask the next
question -- "by what?" -- because the answer is what separates a hold from a
coincidence that has not expired yet.

Three cases, all run in a throwaway worktree of HEAD:

  coincidental-hold-names-its-holder
      a reference held ONLY by free text on its line reports that text, the
      line it matched, distance 0, and the grade.

  three-lines-hand-it-back
      move the quoted text three lines in the file it is cited against and the
      same reference stops being held.  This is the accident that released a
      real reference, done on purpose.

  a-guess-is-not-a-holder
      the negative control for the control: attribute holds by "the first span
      on the line" instead of "the span that is actually there", and case two's
      assertion goes red.

The tool under test is the WORKING-TREE copy, copied into the worktree of HEAD,
so uncommitted edits are what run -- the same discipline as the other controls.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = "tools/check_line_refs.py"
DOCS = "docs/BOARD.md"
TARGET = "tools/gate.sh"
SPAN = "another gate is running against"
TICK = chr(96)

HOLD = re.compile(
    r"^HOLD\|ref=(?P<ref>[^|]+):(?P<ref_line>\d+)"
    r"\|target=(?P<target>[^|]+):(?P<number>\d+)"
    r"\|by=(?P<by>[^@]+)@(?P<by_at>[^|]+)"
    r"\|matched=(?P<matched>[^:]+):(?P<matched_line>\d+)"
    r"\|distance=(?P<distance>\d+)"
    r"\|kind=(?P<kind>[a-z]+)"
    r"\|grade=(?P<grade>[a-z ]+)$"
)


def refuse(why: str) -> None:
    print(f"holds: REFUSED|{why}", file=sys.stderr)
    print("holds: not registered: nothing was checked", file=sys.stderr)
    raise SystemExit(2)


def git(*args: str, cwd: pathlib.Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(cwd), *args],
        capture_output=True, text=True, encoding="utf-8",
    )


def run(tree: pathlib.Path, *args: str) -> str:
    done = subprocess.run(
        [sys.executable, str(tree / TOOL), *args],
        capture_output=True, text=True, encoding="utf-8", cwd=str(tree),
    )
    return done.stdout + done.stderr


def holds(text: str) -> dict:
    out = {}
    for line in text.splitlines():
        m = HOLD.match(line)
        if m:
            out["{}:{}".format(m["ref"], m["ref_line"])] = m.groupdict()
    return out


def target_line(tree: pathlib.Path) -> int:
    body = (tree / TARGET).read_text(encoding="utf-8")
    for n, line in enumerate(body.splitlines(), 1):
        if SPAN in line:
            return n
    return 0


def add_reference(tree: pathlib.Path) -> str:
    n = target_line(tree)
    if not n:
        refuse("{} no longer contains the quoted text".format(TARGET))
    path = tree / DOCS
    text = path.read_text(encoding="utf-8")
    row = "Hold control: {t}{s}{t} {f}:{n} names a refusal, and the quoted text on this line is not a file name.".format(
        t=TICK, s=SPAN, f=TARGET, n=n
    )
    if not text.endswith("\n"):
        text += "\n"
    path.write_text(text + "\n" + row + "\n", encoding="utf-8")
    first = len(text.splitlines()) + 2
    return "{}:{}".format(DOCS, first)


def shift_target(tree: pathlib.Path, by: int = 3) -> None:
    path = tree / TARGET
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    lines[0:0] = ["# hold control: injected line {}\n".format(i) for i in range(by)]
    path.write_text("".join(lines), encoding="utf-8")


def guess_attribution(tree: pathlib.Path, restore: bool = False) -> bool:
    path = tree / TOOL
    text = path.read_text(encoding="utf-8")
    good = "for s in spans:\n                                if s in target_lines[number - 1]:"
    bad = "for s in spans:\n                                if True:"
    if restore:
        if bad not in text:
            return False
        path.write_text(text.replace(bad, good), encoding="utf-8")
        return True
    if good not in text:
        return False
    path.write_text(text.replace(good, bad), encoding="utf-8")
    return True


RULE_SAME = '''    for s in spans:
        if s in target_lines[number - 1]:
            return (s, number)
    return None'''

RULE_GUESS = '''    for s in spans:
        if True:
            return (s, number)
    return None'''

RULE_WIDE = '''    for k in range(max(0, number - 4), min(len(target_lines), number + 3)):
        for s in spans:
            if s in target_lines[k]:
                return (s, k + 1)
    return None'''


def swap_rule(tree: pathlib.Path, old: str, new: str) -> bool:
    path = tree / TOOL
    text = path.read_text(encoding="utf-8")
    if old not in text:
        return False
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    return True


def verdict_line(text: str) -> str:
    for line in text.splitlines():
        if line.startswith("VERDICT|"):
            return line
    return ""


def main() -> int:
    if not (ROOT / TOOL).exists():
        refuse("{} is not in this tree".format(TOOL))
    probe = git("rev-parse", "--git-dir", cwd=ROOT)
    if probe.returncode != 0:
        refuse("{} is not a git work tree, so there is no HEAD to copy".format(ROOT))
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="holds-"))
    tree = tmp / "tree"
    made = git("worktree", "add", "--detach", str(tree), "HEAD", cwd=ROOT)
    if made.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        refuse("git worktree add failed: {}".format((made.stderr or made.stdout).strip()))
    cases = 0
    red = 0
    try:
        shutil.copy2(ROOT / TOOL, tree / TOOL)
        ref = add_reference(tree)

        cases += 1
        row = holds(run(tree, "--holds")).get(ref)
        if row is None:
            red += 1
            print("coincidental-hold-names-its-holder RED: no HOLD line for {}".format(ref))
        elif (
            row["by"] != SPAN
            or row["target"] != TARGET
            or row["distance"] != "0"
            or row["grade"] != "picked up"
            or row["matched"] != TARGET
        ):
            red += 1
            print(
                "coincidental-hold-names-its-holder RED: by={!r} grade={} distance={} matched={}".format(
                    row["by"], row["grade"], row["distance"], row["matched"]
                )
            )
        else:
            print(
                "coincidental-hold-names-its-holder ok: {} held by {!r}, matched {}:{}, grade {}, kind {}".format(
                    ref, row["by"], row["matched"], row["matched_line"], row["grade"], row["kind"]
                )
            )

        cases += 1
        was = target_line(tree)
        shift_target(tree, 3)
        row = holds(run(tree, "--holds")).get(ref)
        if row is not None:
            red += 1
            print(
                "three-lines-hand-it-back RED: {} is still held by {!r} after the text moved three lines".format(
                    ref, row["by"]
                )
            )
        else:
            print(
                "three-lines-hand-it-back ok: {} is not held once the quoted text moved from {}:{} to {}:{}".format(
                    DOCS, TARGET, was, TARGET, was + 3
                )
            )

        cases += 1
        if not swap_rule(tree, RULE_SAME, RULE_GUESS):
            red += 1
            print("a-guess-is-not-a-holder RED: could not find the hold rule to mutate")
        else:
            guessed = holds(run(tree, "--holds")).get(ref)
            if guessed is None:
                red += 1
                print(
                    "a-guess-is-not-a-holder RED: even a guesser reported no hold, so case two would pass for the wrong reason"
                )
            else:
                print(
                    "a-guess-is-not-a-holder ok: a guesser holds {} by {!r} -- case two's assertion is what catches it".format(
                        ref, guessed["by"]
                    )
                )
            swap_rule(tree, RULE_GUESS, RULE_SAME)

        cases += 1
        if not swap_rule(tree, RULE_SAME, RULE_WIDE):
            red += 1
            print("a-widened-rule-must-say-it-reached RED: could not find the hold rule to widen")
        else:
            out = run(tree, "--holds")
            said = verdict_line(out)
            row = holds(out).get(ref)
            distance = None if row is None else int(row["distance"])
            if distance in (None, 0):
                red += 1
                print(
                    "a-widened-rule-must-say-it-reached RED: the widened rule reports distance={} for {}, so this case cannot tell whether the rule reached".format(
                        distance, ref
                    )
                )
            elif "distance-without-a-widened-kind" not in said:
                red += 1
                print(
                    "a-widened-rule-must-say-it-reached RED: {} is held at distance {} with kind {}, and the verdict does not say so: {}".format(
                        ref, distance, row["kind"], said
                    )
                )
            else:
                print(
                    "a-widened-rule-must-say-it-reached ok: the widened rule holds {} at distance {}, keeps kind={}, and is refused by distance-without-a-widened-kind".format(
                        ref, distance, row["kind"]
                    )
                )
            swap_rule(tree, RULE_WIDE, RULE_SAME)
    finally:
        git("worktree", "remove", "--force", str(tree), cwd=ROOT)
        shutil.rmtree(tmp, ignore_errors=True)

    green = cases - red
    tail = "" if red else " -- none"
    print("holds: cases={} red={} green={}{}".format(cases, red, green, tail))
    return 1 if red else 0


if __name__ == "__main__":
    raise SystemExit(main())
