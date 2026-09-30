"""Convert an Inimerse `.prof` folded-stacks file into flamegraph formats.

`inimerse profile <script.im> <out.prof>` writes a companion `<out.prof.folded>`
with two collapsed-stack views (Brendan Gregg format):
    <fn>;<fn>;... <call count>
    <fn>;...;..._us <microseconds>

This tool converts that into:
  - stdout (default): the collapsed input for flamegraph.pl
        flamegraph.pl < out.folded > flame.svg
  - --speedscope PATH: a speedscope-compatible JSON (evented format) that
        https://speedscope.app can open directly.
  - --ignore-us: drop the `_us` time-weighted view and emit only call counts.
"""
import argparse
import json
import sys
from pathlib import Path


def parse_folded(path):
    counts = []   # (stack:list[str], weight:int, time_weighted:bool)
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        stack, _, weight = line.rpartition(" ")
        if not stack or not weight.isdigit():
            continue
        time_weighted = stack.endswith("_us")
        frames = stack[:-3].split(";") if time_weighted else stack.split(";")
        counts.append((frames, int(weight), time_weighted))
    return counts


def write_speedscope(path, counts, unit):
    # speedscope "evented" format: open/close frame events over shared frames.
    frame_map = {}
    frames = []

    def frame_id(name):
        if name not in frame_map:
            frame_map[name] = len(frames)
            frames.append({"name": name})
        return frame_map[name]

    events = []
    for frames_stack, weight, time_weighted in counts:
        t = 0.0
        for f in frames_stack:
            events.append({"type": "O", "at": t, "frame": frame_id(f)})
            t += weight
        for f in reversed(frames_stack):
            t -= weight
            events.append({"type": "C", "at": t, "frame": frame_id(f)})
    doc = {
        "$schema": "https://www.speedscope.app/file-format-schema.json",
        "shared": {"frames": frames},
        "profiles": [{
            "type": "evented",
            "name": f"inimerse profile ({unit})",
            "unit": unit,
            "startValue": 0,
            "endValue": max((e["at"] for e in events), default=0) or 1,
            "events": events,
        }],
    }
    Path(path).write_text(json.dumps(doc), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folded", help="path to the .prof.folded file")
    ap.add_argument("--speedscope", help="also write a speedscope JSON here")
    ap.add_argument("--ignore-us", action="store_true",
                    help="drop the time-weighted (_us) view")
    args = ap.parse_args()
    counts = parse_folded(args.folded)
    if not counts:
        sys.exit(f"error: no folded stacks found in {args.folded}")
    out = []
    for frames, weight, time_weighted in counts:
        if args.ignore_us and time_weighted:
            continue
        out.append(f"{';'.join(frames)} {weight}")
    print("\n".join(out))
    if args.speedscope:
        write_speedscope(args.speedscope,
                         [c for c in counts if not c[2]],
                         unit="calls")
        write_speedscope(args.speedscope.replace(".json", "_us.json"),
                         [c for c in counts if c[2]],
                         unit="microseconds")
        print(f"speedscope: {args.speedscope}", file=sys.stderr)


if __name__ == "__main__":
    main()
