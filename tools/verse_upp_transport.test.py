#!/usr/bin/env python3
"""P1 `--upp`: the Layer protocol behind the UPP envelope, and nothing else.

The claim under test is an equality: one scenario, read through the plain
transport and through `--upp`, produces the same response lines.  An equality
between two things that can print nothing is not evidence, so the transcript
is required to be non-empty and to carry a refusal the Layer itself produced
(a `no_session` answer -- an error *inside* the envelope, which a transport
error must not be confused with), and a second scenario with one more `put`
is required to differ from the first, so the comparison is shown to be live
rather than constant-true.

The four refusals the envelope adds on top of upp.h are then exercised
against the server directly, each by exit code and by the words on stderr:
a `verse` frame before `start`, an unknown frame type, a frame that is not a
frame, an incompatible hello, and a payload past the Layer's request bound.

Usage: verse_upp_transport.test.py <path-to-inim-client> [--self-check]
Env:   INIM_SERVER_BIN  path to inim-server (set by CTest)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

CLIENT = sys.argv[1] if len(sys.argv) > 1 else "inim-client"
SELF_CHECK = "--self-check" in sys.argv[2:]
SERVER = os.environ.get("INIM_SERVER_BIN", "inim-server")

checks = 0
failures = []


def check(cond, label, detail=""):
    global checks
    checks += 1
    if not cond:
        failures.append(f"{label}: {detail}")


def jline(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


HELLO = {"op": "hello", "protocol": 1, "client": "upp-transport",
         "caps": ["put", "undo", "drain", "status"]}

# One scenario, two transports.  The first line is a request the Layer must
# refuse -- it is sent before the hello -- so the envelope has to carry a
# refusal out as a response, not turn it into a transport failure.
SCENARIO = [
    jline({"op": "put", "key": "early", "cell": "0,0,0", "value": 1}),
    jline(HELLO),
    jline({"op": "put", "key": "a", "cell": "0,0,0", "value": 7}),
    jline({"op": "status"}),
    jline({"op": "bye"}),
]

# A request that is large but still inside the Layer's bound: 60 KiB of key.
# This is the size where the frame buffer stops being the plain reader's stack
# buffer, so it is the case that says whether the envelope really carries the
# request or only appears to.
LARGE_KEY = "k" * 60000
SCENARIO_LARGE = [
    jline(HELLO),
    jline({"op": "put", "key": LARGE_KEY, "cell": "0,0,0", "value": 3}),
    jline({"op": "status"}),
    jline({"op": "bye"}),
]

SCENARIO_SECOND_PUT = [
    jline({"op": "put", "key": "early", "cell": "0,0,0", "value": 1}),
    jline(HELLO),
    jline({"op": "put", "key": "a", "cell": "0,0,0", "value": 7}),
    jline({"op": "put", "key": "b", "cell": "1,0,0", "value": 8}),
    jline({"op": "status"}),
    jline({"op": "bye"}),
]


def first_difference(a, b):
    """Index of the first line that differs, or -1 when the lists are equal."""
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return -1 if len(a) == len(b) else min(len(a), len(b))


def run_client(root, scenario, verse="main", upp=False, timeout=40):
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, "scenario.txt")
    with open(path, "w") as f:
        f.write("\n".join(scenario) + "\n")
    argv = [CLIENT, "--server", SERVER]
    if upp:
        argv.append("--upp")
    argv += [root, verse, path]
    proc = subprocess.run(argv, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    lines = [l for l in proc.stdout.splitlines() if l.strip()]
    return proc, lines


def run_server_lines(root, lines, verse="main", upp=False, timeout=40):
    os.makedirs(root, exist_ok=True)
    argv = [SERVER]
    if upp:
        argv.append("--upp")
    argv += [root, verse]
    proc = subprocess.run(argv, input="\n".join(lines) + "\n",
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    return proc


def frame(type_, payload):
    return jline({"upp": 1, "type": type_, "payload": payload})


def manifest(abi):
    return {"id": "probe-client", "name": "probe", "version": "1.0.0",
            "engine": "inimerse", "entry": "verse", "abi": abi,
            "abiRange": str(abi), "files": {}, "capabilities": []}


# ---------------------------------------------------------------- the equality
def test_same_transcript(root):
    plain_root = os.path.join(root, "plain")
    upp_root = os.path.join(root, "upp")
    plain, plain_lines = run_client(plain_root, SCENARIO, upp=False)
    upp, upp_lines = run_client(upp_root, SCENARIO, upp=True)

    check(first_difference(plain_lines, upp_lines) == -1,
          "same: both transports print the same lines",
          (plain_lines, upp_lines))
    check(len(plain_lines) == len(SCENARIO), "same: one response per request", plain_lines)
    check(plain.returncode == 0 and upp.returncode == 0,
          "same: the client reports the server's clean exit",
          (plain.returncode, upp.returncode, upp.stderr))

    # The first answer is the Layer's own refusal.  If the envelope swallowed
    # it, both transcripts would still be equal -- and equal for the wrong
    # reason -- so the refusal is asserted by name across both transports.
    for label, lines in (("plain", plain_lines), ("upp", upp_lines)):
        if len(lines) < 2:
            check(False, f"same: the {label} transport answered", lines)
            continue
        try:
            first = json.loads(lines[0])
        except ValueError as e:
            check(False, f"same: the {label} transport answered a line", (lines[0], e))
            continue
        check(first.get("code") == "no_session",
              f"same: the Layer's refusal survives the {label} transport", first)

    # The same equality one order of magnitude of payload further up: a 60 KiB
    # request is past VL_LINE_MAX/2 and still inside the Layer's bound, so it
    # rides through a frame that could not be read with a 64 KiB line buffer.
    big_plain, big_plain_lines = run_client(os.path.join(root, "big-plain"), SCENARIO_LARGE)
    big_upp, big_upp_lines = run_client(os.path.join(root, "big-upp"), SCENARIO_LARGE, upp=True)
    check(first_difference(big_plain_lines, big_upp_lines) == -1,
          "same: a 60 KiB request answers identically through both transports",
          (big_plain_lines, big_upp_lines))
    check(big_plain.returncode == 0 and big_upp.returncode == 0,
          "same: and neither client exits non-zero",
          (big_plain.returncode, big_upp.returncode, big_upp.stderr))
    if len(big_upp_lines) > 1:
        try:
            answered = json.loads(big_upp_lines[1])
        except ValueError:
            answered = {}
        check(answered.get("ok") is True,
              "same: the 60 KiB put was committed, not refused", answered)

    # A second scenario with one more `put` must not compare equal: that is
    # what makes the equality above an observation instead of a constant.
    other, other_lines = run_client(os.path.join(root, "other"), SCENARIO_SECOND_PUT, upp=True)
    check(first_difference(upp_lines, other_lines) >= 0,
          "control: one more put changes the transcript", (upp_lines, other_lines))
    check(other.returncode == 0, "control: the second run also exits cleanly", other.stderr)


# ------------------------------------------------------ the envelope is opt-in
def test_plain_is_not_upp(root):
    """A UPP frame on the plain transport is a Layer request, and refused as
    one.  If `--upp` were implicit, this line would be accepted as a hello."""
    proc = run_server_lines(os.path.join(root, "plain-envelope"),
                            [frame("hello", {"role": "client", "manifest": manifest(1)})])
    check(proc.returncode == 0, "opt-in: the plain server exits cleanly", proc.stderr)
    out = [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]
    check(len(out) == 1, "opt-in: one response", out)
    # It was read as a Layer request and refused for having no `op`: the
    # refusal is the proof, not a specific code -- a future Layer may name a
    # missing field differently, and what matters is that the frame was never
    # treated as an envelope.
    check(out[0].get("ok") is False, "opt-in: a UPP frame is not accepted", out)
    check("op" in str(out[0].get("error", "")) or out[0].get("code") == "no_session",
          "opt-in: it is refused as a Layer request, not as an envelope", out)


# ------------------------------------------------------------ the refusals
def refused(root, lines, want_rc, want_text, label, expect_frames=0):
    """`expect_frames` is the number of frames sent *before* the refused one
    that the protocol obliges the server to answer (a hello gets a welcome);
    asserting the count is how a refusal is told apart from an answer."""
    proc = run_server_lines(root, lines, upp=True)
    check(proc.returncode == want_rc, f"refuse: {label} exits {want_rc}",
          (proc.returncode, proc.stderr))
    check(want_text in proc.stderr, f"refuse: {label} is named on stderr",
          (want_text, proc.stderr))
    answered = [l for l in proc.stdout.splitlines() if l.strip()]
    check(len(answered) == expect_frames,
          f"refuse: {label} answers {expect_frames} frames and not the request",
          proc.stdout)


def test_refusals(root):
    # (1) a `verse` frame before `start`: the session has no agreed ABI yet
    refused(os.path.join(root, "early-verse"),
            [frame("verse", HELLO)], 4, "not running", "a verse frame before start")

    # (2) a frame type nothing knows: dropped in silence by upp_session_apply,
    #     which is why the transport refuses it instead
    refused(os.path.join(root, "unknown-type"),
            [jline({"upp": 1, "type": "nope", "payload": {}})],
            4, "unknown frame type", "an unknown frame type")

    # (3) a line that is not a frame at all
    refused(os.path.join(root, "malformed"),
            ["{\"type\":\"hello\"}"], 4, "upp refused", "a malformed frame")

    # (4) a hello whose abi the server does not speak: exit 5, not an
    #     `incompatible` frame (see src/verse/server.c's header comment)
    refused(os.path.join(root, "incompatible"),
            [frame("hello", {"role": "client", "manifest": manifest(9)})],
            5, "hello refused", "an incompatible hello")

    # (5) a payload past the Layer's bound, carried by a frame that was read
    #     whole.  The buffer this frame needed is past VL_LINE_MAX, so this
    #     also proves the frame reader is not a 64 KiB reader: it had to
    #     measure 100 kB to refuse 100 kB by name.
    refused(os.path.join(root, "over-bound"),
            [frame("hello", {"role": "client", "manifest": manifest(1),
                             "capabilities": []}),
             frame("start", {"entry": "verse", "args": []}),
             frame("verse", {"op": "put", "key": "z" * 100000,
                             "cell": "0,0,0", "value": 1})],
            4, "past the Layer's", "an over-bound payload", expect_frames=1)


# ---------------------------------------------------------------- self-check
def self_check():
    """The comparator this test rests on must be able to answer 'different'."""
    n = 0
    bad = []
    cases = [
        (["a", "b"], ["a", "b"], -1),
        (["a", "b"], ["a", "c"], 1),
        (["a"], ["a", "b"], 1),
        (["a", "b"], ["a"], 1),
        ([], [], -1),
    ]
    for i, (a, b, want) in enumerate(cases):
        n += 1
        got = first_difference(a, b)
        if got != want:
            bad.append(f"case {i}: first_difference({a},{b})={got}, want {want}")
    # an empty pair compares equal, which is exactly why test_same_transcript
    # asserts the line count as well -- proved here rather than remembered
    n += 1
    if first_difference([], []) != -1:
        bad.append("case 5: the empty pair must still compare equal (that is the trap)")
    if bad:
        print("verse_upp_transport self-check: %d checks, %d FAILURES" % (n, len(bad)))
        for b in bad:
            print("  FAIL " + b)
        return 1
    print("verse_upp_transport self-check: %d checks, 0 failures" % n)
    return 0


def main():
    if SELF_CHECK:
        return self_check()

    root = tempfile.mkdtemp(prefix="verse-upp-transport-")
    try:
        for name, fn in (("same", test_same_transcript),
                         ("optin", test_plain_is_not_upp),
                         ("refuse", test_refusals)):
            sub = os.path.join(root, name)
            os.makedirs(sub, exist_ok=True)
            # A stage that raises is red, but it is worth a named FAIL: an
            # exception in the harness and a wrong reading are different
            # failures, and a traceback hides which one happened.
            try:
                fn(sub)
            except Exception as e:          # noqa: BLE001 -- reported, not swallowed
                check(False, f"{name}: the stage raised", repr(e))
    finally:
        shutil.rmtree(root, ignore_errors=True)

    if failures:
        print(f"verse_upp_transport: {checks} checks, {len(failures)} FAILURES")
        for f in failures:
            print("  FAIL " + f)
        return 1
    print(f"verse_upp_transport: {checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
