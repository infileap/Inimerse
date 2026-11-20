"""Scanner tools regression (roadmap §3.1/§3.3): cpp_scan identifies
extern "C" functions/structs/enums/macros and emits bindgen-compatible IDL
for scalar functions; python_scan identifies typed functions and reports
dynamic constructs. The generated IDL must pass tools/bindgen.py."""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

CPP_HEADER = """\
#ifndef NATIVE_H
#define NATIVE_H
#include <stdint.h>
#define NATIVE_VERSION 42
typedef struct point { int32_t x; int32_t y; } point;
extern "C" {
int32_t native_add(int32_t a, int32_t b);
void native_log(const char* msg);
int32_t native_cb(void (*cb)(int), int32_t v);
}
std::string cpp_only();
#endif
"""

PY_MODULE = """\
def add(a: int, b: int) -> int:
    return a + b

def untyped(a, b):
    return a + b

@staticmethod
def decorated(x: int) -> int:
    return x

async def fetch(url: str):
    ...

def varargs(*args, **kwargs):
    ...

class Greeter:
    def greet(self, name: str) -> str:
        return "hi"
"""


def main():
    with tempfile.TemporaryDirectory(prefix="inimerse-scan-") as td:
        root = Path(td)
        (root / "native.h").write_text(CPP_HEADER, encoding="utf-8")
        (root / "pymod.py").write_text(PY_MODULE, encoding="utf-8")

        # cpp_scan: 2 convertible + warnings for callback/struct, IDL -> bindgen
        rc = subprocess.run([sys.executable, str(HERE / "cpp_scan.py"), "native.h",
                             "--emit-idl", "native.def", "-o", "cpp.json"],
                            cwd=root, capture_output=True)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode())
        idl = (root / "native.def").read_text(encoding="utf-8")
        if not ("fn native_add(a: i32, b: i32) -> i32" in idl): raise AssertionError(idl)
        if not ("native_cb" not in idl and "cpp_only" not in idl): raise AssertionError(idl)  # rejected, not wrapped
        import json
        inv = json.loads((root / "cpp.json").read_text(encoding="utf-8"))
        if not (any("native_cb" in w for w in inv["warnings"])): raise AssertionError(inv["warnings"])
        if not (any("struct point" in w for w in inv["warnings"])): raise AssertionError
        if not ([s["name"] for s in inv["inventories"][0]["structs"]] == ["point"]): raise AssertionError(inv)
        if not ([m["name"] for m in inv["inventories"][0]["macros"]] == ["NATIVE_H", "NATIVE_VERSION"]): raise AssertionError(inv)
        rc = subprocess.run([sys.executable, str(HERE / "bindgen.py"), "native.def",
                             "--language", "c", "--out", "wrap.h"], cwd=root, capture_output=True)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode())

        # python_scan: typed fn in IDL, dynamic parts rejected with reasons
        rc = subprocess.run([sys.executable, str(HERE / "python_scan.py"), "pymod.py",
                             "--emit-idl", "py.def", "-o", "py.json"],
                            cwd=root, capture_output=True)
        if not (rc.returncode == 0): raise AssertionError(rc.stderr.decode())
        idl = (root / "py.def").read_text(encoding="utf-8")
        if not ("fn add(a: i32, b: i32) -> i32" in idl): raise AssertionError(idl)
        if not ("untyped" not in idl and "decorated" not in idl and "fetch" not in idl): raise AssertionError(idl)
        inv = json.loads((root / "py.json").read_text(encoding="utf-8"))
        if not (any("untyped" in w for w in inv["warnings"])): raise AssertionError
        if not (any("decorator" in w for w in inv["warnings"])): raise AssertionError
        if not (any("async" in w for w in inv["warnings"])): raise AssertionError
        if not ([c["name"] for c in inv["inventories"][0]["classes"]] == ["Greeter"]): raise AssertionError(inv)
    print("scan tools: ok")


if __name__ == "__main__":
    main()
