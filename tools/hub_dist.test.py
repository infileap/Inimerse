"""Embedded hub distribution regression (M2, white paper §55.4/§46.2).

Covers the POSIX embedded hub end to end:
  - TCP and UDP package distribution on the same port, byte-identical bodies
    (`GET /v/<id>` is the dialect verse_fetch/verse_udp_fetch speak)
  - `verse_open("verse://host:port/id")` (HTTP) and
    `verse_open("verse://udp://host:port/id")` (UDP) both succeed from a script
  - `verse_listen(port)` starts a serving hub from script space
"""
import base64
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAYLOAD = b"Inimerse hub distribution payload \x00\x01\x02 end"


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
from testports import (distinct_ports,  # noqa: E402
                       start_hub_bound_ports,
                       wait_http_ping)


def package_json(pkg_id="testpkg"):
    main = b'say "remote verse ok"\n'
    pkg = {"id": pkg_id, "main": "main.im", "files": {"main.im": base64.b64encode(main).decode()}}
    return pkg


def upload_blob(port, pkg_id, blob):
    """Store raw bytes as the .vverse file for pkg_id (the hub returns the
    whole package file from /v/<id>; clients unpack it)."""
    body = json.dumps({"id": pkg_id, "data": base64.b64encode(blob).decode()})
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    conn.request("POST", "/package", body=body)
    resp = conn.getresponse()
    resp.read()
    conn.close()
    assert resp.status == 201, f"upload failed: {resp.status}"


def upload_package(port, pkg):
    upload_blob(port, pkg["id"], json.dumps(pkg).encode())


def tcp_fetch(port, pkg_id):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    conn.request("GET", f"/v/{pkg_id}")
    resp = conn.getresponse()
    data = resp.read()
    conn.close()
    assert resp.status == 200, f"tcp /v/ failed: {resp.status}"
    return data


def udp_fetch(port, pkg_id, timeout=5.0):
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.settimeout(timeout)
    u.sendto(f"GET /v/{pkg_id}".encode(), ("127.0.0.1", port))
    data, _ = u.recvfrom(65536)
    u.close()
    return data


def wait_port(port, timeout=10.0):
    # TCP-accept readiness only; this suite's engine answers no /ping.
    from testports import wait_port as _wait_port
    return _wait_port(port, timeout=timeout)


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-hub-") as td:
        root = Path(td)
        hub_dir = root / "universe"
        hub_dir.mkdir()
        (root / "hub.im").write_text('say "hub"\nwait 90\n', encoding="utf-8")
        # The hub's own TCP and HTTP ports are kernel-assigned and read back
        # from the lines it prints, so this suite never holds a number it is
        # not using (docs/STATUS.md 2.9).  Only the verse_listen port that a
        # *script* has to name is still reserved here: verse_listen cannot
        # report an ephemeral port back to the script.
        listen_port = distinct_ports(1)[0]
        env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
        hub, _tcp_port, http_port = start_hub_bound_ports(
            [str(engine), "--headless", "--port", "0", "--http-port", "0",
             str(root / "hub.im")],
            cwd=root, env=env, log_path=root / "hub.log")
        try:
            assert wait_port(http_port), "hub did not start"

            # TCP/UDP distribution must return the package file byte for byte
            upload_blob(http_port, "blobpkg", PAYLOAD)
            tcp_data = tcp_fetch(http_port, "blobpkg")
            udp_data = udp_fetch(http_port, "blobpkg")
            assert tcp_data == PAYLOAD, f"tcp body mismatch: {tcp_data[:40]!r}"
            assert udp_data == PAYLOAD, f"udp body mismatch: {udp_data[:40]!r}"
            # a real package for the script-space client test
            upload_package(http_port, package_json())

            # script-space client over both transports
            script = root / "fetch.im"
            script.write_text(
                f'a = verse_open("verse://127.0.0.1:{http_port}/testpkg")\n'
                'say "http_open=" + str(a)\n'
                f'b = verse_open("verse://udp://127.0.0.1:{http_port}/testpkg")\n'
                'say "udp_open=" + str(b)\n', encoding="utf-8")
            home = root / "home"
            home.mkdir()
            rc = subprocess.run([str(engine), "run", str(script)],
                                cwd=root, env=dict(env, HOME=str(home)),
                                capture_output=True, timeout=60)
            out = rc.stdout.decode(errors="replace")
            assert "http_open=1" in out, out + rc.stderr.decode(errors="replace")
            assert "udp_open=1" in out, out + rc.stderr.decode(errors="replace")

            # verse_listen starts a serving hub from script space
            server = root / "serve.im"
            server.write_text(
                f'p = verse_listen({listen_port})\n'
                'say "listening=" + str(p)\n'
                'wait 30\n', encoding="utf-8")
            srv = subprocess.Popen([str(engine), "run", str(server)],
                                   cwd=root, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            try:
                assert wait_port(listen_port), "verse_listen did not open the port"
                # the serving hub answers the same dialect
                srv_udp = udp_fetch(listen_port, "testpkg")
                assert b'"main": "main.im"' in srv_udp, f"verse_listen udp mismatch: {srv_udp[:80]!r}"
            finally:
                srv.terminate()
                try:
                    srv.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    srv.kill()
        finally:
            hub.terminate()
            try:
                hub.wait(timeout=5)
            except subprocess.TimeoutExpired:
                hub.kill()
    print("hub distribution: ok")


if __name__ == "__main__":
    main()
