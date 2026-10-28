#!/usr/bin/env python3
"""The single decision point for `%` (MOD_RULE v1).

`%` used to be decided in four independent places:

  * the interpreter            (`src/vm/vm.c`, `L_MOD`),
  * the native backend         (`src/compilation/aot_native.c`, `nv_mod` in `kPreamble`),
  * the wasm backend           (`src/compilation/wasm_backend.c`, three `W_I64_REM_S` sites),
  * this repository's own test (`tools/mod_semantics.test.py`, a hand-written `TABLE`).

Four places meant four chances to disagree, and one of them *did* disagree: a NaN
divisor (`7 % (0.0/0.0)`) printed 7 on the interpreter and the native backend and
was refused by the wasm backend, while a NaN dividend (`(0.0/0.0) % 7`) printed
-1, -1 and 0.  Nobody was wrong about the language; each place had its own
opinion about what a NaN truncates to.

This module is now the only place where that opinion lives.  It holds the rule
as executable Python and generates the case matrix; the three backends must
*conform*, and `tools/mod_semantics.test.py` fails when they do not.  The C
realisations carry `authority: tools/mod_spec.py MOD_RULE` comments and are not
allowed to state the rule themselves.

The rule
--------
1. Truncate BOTH operands toward zero to int64, with these boundaries:
   NaN -> 0, x >= 2^63 -> INT64_MAX, x < -2^63 -> INT64_MIN.  (This is WebAssembly's
   `i64.trunc_sat_f64_s`; the interpreter and the native backend previously used a
   plain C cast, which is undefined for NaN -- on x86-64 it produced INT64_MIN.)
2. If the truncated DIVISOR is 0, refuse the program with `division_by_zero`.
   The test is on the truncated divisor, so `7 % 0.5` is a refusal.
3. If the truncated divisor is -1, the answer is 0 -- never `INT64_MIN % -1`, which
   overflows the integer division and traps.
4. Otherwise the answer is the C remainder `a % b`: sign follows the DIVIDEND,
   magnitude is `|a| % |b|`.  It is an INT, even when an operand was a float.
5. Nothing else.  There is no float remainder, no NaN result, no promotion: the
   result is always an int64 that fits.
"""

MOD_RULE = 'mod-rule-v1'

REFUSED = 'REFUSED'

INT64_MAX = 2 ** 63 - 1
INT64_MIN = -(2 ** 63)


def trunc_sat(x):
    """Truncate toward zero to int64, saturating, NaN -> 0 (rule 1)."""
    if isinstance(x, int):
        return max(min(x, INT64_MAX), INT64_MIN)
    if x != x:                                   # NaN
        return 0
    if x >= float(2 ** 63):
        return INT64_MAX
    if x < float(-(2 ** 63)):
        return INT64_MIN
    return int(x)


def remainder(a, b):
    """The answer to `a % b`, or REFUSED (rules 2-4)."""
    y = trunc_sat(b)
    if y == 0:
        return REFUSED
    if y == -1:
        return 0
    x = trunc_sat(a)
    r = abs(x) % abs(y)
    return -r if x < 0 else r


def rule_text():
    """The rule as text -- this is what the reading's sha pins."""
    return (MOD_RULE + '|' + (trunc_sat.__doc__ or '') + '|' + (remainder.__doc__ or ''))


def spec_sha():
    import hashlib
    return hashlib.sha256(rule_text().encode('utf-8')).hexdigest()[:12]


# (expression, dividend, divisor).  The expression is what the three backends run;
# the two numbers are what it evaluates to, so the expectation is computed here and
# nowhere else.  `float('nan')` / `float('inf')` are written as the expressions the
# language itself uses for them (`0.0/0.0`, `7.0/0`).
NAN = float('nan')
INF = float('inf')

_CASES = [
    ('7 % 3', 7, 3),
    ('-7 % 3', -7, 3),
    ('7 % -3', 7, -3),
    ('-7 % -3', -7, -3),
    ('5 % 0', 5, 0),
    ('-5 % 0', -5, 0),
    ('7 % 0.0', 7, 0.0),
    ('7 % 0.5', 7, 0.5),
    ('7 % -0.5', 7, -0.5),
    ('2.5 % 1', 2.5, 1),
    ('-7.5 % 2', -7.5, 2),
    ('7.5 % -2', 7.5, -2),
    ('1 % -1', 1, -1),
    ('(-9223372036854775807 - 1) % -1', INT64_MIN, -1),
    ('9223372036854775807 % 7', INT64_MAX, 7),
    ('(-9223372036854775807 - 1) % 7', INT64_MIN, 7),
    ('3000000000 % 7', 3000000000, 7),
    ('7000000000 % 4000000000', 7000000000, 4000000000),
    ('-7000000000 % 4000000000', -7000000000, 4000000000),
    ('10 % 4294967296', 10, 4294967296),
    ('1e30 % 7', 1e30, 7),
    ('-1e30 % 7', -1e30, 7),
    ('(7.0/0) % 7', INF, 7),
    ('-7.0/0 % 7', -INF, 7),
    ('7 % (0.0/0.0)', 7, NAN),
    ('(0.0/0.0) % 7', NAN, 7),
    ('1e30 % 0.5', 1e30, 0.5),
    ('-1e-9 % 7', -1e-9, 7),
]


def matrix():
    """[(expr, want)] where want is a decimal string or REFUSED."""
    out = []
    for expr, a, b in _CASES:
        r = remainder(a, b)
        out.append((expr, REFUSED if r == REFUSED else str(r)))
    return out


def summarise():
    m = matrix()
    return {'spec': MOD_RULE, 'sha': spec_sha(), 'cases': len(m),
            'refused': sum(1 for _, w in m if w == REFUSED),
            'valued': sum(1 for _, w in m if w != REFUSED)}


if __name__ == '__main__':
    s = summarise()
    print('MOD-SPEC|rule=%s|sha=%s|cases=%d|refused=%d|valued=%d'
          % (s['spec'], s['sha'], s['cases'], s['refused'], s['valued']))
    for expr, want in matrix():
        print('MOD-CASE|%s|want=%s' % (expr, want))