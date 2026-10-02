"""CRP session flow regression (M2, white paper §55.3/§55.6).

Starts a headless hub from the engine, then runs the Node websocket flow
test which asserts: protocol version mismatch is rejected explicitly,
message gaps produce resume_required with the correct last_applied, and
duplicates do not advance the sequence.

Run directly or via CTest (crp_session_flow_regression).
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
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
from testports import distinct_ports  # noqa: E402
from testports import wait_http_ping  # noqa: E402


def wait_ready(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as c:
                c.sendall(b"GET /health HTTP/1.0\r\nHost: x\r\n\r\n")
                if b"ok" in c.recv(512):
                    return True
        except OSError:
            time.sleep(0.1)
    return False


def main():
    engine = find_engine()
    node = shutil.which("node")
    assert node, "node is required for the websocket flow test"
    with tempfile.TemporaryDirectory(prefix="inimerse-crp-") as td:
        root = Path(td)
        (root / "hub.im").write_text('say "hub"\nwait 60\n', encoding="utf-8")
        # distinct_ports() holds both at once: two free_port() calls in one
        # expression can return the same number and break the engine's bind.
        tcp_port, http_port = distinct_ports(2)
        proc = subprocess.Popen(
            [str(engine), "--headless", "--port", str(tcp_port),
             "--http-port", str(http_port), "hub.im"],
            cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            assert wait_ready(http_port), "hub did not become ready"
            rc = subprocess.run([node, str(HERE / "crp_session_flow.test.js"),
                                 f"ws://127.0.0.1:{http_port}"],
                                capture_output=True, timeout=30)
            assert rc.returncode == 0, rc.stderr.decode(errors="replace") + rc.stdout.decode(errors="replace")
            assert b"crp session flow: ok" in rc.stdout, rc.stdout
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    print("crp session flow: ok")


if __name__ == "__main__":
    main()
