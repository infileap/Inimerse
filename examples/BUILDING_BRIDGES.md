# Building the cross-language bridges

`inimerse` ships two bindings that are generated from one IDL — a CPython
extension exposing `PyInit_inimerse()` (`import inimerse`) and a JNI facade
(`InimerseBridge`) driving the same engine functions in a JVM. Both are real
build products: the extension is packaged into a real wheel and the Java side
becomes a real jar plus a real `libinimerse_bridge.so`.

Building them needs two things this checkout does not vendor:

* `Python.h` and `<multiarch>/python3.14/pyconfig.h` (`python3.14-dev`,
  `libpython3.14-dev`);
* a JDK with `javac`, `jar` and `jni.h` (`openjdk-17-jdk-headless`).

Neither is required to build or run the engine. When they are absent CMake
prints a warning and the two `xlang_*_bridge` regressions report themselves as
**skipped** — and `tools/gate.sh` fails the ctest stage on any skip, so a gate
can never go green while claiming evidence it did not collect.

## 1. Unpack a toolchain prefix (no root required)

The build user on the reference host has no passwordless `sudo`, so the Debian
packages are downloaded and unpacked into a prefix **outside the repository**.
Nothing from the prefix is ever committed.

```sh
PREFIX="${HOME}/.local/xlang-toolchain"
mkdir -p "${PREFIX}/debs"
cd "${PREFIX}/debs"

apt-get download \
    python3.14-dev libpython3.14-dev \
    openjdk-17-jdk-headless openjdk-17-jre-headless

for deb in ./*.deb; do dpkg-deb -x "${deb}" "${PREFIX}"; done
```

At the time of writing this fetched 126 MB (410 MB unpacked):
`libpython3.14-dev 3.14.4-1ubuntu0.2`, `python3.14-dev 3.14.4-1ubuntu0.2`,
`openjdk-17-jdk-headless` / `openjdk-17-jre-headless 17.0.20.1+1~26.04`.

The deb must match the interpreter the extension will be imported by. Check
that before going further — if the host python and the headers are different
minor versions, the extension will not import and that is a fact to report, not
to paper over by switching interpreters:

```sh
python3 -VV
ls "${PREFIX}/usr/include/python3.14/Python.h"
```

## 2. Repair the JDK's absolute symlinks

`dpkg-deb -x` preserves symlinks as they are packaged, and the OpenJDK
configuration files are packaged as **absolute** links into `/etc`:

```console
$ "${PREFIX}/usr/lib/jvm/java-17-openjdk-amd64/bin/javac" -version
Exception in thread "main" java.lang.InternalError: Error loading java.security file
        at java.base/java.security.Security.initialize(Security.java:106)
```

`/etc/java-17-openjdk/` does not exist on a host that never installed the
package, so every such link dangles. Rewrite each one into a prefix-relative
link whenever its target is actually present inside the prefix:

```sh
PREFIX="${HOME}/.local/xlang-toolchain"
find "${PREFIX}" -type l | while IFS= read -r link; do
    target="$(readlink "${link}")"
    case "${target}" in
        /*) ;;
        *) continue ;;                                # already relative
    esac
    resolved="${PREFIX}${target}"
    [ -e "${resolved}" ] || { echo "dangling (left alone): ${link}"; continue; }
    ln -sfn "$(realpath --relative-to="$(dirname "${link}")" "${resolved}")" "${link}"
done
```

On the reference host this rewrote 24 links, after which the tools report
themselves correctly:

```console
$ "${PREFIX}/usr/lib/jvm/java-17-openjdk-amd64/bin/javac" -version
javac 17.0.20.1
$ "${PREFIX}/usr/lib/jvm/java-17-openjdk-amd64/bin/java" -version
openjdk version "17.0.20.1" 2026-08-18
```

One dangling link is expected and deliberately left alone:
`lib/security/cacerts -> /etc/ssl/certs/java/cacerts`. It is the JVM trust
store and nothing here performs TLS.

## 3. Point the build at the prefix

The prefix is located by one cache variable, defaulting to
`${HOME}/.local/xlang-toolchain`. The repository never hard-codes it:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DXLANG_TOOLCHAIN="${HOME}/.local/xlang-toolchain"
cmake --build build -j"$(nproc)"
```

`XLANG_TOOLCHAIN` in the environment does the same thing. The layout the build
expects is exactly the Debian one that steps 1–2 produced:

| what | path inside the prefix |
| --- | --- |
| CPython headers | `usr/include/python3.14/Python.h` |
| multiarch `pyconfig.h` | `usr/include/x86_64-linux-gnu/python3.14/pyconfig.h` |
| JNI headers | `usr/lib/jvm/java-17-openjdk-amd64/include/jni.h` |
| JDK tools | `usr/lib/jvm/java-17-openjdk-amd64/bin/{java,javac,jar}` |

## 4. What the build produces

Everything lands in `build/bridge/`:

| artifact | source |
| --- | --- |
| `inimerse<EXT_SUFFIX>` | `src/bridge/inimerse_extension.c` + `src/bridge/bridge_abi.c` |
| `libinimerse_bridge.so` | `src/bridge/bridge_jni.c` + `src/bridge/bridge_abi.c` |
| `InimerseBridge.jar` | `src/bridge/InimerseBridgeMain.java` + the generated facade |
| `inimerse-<version>-<tags>.whl` | assembled by the Python regression from the extension above |

The C header and the Java facade are not checked in; both are emitted by
`tools/bindgen.py` into `build/bindings/` from the single IDL in
`src/bridge/inimerse_bridge.def`. Do not edit the generated files — edit the
IDL.

## 5. Running the bridges by hand

Both regressions accept the same arguments (build directory, toolchain prefix,
optional repository root) and print one `ok`/`FAIL` line per check:

```sh
python3 tools/xlang_python_bridge.test.py build "${HOME}/.local/xlang-toolchain"
python3 tools/xlang_java_bridge.test.py   build "${HOME}/.local/xlang-toolchain"
```

The Python one assembles a wheel, creates a virtual environment, installs the
wheel into it and imports the module from there. The Java one runs the jar on a
real JVM and then demands that Java and Python print *byte-identical* answers
for the same fixture, including the engine's failure path (an engine error must
cross as `InimerseException` carrying the engine's code).

A skip (exit 77) is reported by ctest as `***Skipped`; `tools/gate.sh` counts
those and refuses to pass while any exist.

## 6. Things worth knowing before you rely on this

* **`python3 -m venv` needs `ensurepip`**, which lives in `python3.14-venv` and
  is not installed on the reference host. `python3 -m venv --without-pip`
  still creates a fully isolated interpreter, and the host `pip` installs the
  wheel into that environment's `site-packages` (`pip install --target`). The
  regression does that, and asserts that the imported module really resolves
  inside the venv and not inside the build tree.
* **`python3 -m build` is not available** either, so the wheel is assembled by
  hand with `zipfile`. A wheel is a zip with a `dist-info` directory; the module
  inside it is the exact file the compiler produced.
* **The engine's parser is fatal on bad input.**
  `src/parser/parser.c` calls `exit(1)` when a parse fails, so
  `inimerse.parse_count()` / `InimerseBridge.parse_count()` given malformed
  source terminates the host process (the Python interpreter or the JVM). The
  bridge cannot catch that — it is the engine's existing error policy, not
  something the bindings add. Only hand the bridge source you know parses.
  See `src/bridge/bridge_fixture.im` for a fixture that does, and note its
  header for a construct the parser rejects despite looking reasonable.
* **`inim_load_text()` is defined in `src/main.c`** while the rest of the
  engine calls it, so both bridge libraries compile that one file with the CLI
  entry point renamed. This is the same source, not a copy; see the comment in
  `CMakeLists.txt`.
