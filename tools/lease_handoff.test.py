"""Session authority, lease health and node handoff regression (M4 §55.5).

A directory must observe node health instead of assuming it, schedulers must
stop assigning authority to unhealthy nodes, and a handoff may only transfer
authority after the target proved healthy and the checkpoint/event tail match
— verification failure keeps the source authority (no silent split-brain):

  - healthy nodes (endpoints answering /ping) report health=up
  - a node with a dead endpoint reports health=down and is excluded from
    /node/schedule (with the exclusion counted, not dropped silently)
  - handoff A->B succeeds, bumps the generation and freezes the source
  - a mismatching event tail or checkpoint is refused and the authority stays
  - an unhealthy or unknown target is refused
"""
import hashlib
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
from testports import (distinct_ports,  # noqa: E402
                       wait_http_ping)


def wait_port(port, timeout=10.0):
    # Shared implementation in tools/testports.py -- these suites need the
    # /ping round trip, not merely a TCP accept (see testports.wait_http_ping).
    return wait_http_ping(port, timeout=timeout)


def start_hub(engine, root, http_port, hub_dir, tcp_port=None):
    (root / f"hub{http_port}.im").write_text('say "hub"\nwait 120\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
    if tcp_port is None:
        # Reached only if a caller forgets to pass one.  This pool knows
        # nothing about the HTTP ports the caller holds, so the number can
        # collide with one of them -- measured 9/500 -- and the engine then
        # comes up with no HTTP service at all (bind fails EADDRINUSE while
        # the process stays alive).  Pass a port from the caller's own
        # distinct_ports() batch instead of relying on this.
        tcp_port = distinct_ports(1)[0]
    return subprocess.Popen([str(engine), "--headless", "--port", str(tcp_port),
                             "--http-port", str(http_port), str(root / f"hub{http_port}.im")],
                            cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_script(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=cwd,
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


def node_advertise_script(hub_port, endpoint_port, expires, home_unused=None):
    """Script that creates an identity, signs and publishes an advertisement."""
    return f'''\
verse_identity_new()
endpoint = "127.0.0.1:{endpoint_port}"
caps = "events,relay"
expires = "{expires}"
payload = endpoint + "|" + caps + "|" + expires
sig = verse_sign(payload)
r = verse_node_advertise("127.0.0.1:{hub_port}", sig, expires, endpoint, caps)
say "advertise=" + str(r)
say "node=" + verse_identity_pubkey()
'''


def http_json(port, method, path, payload=None):
    import http.client
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    body = json.dumps(payload) if payload is not None else None
    conn.request(method, path, body=body)
    resp = conn.getresponse()
    text = resp.read().decode(errors="replace")
    conn.close()
    return resp.status, text


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-lease-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()

        # All four at once, held together, so no two of them can be the same
        # number.  (Three separate free_port() calls could collide; and the
        # dead port must never be derived by +1, which could land on a real hub.)
        # One call, not two: separate calls build separate pools, and the
        # second can be handed a number the first already returned (measured
        # 5/200 at count 6).  An overlap means a hub binds a port that is
        # already taken, fails with EADDRINUSE, and never listens.
        dir_port, node_a_port, node_b_port, dead_port, *tcp_ports = distinct_ports(7)
        hubs = [start_hub(engine, root, p, root / f"universe{p}", tcp)
                for p, tcp in zip((dir_port, node_a_port, node_b_port), tcp_ports)]
        try:
            for p in (dir_port, node_a_port, node_b_port):
                assert wait_port(p), f"hub {p} did not start"

            expires = str(int(time.time() * 1000) + 300000)
            node_ids = {}
            for name, ep_port in (("A", node_a_port), ("B", node_b_port), ("C", dead_port)):
                script = root / f"adv{name}.im"
                script.write_text(node_advertise_script(dir_port, ep_port, expires), encoding="utf-8")
                rc = run_script(engine, script, home, root)
                # publication is idempotent-safe to retry (a fresh identity is
                # generated per attempt); a persistent failure is a real bug
                for _ in range(3):
                    if b"advertise=1" in rc.stdout:
                        break
                    time.sleep(0.3)
                    rc = run_script(engine, script, home, root)
                assert b"advertise=1" in rc.stdout, rc.stdout + rc.stderr
                node_ids[name] = rc.stdout.decode().split("node=")[1].strip().split("\n")[0]

            # 1. discover reports observed health (up for A/B, down for the dead
            #    endpoint); observation is asynchronous, so wait for it
            health = {}
            for attempt in range(60):
                _, disc = http_json(dir_port, "GET", "/node/discover")
                data = json.loads(disc)
                health = {n["node_id"]: n["health"] for n in data["nodes"]}
                if (health.get(node_ids["A"]) == "up" and health.get(node_ids["B"]) == "up"
                        and health.get(node_ids["C"]) == "down"):
                    break
                time.sleep(0.25)
            assert health.get(node_ids["A"]) == "up", health
            assert health.get(node_ids["B"]) == "up", health
            assert health.get(node_ids["C"]) == "down", health

            # 2. scheduler view excludes the unhealthy node, and says so
            _, sched = http_json(dir_port, "GET", "/node/schedule")
            sched_data = json.loads(sched)
            scheduled = {n["node_id"] for n in sched_data["nodes"]}
            assert node_ids["A"] in scheduled and node_ids["B"] in scheduled, sched_data
            assert node_ids["C"] not in scheduled, sched_data
            assert sched_data["excluded"]["unhealthy"] >= 1, sched_data

            # capability filter is likewise reported
            _, sched_flt = http_json(dir_port, "GET", "/node/schedule?caps=events")
            assert json.loads(sched_flt)["count"] >= 2, sched_flt

            # 3. script-space scheduler view only lists healthy nodes
            sched_script = root / "sched.im"
            sched_script.write_text(
                f'v = verse_node_schedule("127.0.0.1:{dir_port}")\n'
                'say "sched_count=" + str(v["count"])\n'
                'n0 = v["nodes"][0]\n'
                'say "health0=" + n0["health"]\n', encoding="utf-8")
            rc = run_script(engine, sched_script, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "sched_count=2" in out, out + rc.stderr.decode(errors="replace")
            assert "health0=up" in out, out

            # 4. register authority on A, then hand off to B
            empty_tail = hashlib.sha256(b"").hexdigest()
            snapshot = hashlib.sha256(b"checkpoint-1").hexdigest()
            status, body = http_json(dir_port, "POST", "/session/authority", {
                "verse": "v1", "peer": "p1", "authority": node_ids["A"]})
            assert status == 200, (status, body)

            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "from_authority": node_ids["A"],
                "to_authority": node_ids["B"], "snapshot_hash": snapshot,
                "event_tail_hash": empty_tail, "rules_version": "1"})
            assert status == 200 and '"ok":true' in body, (status, body)
            result = json.loads(body)
            assert result["generation"] == 2 and result["authority"] == node_ids["B"], result

            _, auth = http_json(dir_port, "GET", "/session/authority?verse=v1&peer=p1")
            auth_data = json.loads(auth)
            assert auth_data["authority"] == node_ids["B"], auth_data
            assert auth_data["generation"] == 2, auth_data
            assert auth_data["checkpoint"] == snapshot, auth_data

            # 5. a mismatching event tail is refused and authority does not move
            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "from_authority": node_ids["B"],
                "to_authority": node_ids["A"], "snapshot_hash": snapshot,
                "event_tail_hash": "00" * 32, "rules_version": "1"})
            assert status == 409 and "event_tail_mismatch" in body, (status, body)
            _, auth = http_json(dir_port, "GET", "/session/authority?verse=v1&peer=p1")
            assert json.loads(auth)["authority"] == node_ids["B"], auth

            # 6. a mismatching checkpoint is refused too
            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "from_authority": node_ids["B"],
                "to_authority": node_ids["A"], "snapshot_hash": "11" * 32,
                "event_tail_hash": empty_tail, "rules_version": "1"})
            assert status == 409 and "checkpoint_mismatch" in body, (status, body)

            # 7. unhealthy and unknown targets are refused
            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "to_authority": node_ids["C"],
                "snapshot_hash": snapshot, "event_tail_hash": empty_tail})
            assert status == 503 and "target_unhealthy" in body, (status, body)
            status, body = http_json(dir_port, "POST", "/node/handoff", {
                "verse": "v1", "peer": "p1", "to_authority": "ff" * 32,
                "snapshot_hash": snapshot, "event_tail_hash": empty_tail})
            assert status == 404 and "target_unknown" in body, (status, body)

            # 8. script-space authority read after the successful handoff
            auth_script = root / "auth.im"
            auth_script.write_text(
                f'a = verse_session_authority("127.0.0.1:{dir_port}", "v1", "p1")\n'
                'say "authority=" + a["authority"]\n'
                'say "generation=" + str(a["generation"])\n', encoding="utf-8")
            rc = run_script(engine, auth_script, home, root)
            out = rc.stdout.decode(errors="replace")
            assert f"authority={node_ids['B']}" in out, out + rc.stderr.decode(errors="replace")
            assert "generation=2" in out, out

            # 9. script-space handoff to A (source B), carrying the same tail
            handoff_script = root / "handoff.im"
            handoff_script.write_text(
                f'r = verse_node_handoff("127.0.0.1:{dir_port}", "v1", "p1", '
                f'"{node_ids["A"]}", "{snapshot}", "{empty_tail}")\n'
                'say "handoff=" + str(r)\n', encoding="utf-8")
            rc = run_script(engine, handoff_script, home, root)
            assert b"handoff=1" in rc.stdout, rc.stdout + rc.stderr
            _, auth = http_json(dir_port, "GET", "/session/authority?verse=v1&peer=p1")
            assert json.loads(auth)["authority"] == node_ids["A"], auth
            assert json.loads(auth)["generation"] == 3, auth
        finally:
            for h in hubs:
                h.terminate()
                try:
                    h.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    h.kill()
    print("lease handoff: ok")


if __name__ == "__main__":
    main()
