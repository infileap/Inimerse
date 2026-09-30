#!/usr/bin/env python3
"""Automatic migration report (v0.5 roadmap §3.1-§3.3).

Scans C/C++ or Python sources for constructs that cannot be converted to
Inimerse automatically and produces a markdown report listing:

  - non-convertible syntax with file:line (manual adaptation points)
  - dependencies (imports / includes)
  - runtime assumptions (GC, dynamic typing, threads, signal handlers)

Usage:
    python3 tools/migrate_report.py <files or dirs...> --target inimerse -o report.md
"""
import argparse
import re
import sys
from pathlib import Path

PYTHON_RULES = [
    ("yield", r"\byield\b", "generators have no Inimerse equivalent"),
    ("async", r"\basync\s+def\b|\bawait\b", "async/await must map to Inimerse Result propagation or threads"),
    ("lambda", r"\blambda\b", "lambdas map to Inimerse function values; closures need review"),
    ("metaclass", r"metaclass\s*=", "metaclasses are not convertible (dynamic typing)"),
    ("dynamic-attr", r"__getattr__|__setattr__", "dynamic attribute access is not convertible"),
    ("exec-eval", r"\bexec\(|\beval\(", "dynamic code execution is not convertible"),
    ("varargs", r"def\s+\w+\([^)]*(\*\*?\w+)", "*args/**kwargs need explicit signatures"),
    ("decorator", r"^\s*@(\w+)", "decorators must be inlined or modeled as higher-order functions"),
    ("global-stmt", r"^\s*global\s+\w+", "global declarations map to Inimerse globals; verify intent"),
]

C_RULES = [
    ("setjmp", r"\bsetjmp\b|\blongjmp\b", "non-local jumps are not convertible"),
    ("threads", r"\bpthread_create\b|CreateThread\b", "threads map to Inimerse thread blocks; review lifetime"),
    ("func-ptr", r"\(\s*\*\s*\w+\s*\)\s*\(", "function pointers map to function values; review call targets"),
    ("varargs", r"\w+\s*\(\s*[^)]*\.\.\.\s*\)", "variadic functions are not convertible"),
    ("goto", r"\bgoto\s+\w+", "goto maps to labels; verify control flow"),
    ("alloca", r"\balloca\s*\(", "stack allocation size must be static in Inimerse"),
    ("signal", r"\bsignal\s*\(|sigaction\b", "signal handlers are not convertible"),
    ("asm", r"\basm\b|__asm__", "inline assembly is not convertible"),
]

INCLUDE_RE = r"^\s*#\s*include\s*[<\"]([^\">]+)"
IMPORT_RE = r"^\s*(?:import|from)\s+([\w.]+)"


def scan_file(path, lang):
    text = path.read_text(encoding="utf-8", errors="replace")
    rules = PYTHON_RULES if lang == "python" else C_RULES
    findings, deps = [], []
    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("#!") or (
                lang == "c" and stripped.startswith("#") and "include" not in stripped):
            if not (lang == "c" and "include" in stripped):
                continue
        for name, pattern, note in rules:
            if re.search(pattern, line):
                findings.append((path, lineno, name, note, stripped[:100]))
        m = re.match(INCLUDE_RE if lang == "c" else IMPORT_RE, line)
        if m:
            deps.append((path, m.group(1)))
    return findings, deps


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("sources", nargs="+", help="files or directories to scan")
    ap.add_argument("--target", default="inimerse", choices=["inimerse"])
    ap.add_argument("-o", "--output", default=None, help="write markdown report here (default: stdout)")
    args = ap.parse_args()

    files = []
    for s in args.sources:
        p = Path(s)
        if p.is_dir():
            files += [f for f in sorted(p.rglob("*"))
                      if f.suffix in (".py", ".c", ".h", ".cpp", ".hpp") and not f.is_symlink()]
        elif p.is_file():
            files.append(p)
    if not files:
        sys.exit("error: no source files found")

    all_findings, all_deps = [], []
    for f in files:
        lang = "python" if f.suffix == ".py" else "c"
        try:
            fi, de = scan_file(f, lang)
        except OSError as e:
            print(f"warning: cannot read {f}: {e}", file=sys.stderr)
            continue
        all_findings += fi
        all_deps += de

    lines = [
        "# Migration Report (auto-generated)",
        "",
        f"- Target: `{args.target}`",
        f"- Scanned: {len(files)} files",
        f"- Manual adaptation points: {len(all_findings)}",
        f"- Dependencies: {len(all_deps)}",
        "",
        "## 非可转换语法（手动适配点）",
        "",
        "| 位置 | 类别 | 说明 | 代码 |",
        "|---|---|---|---|",
    ]
    for path, lineno, name, note, code in all_findings:
        code_esc = code.replace("|", "\\|")
        lines.append(f"| `{path}:{lineno}` | {name} | {note} | `{code_esc}` |")
    lines += ["", "## 依赖", "", "| 来源 | 依赖 |", "|---|---|"]
    for path, dep in all_deps:
        lines.append(f"| `{path}` | `{dep}` |")
    lines += [
        "",
        "## 运行时假设",
        "",
        "- Python 源依赖宿主 GC 与动态类型：转换后所有变量为 Inimerse 静态值；对象生命周期由引用计数/GC 覆盖，需回归测试。",
        "- C 源假设手动内存管理与平台 ABI：转换后由 Inimerse 运行时管理；native 模块须通过能力沙箱声明。",
        "- 线程假设（pthread/async）：须映射为 Inimerse thread/task 语义，超时与取消行为需逐点验证。",
        "",
    ]
    report = "\n".join(lines)
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"migration report: {args.output}")
    else:
        print(report)


if __name__ == "__main__":
    main()
