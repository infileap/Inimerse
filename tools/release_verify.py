#!/usr/bin/env python3
"""Validate a locally assembled V0.4 release directory."""
import argparse, hashlib, json, re, shutil, subprocess, tarfile, zipfile
from pathlib import Path

def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def verify_archive_layout(path, require_wasm=False):
    """Check that Linux archives contain an executable engine entrypoint."""
    if path.name.endswith(".tar.gz"):
        try:
            with tarfile.open(path, "r:gz") as archive:
                entries = {member.name: member for member in archive.getmembers()}
        except (OSError, tarfile.TarError) as exc:
            raise SystemExit(f"invalid tar.gz archive: {path.name}: {exc}")
        for entry in ("inimerse", "inim"):
            candidates = [member for name, member in entries.items()
                         if name.endswith(f"/bin/{entry}") or name == f"bin/{entry}"]
            if not candidates:
                raise SystemExit(f"missing executable {entry} in archive: {path.name}")
            if not any(member.mode & 0o111 for member in candidates):
                raise SystemExit(f"{entry} is not executable in archive: {path.name}")
        if require_wasm and not any(
            name.endswith("/share/inimerse/wasm_probe.wasm")
            for name in entries
        ):
            raise SystemExit(f"missing WASM probe in archive: {path.name}")
    elif path.name.endswith(".zip"):
        try:
            with zipfile.ZipFile(path) as archive:
                entries = archive.infolist()
        except (OSError, zipfile.BadZipFile) as exc:
            raise SystemExit(f"invalid zip archive: {path.name}: {exc}")
        for entry in ("inimerse", "inim"):
            candidates = [
                info for info in entries
                if info.filename.endswith(f"/bin/{entry}") or info.filename == f"bin/{entry}"
            ]
            if not candidates:
                raise SystemExit(f"missing executable {entry} in archive: {path.name}")
            modes = [(info.external_attr >> 16) & 0o777 for info in candidates]
            if not any(mode & 0o111 for mode in modes):
                raise SystemExit(f"{entry} is not executable in archive: {path.name}")
        if require_wasm and not any(
            info.filename.endswith("/share/inimerse/wasm_probe.wasm")
            for info in entries
        ):
            raise SystemExit(f"missing WASM probe in archive: {path.name}")

def verify_deb_dependencies(path):
    dpkg_deb = shutil.which("dpkg-deb")
    if not dpkg_deb:
        raise SystemExit("dpkg-deb is required to verify Debian metadata")
    try:
        result = subprocess.run(
            [dpkg_deb, "-f", str(path), "Depends"],
            check=False, capture_output=True, text=True,
        )
    except OSError as exc:
        raise SystemExit(f"cannot inspect Debian metadata: {exc}")
    if result.returncode != 0:
        raise SystemExit(f"invalid Debian package: {path.name}")
    dependencies = {item.strip().split()[0] for item in result.stdout.split(",") if item.strip()}
    if "python3" not in dependencies:
        raise SystemExit(f"Debian package lacks python3 dependency: {path.name}")

def verify_deb_layout(path, require_wasm=False):
    dpkg_deb = shutil.which("dpkg-deb")
    if not dpkg_deb:
        raise SystemExit("dpkg-deb is required to verify Debian contents")
    try:
        result = subprocess.run(
            [dpkg_deb, "-c", str(path)],
            check=False, capture_output=True, text=True,
        )
    except OSError as exc:
        raise SystemExit(f"cannot inspect Debian contents: {exc}")
    if result.returncode != 0:
        raise SystemExit(f"invalid Debian package: {path.name}")
    entries = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 6:
            entries[fields[-1].lstrip("./")] = fields[0]
    for entry in ("usr/bin/inimerse", "usr/bin/inim"):
        mode = entries.get(entry)
        if mode is None:
            raise SystemExit(f"missing {entry} in Debian package: {path.name}")
        if not mode.startswith("-rwx"):
            raise SystemExit(f"{entry} is not executable in Debian package: {path.name}")
    if require_wasm and "usr/share/inimerse/wasm_probe.wasm" not in entries:
        raise SystemExit(f"missing WASM probe in Debian package: {path.name}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("artifact_dir", type=Path)
    ap.add_argument("--version", required=True)
    ap.add_argument("--winget-dir", type=Path)
    ap.add_argument("--require-wasm", action="store_true")
    ap.add_argument("--require-deb-python3", action="store_true")
    args = ap.parse_args()
    root = args.artifact_dir
    expected = [root / f"inimerse-{args.version}-Linux-x86_64.{ext}" for ext in ("tar.gz", "zip", "deb")]
    missing = [str(p) for p in expected if not p.is_file()]
    if missing:
        raise SystemExit("missing release artifacts: " + ", ".join(missing))
    sums = root / "SHA256SUMS"
    if not sums.is_file():
        raise SystemExit(f"missing checksum manifest: {sums}")
    listed = {}
    for line in sums.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 2:
            listed[Path(parts[-1]).name] = parts[0].lower()
    for artifact in expected:
        actual = sha256(artifact)
        if listed.get(artifact.name) != actual:
            raise SystemExit(f"checksum mismatch: {artifact.name}")
        if artifact.name.endswith((".tar.gz", ".zip")):
            verify_archive_layout(artifact, require_wasm=args.require_wasm)
        elif artifact.name.endswith(".deb"):
            if args.require_deb_python3:
                verify_deb_dependencies(artifact)
            if args.require_wasm:
                verify_deb_layout(artifact, require_wasm=True)
    if args.winget_dir:
        manifests = list(args.winget_dir.rglob("*.yaml"))
        version_hits = sum(args.version in p.read_text(encoding="utf-8") for p in manifests)
        if not manifests or version_hits != len(manifests):
            raise SystemExit("Winget manifests are missing or version-inconsistent")
        placeholders = [
            str(p) for p in manifests
            if "REPLACE_WITH_RELEASE_SHA256" in p.read_text(encoding="utf-8")
        ]
        if placeholders:
            raise SystemExit("Winget manifests contain placeholder hashes: " + ", ".join(placeholders))
    print(json.dumps({"version": args.version, "artifacts": [p.name for p in expected], "checksums": "ok", "winget": bool(args.winget_dir)}, ensure_ascii=False))

if __name__ == "__main__":
    main()
