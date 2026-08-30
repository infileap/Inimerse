#!/usr/bin/env python3
"""Validate a locally assembled V0.4 release directory."""
import argparse, hashlib, json, re
from pathlib import Path

def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("artifact_dir", type=Path)
    ap.add_argument("--version", required=True)
    ap.add_argument("--winget-dir", type=Path)
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
    if args.winget_dir:
        manifests = list(args.winget_dir.rglob("*.yaml"))
        version_hits = sum(args.version in p.read_text(encoding="utf-8") for p in manifests)
        if not manifests or version_hits != len(manifests):
            raise SystemExit("Winget manifests are missing or version-inconsistent")
    print(json.dumps({"version": args.version, "artifacts": [p.name for p in expected], "checksums": "ok", "winget": bool(args.winget_dir)}, ensure_ascii=False))

if __name__ == "__main__":
    main()
