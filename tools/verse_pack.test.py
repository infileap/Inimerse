"""Package signing / dependency / version-check regression on POSIX (M3).

Uses the checked-in signed sample vtest_signed.vverse plus freshly packed
packages to assert (white paper §24.4 package index / §54 ABI compatibility):

  - identity creation and ed25519 sign/verify round-trip; tampered data fails
  - verse_pack signs with the local identity (publisher + signature present)
  - verse_open accepts a self-signed package
  - tampered package bytes are rejected (sha256 mismatch)
  - a tampered signature is rejected (signature mismatch)
  - min_version newer than the engine is rejected with a dependency message
  - the signature covers file contents only, so editing min_version on the
    sample keeps it verifiable (used to exercise the accept path)
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def find_engine():
    env = os.environ.get("INIMERSE_BIN")
    cands = [Path(env)] if env else []
    cands += [REPO / "build" / "inimerse", REPO / "build-local" / "inimerse"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; set INIMERSE_BIN")


def run(engine, script, home, cwd):
    return subprocess.run([str(engine), "run", str(script)], cwd=cwd,
                          env=dict(os.environ, INIMERSE_HOME=str(home)),
                          capture_output=True, timeout=60)


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


def main():
    engine = find_engine()
    sample = REPO / "vtest_signed.vverse"
    assert sample.is_file(), f"missing sample {sample}"
    with tempfile.TemporaryDirectory(prefix="inimerse-pkg-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()
        (root / "pkgdir").mkdir()
        (root / "pkgdir" / "main.im").write_text('say "packed verse ok"\n', encoding="utf-8")
        (root / "pkgdir" / "data.txt").write_text("data payload\n", encoding="utf-8")

        # 1. identity + sign/verify round trip, then pack (auto-signed)
        (root / "sign.im").write_text(SIGN_ROUNDTRIP, encoding="utf-8")
        rc = run(engine, root / "sign.im", home, root)
        out = rc.stdout.decode(errors="replace")
        assert "pub_len=64" in out, out + rc.stderr.decode(errors="replace")
        assert "verify_ok=1" in out, out
        assert "verify_tampered=0" in out, out

        pkg = json.loads((root / "mypkg.vverse").read_text(encoding="utf-8"))
        assert len(pkg.get("publisher", "")) == 64, pkg
        assert len(pkg.get("signature", "")) == 128, pkg
        assert pkg["version"] == "1.2.3" and pkg["min_version"] == "0.5.0", pkg

        opener = root / "open.im"
        opener.write_text(OPEN_PKG.format(path=str(root / "mypkg.vverse")), encoding="utf-8")
        rc = run(engine, opener, home, root)
        out, err = rc.stdout.decode(errors="replace"), rc.stderr.decode(errors="replace")
        assert "open=1" in out, out + err
        assert "mismatch" not in err, err
        assert (home / "universe" / "pkgdir" / "main.im").is_file(), "package not unpacked"

        # 2. tampering with file content breaks sha256 -> rejected
        pkg["files"]["data.txt"] = pkg["files"]["data.txt"][:-4] + "AAAA"
        tampered = root / "tampered.vverse"
        tampered.write_text(json.dumps(pkg), encoding="utf-8")
        opener.write_text(OPEN_PKG.format(path=str(tampered)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"mismatch" in rc.stderr or b"rejected" in rc.stderr, rc.stderr

        # 3. tampering with the signature itself -> rejected
        pkg2 = json.loads((root / "mypkg.vverse").read_text(encoding="utf-8"))
        pkg2["signature"] = ("0" if pkg2["signature"][0] != "0" else "1") + pkg2["signature"][1:]
        badsig = root / "badsig.vverse"
        badsig.write_text(json.dumps(pkg2), encoding="utf-8")
        opener.write_text(OPEN_PKG.format(path=str(badsig)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"signature mismatch" in rc.stderr, rc.stderr

        # 4. min_version dependency check: engine 0.5.0 < 9.9.9 -> rejected
        pkg3 = json.loads((root / "mypkg.vverse").read_text(encoding="utf-8"))
        pkg3["min_version"] = "9.9.9"
        newver = root / "newver.vverse"
        newver.write_text(json.dumps(pkg3), encoding="utf-8")
        opener.write_text(OPEN_PKG.format(path=str(newver)), encoding="utf-8")
        rc = run(engine, opener, home, root)
        assert b"open=0" in rc.stdout, rc.stdout + rc.stderr
        assert b"needs infiverse >=" in rc.stderr, rc.stderr

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

        # 7. the committed vector must be reproducible: packing the same
        #    sources with the same identity seed yields identical bytes
        seed = hashlib.sha256(b"inimerse ed25519 test vector v1").hexdigest()
        regen = root / "regen"
        (regen / "pkgdir").mkdir(parents=True)
        (regen / "pkgdir" / "main.im").write_text('say "vtest vector verse"\n', encoding="utf-8")
        (regen / "pkgdir" / "data.txt").write_bytes(b"hello verse data\r\n")
        home2 = regen / "home"
        home2.mkdir()
        (regen / "gen.im").write_text(
            'verse_identity_new()\n'
            f'write_file("{home2}/universe/identity.seed", "{seed}")\n'
            'verse_pack("pkgdir", "out.vverse", 0, "1.0.0", "0.9.0")\n', encoding="utf-8")
        rc = run(engine, regen / "gen.im", home2, regen)
        assert rc.returncode == 0, rc.stderr.decode(errors="replace")
        assert (regen / "out.vverse").read_bytes() == sample.read_bytes(), (
            "signed vector is not reproducible from its seed")
    print("verse package: ok")


if __name__ == "__main__":
    main()
