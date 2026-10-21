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

★ Why <product.json> is optional.  The first version required a real product on
disk, which meant this test could not be registered in the suite: the only
products carrying `frozen` + `four_piece` live under `.scratch/`, which is
git-ignored, so a checked-out tree has nothing to point it at — and a test that
cannot run where the gate runs is not a control.  With no argument the three
cells are built from a SYNTHETIC product that reproduces the generator's own
two-write shape (`frozen={}` first, then `frozen={…}` + `four_piece`), so the
suite gets the three exit codes without needing a fixture.  Passing a real
product still exercises the same three cells against real bytes.

Usage:
    python3 tools/check_frozen_identity.test.py [<product.json>] [--self-key NAME] [-v]

With no argument it builds a synthetic product, so it runs in a fresh checkout
where the git-ignored .scratch/ tree is absent.  Exit codes: 0 every cell as
expected, 1 some cell differed, 3 the given product path does not exist (an
input fault, deliberately outside the 0/1/2 the cells themselves assert).
"""
import argparse
import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'check_frozen_identity.py')
DELTA = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     'check_frozen_identity_delta.py')


def run(args):
    r = subprocess.run([sys.executable, CHECKER] + args,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def run_delta(args):
    r = subprocess.run([sys.executable, DELTA] + args,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def dump(o):
    return json.dumps(o, ensure_ascii=False, indent=1).encode()


def build_synthetic_product(path, key):
    """Write a product with the generator's two-write shape, at `path`.

    The generator (marks129.py:405-418) writes twice: first with `frozen={}`
    and no `four_piece`, then again with both filled in.  So `frozen[<self>]`
    must record the FIRST dump.  Reproducing that shape here is what lets the
    three cells run in a tree that has no generated product in it.
    """
    body = {
        'item': 'synthetic product for check_frozen_identity.test.py',
        'criteria_file': {'path': '/synthetic/criteria.md'},
        'answers': {'q1': [1, 2, 3], 'q2': {'n': 0, 'rows': []}},
        'honest_boundary': 'synthetic: this fixture exists only to give the checker '
                           'a product whose frozen[<self>] is the first dump',
    }
    first = dict(body, frozen={})
    first_b = dump(first)
    entries = {
        key: {'path': '/synthetic/' + key, 'bytes': len(first_b),
              'mtime': '1970-01-01T00:00:00.000000000',
              'sha256': sha256(first_b)},
        'other.py': {'path': '/synthetic/other.py', 'bytes': 11,
                     'mtime': '1970-01-01T00:00:00.000000000',
                     'sha256': sha256(b'other.py\n\n')},
    }
    product = dict(first)
    product['frozen'] = entries
    product['four_piece'] = {'pre': {'pwd': '/synthetic'}, 'post': {'pwd': '/synthetic'},
                             'self_increment': {'n_py': 0}}
    raw = dump(product)
    with open(path, 'wb') as f:
        f.write(raw)
    return path


def load_product(path):
    """Pure classifier for a caller-supplied product.  No prints, no exits.

    Returns (obj, outcome, marker):
      ('ok',      '')                  obj is the parsed product
      ('missing', 'INPUT_MISSING')     path is not a regular file
      ('invalid', 'INPUT_BAD_JSON')    unreadable as JSON, or not an object
      ('invalid', 'INPUT_NO_FROZEN')   no usable "frozen" mapping

    A product whose "frozen" simply lacks the key you named is NOT classified
    here: that is the checker's own verdict NO_FROZEN_SELF at rc 2, and calling
    it an input fault would hide a real branch of the checker behind a
    test-side refusal -- the same shape as the `missing` cell that once passed
    because the checker was absent.  That path is asserted by the `no-self-key`
    cell instead, so the harness cannot report a refusal it never saw.

    Every one of these is an INPUT fault, and they all share the test's single
    input code (3) while the marker says which one it was -- the same shape the
    checker already uses for its verdict code 2, which covers NO_PRODUCT /
    BAD_JSON / NO_FROZEN_SELF.  One code per CLASS, markers for the members:
    codes are a shared resource and collide with somebody's expectation set,
    markers are free and can be subdivided forever.

    ★ This exists as a pure function so the three bad-input branches can be
    EXECUTED by an in-process cell instead of being reasoned about -- and so
    that no cell has to re-invoke this program recursively to reach them.
    """
    if not os.path.isfile(path):
        return None, 'missing', 'INPUT_MISSING'
    try:
        obj = json.loads(open(path, encoding='utf-8').read())
    except Exception:
        return None, 'invalid', 'INPUT_BAD_JSON'
    if not isinstance(obj, dict):
        return None, 'invalid', 'INPUT_BAD_JSON'
    if not isinstance(obj.get('frozen'), dict):
        return None, 'invalid', 'INPUT_NO_FROZEN'
    return obj, 'ok', ''


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('product', nargs='?', default=None,
                    help='a real generated product json; omitted => build a '
                         'synthetic one with the same two-write shape')
    ap.add_argument('--self-key', default=None,
                    help='key under "frozen" naming the product itself')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args(argv)

    # An unusable product from the caller is an INPUT fault, not a verdict, and
    # it is classified by the pure function above.  Three branches reach here --
    # no such file, not JSON, JSON without a usable "frozen" -- and before this
    # guard the last two died in json.load / on d['frozen'] with a traceback and
    # exit 1, which is the same code the checker uses to say "the identity does
    # not hold".  The code is 3: the cells below assert the checker's verdict
    # codes {0,1,2} and `missing` expects 2, so 2 here would move the collision
    # up a floor instead of removing it.
    if a.product is not None:
        _obj, _outcome, _marker = load_product(a.product)
        if _outcome != 'ok':
            print('%s %s' % (_marker, a.product))
            print('VERDICT input_%s' % _outcome)
            return 3

    tmp = tempfile.mkdtemp(prefix='frozen-identity-test-')
    synthetic = a.product is None
    if synthetic:
        key = a.self_key or 'synthetic_product.json'
        a.product = build_synthetic_product(os.path.join(tmp, key), key)
    else:
        key = a.self_key or os.path.basename(a.product)
    product_obj, _o, _m = load_product(a.product)
    if _o != 'ok':                      # synthetic products are built above
        print('%s %s' % (_m, a.product))
        print('VERDICT input_%s' % _o)
        return 3
    print('SOURCE      %s' % ('synthetic fixture' if synthetic else a.product))
    cells = []

    def cell(name, want, args, must_contain=None):
        got, out = run(args)
        ok = got == want
        # ★ 只比退出码会让「程序根本没跑起来」冒充「正确地拒绝了坏输入」：
        # a missing checker file and a NO_PRODUCT both exit non-zero, and the
        # `missing` cell (want=2) would then pass for the wrong reason.  Every
        # non-zero cell therefore also asserts the failure mode by name.
        if ok and must_contain is not None and must_contain not in out:
            ok = False
        cells.append((name, want, got, ok))
        print('%-10s want=%-2d got=%-2d %s' % (name, want, got, 'OK' if ok else 'BAD'))
        if a.verbose or not ok:
            if must_contain is not None:
                print('    must_contain %r -> %s' % (must_contain, must_contain in out))
            for line in out.rstrip().splitlines():
                print('    ' + line)

    try:
        # cell / input-classification: the bad-input branches are EXECUTED here,
        # in process, against the pure classifier -- not reasoned about.  Before
        # this cell, two of them (not JSON, JSON without a usable `frozen`) were
        # only reachable by re-running this program from outside, and both died
        # with a traceback and exit 1, i.e. they impersonated "the checker
        # refused it".  Naming them as cases puts them inside the same command
        # that reports pass=6/6, so they cannot go stale unnoticed.
        bad = os.path.join(tmp, 'not-json.json')
        open(bad, 'w', encoding='utf-8').write('this is not json\n')
        nofr = os.path.join(tmp, 'no-frozen.json')
        open(nofr, 'w', encoding='utf-8').write('{"item": "x"}\n')
        cases = [
            (os.path.join(tmp, 'not-there.json'), 'missing', 'INPUT_MISSING'),
            (bad, 'invalid', 'INPUT_BAD_JSON'),
            (nofr, 'invalid', 'INPUT_NO_FROZEN'),
        ]
        okc, gotc = True, []
        for path, want_o, want_m in cases:
            _x, o2, m2 = load_product(path)
            gotc.append('%s / %s' % (o2, m2))
            if (o2, m2) != (want_o, want_m):
                okc = False
        cells.append(('input-classify', 0, 0 if okc else 1, okc))
        print('%-14s want=0  got=%d %s'
              % ('input-classify', 0 if okc else 1, 'OK' if okc else 'BAD'))
        if a.verbose or not okc:
            for got_line, (path, want_o, want_m) in zip(gotc, cases):
                print('    %-18s got=%-24s want=%s / %s'
                      % (os.path.basename(path), got_line, want_o, want_m))

        # cell 0 / positive: the real product on disk
        cell('positive', 0, [a.product, '--self-key', key])

        # cell 1 / forged: frozen[<key>] rewritten to hold the *last* dump's own
        # bytes, so the checker must refuse the identity and exit 1.
        d = copy.deepcopy(product_obj)
        raw = json.dumps(d, ensure_ascii=False, indent=1).encode()
        if key in (d.get('frozen') or {}):
            d['frozen'][key] = {
                'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
                'mtime': d['frozen'][key].get('mtime'),
            }
        forged = os.path.join(tmp, 'forged.json')
        open(forged, 'w', encoding='utf-8').write(
            json.dumps(d, ensure_ascii=False, indent=1))
        cell('forged', 1, [forged, '--self-key', key], must_contain='not_established')

        # cell / no-self-key: the checker must answer its OWN verdict here, not
        # the harness.  Taking a product whose frozen entries are named something
        # else and asking for the original key reaches checker rc 2 /
        # NO_FROZEN_SELF -- the path whose absence let a renamed forgery report
        # "could not read" instead of "no such entry".  Asserting it here keeps
        # that branch from being swallowed by the harness's input guard.
        renamed = copy.deepcopy(product_obj)
        renamed['frozen'] = {'some-other-name.json': next(iter(product_obj['frozen'].values()))}
        rp = os.path.join(tmp, 'renamed.json')
        open(rp, 'w', encoding='utf-8').write(
            json.dumps(renamed, ensure_ascii=False, indent=1))
        cell('no-self-key', 2, [rp, '--self-key', key],
             must_contain='NO_FROZEN_SELF')

        # cell 2 / missing product
        cell('missing', 2, [os.path.join(tmp, 'no-such-file.json')], must_contain='NO_PRODUCT')

        # cells 3-4 / the delta reproducer that the identity checker's docstring
        # points at.  ★ It has to be run by something, or the number the docstring
        # quotes is again a reading kept where nothing executes it.
        #   cell 3: on the synthetic fixture the slope must come out at the value
        #           *derived* from the product (4 here, 5 on marks129.json); a
        #           hard-coded 5 is exactly what this cell exists to catch.
        got, out = run_delta([a.product])
        slope_line = [l for l in out.splitlines() if l.startswith('ABSOLUTE')]
        derived = int(slope_line[0].rsplit('+', 1)[1].split()[0]) if slope_line else None
        ok3 = got == 0 and derived is not None and ('expected slope %+d' % derived) in out
        cells.append(('delta', 0, got, ok3))
        print('%-10s want=%-2d got=%-2d %s (derived slope %s)'
              % ('delta', 0, got, 'OK' if ok3 else 'BAD', derived))
        if a.verbose or not ok3:
            for line in out.rstrip().splitlines():
                print('    ' + line)

        #   cell 4: the derived slope must be *the product's*, not a constant.  Two
        #           more frozen entries whose paths live under the product's own
        #           recorded root must move the slope by exactly +2; if the script
        #           returns the same number for both, it is quoting a constant.
        #           ★ The added paths must be under that root, not under some fixed
        #           "/synthetic" — the checker scopes its count to the root it reads
        #           from the product, so a path outside it correctly contributes 0.
        d2 = copy.deepcopy(product_obj)
        root2 = None
        for k in sorted(d2['frozen']):
            p = d2['frozen'][k].get('path')
            if isinstance(p, str):
                root2 = p.rsplit('/', 1)[0]
                break
        for extra in ('a.py', 'b.py'):
            d2['frozen'][extra] = {
                'path': root2 + '/' + extra, 'bytes': 3,
                'mtime': '1970-01-01T00:00:00.000000000',
                'sha256': hashlib.sha256(b'x\n').hexdigest()}
        wide = os.path.join(tmp, 'wider.json')
        open(wide, 'w', encoding='utf-8').write(json.dumps(d2, ensure_ascii=False, indent=1))
        got4, out4 = run_delta([wide])
        slope4 = [l for l in out4.splitlines() if l.startswith('ABSOLUTE')]
        derived4 = int(slope4[0].rsplit('+', 1)[1].split()[0]) if slope4 else None
        ok4 = got4 == 0 and derived4 is not None and derived4 == derived + 2
        cells.append(('delta-slope', 0, got4, ok4))
        print('%-10s want=%-2d got=%-2d %s (two more paths => slope %s, was %s)'
              % ('delta-slope', 0, got4, 'OK' if ok4 else 'BAD', derived4, derived))
        if a.verbose or not ok4:
            for line in out4.rstrip().splitlines():
                print('    ' + line)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    passed = sum(1 for _, _, _, ok in cells if ok)
    print()
    print('CELLS pass=%d/%d' % (passed, len(cells)))
    print('VERDICT %s' % ('all_cells_expected' if passed == len(cells) else 'some_cells_failed'))
    return 0 if passed == len(cells) else 1


if __name__ == '__main__':
    sys.exit(main())
