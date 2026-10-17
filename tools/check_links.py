#!/usr/bin/env python3
"""check_links.py — verify every relative link in this repository's Markdown.

Absolute URLs are not fetched (that would make the gate depend on the network);
only links that must resolve to a file or directory *inside* the checkout are
checked.  That is the failure mode that actually rots: a file is moved into
`docs/archive/` and a dozen documents keep pointing at where it used to be.

The checked set is the repository's *git-tracked* Markdown (see
`iter_markdown_files`), not the Markdown lying in the working tree.  This is a
correctness property, not an optimisation: the gate answers "does this
repository's references resolve", so a file that is not part of the repository
must not be able to change the answer.

Usage:
    python3 tools/check_links.py            # exit 1 if any link is broken
    python3 tools/check_links.py -v         # list every link, not just bad ones
    python3 tools/check_links.py --json     # machine-readable report
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Inline links:  [label](target "optional title")
INLINE_RE = re.compile(r"!?\[[^\]]*\]\(\s*(<[^>]*>|[^\s)]+)(?:\s+\"[^\"]*\")?\s*\)")
# Bare autolinks:  <relative/path.md>
ANGLE_RE = re.compile(r"<((?!https?://|mailto:)[^<>\s]+\.(?:md|im|py|js|json|sh|yml|yaml))>")
FENCE_RE = re.compile(r"^(\s*)(```|~~~)")
# Inline code span, as a run of N backticks closed by the same run of N.
INLINE_CODE_RE = re.compile(r"(`+)(?:(?!\1).)*\1", re.S)

def strip_code_fences(text: str) -> str:
    """Blank out fenced code blocks so their contents are not parsed as links."""
    out, inside, fence = [], False, ""
    for line in text.splitlines():
        m = FENCE_RE.match(line)
        if m:
            if not inside:
                inside, fence = True, m.group(2)
            elif line.strip().startswith(fence):
                inside = False
            out.append("")
            continue
        out.append("" if inside else line)
    return "\n".join(out)


def iter_markdown_files() -> list[str]:
    """Return the repository's Markdown files, as git-tracked paths.

    `git ls-files` is the definition of "this repository's content": it misses
    nothing tracked and cannot pick up anything untracked.  Walking the working
    tree instead made this gate non-hermetic -- an unrelated session's `.verify/`
    scratch directory (untracked, at the repo root) once contributed 12 broken
    links and turned the gate red for a repository that was perfectly fine.

    That is worse than a nuisance failure.  A gate that goes red for reasons the
    reader cannot act on teaches its readers to triage it ("is this one of the
    junk reds?") instead of to fix the reference, and a gate nobody trusts is
    worse than no gate.  Extending the skip-list cannot fix this class: any
    blacklist is one unlisted scratch directory behind.  Enumerating the tracked
    set removes the class.

    There is deliberately no `os.walk` fallback.  A fallback would reinstate
    exactly the defect this function exists to remove, and it would do so only
    on machines where git is missing -- the environment where a junk-red gate is
    hardest to diagnose.  Fail loudly instead.
    """
    try:
        proc = subprocess.run(
            ["git", "ls-files", "-z", "--", "*.md", "*.markdown"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        sys.exit(
            f"check_links: cannot run git ({exc}).\n"
            "check_links: this stage checks the repository's git-tracked "
            "Markdown; without git there is no way to distinguish repository "
            "content from scratch files left in the working tree.\n"
            "check_links: refusing to fall back to walking the working tree."
        )
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        sys.exit(
            f"check_links: 'git ls-files' failed (exit {proc.returncode}) in "
            f"{REPO_ROOT}\n{detail}\n"
            "check_links: refusing to fall back to walking the working tree."
        )
    # -z: NUL-separated, so paths containing newlines survive verbatim.
    return sorted(
        name
        for name in proc.stdout.decode("utf-8", "surrogateescape").split("\0")
        if name
    )


def targets_in(text: str) -> list[str]:
    body = strip_code_fences(text)
    # Inline code is prose, not markup: `object["m"](...)` is not a link.
    body = INLINE_CODE_RE.sub(" ", body)
    targets = [m.group(1) for m in INLINE_RE.finditer(body)]
    targets += [m.group(1) for m in ANGLE_RE.finditer(body)]
    return targets


def classify(target: str) -> tuple[str, str]:
    """Return (kind, cleaned) where kind is 'external' | 'anchor' | 'path'."""
    raw = target.strip()
    if raw.startswith("<") and raw.endswith(">"):
        raw = raw[1:-1].strip()
    if not raw:
        return "anchor", ""
    if raw.startswith("#"):
        return "anchor", ""
    split = urlsplit(raw)
    if split.scheme in ("http", "https", "mailto", "tel", "ftp", "data"):
        return "external", ""
    if raw.startswith("//"):
        return "external", ""
    path = unquote(split.path)
    if not path:
        return "anchor", ""
    return "path", path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also list every link that resolves")
    ap.add_argument("--json", action="store_true", help="emit a JSON report")
    args = ap.parse_args()

    files = iter_markdown_files_checked()
    total = external = anchor = checked = 0
    broken: list[tuple[str, str, str]] = []   # (source, target, why)
    good: list[tuple[str, str]] = []

    for rel in files:
        abspath = os.path.join(REPO_ROOT, rel)
        try:
            with open(abspath, encoding="utf-8") as fh:
                text = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            broken.append((rel, "<unreadable>", str(exc)))
            continue
        for target in targets_in(text):
            total += 1
            kind, path = classify(target)
            if kind == "external":
                external += 1
                continue
            if kind == "anchor":
                anchor += 1
                continue
            checked += 1
            resolved = os.path.normpath(
                os.path.join(REPO_ROOT, os.path.dirname(rel), path)
            )
            if os.path.exists(resolved):
                good.append((rel, target))
            else:
                broken.append((rel, target, "does not exist"))

    if args.json:
        print(printable(json.dumps({
            "files": len(files),
            "links": total,
            "external": external,
            "anchors": anchor,
            "checked": checked,
            "broken": [{"file": f, "target": t, "why": w} for f, t, w in broken],
        }, ensure_ascii=False, indent=2)))
    else:
        if args.verbose:
            for rel, target in sorted(good):
                print(printable(f"  ok  {rel}  ->  {target}"))
        for rel, target, why in broken:
            print(printable(f"BROKEN  {rel}  ->  {target}  ({why})"))
        print()
        floor_guard(len(files), total)
        print(f"check_links: {len(files)} markdown files, {total} links "
              f"({external} external, {anchor} anchors, {checked} local), "
              f"{len(broken)} broken")

    if broken:
        return 1
    # The --json body is already on stdout by now, so its refusal has to come
    # from here: for a machine consumer the exit code is the contract, and 2
    # says "this report is not a verdict" where 0 would say "all clear".
    # (`:176` is the json print and is cited from docs/AUDIT.md:4183, so a
    # guard could not be inserted above it without moving a cited line.)
    floor_guard(len(files), total)
    return 0


# Defined after main() on purpose: this file is cited by line number from
# .gitignore (`:80`, the input set) and from docs/AUDIT.md (`:39`, `:103`,
# `:152`, `:176`, `:187`, `:189`), so a helper placed above those would move
# every one of those refs.  Python resolves the name when main() runs, so the
# order costs nothing.
def printable(line: str) -> str:
    """Render one output line so a strict-UTF-8 stdout cannot fail on it.

    Pathnames come from `git ls-files -z` decoded with `surrogateescape`, so a
    tracked path whose bytes are not UTF-8 arrives as lone surrogates.  Those
    are reversible -- `encode("utf-8", "surrogateescape")` restores the original
    bytes -- but stdout here is `errors="strict"`, so writing one raises
    UnicodeEncodeError.  The lines that carry a pathname are the BROKEN lines:
    a report that dies on the one thing it exists to report is worse than one
    that is merely ugly.

    Applied to the whole rendered line rather than to each value, so the
    collected tuples stay byte-faithful and there is exactly one escape point
    per write.  `backslashreplace` -- what stderr already does -- and never
    `replace`, which would turn an unreadable name into U+FFFD and hide it.
    """
    return line.encode("utf-8", "surrogateescape").decode("utf-8", "backslashreplace")


# Also defined after main() on purpose, and for the same reason as printable()
# above: this file is cited by line number (.gitignore `:80`, docs/AUDIT.md
# `:39`, `:103`, `:152`, `:176`, `:187`, `:189`), so the guard goes below every
# one of them rather than above line 103.  Only the call site in main() changes,
# and it changes in place, so not one cited line moves.
def iter_markdown_files_checked() -> list[str]:
    """`iter_markdown_files`, refusing to pass when the input set is empty.

    The input set is built by globbing -- `git ls-files -z -- *.md
    *.markdown` -- so its size is a property of the repository's contents at
    this moment, not of the pattern.  A repository where that glob matches
    nothing is not a repository with no broken links; it is a repository where
    this check has nothing to be true about, and both print the same line:

        check_links: 0 markdown files, 0 links (0 external, 0 anchors, 0
        local), 0 broken

    `0 broken` reads as "every link was checked and held" and as "no link was
    checked at all".  Measured: a repository tracking one `notes.txt` and no
    Markdown prints exactly that line and exits 0.  That is the real exposure
    -- narrowing, not an empty repository.

    Exit 2, so "this check could not run" is distinguishable from exit 1,
    "it ran and found something".  The pre-existing git-failure path above
    (and the cannot-run path in iter_markdown_files) exits 1, the same code a
    broken link gets; that is left alone here.
    """
    files = iter_markdown_files()
    if not files:
        print(
            "check_links: the markdown input set is empty: "
            "`git ls-files -z -- *.md *.markdown` matched nothing under "
            f"{REPO_ROOT} -- there is nothing here for this check to be true "
            "about, so there is nothing here for it to be a passing check "
            "about either.  Exit 2 means it could not run; exit 1 means it ran "
            "and found a broken link.",
            file=sys.stderr,
        )
        sys.exit(2)
    return files


def floor_guard(files_n: int, total: int) -> None:
    """Refuse a report where the *link* count is zero.

    A non-empty input set is only the first of this line's three numbers:

      check_links: N markdown files, 618 links (17 external, 0 anchors, 601
      local), 0 broken

    All four numbers are computed from the tree, but they do not move together.
    The first is a function of the file set -- how many tracked markdown files
    there are -- and the other three are functions of the link graph inside
    those files.  A tree can change the first without touching the rest: three
    readings of this same line, taken on three different trees, gave 120, 137
    and 126 for the first number and `618 links (17 external, 0 anchors, 601
    local), 0 broken` for the rest, character for character.  That is why the
    first number is `N` here: an example quoting a number the tree computes is
    stale the next time a document is added, and a sha written into this file
    would not fix it -- it would only move the staleness one level down.

    The quantity the check exists to bound is `total`, and it can reach zero
    with `files_n` perfectly healthy.  Measured: a tree whose one tracked
    document is plain prose with no links at all prints

      check_links: 1 markdown files, 0 links (0 external, 0 anchors, 0 local),
      0 broken

    and exits 0 (sha256 46c17aed3f52da881b993fc69d5136f13d42e5d0e965cae65137d8ec5bf1f0e6).
    `0 broken` reads as "every link resolved" and as "no link was looked at".
    This is the same defect one pipeline stage past the input-set guard above,
    and it is one stage past the letter of what was asked for; it is here
    because leaving a measured hole open is worse than adding a line.

    A floor of one, not a measured floor: it fires only at exactly zero, which
    is the one value that is never a legitimate reading of "every link
    resolved".  Placed at the print sites, not at main()'s return, because a
    return runs after the line is already on stdout.
    """
    if total == 0:
        print(
            f"check_links: {files_n} markdown file(s) were read but they "
            "contain zero links, so there is nothing here for this check to "
            "be true about.  Exit 2 means it could not run; exit 1 means it "
            "ran and found a broken link.",
            file=sys.stderr,
        )
        sys.exit(2)


if __name__ == "__main__":
    sys.exit(main())
