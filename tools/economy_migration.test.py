#!/usr/bin/env python3
"""Economy migration over the hub's HTTP bridge (M4 §43.5).

A currency's economy must be able to leave one runtime and continue in another
without trusting the exporter's bookkeeping: the migration package carries the
signed definition, a balance snapshot and the hash-chained ledger, and the
importer re-derives every digest from the package instead of believing it.

Verified here against the hub implementation:

  - /economy/export of an unknown currency is refused (currency_not_found)
  - /economy/export of a live currency returns a package whose balances_hash,
    ledger_tail, definition_hash and content_hash all recompute from the
    exported bytes, and whose ledger chain links correctly
  - that package imports into a fresh hub and reports "imported", echoing the
    ledger_tail and balances_hash it was built from
  - re-importing the same package is idempotent ("already_present") and does
    not consume extra slots in the import registry
  - a second, different package for an already-imported currency is refused
    (conflict) instead of silently overwriting the import
  - a package edited inside the ledger no longer matches its tail and is
    refused (ledger_chain_broken) -- the chain commits to every entry
  - a package whose balances_hash field was edited is refused (integrity_failed),
    and one whose snapshot contradicts its own balances_hash must be refused too
  - an imported ledger never appears as locally committed history: the target
    hub's audit chain and balances are unchanged by a successful import, while
    the imported currency does become mintable there
  - GET /economy/domain/<cid> resolves a currency declared on the hub
  - a package that is byte-identical to the export except for JSON whitespace
    (a legal re-encoding) still imports

Requests are sent as the exact bytes /economy/export returned wherever the
package itself is under test, so no re-encoding artefact can be mistaken for
an implementation result; tampered variants are produced by editing those
bytes in place (one field, everything else identical).
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
FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name}: {detail}")
        FAILURES.append(f"{name}: {detail}")
    return bool(cond)


def brief(obj, n=500):
    s = (obj if isinstance(obj, str) else json.dumps(obj)).strip()
    return s if len(s) <= n else s[:n] + "...(truncated)"


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
from testports import start_hub_bound_ports  # noqa: E402
from testports import wait_http_ping  # noqa: E402


def wait_port(port, timeout=10.0):
    # Shared implementation in tools/testports.py -- these suites need the
    # /ping round trip, not merely a TCP accept (see testports.wait_http_ping).
    return wait_http_ping(port, timeout=timeout)


def post_raw(port, path, raw):
    """POST the given bytes with no client-side re-encoding."""
    import http.client
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    conn.request("POST", path, body=raw)
    resp = conn.getresponse()
    text = resp.read().decode(errors="replace")
    conn.close()
    return resp.status, text


def http_json(port, method, path, payload=None):
    import http.client
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    body = json.dumps(payload) if payload is not None else None
    conn.request(method, path, body=body)
    resp = conn.getresponse()
    text = resp.read().decode(errors="replace")
    conn.close()
    return resp.status, text


def start_hub(engine, cwd, script, log):
    """Start one hub on kernel-assigned ports; return `(proc, http_port)`.

    Both of the hub's ports are the kernel's choice (`--port 0 --http-port 0`)
    and are read back from the engine's own startup lines, so this suite never
    reserves and never releases a port for a child.  It used to pass in both
    numbers from one pool, which left a release-to-bind window another suite's
    pool could win (docs/STATUS.md 2.9, ~1 ctest -j12 run in 80); earlier still,
    allocating a listen port inside this function with its own
    ``distinct_ports(1)`` let one land on a held HTTP port (measured 9/500) --
    the engine's second bind then failed with EADDRINUSE and it came up with
    *no HTTP service at all* while the process stayed alive, the exact stderr
    signature being a printed ``headless: 127.0.0.1:N`` and ``[0]="hub"`` but
    no ``http api:`` line, so every later request hung until the suite's
    readiness budget expired.  Neither failure is possible now.

    Raises testports.HubStartError (carrying the hub's stderr) if the engine
    never reports its ports.
    """
    cwd.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, INIMERSE_HUB_DIR=str(cwd / "universe"))
    proc, _tcp_port, http_port = start_hub_bound_ports(
        [engine, "--headless", "--port", "0", "--http-port", "0", script],
        cwd=str(cwd), env=env, log_path=log)
    return proc, http_port


def run_script(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=str(cwd),
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


def seed_for(name):
    return hashlib.sha256(f"econ-issuer-{name}".encode()).hexdigest()


def declare_domain_via_script(engine, workdir, home, hub_port, domain, kind="utility"):
    """Returns (currency_id, issuer_pubkey) declared with a fixed identity so
    later scripts (mint) can reconstruct the same issuer key."""
    seed = seed_for(domain)
    script = workdir / f"domain-{domain}.im"
    script.write_text(
        'verse_identity_new()\n'
        f'write_file("{home}/universe/identity.seed", "{seed}")\n'
        f'cid = verse_econ_domain("127.0.0.1:{hub_port}", "{domain}", "{kind}", "coin", "domain-only")\n'
        'say "cid=" + cid\n'
        'say "issuer=" + verse_identity_pubkey()\n', encoding="utf-8")
    rc = run_script(engine, script, home, workdir)
    out = rc.stdout.decode(errors="replace")
    assert "cid=" in out and len(out.split("cid=")[1].split("\n")[0].strip()) == 64, \
        out + rc.stderr.decode(errors="replace")
    return out.split("cid=")[1].split("\n")[0].strip(), out.split("issuer=")[1].strip()


def mint_once(engine, workdir, home, hub_port, identity, cid, account, amount, key):
    """Run a mint script and return its verdict; verse_econ_mint reports mint=1
    only when the hub answered "settled", so mint=0 means it was refused."""
    script = workdir / f"mint-{identity}-{key}.im"
    script.write_text(
        f'write_file("{home}/universe/identity.seed", "{seed_for(identity)}")\n'
        f'ok = verse_econ_mint("127.0.0.1:{hub_port}", "{cid}", "{account}", "{amount}", "{key}")\n'
        'say "mint=" + str(ok)\n', encoding="utf-8")
    rc = run_script(engine, script, home, workdir)
    out = rc.stdout.decode(errors="replace") + rc.stderr.decode(errors="replace")
    m = re.search(r"mint=\d", out)
    return m.group(0) if m else out


def mint(engine, workdir, home, hub_port, identity, cid, account, amount, key):
    out = mint_once(engine, workdir, home, hub_port, identity, cid, account, amount, key)
    assert "mint=1" in out, out


def sha256_hex(text):
    return hashlib.sha256(text.encode()).hexdigest()


def balances_digest(balances):
    """Recompute the hub's order-independent balance digest: sha256 over
    "<account>|<amount>|<version>\\n" lines sorted by account."""
    payload = "".join(f'{b["account"]}|{b["amount"]}|{b["version"]}\n'
                      for b in sorted(balances, key=lambda b: b["account"]))
    return sha256_hex(payload)


def links_ok(currency_id, entries):
    """Recompute the ledger chain the way the hub documents it:
    hash = sha256(prev || seq|kind|currency|from|to|amount|idem), starting from
    the first entry's own prev (a per-currency slice can begin mid-chain)."""
    if not entries:
        return True, "0"
    prev = entries[0]["prev"]
    for ev in entries:
        canon = (f'{ev["seq"]}|{ev["kind"]}|{currency_id}|{ev["from"]}|'
                 f'{ev["to"]}|{ev["amount"]}|{ev["idem"]}')
        if ev["prev"] != prev or ev["hash"] != sha256_hex(prev + canon):
            return False, prev
        prev = ev["hash"]
    return True, prev


def content_hash_of(pkg):
    return sha256_hex("1|{}|{}|{}|{}|{}|{}".format(
        pkg.get("service_id") or "local", pkg["exported_at"], pkg["currency"]["currency_id"],
        pkg["definition_hash"], pkg["ledger_tail"], pkg["balances_hash"]))


def flip_hex(h):
    return ("0" if h[0] != "0" else "1") + h[1:]


def export_package(port, cid):
    """POST /economy/export and return (status, raw_body_text)."""
    return post_raw(port, "/economy/export", json.dumps({"currency_id": cid}, separators=(",", ":")))


def _ledger_split(raw):
    at = raw.index('"ledger":[')
    return raw[:at], raw[at:]


def tamper_entry_amount(raw, index, delta=1):
    """Change ledger entry `index`'s amount by `delta`; every other byte stays
    identical (hash fields, ledger_tail and content_hash are untouched)."""
    head, tail = _ledger_split(raw)
    m = list(re.finditer(r'"amount":(-?\d+)', tail))[index]
    new = '"amount":%d' % (int(m.group(1)) + delta)
    return head + tail[:m.start()] + new + tail[m.end():]


def tamper_last_ledger_hash(raw):
    """Flip one hex digit of the LAST ledger entry's hash field, leaving the
    package's claimed ledger_tail untouched."""
    head, tail = _ledger_split(raw)
    m = list(re.finditer(r'"hash":"([0-9a-f]{64})"', tail))[-1]
    return head + tail[:m.start()] + '"hash":"%s"' % flip_hex(m.group(1)) + tail[m.end():]


def delete_first_ledger_entry(raw):
    """Remove the first ledger entry object, keeping the tail hash intact."""
    head, tail = _ledger_split(raw)
    start = tail.index("{")
    end = tail.index("},", start) + 2
    return head + tail[:start] + tail[end:]


def tamper_balance_amount(raw):
    """Change the first balance entry's amount, leaving balances_hash and
    content_hash exactly as exported (the package contradicts itself)."""
    m = re.search(r'"balances":\[\{"account":"[^"]*","amount":(-?\d+)', raw)
    return raw[:m.start(1)] + str(int(m.group(1)) + 1) + raw[m.end(1):]


def tamper_balances_hash(raw):
    """Flip one hex digit of the package's own balances_hash field."""
    m = re.search(r'"balances_hash":"([0-9a-f]{64})"', raw)
    return raw[:m.start(1)] + flip_hex(m.group(1)) + raw[m.end(1):]


def main():
    engine = find_engine()
    hubs = []
    with tempfile.TemporaryDirectory(prefix="inimerse-econ-mig-") as td:
        root = Path(td)
        work = root / "work"
        work.mkdir()
        home = root / "home"
        home.mkdir()
        hub_script = root / "hub.im"
        hub_script.write_text('say "hub"\nwait 120\n', encoding="utf-8")

        # Six hubs come up during this suite, each on two kernel-assigned
        # ports that it reports itself, so there is nothing left to reserve --
        # and therefore no release-to-bind window for a sibling suite to win
        # (docs/STATUS.md 2.9).  This suite used to hold twelve numbers in one
        # pool for that purpose.

        def start(role):
            proc, http_port = start_hub(engine, root / role, hub_script,
                                        root / f"{role}.log")
            hubs.append(proc)
            assert wait_port(http_port), f"{role} hub did not start"
            return http_port

        try:
            src_port = start("src")
            tgt_port = start("tgt")

            # ---- source hub: two signed currencies with real history ----
            cid_a, issuer_a = declare_domain_via_script(engine, work, home, src_port, "alpha")
            cid_b, issuer_b = declare_domain_via_script(engine, work, home, src_port, "beta")
            assert cid_a != cid_b

            mint(engine, work, home, src_port, "alpha", cid_a, "alpha/alice", "500", "mint-a")
            status, settle = http_json(src_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "alpha/bob",
                "amount": 200, "idempotency_key": "pay-a1"})
            assert status == 200 and json.loads(settle)["status"] == "settled", (status, settle)

            mint(engine, work, home, src_port, "beta", cid_b, "beta/alice", "700", "mint-b")
            status, settle = http_json(src_port, "POST", "/economy/settle", {
                "currency_id": cid_b, "from": "beta/alice", "to": "beta/bob",
                "amount": 100, "idempotency_key": "pay-b1"})
            assert status == 200 and json.loads(settle)["status"] == "settled", (status, settle)

            # ---- 1. export of a currency that does not exist ----
            status, body = export_package(src_port, "ab" * 32)
            check("export unknown currency -> 404 currency_not_found",
                  status == 404 and "currency_not_found" in body,
                  f"status={status} body={brief(body)}")

            # ---- 2. export of a live currency: the package recomputes ----
            status, raw_a = export_package(src_port, cid_a)
            check("export live currency -> 200", status == 200, f"status={status} body={brief(raw_a)}")
            pkg_a = json.loads(raw_a) if status == 200 else {}
            assert pkg_a, f"export of a live currency produced no package: {brief(raw_a)}"

            check("export declares format_version 1 / service_id local",
                  pkg_a.get("format_version") == 1 and pkg_a.get("service_id") == "local", brief(pkg_a))
            check("export carries the exported currency definition",
                  pkg_a["currency"]["currency_id"] == cid_a
                  and pkg_a["currency"]["issuer"] == issuer_a, brief(pkg_a.get("currency")))
            got_bal = {b["account"]: (b["amount"], b["version"]) for b in pkg_a["balances"]}
            check("exported balance snapshot matches history",
                  got_bal == {"alpha/alice": (300, 2), "alpha/bob": (200, 1)}, repr(got_bal))
            check("balances_hash recomputes from the exported snapshot",
                  pkg_a["balances_hash"] == balances_digest(pkg_a["balances"]),
                  f'claim={pkg_a["balances_hash"]} calc={balances_digest(pkg_a["balances"])}')
            check("definition_hash recomputes from the exported definition",
                  pkg_a["definition_hash"] == sha256_hex(pkg_a["currency"]["definition"]),
                  f'claim={pkg_a["definition_hash"]}')
            ok, tail = links_ok(cid_a, pkg_a["ledger"])
            check("exported ledger chain links correctly from genesis",
                  ok and len(pkg_a["ledger"]) == 2 and pkg_a["ledger"][0]["prev"] == "0",
                  f"ok={ok} entries={brief(pkg_a['ledger'])}")
            check("ledger_tail is the last exported ledger hash",
                  pkg_a["ledger_tail"] == tail == pkg_a["ledger"][-1]["hash"],
                  f'tail={pkg_a["ledger_tail"]} last={pkg_a["ledger"][-1]["hash"]}')
            check("content_hash seals the exported meta",
                  pkg_a["content_hash"] == content_hash_of(pkg_a),
                  f'claim={pkg_a["content_hash"]} calc={content_hash_of(pkg_a)}')

            status, raw_b = export_package(src_port, cid_b)
            assert status == 200, (status, raw_b)
            pkg_b = json.loads(raw_b)
            ok_b, tail_b = links_ok(cid_b, pkg_b["ledger"])
            check("beta package chain links correctly and its tail matches",
                  ok_b and pkg_b["ledger_tail"] == tail_b == pkg_b["ledger"][-1]["hash"],
                  f"ok={ok_b} entries={brief(pkg_b['ledger'])}")
            # the retained ledger is one global chain: a per-currency slice can
            # begin at a prev hash that lives outside the package
            print(f"  [note] beta slice starts at prev={pkg_b['ledger'][0]['prev'][:16]}... = "
                  f"alpha's tail ({pkg_a['ledger_tail'][:16]}...): a per-currency package is not "
                  f"verifiable from genesis in isolation")

            # ---- the observable a verifier would use to resolve a definition ----
            status, body = http_json(src_port, "GET", f"/economy/domain/{cid_a}")
            print(f"  [req ] GET /economy/domain/{cid_a} (alpha IS declared on this hub)")
            print(f"  [resp] status={status} body={brief(body)}")
            check("GET /economy/domain/<cid> resolves a currency declared on this hub",
                  status == 200 and json.loads(body).get("currency_id") == cid_a,
                  f"status={status} body={brief(body)}")

            # ---- baseline on the target hub BEFORE any import ----
            status, body = http_json(tgt_port, "GET", f"/economy/audit?currency_id={cid_b}")
            audit_before = json.loads(body)
            status, body = http_json(tgt_port, "GET", f"/economy/balance?currency_id={cid_b}&account=beta/alice")
            balance_before = json.loads(body)
            status, body = http_json(tgt_port, "GET", f"/economy/domain/{cid_b}")
            domain_before = status
            mint_before = mint_once(engine, work, home, tgt_port, "beta", cid_b,
                                    "beta/carol", "5", "mint-b-before")
            check("target hub has no local beta history before the import",
                  audit_before["count"] == 0 and balance_before["amount"] == 0 and domain_before == 404,
                  brief(body))
            print(f"  [req ] mint on the TARGET hub for beta before the import")
            print(f"  [resp] {brief(mint_before.strip())}")
            check("target hub cannot mint beta before the import",
                  "mint=1" not in mint_before, brief(mint_before))

            # ---- 3. successful export -> import round trip (bytes verbatim) ----
            status, body = post_raw(tgt_port, "/economy/import", raw_a)
            res = json.loads(body)
            check("round-trip import of the exported bytes -> 200 imported",
                  status == 200 and res.get("status") == "imported",
                  f"status={status} body={brief(body)}")
            check("import echoes the ledger_tail and balances_hash of the package",
                  res.get("ledger_tail") == pkg_a["ledger_tail"]
                  and res.get("balances_hash") == pkg_a["balances_hash"], brief(body))
            check("import echoes the imported currency_id",
                  res.get("currency_id") == cid_a, brief(body))

            # ---- 4. re-importing the same package is idempotent ----
            status, body = post_raw(tgt_port, "/economy/import", raw_a)
            check("re-importing the same package -> already_present",
                  status == 200 and json.loads(body).get("status") == "already_present",
                  f"status={status} body={brief(body)}")
            repeat = (status, body)
            for _ in range(19):
                repeat = post_raw(tgt_port, "/economy/import", raw_a)
                if not (repeat[0] == 200 and json.loads(repeat[1]).get("status") == "already_present"):
                    break
            check("20 more re-imports stay already_present (no duplicate registry slot)",
                  repeat[0] == 200 and json.loads(repeat[1]).get("status") == "already_present",
                  f"status={repeat[0]} body={brief(repeat[1])}")

            # ---- 5. a successfully imported ledger is not local history ----
            status, body = post_raw(tgt_port, "/economy/import", raw_b)
            check("second currency imports successfully", status == 200
                  and json.loads(body).get("status") == "imported",
                  f"status={status} body={brief(body)}")

            status, body = http_json(tgt_port, "GET", f"/economy/audit?currency_id={cid_b}")
            audit_after = json.loads(body)
            check("imported ledger does not enter the target audit chain",
                  audit_after["count"] == 0 and audit_after["chain_ok"] == 1
                  and audit_after["events"] == [] and audit_after == audit_before, brief(body))
            status, body = http_json(tgt_port, "GET", f"/economy/balance?currency_id={cid_b}&account=beta/alice")
            balance_after = json.loads(body)
            check("imported balances are not applied locally (beta/alice still 0)",
                  balance_after["amount"] == 0 and balance_after["version"] == 0
                  and balance_after == balance_before, brief(body))
            status, body = http_json(tgt_port, "GET", f"/economy/audit?currency_id={cid_a}")
            alpha_audit = json.loads(body)
            check("the round-tripped currency has no local events either",
                  alpha_audit["count"] == 0 and alpha_audit["chain_ok"] == 1, brief(body))
            status, body = http_json(tgt_port, "GET", f"/economy/domain/{cid_b}")
            domain_after = status
            print(f"  [note] GET /economy/domain/<beta> on the target hub: "
                  f"{domain_before} before import, {domain_after} after")
            print("  [note] the definition-registration side effect of an import cannot be "
                  "observed through GET /economy/domain/<cid>: see the domain-endpoint case above")

            # ---- 6. a different package for an already-imported currency ----
            status, settle = http_json(src_port, "POST", "/economy/settle", {
                "currency_id": cid_a, "from": "alpha/alice", "to": "alpha/bob",
                "amount": 50, "idempotency_key": "pay-a2"})
            assert status == 200 and json.loads(settle)["status"] == "settled", (status, settle)
            status, raw_a2 = export_package(src_port, cid_a)
            assert status == 200, (status, raw_a2)
            pkg_a2 = json.loads(raw_a2)
            check("a later export of the same currency is a different package",
                  pkg_a2["content_hash"] != pkg_a["content_hash"]
                  and pkg_a2["balances_hash"] != pkg_a["balances_hash"], brief(raw_a2))
            status, body = post_raw(tgt_port, "/economy/import", raw_a2)
            err = json.loads(body).get("error")
            check("importing a different package for an imported currency -> 409 conflict",
                  status == 409 and err == "conflict", f"status={status} body={brief(body)}")

            # ---- 7. integrity of the package envelope ----
            status, body = post_raw(tgt_port, "/economy/import",
                                    json.dumps({k: v for k, v in pkg_a.items() if k != "content_hash"},
                                               separators=(",", ":")))
            check("import without content_hash -> 400 currency_content_hash_definition_required",
                  status == 400 and "currency_content_hash_definition_required" in body,
                  f"status={status} body={brief(body)}")

            # a sealed meta field, when edited, must not survive the seal check
            tampered_hash = tamper_balances_hash(raw_b)
            status, body = post_raw(tgt_port, "/economy/import", tampered_hash)
            print(f"  [req ] balances_hash field tamper: "
                  f"{pkg_b['balances_hash'][:16]}... -> {json.loads(tampered_hash)['balances_hash'][:16]}...")
            print(f"  [resp] status={status} body={brief(body)}")
            check("import with a tampered balances_hash field -> 409 integrity_failed",
                  status == 409 and json.loads(body).get("error") == "integrity_failed",
                  f"status={status} body={brief(body)}")

            # the balance snapshot itself is covered by nothing: balances_hash is
            # taken as a claim and never re-derived from the balances array, so a
            # package whose snapshot contradicts its own digest is still accepted
            tampered_balance = tamper_balance_amount(raw_b)
            tbal_pkg = json.loads(tampered_balance)
            assert tbal_pkg["balances"][0]["amount"] != pkg_b["balances"][0]["amount"]
            check("(premise) the edited snapshot no longer recomputes to its balances_hash",
                  balances_digest(tbal_pkg["balances"]) != pkg_b["balances_hash"],
                  f'calc={balances_digest(tbal_pkg["balances"])} claim={pkg_b["balances_hash"]}')
            check("(premise) the snapshot-tampered package keeps the exported digests",
                  tbal_pkg["balances_hash"] == pkg_b["balances_hash"]
                  and tbal_pkg["content_hash"] == pkg_b["content_hash"], brief(tampered_balance))
            bal_port = start("bal")
            status, body = post_raw(bal_port, "/economy/import", tampered_balance)
            print(f"  [req ] balance snapshot tamper: balances[0].amount="
                  f"{pkg_b['balances'][0]['amount']} -> {tbal_pkg['balances'][0]['amount']}, "
                  f"balances_hash/content_hash exactly as exported")
            print(f"  [resp] status={status} body={brief(body)}")
            check("import of a snapshot that contradicts its own balances_hash is refused",
                  status != 200, f"status={status} body={brief(body)}")

            # ---- 8. the chain must recommit to every entry it accepts ----
            t_tail = tamper_last_ledger_hash(raw_b)
            assert json.loads(t_tail)["ledger"][-1]["hash"] != pkg_b["ledger"][-1]["hash"]
            status, body = post_raw(tgt_port, "/economy/import", t_tail)
            print(f"  [req ] ledger tail tamper: ledger[-1].hash="
                  f"{pkg_b['ledger'][-1]['hash'][:16]}... -> "
                  f"{json.loads(t_tail)['ledger'][-1]['hash'][:16]}..., ledger_tail field unchanged")
            print(f"  [resp] status={status} body={brief(body)}")
            check("import with a tampered ledger tail hash -> 409 ledger_chain_broken",
                  status == 409 and json.loads(body).get("error") == "ledger_chain_broken",
                  f"status={status} body={brief(body)}")

            # the same package with one ledger entry's amount edited (hash field,
            # ledger_tail and content_hash left exactly as exported): the chain no
            # longer commits to the entry, so this must be refused as broken
            t_amt = tamper_entry_amount(raw_b, 0, +1)
            t_amt_pkg = json.loads(t_amt)
            premise_ok, _ = links_ok(cid_b, t_amt_pkg["ledger"])
            check("(premise) an edited ledger entry breaks the chain locally", not premise_ok,
                  brief(t_amt_pkg["ledger"]))
            check("(premise) the tampered package keeps the exported ledger_tail "
                  "and content_hash",
                  t_amt_pkg["ledger_tail"] == pkg_b["ledger_tail"]
                  and t_amt_pkg["content_hash"] == pkg_b["content_hash"], brief(t_amt))
            amt_port = start("amt")
            status, body = post_raw(amt_port, "/economy/import", t_amt)
            print(f"  [req ] ledger entry tamper: ledger[0].amount="
                  f"{pkg_b['ledger'][0]['amount']} -> {t_amt_pkg['ledger'][0]['amount']}, "
                  f"all hash fields, ledger_tail and content_hash exactly as exported")
            print(f"  [resp] status={status} body={brief(body)}")
            check("import with a tampered ledger entry amount -> 409 ledger_chain_broken",
                  status == 409 and json.loads(body).get("error") == "ledger_chain_broken",
                  f"status={status} body={brief(body)}")

            # dropping history entirely must not pass either: the remaining tail
            # hash still matches ledger_tail, but the chain is missing its head
            t_drop = delete_first_ledger_entry(raw_b)
            t_drop_pkg = json.loads(t_drop)
            check("(premise) the edited package really lost its first ledger entry",
                  len(t_drop_pkg["ledger"]) == len(pkg_b["ledger"]) - 1
                  and t_drop_pkg["ledger_tail"] == pkg_b["ledger_tail"], brief(t_drop))
            drop_port = start("drop")
            status, body = post_raw(drop_port, "/economy/import", t_drop)
            print(f"  [req ] ledger head deletion: ledger entries "
                  f"{len(pkg_b['ledger'])} -> {len(t_drop_pkg['ledger'])}, "
                  f"ledger_tail/hash fields unchanged")
            print(f"  [resp] status={status} body={brief(body)}")
            check("import with the first ledger entry deleted -> 409 ledger_chain_broken",
                  status == 409 and json.loads(body).get("error") == "ledger_chain_broken",
                  f"status={status} body={brief(body)}")

            # ---- 9. legal JSON whitespace must not change the verdict ----
            assert '"ledger":[' in raw_b
            spaced = raw_b.replace('"ledger":[', '"ledger": [', 1)
            assert spaced != raw_b and json.loads(spaced) == pkg_b
            ws_port = start("ws")
            status, body = post_raw(ws_port, "/economy/import", spaced)
            print(f"  [req ] re-encoding: '\\\"ledger\\\":[' -> '\\\"ledger\\\": [' "
                  f"({len(spaced) - len(raw_b):+d} byte), JSON-equivalent to the export")
            print(f"  [resp] status={status} body={brief(body)}")
            check("an untampered package with legal JSON whitespace -> 200 imported",
                  status == 200 and json.loads(body).get("status") == "imported",
                  f"status={status} body={brief(body)}")

            # ---- 10. an imported currency is usable on the target hub ----
            # GET /economy/domain/<cid> cannot answer this (it is broken, see the
            # domain-endpoint case), so ask the mint path, which resolves the
            # currency through the same registry and verifies the registration.
            mint_after = mint_once(engine, work, home, tgt_port, "beta", cid_b,
                                   "beta/carol", "5", "mint-b-after")
            print(f"  [req ] mint on the TARGET hub for the imported beta, account beta/carol")
            print(f"  [resp] {brief(mint_after.strip())}")
            check("the imported definition is resolvable and mintable on the target hub",
                  "mint=1" in mint_after, brief(mint_after))
        except AssertionError:
            for log in sorted(root.glob("*.log")):
                if log.exists():
                    sys.stderr.write(f"--- {log.name} tail ---\n"
                                     + "".join(log.read_text(errors="replace").splitlines(True)[-10:]))
            raise
        finally:
            for p in hubs:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()

    if FAILURES:
        print(f"economy migration: FAIL ({len(FAILURES)} failed)")
        for f in FAILURES:
            print(f"  - {f}")
        sys.exit(1)
    print("economy migration: ok")


if __name__ == "__main__":
    main()
