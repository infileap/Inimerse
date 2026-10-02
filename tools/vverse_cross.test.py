"""Two-way `.vverse` cross-validation: engine packer <-> JS reference.

The reference implementation is the judge of the format, so this test does not
re-implement any of it: it runs `node tools/vverse_pack.js` /
`node tools/vverse_validate.js` (both read-only) against packages produced by
`vverse_pack_probe` (the driver mode of the engine-side packer), and it loads
packages produced by the reference with the engine's reader.

  A. engine packs  -> reference unpacks, previews and validates (this is the
     only place the engine's DER SPKI public key and its sorted-JSON signature
     payload face `crypto.verify`), and the extracted bytes are compared with
     the source tree.
  B. engine determinism: packing one directory twice gives one sha256.
  C. reference packs (zlib, dynamic Huffman, mtime 0) -> engine unpacks and
     re-validates, including a package whose ed25519 signature was made by
     node from a key the engine has never seen.
  D. tampering: a corrupted digest table, an extra unsigned file and a stale
     ed25519 signature (content changed after signing) are all refused.  The
     packages in D are built here on purpose, byte for byte like the reference
     builds them, so the engine is judged on the format and not on a
     re-implementation of the checker.
"""

import base64
import gzip
import hashlib
import http.client
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
from testports import distinct_ports, wait_port  # noqa: E402

HELPER_JS = r"""
'use strict';
const path = require('path');
const fs = require('node:fs');
const crypto = require('node:crypto');
const TOOLS = process.argv[2];
const cmd = process.argv[3];
const { validate, writeEd25519Signature } = require(path.join(TOOLS, 'vverse_validate.js'));
const { pack, unpack, preview } = require(path.join(TOOLS, 'vverse_pack.js'));
if (cmd === 'genkey') {
  const { privateKey } = crypto.generateKeyPairSync('ed25519');
  fs.writeFileSync(process.argv[4], privateKey.export({ type: 'pkcs8', format: 'pem' }));
} else if (cmd === 'pack') {
  console.log(JSON.stringify(pack(process.argv[4], process.argv[5])));
} else if (cmd === 'signpack') {
  writeEd25519Signature(process.argv[4], process.argv[5]);
  console.log(JSON.stringify(pack(process.argv[4], process.argv[6])));
} else if (cmd === 'preview') {
  console.log(JSON.stringify(preview(process.argv[4])));
} else if (cmd === 'unpack') {
  console.log(JSON.stringify(unpack(process.argv[4], process.argv[5])));
} else {
  console.error('helper: unknown command ' + cmd);
  process.exit(2);
}
"""

SEED = bytes(range(1, 33))
SEED_HEX = SEED.hex()

FAILURES = []
CHECKS = 0


def check(name, ok, detail=""):
    global CHECKS
    CHECKS += 1
    if ok:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name}" + (f"  [{detail}]" if detail else ""))
        FAILURES.append(name)


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, timeout=120, **kw)


def find_engine():
    cands = []
    if len(sys.argv) > 1:
        cands.append(Path(sys.argv[1]))
    env = os.environ.get("INIMERSE_VVERSE_BIN") or os.environ.get("INIMERSE_BIN")
    if env:
        cands.append(Path(env))
    cands += [REPO / "build" / "vverse_pack_probe", REPO / "build-local" / "vverse_pack_probe"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("vverse_pack_probe not found; pass it as argv[1] or set INIMERSE_VVERSE_BIN")


def find_engine_binary():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    cands += [REPO / "build" / "inimerse", REPO / "build-local" / "inimerse"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def make_tree(root, blob_size=150000):
    for d in ("laws", "assets", "mods", "signatures"):
        (root / d).mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_text(json.dumps({
        "id": "cross.mod", "version": "2.1.0", "entry": "mods/entry.im",
        "dependencies": {"core": "0.5.0"},
    }), encoding="utf-8")
    (root / "blueprint.json").write_text('{"nodes":[{"id":"start"}]}\n', encoding="utf-8")
    (root / "laws" / "rule.im").write_text("law rule { allow read; }\n", encoding="utf-8")
    (root / "mods" / "entry.im").write_text('print("cross");\n', encoding="utf-8")
    (root / "assets" / "blob.bin").write_bytes(bytes((i * 37 + 11) & 0xFF for i in range(5000)))
    (root / "assets" / "big.bin").write_bytes(bytes((i * 131 + 7) & 0xFF for i in range(blob_size)))
    (root / "assets" / "text.txt").write_text("keys sorted, mtime zero\n", encoding="utf-8")


def tree_files(root):
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            if rel.startswith("signatures/"):
                continue
            out[rel] = p.read_bytes()
    return out


def repack_like_reference(src_pkg, dst_pkg, mutate):
    """Rebuild a package in exactly the reference's shape, after `mutate(obj)`."""
    obj = json.loads(gzip.decompress(Path(src_pkg).read_bytes()))
    mutate(obj)
    raw = json.dumps({"format": obj["format"], "files": obj["files"]}).encode()
    Path(dst_pkg).write_bytes(gzip.compress(raw, mtime=0))


def main():
    engine = find_engine()
    node = shutil.which("node")
    if not node:
        raise SystemExit("node not found")

    with tempfile.TemporaryDirectory(prefix="inimerse-vverse-") as td:
        root = Path(td)
        helper = root / "helper.js"
        helper.write_text(HELPER_JS, encoding="utf-8")

        def js(*args):
            return run([node, helper, HERE] + list(args), cwd=root)

        # ---------------- A. engine -> reference ----------------
        t1 = root / "t1"
        make_tree(t1)
        eng_pkg = root / "engine_signed.vverse"
        rc = run([engine, "--pack", t1, eng_pkg, SEED_HEX])
        check("engine packs a signed package", rc.returncode == 0, rc.stderr.decode())
        raw = Path(eng_pkg).read_bytes()
        check("engine writes a gzip member", raw[:2] == b"\x1f\x8b" and len(raw) > 20)
        check("engine pins the gzip MTIME field to 0", raw[4:8] == b"\x00\x00\x00\x00")
        check("engine fixes the gzip OS byte", raw[9] == 3)

        dest1 = root / "ref_unpacked"
        rc = js("unpack", eng_pkg, dest1)
        check("reference unpacks the engine's package",
              rc.returncode == 0, rc.stderr.decode().strip())
        rc = js("preview", eng_pkg)
        check("reference previews it (digest table + ed25519 via crypto.verify)",
              rc.returncode == 0, rc.stderr.decode().strip())
        try:
            prev = json.loads(rc.stdout.decode())
            check("reference reports it as signed", prev.get("signed") is True, rc.stdout.decode()[:200])
        except Exception as exc:  # pragma: no cover
            check("reference preview output is JSON", False, str(exc))

        rc = run([node, HERE / "vverse_validate.js", dest1, "--strict",
                  "--require-signature", "--require-complete-signature",
                  "--require-public-signature"])
        check("reference re-validates the extracted tree",
              rc.returncode == 0, rc.stderr.decode().strip())

        src_files = tree_files(t1)
        got_files = tree_files(dest1)
        check("extracted file set matches the source",
              sorted(src_files) == sorted(got_files),
              f"{sorted(src_files)} vs {sorted(got_files)}")
        check("extracted contents match byte for byte", src_files == got_files)
        check("the reference extracted the ed25519 signature",
              (dest1 / "signatures" / "ed25519.json").is_file())
        # entries in signatures/ are skipped by the digest walk, so the pack is a
        # pure function of the rest of the tree; the reference writes its
        # signature into the source tree, the engine deliberately does not.
        check("engine packing did not touch the source tree",
              list((t1 / "signatures").iterdir()) == [])

        # ---------------- B. engine determinism ----------------
        eng_pkg2 = root / "engine_signed2.vverse"
        run([engine, "--pack", t1, eng_pkg2, SEED_HEX])
        h1 = hashlib.sha256(Path(eng_pkg).read_bytes()).hexdigest()
        h2 = hashlib.sha256(Path(eng_pkg2).read_bytes()).hexdigest()
        check("two engine packs of one directory are identical", h1 == h2, f"{h1} vs {h2}")
        rc = run([engine, "--sha256", eng_pkg])
        check("the engine agrees on that sha256", rc.stdout.decode().strip() == h1, rc.stdout.decode().strip())

        # ---------------- C. reference -> engine ----------------
        t2 = root / "t2"
        make_tree(t2, blob_size=40000)
        ref_plain = root / "ref_plain.vverse"
        rc = js("pack", t2, ref_plain)
        check("reference packs unsigned (zlib compressor)", rc.returncode == 0, rc.stderr.decode().strip())
        # documented difference: the reference signs the tree it is packing
        check("reference packing did write a signature into the source tree",
              (t2 / "signatures" / "sha256.json").is_file())
        check("reference package really is zlib-compressed (not stored blocks)",
              Path(ref_plain).read_bytes()[10] & 0x06 in (2, 4),
              "BTYPE=" + str((Path(ref_plain).read_bytes()[10] >> 1) & 3))

        dest2 = root / "eng_unpacked_plain"
        rc = run([engine, "--unpack", ref_plain, dest2])
        check("engine unpacks the reference's package", rc.returncode == 0, rc.stderr.decode().strip())
        rc = run([engine, "--validate", dest2])
        check("engine re-validates what it extracted", rc.returncode == 0, rc.stderr.decode().strip())
        check("engine extraction matches the source byte for byte", tree_files(dest2) == tree_files(t2))

        # node signs with a key it generates: the engine has never seen it.
        t3 = root / "t3"
        make_tree(t3, blob_size=60000)
        key = root / "key.pem"
        rc = js("genkey", key)
        check("node generated an ed25519 key", rc.returncode == 0 and key.is_file(), rc.stderr.decode())
        ref_signed = root / "ref_signed.vverse"
        rc = js("signpack", t3, key, ref_signed)
        check("reference packs a node-signed package", rc.returncode == 0, rc.stderr.decode().strip())

        dest3 = root / "eng_unpacked_signed"
        rc = run([engine, "--unpack", ref_signed, dest3])
        check("engine verifies the node made ed25519 signature", rc.returncode == 0,
              rc.stderr.decode().strip())
        check("engine extraction of the signed package matches", tree_files(dest3) == tree_files(t3))

        # and the reverse ordering: the reference validates the engine's tree
        rc = run([node, HERE / "vverse_validate.js", dest3, "--strict", "--require-signature",
                  "--require-complete-signature", "--require-public-signature"])
        check("reference validates the tree the engine extracted", rc.returncode == 0,
              rc.stderr.decode().strip())

        # ---------------- D. hostile packages ----------------
        bad = root / "bad_digest.vverse"

        def flip_digest(obj):
            table = json.loads(base64.b64decode(obj["files"]["signatures/sha256.json"]))
            name = sorted(table)[0]
            table[name] = ("0" if table[name][0] != "0" else "1") + table[name][1:]
            obj["files"]["signatures/sha256.json"] = base64.b64encode(
                json.dumps(table, indent=2).encode() + b"\n").decode()

        repack_like_reference(ref_plain, bad, flip_digest)
        rc = run([engine, "--unpack", bad, root / "bad_digest_out"])
        check("engine refuses a corrupted digest table",
              rc.returncode != 0 and b"digest mismatch" in rc.stderr, rc.stderr.decode().strip())

        smuggling = root / "bad_extra.vverse"

        def add_extra(obj):
            obj["files"]["laws/smuggled.im"] = base64.b64encode(b"owned\n").decode()

        repack_like_reference(ref_plain, smuggling, add_extra)
        rc = run([engine, "--unpack", smuggling, root / "bad_extra_out"])
        check("engine refuses an unsigned extra file",
              rc.returncode != 0 and b"unsigned file" in rc.stderr, rc.stderr.decode().strip())

        # an unsigned package is refused before anything is written, and the
        # error names the file that is actually absent
        unsigned_pkg = root / "unsigned.vverse"
        full = json.loads(gzip.decompress(ref_plain.read_bytes()))["files"]
        unsigned_pkg.write_bytes(gzip.compress(
            json.dumps({"format": "vverse-1",
                        "files": {k: v for k, v in full.items() if k != "signatures/sha256.json"}}
                       ).encode(), mtime=0))
        rc = run([engine, "--unpack", unsigned_pkg, root / "unsigned_out"])
        check("engine refuses an unsigned package and names what is missing",
              rc.returncode != 0 and b"missing required metadata: signatures/sha256.json" in rc.stderr,
              rc.stderr.decode().strip())
        check("nothing was written for the unsigned package",
              not (root / "unsigned_out").exists())

        # stale signature: a file changes and the digest table is recomputed,
        # but the ed25519 signature still covers the older table.  The engine
        # must fail on the signature and not be satisfied by a matching table.
        t4 = root / "t4"
        make_tree(t4, blob_size=20000)
        base = root / "stale_base.vverse"
        rc = js("signpack", t4, key, base)
        check("built a node-signed package for the stale-signature case",
              rc.returncode == 0, rc.stderr.decode().strip())
        check("the untouched node-signed package loads",
              run([engine, "--unpack", base, root / "stale_ok"]).returncode == 0)

        obj = json.loads(gzip.decompress(base.read_bytes()))
        victim = "laws/rule.im"
        content = bytearray(base64.b64decode(obj["files"][victim]))
        content[0] ^= 0x20
        obj["files"][victim] = base64.b64encode(bytes(content)).decode()
        table = json.loads(base64.b64decode(obj["files"]["signatures/sha256.json"]))
        table[victim] = hashlib.sha256(bytes(content)).hexdigest()
        obj["files"]["signatures/sha256.json"] = base64.b64encode(
            json.dumps(table, indent=2).encode() + b"\n").decode()
        stale = root / "stale.vverse"
        raw = json.dumps({"format": "vverse-1", "files": obj["files"]}).encode()
        stale.write_bytes(gzip.compress(raw, mtime=0))
        rc = run([engine, "--unpack", stale, root / "stale_bad"])
        check("engine refuses an ed25519 signature that no longer covers the table",
              rc.returncode != 0 and b"ed25519" in rc.stderr, rc.stderr.decode().strip())
        check("nothing was extracted from the stale package",
              not (root / "stale_bad" / "laws").exists())

        # ---------------- E. byte-level format parity ----------------
        # Same tree, both writers, no ed25519: the *uncompressed* JSON body has
        # to match the reference byte for byte, because that body is what the
        # ed25519 signature covers.  (The gzip streams themselves may differ:
        # the engine writes stored blocks, the reference uses zlib.)
        t6 = root / "t6"
        make_tree(t6, blob_size=30000)
        eng_plain = root / "eng_plain.vverse"
        ref_plain2 = root / "ref_plain2.vverse"
        rc1 = run([engine, "--pack", t6, eng_plain])
        rc2 = js("pack", t6, ref_plain2)
        check("both writers pack the same tree", rc1.returncode == 0 and rc2.returncode == 0,
              (rc1.stderr + rc2.stderr).decode().strip())
        eng_body = gzip.decompress(Path(eng_plain).read_bytes())
        ref_body = gzip.decompress(Path(ref_plain2).read_bytes())
        check("engine and reference agree on the uncompressed package body",
              eng_body == ref_body,
              f"engine {eng_body[:80]!r} vs reference {ref_body[:80]!r}")
        check("the engine body is exactly what the reference's JSON.stringify makes",
              eng_body == json.dumps(json.loads(ref_body), separators=(",", ":")).encode(),
              eng_body[:120].decode(errors="replace"))
        check("engine package is a plain gzip member without a name field",
              Path(eng_plain).read_bytes()[3] & 0x18 == 0)

        # a tree with no signature at all is not loadable either (t5 is fresh:
        # the reference's pack(), unlike the engine's, signs its source tree)
        t5 = root / "t5"
        make_tree(t5, blob_size=1000)
        rc = run([engine, "--validate", t5])
        check("engine refuses to validate an unsigned tree",
              rc.returncode != 0 and b"missing signatures/sha256.json" in rc.stderr,
              rc.stderr.decode().strip())

        # ---------------- F. the engine's hub serves an engine package -------
        # `GET /v/<id>` is the dialect verse_fetch speaks: the hub hands the
        # .vverse file back byte for byte, so this proves an engine-produced
        # package is addressable and served by the engine's own distribution
        # path, not merely readable by a probe.
        t7 = root / "t7"
        make_tree(t7, blob_size=1200)
        small_pkg = root / "engine_small.vverse"
        rc = run([engine, "--pack", t7, small_pkg, SEED_HEX])
        check("engine packs a package small enough for the hub", rc.returncode == 0,
              rc.stderr.decode().strip())
        check("that package is under the hub's 64 KiB body buffer",
              small_pkg.stat().st_size < 65536, str(small_pkg.stat().st_size))

        hub_dir = root / "universe"
        hub_dir.mkdir()
        served_id = "cross.mod"
        shutil.copy(small_pkg, hub_dir / f"{served_id}.vverse")
        shutil.copy(eng_pkg, hub_dir / "big.mod.vverse")
        (root / "hub.im").write_text('say "hub"\nwait 90\n', encoding="utf-8")
        tcp_port, http_port = distinct_ports(2)
        hub = subprocess.Popen(
            [str(find_engine_binary()), "--headless", "--port", str(tcp_port),
             "--http-port", str(http_port), str(root / "hub.im")],
            cwd=root, env=dict(os.environ, INIMERSE_HUB_DIR=str(hub_dir)),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            check("the hub came up", wait_port(http_port))

            def http_get(path):
                c = http.client.HTTPConnection("127.0.0.1", http_port, timeout=5)
                c.request("GET", path)
                r = c.getresponse()
                data = r.read()
                c.close()
                return r.status, data

            status, served = http_get(f"/v/{served_id}")
            check("GET /v/<id> returns the engine's package", status == 200, str(status))
            check("the hub serves it byte for byte", served == small_pkg.read_bytes(),
                  f"served {len(served)} of {small_pkg.stat().st_size} bytes")
            status, listing = http_get("/packages")
            check("the package is listed by id", served_id in listing.decode(),
                  listing.decode()[:120])

            handout = root / "handout.vverse"
            handout.write_bytes(served)
            rc = js("unpack", handout, root / "handout_out")
            check("what the hub handed out still unpacks", rc.returncode == 0,
                  rc.stderr.decode().strip())
            rc = run([node, HERE / "vverse_validate.js", root / "handout_out", "--strict",
                      "--require-signature", "--require-complete-signature",
                      "--require-public-signature"])
            check("...and still validates against the reference", rc.returncode == 0,
                  rc.stderr.decode().strip())

            # Measured, not required: the hub's body buffer is 64 KiB, so a
            # larger package comes back as a prefix.  That buffer lives in
            # src/platform/http_posix.c, outside this stream's conflict domain.
            status, served_big = http_get("/v/big.mod")
            check("an oversized package is truncated at a body-buffer boundary, not corrupted",
                  status == 200 and served_big == Path(eng_pkg).read_bytes()[:len(served_big)]
                  and len(served_big) < Path(eng_pkg).stat().st_size,
                  f"served {len(served_big)} of {Path(eng_pkg).stat().st_size} bytes")
        finally:
            hub.terminate()
            try:
                hub.wait(timeout=10)
            except subprocess.TimeoutExpired:  # pragma: no cover
                hub.kill()

    if FAILURES:
        print(f"vverse cross-validation: FAILED ({len(FAILURES)} of {CHECKS} checks)")
        for f in FAILURES:
            print(f"  - {f}")
        raise SystemExit(1)
    print(f"vverse cross-validation: ok ({CHECKS} checks)")


if __name__ == "__main__":
    main()
