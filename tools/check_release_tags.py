#!/usr/bin/env python3
"""Why this exists (docs/AUDIT.md 1.74 E): a tag is a moving pointer.

`docs/STATUS.md`'s release-point cell once read `v0.5.2` -> `0ebd68d`, and it
was true when it was written.  The tag was then moved twice, and the same cell
became false without anyone editing it.  That is the fourth kind of failure --
"right when written, then somebody moved the thing being measured" -- and the
first three (a command that does not reproduce, a filter that is wrong, a
literal that counts itself) are all avoidable by writing more carefully.  This
one is not.

It is also invisible locally: moving a tag leaves no trace.  `git reflog show
v0.5.2` prints nothing, so there is no history to read.  A number that was true
and is now false, with no edit and no log entry, has to be checked by running
something, not by reading something.

What it checks, for the tags `git tag -l` reports:

  1. every version mention written in backticks in docs/ names a tag that
     exists;
  2. every release-point binding -- a backticked tag, then an arrow or `=`,
     then a backticked sha, adjacent -- records the sha that
     `git rev-parse --short '<tag>^{commit}'` gives today.

The tag names are never written down in this file.  They come from
`git tag -l`, so the next release adds one without anyone editing this checker,
and a checker that had `v0.5.2` in it would need editing at exactly the moment
it is most likely to be forgotten.

Three honest boundaries.

  * A mention is a version number written in backticks that either starts the
    quoted text or follows a quote character.  A version number that follows a
    bare space inside a phrase belongs to the phrase, and both of the false
    positives this rule exists for look exactly like that: docs/STATUS.md:1857
    writes `oauth_loop v0.1.0` (a Cargo crate version, and `v0.1.0` is not a
    tag) and docs/REQUIREMENTS_ANALYSIS.md:85 writes `f90d355 Release v0.3.0`
    (a commit message).  Neither is a tag reference.  A tag mentioned bare and
    misspelled would be missed.  The major-number filter is derived from the
    tag namespace for the same reason: `v24.20.0` in docs/WASM.md is a Node
    version.

  * A binding is recognized by shape, so a document that records a *former*
    binding must not write it in the binding shape.  This checker cannot tell a
    claim from a recollection, and neither can a reader skimming a table cell --
    which is the point.  Where a document does need to quote an old binding, it
    puts it in a fenced block, and a fenced block is not an assertion.

  * A register is a document whose subject is defects, and it quotes false
    values on purpose: `docs/AUDIT.md` 1.74 E quotes the old STATUS.md cell
    verbatim, and those quotes are the evidence.  Registers are listed in
    REGISTERS below, each with the reason.  The same guard as
    `tools/check_orphan_targets.py`'s ALLOWED applies -- **a register that
    excludes nothing is reported**, because an allowance that excuses nothing
    is a sentence no run can falsify.

Negative evidence (this is a re-runnable experiment, not a claim):

    git tag v0.0.0-test <some commit>
    cp docs/RELEASE_0.5.2.md /tmp/x.md          # a copy, never the original
    sed -i 's/`v0.5.1` -> `4e444dd`/`v0.0.0-test` -> `deadbee`/' /tmp/x.md
    python3 tools/check_release_tags.py --docs-root /tmp

    => the binding names a tag that exists and records the wrong sha: red.

And the control that proves the rule is not vacuous: write the same wrong sha
for a tag that does NOT exist => "names a tag that is not in git tag -l": red.
A checker that reported nothing when a binding was wrong and nothing when the
tag was missing would be reporting nothing at all.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parent.parent

# A document whose subject is defects, and which quotes false values on purpose.
# Each entry says why.  A register that excludes no binding is reported: an
# allowance that excuses nothing is a sentence no run can falsify.
REGISTERS = {
    "docs/AUDIT.md": (
        "the defect register: 1.74 E quotes the old STATUS.md cell verbatim, "
        "and those quotes are the evidence for the defect it records"
    ),
}

# How many bindings the sweep must still find.  A sweep that finds zero is
# green and asserts nothing -- the same shape as docs/AUDIT.md 1.65's zero
# hits.  This is a floor, not a target: finding more is fine, finding fewer is
# a document that stopped making release claims, or a rule that stopped
# matching them.
EXP_TAG_BINDINGS = int(os.environ.get("EXP_TAG_BINDINGS", "3"))

TAG_NAME = re.compile(r"^v(\d+)\.(\d+)\.(\d+)")
# No `.` in the trailing class: `v0.5.0..5868940` is a range expression, and a
# class that swallowed the `..` would report the whole range as a tag name.
TAG_REF = re.compile(r"v(\d+)\.(\d+)\.(\d+)[A-Za-z0-9\-]*")
BACKTICKED = re.compile(r"`([^`\n]*)`")
BINDING = re.compile(r"`(v[0-9][^`\s]*)`[ \t]*(?:→|->|=)[ \t]*`([0-9a-f]{7,40})`")
FENCE = re.compile(r"^\s*(?:```|~~~)")
QUOTE = re.compile(r"^\s*>")
# A tag reference starts the quoted text or follows a quote character.  A
# version number that follows a bare space inside a phrase belongs to the
# phrase: `Release v0.3.0` is a commit message and `oauth_loop v0.1.0` is a
# Cargo crate version, and neither of them is a tag.
REF_LEAD = set("'\"([<")


def refs_in(span):
    """Tag references written inside one backticked span."""
    for m in TAG_REF.finditer(span):
        if m.start() == 0 or span[m.start() - 1] in REF_LEAD:
            yield m


def git(*args):
    r = subprocess.run(
        ["git", "-C", str(ROOT)] + list(args), capture_output=True, text=True
    )
    if r.returncode != 0:
        sys.stderr.write(
            "check_release_tags: git %s failed:\n%s" % (" ".join(args), r.stderr)
        )
        raise SystemExit(2)
    return r.stdout


def tags():
    """tag name -> short sha of the commit it points at today."""
    out = {}
    for name in git("tag", "-l").split("\n"):
        name = name.strip()
        if name:
            out[name] = git("rev-parse", "--short", name + "^{commit}").strip()
    if not out:
        sys.stderr.write("check_release_tags: git tag -l reported no tags\n")
        raise SystemExit(2)
    return out


def docs_markdown(root_override):
    """[(display path, path to read)] for every markdown document in scope."""
    if root_override:
        base = Path(root_override)
        if not base.is_dir():
            sys.stderr.write(
                "check_release_tags: --docs-root %s is not a directory\n"
                % root_override
            )
            raise SystemExit(2)
        return [(str(p.relative_to(base)), p) for p in sorted(base.rglob("*.md"))]
    # -z, because without it git C-quotes paths containing spaces or non-ASCII
    # and the quoted form no longer matches a suffix test.  That trap has now
    # cost this repository three times (docs/AUDIT.md 1.75).
    out = git("ls-files", "-z", "docs/")
    rels = [p for p in out.split("\0") if p.endswith(".md")]
    return [(r, ROOT / r) for r in rels]


def main():
    argv = sys.argv[1:]
    root_override = None
    if argv and argv[0] == "--docs-root":
        root_override = argv[1]
        argv = argv[2:]
    if argv:
        sys.stderr.write("check_release_tags: unexpected argument %r\n" % argv[0])
        raise SystemExit(2)

    known = tags()
    majors = {TAG_NAME.match(n).group(1) for n in known if TAG_NAME.match(n)}

    checked = []          # (rel, lineno, tag, sha)
    excluded = []         # (rel, lineno, tag, sha, why)
    missing_tags = []     # (rel, lineno, name)
    mentions = 0
    skipped_major = 0
    excused_by = {}

    documents = docs_markdown(root_override)
    scanned = {rel for rel, _ in documents}
    for rel, path in documents:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        in_fence = False
        for lineno, line in enumerate(text.split("\n"), 1):
            if FENCE.match(line):
                in_fence = True if not in_fence else False
                continue
            quoted = bool(QUOTE.match(line))

            for span in BACKTICKED.findall(line):
                for m in refs_in(span):
                    if m.group(1) not in majors:
                        skipped_major += 1
                        continue
                    mentions += 1
                    name = m.group(0)
                    if name not in known:
                        missing_tags.append((rel, lineno, name))

            for m in BINDING.finditer(line):
                name, sha = m.group(1), m.group(2)
                if name not in known:
                    missing_tags.append((rel, lineno, name))
                    continue
                if rel in REGISTERS:
                    excluded.append((rel, lineno, name, sha, "register"))
                    excused_by[rel] = excused_by.get(rel, 0) + 1
                elif in_fence:
                    excluded.append((rel, lineno, name, sha, "fenced block"))
                elif quoted:
                    excluded.append((rel, lineno, name, sha, "blockquote"))
                else:
                    checked.append((rel, lineno, name, sha))

    wrong = [c for c in checked if c[3] != known[c[2]]]
    idle = [r for r in REGISTERS if r in scanned and r not in excused_by]

    print(
        "check_release_tags: %d tag(s) in the namespace (major(s) %s); "
        "%d version mention(s) in backticks, %d skipped as another project's "
        "number." % (len(known), ", ".join(sorted(majors)), mentions, skipped_major)
    )
    print(
        "check_release_tags: %d binding(s) checked, %d excluded by structure "
        "(%s)."
        % (
            len(checked),
            len(excluded),
            ", ".join(
                sorted({"%s x%d" % (w, sum(1 for e in excluded if e[4] == w)) for w in {e[4] for e in excluded}})
            )
            or "none",
        )
    )
    for rel, lineno, name, sha, why in excluded:
        print(
            "check_release_tags:   excluded %s:%d  `%s` -> `%s`  (%s)"
            % (rel, lineno, name, sha, why)
        )
    for rel, lineno, name in missing_tags:
        print(
            "check_release_tags: %s:%d names `%s`, which is not in `git tag -l`"
            % (rel, lineno, name)
        )
    for rel, lineno, name, sha in wrong:
        print(
            "check_release_tags: %s:%d records `%s` -> `%s`, but `git rev-parse "
            "--short '%s^{commit}'` is `%s` today -- the tag moved and the "
            "document did not" % (rel, lineno, name, sha, name, known[name])
        )
    for rel in idle:
        print(
            "check_release_tags: %s is listed in REGISTERS (%s), but no binding "
            "was excluded there, so the allowance excuses nothing"
            % (rel, REGISTERS[rel])
        )

    failed = bool(missing_tags or wrong or idle)
    if len(checked) < EXP_TAG_BINDINGS:
        print(
            "check_release_tags: only %d binding(s) were checked, below "
            "EXP_TAG_BINDINGS=%d -- a sweep that finds nothing is green and "
            "asserts nothing" % (len(checked), EXP_TAG_BINDINGS)
        )
        failed = True

    if failed:
        print("check_release_tags: FAILED")
        return 1
    print(
        "check_release_tags: every binding agrees with `git rev-parse` today.  "
        "This says the documents and the tags agree right now; it does not say "
        "the tags point where they were meant to, and it cannot see a tag "
        "that moves after this run."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
