#!/usr/bin/env python3
"""INIMBC round-trip instrument (T4).

Mirrors src/compiler/bytecode.c's bytecode_write / bytecode_read on an in-memory
buffer with ONE byte counter, so "written" and "consumed" are the same measurement
applied twice -- the identity written == consumed is only meaningful that way.

Reading shape:
  FIELDS|count=..|strings=..|floats=..|funcs=..|threads=..|tries=..|globals=..
  BYTES|written=<n>|consumed=<n>|delta=<n-m>|trailer=<bytes left in file>|<case>
  CLASS|case=..|form=truncate-without-drain|skip|refuse|accepted|verdict=..
  STATUS|<case>|decode=ok|null|written_count=..|read_count=..
"""
import json, struct, sys

FUNC_CAP, THREAD_CAP, GLEN_CAP = 1024, 32, 4096
FIELD_LIST = ["count","code","string_pool","float_pool","func_count","funcs",
              "func_argc","func_names","thread_count","threads","thread_argc",
              "thread_names","try_entries","global_names"]

class Seg:
    def __init__(self):
        self.count=0; self.code=[]; self.string_pool=[]; self.float_pool=[]
        self.func_count=0; self.funcs=[]; self.func_argc=[]; self.func_names=[]
        self.thread_count=0; self.threads=[]; self.thread_argc=[]; self.thread_names=[]
        self.try_entries=[]; self.global_names=[]

def enc_seg(s, out):
    def w32(v): out.extend(struct.pack("<i", v))
    def nt(b):
        # b already carries its terminating NUL: the writer stores len = strlen+1
        # and then exactly len bytes (src/compiler/bytecode.c:189-191).
        w32(len(b)); out.extend(b)
    w32(s.count)
    w32(len(s.string_pool))
    for b in s.string_pool: nt(b)
    w32(len(s.float_pool))
    for f in s.float_pool: out.extend(struct.pack("<d", f))
    for ins in s.code:
        for v in ins: w32(v)
    w32(s.func_count)
    for f, a, n in zip(s.funcs, s.func_argc, s.func_names):
        enc_seg(f, out); w32(a); nt(n)
    w32(s.thread_count)
    for t, a, n in zip(s.threads, s.thread_argc, s.thread_names):
        enc_seg(t, out); w32(a); nt(n)
    w32(len(s.try_entries))
    for (a,b,c,d) in s.try_entries:
        w32(a); w32(b); w32(c); w32(d)
    w32(len(s.global_names))
    for g in s.global_names: nt(g)

def encode(s, header=True):
    out = bytearray()
    if header: out.extend(b"INIMBC" + bytes([3,0]))
    enc_seg(s, out)
    return bytes(out)

class Refused(Exception): pass

def dec_seg(buf, cur):
    def r32():
        nonlocal cur
        if cur+4 > len(buf): raise Refused("short")
        v = struct.unpack_from("<i", buf, cur)[0]; cur += 4; return v
    def rbytes(n):
        nonlocal cur
        if n < 0 or cur+n > len(buf): raise Refused("short")
        b = bytes(buf[cur:cur+n]); cur += n; return b
    s = Seg()
    s.count = r32()
    if s.count < 0: raise Refused("neg count")
    ns = r32()
    if ns < 0: raise Refused("neg strings")
    for _ in range(ns):
        ln = r32(); s.string_pool.append(rbytes(ln))
    nf = r32()
    if nf < 0: raise Refused("neg floats")
    for _ in range(nf): s.float_pool.append(struct.unpack("<d", rbytes(8))[0])
    if s.count < 0: raise Refused("neg code")
    for _ in range(s.count):
        s.code.append(tuple(struct.unpack("<4i", rbytes(16))))
    fc = r32()
    if fc < 0: raise Refused("neg funcs")
    i = 0
    while i < fc and i < FUNC_CAP:          # src/compiler/bytecode.c:274
        sub, cur = dec_seg(buf, cur)
        s.funcs.append(sub)
        s.func_argc.append(r32())
        nl = r32(); s.func_names.append(rbytes(nl))
        i += 1
    s.func_count = fc
    tc = r32()
    if tc < 0: raise Refused("neg threads")
    j = 0
    while j < tc and j < THREAD_CAP:        # src/compiler/bytecode.c:289
        sub, cur = dec_seg(buf, cur)
        s.threads.append(sub)
        s.thread_argc.append(r32())
        nl = r32(); s.thread_names.append(rbytes(nl))
        j += 1
    s.thread_count = tc
    nt = r32()
    if nt < 0: raise Refused("neg tries")
    for _ in range(nt):
        v = struct.unpack("<4i", rbytes(16)); s.try_entries.append(v)
    ng = r32()
    if ng < 0: raise Refused("neg globals")
    for _ in range(ng):
        glen = r32()
        if glen <= 0 or glen > GLEN_CAP: raise Refused("glen %d" % glen)   # :319
        s.global_names.append(rbytes(glen))
    return s, cur

def decode(blob):
    """Returns (seg, consumed, trailer_len) or raises Refused."""
    if len(blob) < 8 or blob[:6] != b"INIMBC": raise Refused("magic")
    if blob[6] != 3: raise Refused("version")
    seg, cur = dec_seg(blob, 8)
    return seg, cur, len(blob) - cur

def fields(seg):
    return {k: getattr(seg, k) for k in FIELD_LIST}

def same_fields(a, b):
    fa, fb = fields(a), fields(b)
    return [k for k in FIELD_LIST if fa[k] != fb[k]]

def tiny_seg(name=b"m", funcs=0, threads=0, globals_=None, glen=None):
    s = Seg()
    s.count = 0
    s.func_count = funcs
    for i in range(funcs):
        sub = Seg(); s.funcs.append(sub); s.func_argc.append(0); s.func_names.append(b"f")
    s.thread_count = threads
    for i in range(threads):
        sub = Seg(); s.threads.append(sub); s.thread_argc.append(0); s.thread_names.append(b"t")
    if globals_ is None: globals_ = []
    if glen is not None: globals_ = [b"g"*(glen-1)]
    s.global_names = globals_
    return s

DEPS_MAGIC = 0x53504449  # src/compilation/deps.h: "IDPS"

def split_trailer(blob):
    """Return (stream_len, trailer_len, deps_count) -- the trailer is a 16-byte
    footer (magic|abi|count|trailer_len) preceded by its entries, so it can be
    identified rather than assumed by arithmetic."""
    if len(blob) < 16: return len(blob), 0, None
    magic, abi, count, tlen = struct.unpack_from("<4I", blob, len(blob)-16)
    # trailer_len INCLUDES the 16-byte footer (src/compilation/deps.c:12,51)
    if magic != DEPS_MAGIC or tlen > len(blob): return len(blob), 0, None
    return len(blob) - tlen, tlen, count

def run_case(label, blob):
    try:
        seg, consumed, trailer = decode(blob)
    except Refused as e:
        print("CLASS|case=%s|form=refuse|why=%s|verdict=reader_returned_null" % (label, e))
        print("STATUS|%s|decode=null|why=%s" % (label, e))
        return
    stream_len, trailer_len, deps_count = split_trailer(blob)
    written = stream_len - 8
    print("TRAILER|case=%s|trailer_bytes=%d|deps=%s|verified=%s"
          % (label, trailer_len, deps_count, "yes" if trailer_len else "none"))
    print("FIELDS|case=%s|count=%d|strings=%d|floats=%d|funcs_read=%d|funcs_decl=%d|threads_read=%d|threads_decl=%d|globals=%d"
          % (label, seg.count, len(seg.string_pool), len(seg.float_pool),
             len(seg.funcs), seg.func_count, len(seg.threads), seg.thread_count, len(seg.global_names)))
    print("BYTES|case=%s|written=%d|consumed=%d|delta=%d|trailer=%d|file=%d"
          % (label, written, consumed-8, written-(consumed-8), trailer, len(blob)))
    trunc = (seg.func_count > FUNC_CAP) or (seg.thread_count > THREAD_CAP)
    if trunc:
        form = "truncate-without-drain"
    else:
        form = "accepted"
    print("CLASS|case=%s|form=%s|verdict=%s" % (label, form,
          "structure differs from written counts" if trunc else "round-trip candidate"))
    print("STATUS|%s|decode=ok|written_count=%d|read_count=%d" % (label, seg.func_count, len(seg.funcs)))

def main():
    if len(sys.argv) < 2:
        print("usage: check_inimbc_roundtrip.py <image.inim> [--json]", file=sys.stderr)
        return 2
    path = sys.argv[1]
    blob = open(path, "rb").read()
    run_case("real:%s" % path.split("/")[-1], blob)
    # A: decode, re-encode, compare fields
    try:
        seg, _, _ = decode(blob)
        re = encode(seg)
        diff = same_fields(seg, decode(re)[0]) if True else []
        print("ROUNDTRIP|case=real|fields_differing=%d|%s|reencoded_bytes=%d|original_stream=%d"
              % (len(diff), ",".join(diff) or "-", len(re)-8, len(blob)-8))
    except Refused as e:
        print("ROUNDTRIP|case=real|skip|reason=%s" % e)

    for label, seg in [("synthetic:funcs=1025", tiny_seg(funcs=1025)),
                       ("synthetic:threads=33", tiny_seg(threads=33)),
                       ("synthetic:funcs=1024", tiny_seg(funcs=1024)),
                       ("synthetic:glen=4100", tiny_seg(glen=4100)),
                       ("synthetic:glen=4095", tiny_seg(glen=4095))]:
        run_case(label, encode(seg))
    print("DEF|eq=%s" % ",".join(FIELD_LIST))
    print("CAPS|funcs=%d|threads=%d|glen=%d|source=src/compiler/bytecode.c:274,289,319" % (FUNC_CAP, THREAD_CAP, GLEN_CAP))
    return 0

if __name__ == "__main__":
    sys.exit(main())
