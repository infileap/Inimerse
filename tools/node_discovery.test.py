"""Node discovery + verifiable advertisements regression (M4 first block, §55.2).

A hub directory accepts only signed, unexpired node advertisements; clients
re-verify every claim locally so a lying directory cannot invent nodes:

  - script publishes a signed advertisement (identity = signing key)
  - the hub rejects forged signatures and expired claims explicitly
  - verse_node_discover returns entries with verified/reason and source fields
  - against a *fake* directory (a local HTTP server returning forged claims)
    the client marks every node unverified -> trust comes from the signature,
    not from the directory
  - multiple hubs: verse_hub_add/verse_hub_ping work for two hubs and
    verse_node_discover reads them independently
"""
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    cands += [HERE.parent / "build" / "inimerse", HERE.parent / "build-local" / "inimerse"]
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


def start_hub(engine, root, hub_dir):
    """Start a hub; return `(proc, http_port, tcp_port)`.

    Both ports are kernel-assigned (`--port 0 --http-port 0`) and read back
    from the engine's own startup lines, so this suite never reserves or
    releases a hub port and the release-to-bind race in docs/STATUS.md 2.9
    cannot happen.  Raises testports.HubStartError (carrying the hub's stderr)
    if the engine never reports a port.
    """
    (root / "hub.im").write_text('say "hub"\nwait 90\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
    proc, tcp_port, http_port = start_hub_bound_ports(
        [engine, "--headless", "--port", "0", "--http-port", "0", str(root / "hub.im")],
        cwd=root, env=env, log_path=root / f"{hub_dir.name}.log")
    return proc, http_port, tcp_port


def wait_port(port, timeout=10.0):
    # Shared implementation in tools/testports.py -- these suites need the
    # /ping round trip, not merely a TCP accept (see testports.wait_http_ping).
    return wait_http_ping(port, timeout=timeout)


PUBLISH = '''\
verse_identity_new()
endpoint = "127.0.0.1:{port}"
caps = "events,snapshot,udp"
expires = "{expires}"
payload = endpoint + "|" + caps + "|" + expires
sig = verse_sign(payload)
r = verse_node_advertise("127.0.0.1:{hub}", sig, expires, endpoint, caps)
say "advertise=" + str(r)
'''

# note: a `say` immediately followed by `if` is the modifier form
# (`say X if cond`), so keep an assignment between them.
DISCOVER = '''\
v = verse_node_discover("127.0.0.1:{hub}")
c = v["count"]
say "count=" + str(c)
z = 0
if c > 0 {{
    n0 = v["nodes"][0]
    say "verified=" + str(n0["verified"])
    say "reason=" + n0["reason"]
    say "endpoint=" + n0["endpoint"]
    say "caps=" + n0["caps"]
}}
'''


def run_script(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=cwd,
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


def post_json(port, path, payload):
    import http.client
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    conn.request("POST", path, body=json.dumps(payload))
    resp = conn.getresponse()
    body = resp.read().decode(errors="replace")
    conn.close()
    return resp.status, body


class FakeDirectory(BaseHTTPRequestHandler):
    """A directory that fabricates node claims nobody can have signed."""
    def do_GET(self):
        if self.path.startswith("/node/discover"):
            node = {
                "node_id": "aa" * 32,
                "payload": "127.0.0.1:1\nrelay\n99999999999999",
                "signature": "bb" * 64,
                "endpoint": "127.0.0.1:1",
                "caps": "relay",
                "expires_at": 99999999999999,
                "source": "directory",
                "observed_at": 0,
                "evidence_ref": "sha256:deadbeef",
                "superseded": 0,
            }
            body = json.dumps({"nodes": [node], "expired": 0}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()
    def log_message(self, *a):
        pass


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-nodes-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()
        # Nothing here reserves a port by number.  The hubs ask the kernel
        # (`--port 0`) and report what they got; the fake directory does the
        # same by binding port 0 and reading the number back off the listening
        # socket.  Reserving a number first and binding it later leaves a
        # release-to-bind window, and under `ctest -j4` a sibling suite that
        # asked for the same number in that window wins the race and this suite
        # dies with `OSError: [Errno 98] Address already in use` before a single
        # assertion runs.  That is not hypothetical: 4 failures in 120 runs at
        # 32-way load, which is what docs/STATUS.md 10.38 records.  See
        # docs/STATUS.md 2.9 for the original version of this race.
        hub, hub_port, _hub_tcp = start_hub(engine, root, root / "universe")
        try:
            assert wait_port(hub_port), "hub did not start"
            expires = str(int(time.time() * 1000) + 120000)

            # 1. publish a signed advertisement
            pub = root / "pub.im"
            pub.write_text(PUBLISH.format(port=hub_port, hub=hub_port, expires=expires), encoding="utf-8")
            rc = run_script(engine, pub, home, root)
            assert b"advertise=1" in rc.stdout, rc.stdout + rc.stderr

            # 2. discover: verified entry with provenance
            disc = root / "disc.im"
            disc.write_text(DISCOVER.format(hub=hub_port), encoding="utf-8")
            rc = run_script(engine, disc, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "count=1" in out, out + rc.stderr.decode(errors="replace")
            assert "verified=1" in out, out
            assert "reason=ok" in out, out
            assert f"endpoint=127.0.0.1:{hub_port}" in out, out
            assert "caps=events,snapshot,udp" in out, out

            # 3. the hub rejects forged signatures...
            status, body = post_json(hub_port, "/node/advertise", {
                "node_id": "cc" * 32, "payload": "x\ny\n1", "signature": "dd" * 64,
                "endpoint": "h:1", "caps": "relay", "expires_at": 0,
            })
            assert status == 400 and "invalid_signature" in body, (status, body)

            # ...and expired claims (§55.2: no silent merge of stale claims)
            status, body = post_json(hub_port, "/node/advertise", {
                "node_id": "cc" * 32, "payload": "x\ny\n1", "signature": "dd" * 64,
                "endpoint": "h:1", "caps": "relay", "expires_at": 1,
            })
            assert status == 400 and "expired_advertisement" in body, (status, body)

            # 4. a lying directory cannot make the client trust a node
            # Bind first, then learn the port: the socket is held from the
            # moment the kernel assigns the number, so no sibling can take it.
            fake = HTTPServer(("127.0.0.1", 0), FakeDirectory)
            fake_port = fake.server_address[1]
            threading.Thread(target=fake.serve_forever, daemon=True).start()
            try:
                disc.write_text(DISCOVER.format(hub=fake_port), encoding="utf-8")
                rc = run_script(engine, disc, home, root)
                out = rc.stdout.decode(errors="replace")
                assert "count=1" in out, out + rc.stderr.decode(errors="replace")
                assert "verified=0" in out, out
                assert "reason=bad_signature" in out, out
            finally:
                fake.shutdown()

            # 5. two hubs: list, ping and discover independently
            hub2, hub2_port, _hub2_tcp = start_hub(engine, root, root / "universe2")
            try:
                assert wait_port(hub2_port), "second hub did not start"
                multi = root / "multi.im"
                multi.write_text(
                    'say "add1=" + str(verse_hub_add("127.0.0.1:%d"))\n'
                    'say "add2=" + str(verse_hub_add("127.0.0.1:%d"))\n'
                    'p1 = verse_hub_ping("127.0.0.1:%d")\n'
                    'p2 = verse_hub_ping("127.0.0.1:%d")\n'
                    'say "ping_ok=" + str(p1 > 0 and p2 > 0)\n'
                    # Report the two round trips separately: the bare boolean
                    # cannot say *which* hub went missing, and a failure that
                    # names neither the hub nor the value is a dead end -- the
                    # first recorded occurrence of this flake cost a full
                    # investigation and produced no mechanism.
                    'say "ping_ms=" + str(p1) + "," + str(p2)\n'
                    'v2 = verse_node_discover("127.0.0.1:%d")\n'
                    'say "hub2_count=" + str(v2["count"])\n'
                    % (hub_port, hub2_port, hub_port, hub2_port, hub2_port),
                    encoding="utf-8")
                rc = run_script(engine, multi, home, root)
                out = rc.stdout.decode(errors="replace")
                assert "add1=1" in out and "add2=1" in out, out + rc.stderr.decode(errors="replace")
                assert "ping_ok=true" in out or "ping_ok=1" in out, out
                assert "hub2_count=0" in out, out  # no advertisement on hub2 yet
                # publish to hub2 and see it there
                pub2 = root / "pub2.im"
                pub2.write_text(PUBLISH.format(port=hub2_port, hub=hub2_port, expires=expires), encoding="utf-8")
                rc = run_script(engine, pub2, home, root)
                assert b"advertise=1" in rc.stdout, rc.stdout + rc.stderr
                rc = run_script(engine, multi, home, root)
                out = rc.stdout.decode(errors="replace")
                assert "hub2_count=1" in out, out + rc.stderr.decode(errors="replace")
            finally:
                hub2.terminate()
                try:
                    hub2.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    hub2.kill()
        finally:
            hub.terminate()
            try:
                hub.wait(timeout=5)
            except subprocess.TimeoutExpired:
                hub.kill()
    print("node discovery: ok")


if __name__ == "__main__":
    main()
