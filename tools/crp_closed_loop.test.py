#!/usr/bin/env python3
"""CRP closed loop across a real process boundary: crp-hub <-> crp-peer.

Two separate binaries talk over a real TCP socket.  This script starts
`crp-hub` on an ephemeral port (the hub announces the port it actually bound,
so there is no port race -- see docs/STATUS.md 2.9), then runs `crp-peer` as a
third process.  The hub owns every piece of CRP state; the peer only ever sees
it through the socket.  Nothing here links the engine's registry in-process, so
the test cannot pass through shared memory.

The peer's verbatim transcript is echoed with a "| " prefix: the exchanges are
the evidence, and the driver re-checks the aggregate shape of that transcript
independently of the peer's own assertions.

Usage: crp_closed_loop.test.py <path-to-crp-peer>
Env:   INIM_CRP_HUB_BIN  path to crp-hub (set by CTest)
"""
import json
import os
import atexit
import shutil
import subprocess
import tempfile
import sys

PEER = sys.argv[1] if len(sys.argv) > 1 else "crp-peer"
HUB = os.environ.get("INIM_CRP_HUB_BIN", "crp-hub")

# Same secret and frozen clock as src/verse/crp_probe.c, so the whole transcript
# is reproducible byte for byte.
SECRET = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
# The secret on this pair's line of the hub's admissions table -- and, during a
# migration, the same string the old hub-wide CRP_ENROLL_SECRET used to hold.
# It is deliberately different from the token-signing secret: a token admits a
# caller to /signal, whereas the admissions line decides who may be issued a
# token at all.  The hub reads the table, not the variable: the variable is only
# how the caller knows which secret to prove.
ENROLL = "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"
NOW = 1767225600000  # 2026-01-01T00:00:00Z

checks = 0
failures = []


def check(cond, label, detail=""):
    global checks
    checks += 1
    if not cond:
        failures.append(f"{label}: {detail}")


def main():
    # The hub is handed exactly one source: a table written here, named by
    # CRP_ADMISSIONS_FILE.  ("nope" is admitted without being registered, so the
    # 404 branch -- only an admitted pair may learn its verse is unknown -- is
    # still reachable.)
    tdir = tempfile.mkdtemp(prefix="crp-admissions-")
    atexit.register(shutil.rmtree, tdir, ignore_errors=True)
    admissions = os.path.join(tdir, "portal_admissions.tsv")
    with open(admissions, "w", encoding="utf-8") as fh:
        fh.write("# portal admissions (closed loop)\n")
        # crp-peer defaults its id to "peer-a" when argv[4] is omitted, and this
        # driver omits it: the admitted pairs follow the peer the run actually
        # speaks as.
        fh.write(f"demo\tpeer-a\t{ENROLL}\tsignal\n")
        fh.write(f"nope\tpeer-a\t{ENROLL}\tsignal\n")
        # The second peer the transcript opens a portal as; it must be admitted
        # too, or "a different peer gets a different token" cannot be reached.
        fh.write(f"demo\tpeer-b\t{ENROLL}\tsignal\n")
    env = dict(os.environ, CRP_ENROLL_SECRET=ENROLL, CRP_ADMISSIONS_FILE=admissions)
    hub = subprocess.Popen(
        [HUB, "0", SECRET, str(NOW)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", env=env,
    )
    try:
        announce = hub.stdout.readline()
        check(announce.strip().startswith("{"), "hub: announces a port", announce)
        try:
            port = json.loads(announce)["port"]
        except Exception as exc:  # noqa: BLE001 - report the raw line instead
            check(False, "hub: port announcement is JSON", f"{exc}: {announce}")
            return 1
        check(isinstance(port, int) and port > 0, "hub: bound a real port", port)

        peer = subprocess.run([PEER, str(port), SECRET, str(NOW)],
                              capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30, env=env)
        for line in peer.stdout.splitlines():
            print("| " + line)
        if peer.stderr.strip():
            for line in peer.stderr.splitlines():
                print("! " + line)

        check(peer.returncode == 0, "peer: exits 0", peer.returncode)
        check("0 failures" in peer.stdout, "peer: reports no failures",
              peer.stdout.strip().splitlines()[-1:] or "")

        # Aggregate shape of the transcript, judged independently of the peer.
        out = peer.stdout
        check("frame rejected" not in out,
              "hub: every frame the peer built decoded and agreed", out[:400])
        check(out.count('"frameText"') >= 8,
              "peer: sent real CRP frames over the socket",
              out.count('"frameText"'))
        # The portal caller had to prove who it is: four well-formed requests --
        # absent, malformed and mismatched proofs, plus one naming no peer at all
        # (which names no pair, so admission refuses it) -- were all refused, and
        # all with the same body: the reason goes to the hub's log, not to the
        # caller.
        check(out.count("invalid enrollment proof") == 4,
              "hub: refused absent, malformed, mismatched and peerless enrollment",
              out.count("invalid enrollment proof"))
        check('"sessions":0' in out,
              "hub: refused portals created no session", out.count('"sessions":0'))
        for status, want, label in (
            (403, 8, "capability token or enrollment proof refused"),
            # One fewer 404 than before: a request naming no peer is now an
            # unregistered pair (403) rather than a shape error (404), because
            # admission is decided before the shape is inspected.
            (404, 2, "unknown verse refused"),
            (409, 1, "out-of-order resume refused"),
            (400, 3, "malformed request refused"),
            (202, 3, "signals accepted"),
            (200, 14, "successful exchanges"),
        ):
            got = out.count(f'"status":{status}')
            check(got == want, f"transcript: {label}", f"got {got}, want {want}")
        check(out.count(">> ") == out.count("<< "),
              "transcript: every request got exactly one response",
              (out.count(">> "), out.count("<< ")))
    finally:
        try:
            hub_out, hub_err = hub.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            hub.kill()
            hub_out, hub_err = hub.communicate()
            check(False, "hub: exited on its own after bye", "timed out")

    for line in hub_out.splitlines():
        print("| hub: " + line)
    if hub_err.strip():
        for line in hub_err.splitlines():
            print("! hub: " + line)

    check(hub.returncode == 0, "hub: exits 0 after bye", hub.returncode)
    try:
        tail = json.loads(hub_out.strip().splitlines()[-1])
        check(tail.get("bye") is True, "hub: saw an explicit bye", tail)
        check(tail.get("served") == 1, "hub: served exactly one peer process", tail)
        check(tail.get("exchanges", 0) >= 25, "hub: counted the exchanges", tail)
    except Exception as exc:  # noqa: BLE001
        check(False, "hub: final line is JSON", f"{exc}: {hub_out!r}")

    if failures:
        print(f"crp_closed_loop: {checks} checks, {len(failures)} FAILURES")
        for f in failures:
            print("  FAIL " + f)
        return 1
    print(f"crp_closed_loop: {checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
