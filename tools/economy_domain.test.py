"""Economy domain isolation, auditable settlement and bridges (M4 §43/§22).

Definitions, balances, issuance and settlement are separate objects; money
only enters through auditable mint/settle events in a hash-chained ledger;
different economic domains cannot transfer directly, and a retried settlement
must never move funds twice:

  - currency definitions must be signed by their issuer (currency_id = hash of
    the canonical definition); forged signatures are refused
  - in-domain settlement moves funds and bumps versions
  - replaying the same idempotency key reports replay without moving funds
  - cross-domain transfers are refused explicitly (never a silent conversion)
  - mint requires the issuer's signature and appears in the audit ledger
  - the audit chain recomputes cleanly; a bridge can be declared but its
    execution is reported as not implemented rather than faked
"""
import hashlib
import json
import os
import re
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
from testports import distinct_ports, wait_http_ping  # noqa: E402


def wait_port(port, timeout=10.0):
    # Shared implementation in tools/testports.py -- these suites need the
    # /ping round trip, not merely a TCP accept (see testports.wait_http_ping).
    return wait_http_ping(port, timeout=timeout)


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


def seed_for(name):
    return hashlib.sha256(f"econ-issuer-{name}".encode()).hexdigest()


def declare_domain_via_script(engine, root, home, hub_port, domain, kind="utility"):
    """Returns (currency_id, issuer_pubkey) declared with a fixed identity so
    later scripts (mint) can reconstruct the same issuer key."""
    seed = seed_for(domain)
    script = root / f"domain-{domain}.im"
    script.write_text(
        'verse_identity_new()\n'
        f'write_file("{home}/universe/identity.seed", "{seed}")\n'
        f'cid = verse_econ_domain("127.0.0.1:{hub_port}", "{domain}", "{kind}", "coin", "domain-only")\n'
        'say "cid=" + cid\n'
        'say "issuer=" + verse_identity_pubkey()\n', encoding="utf-8")
    rc = run_script(engine, script, home, root)
    out = rc.stdout.decode(errors="replace")
    assert "cid=" in out and len(out.split("cid=")[1].split("\n")[0].strip()) == 64, out + rc.stderr.decode(errors="replace")
    return out.split("cid=")[1].split("\n")[0].strip(), out.split("issuer=")[1].strip()


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-econ-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()
        # Held simultaneously: a bare free_port() is TOCTOU, and under
        # `ctest -j12` another suite can claim the same number in the gap,
        # making the engine's bind fail with EADDRINUSE.
        hub_port, hub_tcp_port = distinct_ports(2)
        hub_script = root / "hub.im"
        hub_script.write_text('say "hub"\nwait 120\n', encoding="utf-8")
        env = dict(os.environ, INIMERSE_HUB_DIR=str(root / "universe"))
        hub = subprocess.Popen([str(engine), "--headless", "--port", str(hub_tcp_port),
                                "--http-port", str(hub_port), str(hub_script)],
                               cwd=root, env=env, stdout=subprocess.DEVNULL,
                               stderr=open(root / "hub.log", "w"))
        try:
            assert wait_port(hub_port), "hub did not start"

            # 1. two signed currency domains in different domains
            cid_a, issuer_a = declare_domain_via_script(engine, root, home, hub_port, "alpha")
            cid_b, _ = declare_domain_via_script(engine, root, home, hub_port, "beta")
            assert cid_a != cid_b

            # a forged issuer signature is refused
            canon = f"gamma|{'ab'*32}|utility||coin|domain-only"
            status, body = http_json(hub_port, "POST", "/economy/domain", {
                "domain_id": "gamma", "issuer": "ab" * 32, "value_kind": "utility",
                "denomination": "coin", "transfer_policy": "domain-only", "signature": "cd" * 64})
            assert status == 400 and "invalid_signature" in body, (status, body)

            # an unknown value kind is refused: the four kinds never merge (§43.1)
            status, body = http_json(hub_port, "POST", "/economy/domain", {
                "domain_id": "delta", "issuer": "ab" * 32, "value_kind": "crypto",
                "denomination": "coin", "signature": "cd" * 64})
            assert status == 400 and "invalid_value_kind" in body, (status, body)

            # 2. mint requires the issuer signature (script holds the identity)
            mint_script = root / "mint.im"
            mint_script.write_text(
                f'write_file("{home}/universe/identity.seed", "{seed_for("alpha")}")\n'
                f'ok = verse_econ_mint("127.0.0.1:{hub_port}", "{cid_a}", "alpha/alice", "500", "mint-1")\n'
                'say "mint=" + str(ok)\n'
                f'b = verse_econ_balance("127.0.0.1:{hub_port}", "{cid_a}", "alpha/alice")\n'
                'say "amount=" + str(b["amount"]) + " version=" + str(b["version"])\n', encoding="utf-8")
            rc = run_script(engine, mint_script, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "mint=1" in out, out + rc.stderr.decode(errors="replace")
            assert "amount=500 version=1" in out, out

            # a mint signed by someone else fails (service account holds a
            # different identity because it never created this currency)
            impostor = root / "impostor.im"
            impostor.write_text(
                f'write_file("{home}/universe/identity.seed", "{seed_for("impostor")}")\n'
                f'ok = verse_econ_mint("127.0.0.1:{hub_port}", "{cid_a}", "alpha/alice", "1000", "mint-forged")\n'
                'say "forged_mint=" + str(ok)\n', encoding="utf-8")
            rc = run_script(engine, impostor, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "forged_mint=0" in out, out + rc.stderr.decode(errors="replace")

            # 3. in-domain settlement moves funds
            status, settle = http_json(hub_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "alpha/bob",
                "amount": 200, "idempotency_key": "pay-1"})
            assert status == 200, (status, settle)
            res = json.loads(settle)
            assert res["status"] == "settled" and res["balance_to"] == 200, res

            # 4. replaying the same key must not move funds again
            status, again = http_json(hub_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "alpha/bob",
                "amount": 200, "idempotency_key": "pay-1"})
            assert status == 200, (status, again)
            res2 = json.loads(again)
            assert res2["status"] == "replay", res2
            assert res2["balance_to"] == 200, res2
            _, bal = http_json(hub_port, "GET", f"/economy/balance?currency_id={cid_a}&account=alpha/bob")
            assert json.loads(bal)["amount"] == 200, bal

            # insufficient funds are refused
            status, poor = http_json(hub_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "alpha/bob",
                "amount": 100000, "idempotency_key": "pay-2"})
            assert status == 409 and "insufficient_balance" in poor, (status, poor)

            # 5. cross-domain transfer is denied (no silent conversion)
            status, cross = http_json(hub_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "beta/carol",
                "amount": 10, "idempotency_key": "cross-1"})
            assert status == 409, (status, cross)
            denied = json.loads(cross)
            assert denied["error"] == "cross_domain_transfer_denied", denied
            assert "bridge" in denied["hint"], denied

            # accounts must state their domain
            status, bad_acct = http_json(hub_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alice", "to": "bob",
                "amount": 1, "idempotency_key": "bad-1"})
            assert status == 400 and "invalid_account_format" in bad_acct, (status, bad_acct)

            # 6. the audit chain recomputes cleanly (mint + transfer)
            status, audit = http_json(hub_port, "GET", f"/economy/audit?currency_id={cid_a}")
            ledger = json.loads(audit)
            assert ledger["chain_ok"] == 1, ledger
            kinds = [e["kind"] for e in ledger["events"]]
            assert kinds.count("mint") == 1 and kinds.count("transfer") == 1, ledger
            prev = "0"
            for ev in ledger["events"]:
                # recompute the chain exactly as the hub documents it:
                # hash = sha256(prev || seq|kind|currency|from|to|amount|idem)
                canon = (f'{ev["seq"]}|{ev["kind"]}|{ev["currency_id"]}|{ev["from"]}|'
                         f'{ev["to"]}|{ev["amount"]}|{ev["idem"]}')
                expect = hashlib.sha256((prev + canon).encode()).hexdigest()
                assert ev["prev"] == prev, (ev, prev)
                assert ev["hash"] == expect, (ev, expect)
                prev = ev["hash"]

            # a bridge can be declared; execution is explicitly not implemented
            status, bridge = http_json(hub_port, "POST", "/economy/bridge", {
                "source_domain": "alpha", "target_domain": "beta",
                "rate": "2:1", "fee": "1%", "limit": "1000",
                "oracle": "local", "rollback_policy": "refund"})
            assert status == 200, (status, bridge)
            br = json.loads(bridge)
            assert br["ok"] is True and br["execution"] == "not_implemented", br
            _, listed = http_json(hub_port, "GET", "/economy/bridge?source_domain=alpha&target_domain=beta")
            bridges = json.loads(listed)["bridges"]
            assert len(bridges) == 1 and bridges[0]["execution"] == "not_implemented", listed

            # 7. script-space views
            view = root / "view.im"
            view.write_text(
                f'a = verse_econ_audit("127.0.0.1:{hub_port}", "{cid_a}")\n'
                'say "audit_count=" + str(a["count"]) + " chain_ok=" + str(a["chain_ok"])\n'
                'r = verse_econ_settle("127.0.0.1:%d", "%s", "alpha/bob", "alpha/alice", "50", "pay-3")\n'
                'say "settle_status=" + r["status"] + " balance=" + str(r["balance_to"])\n'
                % (hub_port, cid_a), encoding="utf-8")
            rc = run_script(engine, view, home, root)
            out = rc.stdout.decode(errors="replace")
            assert "audit_count=2 chain_ok=1" in out, out + rc.stderr.decode(errors="replace")
            # balance_to is the receiver (alice): 500 - 200 + 50 = 350
            assert "settle_status=settled balance=350" in out, out
        except AssertionError:
            log = (root / "hub.log")
            if log.exists():
                sys.stderr.write("--- hub log tail ---\n" + "".join(log.read_text(errors="replace").splitlines(True)[-10:]))
            raise
        finally:
            hub.terminate()
            try:
                hub.wait(timeout=5)
            except subprocess.TimeoutExpired:
                hub.kill()
    print("economy domain: ok")


if __name__ == "__main__":
    main()
