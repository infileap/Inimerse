"""Engine-side desugaring: `inimerse --desugar` rewrites `say@target expr`.

`say@target` is the one documented rewrite (docs/API.md §3.2) that is
*unreachable* without the desugarer, because `@` has no case in the lexer at all
(docs/API.md §3.2.1): the engine rejects the sugar and only `--desugar` turns it
into `say_target("target", expr)`.  That makes it worth an executable pin rather
than a hand-run.

This replaces tools/desugar_probe.sh, which was the only executable evidence for
the whole `--desugar` channel and was referenced by nothing -- so it never ran in
any suite.  Same three assertions, plus the negative control that the engine does
*not* accept the sugar on its own; ported to Python so it also runs on Windows,
where the shell probe could not.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = 'say@chat "hello"\nsay@ai "trace";\nprint "plain"\n'
REWRITES = ('say_target("chat", "hello")', 'say_target("ai", "trace")', 'say "plain"')


def find_engine():
    if len(sys.argv) > 1:
        return Path(sys.argv[1]).resolve()
    candidates = []
    env = os.environ.get("INIMERSE_BIN")
    if env:
        candidates.append(Path(env))
    for _dir in ("build", "build-local", "build-windows-gcc", "build-py"):
        for _name in ("inimerse", "inimerse.exe"):
            candidates.append(Path(_dir) / _name)
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand.resolve()
    return None


def main():
    engine = find_engine()
    assert engine, "inimerse engine binary not found (set INIMERSE_BIN to override, or pass it as argv[1])"
    with tempfile.TemporaryDirectory(prefix="inimerse-desugar-") as td:
        src = Path(td) / "in.im"
        out = Path(td) / "out.im"
        src.write_text(SOURCE, encoding="utf-8")

        rc = subprocess.run([str(engine), "--desugar", str(src), str(out)],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=60)
        assert rc.returncode == 0, f"inimerse --desugar exited {rc.returncode}\n{rc.stdout}\n{rc.stderr}"
        assert out.is_file(), f"inimerse --desugar wrote no output file\n{rc.stderr}"
        text = out.read_text(encoding="utf-8")
        for needle in REWRITES:
            assert needle in text, f"the desugared output is missing {needle!r}:\n{text}"

        # Negative control: the same source without --desugar must be refused.
        # If this ever starts passing, `say@target` has become core syntax and
        # docs/API.md §3.2.1 ("仅经脱糖") is stale.
        rc2 = subprocess.run([str(engine), "--no-mods", str(src)],
                             capture_output=True, text=True, encoding="utf-8",
                             errors="replace", timeout=60)
        assert rc2.returncode != 0, (
            "the engine ran `say@chat` without --desugar; the rewrite is no longer "
            f"the only path\n{rc2.stdout}\n{rc2.stderr}")

    print("desugar tests: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
