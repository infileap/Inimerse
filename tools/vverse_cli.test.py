"""`.im` -> real `.vverse`: the engine builtin `verse_pack` must not invent a format.

`verse_pack(root, out)` is registered in the engine
(`src/mod/verse_dist_mod.c`, `b_verse_pack`) and is already called from
`src/lobby_online_src.im:40`, so `.im` scripts CAN pack.  What was broken is the
container: `b_verse_pack` used to hand-roll bare JSON with sha256 **hex**
(`{"id":...,"files":{"x":"<hex>"}}`), which the rest of the toolchain cannot
read at all:

    node tools/vverse_pack.js preview <pkg>   ->  incorrect header check
    node tools/vverse_pack.js unpack  <pkg>   ->  exit 1

The format is owned by the read-only reference implementation
(`tools/vverse_pack.js` / `tools/vverse_validate.js`) and the engine-side
container that already matches it is `src/common/vverse_pack.c` +
`src/common/gzip.c`.  This test drives the builtin through the actual `.im`
boundary and judges the result with the reference, so the two cannot drift
apart again:

  1. an `.im` script calling `verse_pack` succeeds and reports the output path;
  2. `node tools/vverse_pack.js unpack` reads the package (bidirectional);
  3. the unpacked tree passes `node tools/vverse_validate.js --strict
     --require-signature --require-complete-signature` (the strictest mode: the
     digest table must exist AND cover every packaged file);
  4. the engine's own reader (`build/vverse_pack_probe --unpack`) reads it too;
  5. the extracted bytes are the source bytes, file for file;
  6. packing does NOT mutate the source tree (full sha256 manifest before/after
     must be identical) -- the JS writer drops a fresh signatures/sha256.json
     into the tree, the engine must not;
  7. packing is a pure function of the tree: two packs of the same tree are
     byte-identical.

Usage: python3 tools/vverse_cli.test.py <inimerse> <vverse_pack_probe>
       (or set INIMERSE_BIN / INIMERSE_VVERSE_BIN)
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
    return subprocess.run([str(c) for c in cmd], capture_output=True, timeout=180, **kw)


def find_engine():
    cands = []
    if len(sys.argv) > 1:
        cands.append(Path(sys.argv[1]))
    env = os.environ.get("INIMERSE_BIN")
    if env:
        cands.append(Path(env))
    cands += [REPO / "build" / "inimerse", REPO / "build-local" / "inimerse"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("inimerse engine not found; pass it as argv[1] or set INIMERSE_BIN")


def find_probe():
    cands = []
    if len(sys.argv) > 2:
        cands.append(Path(sys.argv[2]))
    env = os.environ.get("INIMERSE_VVERSE_BIN")
    if env:
        cands.append(Path(env))
    cands += [REPO / "build" / "vverse_pack_probe", REPO / "build-local" / "vverse_pack_probe"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise SystemExit("vverse_pack_probe not found; pass it as argv[2] or set INIMERSE_VVERSE_BIN")


def make_tree(root):
    """A strict-structure verse tree: every file the reference insists on."""
    for d in ("laws", "assets", "mods", "signatures"):
        (root / d).mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_text(json.dumps({
        "id": "cli.verse", "version": "3.2.1", "entry": "mods/entry.im",
        "dependencies": {"core": "1.0.0"},
    }), encoding="utf-8")
    (root / "blueprint.json").write_text('{"nodes":[{"id":"start"}]}\n', encoding="utf-8")
    (root / "laws" / "rule.im").write_text("law rule { allow read; }\n", encoding="utf-8")
    (root / "mods" / "entry.im").write_text('say "cli pack";\n', encoding="utf-8")
    (root / "assets" / "blob.bin").write_bytes(bytes((i * 41 + 3) & 0xFF for i in range(7000)))
    (root / "assets" / "note.txt").write_text("builtin pack\n", encoding="utf-8")


def tree_manifest(root):
    """Full content manifest: relative path -> (size, sha256).  mtime excluded
    on purpose: packing must not rewrite files, and a re-stat with a different
    timestamp is noise, while a changed size or digest is not."""
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            out[p.relative_to(root).as_posix()] = (
                p.stat().st_size, hashlib.sha256(p.read_bytes()).hexdigest())
    return out


def unpacked_manifest(root):
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            out[p.relative_to(root).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


PACK_IM = '''\
r = verse_pack("{tree}", "{pkg}")
say "ret=" + str(r)
'''


def main():
    engine = find_engine()
    probe = find_probe()
    node = shutil.which("node")
    assert node, "node not found: the reference implementation is the judge here"

    with tempfile.TemporaryDirectory(prefix="inimerse-vverse-cli-") as td:
        root = Path(td)
        home = root / "home"
        home.mkdir()
        tree = root / "src"
        make_tree(tree)

        # ---- 6. source tree is snapshotted before packing -------------------
        before = tree_manifest(tree)

        script = root / "pack.im"
        script.write_text(
            PACK_IM.format(tree=tree.as_posix(), pkg=(root / "out.vverse").as_posix()),
            encoding="utf-8")
        pkg = root / "out.vverse"
        rc = run([engine, "run", str(script)], cwd=root,
                 env=dict(os.environ, INIMERSE_HOME=str(home)))
        out = rc.stdout.decode(errors="replace")
        err = rc.stderr.decode(errors="replace")

        # ---- 1. the builtin reports success to the script -------------------
        check("`.im` verse_pack exits 0", rc.returncode == 0, err[-400:])
        check("`.im` verse_pack returns the output path",
              f"ret={pkg.as_posix()}" in out, out[-400:])
        check("the package file exists", pkg.is_file(), str(pkg))

        # ---- 6. packing did not mutate the source tree ----------------------
        after = tree_manifest(tree)
        check("packing does not mutate the source tree", before == after,
              f"before={sorted(before)} after={sorted(after)}")

        # ---- 2. the reference reads the engine's package --------------------
        unref = root / "unpacked-by-reference"
        r = run([node, str(REPO / "tools" / "vverse_pack.js"), "unpack", str(pkg), str(unref)])
        check("reference `vverse_pack.js unpack` reads the engine's package",
              r.returncode == 0,
              (r.stderr.decode(errors="replace") or r.stdout.decode(errors="replace"))[-400:])

        # ---- 3. strictest reference validation ------------------------------
        r = run([node, str(REPO / "tools" / "vverse_validate.js"), str(unref),
                 "--strict", "--require-signature", "--require-complete-signature"])
        check("unpacked tree passes `vverse_validate.js --strict "
              "--require-signature --require-complete-signature`",
              r.returncode == 0,
              (r.stderr.decode(errors="replace") or r.stdout.decode(errors="replace"))[-400:])

        # ---- 4. the engine's read side reads it too -------------------------
        unprobe = root / "unpacked-by-probe"
        r = run([probe, "--unpack", str(pkg), str(unprobe)])
        check("engine reader `vverse_pack_probe --unpack` reads the package",
              r.returncode == 0,
              (r.stderr.decode(errors="replace") or r.stdout.decode(errors="replace"))[-400:])

        # ---- 5. and the bytes survive the round trip ------------------------
        want = {k: v[1] for k, v in before.items()}
        got = unpacked_manifest(unref)
        # the digest table is generated by the packer, not taken from the tree
        got.pop("signatures/sha256.json", None)
        check("unpacked bytes match the source tree", want == got,
              f"want={sorted(want)} got={sorted(got)}")

        # ---- 7. determinism: same tree, same bytes --------------------------
        pkg2 = root / "out2.vverse"
        script.write_text(
            PACK_IM.format(tree=tree.as_posix(), pkg=pkg2.as_posix()), encoding="utf-8")
        rc2 = run([engine, "run", str(script)], cwd=root,
                  env=dict(os.environ, INIMERSE_HOME=str(home)))
        same = (pkg2.is_file() and pkg.is_file()
                and hashlib.sha256(pkg2.read_bytes()).hexdigest()
                == hashlib.sha256(pkg.read_bytes()).hexdigest())
        check("two packs of the same tree are byte-identical", same,
              f"rc={rc2.returncode} a={pkg.stat().st_size if pkg.is_file() else -1} "
              f"b={pkg2.stat().st_size if pkg2.is_file() else -1}")
        check("the second pack also left the source tree alone",
              tree_manifest(tree) == before)

    print()
    if FAILURES:
        print(f"vverse_cli: {len(FAILURES)} failure(s) out of {CHECKS} checks")
        for f in FAILURES:
            print(f"  - {f}")
        return 1
    print(f"vverse_cli: {CHECKS}/{CHECKS} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
