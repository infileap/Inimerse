#!/usr/bin/env bash
set -u

probe="${1:?http probe executable is required}"
last_rc=1
for attempt in 1 2 3 4 5; do
    if "$probe"; then
        exit 0
    else
        last_rc=$?
        printf 'http_probe attempt %s failed (rc=%s)\n' "$attempt" "$last_rc" >&2
        sleep 1
    fi
done
exit "$last_rc"
