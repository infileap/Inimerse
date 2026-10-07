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
    one case with another and the count is unchanged.  A label digest is the
    layer that covers that, and it is not here yet.
  * A count is not coverage.  This asserts that the set did not move without
    anyone saying so; it does not assert that the set is sufficient.
  * Each harness counts the tables AND checks that its construction still
    produces one case per table row, so a change of shape is caught even when
    the total happens to come out equal.
  * An equality, not a floor.  A count that grew silently is as unreviewed as
    one that shrank.
"""

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
