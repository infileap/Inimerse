#!/usr/bin/env python3
"""check_frozen_identity.py — is `frozen[<self>]` recording the *first* dump or the last?

Why this exists (docs/AUDIT.md §1.266 乙 / §1.267):  a verifier kept in an
ignored directory is not evidence — it can vanish exactly like the /tmp files
that already cost two rounds.  This is the same check, moved into the tracked
tree and made reproducible by anyone.

What it checks
--------------
`marks129.py` writes its product twice:

  1. `out['frozen'] = {}`                      → first `json.dump(…, indent=1)`
  2. `out['frozen'] = {freeze(three files)}`   → second `json.dump(…)` overwrites

so the bytes recorded under `frozen[<self>]` must be the **first** version, not
the file now on disk.  This script reconstructs both versions from the product
itself (no re-run of the generator) and reports whether the identity holds:

  - `DUMP2_REPRO` — the product as it stands, re-serialized with the same
    `ensure_ascii=False, indent=1`; must equal the file byte-for-byte.
  - `DUMP1_REPRO` — the same object with `frozen` set to `{}` and `four_piece`
    removed; must equal `frozen[<self>]`'s recorded bytes and sha256.

It proves an *identity*, which is stronger than "the sizes differ by N bytes":
that difference moves with path length, so it is not a constant and must not be
quoted as evidence.  Two readings, both re-derivable with
`tools/check_frozen_identity_delta.py` (which substitutes whole root prefixes
rather than padding individual strings):

  - for the generated `.scratch/ident/marks/marks129.json`, delta = **1476 B**
    at its recorded root of 42 characters;
  - the slope is **+5 B per added root character** (1411/1446/1476/1486/1491/1591
    at root lengths 29/36/42/44/45/65), because DUMP2 carries 6 absolute-path
    strings where DUMP1 carries 1 — `criteria_file` contributes 0, the three
    `frozen[*].path` contribute 3, the two `four_piece[*].pwd` contribute 2.

The four numbers this paragraph used to quote (1429/1449/1474/1576) are retired:
their copies are gone, they do not lie on the +5 slope (1474 and 1576 differ by
2 from the nearest attainable value; 1429 and 1449 by more), and they disagree
digit-for-digit with the ledger's own 1429/1449/1541/1574 for the same family.

Usage
-----
    python3 tools/check_frozen_identity.py <product.json> [--self-key NAME]

`<product.json>` is required — there is no baked-in absolute path, so this can
run against a copy, a worktree, or another machine's checkout.
Exit codes: 0 identity holds, 1 does not, 2 cannot read / cannot reconstruct.
"""
import argparse
import hashlib
import json
import os
import sys


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('product', help='path to the generated product json')
    ap.add_argument('--self-key', default=None,
                    help='key under "frozen" naming the product itself '
                         '(default: the product file name)')
    args = ap.parse_args(argv)

    p = args.product
    if not os.path.isfile(p):
        print('NO_PRODUCT %s' % p)
        return 2
    raw = open(p, 'rb').read()
    try:
        d = json.loads(raw)
    except Exception as exc:
        print('BAD_JSON %s: %s' % (p, exc))
        return 2

    print('PRODUCT      %s' % os.path.abspath(p))
    print('ON_DISK      bytes=%d sha256=%s' % (len(raw), sha256(raw)))

    dump2 = json.dumps(d, ensure_ascii=False, indent=1).encode()
    print('DUMP2_REPRO  bytes=%d sha256=%s equals_on_disk=%s'
          % (len(dump2), sha256(dump2), dump2 == raw))

    d1 = {k: v for k, v in d.items() if k != 'four_piece'}
    d1['frozen'] = {}
    dump1 = json.dumps(d1, ensure_ascii=False, indent=1).encode()
    print('DUMP1_REPRO  bytes=%d sha256=%s' % (len(dump1), sha256(dump1)))

    key = args.self_key or os.path.basename(p)
    frozen_self = (d.get('frozen') or {}).get(key)
    if not frozen_self:
        print('NO_FROZEN_SELF  frozen has no entry %r (keys: %s)'
              % (key, sorted((d.get('frozen') or {}).keys())))
        return 2
    print('FROZEN_SELF  bytes=%d sha256=%s mtime=%s'
          % (frozen_self['bytes'], frozen_self['sha256'],
             frozen_self.get('mtime')))

    bytes_eq = frozen_self['bytes'] == len(dump1)
    sha_eq = frozen_self['sha256'] == sha256(dump1)
    differs = frozen_self['sha256'] != sha256(dump2)
    print('IDENTITY     bytes_eq=%s sha_eq=%s differs_from_dump2=%s'
          % (bytes_eq, sha_eq, differs))
    ok = bytes_eq and sha_eq and differs
    print('VERDICT      %s' % ('frozen_self_is_dump1' if ok else 'not_established'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
