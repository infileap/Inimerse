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
     `git rev-parse --short '<tag>^{commit}'` gives today;
  3. the release baseline the root README states is the newest tag.  That
     number is a copy, and it read `0.5.0` while `v0.5.2` was the newest tag:
     a copy nobody compares expires in silence.  The check is here because the
     sweep below reads docs/ only, and the root README is not under docs/.

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

  * A binding is a claim about where a tag points today unless the text says
    otherwise, and it has to say so NEXT TO THE BINDING -- not by living in a
    file that some list excuses.  Two things say it.  A *recollection* needs a
    marker ("记录", "初稿写过", "曾经的", "[obs:") AND the falsifier that makes
    the old value old ("失效", "不再", "已移到").  A *quotation* of another
    document's text needs a `docs/...` path and a quotation verb on the line.
    An exemption granted by a list of files cannot keep up with a rule written
    by shape, and this checker had one: `docs/STATUS.md:42` says "本格初稿写过
    `v0.5.2` -> `0ebd68d` ... 前者在 tag 被移动后失效" -- the document followed
    the move -- and the list version printed "the tag moved and the document
    did not".  The same guard as `tools/check_orphan_targets.py`'s ALLOWED
    applies, one class at a time -- **every exemption class must exempt at
    least one binding in this tree, or it is reported**.  An unused licence is
    not the same thing as a blind sweep: an input *form* with no member is a
    failure (`tools/check_line_refs.py` reports one that way, because deleting
    the form would blind the sweep), while an *exemption class* with no member
    is a mechanism no run can exercise, and this docstring would still read as
    if it stood guard.  A class that is legitimately empty is deleted, not
    excused.  This file had a third one, a `>` blockquote, and no binding in
    `docs/` is written in that form (`git grep -nE '^[[:space:]]*>' -- docs/`
    finds none), so the branch is gone; a blockquoted binding is now read by
    the shape rule like any other line, and a blockquote of another document
    carries the `docs/...` path and the verb that make it a quotation.

Negative evidence (these are re-runnable experiments, not claims).  All of
them run against a COPY under --docs-root, never against docs/ itself:

    git tag v0.0.0-test <some commit>
    cp -r docs /tmp/ctl && rm /tmp/ctl/RELEASE_0.5.2.md
    printf '%s\n' '`v0.0.0-test` -> `deadbee`' >> /tmp/ctl/STATUS.md
    python3 tools/check_release_tags.py --docs-root /tmp/ctl

    => a bare binding, no record marker, no falsifier: red.

  * the same line with the marker and the falsifier, and the same wrong sha:
    "记录：`v0.0.0-test` 曾经的绑定 `deadbee`，已在 tag 移动后失效" => exempt, and
    the run is green.  That pair is the rule's two directions: it is the SHAPE
    that decides, not the file the line lives in.
  * and the control that proves the sweep is not vacuous: write the same wrong
    sha for a tag that does NOT exist => "names a tag that is not in git tag
    -l": red.  A checker that reported nothing when a binding was wrong and
    nothing when the tag was missing would be reporting nothing at all.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parent.parent

# There is no file-wide exemption list, and that is the change: a list of
# excused files is a rule about where a sentence LIVES, while the rule is about
# what the sentence SAYS.  `docs/AUDIT.md` was on that list, and the list is
# what made the checker print a false verdict about `docs/STATUS.md:42`.
RECORD_MARKERS = ("记录", "初稿写过", "曾经的", "曾记", "曾把", "当时", "[obs:")
FALSIFIER_MARKERS = ("失效", "不再", "已移到", "已漂", "移动后", "旧值", "[obs:")
QUOTE_VERBS = ("里写", "写过", "原文", "引文", "引用")
OTHER_DOC = re.compile(r"`docs/[^`]+`")

# How many bindings the sweep must still find.  A sweep that finds zero is
# green and asserts nothing -- the same shape as docs/AUDIT.md 1.65's zero
# hits.  This is a floor, not a target: finding more is fine, finding fewer is
# a document that stopped making release claims, or a rule that stopped
# matching them.
# 2, and it was 3 while the exemption was a list of files: `docs/STATUS.md:42`
# carries two bindings that the list did not excuse and the shape rule does
# (both are recollections), so the floor moved with the rule it measures.  It
# is still a floor on a set that has to be non-empty, which is what the number
# is for -- `--only release-tags` would otherwise go green on a checker that
# stopped matching bindings at all.
EXP_TAG_BINDINGS = int(os.environ.get("EXP_TAG_BINDINGS", "2"))

# The exemption rules have to exempt something, or they are sentences no run can
# falsify.  This is the same guard the deleted file list had, moved onto the
# rules that replaced it -- and it is per class, because one floor on one class
# leaves the other classes free to be dead.  A floor per class, not a target.
EXP_TAG_EXEMPTION_CLASSES = tuple(
    c
    for c in os.environ.get(
        "EXP_TAG_EXEMPTION_CLASSES", "fenced block,quotation,recollection"
    ).split(",")
    if c
)

# How many lines a record marker and a falsifier may sit in to excuse a binding:
# its own line, plus the same reach on each side.  A sentence that says "the tag
# moved and this number is the old one" can put the marker above and the
# falsifier below, so a window that reaches upwards only reports a falsifier
# that IS on the line next to the binding as absent -- and quotes the rule it is
# breaking while doing it.  At 2 this reaches upwards only, which is what this
# file did before the asymmetry was measured.
TAG_WINDOW_LINES = 3

TAG_NAME = re.compile(r"^v(\d+)\.(\d+)\.(\d+)")
# No `.` in the trailing class: `v0.5.0..5868940` is a range expression, and a
# class that swallowed the `..` would report the whole range as a tag name.
TAG_REF = re.compile(r"v(\d+)\.(\d+)\.(\d+)[A-Za-z0-9\-]*")
BACKTICKED = re.compile(r"`([^`\n]*)`")
BINDING = re.compile(r"`(v[0-9][^`\s]*)`[ \t]*(?:→|->|=)[ \t]*`([0-9a-f]{7,40})`")
FENCE = re.compile(r"^\s*(?:```|~~~)")
# The release baseline the root README states.  A baseline is a copy of the
# newest tag, and this one read `0.5.0` while `v0.5.2` was the newest tag.
BASELINE = re.compile(r"当前发布基线：\*\*([0-9]+\.[0-9]+\.[0-9]+)\*\*")
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


def baseline_claim():
    """(version, lineno) the root README states as the release baseline."""
    try:
        text = (ROOT / "README.md").read_text(encoding="utf-8")
    except OSError:
        return None
    for lineno, line in enumerate(text.split("\n"), 1):
        m = BASELINE.search(line)
        if m:
            return m.group(1), lineno
    return None


def recollection(line, lines, lineno):
    """Why this binding is not a claim, or None if it is one.

    The window is TAG_WINDOW_LINES lines centred on the binding, because a
    sentence that says "the tag moved and this number is the old one" can put
    the marker and the falsifier on either side of it.  This sentence and the
    failure message in main() have always promised both sides; the slice did
    not, and reached one line up instead of one line each way.
    """
    above = TAG_WINDOW_LINES // 2
    below = TAG_WINDOW_LINES - 1 - above
    window = "\n".join(lines[max(0, lineno - 1 - above):lineno + below])
    marked = [w for w in RECORD_MARKERS if w in window]
    falsified = [w for w in FALSIFIER_MARKERS if w in window]
    if marked and falsified:
        return "recollection"
    if OTHER_DOC.search(line) and any(v in line for v in QUOTE_VERBS):
        return "quotation"
    return None


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
    versioned = [n for n in known if TAG_NAME.match(n)]
    newest = (
        max(versioned, key=lambda n: tuple(int(x) for x in TAG_NAME.match(n).groups()))
        if versioned
        else None
    )

    checked = []          # (rel, lineno, tag, sha)
    excluded = []         # (rel, lineno, tag, sha, why)
    missing_tags = []     # (rel, lineno, name)
    mentions = 0
    skipped_major = 0
    excused_by = {}

    documents = docs_markdown(root_override)
    for rel, path in documents:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        in_fence = False
        lines = text.split("\n")
        for lineno, line in enumerate(lines, 1):
            if FENCE.match(line):
                in_fence = True if not in_fence else False
                continue

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
                if in_fence:
                    excluded.append((rel, lineno, name, sha, "fenced block"))
                else:
                    why = recollection(line, lines, lineno)
                    if why:
                        excluded.append((rel, lineno, name, sha, why))
                    else:
                        checked.append((rel, lineno, name, sha))

    # Counted from what was actually exempted, not incremented at the branch
    # that exempted it: a class can be reached from more than one place, and a
    # count that only sees one of them is a count that can be zero while the
    # class is doing its job.
    excused_by = {}
    for _rel, _lineno, _name, _sha, why in excluded:
        excused_by[why] = excused_by.get(why, 0) + 1

    wrong = [c for c in checked if c[3] != known[c[2]]]

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
            "--short '%s^{commit}'` is `%s` today.  Read as a claim: no record "
            "marker and no falsifier sit on this line or the lines next to it, "
            "so this is taken as where the tag points now -- a recollection is "
            "exempt, and this line is not written as one"
            % (rel, lineno, name, sha, name, known[name])
        )
    # The baseline is a copy of the newest tag, and this is what makes the copy
    # safe: a number nobody compares is a number that expires in silence.
    claim = baseline_claim()
    baseline_failed = False
    if claim is None:
        print(
            "check_release_tags: the root README states no release baseline -- a "
            "check that can find nothing is green and asserts nothing.  If the "
            "line was dropped on purpose, drop this assertion in the same commit."
        )
        baseline_failed = True
    else:
        version, lineno = claim
        if newest is None or ("v" + version) != newest:
            print(
                "check_release_tags: README.md:%d states the release baseline "
                "`%s`, and the newest tag is `%s`.  A baseline no release moves "
                "is a number nobody is keeping -- move it in the commit that "
                "tags, or drop the number and point at `git tag "
                "--sort=-v:refname | head -1`." % (lineno, version, newest or "none")
            )
            baseline_failed = True

    failed = bool(missing_tags or wrong) or baseline_failed
    for klass in EXP_TAG_EXEMPTION_CLASSES:
        if excused_by.get(klass, 0) == 0:
            print(
                "check_release_tags: the %s exemption class exempted no binding "
                "in this tree, and EXP_TAG_EXEMPTION_CLASSES still lists it -- a "
                "class no run can exercise is a rule that stands guard in the "
                "docstring only.  Delete the class, or find what it should have "
                "caught." % klass
            )
            failed = True
    for klass in sorted(excused_by):
        if klass not in EXP_TAG_EXEMPTION_CLASSES:
            print(
                "check_release_tags: the %s exemption class exempted %d binding(s) "
                "and EXP_TAG_EXEMPTION_CLASSES does not list it -- an exemption "
                "nobody declared is one nobody decided on."
                % (klass, excused_by[klass])
            )
            failed = True
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
        "check_release_tags: every binding agrees with `git rev-parse` today, "
        "and the root README states the newest tag.  "
        "This says the documents and the tags agree right now; it does not say "
        "the tags point where they were meant to, and it cannot see a tag "
        "that moves after this run."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
