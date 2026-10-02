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


def scan_files() -> list[str]:
    """README.md plus the non-recursive contents of docs/ and future/."""
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
    return files


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
    broken: list[tuple[str, str]] = []
    good: list[tuple[str, str]] = []

    for rel in files:
        try:
            with open(os.path.join(REPO_ROOT, rel), encoding="utf-8") as fh:
                text = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            broken.append((rel, f"<unreadable: {exc}>"))
            continue
        for path in doc_paths_in(text):
            total += 1
            if os.path.exists(os.path.join(REPO_ROOT, path)):
                good.append((rel, path))
            else:
                broken.append((rel, path))

    if args.json:
        print(json.dumps({
            "files": len(files),
            "references": total,
            "broken": [{"file": f, "path": p} for f, p in broken],
        }, ensure_ascii=False, indent=2))
    else:
        if args.verbose:
            for rel, path in sorted(set(good)):
                print(f"  ok  {rel}  ->  {path}")
        for rel, path in broken:
            print(f"BROKEN  {rel}  ->  {path}  (no such file)")
        print()
        print(f"check_doc_paths: {len(files)} markdown files, {total} backtick "
              f"`docs/…`/`future/…` .md references, {len(broken)} broken")

    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
