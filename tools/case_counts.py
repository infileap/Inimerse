#!/usr/bin/env python3
"""A case count is a decision, not a number that may drift on its own.

`tools/gate.sh` pins the fuzz counts with absolute `EXP_FUZZ_*` constants.  The
three semantic harnesses print how many cases they ran, and until now nothing
compared that number with anything: delete thirty `logic_semantics` cases and
the harness still prints `ok (34 cases ...)`, still exits 0, and the stage is
green while the coverage is gone.

This module does not add another absolute.  A constant saying "the count is 34"
has to be bumped by hand for every honest addition, and whoever deletes cases
can bump it in the same breath -- either way the number itself is a copy that
nothing compares.  What is asserted here is a DELTA against a named base tree:
the count may change, but only in a commit that says so.

The base is a sha, not `HEAD~1`.  A moving target is not a judgement; a sha is
a sentence someone wrote, and changing it is an action that shows up in a diff.

The base count is written down nowhere.  `git show <base>:<harness>` is loaded
and the SAME tables are counted on both sides, so this module holds no 16, no
64 and no 104 -- it holds the base tree's own answer.  A copy drifts and then
lies; a reading taken from the tree it names cannot.

Boundaries, stated rather than implied:

  * A delta catches deletion and addition.  It does NOT catch a swap: replace
    one case with another -- a strong assertion for a weaker one -- and the
    count is unchanged while the coverage is gone.  `expect_labels` is the
    layer that covers it: it compares the SET of case identities against the
    same base tree, so a swap lands as one added plus one removed even though
    the total never moved.  A digest of that set is printed for the record; it
    is not what is asserted, because a digest can only say "different" while a
    set difference says which case moved.
  * A count is not coverage.  This asserts that the set did not move without
    anyone saying so; it does not assert that the set is sufficient.
  * Each harness counts the tables AND checks that its construction still
    produces one case per table row, so a change of shape is caught even when
    the total happens to come out equal.
  * An equality, not a floor.  A count that grew silently is as unreviewed as
    one that shrank.
  * A label is only as stable as the text it is written as.  The convention is
    below; a label that drifts on its own makes every reading about it a
    reading about nothing.
"""

import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)

# The tree the counts are compared against.  Change it only in a commit that
# also changes what it means: the counts were last decided here.
BASE = "930d8fac7a6c6804d6a8b8f621f97225c877463b"


def _git(*args):
    return subprocess.run(["git"] + list(args), cwd=ROOT,
                          capture_output=True, text=True)


def _assert_base_is_ancestor():
    if _git("merge-base", "--is-ancestor", BASE, "HEAD").returncode != 0:
        sys.stderr.write(
            "case_counts: base %s is not an ancestor of HEAD.\n"
            "case_counts: a base that is not in this history measures a different tree,\n"
            "case_counts: so the delta would be a number about nothing.  Name the commit\n"
            "case_counts: the counts were last decided in.\n" % BASE[:12])
        raise SystemExit(2)


def expect_equal(a, b, what):
    """A shape check inside a harness: the total and its parts must agree."""
    if a != b:
        sys.stderr.write(
            "case_counts: %s: %d != %d -- the construction and the tables it is built\n"
            "case_counts: from no longer describe the same set.\n" % (what, a, b))
        raise SystemExit(2)


def _load(path):
    spec = importlib.util.spec_from_file_location("case_counts_base", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def count(path, rows, expected_delta, label, current=None):
    """Assert the harness at `path` has `expected_delta` more rows than BASE.

    `rows(module)` returns the number of cases in that module's tables; it is
    applied to the base module and to the running one, so both sides are
    counted by the same expression.
    """
    _assert_base_is_ancestor()
    here = os.path.abspath(path)
    rel = os.path.relpath(here, ROOT)
    got = _git("show", "%s:%s" % (BASE, rel))
    if got.returncode != 0:
        sys.stderr.write("case_counts: cannot read %s from base %s: %s\n"
                         % (rel, BASE[:12], got.stderr.strip()))
        raise SystemExit(2)

    # Next to the original, so that anything the harness derives from its own
    # location is still derived correctly while it is imported.
    fd, tmp = tempfile.mkstemp(prefix=".case_counts_base_", suffix=".py",
                               dir=os.path.dirname(here))
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(got.stdout)
        base_n = rows(_load(tmp))
    finally:
        os.unlink(tmp)
    cur_n = rows(current if current is not None else sys.modules["__main__"])

    delta = cur_n - base_n
    behind = _git("rev-list", "--count", "%s..HEAD" % BASE).stdout.strip() or "?"
    if delta != expected_delta:
        sys.stderr.write(
            "case_counts: %s has %d %s, base %s has %d -- delta %+d, expected %+d.\n"
            % (rel, cur_n, label, BASE[:12], base_n, delta, expected_delta))
        sys.stderr.write(
            "case_counts: a case count is a decision.  If the move is intended, change the\n"
            "case_counts: expected delta in the same commit; if it is not, this is exactly\n"
            "case_counts: the silent loss this pin exists to stop.\n")
        raise SystemExit(1)
    print("case_counts: %s %d (base %d @ %s, %s commit(s) behind HEAD) delta %+d (expect %+d)"
          % (label, cur_n, base_n, BASE[:12], behind, delta, expected_delta))
    return cur_n


# ------------------------------------------------------------ case identities
#
# What a case IS, per harness.  The count above is blind to a swap, so the
# identity of every case is taken from the same tables and compared with the
# base tree's own identities.
#
# The stabilization convention, written down here because a digest that drifts
# is a digest that says nothing:
#
#   * sorting: Python's default string order (by code point), via `sorted()`
#   * separator: one "\n" between labels, no trailing newline
#   * truncation: the first 12 hex characters of the sha256 hexdigest
#   * the label text must be stable on its own: no `repr()` of a sentinel
#     object (it prints a memory address), no absolute paths, no timestamps,
#     no counts, no ordering.  A sentinel expectation is spelled `REFUSED`.
#
# The identities are read from the BASE module too, so they may only use names
# that version already has.  The tables below have been module-level in every
# version of these three harnesses, which is what makes that possible.
CASE_LABELS = {
    "mod_semantics.test.py": lambda m: [
        "TABLE %s => %s" % (expr, _want(want, m)) for expr, want in m.TABLE
    ],
    "logic_semantics.test.py": lambda m: (
        ["TABLE say %s => %s" % (expr, _want(want, m)) for expr, want in m.TABLE]
        + ["COND if %s => %s" % (expr, _want(want, m)) for expr, want in m.COND]
        + ["SHORT_CIRCUIT short-circuit: %s => %s" % (expr, _want(want, m))
           for expr, want in m.SHORT_CIRCUIT]
        + ["CHAIN %s => %s" % (label, _want(want, m))
           for label, _src, want in m.CHAIN]
        + ["FLOAT say %s => %s" % (expr, _want(want, m)) for expr, want in m.FLOAT]
    ),
    "aot_native.test.py": lambda m: (
        ["EQUIVALENCE %s %r => %r" % (name, src, want)
         for name, src, want in m.EQUIVALENCE]
        + ["DIVERGENCE %s %r => %r|%r" % (name, src, want_interp, want_native)
           for name, src, want_interp, want_native in m.DIVERGENCE]
        + ["RUNTIME_ERROR %s %r => %s" % (name, src, kind)
           for name, src, kind in m.RUNTIME_ERROR]
        + ["REFUSAL %s %r" % (name, src) for name, src in m.REFUSAL]
    ),
}


def _want(want, module):
    """Spell an expected value.  A sentinel must not leak its repr()."""
    return "REFUSED" if want is getattr(module, "REFUSED", None) else want


def labels_for(path, module):
    """The case identities of one harness, or a refusal if it has none."""
    key = os.path.basename(path)
    fn = CASE_LABELS.get(key)
    if fn is None:
        sys.stderr.write(
            "case_counts: no label definition for %s -- a harness whose cases have no\n"
            "case_counts: identity is exempt from the swap check in silence.  Add it to\n"
            "case_counts: CASE_LABELS, or state why a swap there cannot happen.\n" % key)
        raise SystemExit(2)
    return fn(module)


def digest(labels):
    """The digest of a label set -- see the convention above for its shape."""
    return hashlib.sha256("\n".join(sorted(labels)).encode("utf-8")).hexdigest()[:12]


def expect_labels(path, label, expected_added=0, expected_removed=0, current=None):
    """Assert the harness at `path` has the same case identities as BASE.

    `expected_added`/`expected_removed` are the moves a commit says it makes;
    they default to none, because an unreviewed swap is exactly what this is
    for.
    """
    _assert_base_is_ancestor()
    here = os.path.abspath(path)
    rel = os.path.relpath(here, ROOT)
    got = _git("show", "%s:%s" % (BASE, rel))
    if got.returncode != 0:
        sys.stderr.write("case_counts: cannot read %s from base %s: %s\n"
                         % (rel, BASE[:12], got.stderr.strip()))
        raise SystemExit(2)

    fd, tmp = tempfile.mkstemp(prefix=".case_counts_base_", suffix=".py",
                               dir=os.path.dirname(here))
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(got.stdout)
        base_labels = list(labels_for(rel, _load(tmp)))
    finally:
        os.unlink(tmp)
    cur_labels = list(labels_for(
        rel, current if current is not None else sys.modules["__main__"]))

    for who, ls in (("base", base_labels), ("HEAD", cur_labels)):
        dups = sorted(x for x in set(ls) if ls.count(x) > 1)
        if dups:
            sys.stderr.write(
                "case_counts: %s: %s has %d duplicate label(s), e.g. %r -- two cases\n"
                "case_counts: written the same way are one case to this pin.\n"
                % (label, who, len(dups), dups[:3]))
            raise SystemExit(2)

    base_set, cur_set = set(base_labels), set(cur_labels)
    added = sorted(cur_set - base_set)
    removed = sorted(base_set - cur_set)
    behind = _git("rev-list", "--count", "%s..HEAD" % BASE).stdout.strip() or "?"
    if len(added) != expected_added or len(removed) != expected_removed:
        sys.stderr.write(
            "case_counts: %s labels moved: %d added, %d removed (expected %+d/%+d).\n"
            % (rel, len(added), len(removed), expected_added, expected_removed))
        for x in removed[:5]:
            sys.stderr.write("case_counts:   removed: %s\n" % x)
        for x in added[:5]:
            sys.stderr.write("case_counts:   added:   %s\n" % x)
        sys.stderr.write(
            "case_counts: a count cannot see a swap: replace one case with another and the\n"
            "case_counts: total never moves while the coverage is gone.  If the move is\n"
            "case_counts: intended, write the expected numbers in the same commit.\n")
        raise SystemExit(1)
    print("case_counts: %s labels=%s (base %s @ %s, %s commit(s) behind HEAD) "
          "%d label(s), added %d, removed %d (expect %+d/%+d)"
          % (label, digest(cur_labels), digest(base_labels), BASE[:12], behind,
             len(cur_labels), len(added), len(removed), expected_added, expected_removed))
    return len(cur_labels)
