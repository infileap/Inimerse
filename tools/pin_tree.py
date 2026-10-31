#!/usr/bin/env python3
"""What a reader can do with the sha a pin records.

A pin names the tree its numbers were measured on, and it is read by somebody who
was not there when it was written.  "The object is in my database" and "a reader
can find it" are two different questions, and only the second one survives a
rebase:

  * `git cat-file -t <sha>` answers resolvability.  A commit a rebase left behind
    still answers here until the local object database is collected, so a pin can
    look healthy on the machine that wrote it;
  * `git for-each-ref --contains <sha>` answers whether any ref holds it.  A
    commit no ref holds is absent from every other clone, so the tree a reader is
    told to go and look at does not exist for them.

The failure is quiet in the worst way: the pin's numbers still recompute and
still match, so a check that only recomputes numbers prints PASS beside a tree
nobody can open.  Measured 2026-11-01, after a rebase and a force-push:
tools/aot_mir_cover.pin.json recorded tree=e2a6655d -- `cat-file -t` said commit,
`for-each-ref --contains` said nothing, and --check-pin recomputed covered=29|of=29
beside it.  The same question, asked of the other pins and of the `[obs: <sha>]`
markers under docs/, found two more instances that nobody had reported (one pin's
tree held alive by a single local branch, and twelve markers on a commit no ref
holds), so this lives in one module rather than in whichever checker found it
first.

Readings: this module prints none of its own.  The caller prints its own token,
because the tool that reads a pin is the one that has to say which pin:

  <TOKEN>|tree=..|resolves=yes|no|refs=N|reachable=yes|no|head=..

`state()` is the pair of questions; `problems()` turns them into the reason
strings a checker prints and fails on; `reading()` is the line above.

    import pin_tree
    st = pin_tree.state(sha, ROOT)
    for why in pin_tree.problems(sha, ROOT):
        print("IR-X-PIN-TREE-PROBLEM|" + why)
    print(pin_tree.reading("IR-X-PIN-TREE", sha, ROOT, head=head_sha()))

`python3 tools/pin_tree.py --self-test` asks the two questions of fixtures whose
answer is known -- including a commit built with `git commit-tree` and given to no
ref, which is the dangling case made on purpose instead of waited for.  The
fixture is built inside `tempfile.mkdtemp()`, so nothing here reads or writes a
path another process shares.  `--check SHA...` prints the same reading for shas
named on the command line and exits 1 if any of them is not lookable -- that is
how the sweep above was run.

Exit codes: 0 when every self-test case got the answer it was written for, and 1
otherwise; `--check` exits 1 when any named sha is not something a reader can look
up.
"""

import argparse
import atexit
import os
import shutil
import subprocess
import sys
import tempfile


def _git(args, root=None):
    return subprocess.run(["git"] + list(args),
                          cwd=str(root) if root else None,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def state(sha, root=None):
    """What a reader can do with `sha`: resolve it, and find a ref that holds it.

    `type` is the object type git reports (None when nothing resolves), and
    `refs` is the list of refs that contain it -- empty is the interesting
    answer, because it means the object is in this database and no other one."""
    if not sha or not isinstance(sha, str):
        return {"type": None, "refs": []}
    t = _git(["cat-file", "-t", sha], root)
    if t.returncode != 0:
        return {"type": None, "refs": []}
    refs = []
    r = _git(["for-each-ref", "--contains", sha, "--format=%(refname)"], root)
    if r.returncode == 0:
        refs = [ln for ln in r.stdout.decode("utf-8", "replace").split("\n") if ln.strip()]
    return {"type": t.stdout.decode("utf-8", "replace").strip(), "refs": refs}


def problems(sha, root=None):
    """The reasons a recorded tree is not something a reader can look up.

    Two reasons, one sentence each, because they fail differently: the first says
    the field does not name a commit at all, the second says it names one that
    only this database has.  Both are failures in the mode that promises the pin
    is usable, not notes printed beside a PASS."""
    st = state(sha, root)
    if st["type"] != "commit":
        return ["the pin records tree=%s, which does not resolve to a commit here -- a reader "
                "cannot look up what was measured" % sha]
    if not st["refs"]:
        return ["the pin records tree=%s, which resolves but is held by no ref -- it survives "
                "only as a dangling object and is absent from a fresh clone" % sha]
    return []


def reading(token, sha, root=None, head=None):
    """The one line a caller prints: what the recorded tree resolves to, and
    whether anybody else can get it."""
    st = state(sha, root)
    return ("%s|tree=%s|resolves=%s|refs=%d|reachable=%s|head=%s"
            % (token, sha, "yes" if st["type"] == "commit" else "no", len(st["refs"]),
               "yes" if st["refs"] else "no", sha if head is None else head))


# --- the self-test's fixture -------------------------------------------------

_FIXTURE = None


def _fixture_git(args, cwd):
    env = dict(os.environ)
    env.update({"GIT_CONFIG_NOSYSTEM": "1", "HOME": cwd,
                "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
                "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z"})
    return subprocess.run(["git", "-c", "user.name=pin_tree",
                           "-c", "user.email=pin_tree@example.invalid",
                           "-c", "commit.gpgsign=false"] + list(args),
                          cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          env=env)


def fixture():
    """A repository built for the self-test: one commit a ref holds, one it does not.

    `git commit-tree` writes a commit and updates no ref, which is exactly the
    state a rebase or a force-push leaves behind -- so the dangling case is made
    here instead of waited for.  Built under `tempfile.mkdtemp()`, and the caller
    never sees the path except through this dict."""
    global _FIXTURE
    if _FIXTURE:
        return _FIXTURE
    d = tempfile.mkdtemp(prefix="pin_tree.fixture.")
    atexit.register(shutil.rmtree, d, True)
    _fixture_git(["init", "-q", "."], d)
    with open(os.path.join(d, "f.txt"), "w") as fh:
        fh.write("x\n")
    _fixture_git(["add", "f.txt"], d)
    _fixture_git(["commit", "-q", "-m", "a commit a ref holds"], d)
    held = _fixture_git(["rev-parse", "HEAD"], d).stdout.decode().strip()
    tree = _fixture_git(["rev-parse", "HEAD^{tree}"], d).stdout.decode().strip()
    dangling = _fixture_git(["commit-tree", tree, "-m", "a commit no ref holds"],
                            d).stdout.decode().strip()
    _FIXTURE = {"root": d, "held": held, "dangling": dangling, "tree": tree}
    return _FIXTURE


def self_test():
    """The two questions on inputs whose answer is known, including the negative
    control: a commit that resolves and that no ref holds."""
    bad = []
    cases = []

    def expect(name, got, want):
        red = got != want
        cases.append(name)
        print("PIN-TREE-SELFTEST|case=%s|want=%s|got=%s|red=%s"
              % (name, want, got, "yes" if red else "no"))
        if red:
            bad.append(name)

    fx = fixture()
    held = state(fx["held"], fx["root"])
    dang = state(fx["dangling"], fx["root"])

    expect("a commit a ref holds resolves", held["type"], "commit")
    expect("...and some ref contains it", len(held["refs"]) > 0, True)
    expect("...so it is not a problem", problems(fx["held"], fx["root"]), [])
    expect("a commit no ref holds still resolves in the database that made it",
           dang["type"], "commit")
    expect("...and no ref contains it, so no other clone has it", dang["refs"], [])
    expect("...and that is a problem, stated once", len(problems(fx["dangling"], fx["root"])), 1)
    expect("the reason names the sha it is about",
           fx["dangling"] in problems(fx["dangling"], fx["root"])[0], True)
    expect("a tree object is not a commit", state(fx["tree"], fx["root"])["type"], "tree")
    expect("...and is refused for that reason, not for reachability",
           "does not resolve to a commit" in problems(fx["tree"], fx["root"])[0], True)
    expect("a sha nothing has is not a pin", state("0" * 40, fx["root"])["type"], None)
    expect("...and is refused", len(problems("0" * 40, fx["root"])), 1)
    expect("an empty field is refused", len(problems("", fx["root"])), 1)
    expect("a missing field is refused", len(problems(None, fx["root"])), 1)
    expect("a non-string field is refused", len(problems(12345, fx["root"])), 1)
    expect("the reading says a dangling tree is not reachable",
           reading("TOK", fx["dangling"], fx["root"], head="0" * 40),
           "TOK|tree=%s|resolves=yes|refs=0|reachable=no|head=%s" % (fx["dangling"], "0" * 40))
    expect("the reading says a held tree is reachable",
           reading("TOK", fx["held"], fx["root"]),
           "TOK|tree=%s|resolves=yes|refs=%d|reachable=yes|head=%s"
           % (fx["held"], len(held["refs"]), fx["held"]))

    print("PIN-TREE-SELFTEST-VERDICT|cases=%d|red=%d|verdict=%s"
          % (len(cases), len(bad), "FAIL" if bad else "PASS"))
    if bad:
        print("pin_tree self-test: FAIL -- " + "; ".join(bad), file=sys.stderr)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true",
                    help="ask the two questions of fixtures whose answer is known")
    ap.add_argument("--check", action="append", default=[], metavar="SHA",
                    help="print the reading for this sha and fail if it is not lookable")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if args.check:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        bad = 0
        for sha in args.check:
            print(reading("PIN-TREE", sha, root))
            for why in problems(sha, root):
                print("PIN-TREE-PROBLEM|" + why)
                bad += 1
        return 1 if bad else 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
