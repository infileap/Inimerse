#!/usr/bin/env python3
"""P1 closed loop: create / enter / sync / drain / recover / undo / replay.

The eight P1 steps are exercised across a real process boundary: this script
talks to an `inim-server` child process over pipes, and drives `inim-client`
(itself a separate process that spawns its own server) for the interactive
half.  Nothing here links the server code in-process, so the test cannot pass
by accident through shared memory.

Usage: verse_closed_loop.test.py <path-to-inim-client>
Env:   INIM_SERVER_BIN  path to inim-server (set by CTest)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

CLIENT = sys.argv[1] if len(sys.argv) > 1 else "inim-client"
SERVER = os.environ.get("INIM_SERVER_BIN", "inim-server")

checks = 0
failures = []


def check(cond, label, detail=""):
    global checks
    checks += 1
    if not cond:
        failures.append(f"{label}: {detail}")


def run_server(root, lines, verse="main", timeout=30):
    """Start a fresh server process on `root` and feed it `lines`."""
    proc = subprocess.run(
        [SERVER, root, verse],
        input="\n".join(lines) + "\n",
        capture_output=True, text=True, timeout=timeout,
    )
    out = [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]
    return proc, out


HELLO = {"op": "hello", "protocol": 1, "client": "closed-loop",
         "caps": ["put", "undo", "drain", "status"]}


def jline(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


# ---------------------------------------------------------------- 1. create
def test_create(root):
    proc, out = run_server(root, [jline(HELLO), jline({"op": "bye"})])
    check(proc.returncode == 0, "create: server exits cleanly", proc.stderr)

    vdir = os.path.join(root, "verse", "main")
    for name in ("manifest.json", "events.log", "commit.head"):
        check(os.path.exists(os.path.join(vdir, name)),
              "create: writes " + name, vdir)

    man = json.load(open(os.path.join(vdir, "manifest.json")))
    check(man.get("verse_id") == "main", "create: manifest names the verse", man)
    check(man.get("abi") == "infiverse.mv1/abi/1.0", "create: manifest pins the abi", man)
    check(bool(man.get("created_at")), "create: manifest has created_at", man)

    # re-opening the same root must not re-create or overwrite it
    proc2, out2 = run_server(root, [jline(HELLO), jline({"op": "bye"})])
    check(proc2.returncode == 0, "create: reopening is not an error", proc2.stderr)
    man2 = json.load(open(os.path.join(vdir, "manifest.json")))
    check(man2 == man, "create: manifest is never silently rewritten", (man, man2))


# ------------------------------------------------------------- 2. enter
def test_enter(root):
    proc, out = run_server(root, [
        jline({"op": "put", "key": "early", "cell": "0,0,0", "value": 1}),
        jline({"op": "hello", "protocol": 1, "client": "x", "caps": ["put", "time_travel"]}),
        jline({"op": "hello", "protocol": 9, "client": "x", "caps": []}),
        jline(HELLO),
        jline({"op": "bye"}),
    ])

    check(out[0].get("code") == "no_session", "enter: op before hello refused", out[0])
    check(out[1].get("code") == "capability_refused", "enter: unknown cap refused", out[1])
    check("time_travel" in out[1].get("error", ""), "enter: refusal names the cap", out[1])
    check(out[2].get("code") == "protocol_mismatch", "enter: bad version refused", out[2])
    check(out[3].get("ok") is True, "enter: handshake accepted", out[3])
    check(out[3].get("verse_id") == "main", "enter: handshake names the verse", out[3])
    check(out[3].get("caps") == ["drain", "put", "status", "undo"],
          "enter: agreed caps are reported", out[3])
    check(out[3].get("seq") == 0, "enter: nothing was committed before hello", out[3])


# ------------------------------------------------------------- 3. sync
def test_sync(root):
    reqs = [
        HELLO,
        {"op": "put", "key": "a", "cell": "0,0,0", "value": 5},
        {"op": "put", "key": "b", "cell": "0,0,0", "value": 6},
        {"op": "status"},
    ]
    # a client that tries to assert final state is refused for every field
    for field in ("seq", "rev", "head", "balance", "state_hash", "committed"):
        reqs.append({"op": "put", "key": "evil", "cell": "9,9,9", "value": 1, field: 42})
    reqs.append({"op": "status"})
    reqs.append({"op": "bye"})

    proc, out = run_server(root, [jline(r) for r in reqs])

    check(out[1]["seq"] == 1 and out[2]["seq"] == 2,
          "sync: server assigns monotonically increasing seq",
          (out[1], out[2]))
    check("seq" not in reqs[1], "sync: the client never sent a seq")
    check(out[3]["seq"] == 2, "sync: status agrees on seq", out[3])
    check(out[3]["cells"] == [{"cell": "0,0,0", "value": 6}],
          "sync: the server's cell view is authoritative", out[3])

    for i, field in enumerate(("seq", "rev", "head", "balance", "state_hash", "committed")):
        r = out[4 + i]
        check(r.get("code") == "client_authority",
              f"sync: client-supplied '{field}' refused", r)
        check(field in r.get("error", ""),
              f"sync: refusal names '{field}'", r)

    check(out[10]["seq"] == 2, "sync: refusals did not advance seq", out[10])
    check(out[10]["rejected"] == 6, "sync: the server counted the refusals", out[10])
    check(out[10]["cells"] == [{"cell": "0,0,0", "value": 6}],
          "sync: refusals did not mutate state", out[10])


# ------------------------------------------------------------- 4. drain
def test_drain(root):
    proc, out = run_server(root, [jline(HELLO), jline({"op": "drain"}), jline({"op": "bye"})])
    check(out[1].get("drained") is True, "drain: reports drained", out[1])
    check(out[1].get("pending") == 0, "drain: nothing left pending", out[1])
    check(out[1].get("head") == out[2].get("head"), "drain: bye agrees with drain", (out[1], out[2]))
    snap = os.path.join(root, "verse", "main", "snapshots", "state.json")
    check(os.path.exists(snap), "drain: wrote a snapshot", snap)
    snap_obj = json.load(open(snap))
    check(isinstance(snap_obj, dict), "drain: snapshot is a json object", snap_obj)


# ------------------------------------------------------------- 6. undo
def test_undo(root):
    proc, out = run_server(root, [
        jline(HELLO),
        jline({"op": "undo", "key": "u0", "target": 7}),      # target never existed
        jline({"op": "put", "key": "p1", "cell": "0,0,0", "value": 100}),
        jline({"op": "put", "key": "p2", "cell": "0,0,0", "value": 200}),
        jline({"op": "undo", "key": "u1", "target": 2}),
        jline({"op": "status"}),
        jline({"op": "undo", "key": "u1", "target": 2}),      # same key: must not apply again
        jline({"op": "status"}),
        jline({"op": "bye"}),
    ])

    check(out[1].get("ok") is False,
          "undo: a target that never existed is refused", out[1])

    check(out[4].get("ok") is True and out[4].get("seq") == 3,
          "undo: committed as a new record", out[4])
    check(out[5]["cells"] == [{"cell": "0,0,0", "value": 100}],
          "undo: restored the value the put overwrote", out[5])

    check(out[7]["seq"] == 3, "undo: a repeated key adds no record", out[7])
    check(out[7]["cells"] == [{"cell": "0,0,0", "value": 100}],
          "undo: a repeated key does not apply twice", out[7])


# ---------------------------------------------- 5 + 7. crash, recover, replay
def test_recovery(root):
    """Commit, replay, then crash; a fresh process must rebuild the same state."""
    script = [
        jline(HELLO),
        jline({"op": "put", "key": "k1", "cell": "1,0,0", "value": 11}),
        jline({"op": "put", "key": "k2", "cell": "2,0,0", "value": 22}),
        jline({"op": "put", "key": "k3", "cell": "3,0,0", "value": 33}),
        jline({"op": "undo", "key": "u1", "target": 3}),
        jline({"op": "drain"}),
        jline({"op": "status"}),
    ]
    proc, out = run_server(root, script)
    live = out[-1]
    check(live["seq"] == 4, "replay: four records committed", live)

    # replay from disk in a brand new process
    proc2, out2 = run_server(root, [jline(HELLO), jline({"op": "status"})])
    replayed = out2[1]
    check(replayed["seq"] == live["seq"], "replay: seq survives a restart",
          (live["seq"], replayed["seq"]))
    check(replayed["head"] == live["head"],
          "replay: the chain head is byte-identical after replay",
          (live["head"], replayed["head"]))
    check(replayed["cells"] == live["cells"],
          "replay: cell state is reconstructed identically",
          (live["cells"], replayed["cells"]))

    # a third, independent replay must agree again (determinism)
    proc3, out3 = run_server(root, [jline(HELLO), jline({"op": "status"})])
    check(out3[1]["head"] == live["head"],
          "replay: a second replay agrees (deterministic)", out3[1])


def test_crash_and_resume(root):
    """SIGKILL the server mid-session; the next process must find a clean log."""
    script = [
        jline(HELLO),
        jline({"op": "put", "key": "k1", "cell": "1,1,1", "value": 7}),
        jline({"op": "drain"}),   # drop a durable snapshot
        jline({"op": "put", "key": "k2", "cell": "2,2,2", "value": 8}),
        "#crash",
    ]
    scenario = os.path.join(root, "crash.scenario")
    with open(scenario, "w") as f:
        f.write("\n".join(script) + "\n")

    proc = subprocess.run([CLIENT, "--server", SERVER, root, "main", scenario],
                          capture_output=True, text=True, timeout=30)
    check(proc.returncode == 137, "recover: the client reports the crash", proc.returncode)
    check("#crash: server killed" in proc.stdout, "recover: crash was deliberate", proc.stdout)

    # the log on disk was never left half-written, so a fresh process recovers
    proc2, out2 = run_server(root, [jline(HELLO), jline({"op": "status"})])
    st = out2[1]
    check(st.get("ok") is True, "recover: a fresh server opens the crashed root", st)
    check(st["seq"] >= 1, "recover: committed records survived the crash", st)
    values = {c["cell"]: c["value"] for c in st["cells"]}
    check(values.get("1,1,1") == 7, "recover: the drained record survived", values)

    proc3, out3 = run_server(root, [jline(HELLO), jline({"op": "drain"}), jline({"op": "bye"})])
    check(out3[1].get("drained") is True,
          "recover: the recovered Layer drains cleanly", out3[1])
    check(proc3.returncode == 0, "recover: the recovered server exits 0", proc3.stderr)


def test_tamper_requires_recovery(root):
    """The log is not self-authenticating: only the commit pointer detects a
    rewrite, and a detected rewrite must surface as RECOVERY_REQUIRED."""
    _, out = run_server(root, [
        jline(HELLO),
        jline({"op": "put", "key": "t1", "cell": "5,5,5", "value": 1}),
        jline({"op": "put", "key": "t2", "cell": "5,5,5", "value": 2}),
        jline({"op": "bye"}),
    ])

    log = os.path.join(root, "verse", "main", "events.log")
    with open(log, "rb") as f:
        data = bytearray(f.read())
    idx = data.find(b'"value":2')
    check(idx > 0, "tamper: fixture found a value to rewrite", log)
    if idx <= 0:
        return

    data[idx + len('"value":')] = ord("9")   # 2 -> 9, same length
    with open(log, "wb") as f:
        f.write(bytes(data))

    # A rewritten record is indistinguishable from a legitimate one by the
    # chain alone: recomputing the chain over the tampered bytes still yields a
    # self-consistent log.  Only the durable commit pointer can tell.
    head_file = os.path.join(root, "verse", "main", "commit.head")
    anchor = json.load(open(head_file))
    check("head" in anchor and "seq" in anchor,
          "tamper: the commit pointer records head and seq", anchor)
    check(anchor["seq"] == 2, "tamper: the pointer still names the pre-tamper seq", anchor)

    proc, out2 = run_server(root, [jline(HELLO), jline({"op": "drain"})])
    check(out2[0].get("ok") is False,
          "tamper: no session is granted over an unanchored log", out2[0])
    check(out2[0].get("code") == "recovery_required",
          "tamper: the refusal is RECOVERY_REQUIRED", out2[0])
    check(out2[1].get("drained") is False,
          "tamper: a rewritten log is not reported as drained", out2[1])
    check(out2[1].get("code") == "recovery_required",
          "tamper: the drain failure is RECOVERY_REQUIRED", out2[1])
    check(proc.returncode == 2,
          "tamper: the server exits 2 rather than pretending it is healthy",
          proc.returncode)

    # the recomputed chain really did move: the pointer and the log now
    # disagree, which is the whole reason the pointer exists
    tail = out2[1].get("head")
    check(tail != anchor["head"],
          "tamper: the recomputed head differs from the recorded pointer",
          (anchor["head"], tail))


def main():
    root = tempfile.mkdtemp(prefix="verse_closed_loop_")
    try:
        # Each stage gets a clean root so the stages stay independent.
        stages = [
            ("create", test_create),
            ("enter", test_enter),
            ("sync", test_sync),
            ("drain", test_drain),
            ("undo", test_undo),
            ("replay", test_recovery),
            ("recover", test_crash_and_resume),
            ("tamper", test_tamper_requires_recovery),
        ]
        for name, fn in stages:
            sub = os.path.join(root, name)
            os.makedirs(sub, exist_ok=True)
            fn(sub)
    finally:
        shutil.rmtree(root, ignore_errors=True)

    if failures:
        print(f"verse_closed_loop: {checks} checks, {len(failures)} FAILURES")
        for f in failures:
            print("  FAIL " + f)
        return 1
    print(f"verse_closed_loop: {checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
