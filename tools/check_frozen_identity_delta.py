#!/usr/bin/env python3
"""check_frozen_identity_delta.py — re-derive the two readings check_frozen_identity.py quotes.

Why this exists (docs/AUDIT.md §1.264-§1.268): the identity check answers "is
frozen[<self>] the first dump?" — a yes/no.  Its docstring also quotes *numbers*
(the byte difference, and how it moves with path length), and numbers in a
tracked file must be re-derivable, not remembered.  This script is the entry
point those numbers point at.

Two readings, both computed from the product alone (no re-run of the generator):

  1. DELTA  = len(DUMP2) - len(DUMP1), where DUMP2 is the product as it stands
     and DUMP1 is the same object with `frozen = {}` and `four_piece` removed.

  2. SLOPE  = d(DELTA) / d(root length).  Measured by *substituting whole root
     prefixes* — every string that is the recorded root, or lives under it, is
     rewritten to a synthetic root of a different length.  Padding individual
     strings instead moves only some of them and measures the padding, not the
     product.

★ The slope is derived, never assumed.  A path string contributes +1 byte per
character to DELTA exactly when it appears in DUMP2 and not in DUMP1, so the
expected slope is `#strings_under_root(DUMP2) - #strings_under_root(DUMP1)`.
For the generated `marks129.json` that reads 6 - 1 = 5; for the synthetic
fixture the test file builds it reads 4 - 1 = 3.  Do not copy those numbers —
the script prints the count it used.  Hard-coding 5 would make this pass on one
product and fail on every other, which is a reading of the product dressed up
as a property of the checker.

The count is scoped to the recorded root, not to "starts with /".  This product
carries a literal `"/"` (a suffix field in `answers`); I measured that it appears
in **both** dumps and therefore cancels out of the difference, so counting it
would inflate both counts by one and still yield the right slope.  Scoping to the
root is still the honest formulation: the prediction should count only the
strings that actually move when the root is renamed, rather than lean on that
cancellation holding for every future product.

Usage:
    python3 tools/check_frozen_identity_delta.py <product.json> [-v]

Exit codes: 0 the readings hold and agree with the derived prediction, 1 a
prediction failed, 2 cannot read / cannot reconstruct (no `frozen`, no
`four_piece`, or no recorded root).
"""
import argparse
import copy
import json
import sys

ROOT_LENGTHS = (29, 36, 42, 44, 45, 65)


def ser(o):
    return json.dumps(o, ensure_ascii=False, indent=1).encode()


def two_dumps(d):
    first = {k: v for k, v in d.items() if k != 'four_piece'}
    first['frozen'] = {}
    return ser(first), ser(d)


def delta_of(d):
    a, b = two_dumps(d)
    return len(b) - len(a)


def under_root(o, root, out=None):
    """Every string that is `root` itself or lives beneath it, in serialization order.

    ★ Counting "strings that begin with /" would be wrong in both directions:
    this product contains a literal `"/"` (a suffix field in `answers`), which
    looks absolute but never moves with the root, and it would have been summed
    into the slope prediction on one side only if the two dumps had differed in
    whether they carried it.  Scoping the count to `root` is what makes the
    prediction a function of the product's real paths.
    """
    out = [] if out is None else out
    if isinstance(o, dict):
        for v in o.values():
            under_root(v, root, out)
    elif isinstance(o, list):
        for v in o:
            under_root(v, root, out)
    elif isinstance(o, str) and (o == root or o.startswith(root + '/')):
        out.append(o)
    return out


def root_of(d):
    """The directory the product records for itself, or None."""
    for name in sorted((d.get('frozen') or {})):
        v = d['frozen'][name]
        if isinstance(v, dict) and isinstance(v.get('path'), str):
            return v['path'].rsplit('/', 1)[0]
    return None


def regrow(o, old, new):
    """Rewrite every string that *is* `old` or starts with `old/`.

    ★ Both cases are needed.  `frozen[*].path` starts with `old + '/'`, but
    `four_piece[*].pwd` **equals** `old` exactly — a guard written only as
    `startswith(old + '/')` skips those and reports a slope of 3 where the
    product has 5.  That is not a rounding detail: it is the difference between
    modelling "the directory was renamed longer" and "some strings in it were
    extended", and only the first is what the docstring claims.
    """
    if isinstance(o, dict):
        for k, v in o.items():
            if isinstance(v, str) and (v == old or v.startswith(old + '/')):
                o[k] = new + v[len(old):]
            else:
                regrow(v, old, new)
    elif isinstance(o, list):
        for i, v in enumerate(o):
            if isinstance(v, str) and (v == old or v.startswith(old + '/')):
                o[i] = new + v[len(old):]
            else:
                regrow(v, old, new)


def synthetic_root(n):
    return '/' + 'r' * (n - 1)


def measure(product, verbose=False):
    """Return (ok, report-lines).  Pure: touches no file, writes nothing."""
    lines = []
    say = lines.append

    raw = open(product, 'rb').read()
    d = json.loads(raw)
    if not isinstance(d, dict):
        return None, ['BAD_SHAPE product is not an object']
    if not d.get('frozen'):
        return None, ['NO_FROZEN product has no frozen entries']
    if 'four_piece' not in d:
        return None, ['NO_FOUR_PIECE product has no four_piece']
    root = root_of(d)
    if root is None:
        return None, ['NO_ROOT no frozen[*].path to read a root from']

    a, b = two_dumps(d)
    delta = len(b) - len(a)
    say('PRODUCT       %s' % product)
    say('DUMP1_REPRO   bytes=%d' % len(a))
    say('DUMP2_REPRO   bytes=%d  equals_on_disk=%s' % (len(b), b == raw))
    say('DELTA         %d' % delta)
    say('ROOT          %r  (len %d)' % (root, len(root)))

    first = {k: v for k, v in d.items() if k != 'four_piece'}
    first['frozen'] = {}
    n1 = len(under_root(first, root))
    n2 = len(under_root(d, root))
    expected = n2 - n1
    say('ABSOLUTE      DUMP1=%d DUMP2=%d  => expected slope %+d per character'
        % (n1, n2, expected))

    ok = True
    say('')
    say('SLOPE by whole-root substitution (recorded root -> synthetic root):')
    for n in ROOT_LENGTHS:
        e = copy.deepcopy(d)
        regrow(e, root, synthetic_root(n))
        got = delta_of(e)
        want = delta + expected * (n - len(root))
        good = got == want
        ok = ok and good
        say('  rootlen %-4d delta %-6d  predicted %-6d %s'
            % (n, got, want, 'ok' if good else 'MISMATCH'))

    # each group measured on its own; the whole must be their sum
    say('')
    say('PARTS (each measured; the whole must be their sum):')
    parts = {}
    for name, sel in (
            ('criteria_file.path', lambda e: [e['criteria_file']]),
            ('frozen[*].path', lambda e: [e['frozen'][k] for k in e['frozen']]),
            ('four_piece[*].pwd', lambda e: [e['four_piece'][s] for s in ('pre', 'post')]),
    ):
        e = copy.deepcopy(d)
        for obj in sel(e):
            for k in list(obj):
                if isinstance(obj[k], str) and (obj[k] == root or obj[k].startswith(root + '/')):
                    obj[k] = obj[k] + 'x'
        parts[name] = delta_of(e) - delta
        say('  %-24s %+d per character' % (name, parts[name]))
    total = sum(parts.values())
    ok = ok and total == expected
    say('  %-24s %+d   expected %+d  %s'
        % ('sum', total, expected, 'ok' if total == expected else 'MISMATCH'))

    say('')
    say('NESTING/KEY/TRAILER closure of the delta (%d B):' % delta)
    vf, vp = ser(d['frozen']), ser(d['four_piece'])
    lf, lp = vf.count(b'\n') + 1, vp.count(b'\n') + 1
    kf, kp = len(b'"frozen": '), len(b'"four_piece": ')
    placeholder = len(b'"frozen": {}\n')
    terms = [
        ('frozen value, standalone', len(vf)),
        ('frozen nesting (+1 space x %d lines)' % (lf - 1), lf - 1),
        ('"frozen": key text', kf),
        ('frozen trailing comma + newline + close-indent', 2),
        ('four_piece value, standalone', len(vp)),
        ('four_piece nesting (+1 space x %d lines)' % (lp - 1), lp - 1),
        ('"four_piece": key text', kp),
        ('four_piece trailing newline + close-indent', 2),
        ('minus DUMP1 placeholder line', -placeholder),
    ]
    tot = 0
    for name, v in terms:
        tot += v
        say('  %-52s %+6d' % (name, v))
    ok = ok and tot == delta
    say('  %-52s %+6d   equals DELTA %d  %s'
        % ('sum', tot, delta, 'ok' if tot == delta else 'MISMATCH'))
    return ok, lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('product', help='a generated product json')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args(argv)
    try:
        ok, lines = measure(args.product, args.verbose)
    except Exception as exc:
        print('BAD_JSON %s: %s' % (args.product, exc))
        return 2
    for line in lines:
        print(line)
    if ok is None:
        return 2
    print('')
    print('VERDICT %s' % ('delta_and_slope_reproduced' if ok else 'prediction_failed'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
