#!/usr/bin/env python3
"""check_doc_paths.py — verify `docs/…` / `future/…` references written in prose.

Why this exists (docs/streams/docs-audit.md §3.2, docs/BOARD.md §3):

`tools/check_links.py` strips *inline code spans* before scanning for links, for
a good reason: a span such as ``object["name"](...)`` would otherwise be parsed
as a Markdown link (15 false positives when the rule was added).  But this
repository writes most of its cross-document references *inside* backticks, so
that same stripping rule makes check_links.py blind to an entire failure class.
At the time this checker was written, `docs/REQUIREMENTS_ANALYSIS.md` cited 22
`docs/<name>.md` paths that had been moved into `docs/archive/`, and
`check_links.py` still reported `0 broken`.  **A green check_links.py is not
evidence that the paths in the prose resolve.**

This is the complement, not a replacement: it looks *only* inside inline code
spans and verifies that each `docs/…` / `future/…` reference that ends in `.md`
resolves to a file in the checkout.

Scope — deliberately narrower than "every .md file in the repository":

  scanned  README.md, docs/*.md, future/*.md
           (the delivered documentation set)
  skipped  docs/archive/, future/archive/
           frozen history: archived files must keep quoting the paths as they
           were when the text was written (docs/BOARD.md §4, stream brief §6)
  skipped  docs/streams/
           internal per-stream work orders, not delivered docs; they quote
           broken paths *on purpose*, as the subject of the audit
           (docs/streams/docs-audit.md §3.1 tabulates 22 broken references)
  skipped  everything else (tools/, Infiverse_standard/, repository root, …)
           those are other streams' conflict domains (docs/BOARD.md §4/§5):
           e.g. tools/dsh-inimerse/marketplace/README.md and root files such as
           icon_spec.md are not this stream's to edit

Only paths ending in `.md` are checked.  Prose that names a *proposed*
directory or a version shorthand — e.g. `docs/infiverse.protocol.v1/` (RFC that
STATUS.md §9.3 explicitly declines to create) or `docs/RELEASE_0.2.0`–`0.4.1`
— is not a file reference and must not be reported as a broken one.

Usage:
    python3 tools/check_doc_paths.py            # exit 1 if any path is broken
    python3 tools/check_doc_paths.py -v         # list every path that resolves
    python3 tools/check_doc_paths.py --json     # machine-readable report
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Inline code span: a run of N backticks closed by the same run of N.
# (Same expression as tools/check_links.py:39; kept textually identical so the
# two checkers agree on what "inside a backtick" means.)
INLINE_CODE_RE = re.compile(r"(`+)(?:(?!\1).)*\1", re.S)
FENCE_RE = re.compile(r"^(\s*)(```|~~~)")

# A `docs/…` or `future/…` reference that names a Markdown file.  The lookbehind
# keeps longer or differently-anchored tokens out, and the trailing group keeps
# a match from stopping inside a longer path segment.  CJK is in the character
# class because several archived docs have Chinese file names
# (docs/archive/工作台使用教程.md); without it the checker would silently skip
# those references instead of reporting them.
DOC_PATH_RE = re.compile(
    r"(?<![\w./-])((?:docs|future)/[A-Za-z0-9_.\-/\u4e00-\u9fff]*\.md)(?![\w-])"
)


def tracked_paths() -> set[str]:
    """Repo-relative paths git tracks under README.md, docs/ and future/.

    Used to *filter* scan_files(), never to replace it.  The split matters:
    scan_files() decides which documents this checker owns -- deliberately not
    docs/archive/, docs/streams/ or the repository root (see the module
    docstring) -- and that scope is not expressible as a pathspec, because git's
    `docs/*.md` matches `docs/archive/*.md` too (`*` crosses `/` in a pathspec;
    measured: 51 matches, 6 of them non-recursive).

    What git *is* used for is hermeticity.  A file that is not tracked is not
    repository content, so it must not be able to turn this gate red.  This
    checker is bounded to two directories rather than the whole tree, which is
    why the repo-root `.verify/` incident never reached it -- but the same defect
    class was reachable one directory over and was measured: an untracked
    `docs/<name>.md` carrying a broken backtick reference produced
    `1 broken` / exit 1 against a repository that was fine.

    As in check_links.py there is no fallback to listing the directory when git
    is missing: a fallback would restore the defect precisely on the machines
    where a junk-red gate is hardest to explain.
    """
    try:
        proc = subprocess.run(
            ["git", "ls-files", "-z", "--", "README.md", "docs", "future"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        sys.exit(
            f"check_doc_paths: cannot run git ({exc}).\n"
            "check_doc_paths: this stage checks the repository's git-tracked "
            "documents; without git there is no way to distinguish repository "
            "content from scratch files left in the working tree.\n"
            "check_doc_paths: refusing to fall back to listing the working tree."
        )
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        sys.exit(
            f"check_doc_paths: 'git ls-files' failed (exit {proc.returncode}) in "
            f"{REPO_ROOT}\n{detail}\n"
            "check_doc_paths: refusing to fall back to listing the working tree."
        )
    return {
        name
        for name in proc.stdout.decode("utf-8", "surrogateescape").split("\0")
        if name
    }


def scan_files() -> list[str]:
    """README.md plus the non-recursive contents of docs/ and future/.

    The candidates come from the directory listing (that is what defines this
    checker's scope); the tracked-path filter then drops anything that is not
    part of the repository.  For a tracked file the result is identical to the
    listing alone, which is why this changes no existing verdict.
    """
    tracked = tracked_paths()
    files = []
    if os.path.isfile(os.path.join(REPO_ROOT, "README.md")):
        files.append("README.md")
    for tree in ("docs", "future"):
        absdir = os.path.join(REPO_ROOT, tree)
        if not os.path.isdir(absdir):
            continue
        for name in sorted(os.listdir(absdir)):
            if name.endswith(".md") and os.path.isfile(os.path.join(absdir, name)):
                files.append(f"{tree}/{name}")
    return [f for f in files if f in tracked]


def strip_code_fences(text: str) -> str:
    """Blank out fenced code blocks: an example command is not a reference."""
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


def doc_paths_in(text: str) -> list[str]:
    """Every `docs/…`/`future/…` .md path that appears inside an inline span."""
    body = strip_code_fences(text)
    found = []
    for span in INLINE_CODE_RE.finditer(body):
        for m in DOC_PATH_RE.finditer(span.group(0)):
            found.append(m.group(1))
    return found


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also list every reference that resolves")
    ap.add_argument("--json", action="store_true", help="emit a JSON report")
    args = ap.parse_args()

    files = scan_files()
    total = 0
    broken: list[tuple[str, str, str]] = []
    good: list[tuple[str, str]] = []

    for rel in files:
        try:
            with open(os.path.join(REPO_ROOT, rel), encoding="utf-8") as fh:
                text = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            broken.append((rel, "<unreadable>", str(exc)))
            continue
        for path in doc_paths_in(text):
            total += 1
            if os.path.exists(os.path.join(REPO_ROOT, path)):
                good.append((rel, path))
            else:
                broken.append((rel, path, "no such file"))

    if args.json:
        print(printable(json.dumps({
            "files": len(files),
            "references": total,
            "broken": [{"file": f, "path": p, "why": w} for f, p, w in broken],
        }, ensure_ascii=False, indent=2)))
    else:
        if args.verbose:
            for rel, path in sorted(set(good)):
                print(printable(f"  ok  {rel}  ->  {path}"))
        for rel, path, why in broken:
            print(printable(f"BROKEN  {rel}  ->  {path}  ({why})"))
        print()
        print(f"check_doc_paths: {len(files)} markdown files, {total} backtick "
              f"`docs/…`/`future/…` .md references, {len(broken)} broken")

    return 1 if broken else 0


# Defined after main() on purpose: this file is cited by line number from
# .gitignore (`:99`, the input set) and from docs/AUDIT.md (`:124`, `:188`,
# `:211`), so a helper placed above those would move every one of those refs.
# Python resolves the name when main() runs, so the order costs nothing.
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


if __name__ == "__main__":
    sys.exit(main())
