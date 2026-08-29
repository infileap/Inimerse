#!/usr/bin/env python3
"""Small reproducible V0.4 runtime benchmark harness."""
import argparse, json, statistics, subprocess, time
from pathlib import Path

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('script'); ap.add_argument('-e', '--engine', default='inimerse'); ap.add_argument('-n', '--iterations', type=int, default=5); ap.add_argument('--json', action='store_true'); args = ap.parse_args()
    times = []
    for _ in range(max(1, args.iterations)):
        t = time.perf_counter(); p = subprocess.run([args.engine, args.script], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE); dt = time.perf_counter() - t
        if p.returncode: raise SystemExit(f'benchmark run failed ({p.returncode}): {p.stderr.decode(errors="replace")}')
        times.append(dt)
    result = {'engine': args.engine, 'script': str(Path(args.script)), 'iterations': len(times), 'mean_ms': statistics.mean(times) * 1000, 'min_ms': min(times) * 1000, 'max_ms': max(times) * 1000}
    print(json.dumps(result, ensure_ascii=False) if args.json else f"{result['script']}: mean {result['mean_ms']:.3f} ms (min {min(times)*1000:.3f}, max {result['max_ms']:.3f}, n={len(times)})")
if __name__ == '__main__': main()
