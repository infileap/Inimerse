#!/usr/bin/env python3
"""Fail if a test suite binds its own HTTP server to a reserved port number.

`tools/testports.py` exists because reserving a port and binding it later
leaves a release-to-bind window: between the moment the pool lets the number go
and the moment the server claims it, any sibling process on the machine can
take it.  Under `ctest -j4` that sibling is another suite, and the loser dies
with `OSError: [Errno 98] Address already in use` before it runs an assertion.

That is exactly what happened to `tools/node_discovery.test.py`, whose fake
directory did `distinct_ports(1)[0]` and bound it much later.  Measured on this
machine: 4 failures in 120 runs at 32-way load (3.3%), and 0 in 120 runs once
the server bound port 0 and read the number back off the listening socket
(docs/STATUS.md 10.38).

Binding port 0 has no window at all -- the kernel assigns the number and the
socket holds it from that instant -- so the rule is: a server a suite binds for
itself asks for 0 and then reads `server_address`.

This check is deliberately narrow.  Ports handed to a *child process* (a hub
started with `--port 0`, or a TCP port a suite passes down) are a different
question with its own history in docs/STATUS.md 2.9; flagging those here would
mean rewriting suites that are not part of this defect.
"""

import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"

# A server the suite binds in-process.  `http.server.ThreadingHTTPServer` and a
# bare `HTTPServer` (imported from `http.server`) are the two spellings in use.
SERVER = re.compile(r"\b(ThreadingHTTPServer|HTTPServer)\s*\(\s*\(([^)]*)\)")

REASON = (
    "binds a reserved port number instead of asking the kernel for one. "
    "Reserving a number and binding it later leaves a release-to-bind window "
    "that a sibling suite can win under `ctest -j4`, which kills this suite "
    "with 'Address already in use' before any assertion runs (measured: 4/120 "
    "runs at 32-way load). Bind port 0 and read the number back off the "
    "listening socket instead, the way tools/inim.test.py does."
)


def port_argument(address_tuple: str) -> str:
    """The second element of an (host, port) tuple, as written."""
    parts = [p.strip() for p in address_tuple.split(",")]
    return parts[1] if len(parts) > 1 else ""


def main():
    failures = []

    for path in sorted(TOOLS.glob("*.test.py")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in SERVER.finditer(text):
            port = port_argument(m.group(2))
            if port != "0":
                line = text[: m.start()].count("\n") + 1
                failures.append(
                    f"{os.path.relpath(path, REPO_ROOT)}:{line}: "
                    f"{m.group(0)} -> port {port!r} {REASON}"
                )

    if failures:
        print("check_test_ports: FAILED", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print("check_test_ports: ok (every in-process test server binds port 0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
