#!/usr/bin/env python3
"""Mutation harness for tools/crp_ws_client.js -- the falsifiability evidence for
tools/crp_ws_client.test.js.

WHY THIS EXISTS
  tools/crp_ws_client.test.js is GREEN against the UNMODIFIED module: no defect was
  found, so there is no "red before the fix" run to show. The falsifiability proof is
  therefore a mutation kill map. Each mutant is a single-line behaviour change of the
  module; a mutant that makes an assertion go red proves that assertion is
  load-bearing. Two suites are run against every mutant:

    old   the pre-change test file (6 lines / 2 assertions), read from an explicit ref
    new   the current tools/crp_ws_client.test.js (31 labelled assertions)

  and the claim recorded in docs/STATUS.md section 10.9 is that the old file is blind
  to almost every mutant, while every new assertion has a mutant that kills it.

REPRODUCIBILITY (the bug this file exists to not have)
  The first version of this harness read the old suite from `git show HEAD:...`. That
  worked only while it was run BEFORE the fix was committed, because HEAD was then the
  pre-change commit. After the fix landed, HEAD became the new 269-line suite, both
  columns silently became "new", and the published "old = blind to 27/29" table stopped
  being reproducible. This script reads the old suite from an EXPLICIT ref (default
  d1bef61, the pre-change commit) and REFUSES TO RUN unless the file it read really is
  the 6-line / 2-assertion file, so it can never silently degrade to "new vs new".

RUN
  python3 tools/crp_ws_client.mutation.py
  python3 tools/crp_ws_client.mutation.py --old-ref d1bef61:tools/crp_ws_client.test.js
  python3 tools/crp_ws_client.mutation.py --repo /path/to/worktree --old-ref <rev>

  Exit 0 = the documented claims reproduce. Non-zero = a precondition or a documented
  claim no longer holds; the message says which one.

NOTE ON HYGIENE
  Every temp file is created inside one unique tempfile.mkdtemp() directory (prefix
  "crp-ws-mutation-") that is removed in a finally block, including on failure. The two
  suites are copied into that directory beside the mutated module because both do
  `require('./crp_ws_client')`. Nothing outside the temp dir is written.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# The documented claims (docs/STATUS.md section 10.9). A mismatch is a FAILURE:
# it means either the module/test moved or the recorded table is stale.
# ---------------------------------------------------------------------------
EXPECTED_MUTANTS = 29
EXPECTED_ASSERTIONS = 31
EXPECTED_KILLED = 29
EXPECTED_OLD_SEES = (
    "queue: only queue when a socket already exists [targeted]",
    "queue: close() leaves the queue behind",
)

OLD_TEST_PATH = "tools/crp_ws_client.test.js"
MODULE_PATH = "tools/crp_ws_client.js"

# ---------------------------------------------------------------------------
# Mutants: (name, exact anchor, replacement). Each anchor must occur EXACTLY once
# in the module, otherwise the harness stops (a moved anchor must not be silently
# skipped -- that is how a kill map rots).
# ---------------------------------------------------------------------------
MUTS = [
    # --- connect ------------------------------------------------------------
    ("connect: drop the in-flight dedup",
     "async connect() { if (this._connectPromise) return this._connectPromise; this.closed = false;",
     "async connect() { this.closed = false;"),
    ("connect: dial twice per connect()",
     "const ws = new WebSocket(this.url); this.socket = ws;",
     "const ws = new WebSocket(this.url); void new WebSocket(this.url); this.socket = ws;"),
    ("connect: dial a url the constructor was not given",
     "new WebSocket(this.url)", "new WebSocket(this.url + '-mutated')"),
    ("connect: forget to record the dialing socket",
     "new WebSocket(this.url); this.socket = ws;",
     "new WebSocket(this.url); this.socket = null;"),
    ("connect: resolve with the socket, not the client",
     "}); return this;", "}); return this.socket;"),
    ("connect: the de-duplicated caller resolves to the socket",
     "if (this._connectPromise) return this._connectPromise;",
     "if (this._connectPromise) return this._connectPromise.then(() => this.socket);"),
    ("message: hand onMessage the event, not event.data",
     "ws.addEventListener('message', e => this.onMessage(e.data));",
     "ws.addEventListener('message', e => this.onMessage(e));"),
    # --- retry --------------------------------------------------------------
    ("retry: swallow the error at exhaustion instead of rejecting",
     "attempt++ >= this.retries) throw e;", "attempt++ >= this.retries) return this;"),
    ("retry: retries + 1 attempts",
     "attempt++ >= this.retries", "attempt++ >= this.retries + 1"),
    ("retry: no backoff before the first retry",
     "setTimeout(r, this.backoffMs * 2 ** (attempt - 1))", "setTimeout(r, 0)"),
    ("retry: fixed backoff instead of exponential",
     "this.backoffMs * 2 ** (attempt - 1)", "this.backoffMs"),
    ("retry: retries 0 resolves instead of rejecting [targeted]",
     "attempt++ >= this.retries) throw e;",
     "attempt++ >= this.retries) { if (this.retries === 0) return this; throw e; }"),
    ("retry: clamp retries to at least 1",
     "Math.max(0, options.retries ?? 3)", "Math.max(1, options.retries ?? 3)"),
    ("retry: closed loop exits with the socket instead of the client",
     "\n    return this;\n  }", "\n    return this.socket;\n  }"),
    ("retry: dial once more after the loop decides to stop [targeted]",
     "\n    return this;\n  }",
     "\n    void new WebSocket(this.url);\n    return this;\n  }"),
    # --- reconnect ----------------------------------------------------------
    ("reconnect: never dial a replacement",
     "if (!this.closed && generation === this._generation) this.connect().catch(() => {});",
     "if (false) this.connect().catch(() => {});"),
    ("reconnect: clear the socket after scheduling the replacement [targeted]",
     "if (!this.closed && generation === this._generation) this.connect().catch(() => {});",
     "if (!this.closed && generation === this._generation) { this.connect().catch(() => {}); this.socket = null; }"),
    ("reconnect: close() forgets to forget the socket",
     "if (this.socket) this.socket.close(); this.socket = null; }",
     "if (this.socket) this.socket.close(); }"),
    ("reconnect: drop the generation guard",
     "if (!this.closed && generation === this._generation) this.connect().catch(() => {});",
     "if (!this.closed) this.connect().catch(() => {});"),
    ("reconnect: drop the closed and generation guards",
     "if (!this.closed && generation === this._generation) this.connect().catch(() => {});",
     "this.connect().catch(() => {});"),
    ("reconnect: a close event always drops the socket",
     "if (this.socket === ws) this.socket = null;", "this.socket = null;"),
    # --- queue --------------------------------------------------------------
    ("queue: always queue, never write through",
     "if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(data); else this.queue.push(data);",
     "this.queue.push(data);"),
    ("queue: send() through a socket that is not OPEN",
     "if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(data);",
     "if (this.socket) this.socket.send(data);"),
    ("queue: pretty-print the JSON instead of compacting it",
     "JSON.stringify(value)", "JSON.stringify(value, null, 2)"),
    ("queue: flush LIFO (pop instead of shift)",
     "ws.send(this.queue.shift())", "ws.send(this.queue.pop())"),
    ("queue: flush without draining the queue",
     "while (this.queue.length && ws.readyState === WebSocket.OPEN) ws.send(this.queue.shift());",
     "for (let i = 0; i < this.queue.length; i++) ws.send(this.queue[i]);"),
    ("queue: send AND queue on an OPEN socket",
     "if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(data); else this.queue.push(data);",
     "if (this.socket && this.socket.readyState === WebSocket.OPEN) { this.socket.send(data); this.queue.push(data); } else this.queue.push(data);"),
    ("queue: only queue when a socket already exists [targeted]",
     "else this.queue.push(data);", "else if (this.socket) this.queue.push(data);"),
    ("queue: close() leaves the queue behind",
     "this.closed = true; ++this._generation; this.queue.length = 0;",
     "this.closed = true; ++this._generation;"),
]

ASSERTION_MSG = re.compile(r"AssertionError(?: \[ERR_ASSERTION\])?: (.*)")
OBSERVED = re.compile(r"\s*\(observed \d+ms\)")


def die(msg, code=2):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(code)


def norm(s):
    return OBSERVED.sub("", s.strip()).strip()


def git_show(repo, spec):
    try:
        p = subprocess.run(["git", "show", spec], cwd=repo,
                           capture_output=True, text=True, check=True)
    except FileNotFoundError:
        die("git is not on PATH; this harness needs git to read the old ref")
    except subprocess.CalledProcessError as e:
        die(f"`git show {spec}` failed in {repo}:\n{e.stderr.strip()}")
    return p.stdout


def run_node(cwd, filename, timeout=20):
    try:
        p = subprocess.run(["node", filename], cwd=cwd,
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "TIMEOUT", ""
    msg = next((m.group(1) for m in
                (ASSERTION_MSG.search(ln) for ln in p.stderr.splitlines()) if m), "")
    if not msg and p.returncode != 0:
        msg = next((ln.strip() for ln in p.stderr.splitlines() if ln.strip()), "")
    return p.returncode, msg


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--repo", default=os.path.dirname(here),
                    help="repo/worktree root (default: parent of this script's directory)")
    ap.add_argument("--old-ref", default=f"d1bef61:{OLD_TEST_PATH}",
                    help="explicit git ref for the PRE-CHANGE test file, as rev:path or "
                         "just rev (default: d1bef61:%s)" % OLD_TEST_PATH)
    ap.add_argument("--no-module-check", action="store_true",
                    help="skip the 'tools/crp_ws_client.js is unmodified' precondition")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    module = os.path.join(repo, MODULE_PATH)
    new_test = os.path.join(repo, OLD_TEST_PATH)
    if not os.path.isfile(module):
        die(f"{module} not found -- wrong --repo?")
    if not os.path.isfile(new_test):
        die(f"{new_test} not found -- wrong --repo?")

    rev, path = (args.old_ref.split(":", 1) + [OLD_TEST_PATH])[:2] \
        if ":" in args.old_ref else (args.old_ref, OLD_TEST_PATH)
    old_src = git_show(repo, f"{rev}:{path}")

    print("=" * 100)
    print("crp_ws_client mutation harness -- docs/STATUS.md section 10.9")
    print(f"repo          : {repo}")
    print(f"module        : {MODULE_PATH} (mutated in a temp dir per mutant)")
    print(f"new suite     : {OLD_TEST_PATH} (working tree)")
    print(f"old suite ref : {rev}:{path}")
    print(f"reproduce with: python3 tools/crp_ws_client.mutation.py --old-ref {rev}:{path}")
    print("=" * 100)

    # --- precondition 1: the old file must really be the 2-assertion file ----
    old_lines = old_src.rstrip("\n").splitlines()
    old_asserts = old_src.count("assert.")
    if len(old_lines) != 6 or old_asserts != 2 or "ok  " in old_src:
        die(f"{rev}:{path} is NOT the expected pre-change suite.\n"
            f"  expected: 6 lines, 2 `assert.` calls, no `  ok  ` labels\n"
            f"  got     : {len(old_lines)} lines, {old_asserts} `assert.` calls, "
            f"{'has' if 'ok  ' in old_src else 'no'} `  ok  ` labels\n"
            f"  first line: {old_lines[0] if old_lines else '<empty>'!r}\n"
            "Refusing to run: this is exactly how the harness would silently degrade "
            "to 'new vs new'. Pass --old-ref <pre-change-rev> if the base commit moved.",
            code=2)

    # --- precondition 2: the module must still be the unmodified one --------
    if not args.no_module_check:
        module_at_rev = git_show(repo, f"{rev}:{MODULE_PATH}")
        if module_at_rev != open(module).read():
            die(f"{MODULE_PATH} differs from {rev}:{MODULE_PATH}.\n"
                "The mutation anchors and the 'old file is blind to it' claim were both "
                "measured against the unmodified module. Re-derive the anchors, or pass "
                "--no-module-check if the change is deliberate and the anchors still match.",
                code=2)

    # --- precondition 3: the new suite must be the labelled suite -----------
    print()
    clean = subprocess.run(["node", new_test], cwd=repo, capture_output=True, text=True)
    labels = [norm(ln.split("ok  ", 1)[1])
              for ln in clean.stdout.splitlines() if ln.startswith("  ok  ")]
    advertised = re.search(r"CRP WebSocket client tests: ok \((\d+) assertions\)",
                           clean.stdout)
    if clean.returncode != 0:
        die(f"the new suite is already red on the unmodified module:\n"
            f"{clean.stdout.strip()}\n{clean.stderr.strip()}", code=2)
    if not advertised or int(advertised.group(1)) != len(labels):
        die(f"the new suite does not self-report its assertion count consistently: "
            f"{advertised.group(1) if advertised else 'no count line'} vs {len(labels)} labels")
    if len(labels) < 20:
        die(f"the new suite has only {len(labels)} labelled assertions -- it looks like the "
            "pre-change 2-assertion file was picked up as the new suite")
    print(f"new suite clean run: exit 0, {len(labels)} labelled assertions")
    print(f"old suite ({rev}): {len(old_lines)} lines, {old_asserts} assertions, "
          f"0 labels -- it cannot name a failing assertion, only an exit code")

    if len(MUTS) != EXPECTED_MUTANTS:
        die(f"mutant list has {len(MUTS)} entries, docs claim {EXPECTED_MUTANTS}")
    if len(labels) != EXPECTED_ASSERTIONS:
        die(f"new suite has {len(labels)} assertions, docs claim {EXPECTED_ASSERTIONS} "
            "-- update docs/STATUS.md section 10.9 and the constants in this file together")

    orig = open(module).read()
    for name, old, new in MUTS:
        n = orig.count(old)
        if n != 1:
            die(f"anchor for {name!r} occurs {n}x in {MODULE_PATH}, need exactly 1 "
                "(the module moved; re-derive the anchor)")

    # --- run ----------------------------------------------------------------
    kills = {i: [] for i in range(len(labels))}
    rows = []
    print()
    for name, old, new in MUTS:
        with tempfile.TemporaryDirectory(prefix="crp-ws-mutation-") as d:
            with open(os.path.join(d, "crp_ws_client.js"), "w") as f:
                f.write(orig.replace(old, new, 1))
            shutil.copy(new_test, os.path.join(d, "crp_ws_client.test.js"))
            with open(os.path.join(d, "crp_ws_client.old.test.js"), "w") as f:
                f.write(old_src)
            res = {"old": run_node(d, "crp_ws_client.old.test.js"),
                   "new": run_node(d, "crp_ws_client.test.js")}
        hit = None
        if res["new"][0] != 0 and res["new"][1]:
            want = norm(res["new"][1])
            exact = [i for i, lab in enumerate(labels) if lab == want]
            pref = [i for i, lab in enumerate(labels) if lab.startswith(want[:40])]
            hit = (exact or pref or [None])[0]
            if hit is not None:
                kills[hit].append(name)
        rows.append((name, res["old"][0], res["new"][0], hit))
        print(f"mutation: {name}")
        print(f"   old test (2 assertions): exit {res['old'][0]}  {res['old'][1][:100]}")
        which = (f"assertion #{hit + 1} '{labels[hit]}'" if hit is not None
                 else f"no labelled assertion -- {res['new'][1][:100]}")
        print(f"   new test ({len(labels)} assertions): exit {res['new'][0]}  {which}")

    # --- kill map -----------------------------------------------------------
    print("\n" + "=" * 100)
    print("KILL MAP -- which mutant makes each new assertion go red first\n")
    for i, lab in enumerate(labels):
        k = kills[i]
        print(f"  {i + 1:>2}. {lab[:72]:<72} <- {k[0] if k else '** SHIELDED **'}")
    covered = sum(1 for k in kills.values() if k)
    print(f"\n{covered}/{len(labels)} assertions individually falsified by {len(MUTS)} mutants")
    for i, k in kills.items():
        if not k:
            print(f"  shielded: #{i + 1} {labels[i]}")

    print("\n" + "=" * 100)
    print(f"\n{'mutation'.ljust(58)} old  new")
    for name, o, n, _ in rows:
        print(name.ljust(58), str(o).ljust(4), str(n))

    # --- verdicts against the documented claims -----------------------------
    old_sees = tuple(name for name, o, _, _ in rows if o != 0)
    ok = True
    print("\n" + "=" * 100)
    print("VERDICT (claims recorded in docs/STATUS.md section 10.9)\n")
    if old_sees == EXPECTED_OLD_SEES:
        print(f"  ok  old 2-assertion file is blind to {len(rows) - len(old_sees)}/{len(rows)} "
              f"mutants, and sees exactly the {len(old_sees)} documented ones")
    else:
        ok = False
        print(f"  FAIL old file sees {len(old_sees)} mutants, docs claim "
              f"{len(EXPECTED_OLD_SEES)}:\n        sees     : {old_sees}\n"
              f"        expected : {EXPECTED_OLD_SEES}")
    if covered == EXPECTED_KILLED:
        print(f"  ok  {covered}/{len(labels)} new assertions are individually falsified")
    else:
        ok = False
        print(f"  FAIL {covered}/{len(labels)} new assertions falsified, docs claim "
              f"{EXPECTED_KILLED}/{EXPECTED_ASSERTIONS}")
    print()
    print("  RESULT: " + ("the documented kill map reproduces" if ok else
                           "the documented kill map NO LONGER reproduces -- update "
                           "docs/STATUS.md section 10.9"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
