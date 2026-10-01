"""Replay closure regression (Infiverse M1, roadmap §52 protocol-core 3+4).

Covers the deterministic replay loop end to end:
  - named random streams seeded via replay_seed are reproducible across runs
  - replay_log appends chained-hash event envelopes (§24.2 minimal form)
  - replay_verify validates the chain and reports IntegrityError on tampering
  - identical seeds produce identical state hashes (deterministic execution)
"""
import hashlib
import os
import subprocess
import sys
import tempfile
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


def run(engine, *args, cwd):
    return subprocess.run([str(engine), *args], cwd=cwd, capture_output=True, timeout=60)


SIM = '''\
replay_seed("world", 2026)
replay_seed("loot", 777)
replay_log_begin("run.elog", "replay-test")

state = {"tick": 0, "score": 0, "loot": []}
i = 0
while i < 5 {
    replay_tick(1)
    roll = replay_rand("loot", 100)
    state["tick"] = replay_time()
    state["score"] = state["score"] + roll
    push(state["loot"], roll)
    replay_log("loot.roll", {"step": i, "roll": roll, "tick": replay_time()})
    i = i + 1
}
h = replay_state_hash(state)
v = replay_verify("run.elog")
if !v["ok"] {
    say "INTEGRITY: " + v["category"] + " " + v["message"]
}
say "state_hash=" + h
say "verify_ok=" + str(v["ok"]) + " events=" + str(v["count"])
'''

VERIFY = '''\
v = replay_verify("run.elog")
if !v["ok"] {
    say "TAMPER: " + v["category"] + " " + v["message"]
}
say "ok=" + str(v["ok"]) + " count=" + str(v["count"]) + " last=" + str(v["last_seq"])
'''


def main():
    engine = find_engine()
    with tempfile.TemporaryDirectory(prefix="inimerse-replay-") as td:
        root = Path(td)
        (root / "sim.im").write_text(SIM, encoding="utf-8")
        (root / "verify.im").write_text(VERIFY, encoding="utf-8")

        # run 1: log written, chain verifies, deterministic hash reported
        rc = run(engine, "run", "sim.im", cwd=root)
        assert rc.returncode == 0, rc.stderr.decode(errors="replace")
        out1 = rc.stdout.decode(errors="replace")
        assert "verify_ok=true events=5" in out1, out1
        h1 = out1.split("state_hash=")[1].split("\n")[0]

        # run 2 (fresh process): identical seed -> identical output = determinism
        rc = run(engine, "run", "sim.im", cwd=root)
        out2 = rc.stdout.decode(errors="replace")
        h2 = out2.split("state_hash=")[1].split("\n")[0]
        assert h1 == h2, f"non-deterministic state hash: {h1} vs {h2}"
        assert hashlib.sha256(out2.encode()).hexdigest() == hashlib.sha256(out1.encode()).hexdigest()

        # log exists and verifies clean
        log = root / "run.elog"
        assert log.is_file()
        rc = run(engine, "run", "verify.im", cwd=root)
        assert rc.returncode == 0, rc.stderr.decode(errors="replace")
        assert "ok=true count=5" in rc.stdout.decode(errors="replace"), rc.stdout

        # tamper with any payload byte -> IntegrityError with chain context
        data = log.read_text(encoding="utf-8").replace('"roll":', '"roll":9', 1)
        assert data != log.read_text(encoding="utf-8")
        log.write_text(data, encoding="utf-8")
        rc = run(engine, "run", "verify.im", cwd=root)
        out = rc.stdout.decode(errors="replace")
        assert "TAMPER: IntegrityError" in out, out
        assert "ok=false" in out, out
    print("replay closure: ok")


if __name__ == "__main__":
    main()
