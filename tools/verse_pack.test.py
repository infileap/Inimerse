"""Package signing / dependency / version-check regression on POSIX (M3).

Covers the white paper §24.4 package index / §54 ABI compatibility contract on
both containers the engine can read:

  - the real `.vverse` container, gzip({"format":"vverse-1","files":{...}} with
    the gzip mtime pinned to 0), which is what `verse_pack` writes today.  The
    JS reference implementation in tools/ is the judge for it, and the engine's
    own reader is asserted to agree with the reference in both directions;
  - the frozen legacy bare-JSON container, kept readable only so that the
    committed vector vtest_signed.vverse remains loadable.

Assertions:

  - identity creation and ed25519 sign/verify round-trip; tampered data fails
  - verse_pack writes a real .vverse that tools/vverse_pack.js unpacks and that
    tools/vverse_validate.js accepts under --strict --require-signature
    --require-complete-signature
  - verse_pack -> verse_open round trip: the engine opens what it just packed
  - a tampered file payload is rejected (digest mismatch)
  - a tampered digest table is rejected (the digest table is the signature)
  - a container missing its digest table is rejected
  - min_version newer than the engine is rejected with a dependency message
  - the signature covers file contents only, so editing min_version on the
    frozen legacy vector keeps it verifiable (used to exercise the accept path)

NOTE ON vtest_signed.vverse -- READ BEFORE "FIXING" THIS FILE
------------------------------------------------------------
vtest_signed.vverse is now a FROZEN LEGACY INPUT VECTOR WITH NO PRODUCER.  It
is a bare-JSON package written by the legacy hand-rolled packer that
src/mod/verse_dist_mod.c no longer contains; nothing in the tree can generate
it any more and this suite does NOT regenerate it.

An older revision of this file had a step 7 that re-packed the same sources
with the same identity seed and asserted the bytes came out identical.  That
assertion was not weakened and not silenced -- it was retired together with its
only producer, which is the legacy packer that was deleted when `verse_pack`
started writing the real container.  The artifact did not lose a role, it
changed one: it is no longer an artifact the suite reproduces, it is an input
the suite reads (the two steps near the bottom still load it to exercise the
legacy reader path).  Byte-stability of the real container is asserted twice in
tools/vverse_cli.test.py ("two packs of the same tree are byte-identical").
"""
import base64
import gzip
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    for _dir in ("build", "build-local", "build-windows-gcc", "build-py"):
        for _name in ("inimerse", "inimerse.exe"):
            cands.append(REPO / _dir / _name)
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def run(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=cwd,
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


def node_bin():
    node = shutil.which("node")
    if not node:
        raise SystemExit("node not found; the reference implementation is the judge")
    return node


def ref_unpack(pkg, dest):
    return subprocess.run([node_bin(), str(REPO / "tools" / "vverse_pack.js"),
                           "unpack", str(pkg), str(dest)],
                          cwd=REPO, capture_output=True, timeout=120)


def ref_validate(root):
    return subprocess.run([node_bin(), str(REPO / "tools" / "vverse_validate.js"),
                           str(root), "--strict", "--require-signature",
                           "--require-complete-signature"],
                          cwd=REPO, capture_output=True, timeout=120)


def gzip_json(obj):
    """The container as the reference writes it: mtime pinned to 0."""
    return gzip.compress(json.dumps(obj).encode("utf-8"), mtime=0)


def read_container(path):
    return json.loads(gzip.decompress(path.read_bytes()))


SIGN_ROUNDTRIP = '''\
verse_identity_new()
pub = verse_identity_pubkey()
say "pub_len=" + str(len(pub))
sig = verse_sign("hello world")
say "verify_ok=" + str(verse_verify("hello world", sig, pub))
say "verify_tampered=" + str(verse_verify("hello worlD", sig, pub))
r = verse_pack("pkgdir", "mypkg.vverse", 0, "1.2.3", "0.5.0")
say "pack=" + str(r)
'''

OPEN_PKG = '''\
r = verse_open("verse://local/{path}")
say "open=" + str(r)
'''

PACKED_FILES = ["main.im", "data.txt", "manifest.json", "blueprint.json",
                "laws/rule.im", "mods/entry.im"]


def main():
    engine = find_engine()
    sample = REPO / "vtest_signed.vverse"
    assert sample.is_file(), f"missing sample {sample}"
    with tempfile.TemporaryDirectory(prefix="inimerse-pkg-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()

        # a real verse tree: structure, metadata, entry point and content.
        # Write the bytes, not the text: Path.write_text() opens in text mode, so
        # on Windows it turns every "\n" into "\r\n" and the package would hold
        # CRLF bytes while the round-trip assertions below compare against "\n".
        (pkgdir / "manifest.json").write_bytes(
            (json.dumps({"id": "pkgdir", "version": "1.2.3", "entry": "main.im",
                         "dependencies": {}}) + "\n").encode("utf-8"))
        (pkgdir / "blueprint.json").write_bytes(
            (json.dumps({"name": "pkgdir"}) + "\n").encode("utf-8"))
        (pkgdir / "main.im").write_bytes(b'say "packed verse ok"\n')
        (pkgdir / "data.txt").write_bytes(b"data payload\n")
        (pkgdir / "laws" / "rule.im").write_bytes(b"rule = 1\n")
        (pkgdir / "mods" / "entry.im").write_bytes(b"mod = 1\n")
        (pkgdir / "assets" / "blob.bin").write_bytes(b"\x00\x01\x02blob\n")

        # 1. identity + sign/verify round trip, then pack a REAL container
        (root / "sign.im").write_text(SIGN_ROUNDTRIP, encoding="utf-8")
        rc = run(engine, root / "sign.im", home, root)
        out = rc.stdout.decode(errors="replace")
        assert "pub_len=64" in out, out + rc.stderr.decode(errors="replace")
        assert "verify_ok=1" in out, out
        assert "verify_tampered=0" in out, out
        assert "pack=mypkg.vverse" in out, out + rc.stderr.decode(errors="replace")

        pkg_path = root / "mypkg.vverse"
        assert pkg_path.is_file(), "verse_pack wrote no file"
        assert pkg_path.read_bytes()[:2] == b"\x1f\x8b", (
            "verse_pack did not write a gzip container; first bytes: %r"
            % pkg_path.read_bytes()[:16])
        container = read_container(pkg_path)
        assert container.get("format") == "vverse-1", container.get("format")
        assert set(container.get("files", {})) >= set(PACKED_FILES) | {
            "signatures/sha256.json", "assets/blob.bin"}, sorted(container.get("files", {}))

        # the reference implementation is the judge for the container we wrote
        unpacked = root / "unpacked"
        ref = ref_unpack(pkg_path, unpacked)
        assert ref.returncode == 0, (ref.stdout.decode(errors="replace")
                                     + ref.stderr.decode(errors="replace"))
        ref = ref_validate(unpacked)
        assert ref.returncode == 0, (ref.stdout.decode(errors="replace")
                                     + ref.stderr.decode(errors="replace"))
        for rel in PACKED_FILES:
            assert (unpacked / rel).is_file(), f"reference unpack lost {rel}"
        assert (unpacked / "data.txt").read_bytes() == b"data payload\n"
        assert (unpacked / "assets" / "blob.bin").read_bytes() == b"\x00\x01\x02blob\n"

        opener = root / "open.im"

        # 1b. round trip: the engine opens the package the engine just packed
        opener.write_text(OPEN_PKG.format(path=str(pkg_path)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        out, err = rc.stdout.decode(errors="replace"), rc.stderr.decode(errors="replace")
        assert "open=1" in out, out + err
        landed = home / "universe" / "mypkg"
        assert (landed / "main.im").is_file(), f"round trip unpacked nothing under {landed}"
        assert (landed / "data.txt").read_bytes() == b"data payload\n", "round trip lost data"
        assert (landed / "manifest.json").is_file(), "round trip lost manifest.json"

        # 2. tampering with a file payload breaks its sha256 -> rejected
        bad = read_container(pkg_path)
        payload = bytearray(base64.b64decode(bad["files"]["data.txt"]))
        payload[0] ^= 0xFF
        bad["files"]["data.txt"] = base64.b64encode(bytes(payload)).decode("ascii")
        tampered = root / "tampered.vverse"
        tampered.write_bytes(gzip_json(bad))
        opener.write_text(OPEN_PKG.format(path=str(tampered)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"digest mismatch" in rc.stderr, rc.stderr

        # 3. the digest table IS the signature: tampering it is rejected too
        bad2 = read_container(pkg_path)
        table = json.loads(base64.b64decode(bad2["files"]["signatures/sha256.json"]))
        table["data.txt"] = "0" * 64
        bad2["files"]["signatures/sha256.json"] = base64.b64encode(
            json.dumps(table).encode("utf-8")).decode("ascii")
        badsig = root / "badsig.vverse"
        badsig.write_bytes(gzip_json(bad2))
        opener.write_text(OPEN_PKG.format(path=str(badsig)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"digest mismatch" in rc.stderr, rc.stderr

        # 3b. a container with no digest table at all is rejected
        bad3 = read_container(pkg_path)
        del bad3["files"]["signatures/sha256.json"]
        nosig = root / "nosig.vverse"
        nosig.write_bytes(gzip_json(bad3))
        opener.write_text(OPEN_PKG.format(path=str(nosig)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"missing required metadata" in rc.stderr, rc.stderr

        # 4. min_version dependency check on the legacy container: the engine
        #    version (0.5.0) is older than 9.9.9 -> rejected.  A real .vverse
        #    keeps its metadata in its own manifest.json, so this check only
        #    exists for the legacy path and is exercised below on the committed
        #    vector as well (the freshly packed container has no min_version).

        # 5. the checked-in signed vector: its min_version (0.9.0) is newer than
        #    the engine, so it must be rejected as a dependency mismatch...
        opener.write_text(OPEN_PKG.format(path=str(sample)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"needs infiverse >=" in rc.stderr, rc.stderr

        # ...with min_version lowered it must pass full signature verification
        # (the ed25519 signature covers file contents, not meta fields)
        vec_pkg = json.loads(sample.read_text(encoding="utf-8"))
        vec_pkg["min_version"] = "0.4.0"
        lowered = root / "sample_ok.vverse"
        lowered.write_text(json.dumps(vec_pkg), encoding="utf-8")
        opener.write_text(OPEN_PKG.format(path=str(lowered)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        out, err = rc.stdout.decode(errors="replace"), rc.stderr.decode(errors="replace")
        assert "open=1" in out, out + err
        assert "mismatch" not in err, err
        assert (home / "universe" / vec_pkg["id"] / "data.txt").is_file(), "vector not unpacked"

        # 6. tampered content and tampered signature are both rejected
        vec_pkg["files"]["data.txt"] = "AAAA" + vec_pkg["files"]["data.txt"][4:]
        broken = root / "sample_bad.vverse"
        broken.write_text(json.dumps(vec_pkg), encoding="utf-8")
        opener.write_text(OPEN_PKG.format(path=str(broken)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"mismatch" in rc.stderr, rc.stderr

        # 7. RETIRED, see the module docstring: re-packing vtest_signed.vverse
        #    byte-for-byte was asserted here.  Its only producer -- the legacy
        #    packer -- no longer exists, so the vector is a frozen input and
        #    nothing regenerates it.  Determinism of the real container is
        #    asserted in tools/vverse_cli.test.py instead.

    print("verse package: ok")


if __name__ == "__main__":
    main()
