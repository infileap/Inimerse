#!/usr/bin/env python3
"""check_frozen_identity.test.py — exercise all three exit codes of the checker.

Why a test file (tools/ convention is `<name>.py` + `<name>.test.py`): the whole
point of check_frozen_identity.py is that it can say NO.  A checker that is only
ever observed saying YES is indistinguishable from one that was never run, so
the negative controls are the part that has to be one command away.

Three cells, each asserting a different exit code:

  0  positive   a real product whose frozen[<self>] does hold the first dump
  1  negative   a forged product whose frozen[<self>] holds the *last* dump
                (the forgery names itself <self-key> inside, so --self-key is
                 what makes this cell reachable — without it the checker
                 correctly reports NO_FROZEN_SELF and exits 2, a different cell)
  2  negative   a path that does not exist

Nothing here re-runs the generator and nothing writes into the checkout except a
temp directory that is removed on exit.

Usage:
    python3 tools/check_frozen_identity.test.py <product.json> [--self-key NAME] [-v]
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'check_frozen_identity.py')


def run(args):
    r = subprocess.run([sys.executable, CHECKER] + args,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('product', help='a real generated product json')
    ap.add_argument('--self-key', default=None,
                    help='key under "frozen" naming the product itself')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args(argv)

    key = a.self_key or os.path.basename(a.product)
    tmp = tempfile.mkdtemp(prefix='frozen-identity-test-')
    cells = []

    def cell(name, want, args):
        got, out = run(args)
        ok = got == want
        cells.append((name, want, got, ok))
        print('%-10s want=%-2d got=%-2d %s' % (name, want, got, 'OK' if ok else 'BAD'))
        if a.verbose or not ok:
            for line in out.rstrip().splitlines():
                print('    ' + line)

    try:
        # cell 0 / positive: the real product on disk
        cell('positive', 0, [a.product, '--self-key', key])

        # cell 1 / forged: frozen[<key>] rewritten to hold the *last* dump's own
        # bytes, so the checker must refuse the identity and exit 1.
        d = json.load(open(a.product, encoding='utf-8'))
        raw = json.dumps(d, ensure_ascii=False, indent=1).encode()
        if key in (d.get('frozen') or {}):
            d['frozen'][key] = {
                'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
                'mtime': d['frozen'][key].get('mtime'),
            }
        forged = os.path.join(tmp, 'forged.json')
        open(forged, 'w', encoding='utf-8').write(
            json.dumps(d, ensure_ascii=False, indent=1))
        cell('forged', 1, [forged, '--self-key', key])

        # cell 2 / missing product
        cell('missing', 2, [os.path.join(tmp, 'no-such-file.json')])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    passed = sum(1 for _, _, _, ok in cells if ok)
    print()
    print('CELLS pass=%d/%d' % (passed, len(cells)))
    print('VERDICT %s' % ('all_cells_expected' if passed == len(cells) else 'some_cells_failed'))
    return 0 if passed == len(cells) else 1


if __name__ == '__main__':
    sys.exit(main())
