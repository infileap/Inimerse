"""bindgen regression: one IDL drives C/C++/Java/Python bindings, and type
errors are rejected at generation time (v0.5 roadmap §3.4).  Also asserts the
error-conversion convention in every generated language (roadmap §2.2/§3.2/
§3.3): C int (0=success), Java InimerseException, Python InimerseError."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
BINDGEN = HERE / "bindgen.py"

IDL = """\
interface inimerse_native {
    /// Adds two integers.
    fn add(a: i32, b: i32) -> i32
    /// Greets a caller.
    fn greet(name: string) -> string
    /// Error conversion probe.
    fn fail(code: i32) -> error
    /// Array math.
    fn stats(items: array<i32>) -> f64
}
"""


def run(*args):
    return subprocess.run([sys.executable, str(BINDGEN), *args], capture_output=True)


def main():
    with tempfile.TemporaryDirectory(prefix="inimerse-bindgen-") as td:
        root = Path(td)
        idl = root / "native.def"
        idl.write_text(IDL, encoding="utf-8")

        # --- three languages from one definition ---
        r = run(str(idl), "--language", "c", "--out", str(root / "c" / "inimerse_native.h"))
        if not (r.returncode == 0): raise AssertionError(r.stderr.decode())
        c_h = (root / "c" / "inimerse_native.h").read_text(encoding="utf-8")
        if not ("int inimerse_native_add(int32_t a, int32_t b, int32_t* out);" in c_h): raise AssertionError
        if not ("const char* name, size_t name_len" in c_h): raise AssertionError  # string = UTF-8 C string + length
        if not ("int inimerse_native_fail(int32_t code);" in c_h): raise AssertionError  # error -> int

        r = run(str(idl), "--language", "java", "--out", str(root / "java" / "InimerseNative.java"))
        if not (r.returncode == 0): raise AssertionError(r.stderr.decode())
        java = (root / "java" / "InimerseNative.java").read_text(encoding="utf-8")
        if not ("throw new InimerseException(rc," in java): raise AssertionError  # error -> Java exception
        if not ("int[] items" in java): raise AssertionError  # array<i32> -> int[]
        if not ("private static native void native_log" not in java): raise AssertionError

        r = run(str(idl), "--language", "python", "--out", str(root / "py" / "inimerse_native.py"))
        if not (r.returncode == 0): raise AssertionError(r.stderr.decode())
        py = (root / "py" / "inimerse_native.py").read_text(encoding="utf-8")
        if not ("raise InimerseError(rc," in py): raise AssertionError  # error -> Python exception
        if not ("class InimerseError(RuntimeError)" in py): raise AssertionError

        # C header must be compilable on its own
        if shutil.which("gcc"):
            r = subprocess.run(["gcc", "-fsyntax-only", "-x", "c", str(root / "c" / "inimerse_native.h")],
                               capture_output=True)
            if not (r.returncode == 0): raise AssertionError(r.stderr.decode())

        # --- generation-time type checking ---
        cases = [
            ("unknown type", "interface x {\n fn bad(a: widget) -> i32\n}\n", "unknown type"),
            ("duplicate fn", "interface x {\n fn f(a: i32)\n fn f(b: i32)\n}\n", "duplicate function"),
            ("default param", "interface x {\n fn f(a: i32 = 3)\n}\n", "default parameters"),
            ("bad syntax", "interface x {\n fn f( -> i32\n}\n", "expected 'fn name"),
            ("bad array elem", "interface x {\n fn f(a: array<list>)\n}\n", "unsupported array element"),
        ]
        for name, text, needle in cases:
            bad = root / f"bad_{name.replace(' ', '_')}.def"
            bad.write_text(text, encoding="utf-8")
            r = run(str(bad), "--language", "c", "--out", str(root / "bad.out"))
            if not (r.returncode != 0): raise AssertionError(f"{name}: expected rejection")
            if not (needle in r.stderr.decode()): raise AssertionError(f"{name}: {r.stderr.decode()}")
    print("bindgen: ok")


if __name__ == "__main__":
    main()
