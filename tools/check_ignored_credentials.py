#!/usr/bin/env python3
"""Fail if anything written into a `userdata/` directory could be committed.

The rules in `.gitignore` used to ignore these directories and then re-include
them with `!userdata/` so that `.gitkeep` survived.  Re-including a directory
re-includes everything inside it, so the *default* for a runtime artifact was
"committable" and `git add -A` collected it silently.  Two credentials nearly
left this machine that way:

  * `userdata/oauth_secret_github.txt` -- a client secret written by
    `oauth_set_secret`;
  * `userdata/linked_accounts.json` -- a live `gho_` access token written by
    `oauth_bind` and by the device-flow poll.

The rules were then inverted: the *contents* are ignored and `.gitkeep` is
whitelisted back.  The fix for the next artifact is therefore already in place,
but "already in place" is exactly the kind of claim that quietly stops being
true -- so this check tests the default rather than the list.

The load-bearing assertion is `probe_default_deny()`: a filename nobody has
ever written must already be ignored.  Checking only the three known artifacts
would pass against the old rules the moment someone added a fourth patch line,
which is precisely how the original defect survived two incidents.
"""

import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Every directory the app resolves through `app_root()/userdata`.
USERDATA_DIRS = [
    "userdata",
    "Infiverse_standard/userdata",
    "Infiverse_standard/src-tauri/userdata",
]

# The artifacts that have actually been written so far.  Kept for the record --
# `probe_default_deny()` is what makes the check structural.
KNOWN_ARTIFACTS = [
    "stats.json",
    "oauth_secret_github.txt",
    "oauth_secret_bilibili.txt",
    "linked_accounts.json",
]

# A name no run has ever produced.  If this is not ignored, the rules have gone
# back to "deny the directory, allow its contents".
NEVER_WRITTEN = "__runtime_state_probe_never_created__.txt"


def git(*args):
    return subprocess.run(
        ["git", "-C", REPO_ROOT, *args],
        capture_output=True,
        text=True, encoding="utf-8", errors="replace",
    )


def is_ignored(path):
    """True if `path` matches an ignore rule.

    `--no-index` asks about the rules rather than about the working tree, so
    this answers for paths that do not exist and for files already tracked.
    """
    return git("check-ignore", "--no-index", "-q", path).returncode == 0


def main():
    failures = []

    for d in USERDATA_DIRS:
        if not os.path.isdir(os.path.join(REPO_ROOT, d)):
            continue

        # 1. The structural assertion: default deny.
        probe = f"{d}/{NEVER_WRITTEN}"
        if not is_ignored(probe):
            failures.append(
                f"{probe}: a file that has never been written is COMMITTABLE. "
                "The userdata contents are not ignored by default, so the next "
                "runtime artifact -- possibly a credential -- will be collected "
                "by `git add -A` without anyone deciding that it should be."
            )

        # 2. The artifacts seen so far, which the default should already cover.
        for name in KNOWN_ARTIFACTS:
            path = f"{d}/{name}"
            if not is_ignored(path):
                failures.append(
                    f"{path}: not ignored. This file is written at runtime and "
                    "must never be committed."
                )

        # 3. The whitelist has to keep working, or the directory drops out of
        #    the tree entirely.
        keep = f"{d}/.gitkeep"
        if is_ignored(keep):
            failures.append(
                f"{keep}: ignored. The placeholder must stay visible, otherwise "
                "the directory disappears from a fresh clone."
            )

    # 4. End to end: ask git what `git add -A` would actually stage.
    added = git("add", "-A", "--dry-run")
    if added.returncode != 0:
        failures.append(f"`git add -A --dry-run` failed: {added.stderr.strip()}")
    else:
        for line in added.stdout.splitlines():
            if "userdata/" in line:
                failures.append(
                    f"`git add -A` would stage {line.strip()} -- a runtime file "
                    "is on its way into a commit."
                )

    if failures:
        print("check_ignored_credentials: FAILED", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print(
        "check_ignored_credentials: ok "
        f"({len(USERDATA_DIRS)} userdata dirs deny their contents by default, "
        f".gitkeep still tracked)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
