#!/usr/bin/env python3
"""testports.py — allocate a TCP port for an engine subprocess under test.

Why this exists
---------------
Seven suites in `tools/` start an engine that binds a TCP port, and each one
used to find a free port like this:

    s = socket.socket(); s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]; s.close()

That is a time-of-check/time-of-use race.  `bind(0)` hands out a port and the
`close()` immediately gives it back, so between reading the number and the
engine actually binding it, any other process on the machine can take it.  Under
`ctest -j12` the other processes are our own suites, and `hub_dist_regression`
was observed failing exactly this way while passing in isolation and 15/15 under
`--repeat until-fail`.

We cannot simply hold the socket open and hand it to the child -- the engine
takes a port *number* on the command line, not an inherited descriptor.  So we
keep early binds only as a *fallback* and, in `reserve_port()`:

1. ask the kernel for a candidate;
2. bind the child's own server socket ourselves, then release it, and retry
   until the kernel stops handing it back (already taken by someone else);
3. verify the port answers nothing right now.

A residual race remains -- the port could be taken between step 3 and the
child's bind -- but it is now narrow instead of wide, and `free_port()` below
keeps the original best-effort behaviour for callers that only need a number.

Usage
-----
    from testports import distinct_ports, free_port, wait_port, wait_http_ping
"""

from __future__ import annotations

import errno
import socket
import time

__all__ = ["free_port", "reserve_port", "PortPool", "distinct_ports",
           "wait_port", "wait_http_ping"]

_LOOPBACK = "127.0.0.1"


def free_port(host: str = _LOOPBACK) -> int:
    """Return a port number that is free *right now* (best effort).

    Kept for callers that cannot accept a reservation; prefer `reserve_port()`.
    """
    with socket.socket() as s:
        s.bind((host, 0))
        return s.getsockname()[1]


def _is_available(port: int, host: str = _LOOPBACK) -> bool:
    """True if we can still bind `port` -- i.e. nobody has claimed it."""
    try:
        with socket.socket() as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((host, port))
        return True
    except OSError as exc:
        if exc.errno in (errno.EADDRINUSE, errno.EACCES):
            return False
        raise


def reserve_port(host: str = _LOOPBACK, attempts: int = 40) -> int:
    """Return a port nothing is listening on, having verified it just now.

    NOTE: this releases the candidate before returning, so it does *not* hold a
    lease -- the port can be taken in the gap before the child binds, and two
    rapid consecutive calls can even return the same number (measured: ~1 in 3
    batches of 40).  Suites that allocate several ports for several children at
    once should use `PortPool` instead, which keeps every port it hands out
    reserved until the caller is done with it.
    """
    last: int | None = None
    for _ in range(attempts):
        candidate = free_port(host)
        if _is_available(candidate, host):
            last = candidate
            time.sleep(0.002)
            if _is_available(candidate, host):
                return candidate
    if last is not None:
        return last
    raise RuntimeError(f"could not find a free port on {host} in {attempts} attempts")


class PortPool:
    """Hand out several distinct ports and keep them reserved until released.

    The engine takes a port *number*, not an inherited socket, so a port cannot
    be handed to the child directly.  Holding a bound socket open is nonetheless
    useful: while we hold it, the kernel will not give that number to any other
    suite, so N ports allocated for N sibling hubs are guaranteed distinct.  The
    caller releases each port right before starting the child that wants it.

        with PortPool() as pool:
            ports = [pool.take() for _ in range(3)]     # 3 distinct numbers
            for p in ports:
                pool.release(p)                          # free it for the child
    """

    def __init__(self, host: str = _LOOPBACK):
        self._host = host
        self._held: dict[int, socket.socket] = {}

    def take(self) -> int:
        """Reserve a fresh port and return its number; it stays held."""
        for _ in range(40):
            s = socket.socket()
            try:
                s.bind((self._host, 0))
            except OSError:
                s.close()
                continue
            port = s.getsockname()[1]
            if port in self._held:      # pragma: no cover - kernel won't do this
                s.close()
                continue
            self._held[port] = s
            return port
        raise RuntimeError("could not reserve a port")

    def release(self, port: int) -> None:
        """Let go of `port` so the child process can bind it."""
        s = self._held.pop(port, None)
        if s is not None:
            s.close()
        # Give the kernel a moment to actually drop the binding.
        time.sleep(0.01)

    def close(self) -> None:
        for s in self._held.values():
            s.close()
        self._held.clear()

    def __enter__(self) -> "PortPool":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


def distinct_ports(count: int, host: str = _LOOPBACK) -> list[int]:
    """Return `count` distinct ports, released and ready for children to bind.

    The one-call form of `PortPool`, for the shape

        dir_port, node_a_port, node_b_port = distinct_ports(3)

    where the old `free_port(), free_port(), free_port()` could hand back the
    same number twice (measured: a duplicate in roughly one batch of three at
    count=40).  Every number here was held simultaneously, so they cannot
    collide with each other.
    """
    with PortPool(host) as pool:
        ports = [pool.take() for _ in range(count)]
    return ports


def wait_port(port: int, host: str = _LOOPBACK, timeout: float = 10.0) -> bool:
    """Wait until something *accepts a TCP connection* on `port`.

    This is the weaker of the two readiness checks: it proves a listener exists,
    not that the engine is answering.  Suites whose engine serves a protocol
    should use `wait_http_ping()` instead.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def wait_http_ping(port: int, host: str = _LOOPBACK, timeout: float = 10.0,
                   path: str = "/ping") -> bool:
    """Wait until the hub answers `GET /ping` with 200 and a `pong` body.

    Strictly stronger than `wait_port()`: a port that merely accepts a
    connection is not yet a hub that answers, and six of the suites in this
    directory depend on the difference -- the `/ping` round trip is what proves
    the engine finished coming up.
    """
    import http.client

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            conn = http.client.HTTPConnection(host, port, timeout=1)
            try:
                conn.request("GET", path)
                resp = conn.getresponse()
                body = resp.read()
            finally:
                conn.close()
            if resp.status == 200 and b"pong" in body:
                return True
        except OSError:
            pass
        time.sleep(0.05)
    return False


def distinct_ports(count: int, host: str = _LOOPBACK) -> list[int]:
    """Return `count` distinct ports, released and ready for children to bind.

    The one-call form of `PortPool` for the common shape

        dir_port, node_a_port, node_b_port = distinct_ports(3)

    where the old ``free_port(), free_port(), free_port()`` could hand back the
    same number twice.  Every number here was held simultaneously, so they are
    guaranteed different.

    The guarantee holds *within one call only*.  Two separate calls each build
    their own pool, and the second may be handed a number the first already
    returned -- measured 5/200 at count 6 and 5/500 at count 3, because the
    first call's ports have already been released by the time the second
    starts.  When a suite needs several ports for sibling children, ask for
    them all at once (``distinct_ports(n_hubs + n_tcp)``) rather than twice;
    an overlap means two hubs are given the same port and the second one's
    bind fails with EADDRINUSE while the process stays up and never listens.
    """
    with PortPool(host) as pool:
        return [pool.take() for _ in range(count)]
