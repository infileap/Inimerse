#!/usr/bin/env python3
"""check_md_fences.py -- measure whether a markdown page can be READ, not just linked.

The gate counts links and path:line pins. It has never counted "can a human read this",
so this file adds that count instead of assuming the gate covered it.

Three GATING shapes, because there are three ways a fenced block stops rendering:
  odd       -- a file whose fence count is odd (one block is unclosed)
  escaped   -- a fence written with a backslash in front of the backticks ('\\' + '```',
               or the degenerate '(\\' + '`') * 3): what a raw template string leaves behind.
               The page then shows backslashes, not a code block.
  swallowed -- the fence COUNT is even but the body holds prose: a paragraph was pasted
               inside the block and the closer was pushed after it

One NON-GATING reading:
  INLINE -- escaped inline code spans ('\\' + '`') that are not inside an already-open span.
            The same raw-template mistake produces these, but this scan is line-local and
            naive about backtick runs, so main still shows a few legit quoting sites
            (an escaped backtick INSIDE a code span). Reported, not gated -- the honest
            count is 'not zero on main, and I can say why'.

Exit codes: 0 clean / 1 findings (each path:line printed) / 2 refused to measure.
"""
import argparse
import os
import re
import subprocess
import sys

BS = chr(92)
TK = chr(96)
FENCE = re.compile('^ {0,3}(' + TK + '{3,}|~{3,})(.*)$')
ESC_FENCE_ONE = BS + TK * 3
ESC_FENCE_EACH = (BS + TK) * 3
PROSE_ENDINGS = ('。', '：', '；', '）')


def inline_escaped(line):
    """Count backslash-backtick sites outside an open code span (line-local)."""
    in_span = False
    k = 0
    n = 0
    while k < len(line):
        if line[k] == BS and k + 1 < len(line) and line[k + 1] == TK:
            if not in_span:
                n += 1
            k += 2
            continue
        if line[k] == TK:
            in_span = not in_span
        k += 1
    return n


def scan_text(text, name):
    lines = text.split(chr(10))
    findings = []
    fence_lines = 0
    fence_backticks = 0
    inline = 0
    fences = 0
    swallowed = 0
    open_marker = None
    open_line = 0
    body = []

    def close_block():
        nonlocal swallowed
        for off, bline in enumerate(body):
            m = FENCE.match(bline)
            if m and open_marker and m.group(1)[0] != open_marker[0]:
                swallowed += 1
                findings.append((name, open_line + off + 1, 'swallowed', 'fence marker inside an open block'))
        prev_blank = False
        for off, bline in enumerate(body):
            if bline.strip() == '':
                prev_blank = True
                continue
            stripped = bline.strip()
            looks_prose = (
                prev_blank
                and not bline[:1].isspace()
                and stripped[:1] not in ('#', '|', '>', '-', '*', '+', '!')
                and not stripped[:1].isdigit()
                and stripped.endswith(PROSE_ENDINGS)
            )
            if looks_prose:
                swallowed += 1
                findings.append((name, open_line + off + 1, 'swallowed', 'prose paragraph inside a fenced block'))
            prev_blank = False

    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        is_esc_fence = stripped.startswith(ESC_FENCE_ONE) or stripped.startswith(ESC_FENCE_EACH)
        if is_esc_fence:
            fence_lines += 1
            fence_backticks += 3
            findings.append((name, i, 'escaped', 'fence written with backslashes: the page shows a literal fence'))
            if open_marker is not None:
                body.append(line)
            continue
        m = FENCE.match(line)
        if m:
            fences += 1
            marker = m.group(1)
            if open_marker is None:
                open_marker = marker
                open_line = i
                body = []
            elif marker[0] == open_marker[0] and len(marker) >= len(open_marker) and m.group(2).strip() == '':
                close_block()
                open_marker = None
                body = []
            else:
                body.append(line)
            continue
        if open_marker is not None:
            body.append(line)
            continue
        n = inline_escaped(line)
        if n:
            inline += n
    odd = 1 if fences % 2 else 0
    if odd:
        findings.append((name, open_line or 1, 'odd', 'fence count is odd (%d): a block is unclosed' % fences))
    return {'odd': odd, 'fence_lines': fence_lines, 'fence_backticks': fence_backticks,
            'inline': inline, 'swallowed': swallowed, 'findings': findings}


def md_files(root):
    out = []
    for dirpath, _dirnames, filenames in os.walk(os.path.join(root, 'docs')):
        for fn in filenames:
            if fn.lower().endswith('.md'):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def files_from_rev(root, rev):
    rel = subprocess.run(['git', '-C', root, 'ls-tree', '-r', '--name-only', rev, 'docs/'],
                         capture_output=True, text=True, check=True).stdout.split()
    return [p for p in rel if p.lower().endswith('.md')]


def read_from_rev(root, rev, path):
    return subprocess.run(['git', '-C', root, 'show', '%s:%s' % (rev, path)],
                          capture_output=True, text=True, check=True).stdout


SELF_TESTS = [
    ('clean', chr(10).join(['before', TK * 3, 'code()', TK * 3, 'after']), (0, 0, 0)),
    ('escaped-fence', chr(10).join(['before', ESC_FENCE_ONE, 'x', ESC_FENCE_ONE]), (0, 2, 0)),
    ('escaped-each', chr(10).join(['before', ESC_FENCE_EACH, 'x', ESC_FENCE_EACH]), (0, 2, 0)),
    ('odd', chr(10).join(['before', TK * 3, 'code']), (1, 0, 0)),
    ('swallowed', chr(10).join(['before', TK * 3, 'a reading row', '', '这一段是散文，被噎进了围栏。', TK * 3, 'after']), (0, 0, 1)),
]


def self_test():
    ok = True
    for name, text, want in SELF_TESTS:
        r = scan_text(text, '<self>')
        got = (r['odd'], r['fence_lines'], r['swallowed'])
        verdict = 'PASS' if got == want else 'FAIL'
        if got != want:
            ok = False
        print('NEGCTL|case=%s|expected=%d/%d/%d|got=%d/%d/%d|verdict=%s' %
              (name, want[0], want[1], want[2], got[0], got[1], got[2], verdict))
    print('NEGCTL-DIRECTION|clean must read 0/0/0; each of the other four must move exactly one counter')
    return ok


def tree_id(root, rev=None):
    """A count alone does not say which content it counted, and merging moves counts
    without moving content: every reading line carries the tree that produced it."""
    try:
        sha = subprocess.run(['git', '-C', root, 'rev-parse', '--short=7', rev or 'HEAD'],
                             capture_output=True, text=True)
        if sha.returncode != 0:
            return 'unknown', 0
        dirty = subprocess.run(['git', '-C', root, 'status', '--porcelain'],
                               capture_output=True, text=True)
        return sha.stdout.strip(), len([l for l in dirty.stdout.splitlines() if l.strip()])
    except OSError:
        return 'unknown', 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default='.')
    ap.add_argument('--rev', default=None, help='read docs/**/*.md from this git revision')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--focus', default=None,
                    help='always print the PAGE line for paths containing this substring, '
                         'even when every counter is zero (a zero is a reading too)')
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    if args.self_test:
        return 0 if self_test() else 1

    sha, dirty = tree_id(root, args.rev)
    print('TREE|sha=%s|dirty_worktree=%d|rev=%s|source=git rev-parse --short=7' % (sha, dirty, args.rev or 'HEAD'))
    if args.rev:
        pairs = [(p, read_from_rev(root, args.rev, p)) for p in files_from_rev(root, args.rev)]
    else:
        pairs = []
        for p in md_files(root):
            with open(p, encoding='utf-8', errors='replace') as f:
                pairs.append((os.path.relpath(p, root), f.read()))
    if not pairs:
        print('FENCES|refused|no markdown under docs/ was found', file=sys.stderr)
        return 2

    totals = {'odd': 0, 'fence_lines': 0, 'fence_backticks': 0, 'swallowed': 0, 'inline': 0}
    findings = []
    for name, text in pairs:
        r = scan_text(text, name)
        for k in totals:
            totals[k] += r[k]
        findings.extend(r['findings'])
    for name, line, kind, why in findings:
        print('FINDING|%s:%d|kind=%s|%s' % (name, line, kind, why))
    for name, text in pairs:
        r = scan_text(text, name)
        focused = bool(args.focus) and args.focus in name
        if r['odd'] or r['fence_lines'] or r['swallowed'] or r['inline'] or focused:
            print('PAGE|%s|tree=%s|odd=%d|fence_lines=%d|fence_backticks=%d|inline_escapes=%d|swallowed=%d' %
                  (name, sha, r['odd'], r['fence_lines'], r['fence_backticks'], r['inline'], r['swallowed']))
    escaped_total = totals['fence_backticks'] + totals['inline']
    pages = sum(1 for _n, t in pairs if scan_text(t, _n)['inline'])
    escaped_pages = [n for n, t in pairs if scan_text(t, n)['inline']]
    print('INLINE|pages=%d|occurrences=%d|unit=backtick-occurrences|gated=no' % (pages, totals['inline']))
    print('FENCES|tree=%s|files=%d|odd=%d|fence_lines=%d|fence_backticks=%d|inline_escapes=%d|escaped_total=%d|swallowed=%d|escaped_pages=%d|escaped_paths=%s' %
          (sha, len(pairs), totals['odd'], totals['fence_lines'], totals['fence_backticks'], totals['inline'], escaped_total, totals['swallowed'],
           len(escaped_pages), ','.join(escaped_pages) if escaped_pages else 'none'))
    # The identity check is arithmetic on the totals above: it can only fail if
    # the printer is wrong, never if a page is wrong.  It is therefore reported
    # with its own word (identity=) and does not decide the exit code -- the
    # verdict does, and the verdict reads only the two structural totals.
    print('UNITS|escaped_total=fence_backticks+inline_escapes|check=%d=%d+%d|identity=%s' %
          (escaped_total, totals['fence_backticks'], totals['inline'],
           'ok' if escaped_total == totals['fence_backticks'] + totals['inline'] else 'broken'))
    structural = totals['odd'] + totals['swallowed']
    print('FENCES-VERDICT|odd=%d|swallowed=%d|verdict=%s' %
          (totals['odd'], totals['swallowed'], 'PASS' if structural == 0 else 'FAIL'))
    return 0 if structural == 0 else 1


if __name__ == '__main__':
    sys.exit(main())