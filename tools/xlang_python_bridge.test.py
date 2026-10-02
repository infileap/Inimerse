#!/usr/bin/env python3
"""xlang_python_bridge - the Python half of the v0.5 cross-language bridge.

This regression refuses to trust the build.  It packs the CPython extension
CMake produced into a real wheel, installs that wheel into a clean virtual
environment, and then asks the engine three questions through `import
inimerse`.  Every answer is judged against a value that does not come from the
bridge:

  * the version against ``INIMERSE_PACKAGE_VERSION`` in CMakeLists.txt,
  * the digest against ``hashlib`` over the same fixture bytes,
  * the top-level statement count against the hand count recorded in the
    fixture's own header.

Usage::

    xlang_python_bridge.test.py <build-dir> <toolchain-prefix> [repo-root]

Exit 0 = every check passed, 1 = a check failed, 77 = there is no toolchain on
this host.  77 is deliberate: ``tools/gate.sh`` prints the skipped count, so a
skip can never be mistaken for a pass.

The wheel is assembled by hand rather than with ``python3 -m build`` because
this host has no ``build`` module; a wheel is a zip with a ``.dist-info``
directory, and the module inside it is the exact file the compiler produced.
"""

import hashlib
import re
import subprocess
import sys
import sysconfig
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
FIXTURE = "src/bridge/bridge_fixture.im"

# Hand-counted in the fixture's header; the fixture itself is parsed by the
# engine, so this number is the thing the engine is checked against.
EXPECTED_COUNT = 5

CHECKS = 0
FAILURES = []


def check(name, ok, detail=""):
    global CHECKS
    CHECKS += 1
    if ok:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s  [%s]" % (name, detail))
        FAILURES.append(name)
    return ok


def note(msg):
    print("  --   %s" % msg)


def run(cmd, **kw):
    kw.setdefault("capture_output", True)
    kw.setdefault("text", True)
    kw.setdefault("timeout", 300)
    return subprocess.run(cmd, **kw)


def skip(reason):
    print("xlang_python_bridge: skipped - %s" % reason)
    sys.exit(77)


def toolchain_ok(prefix):
    return (
        (Path(prefix) / "usr/include/python3.14/Python.h").is_file()
        and (Path(prefix) / "usr/lib/jvm/java-17-openjdk-amd64/include/jni.h").is_file()
        and (Path(prefix) / "usr/lib/jvm/java-17-openjdk-amd64/bin/javac").is_file()
    )


def package_version(repo):
    text = (repo / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'set\(INIMERSE_PACKAGE_VERSION\s+"([^"]+)"\)', text)
    return m.group(1) if m else None


def wheel_tag():
    major, minor = sys.version_info[:2]
    cp = "cp%d%d" % (major, minor)
    plat = sysconfig.get_platform().replace("-", "_").replace(".", "_")
    return cp, plat


def build_wheel(bridge_dir, version):
    """Assemble a real wheel around the extension CMake just built."""
    ext = sysconfig.get_config_var("EXT_SUFFIX") or ".so"
    modules = sorted(bridge_dir.glob("inimerse*%s" % ext))
    if not modules:
        return None, ext, "no inimerse%s in %s" % (ext, bridge_dir)
    module = modules[0]

    cp, plat = wheel_tag()
    name = "inimerse-%s-%s-%s-%s.whl" % (version, cp, cp, plat)
    out = bridge_dir / name
    dist = "inimerse-%s.dist-info" % version
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(module, module.name)
        z.writestr(
            "%s/METADATA" % dist,
            "Metadata-Version: 2.1\nName: inimerse\nVersion: %s\n"
            "Summary: Infiverse cross-language bridge\n" % version,
        )
        z.writestr(
            "%s/WHEEL" % dist,
            "Wheel-Version: 1.0\nGenerator: hand-assembled; see "
            "examples/BUILDING_BRIDGES.md\nRoot-Is-Purelib: false\n"
            "Tag: %s-%s-%s\n" % (cp, cp, plat),
        )
        z.writestr(
            "%s/RECORD" % dist,
            "%s,,\n%s/METADATA,,\n%s/WHEEL,,\n%s/RECORD,,\n"
            % (module.name, dist, dist, dist),
        )
    return out, ext, None


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    build_dir = Path(argv[1]).resolve()
    prefix = Path(argv[2]).resolve()
    repo = Path(argv[3]).resolve() if len(argv) > 3 else REPO

    if not toolchain_ok(prefix):
        skip("no python3-dev/JDK prefix at %s" % prefix)

    print("xlang_python_bridge: build=%s repo=%s" % (build_dir, repo))
    bridge_dir = build_dir / "bridge"
    fixture = repo / FIXTURE

    version = package_version(repo)
    check("CMakeLists.txt declares the package version", bool(version), "no INIMERSE_PACKAGE_VERSION")
    check("the fixture exists", fixture.is_file(), str(fixture))
    if FAILURES:
        return 1

    expected_sha = hashlib.sha256(fixture.read_bytes()).hexdigest()
    expected_line = "%s %s %d" % (version, expected_sha, EXPECTED_COUNT)

    # --- the artifacts must exist: the toolchain is here, so a missing build
    # --- product is a failure, not a reason to skip.
    check("the build produced a bridge directory", bridge_dir.is_dir(), str(bridge_dir))
    if FAILURES:
        return 1

    wheel, ext, err = build_wheel(bridge_dir, version)
    check("the CPython extension is present in the build", wheel is not None, err or "")
    if FAILURES:
        return 1
    note("wheel: %s" % wheel)
    check(
        "the wheel really is a zip with a dist-info",
        zipfile.is_zipfile(wheel)
        and any(n.startswith("inimerse-%s.dist-info/" % version) for n in zipfile.ZipFile(wheel).namelist()),
        str(wheel),
    )

    with tempfile.TemporaryDirectory(prefix="inimerse-xlang-") as tmp:
        venv = Path(tmp) / "venv"
        # --without-pip: this host has no ensurepip (python3.14-venv is not
        # installed).  The host pip still does the install, into the venv's own
        # site-packages, so the import we test is a real installation into an
        # isolated interpreter.
        made = run([sys.executable, "-m", "venv", "--without-pip", str(venv)])
        check("a clean virtual environment can be created", made.returncode == 0, (made.stderr or "")[-400:])
        if FAILURES:
            return 1

        py = venv / "bin" / "python"
        site = run([str(py), "-c", "import sysconfig;print(sysconfig.get_paths()['purelib'])"])
        check("the venv reports its own site-packages", site.returncode == 0, site.stderr[-300:])
        if FAILURES:
            return 1
        site = site.stdout.strip()

        inst = run(
            [sys.executable, "-m", "pip", "install", "--no-index", "--no-deps", "--target", site, str(wheel)]
        )
        check("pip installs the wheel into the venv", inst.returncode == 0, (inst.stderr or inst.stdout)[-500:])
        if FAILURES:
            return 1

        probe = (
            "import inimerse, sys\n"
            "print(inimerse.__file__)\n"
            "print(inimerse.version(), inimerse.sha256_file(%r), inimerse.parse_count(open(%r).read()))\n"
            % (str(fixture), str(fixture))
        )
        got = run([str(py), "-c", probe], cwd=tmp)
        check("`import inimerse` works in the clean venv", got.returncode == 0, (got.stderr or "")[-800:])
        if FAILURES:
            return 1

        lines = got.stdout.strip().splitlines()
        check("the probe printed a module path and one line of answers", len(lines) == 2, got.stdout[-300:])
        if FAILURES:
            return 1

        module_file, answers = lines[0].strip(), lines[1].strip()
        check(
            "the imported module lives in the venv, not the build tree",
            str(venv) in module_file and str(bridge_dir) not in module_file,
            module_file,
        )
        check(
            "the venv is isolated from the host's site-packages",
            str(venv) in run([str(py), "-c", "import sys;print(sys.prefix)"]).stdout,
            "",
        )
        check("the three answers are exactly the independently derived line", answers == expected_line,
              "want=%r got=%r" % (expected_line, answers))
        if answers.count(" ") == 2:
            got_ver, got_sha, got_count = answers.split(" ")
            check("version() matches INIMERSE_PACKAGE_VERSION", got_ver == version,
                  "want=%s got=%s" % (version, got_ver))
            check("sha256_file() matches hashlib over the same bytes", got_sha == expected_sha,
                  "want=%s got=%s" % (expected_sha, got_sha))
            check("parse_count() matches the hand count in the fixture", got_count == str(EXPECTED_COUNT),
                  "want=%d got=%s" % (EXPECTED_COUNT, got_count))

    print("xlang_python_bridge: %d check(s), %d failure(s)" % (CHECKS, len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
