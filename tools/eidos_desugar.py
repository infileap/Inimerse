#!/usr/bin/env python3
"""Translate Eidos aliases to the current record core syntax."""
import argparse, re

WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

def _compose_code(src):
    """Desugar simple identifier composition while preserving strings/comments."""
    out=[]; i=0; quote=None; line_comment=False
    while i < len(src):
        c=src[i]
        if line_comment:
            out.append(c); i += 1
            if c in '\r\n': line_comment=False
            continue
        if quote:
            out.append(c); i += 1
            if c == '\\' and i < len(src): out.append(src[i]); i += 1
            elif c == quote: quote=None
            continue
        if c in ('"', "'"): quote=c; out.append(c); i += 1; continue
        if c == '#': line_comment=True; out.append(c); i += 1; continue
        m=re.match(r'([A-Za-z_][A-Za-z0-9_]*)\s*>>\s*([A-Za-z_][A-Za-z0-9_]*)', src[i:])
        if m:
            f,g=m.group(1),m.group(2)
            out.append(f'(x -> {g}({f}(x)))'); i += m.end(); continue
        out.append(c); i += 1
    return ''.join(out)

def translate(src):
    out=[]; i=0; quote=None; line_comment=False
    while i < len(src):
        c=src[i]
        if line_comment:
            out.append(c); i += 1
            if c in '\r\n': line_comment=False
            continue
        if quote:
            out.append(c); i += 1
            if c == '\\' and i < len(src): out.append(src[i]); i += 1
            elif c == quote: quote=None
            continue
        if c in ('"', "'"):
            quote=c; out.append(c); i += 1; continue
        if c == '#' or (c == '/' and i + 1 < len(src) and src[i+1] == '/'):
            if c == '/': out.extend(['/', '/']); i += 2
            else: out.append(c); i += 1
            line_comment=True; continue
        m=WORD.match(src, i)
        if m:
            word=m.group(0); out.append('record' if word in ('eidos','ed') else word); i=m.end(); continue
        out.append(c); i += 1
    return _compose_code(''.join(out))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input'); ap.add_argument('output', nargs='?'); a=ap.parse_args()
    src=open(a.input, encoding='utf-8').read(); dst=translate(src)
    if a.output: open(a.output, 'w', encoding='utf-8', newline='').write(dst)
    else: print(dst, end='')
if __name__ == '__main__': main()
