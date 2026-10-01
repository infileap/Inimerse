#!/usr/bin/env python3
"""check_links.py — verify every relative link in this repository's Markdown.

Absolute URLs are not fetched (that would make the gate depend on the network);
only links that must resolve to a file or directory *inside* the checkout are
checked.  That is the failure mode that actually rots: a file is moved into
`docs/archive/` and a dozen documents keep pointing at where it used to be.

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
import sys
from urllib.parse import unquote, urlsplit

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SKIP_DIRS = {
    ".git", "node_modules", ".worktrees", "target", "userdata",
    "__pycache__", ".venv", "venv", ".pnpm-store",
}
SKIP_PREFIXES = ("build", ".dshm-pr", ".npm")
SKIP_FILES = {"node_modules"}

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
    found = []
    for dirpath, dirnames, filenames in os.walk(REPO_ROOT):
        rel_dir = os.path.relpath(dirpath, REPO_ROOT)
        if rel_dir == ".":
            rel_dir = ""
        dirnames[:] = sorted(
            d for d in dirnames
            if d not in SKIP_DIRS
            and not any(d.startswith(p) for p in SKIP_PREFIXES)
        )
        for name in sorted(filenames):
            if not name.endswith((".md", ".markdown")):
                continue
            if name in SKIP_FILES:
                continue
            found.append(os.path.join(rel_dir, name) if rel_dir else name)
    return found


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

    files = iter_markdown_files()
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
        print(json.dumps({
            "files": len(files),
            "links": total,
            "external": external,
            "anchors": anchor,
            "checked": checked,
            "broken": [{"file": f, "target": t, "why": w} for f, t, w in broken],
        }, ensure_ascii=False, indent=2))
    else:
        if args.verbose:
            for rel, target in sorted(good):
                print(f"  ok  {rel}  ->  {target}")
        for rel, target, why in broken:
            print(f"BROKEN  {rel}  ->  {target}  ({why})")
        print()
        print(f"check_links: {len(files)} markdown files, {total} links "
              f"({external} external, {anchor} anchors, {checked} local), "
              f"{len(broken)} broken")

    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
