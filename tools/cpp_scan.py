#!/usr/bin/env python3
"""C/C++ header scanner (v0.5 roadmap §3.1).

Identifies `extern "C"` functions, structs, enums and #define macros in
headers and emits a JSON inventory.  With `--emit-idl`, simple integer /
floating / bool / string functions are converted into a bindgen-compatible
interface definition (tools/bindgen.py) so C code can be wrapped and called
from Inimerse automatically; anything not convertible is reported as a
manual adaptation point instead of being silently wrapped.

Usage:
    python3 tools/cpp_scan.py <headers or dirs...> [--emit-idl out.def] [-o inventory.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

# C scalar types -> bindgen types
CTYPE_MAP = {
    "int": "i32", "unsigned int": "u32", "long": "i64", "unsigned long": "u64",
    "long long": "i64", "unsigned long long": "u64",
    "short": "i16", "unsigned short": "u16",
    "char": "i8", "unsigned char": "u8", "signed char": "i8",
    "int8_t": "i8", "int16_t": "i16", "int32_t": "i32", "int64_t": "i64",
    "uint8_t": "u8", "uint16_t": "u16", "uint32_t": "u32", "uint64_t": "u64",
    "size_t": "u64", "ssize_t": "i64", "bool": "bool", "_Bool": "bool",
    "float": "f32", "double": "f64",
}

STR_TYPES = {"const char*", "char*", "const char *", "char *"}

FUNC_RE = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?(?P<ret>[\w\s\*]+?)\s+(?P<name>\w+)\s*\((?P<params>[^;{}]*)\)\s*;", re.M)
STRUCT_RE = re.compile(r"\b(?:typedef\s+)?struct\s*(?:\w+\s*)?\{([^{}]*)\}\s*(?P<name>\w+)\s*;")
ENUM_RE = re.compile(r"\b(?:typedef\s+)?enum\s*(?:\w+\s*)?\{([^{}]*)\}\s*(?P<name>\w+)\s*;")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(?P<name>\w+)(?:[ \t]+(?P<value>[^\n]+))?$", re.M)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def norm_type(t):
    t = t.strip()
    ptr = t.endswith("*")
    base = t.rstrip("*").strip()
    if base in STR_TYPES or t in STR_TYPES:
        return "string"
    mapped = CTYPE_MAP.get(base)
    if mapped and ptr:
        return None  # pointer to scalar: manual adaptation
    return mapped


def parse_params(params_src, where, warnings):
    params = []
    params_src = params_src.strip()
    if params_src in ("", "void"):
        return params
    for piece in params_src.split(","):
        piece = piece.strip()
        if not piece:
            continue
        if "..." in piece:
            warnings.append(f"{where}: variadic parameter is not convertible")
            return None
        m = re.match(r"^(?P<type>[\w\s\*]+?)\s*(?P<name>\w+)?$", piece)
        if not m:
            warnings.append(f"{where}: unparseable parameter '{piece}'")
            return None
        btype = norm_type(m.group("type"))
        if btype is None:
            warnings.append(f"{where}: parameter '{piece}' needs manual adaptation "
                            f"(pointers/structs are not auto-wrapped)")
            return None
        name = m.group("name") or f"arg{len(params)}"
        params.append((name, btype))
    return params


def scan_file(path):
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    functions, structs, enums, macros = [], [], [], []
    warnings = []

    # only functions declared inside extern "C" blocks are C-ABI compatible
    extern_c_spans = [m.start()
                      for m in re.finditer(r'extern\s*"C"\s*\{', text)]
    def in_extern_c(pos):
        return any(s <= pos for s in extern_c_spans)

    # crude brace matching for extern "C" ranges
    ranges = []
    for s0 in extern_c_spans:
        depth, i = 1, s0
        while i < len(text) and depth:
            if text[i] == "{": depth += 1
            elif text[i] == "}": depth -= 1
            i += 1
        ranges.append((s0, i))

    def in_range(pos):
        return any(s <= pos < e for s, e in ranges)

    for m in FUNC_RE.finditer(text):
        ret = norm_type(m.group("ret"))
        if ret is None:
            if m.group("ret").strip() not in ("void",):
                warnings.append(f"{path}:{m.start()}: return type '{m.group('ret').strip()}' "
                                f"of {m.group('name')} needs manual adaptation")
                continue
            ret = None
        if not in_range(m.start()) and not in_extern_c(m.start()):
            warnings.append(f"{path}: {m.group('name')} is outside extern \"C\" (C++ ABI)")
            continue
        params = parse_params(m.group("params"), f"{path}:{m.group('name')}", warnings)
        if params is None:
            continue
        functions.append({"name": m.group("name"), "ret": ret, "params": params,
                          "line": text[:m.start()].count("\n") + 1})

    for m in STRUCT_RE.finditer(text):
        structs.append({"name": m.group("name"),
                        "line": text[:m.start()].count("\n") + 1})
        warnings.append(f"{path}: struct {m.group('name')} is passed by pointer in C ABI; "
                        f"wrap field-by-field or mark manual")
    for m in ENUM_RE.finditer(text):
        enums.append({"name": m.group("name"),
                      "line": text[:m.start()].count("\n") + 1})
    for m in DEFINE_RE.finditer(text):
        value = (m.group("value") or "").strip()
        macros.append({"name": m.group("name"), "value": value[:80],
                       "line": text[:m.start()].count("\n") + 1})

    return {"file": str(path), "functions": functions, "structs": structs,
            "enums": enums, "macros": macros, "warnings": warnings}


def emit_idl(inventories, out_path):
    """Convert simple scalar functions into a bindgen interface definition."""
    iface = "c_native"
    lines = [f"// Generated by tools/cpp_scan.py --emit-idl (roadmap §3.1);",
             f"// wrap with: python3 tools/bindgen.py {Path(out_path).name} --language c|java|python",
             f"interface {iface} {{"]
    count = 0
    skipped = []
    for inv in inventories:
        for fn in inv["functions"]:
            ret = f" -> {fn['ret']}" if fn["ret"] else ""
            if any(t == "string" for _, t in fn["params"]) and fn["ret"] == "string":
                skipped.append(f"{fn['name']}: string returns need buffer conventions")
                continue
            params = ", ".join(f"{n}: {t}" for n, t in fn["params"])
            lines.append(f"    fn {fn['name']}({params}){ret}")
            count += 1
    lines.append("}")
    Path(out_path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return count, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("sources", nargs="+", help="headers or directories")
    ap.add_argument("--emit-idl", help="write a bindgen .def for scalar functions")
    ap.add_argument("-o", "--output", help="write JSON inventory here (default: stdout)")
    args = ap.parse_args()

    files = []
    for s in args.sources:
        p = Path(s)
        if p.is_dir():
            files += [f for f in sorted(p.rglob("*"))
                      if f.suffix in (".h", ".hpp", ".hh")]
        elif p.is_file():
            files.append(p)
    if not files:
        sys.exit("error: no headers found")

    inventories, all_warnings = [], []
    for f in files:
        inv = scan_file(f)
        inventories.append(inv)
        all_warnings += inv["warnings"]

    payload = {"inventories": inventories, "warnings": all_warnings}
    report = json.dumps(payload, indent=2)
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"cpp_scan: {len(files)} headers, {sum(len(i['functions']) for i in inventories)} functions, "
              f"{len(all_warnings)} warnings -> {args.output}")
    else:
        print(report)
    if args.emit_idl:
        count, skipped = emit_idl(inventories, args.emit_idl)
        print(f"cpp_scan: {count} functions -> {args.emit_idl}")
        for s in skipped:
            print(f"  skipped: {s}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
