#!/usr/bin/env python3
"""A blocking network command must not run on the thread that drives the webview.

Tauri runs a *synchronous* `#[tauri::command]` on the main thread.  Any command
that shells out to curl with `--max-time 30` therefore freezes the window for the
full timeout whenever the remote host is unreachable.  Measured on this machine,
where github.com does not answer: 30.0 seconds of blocked main thread per call —
and the device flow calls it once per poll, so the window never recovers.

No DOM assertion can observe that: jsdom has no main thread to block, and the
panel suite stubs `invoke` entirely.  So the property is asserted at the source
level instead, which is the only place it is visible.

Add a command to NETWORK_COMMANDS when it performs blocking network I/O.  Making
it `async` is the fix; `tauri::async_runtime::spawn_blocking` keeps the blocking
child off the runtime's worker threads too.
"""
import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(REPO_ROOT, "Infiverse_standard", "src-tauri", "src", "lib.rs")

# Commands that shell out to a blocking curl with a 30s timeout.
NETWORK_COMMANDS = ["oauth_bind", "oauth_device_start", "oauth_device_poll"]


def main() -> int:
    with io.open(SOURCE, encoding="utf-8") as handle:
        src = handle.read()

    problems = []
    for name in NETWORK_COMMANDS:
        match = re.search(r"\n(?:pub )?(async )?fn " + re.escape(name) + r"\(", src)
        if not match:
            problems.append(f"{name}: not found in {os.path.relpath(SOURCE, REPO_ROOT)}")
            continue
        if not match.group(1):
            problems.append(
                f"{name}: declared sync but shells out to a blocking curl — "
                "a sync #[tauri::command] runs on the main thread and freezes the window"
            )

    if problems:
        for problem in problems:
            print(f"check_async_commands: {problem}", file=sys.stderr)
        return 1

    print(
        f"check_async_commands: {len(NETWORK_COMMANDS)} network command(s) are async "
        "(off the webview thread)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
