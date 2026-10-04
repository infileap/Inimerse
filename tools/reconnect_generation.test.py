"""Reconnect after an authority change (M4 §55.6).

A dropped connection is not a departure, and an authority handoff starts a new
sequence domain — so a reattach that carries the old generation must be told
to take a snapshot with last_applied reset to 0, never to replay across the
generation boundary (which could resurrect stale authority):

  - within one generation, reattach replays missing events from last_committed
  - handoff to another node bumps the generation and clears the event window
  - reattaching with the old generation answers snapshot_required /
    authority_changed / last_applied 0
  - reattaching with the new generation resumes cleanly
  - side-effecting requests are gated by idempotency keys (new vs replay)
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent

# The hub-side portal enrollment secret: each hub below is started with it, and
# the /portal call proves possession of it.  Without it the hub refuses every
# /portal outright, so there would be no token to reconnect with.
ENROLL = "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    for _dir in ("build", "build-local", "build-windows-gcc", "build-py"):
        for _name in ("inimerse", "inimerse.exe"):
            cands.append(HERE.parent / _dir / _name)
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


# Port allocation lives in tools/testports.py: the old hand-rolled
# bind(0)/close() had a time-of-check/time-of-use window that made
# hub_dist_regression fail under `ctest -j12` while passing in isolation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from testports import (start_hub_bound_ports,  # noqa: E402
                       wait_http_ping)


def wait_port(port, timeout=10.0):
    # Shared implementation in tools/testports.py -- these suites need the
    # /ping round trip, not merely a TCP accept (see testports.wait_http_ping).
    return wait_http_ping(port, timeout=timeout)


def start_hub(engine, root, hub_dir):
    """Start a hub; return `(proc, http_port, tcp_port)`.

    The hub asks the kernel for both numbers (`--port 0 --http-port 0`) and
    prints what it bound, which this suite reads back.  Nothing is reserved and
    nothing is released, so the release-to-bind window in docs/STATUS.md 2.9 --
    where a sibling suite's pool takes the number before the child binds it --
    no longer exists.  Raises testports.HubStartError, carrying the hub's
    stderr, if the engine never reports its ports.
    """
    script = root / f"{hub_dir.name}.im"
    script.write_text('say "hub"\nwait 120\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir),
               CRP_ENROLL_SECRET=ENROLL)
    proc, tcp_port, http_port = start_hub_bound_ports(
        [engine, "--headless", "--port", "0", "--http-port", "0", script],
        cwd=root, env=env, log_path=root / f"{hub_dir.name}.log")
    return proc, http_port, tcp_port


def http_json(port, method, path, payload=None):
    import http.client
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    body = json.dumps(payload) if payload is not None else None
    conn.request(method, path, body=body)
    resp = conn.getresponse()
    text = resp.read().decode(errors="replace")
    conn.close()
    return resp.status, text


def run_script(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=cwd,
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


def publish_node(engine, root, home, hub_port, endpoint_port, expires):
    """Publish a signed advertisement and return the node id."""
    script = root / f"adv-{endpoint_port}.im"
    script.write_text(
        'verse_identity_new()\n'
        f'endpoint = "127.0.0.1:{endpoint_port}"\n'
        'caps = "events,relay"\n'
        f'expires = "{expires}"\n'
        'payload = endpoint + "|" + caps + "|" + expires\n'
        'sig = verse_sign(payload)\n'
        f'say "adv=" + str(verse_node_advertise("127.0.0.1:{hub_port}", sig, expires, endpoint, caps))\n'
        'say "node=" + verse_identity_pubkey()\n', encoding="utf-8")
    rc = run_script(engine, script, home, root)
    out = rc.stdout.decode(errors="replace")
    assert "adv=1" in out, out + rc.stderr.decode(errors="replace")
    return out.split("node=")[1].strip().split("\n")[0]


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-reconnect-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()
        # Each hub chooses its own ports and reports them, so no hub port is
        # reserved, released or guessed here (docs/STATUS.md 2.9).
        started = [start_hub(engine, root, root / f"u{i}") for i in range(3)]
        hubs = [proc for proc, _http, _tcp in started]
        dir_port, node_a_port, node_b_port = (http for _proc, http, _tcp in started)
        try:
            for p in (dir_port, node_a_port, node_b_port):
                assert wait_port(p), f"hub {p} did not start"
            expires = str(int(time.time() * 1000) + 300000)
            node_a = publish_node(engine, root, home, dir_port, node_a_port, expires)
            node_b = publish_node(engine, root, home, dir_port, node_b_port, expires)

            # session with three committed events under generation 1
            status, auth_body = http_json(dir_port, "POST", "/session/authority", {
                "verse": "v1", "peer": "p1", "authority": node_a})
            assert status == 200, (status, auth_body)
            # /signal requires a capability token scoped to (verse, peer), and
            # /portal issues one only against the enrollment proof.
            import base64
            import hashlib
            import hmac as hmac_mod
            import re
            def enroll_proof(verse, peer):
                return base64.urlsafe_b64encode(
                    hmac_mod.new(ENROLL.encode(), f"{verse}\0{peer}".encode(),
                                 hashlib.sha256).digest()).rstrip(b"=").decode()
            status, no_auth = http_json(dir_port, "POST", "/portal", {"verse": "v1", "peer": "p1"})
            assert status == 403, (status, no_auth)
            # A proof is not enough on its own: /portal mints only for a verse
            # this hub registered (the reference's `verses.has(p.verse)`), so an
            # unregistered verse is refused with 404 and /register lifts it.
            status, unreg = http_json(dir_port, "POST", "/portal", {
                "verse": "v-unregistered", "peer": "p1",
                "auth": enroll_proof("v-unregistered", "p1")})
            assert status == 404, (status, unreg)
            status, reg = http_json(dir_port, "POST", "/register",
                                    {"id": "v1", "endpoint": "127.0.0.1:9000"})
            assert status == 200, (status, reg)
            status, portal = http_json(dir_port, "POST", "/portal", {
                "verse": "v1", "peer": "p1", "auth": enroll_proof("v1", "p1")})
            assert status == 200, (status, portal)
            token = re.search(r'"token":"([^"]+)"', portal).group(1)
            for seq in (1, 2, 3):
                status, sig = http_json(dir_port, "POST", "/signal", {
                    "verse": "v1", "peer": "p1", "seq": seq, "event": f"e{seq}", "token": token})
                assert status == 200, (status, sig)
            # an unauthenticated reattach is refused only when a bad token is
            # supplied; without one the response is explicit about auth state
            status, body = http_json(dir_port, "POST", "/session/reattach", {
                "verse": "v1", "peer": "p1", "generation": 1, "token": "not-a-token"})
            assert status == 403, (status, body)

            # 1. reattach in the same generation replays from last_committed
            status, body = http_json(dir_port, "POST", "/session/reattach", {
                "verse": "v1", "peer": "p1", "generation": 1,
                "last_received_sequence": 3, "last_committed_sequence": 1})
            assert status == 200, (status, body)
            plan = json.loads(body)
            assert plan["resume"] == "replay", plan
            assert plan["replay_from"] == 2 and plan["last_applied"] == 3, plan
            assert [e["seq"] for e in plan["replay"]] == [2, 3], plan

            # 2. state is observable
            _, state = http_json(dir_port, "GET", "/session/state?verse=v1&peer=p1")
            st = json.loads(state)
            assert st["state"] == "connected" and st["generation"] == 1, st

            # 3. hand off to node B (generation 2, old tail dropped)
            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "from_authority": node_a,
                "to_authority": node_b,
                "snapshot_hash": "22" * 32,
                "event_tail_hash": __import__("hashlib").sha256(b"e3").hexdigest()})
            assert status == 200 and '"ok":true' in body, (status, body)

            # 4. reattaching with the OLD generation must ask for a snapshot and
            #    reset last_applied to 0 (never replay across generations)
            status, body = http_json(dir_port, "POST", "/session/reattach", {
                "verse": "v1", "peer": "p1", "generation": 1,
                "last_received_sequence": 3, "last_committed_sequence": 3})
            assert status == 200, (status, body)
            plan = json.loads(body)
            assert plan["resume"] == "snapshot_required", plan
            assert plan["reason"] == "authority_changed", plan
            assert plan["last_applied"] == 0, plan
            assert plan["generation"] == 2, plan

            # 5. reattaching with the NEW generation resumes with an empty tail
            status, body = http_json(dir_port, "POST", "/session/reattach", {
                "verse": "v1", "peer": "p1", "generation": 2,
                "last_received_sequence": 0, "last_committed_sequence": 0})
            plan = json.loads(body)
            assert plan["resume"] == "replay" and plan["last_applied"] == 0, plan
            assert plan["replay"] == [], plan

            # 6. idempotency gate for side-effecting requests
            status, first = http_json(dir_port, "POST", "/session/idem", {
                "verse": "v1", "peer": "p1", "key": "settle-1"})
            assert json.loads(first)["status"] == "new", first
            status, second = http_json(dir_port, "POST", "/session/idem", {
                "verse": "v1", "peer": "p1", "key": "settle-1"})
            assert json.loads(second)["status"] == "replay", second

            # 7. script-space reconnect view
            script = root / "reattach.im"
            script.write_text(
                f'r = verse_session_reattach("127.0.0.1:{dir_port}", "v1", "p1", "1", "3", "3")\n'
                'say "resume=" + r["resume"]\n'
                'say "reason=" + r["reason"]\n'
                'say "last_applied=" + str(r["last_applied"])\n'
                f's = verse_session_state("127.0.0.1:{dir_port}", "v1", "p1")\n'
                'say "state=" + s["state"]\n'
                'say "gen=" + str(s["generation"])\n'
                f'i1 = verse_idem_begin("127.0.0.1:{dir_port}", "v1", "p1", "pay-9")\n'
                f'i2 = verse_idem_begin("127.0.0.1:{dir_port}", "v1", "p1", "pay-9")\n'
                'say "idem=" + i1 + "/" + i2\n', encoding="utf-8")
            rc = run_script(engine, script, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "resume=snapshot_required" in out, out + rc.stderr.decode(errors="replace")
            assert "reason=authority_changed" in out, out
            assert "last_applied=0" in out, out
            assert "state=connected" in out and "gen=2" in out, out
            assert "idem=new/replay" in out, out
        finally:
            for h in hubs:
                h.terminate()
                try:
                    h.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    h.kill()
    print("reconnect generation: ok")


if __name__ == "__main__":
    main()
