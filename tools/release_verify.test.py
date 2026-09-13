import hashlib, io, shutil, subprocess, sys, tarfile, tempfile, zipfile
from pathlib import Path

def main():
    with tempfile.TemporaryDirectory(prefix="inimerse-release-test-") as td:
        root = Path(td)
        artifacts = [root / f"inimerse-0.4.0-Linux-x86_64.{ext}" for ext in ("tar.gz", "zip", "deb")]
        tar_path, zip_path, deb_path = artifacts
        payload = b"#!/bin/sh\nexit 0\n"
        with tarfile.open(tar_path, "w:gz") as archive:
            for entry in ("inimerse", "inim"):
                info = tarfile.TarInfo(f"inimerse-0.4.0-Linux-x86_64/bin/{entry}")
                info.mode = 0o755
                info.size = len(payload)
                archive.addfile(info, io.BytesIO(payload))
        with zipfile.ZipFile(zip_path, "w") as archive:
            for entry in ("inimerse", "inim"):
                info = zipfile.ZipInfo(f"inimerse-0.4.0-Linux-x86_64/bin/{entry}")
                info.external_attr = (0o100755 << 16)
                archive.writestr(info, payload)
        deb_path.write_text(".deb fixture", encoding="utf-8")
        zip_bytes = zip_path.read_bytes()
        lines = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}" for path in artifacts]
        (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
        tool = Path(__file__).with_name("release_verify.py")
        good = subprocess.run([sys.executable, str(tool), str(root), "--version", "0.4.0"], capture_output=True)
        assert good.returncode == 0, good.stderr.decode(errors="replace")
        missing_wasm = subprocess.run(
            [sys.executable, str(tool), str(root), "--version", "0.4.0", "--require-wasm"],
            capture_output=True,
        )
        assert missing_wasm.returncode != 0
        assert b"missing WASM probe" in missing_wasm.stderr
        artifacts[1].write_text("tampered", encoding="utf-8")
        bad = subprocess.run([sys.executable, str(tool), str(root), "--version", "0.4.0"], capture_output=True)
        assert bad.returncode != 0
        artifacts[1].write_bytes(b"not a zip")
        lines = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}" for path in artifacts]
        (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
        malformed = subprocess.run(
            [sys.executable, str(tool), str(root), "--version", "0.4.0"],
            capture_output=True,
        )
        assert malformed.returncode != 0
        assert b"invalid zip archive" in malformed.stderr
        artifacts[1].write_bytes(zip_bytes)
        lines = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}" for path in artifacts]
        (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
        winget = root / "winget"
        winget.mkdir()
        (winget / "installer.yaml").write_text(
            "PackageVersion: 0.4.0\nInstallerSha256: REPLACE_WITH_RELEASE_SHA256\n",
            encoding="utf-8",
        )
        placeholder = subprocess.run(
            [sys.executable, str(tool), str(root), "--version", "0.4.0",
             "--winget-dir", str(winget)],
            capture_output=True,
        )
        assert placeholder.returncode != 0
        (winget / "installer.yaml").write_text(
            "PackageVersion: 0.4.0\nInstallerSha256: " + "a" * 64 + "\n",
            encoding="utf-8",
        )
        ready = subprocess.run(
            [sys.executable, str(tool), str(root), "--version", "0.4.0",
             "--winget-dir", str(winget)],
            capture_output=True,
        )
        assert ready.returncode == 0, ready.stderr.decode(errors="replace")

        nonexec = root / "nonexec"
        nonexec.mkdir()
        nonexec_tar = nonexec / artifacts[0].name
        with tarfile.open(nonexec_tar, "w:gz") as archive:
            for entry in ("inimerse", "inim"):
                info = tarfile.TarInfo(f"inimerse-0.4.0-Linux-x86_64/bin/{entry}")
                info.mode = 0o644
                info.size = len(payload)
                archive.addfile(info, io.BytesIO(payload))
        shutil.copyfile(artifacts[1], nonexec / artifacts[1].name)
        shutil.copyfile(artifacts[2], nonexec / artifacts[2].name)
        nonexec_lines = [
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}"
            for path in (nonexec / name for name in (p.name for p in artifacts))
        ]
        (nonexec / "SHA256SUMS").write_text(
            "\n".join(nonexec_lines) + "\n", encoding="utf-8"
        )
        rejected = subprocess.run(
            [sys.executable, str(tool), str(nonexec), "--version", "0.4.0"],
            capture_output=True,
        )
        assert rejected.returncode != 0
        assert b"not executable" in rejected.stderr
    print("release verifier: ok")

if __name__ == "__main__":
    main()
