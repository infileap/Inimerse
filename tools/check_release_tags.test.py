#!/usr/bin/env python3
"""check_release_tags.test.py -- the two ways this checker must say NO, and one it must stop saying.

Why a test file: the checker's whole value is the NO it prints when a tag moves,
and the regression that motivated this file was a NO printed when nothing moved --
`v0.5.1 -> 4e444dd` was reported as drift while `v0.5.1` still pointed at 4e444dd,
because git's automatic abbreviation length grew from 7 to 8 characters as the object
count grew and the checker compared the recorded STRING to `rev-parse --short`.

So there are two kinds of cell here, and neither is a re-run of the corpus:

  DRIFT THAT IS NOT DRIFT   a document records a prefix SHORTER than today's
                            `--short` prints, but the prefix resolves to the very
                            commit the tag names.  Must be GREEN.
  THE NOS IT MUST KEEP      a record naming a different commit, a record whose
                            abbreviation is ambiguous (`rev-parse` refuses it),
                            and a README baseline naming an older tag.  Must be
                            RED -- each for its own reason, each asserted.

Every cell builds its own throwaway repository under a temp directory (a tag is
moved by writing a new tag object; nothing here touches the checkout) and copies
the checker into it, because the checker anchors itself to the repository that
contains it.  `EXP_TAG_BINDINGS=1` and `EXP_TAG_EXEMPTION_CLASSES=` are set so a
one-binding fixture is not failed by the floors that exist for the real tree.

Usage: python3 tools/check_release_tags.test.py [-v]
Exit: 0 every cell as expected, 1 some cell differed.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "check_release_tags.py"

# Content chosen so two blobs share a SEVEN-character sha1 prefix (the binding
# shape accepts 7-40 hex, so a shorter probe would not be a binding at all).
# git resolves an abbreviation only when it is unique, so the shared prefix is
# REFUSED rather than silently widened -- which is the behaviour under test.
_AMBIGUOUS = None


def _blob_sha1(data):
    import hashlib
    h = hashlib.sha1()
    h.update(b"blob %d\0" % len(data))
    h.update(data)
    return h.hexdigest()


def ambiguous_prefix():
    global _AMBIGUOUS
    if _AMBIGUOUS is not None:
        return _AMBIGUOUS
    seen = {}
    i = 0
    while True:
        data = b"prefix-probe-%d\n" % i
        sha = _blob_sha1(data)
        key = sha[:7]
        if key in seen:
            _AMBIGUOUS = (key, seen[key], data)
            return _AMBIGUOUS
        seen[key] = data
        i += 1


def run(args, cwd=None):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


class Fixture:
    def __init__(self):
        self.dir = Path(tempfile.mkdtemp(prefix="reltags-"))
        self.repo = self.dir / "repo"
        self.repo.mkdir()
        self.run = lambda *a: run(list(a), cwd=self.repo)
        self.run("git", "init", "-q")
        self.run("git", "config", "user.email", "t@example")
        self.run("git", "config", "user.name", "t")
        for sub in ("docs", "tools"):
            (self.repo / sub).mkdir()
        shutil.copy(TOOL, self.repo / "tools" / TOOL.name)
        self.commit1 = self.commit("one")
        self.run("git", "tag", "v0.9.1", self.commit1)
        self.commit2 = self.commit("two")
        self.run("git", "tag", "v0.9.2", self.commit2)
        self.write_readme("0.9.2")
        self.write_docs("")

    def commit(self, text):
        (self.repo / "f.txt").write_text(text)
        self.run("git", "add", "f.txt")
        self.run("git", "commit", "-q", "-m", text)
        return self.run("git", "rev-parse", "HEAD").stdout.strip()

    def write_readme(self, version):
        (self.repo / "README.md").write_text("当前发布基线：**%s**\n" % version)

    def write_docs(self, binding):
        # The checker walks `git ls-files docs/`, so an untracked fixture is
        # invisible to it -- the fixture must commit the document it wants read.
        (self.repo / "docs" / "D.md").write_text(
            "| 版本 | `v0.9.1` = `%s` |\n" % binding if binding else "nothing\n")
        self.run("git", "add", "docs/D.md")
        self.run("git", "commit", "-q", "-m", "docs: record")

    def check(self):
        env = dict(os.environ, EXP_TAG_BINDINGS="1", EXP_TAG_EXEMPTION_CLASSES="")
        r = subprocess.run([sys.executable, str(self.repo / "tools" / TOOL.name)],
                           cwd=self.repo, capture_output=True, text=True, env=env)
        return r.returncode, r.stdout + r.stderr

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


CELLS = []


def cell(fn):
    CELLS.append(fn)
    return fn


@cell
def drift_that_is_not_drift_is_green():
    """A record SHORTER than today's --short, resolving to the tag's own commit."""
    f = Fixture()
    try:
        # core.abbrev=8 forces the printed form wider than the record.  That is
        # the same class as the object count growing: the PRINTED FORM moved,
        # the pointer did not.
        f.run("git", "config", "core.abbrev", "8")
        short = f.run("git", "rev-parse", "--short", "v0.9.1").stdout.strip()
        assert len(short) == 8, short
        record = short[:7]
        f.write_docs(record)
        rc, out = f.check()
        assert rc == 0, "record %r resolves to v0.9.1, expected exit 0, got %d\n%s" % (record, rc, out)
        assert "every binding agrees" in out, out
    finally:
        f.close()


@cell
def record_of_a_different_commit_is_red():
    f = Fixture()
    try:
        f.write_docs(f.run("git", "rev-parse", "--short", "v0.9.2").stdout.strip())
        rc, out = f.check()
        assert rc == 1, "a record naming another commit must exit 1, got %d\n%s" % (rc, out)
        assert "records `v0.9.1`" in out and "FAILED" in out, out
    finally:
        f.close()


@cell
def ambiguous_abbreviation_is_red_not_widened():
    """`rev-parse --verify --quiet` must REFUSE an ambiguous prefix, not retry longer."""
    f = Fixture()
    try:
        key, _data_a, data_b = ambiguous_prefix()
        for data in (_data_a, data_b):
            p = subprocess.run(["git", "hash-object", "-w", "-t", "blob", "--stdin"],
                               input=data, cwd=f.repo, capture_output=True)
            assert p.returncode == 0, p.stderr
        f.write_docs(key)
        rc, out = f.check()
        assert rc == 1, "an ambiguous record must exit 1, got %d\n%s" % (rc, out)
        assert "records `v0.9.1` -> `%s`" % key in out, out
        # The fix's message names the situation; the failing comparison does not.
        assert "does not resolve to the commit" in out, out
        assert "ambiguous" in out, out
    finally:
        f.close()


@cell
def baseline_naming_an_older_tag_is_red():
    f = Fixture()
    try:
        f.write_readme("0.9.1")
        rc, out = f.check()
        assert rc == 1, "a README baseline behind the newest tag must exit 1, got %d\n%s" % (rc, out)
        assert "0.9.1" in out and "FAILED" in out, out
    finally:
        f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    failed = 0
    for fn in CELLS:
        try:
            fn()
            print("ok   %s" % fn.__name__)
        except AssertionError as e:
            failed += 1
            print("FAIL %s: %s" % (fn.__name__, e))
    print("%d/%d cells" % (len(CELLS) - failed, len(CELLS)))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
