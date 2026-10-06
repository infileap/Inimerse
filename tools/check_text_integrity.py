#!/usr/bin/env python3
"""check_text_integrity.py — a text file must not contain a NUL byte.

Why this exists (docs/AUDIT.md §1.55):

Three git-tracked C sources carried NUL bytes inside block comments --
`src/mod/gui_mod.c` (5), `src/lexer/lexer.c` (2), `src/lexer/lexer.h` (1).  The
bytes are inert to the compiler, so nothing was ever red.  They are *not* inert
to the tools that read the sources:

    $ grep -n 'gui_fullscreen' src/mod/gui_mod.c
    1661:static int builtin_gui_fullscreen(VM *vm) {
    grep: src/mod/gui_mod.c: binary file matches

GNU grep decides a file is binary when it meets a NUL byte, and it then stops
listing matches -- but it prints the matches it found *before* that byte, and it
puts the notice on **stderr** while the partial list goes to **stdout**.  Exit
status stays 0.  A reader who sees stdout only, or who does not read the notice,
gets a truncated list that looks complete.  In this instance the two lines that
prove `gui_fullscreen` is registered twice (`:3690`, `:3697`) were the ones
swallowed, and the duplicate is a real defect: `builtin_fullscreen` is
unreachable (docs/AUDIT.md §1.53, docs/API.md:234).

So this is not "the file is unreadable"; it is "the file answers a *shorter*
question than the one asked, with the same shape of answer".  That is the same
defect class this repository keeps recording -- a computation with more than one
production point, a fast path whose gate is taken for the answer -- one level
down, in the tools rather than in the engine.  The fix is to remove the bytes,
not to teach every reader to pass `-a`.

Scope: git-tracked files whose extension marks them as text.  Binary assets
(`.png`, `.ico`, `.icns`, `.bmp`) and the compiler's own bytecode (`.inim`, which
is a serialised program, not source) are out of scope by construction: the
extension list below is an allow-list, so a new binary artefact added to the
repository cannot turn this gate red and force an allow-list edit.

`git ls-files` defines repository content, as in check_links.py and
check_doc_paths.py, and there is no fallback to walking the working tree: an
untracked scratch file must not be able to turn this gate red, and a fallback
would restore exactly that on the machines where a junk-red gate is hardest to
explain.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Extensions this checker owns.  Allow-list, not deny-list -- see the module
# docstring.  `CMakeLists.txt`, `LICENSE` and `Makefile` are matched by name
# below because they have no informative suffix or none at all.
TEXT_SUFFIXES = frozenset(
    {
        ".c",
        ".h",
        ".cc",
        ".cpp",
        ".hpp",
        ".rs",
        ".py",
        ".sh",
        ".bash", ".ps1",
        ".md",
        ".txt",
        ".im",
        ".json",
        ".jsonc",
        ".yml",
        ".yaml",
        ".toml",
        ".ini",
        ".cfg",
        ".cmake",
        ".iss",
        ".ts",
        ".tsx",
        ".js",
        ".mjs",
        ".cjs",
        ".css",
        ".html",
        ".xml",
        ".csv",
        ".gitignore",
        ".gitattributes",
        ".editorconfig",
    }
)

TEXT_NAMES = frozenset({"CMakeLists.txt", "LICENSE", "Makefile", "Dockerfile"})


def tracked_paths() -> list[str]:
    """Every path git tracks, repo-relative, in `git ls-files` order."""
    try:
        proc = subprocess.run(
            ["git", "ls-files", "-z"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        sys.exit(
            f"check_text_integrity: cannot run git ({exc}).\n"
            "check_text_integrity: this stage checks the repository's git-tracked "
            "text files; without git there is no way to distinguish repository "
            "content from scratch files left in the working tree.\n"
            "check_text_integrity: refusing to fall back to listing the working tree."
        )
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        sys.exit(
            f"check_text_integrity: 'git ls-files' failed (exit {proc.returncode}) "
            f"in {REPO_ROOT}\n{detail}\n"
            "check_text_integrity: refusing to fall back to listing the working tree."
        )
    return [n for n in proc.stdout.decode("utf-8", "surrogateescape").split("\0") if n]


def is_text(name: str) -> bool:
    path = Path(name)
    if path.name in TEXT_NAMES or path.name.lower() in TEXT_SUFFIXES:
        return True
    return path.suffix.lower() in TEXT_SUFFIXES


# Why `is_text` compares the full name before the suffix: `Path('.gitignore')`
# has suffix `''` (a leading dot marks a hidden file, not an extension), so a
# suffix-only rule can never reach the dotfile of that name -- and this
# repository tracks five files named that way.  The suffix rule stays: a file
# called `foo.gitignore` does have the suffix `.gitignore`, and dropping the
# rule would put it out of scope.
#
# Allow-list entries that no tracked file matches today.  The list is an
# allow-list for file types this repository has not written yet, so an unused
# entry is not by itself a defect -- but an entry that nothing matches and
# nobody chose is either a typo or a stale entry, and either way it is
# invisible: the checker simply never looks at that kind of file.  Every name
# below was measured on the tree that added this check; adding a file with that
# suffix brings the entry to life and takes it off this list (that is the red
# control for the check below: it fires on an entry nobody stated, and on a
# stated entry that came alive).
NO_FILE_TODAY = frozenset(
    {
        ".bash",
        ".cc",
        ".cfg",
        ".cjs",
        ".cmake",
        ".csv",
        ".editorconfig",
        ".hpp",
        ".ini",
        ".jsonc",
        ".ts",
        ".tsx",
        "Dockerfile",
    }
)


def unmatched_entries(paths: list[str]) -> list[str]:
    """Allow-list entries that no tracked path matches, by name or by suffix.

    Both tests are here for one reason: `.gitignore` is matched by its full
    name and `foo.gitignore` by its suffix, and an entry that neither reaches
    is the one thing this function exists to find.
    """
    names = {Path(p).name for p in paths}
    suffixes = {Path(p).suffix for p in paths}
    return sorted(e for e in TEXT_SUFFIXES | TEXT_NAMES if e not in names and e not in suffixes)


def nul_offsets(path: Path) -> list[int]:
    """Byte offsets of every NUL byte, or [] when the file has none.

    A missing file is reported as an error rather than as "no NUL bytes": a
    tracked path that is not on disk is a different problem, and silently
    counting it as clean would hide it.
    """
    data = path.read_bytes()
    return [i for i, byte in enumerate(data) if byte == 0]


def describe(path: Path, offsets: list[int], limit: int = 3) -> list[str]:
    data = path.read_bytes()
    lines: list[str] = []
    for offset in offsets[:limit]:
        line = data[:offset].count(b"\n") + 1
        lines.append(f"    byte {offset} (line {line})")
    if len(offsets) > limit:
        lines.append(f"    ... and {len(offsets) - limit} more")
    return lines


def main() -> int:
    paths = tracked_paths()
    text = [name for name in paths if is_text(name)]

    dead = unmatched_entries(paths)
    undeclared = [e for e in dead if e not in NO_FILE_TODAY]
    alive = sorted(e for e in NO_FILE_TODAY if e not in dead)
    if undeclared or alive:
        if undeclared:
            print(
                "check_text_integrity: the allow-list has "
                f"{len(undeclared)} entry(ies) that no tracked file matches: "
                + " ".join(undeclared),
                file=sys.stderr,
            )
        if alive:
            print(
                "check_text_integrity: NO_FILE_TODAY names "
                f"{len(alive)} entry(ies) a tracked file matches after all: "
                + " ".join(alive),
                file=sys.stderr,
            )
        print(
            "check_text_integrity: an entry nothing matches is either a typo "
            "or a stale entry, and the file type it names goes unchecked "
            "either way; a stated reason that stopped being true is not a "
            "reason.\n"
            "check_text_integrity: name the entry in NO_FILE_TODAY, or delete "
            "it from the allow-list.",
            file=sys.stderr,
        )
        return 1

    if not text:
        sys.exit(
            "check_text_integrity: 'git ls-files' returned no text file at all, "
            f"out of {len(paths)} tracked path(s).\n"
            "check_text_integrity: a scan that owns nothing cannot report 0 "
            "findings; refusing to pass."
        )

    offenders: list[tuple[str, list[int]]] = []
    for name in text:
        path = REPO_ROOT / name
        try:
            offsets = nul_offsets(path)
        except OSError as exc:
            sys.exit(f"check_text_integrity: cannot read tracked file {name}: {exc}")
        if offsets:
            offenders.append((name, offsets))

    if offenders:
        total = sum(len(offsets) for _, offsets in offenders)
        print(
            f"check_text_integrity: {total} NUL byte(s) in {len(offenders)} of "
            f"{len(text)} tracked text file(s).",
            file=sys.stderr,
        )
        for name, offsets in offenders:
            print(f"  {name}  ({len(offsets)} NUL)", file=sys.stderr)
            for line in describe(REPO_ROOT / name, offsets):
                print(line, file=sys.stderr)
        print(
            "check_text_integrity: a NUL byte makes grep answer a shorter "
            "question with a complete-looking answer -- it lists the matches "
            "before the byte, puts 'binary file matches' on stderr and exits 0. "
            "Replace the byte; do not teach readers to pass -a.\n"
            "check_text_integrity: see docs/AUDIT.md §1.55.",
            file=sys.stderr,
        )
        return 1

    print(
        f"check_text_integrity: {len(text)} text file(s), 0 with NUL bytes; "
        f"{len(TEXT_SUFFIXES)} suffix(es) + {len(TEXT_NAMES)} name(s) in the "
        f"allow-list, {len(dead)} of them match nothing today (stated)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
