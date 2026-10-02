#!/usr/bin/env python3
"""ports_race_probe.py — measure the hub-port race: guessed ports vs kernel-assigned.

This is a *probe*, not a regression test: it starts many hubs, counts how many
never become ready, and prints the failure signatures.  It exists so the
before/after claim for deliverable B in `docs/STATUS.md` 2.9 is reproducible
with a round count and an exact command instead of an anecdote.

Two mechanisms, one measurement:

  --mode old   what the suites did: ask this process's own `PortPool` for
               `hubs * 2` numbers (HTTP + TCP per hub), then hand each number
               to a child.  The pool must release a number before the child can
               bind it, and a *sibling worker's* pool can take that number in
               between -- the child then binds EADDRINUSE, stays alive, serves
               no HTTP at all, and every later request hangs until the suite's
               readiness budget expires.  Signature in the hub's stderr: a
               `headless: 127.0.0.1:N` line and no `http api:` line.

  --mode new   what the converted suites do: ask each hub for
               `--port 0 --http-port 0` and read back the numbers the kernel
               assigned.  No number is reserved, released or guessed, so there
               is nothing for a sibling to steal.

Run several `--workers` at once: the race needs a concurrent pool to win, which
is what `ctest -jN` provides.  A worker's round is exactly one suite's shape: it
allocates, starts `--hubs` hubs at once, waits for each to answer `/ping` with
the same 10 s budget the suites use, then tears everything down.

    # before (pristine main checkout, guessed ports)
    python3 tools/ports_race_probe.py --engine /path/to/main/build/inimerse \
        --mode old --workers 4 --rounds 200 --hubs 6

    # after (stream worktree, kernel-assigned ports)
    python3 tools/ports_race_probe.py --engine build/inimerse \
        --mode new --workers 4 --rounds 200 --hubs 6

Exit status is always 0 -- the numbers are the output, not a verdict.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from testports import (HubStartError, distinct_ports,  # noqa: E402
                       start_hub_bound_ports, wait_http_ping)

HUB_SCRIPT = 'say "hub"\nwait 120\n'


def _tail(path: Path, n: int = 4) -> list[str]:
    try:
        lines = [ln for ln in path.read_text(errors="replace").splitlines() if ln.strip()]
    except OSError:
        return []
    return lines[-n:]


def _reported_http(path: Path) -> bool:
    try:
        return "http api:" in path.read_text(errors="replace")
    except OSError:
        return False


def _start_explicit(engine: Path, cwd: Path, http_port: int, tcp_port: int,
                    log: Path) -> subprocess.Popen:
    """The old way: the caller picks the numbers and the child must bind them."""
    env = dict(os.environ, INIMERSE_HUB_DIR=str(cwd / "universe"))
    with open(log, "wb") as fh:
        return subprocess.Popen(
            [str(engine), "--headless", "--port", str(tcp_port),
             "--http-port", str(http_port), str(cwd / "hub.im")],
            cwd=str(cwd), env=env, stdout=subprocess.DEVNULL, stderr=fh)

def _stop(procs: list[subprocess.Popen]) -> None:
    for p in procs:
        if p.poll() is not None:
            continue
        p.terminate()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


def worker(wid: int, args, engine: Path, failures: list, lock: threading.Lock,
           counters: dict) -> None:
    for round_no in range(args.rounds):
        root = Path(tempfile.mkdtemp(prefix=f"probe-w{wid}-"))
        procs: list[subprocess.Popen] = []
        try:
            hubs: list[tuple[int, Path]] = []      # (http port to wait for, log)
            if args.mode == "new":
                for i in range(args.hubs):
                    d = root / f"h{i}"
                    d.mkdir()
                    (d / "hub.im").write_text(HUB_SCRIPT, encoding="utf-8")
                    log = root / f"h{i}.log"
                    try:
                        proc, _tcp, http_port = start_hub_bound_ports(
                            [engine, "--headless", "--port", "0",
                             "--http-port", "0", d / "hub.im"],
                            cwd=d,
                            env=dict(os.environ, INIMERSE_HUB_DIR=str(d / "universe")),
                            log_path=log, timeout=args.timeout)
                    except HubStartError as exc:
                        with lock:
                            counters["start_errors"] += 1
                            failures.append({
                                "worker": wid, "round": round_no, "stage": "start",
                                "port": None, "error": str(exc),
                                "reported_http": _reported_http(log),
                                "tail": _tail(log),
                            })
                        continue
                    procs.append(proc)
                    hubs.append((http_port, log))
            else:
                numbers = distinct_ports(args.hubs * 2)
                http_ports, tcp_ports = numbers[:args.hubs], numbers[args.hubs:]
                for i in range(args.hubs):
                    d = root / f"h{i}"
                    d.mkdir()
                    (d / "hub.im").write_text(HUB_SCRIPT, encoding="utf-8")
                    log = root / f"h{i}.log"
                    procs.append(_start_explicit(engine, d, http_ports[i], tcp_ports[i], log))
                    hubs.append((http_ports[i], log))

            for port, log in hubs:
                with lock:
                    counters["hubs"] += 1
                if wait_http_ping(port, timeout=args.timeout):
                    continue
                with lock:
                    counters["never_ready"] += 1
                    failure = {
                        "worker": wid, "round": round_no, "stage": "ready",
                        "port": port, "reported_http": _reported_http(log),
                        "tail": _tail(log),
                    }
                    if not failure["reported_http"]:
                        counters["no_http_line"] += 1
                    failures.append(failure)
        finally:
            _stop(procs)
            shutil.rmtree(root, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--engine", required=True, type=Path,
                    help="path to the inimerse binary to probe")
    ap.add_argument("--mode", choices=("old", "new"), default="new")
    ap.add_argument("--workers", type=int, default=4,
                    help="concurrent suite-shaped loops (the source of contention)")
    ap.add_argument("--rounds", type=int, default=200, help="rounds per worker")
    ap.add_argument("--hubs", type=int, default=6, help="hubs per round")
    ap.add_argument("--timeout", type=float, default=10.0,
                    help="readiness budget per hub, as the suites use")
    args = ap.parse_args()

    engine = args.engine.resolve()
    if not (engine.is_file() and os.access(engine, os.X_OK)):
        raise SystemExit(f"not an executable engine: {engine}")

    counters = {"hubs": 0, "never_ready": 0, "no_http_line": 0, "start_errors": 0}
    failures: list[dict] = []
    lock = threading.Lock()

    started = time.monotonic()
    threads = [threading.Thread(target=worker, args=(w, args, engine, failures, lock, counters),
                                daemon=True)
               for w in range(args.workers)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.monotonic() - started

    print(f"engine     : {engine}")
    print(f"mode       : {args.mode}")
    print(f"shape      : {args.workers} workers x {args.rounds} rounds x {args.hubs} hubs"
          f" = {args.workers * args.rounds * args.hubs} hub starts possible")
    print(f"hubs waited: {counters['hubs']}")
    print(f"never ready: {counters['never_ready']} of {counters['hubs']}"
          f"  ({100.0 * counters['never_ready'] / max(1, counters['hubs']):.2f}%)")
    print(f"  of those, no `http api:` line in the hub's stderr:"
          f" {counters['no_http_line']}")
    print(f"start errors (hub never printed its ports): {counters['start_errors']}")
    print(f"elapsed    : {elapsed:.1f}s")
    for f in failures[:5]:
        print(f"  sample w{f['worker']} round {f['round']} port={f['port']}:"
              f" reported_http={f['reported_http']} tail={f['tail']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
