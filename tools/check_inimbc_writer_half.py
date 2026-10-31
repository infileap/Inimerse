#!/usr/bin/env python3
"""check_inimbc_writer_half.py -- INIMBC round-trip, WRITER-HALF only.

Scope, stated once and repeated in the title on purpose:
  This proves that the image the ENGINE WRITER (buildc) produces can be read back by the
  engine itself and executed, and that the byte account of that image matches a PINNED
  reading. It does NOT prove decode(encode(x)) == x for arbitrary x. A later reader must not
  mistake a green here for the whole round-trip proposition.

Rules this test obeys (imposed by the reviewer of this lane):
  1. binary only: it drives `inimerse`, imports no z3, and builds its own image -- it never
     assumes an image already exists. `skip` is not one of its outcomes.
  2. failure is NAMED: if the binary is missing, the mirror tool is missing, buildc fails, or
     the image cannot be accounted for, it prints `not registered: nothing was checked` and
     exits 2. Silence never passes. `--self-check-refusals` measures that claim instead of
     asserting it: it drives three broken configurations and reports their exit codes.
  3. expectations are READINGS, not derivations: the constants below were copied from a
     reading taken on the tree named in TREE, and are compared verbatim. Computing them in
     the test would let the test and the code under test share one mistake.

Exit codes: 0 pass / 1 the reading contradicts the pinned expectation / 2 refused to measure.

Registration belongs to the runner (CMakeLists.txt + EXP_CTEST); this file does not register
itself.
"""
import argparse
import atexit
import os
import shutil
import subprocess
import sys
import tempfile

# Pinned reading. Source: docs/streams/dec-formal-bounds-t4.md, tree 319fd35:
#   BYTES|case=real:hello.inim|written=317|consumed=317|delta=0|trailer=92|file=417
#   ROUNDTRIP|case=real|fields_differing=0|-|reencoded_bytes=317
TREE = '319fd35'
EXPECT = {
    'written': '317',
    'consumed': '317',
    'delta': '0',
    'trailer': '92',
    'file': '417',
    'fields_differing': '0',
}
HELLO_SOURCE = 'say "hello"' + chr(10)


def refuse(why):
    print('not registered: nothing was checked', file=sys.stderr)
    print('REFUSED|%s' % why)
    return 2


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def own_dir(prefix):
    """A directory this process created, removed when it exits.

    The fixed name under /tmp was one path for every worktree and every
    concurrent process on the machine, so a parallel gate -- or this suite's
    own subprocess re-entry -- overwrote the image while another run was
    reading it.  tools/check_shared_tmp.py is the guard; this is the fix it
    asks for.  The directory is private to this process, so it is removed on
    exit rather than left behind for the next run to trip over.
    """
    d = tempfile.mkdtemp(prefix=prefix)
    atexit.register(shutil.rmtree, d, True)
    return d


def kv(line):
    out = {}
    for part in line.strip().split('|')[1:]:
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    return out


def mirror_reading(mirror, image):
    out = subprocess.run([sys.executable, mirror, image], capture_output=True, text=True)
    if out.returncode != 0:
        return None, 'byte-account instrument failed (rc=%d): %s' % (
            out.returncode, (out.stderr or out.stdout).strip()[:200])
    # The instrument also prints SYNTHETIC cases; only the engine-written image may feed the
    # pinned expectation. Filter by NAME (case=real), never by position: taking the last BYTES
    # line once made this test read the glen=4095 synthetic row (written=4126).
    got = {}
    for line in out.stdout.split(chr(10)):
        if 'case=real' not in line:
            continue
        if line.startswith('BYTES|') or line.startswith('ROUNDTRIP|'):
            got.update(kv(line))
    return got, None


def check(args):
    root = os.path.abspath(args.root)
    binary = args.bin if os.path.isabs(args.bin) else os.path.join(root, args.bin)
    workdir = args.workdir or own_dir('inimbc-writer-half-')
    mirror = args.mirror or os.path.join(root, 'tools', 'z3bounds', 'check_inimbc_roundtrip.py')

    print('TEST|name=inimbc-writer-half|scope=writer-half|proves=buildc output is readable by the engine')
    if not os.path.isfile(binary):
        return refuse('binary not found at %s' % binary)
    if not os.path.isfile(mirror):
        return refuse('byte-account instrument not found at %s' % mirror)

    os.makedirs(workdir, exist_ok=True)
    src = os.path.join(workdir, 'hello.im')
    image = args.image or os.path.join(workdir, 'hello.inim')
    if args.image:
        if not os.path.isfile(image):
            return refuse('image not found at %s' % image)
    else:
        if os.path.exists(image):
            os.remove(image)
        with open(src, 'w', encoding='utf-8') as f:
            f.write(HELLO_SOURCE)
        built = run([binary, 'buildc', 'hello.im', 'hello.inim'], workdir)
        if built.returncode != 0 or not os.path.isfile(image):
            return refuse('buildc failed (rc=%d): %s' % (
                built.returncode, (built.stderr or built.stdout).strip()[:200]))
    size = os.path.getsize(image)

    # end-to-end: the engine loads the IMAGE (not the source) and prints hello
    exe = run([binary, 'run', 'hello.inim' if not args.image else image], workdir)
    lines = [ln.strip() for ln in (exe.stdout + exe.stderr).split(chr(10))]
    hello_lines = [ln for ln in lines if ln == 'hello']
    e2e_ok = exe.returncode == 0 and len(hello_lines) == 1
    print('E2E|engine_reads_image=yes|rc=%d|hello_lines=%d|verdict=%s' %
          (exe.returncode, len(hello_lines), 'PASS' if e2e_ok else 'FAIL'))

    got, why = mirror_reading(mirror, image)
    if why:
        return refuse(why)
    if 'written' not in got:
        # The instrument produced no BYTES account for the real case (a skip row is not a
        # reading): nothing was measured. That is a refusal, not a failure -- an unreadable
        # image must exit 2 with the named line, never reach the green path by accident.
        return refuse('the instrument produced no case=real reading for %s' % image)
    got['file'] = str(size)

    bad = ['%s=%s(want %s)' % (k, got.get(k, 'missing'), want)
           for k, want in EXPECT.items() if got.get(k, 'missing') != want]
    print('MIRROR|written=%s|consumed=%s|delta=%s|trailer=%s|file=%s|fields_differing=%s' %
          tuple(got.get(k, 'missing') for k in ('written', 'consumed', 'delta', 'trailer', 'file', 'fields_differing')))
    print('EXPECT|tree=%s|written=%s|consumed=%s|delta=%s|trailer=%s|file=%s|fields_differing=%s' %
          (TREE, EXPECT['written'], EXPECT['consumed'], EXPECT['delta'], EXPECT['trailer'], EXPECT['file'], EXPECT['fields_differing']))
    if bad:
        print('STATUS|FAIL|differing=%s' % ','.join(bad))
        return 1
    if not e2e_ok:
        print('STATUS|FAIL|engine could not execute the image it was handed')
        return 1
    print('STATUS|PASS|scope=writer-half|note=decode(encode(x))==x for arbitrary x remains unproved')
    return 0


def self_check_refusals(args):
    """Measure the refusal path: three broken configurations, three named exits."""
    ok = True
    root = os.path.abspath(args.root)
    binary = args.bin if os.path.isabs(args.bin) else os.path.join(root, args.bin)
    workdir = own_dir('inimbc-writer-half-refusal-')
    scratch = os.path.join(workdir, 'truncated.inim')

    cases = []
    cases.append(('missing-binary', ['--bin', os.path.join(workdir, 'no-such-engine')]))
    cases.append(('missing-instrument', ['--mirror', os.path.join(workdir, 'no-such-mirror.py')]))

    # a corrupted image: take a real one, cut the last 20 bytes so the reader cannot account for it
    src = os.path.join(workdir, 'hello.im')
    good = os.path.join(workdir, 'hello.inim')
    if os.path.isfile(binary):
        with open(src, 'w', encoding='utf-8') as f:
            f.write(HELLO_SOURCE)
        run([binary, 'buildc', 'hello.im', 'hello.inim'], workdir)
        if os.path.isfile(good):
            blob = open(good, 'rb').read()
            with open(scratch, 'wb') as f:
                f.write(blob[:-20])
            cases.append(('measured-wrong-image', ['--image', scratch]))
            unreadable = os.path.join(workdir, 'unreadable.inim')
            with open(unreadable, 'wb') as f:
                f.write(b'NOTANIMAGE' + bytes(64))
            cases.append(('unreadable-image', ['--image', unreadable]))
        else:
            cases.append(('truncated-image', None))
    else:
        cases.append(('truncated-image', None))

    for name, extra in cases:
        if extra is None:
            print('NEGCTL|case=%s|skipped=no-binary|verdict=NOT-MEASURED' % name)
            ok = False
            continue
        cmd = [sys.executable, os.path.abspath(__file__), '--root', root, '--bin', args.bin] + extra
        out = subprocess.run(cmd, capture_output=True, text=True)
        named = 'not registered: nothing was checked' in out.stderr
        if name == 'measured-wrong-image':
            # measured and wrong: exits 1, no refusal line
            verdict = 'PASS' if out.returncode == 1 and not named else 'FAIL'
        else:
            verdict = 'PASS' if out.returncode == 2 and named else 'FAIL'
        if verdict == 'FAIL':
            ok = False
        print('NEGCTL|case=%s|rc=%d|named_line=%s|verdict=%s' %
              (name, out.returncode, 'yes' if named else 'no', verdict))
    print('NEGCTL-DIRECTION|cannot-measure cases must exit 2 AND print the named line; a merely wrong image must be measured and exit 1 -- a green without either is a silent skip')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', default='build/inimerse')
    ap.add_argument('--root', default='.')
    ap.add_argument('--workdir', default=None)
    ap.add_argument('--image', default=None, help='use this image instead of building one')
    ap.add_argument('--mirror', default=None)
    ap.add_argument('--self-check-refusals', action='store_true')
    args = ap.parse_args()
    if args.self_check_refusals:
        return self_check_refusals(args)
    return check(args)


if __name__ == '__main__':
    sys.exit(main())