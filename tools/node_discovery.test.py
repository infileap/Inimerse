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
from testports import (distinct_ports, reserve_port as free_port,  # noqa: E402
                       wait_http_ping)


def start_hub(engine, root, http_port, hub_dir, tcp_port=None):
    (root / "hub.im").write_text('say "hub"\nwait 90\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
    if tcp_port is None:
        # A held allocation, not a bare free_port(): siblings must not
        # be handed the same number as this hub's listen port.
        tcp_port = distinct_ports(1)[0]
    proc = subprocess.Popen([str(engine), "--headless", "--port", str(tcp_port),
                             "--http-port", str(http_port), str(root / "hub.im")],
                            cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return proc


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
        # hub, fake-directory and hub2 are alive at overlapping times; allocate
        # them together so they are guaranteed distinct.
        hub_port, fake_port, hub2_port = distinct_ports(3)
        hub_tcp, hub2_tcp = distinct_ports(2)
        hub = start_hub(engine, root, hub_port, root / "universe", hub_tcp)
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
            fake = HTTPServer(("127.0.0.1", fake_port), FakeDirectory)
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
            hub2 = start_hub(engine, root, hub2_port, root / "universe2", hub2_tcp)
            try:
                assert wait_port(hub2_port), "second hub did not start"
                multi = root / "multi.im"
                multi.write_text(
                    'say "add1=" + str(verse_hub_add("127.0.0.1:%d"))\n'
                    'say "add2=" + str(verse_hub_add("127.0.0.1:%d"))\n'
                    'p1 = verse_hub_ping("127.0.0.1:%d")\n'
                    'p2 = verse_hub_ping("127.0.0.1:%d")\n'
                    'say "ping_ok=" + str(p1 > 0 and p2 > 0)\n'
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
