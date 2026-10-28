#!/usr/bin/env python3
"""Print how much of a stage-guarded test's timeout is contention, not design.

The margin is computed here, never written down.  The limit is read from the
TIMEOUT property this build tree declares, and the worst observed wall time
comes from tools/gate_attrs.recorded.json, which records when, on which tree
and under what external load that time was measured.  Written into the script
instead, the margin would survive somebody raising TIMEOUT and keep saying
"20%" about a budget that no longer exists -- the same half-a-reading that
`skipped 0` was: shaped like the two measurements beside it, actually a literal.

Nothing here carries an exit code.  A margin is a reading; when it cannot be
taken the line says why and the run continues, because an absent reading that
raises would be a criterion in disguise.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORDED = ROOT / "tools" / "gate_attrs.recorded.json"


def read_props(test_dir: Path, test: str) -> tuple[dict | None, str]:
    """The properties this build tree declares for one test, or a reason."""
    said = subprocess.run(
        ["ctest", "--test-dir", str(test_dir), "--show-only=json-v1"],
        capture_output=True, text=True)
    if said.returncode != 0:
        return None, f"ctest --show-only failed: {(said.stderr or said.stdout).strip()[:160]}"
    try:
        doc = json.loads(said.stdout)
    except json.JSONDecodeError as exc:
        return None, f"ctest --show-only did not return json: {exc}"
    for entry in doc.get("tests", []) or []:
        if entry.get("name") != test:
            continue
        props = {}
        for item in entry.get("properties", []) or []:
            props[item.get("name")] = item.get("value")
        return props, ""
    return None, f"no test named {test} in {test_dir}"


def margin_percent(limit_s: float, worst_s: float) -> int:
    """How much of the limit is left, before any of it is spent."""
    return int(round((limit_s - worst_s) / limit_s * 100))


def render(row: dict, props: dict | None, why: str) -> str:
    test = row["test"]
    head = f"GATE-ATTR|test={test}"
    if props is None:
        return f"{head}|margin=na|why={why}"
    serial = "yes" if props.get("RUN_SERIAL") else "absent"
    raw_limit = props.get("TIMEOUT")
    if raw_limit in (None, ""):
        return f"{head}|serial={serial}|margin=na|why=the test declares no TIMEOUT"
    limit_s = float(raw_limit)
    worst_s = float(row["worst_s"])
    if serial != "yes":
        return (f"{head}|limit={limit_s:g}s|worst={worst_s:g}s|serial=absent"
                f"|margin=na|why=RUN_SERIAL is not set, so this margin is not bought")
    return (f"{head}|limit={limit_s:g}s|worst={worst_s:g}s|load={row['load']}"
            f"|serial={serial}|margin={margin_percent(limit_s, worst_s)}%"
            f"|basis=recorded under contention, not designed slack"
            f"|measured-on={row['measured_on']}")


def load_recorded() -> tuple[list[dict] | None, str]:
    try:
        doc = json.loads(RECORDED.read_text())
    except FileNotFoundError:
        return None, f"no recorded measurements at {RECORDED}"
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return None, f"cannot read {RECORDED}: {exc}"
    rows = doc.get("rows")
    if not isinstance(rows, list) or not rows:
        return None, f"{RECORDED} has no rows"
    return rows, ""


def selftest() -> int:
    """Two claims that a literal cannot make: the margin follows TIMEOUT."""
    rows = [{"test": "t", "worst_s": 24.0, "load": "20-hog", "measured_on": "abc1234"}]
    at_30 = render(rows[0], {"TIMEOUT": "30", "RUN_SERIAL": "TRUE"}, "")
    at_60 = render(rows[0], {"TIMEOUT": "60", "RUN_SERIAL": "TRUE"}, "")
    no_serial = render(rows[0], {"TIMEOUT": "30"}, "")
    no_timeout = render(rows[0], {"RUN_SERIAL": "TRUE"}, "")
    over = render({"test": "t", "worst_s": 40.0, "load": "20-hog", "measured_on": "abc1234"},
                  {"TIMEOUT": "30", "RUN_SERIAL": "TRUE"}, "")
    def field(text, name):
        for part in text.split("|"):
            if part.startswith(name + "="):
                return part.split("=", 1)[1]
        return "<absent>"
    checks = [
        ("margin follows the declared limit",
         field(at_30, "margin") == "20%" and field(at_60, "margin") == "60%",
         f"30s -> {field(at_30, 'margin')}, 60s -> {field(at_60, 'margin')}"),
        ("a test without RUN_SERIAL gets no margin",
         field(no_serial, "margin") == "na" and "not bought" in no_serial,
         field(no_serial, "why")),
        ("a test that declares no TIMEOUT says so",
         field(no_timeout, "margin") == "na" and "no TIMEOUT" in no_timeout,
         field(no_timeout, "why")),
        ("a worst time above the limit reads negative, not plenty",
         field(over, "margin").startswith("-"),
         f"{field(over, 'worst')} against {field(over, 'limit')} -> {field(over, 'margin')}"),
    ]
    red = 0
    for name, ok, detail in checks:
        print(f"{'ok:' if ok else 'RED:'} {name} -- {detail}")
        red += 0 if ok else 1
    print(f"gate-attr-selftest: cases={len(checks)} red={red} green={len(checks) - red} -- none" if not red
          else f"gate-attr-selftest: cases={len(checks)} red={red}")
    return 0 if red == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--test-dir", default=str(ROOT / "build"))
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    rows, why = load_recorded()
    if rows is None:
        print(f"GATE-ATTR|refused|why={why}")
        return 0
    for row in rows:
        props, prop_why = read_props(Path(args.test_dir), row["test"])
        print(render(row, props, prop_why))
    return 0


if __name__ == "__main__":
    sys.exit(main())