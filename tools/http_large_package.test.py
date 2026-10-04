"""Oversized HTTP bodies are refused, never truncated; ports may be kernel-assigned.

Part 1 -- truncation (M4 §2.9 follow-up).  hub_body() in
src/platform/http_posix.c serves whole files out of fixed stack buffers.  It
used to fread() into the buffer and never ask whether the file was longer, so
every endpoint that answers with a file body answered a truncated one.
Measured on the unpatched code with a 66560-byte file in the hub directory:

  GET  /content/<hash>   a perfectly good file -> 500 content_corrupt
  GET  /v/<id>           a good package        -> 200 + 65536 bytes
  GET  /package/<id>     a good package        -> 200 + 65536 bytes
  POST /package/fork     wrote the 65536-byte prefix as a new package -> 201

A truncated body is not a short answer, it is a corrupt one: the content
endpoint misdiagnosed the file, and fork installed the corruption as a real
package.  The fix sizes the file first and answers 413 <kind>_too_large with
the real size, so "refused, too big" is distinguishable from "here is the
package".  A file that fits exactly must still be served whole (the boundary
is `size > cap`, not `size >= cap`).

Part 2 -- kernel-assigned ports.  A suite that has to pick its own port must
release it before the child can bind it, and another suite's pool can take the
number in that window (the residual in docs/STATUS.md §2.9: ~1 full `ctest
-j12` run in 80 died with `hub <port> did not start`).  `--port 0` and
`--http-port 0` ask the kernel instead, and the engine prints the port it
really bound on the existing `headless:` / `http api:` stderr lines; this
suite reads those lines rather than guessing, checks the printed port answers,
and checks TCP and UDP ended up on the *same* number (one hub, two transports).

Explicit ports keep working and must still be reported verbatim.
"""
import hashlib
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from http.client import HTTPConnection
from pathlib import Path

HERE = Path(__file__).resolve().parent


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


sys.path.insert(0, str(HERE))
from testports import distinct_ports, wait_http_ping, wait_port  # noqa: E402

CAP = 65536                      # the body buffer in hub_body's caller
SMALL = bytes((i * 7 + 11) % 251 for i in range(4000))
TINY = bytes((i * 11 + 2) % 251 for i in range(900))   # see the UDP note below
EXACT = bytes((i * 13 + 5) % 251 for i in range(CAP))          # fits, exactly
BIG = bytes((i * 17 + 3) % 251 for i in range(CAP + 1024))     # does not fit

failures = []
procs = []


def check(ok, label, detail=""):
    mark = "ok  " if ok else "FAIL"
    print(f"{mark} {label}" + (f" -- {detail}" if detail and not ok else ""), flush=True)
    if not ok:
        failures.append(label)


def http(port, method, path, body=None, timeout=10.0):
    c = HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        headers = {"Content-Type": "application/json"} if body is not None else {}
        c.request(method, path, body=body, headers=headers)
        r = c.getresponse()
        return r.status, r.read()
    finally:
        c.close()


def udp_get(port, vid, timeout=1.0):
    """Datagram form of GET /v/<id>; None when the hub sent nothing.

    The datagram payload here is deliberately small: this sandboxed loopback
    drops any UDP datagram above roughly 1400 bytes (a plain python
    echo server shows the same cutoff), so a large package is not a usable
    probe for "did the hub reply".  What is being pinned is the *decision* to
    reply at all, which is size-independent.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(f"GET /v/{vid}".encode(), ("127.0.0.1", port))
        data, _ = s.recvfrom(70000)
        return data
    except socket.timeout:
        return None
    finally:
        s.close()


def start_hub(engine, cwd, hub_dir):
    """Start a hub with explicit ports; return (proc, http_port, tcp_port, lines)."""
    http_port, tcp_port = distinct_ports(2)
    script = cwd / f"hub{http_port}.im"
    script.write_text('say "hub"\nwait 120\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
    proc = subprocess.Popen(
        [str(engine), "--headless", "--port", str(tcp_port), "--http-port", str(http_port), str(script)],
        cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
    procs.append(proc)
    return proc, http_port, tcp_port, read_ports(proc)


def start_hub_kernel_ports(engine, cwd, hub_dir):
    """Start a hub with `--port 0 --http-port 0`; return the ports it reports."""
    script = cwd / f"k{len(procs)}.im"
    script.write_text('say "hub"\nwait 120\n', encoding="utf-8")
    env = dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir))
    proc = subprocess.Popen(
        [str(engine), "--headless", "--port", "0", "--http-port", "0", str(script)],
        cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
    procs.append(proc)
    tcp, http_port, lines = read_ports(proc)
    return proc, tcp, http_port, lines


def read_ports(proc, timeout=15.0):
    """Read the hub's startup lines; return (tcp, http, lines).

    Reads the pipe non-blocking so that a hub which never prints the lines
    (the pre-fix behaviour: `headless: bind 0 failed` and no `http api:` line
    at all) is a failed check rather than a hung suite.
    """
    fd = proc.stderr.fileno()
    os.set_blocking(fd, False)
    deadline = time.time() + timeout
    buf, lines, tcp, http_port, saw_fail = "", [], None, None, False
    while time.time() < deadline and not saw_fail and (tcp is None or http_port is None):
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.02)
            continue
        if not chunk:
            break
        buf += chunk.decode("utf-8", "replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            line = line.strip()
            if not line:
                continue
            lines.append(line)
            m = re.match(r"^headless: 127\.0\.0\.1:(\d+)$", line)
            if m:
                tcp = int(m.group(1))
            m = re.match(r"^http api: 127\.0\.0\.1:(\d+)$", line)
            if m:
                http_port = int(m.group(1))
            if "failed" in line:
                saw_fail = True
    return tcp, http_port, lines


def main():
    engine = find_engine()
    tmp = Path(tempfile.mkdtemp(prefix="http_large_package."))
    cwd = tmp / "run"
    universe = tmp / "universe"
    cwd.mkdir()
    universe.mkdir()
    (universe / "small.vverse").write_bytes(SMALL)
    (universe / "tiny.vverse").write_bytes(TINY)
    (universe / "exact.vverse").write_bytes(EXACT)
    (universe / "big.vverse").write_bytes(BIG)
    small_hash = hashlib.sha256(SMALL).hexdigest()
    exact_hash = hashlib.sha256(EXACT).hexdigest()
    big_hash = hashlib.sha256(BIG).hexdigest()
    (universe / f"content-{small_hash}").write_bytes(SMALL)
    (universe / f"content-{exact_hash}").write_bytes(EXACT)
    (universe / f"content-{big_hash}").write_bytes(BIG)   # a *good* file, sized 66560

    print("== part 1: oversized bodies are refused, not truncated ==", flush=True)
    proc, port, tcp_port, lines = start_hub(engine, cwd, universe)
    check(wait_http_ping(port), "explicit-port hub answers /ping", str(lines))
    check(proc.poll() is None, "explicit-port hub stays alive", str(lines))

    st, body = http(port, "GET", "/v/small")
    check(st == 200 and body == SMALL, "GET /v/<small> -> 200 with the whole package",
          f"status={st} len={len(body)} want={len(SMALL)}")
    st, body = http(port, "GET", "/v/exact")
    check(st == 200 and body == EXACT, "GET /v/<id> at the buffer size exactly -> 200 whole",
          f"status={st} len={len(body)} want={CAP}")
    st, body = http(port, "GET", "/v/big")
    check(st == 413 and b"package_too_large" in body and str(len(BIG)).encode() in body,
          "GET /v/<big> -> 413 package_too_large with the real size",
          f"status={st} body={body[:120]!r}")
    check(len(body) < CAP, "GET /v/<big> does not answer a truncated 200", f"len={len(body)}")

    st, body = http(port, "GET", "/package/big")
    check(st == 413 and b"package_too_large" in body and str(len(BIG)).encode() in body,
          "GET /package/<big> -> 413 package_too_large with the real size",
          f"status={st} body={body[:120]!r}")

    st, body = http(port, "GET", f"/content/{big_hash}")
    check(st == 413 and b"content_too_large" in body and str(len(BIG)).encode() in body,
          "GET /content/<hash of big good file> -> 413, not a misdiagnosis",
          f"status={st} body={body[:120]!r}")
    check(st != 500, "a file that is merely too large is not called corrupt", f"status={st}")

    st, body = http(port, "GET", f"/content/{small_hash}")
    check(st == 200 and body == SMALL, "GET /content/<small hash> -> 200 with the whole body",
          f"status={st} len={len(body)}")
    st, body = http(port, "GET", f"/content/{exact_hash}")
    check(st == 200 and body == EXACT, "GET /content/<hash> at the buffer size exactly -> 200 whole",
          f"status={st} len={len(body)} want={CAP}")

    fork_path = universe / "big2.vverse"
    st, body = http(port, "POST", "/package/fork", b'{"source":"big","id":"big2"}')
    check(st == 413 and b"package_too_large" in body, "POST /package/fork of a big source -> 413",
          f"status={st} body={body[:120]!r}")
    check(not fork_path.exists(), "the refused fork wrote no truncated package to disk")

    st, body = http(port, "POST", "/package/fork", b'{"source":"small","id":"small2"}')
    check(st == 201, "POST /package/fork of a small source still -> 201", f"status={st} body={body[:120]!r}")
    check((universe / "small2.vverse").read_bytes() == SMALL,
          "the accepted fork is byte-for-byte the source")

    check(udp_get(port, "tiny") == TINY, "UDP GET /v/<tiny> still returns the package body",
          repr(udp_get(port, "tiny"))[:80])
    check(udp_get(port, "big") is None,
          "UDP GET /v/<big> sends nothing (an error document must not be installed as a package)")

    print("== part 2: --port 0 / --http-port 0 are kernel-assigned and reported ==", flush=True)
    proc2, ktcp, khttp, lines2 = start_hub_kernel_ports(engine, cwd, universe)
    check(khttp is not None, "hub with --http-port 0 reports its bound HTTP port", str(lines2))
    check(ktcp is not None, "hub with --port 0 reports its bound TCP port", str(lines2))
    if khttp is not None and ktcp is not None:
        check(khttp > 0 and ktcp > 0, "reported ports are real (non-zero)", f"tcp={ktcp} http={khttp}")
        check(khttp != ktcp, "the two listeners got different ports", f"tcp={ktcp} http={khttp}")
        check(wait_http_ping(khttp, timeout=10.0), "the reported HTTP port answers /ping")
        check(wait_port(ktcp, timeout=10.0), "the reported TCP port accepts")
        check(udp_get(khttp, "tiny") == TINY,
              "UDP answers on the same reported port as TCP (one hub, shared number)",
              repr(udp_get(khttp, "tiny"))[:80])

        print("== part 3: three kernel-assigned hubs at once, all distinct and ready ==", flush=True)
        seen = [(ktcp, khttp)]
        for _ in range(2):
            p, t, h, ls = start_hub_kernel_ports(engine, cwd, universe)
            check(h is not None and t is not None, "concurrent kernel-port hub reports both ports", str(ls))
            if h is None or t is None:
                continue
            seen.append((t, h))
            check(wait_http_ping(h, timeout=10.0), f"concurrent hub http {h} answers /ping")
            check(wait_port(t, timeout=10.0), f"concurrent hub tcp {t} accepts")
        check(len({p for pair in seen for p in pair}) == 2 * len(seen),
              "every concurrently assigned port is distinct", str(seen))

    for p in procs:
        p.terminate()
    for p in procs:
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()

    if failures:
        print(f"\n{len(failures)} check(s) failed:", flush=True)
        for f in failures:
            print(f"  - {f}", flush=True)
        return 1
    print("\nall checks passed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
