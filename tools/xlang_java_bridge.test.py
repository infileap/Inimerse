#!/usr/bin/env python3
"""xlang_java_bridge - the Java half of the v0.5 cross-language bridge.

This regression takes the JNI library and the jar that CMake produced, runs the
generated ``InimerseBridge`` facade on a real JVM, and demands that the JVM
print *the same three answers* the CPython extension prints for the same
fixture.  The answers are additionally judged against values that come from
nowhere near the bridge:

  * the version against ``INIMERSE_PACKAGE_VERSION`` in CMakeLists.txt,
  * the digest against ``hashlib`` over the same fixture bytes,
  * the top-level statement count against the hand count recorded in the
    fixture's own header.

Usage::

    xlang_java_bridge.test.py <build-dir> <toolchain-prefix> [repo-root]

Exit 0 = every check passed, 1 = a check failed, 77 = there is no toolchain on
this host.  77 is deliberate: ``tools/gate.sh`` prints the skipped count, so a
skip can never be mistaken for a pass.
"""

import hashlib
import re
import subprocess
import sys
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
    print("xlang_java_bridge: skipped - %s" % reason)
    sys.exit(77)


def java_home(prefix):
    return Path(prefix) / "usr/lib/jvm/java-17-openjdk-amd64"


def toolchain_ok(prefix):
    home = java_home(prefix)
    return (
        (Path(prefix) / "usr/include/python3.14/Python.h").is_file()
        and (home / "include/jni.h").is_file()
        and (home / "bin/javac").is_file()
        and (home / "bin/java").is_file()
    )


def package_version(repo):
    text = (repo / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'set\(INIMERSE_PACKAGE_VERSION\s+"([^"]+)"\)', text)
    return m.group(1) if m else None


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    build_dir = Path(argv[1]).resolve()
    prefix = Path(argv[2]).resolve()
    repo = Path(argv[3]).resolve() if len(argv) > 3 else REPO

    if not toolchain_ok(prefix):
        skip("no JDK/JNI prefix at %s" % prefix)

    print("xlang_java_bridge: build=%s repo=%s" % (build_dir, repo))
    home = java_home(prefix)
    java = home / "bin" / "java"
    jar = build_dir / "bridge" / "InimerseBridge.jar"
    lib = build_dir / "bridge" / "libinimerse_bridge.so"
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
    check("the build produced InimerseBridge.jar", jar.is_file(), str(jar))
    check("the build produced the JNI library", lib.is_file(), str(lib))
    if FAILURES:
        return 1

    report = run([str(java), "-version"])
    check(
        "the JDK really runs",
        report.returncode == 0 and "17.0.20.1" in (report.stderr or ""),
        (report.stderr or "")[-200:],
    )

    # The jar must carry both the generated facade and the driver.
    names = zipfile.ZipFile(jar).namelist()
    check("the jar contains the generated facade", "InimerseBridge.class" in names, ",".join(sorted(names)))
    check("the jar contains the native-method throwing class",
          "InimerseBridge$InimerseException.class" in names, ",".join(sorted(names)))
    check("the jar contains the driver", "InimerseBridgeMain.class" in names, ",".join(sorted(names)))

    # The facade does System.loadLibrary("inimerse_bridge"), and java.library.path
    # cannot be set from inside the JVM, so it is passed on the command line.
    got = run(
        [str(java), "-Djava.library.path=%s" % (build_dir / "bridge"), "-cp", str(jar),
         "InimerseBridgeMain", str(fixture), str(fixture)]
    )
    check("the JVM ran the bridge without throwing", got.returncode == 0,
          ((got.stderr or "") + (got.stdout or ""))[-800:])
    if FAILURES:
        return 1

    answers = got.stdout.strip()
    note("java says: %s" % answers)
    check("the JVM printed exactly the independently derived line", answers == expected_line,
          "want=%r got=%r" % (expected_line, answers))

    # --- the cross-language claim: the same three answers as Python ----------
    pyprobe = (
        "import sys;sys.path.insert(0,%r);import inimerse;"
        "print(inimerse.version(), inimerse.sha256_file(%r), inimerse.parse_count(open(%r).read()))"
        % (str(build_dir / "bridge"), str(fixture), str(fixture))
    )
    py = run([sys.executable, "-c", pyprobe], cwd=str(build_dir))
    check("the CPython extension answers the same question", py.returncode == 0, (py.stderr or "")[-500:])
    if not FAILURES:
        note("python says: %s" % py.stdout.strip())
        check("Java and Python print the identical line", py.stdout.strip() == answers,
              "python=%r java=%r" % (py.stdout.strip(), answers))

    # --- the error path: an engine failure must cross as InimerseException ---
    missing = build_dir / "bridge" / "definitely-not-here"
    err = run(
        [str(java), "-Djava.library.path=%s" % (build_dir / "bridge"), "-cp", str(jar),
         "InimerseBridgeMain", str(missing), str(fixture)]
    )
    blob = (err.stderr or "") + (err.stdout or "")
    check("a failing engine call exits non-zero", err.returncode != 0, blob[-400:])
    check("the engine error crosses as InimerseException with its code",
          "InimerseException" in blob and "failed (code 3)" in blob, blob[-400:])

    print("xlang_java_bridge: %d check(s), %d failure(s)" % (CHECKS, len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
