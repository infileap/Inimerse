import hashlib, subprocess, sys, tempfile
from pathlib import Path

def main():
    with tempfile.TemporaryDirectory(prefix="inimerse-release-test-") as td:
        root = Path(td)
        artifacts = [root / f"inimerse-0.4.0-Linux-x86_64.{ext}" for ext in ("tar.gz", "zip", "deb")]
        for path in artifacts:
            path.write_text(path.suffix, encoding="utf-8")
        lines = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}" for path in artifacts]
        (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
        tool = Path(__file__).with_name("release_verify.py")
        good = subprocess.run([sys.executable, str(tool), str(root), "--version", "0.4.0"], capture_output=True)
        assert good.returncode == 0, good.stderr.decode(errors="replace")
        artifacts[1].write_text("tampered", encoding="utf-8")
        bad = subprocess.run([sys.executable, str(tool), str(root), "--version", "0.4.0"], capture_output=True)
        assert bad.returncode != 0
    print("release verifier: ok")

if __name__ == "__main__":
    main()
