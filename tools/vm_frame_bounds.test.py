#!/usr/bin/env python3
"""Two ways the VM can write past the end of a thread's register window.

Both are reachable from a bytecode file alone: the call's parameter count and
the frame base are whatever the artifact says, so a saved image with an
outsized func_argc -- or simply a deep enough call chain -- reaches them
without any hand-written C.  The model that found them (docs/streams/
dec-formal-bounds.md) says they are independent: closing the call guard does
not bound the parameter count, and bounding the parameter count does not make
the guard cover a whole frame.

This test builds both witnesses at run time and states each one as a property
of the program's behaviour, not of the memory layout:

  case A  call at frame 2046 (base 2095104, one frame short of the register
          window) with func_argc=1024.  The call cannot fit, so the program
          must not run it: "sink-ran" must not appear.
  case B  call at frame 2045 with func_argc=2000000.  The parameters must be
          bounded by the frame width, so the process must survive: a signal
          death is the failure.

Independence is what makes the pair worth registering: with only the guard
fixed A goes green and B still dies; with only the bound added B goes green
and A still overruns by one register.

Exit status is 0 only when both properties hold.  No z3: the gate has no z3,
and a test that skips where it cannot reach the solver is not a test.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

HEAD = b"INIMBC"
REG_WINDOW = 2048 * 1024        # VM_THREAD_REG_COUNT
FRAME_REGS = 1024               # VM_FRAME_REGS

SRC = """func sink() { say "sink-ran" }
func deep(n) {
  if n > 1 { deep(n - 1) }
  if n == 1 { sink() }
}
deep(%(depth)d)
"""


def le32(b, p):
    return struct.unpack_from("<i", b, p)[0], p + 4


def parse_segment(b, p):
    """Walk one serialized segment and return where it ends.

    Order is bytecode_write's: count, strings, floats, code, funcs, threads,
    try, global names.  Function entries carry their argc between the nested
    segment and the name, which is the field the witnesses patch.
    """
    count, p = le32(b, p)
    n, p = le32(b, p)
    for _ in range(n):
        ln, p = le32(b, p)
        p += ln
    nf, p = le32(b, p)
    p += 8 * nf
    p += 16 * count
    fc, p = le32(b, p)
    funcs = []
    for _ in range(fc):
        _, p = parse_segment(b, p)
        argc_off = p
        argc, p = le32(b, p)
        ln, p = le32(b, p)
        name = b[p:p + ln].split(b"\x00")[0].decode("utf-8", "replace")
        p += ln
        funcs.append({"name": name, "argc": argc, "argc_off": argc_off})
    tc, p = le32(b, p)
    for _ in range(tc):
        _, p = parse_segment(b, p)
        _, p = le32(b, p)
        ln, p = le32(b, p)
        p += ln
    nt, p = le32(b, p)
    p += 16 * nt
    ng, p = le32(b, p)
    for _ in range(ng):
        ln, p = le32(b, p)
        p += ln
    return {"funcs": funcs, "end": p}, p


def build_witness(engine, workdir, tag, depth, argc):
    src = os.path.join(workdir, tag + ".im")
    raw = os.path.join(workdir, tag + ".raw.inim")
    patched = os.path.join(workdir, tag + ".inim")
    with open(src, "w") as f:
        f.write(SRC % {"depth": depth})
    r = subprocess.run([engine, "--no-mods", "compile", src, raw],
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(raw):
        raise SystemExit("compile failed for %s: rc=%d %s" % (tag, r.returncode, r.stderr.strip()))
    b = bytearray(open(raw, "rb").read())
    if not b.startswith(HEAD):
        raise SystemExit("%s: not an INIMBC image" % tag)
    seg, _ = parse_segment(b, 8)
    sink = [f for f in seg["funcs"] if f["name"] == "sink"]
    if not sink:
        raise SystemExit("%s: no sink function in the image (%s)" % (tag, [f["name"] for f in seg["funcs"]]))
    struct.pack_into("<i", b, sink[0]["argc_off"], argc)
    with open(patched, "wb") as f:
        f.write(bytes(b))
    return patched


def run(engine, path):
    try:
        r = subprocess.run([engine, "--no-mods", path], capture_output=True, timeout=300)
        return r.returncode, r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True, help="path to the inimerse engine")
    ap.add_argument("--keep", action="store_true", help="keep the scratch directory")
    args = ap.parse_args()
    engine = os.path.abspath(args.bin)
    if not os.path.exists(engine):
        print("SKIP(no executable engine at %s)" % engine)
        return 2

    workdir = tempfile.mkdtemp(prefix="frame-bounds-")
    reds = []
    greens = 0
    try:
        # case A: the call does not fit in the register window.
        pa = build_witness(engine, workdir, "caseA", 2046, FRAME_REGS)
        rc, out, err = run(engine, pa)
        ran = "sink-ran" in out
        if ran and rc == 0:
            reds.append("A")
            print("frame-bounds: case A call-at-2046 argc=1024 -- RED: the call ran anyway (rc=%s)." % rc)
        else:
            greens += 1
            print("frame-bounds: case A call-at-2046 argc=1024 -- ok: refused (rc=%s, sink-ran absent)." % rc)

        # case B: the parameter count is whatever the artifact says.
        pb = build_witness(engine, workdir, "caseB", 2045, 2000000)
        rc, out, err = run(engine, pb)
        if rc == "timeout" or (isinstance(rc, int) and rc < 0):
            reds.append("B")
            print("frame-bounds: case B call-at-2045 argc=2000000 -- RED: the process died on a signal (rc=%s)." % rc)
        elif isinstance(rc, int) and rc == 0 and "sink-ran" in out:
            greens += 1
            print("frame-bounds: case B call-at-2045 argc=2000000 -- ok: survived (rc=%s, sink-ran present)." % rc)
        else:
            reds.append("B")
            print("frame-bounds: case B call-at-2045 argc=2000000 -- RED: rc=%s stdout=%r stderr=%r"
                  % (rc, out[:120], err[:120]))

        print("frame-bounds: cases=2 red=%d green=%d%s"
              % (len(reds), greens, (" -- " + ",".join(reds)) if reds else ""))
    finally:
        if not args.keep:
            for name in sorted(os.listdir(workdir)):
                os.unlink(os.path.join(workdir, name))
            os.rmdir(workdir)
        else:
            print("frame-bounds: kept %s" % workdir)
    return 1 if reds else 0


if __name__ == "__main__":
    sys.exit(main())
