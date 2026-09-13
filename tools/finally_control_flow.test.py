#!/usr/bin/env python3
"""Verify that cleanup-only control flow gets a compiler diagnostic."""

import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: finally_control_flow.test.py <engine> <script> <message>", file=sys.stderr)
        return 2
    engine, script, message = sys.argv[1:]
    proc = subprocess.run(
        [engine, script],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    output = proc.stdout + proc.stderr
    if proc.returncode == 0:
        print("expected compiler failure, got exit code 0", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1
    if message not in output:
        print("expected diagnostic not found: " + message, file=sys.stderr)
        print(output, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
