"""Migration-report regression (roadmap §3.1-§3.3): tools/migrate_report.py.

This harness exists because nothing ran that tool.  `docs/REQUIREMENTS_ANALYSIS.md`
row 11 and `docs/STATUS.md` §4 listed `tools/migrate_report.py` beside
`bindgen_regression` / `scan_tools_regression` in an "evidence" column, and those
two CTests drive `tools/bindgen.test.py` and `tools/scan_tools.test.py` -- neither
of which mentions the migration report.  The tool worked; the claim that anything
checked it did not.  See docs/AUDIT.md §1.59.

What is asserted, and why each one:

  1. Every rule in the tool's two tables fires on an input built to trigger it.
     A rule that has been silently dropped from PYTHON_RULES / C_RULES is a
     capability that reads as present in the docs and is absent in the code.
  2. The header's `Scanned:` and `Manual adaptation points:` counts equal what
     the body actually contains.  This is the denominator: a report that prints
     `Manual adaptation points: 0` over a full table, or `Scanned: 1 files` after
     reading none, is the same defect this repository keeps meeting -- a
     conclusion asserted without the count that would contradict it.  The
     negative control below pins the other direction.
  3. A clean input reports `0` findings and no table rows, exit 0.  Without this
     a tool that reported everything as a finding would pass (1).
  4. A run with no readable source exits non-zero with a named error rather than
     emitting an empty report that looks like a clean scan.

Usage (CTest passes no engine; this is a pure-Python tool):
    python3 tools/migrate_report.test.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "migrate_report.py"

C_FIXTURE = """\
#include <stdio.h>
#include <stdint.h>

static int depth = 0;

int parse(const char *s) {
    if (!s) goto fail;
    if (setjmp(env)) return -1;
    void *buf = alloca(64);
    pthread_create(&t, NULL, worker, buf);
    int (*fp)(int) = &handler;
    return fp(depth);
fail:
    return 0;
}
"""

PY_FIXTURE = """\
import os

TOTAL = 0

class Meta(type, metaclass=Meta2):
    def __getattr__(self, name):
        return None

def gen():
    yield 1

async def fetch(url):
    await url

def varargs(*args, **kwargs):
    return args

@staticmethod
def decorated(x):
    global TOTAL
    return eval("x")

double = lambda v: v * 2
"""

# One entry per rule name in migrate_report.py's PYTHON_RULES / C_RULES.
EXPECTED_C = ["goto", "setjmp", "alloca", "threads", "func-ptr"]
EXPECTED_PY = ["dynamic-attr", "metaclass", "yield", "async", "varargs",
               "decorator", "global-stmt", "exec-eval", "lambda"]

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL: %s" % what, file=sys.stderr)


def run(args, cwd=None):
    # encoding= is not optional: on Windows the default text=True decoder is
    # the locale codec (gbk), and the report is UTF-8.  Without it subprocess
    # raises UnicodeDecodeError in its reader thread and stdout comes back as
    # None, which then fails as a TypeError instead of a named assertion.
    p = subprocess.run([sys.executable, str(TOOL)] + args,
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=cwd)
    return p.returncode, p.stdout, p.stderr


def table_rows(report, heading):
    """Rows of the markdown table under `heading` (skips the `|---|` rule)."""
    lines = report.splitlines()
    try:
        start = lines.index(heading)
    except ValueError:
        return None
    rows = []
    for line in lines[start + 1:]:
        if line.startswith("## "):
            break
        if line.startswith("|") and not re.match(r"^\|[\s\-|]+\|$", line):
            rows.append(line)
    return rows[1:] if rows else rows  # drop the column header


def header_count(report, label):
    m = re.search(r"^- %s: (\d+)" % re.escape(label), report, re.M)
    return int(m.group(1)) if m else None


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    c_file = td / "fixture.c"
    py_file = td / "fixture.py"
    clean = td / "clean.c"
    c_file.write_text(C_FIXTURE, encoding="utf-8")
    py_file.write_text(PY_FIXTURE, encoding="utf-8")
    clean.write_text("int main(void) { return 0; }\n", encoding="utf-8")

    # ---- 1 + 2: every rule fires, and the counts match the body -------------
    rc, out, err = run([str(c_file), str(py_file)])
    check(rc == 0, "scan of two files exited %d (stderr: %s)" % (rc, err.strip()))
    check("# Migration Report (auto-generated)" in out,
          "report is missing its title line")
    check(header_count(out, "Scanned") == 2,
          "Scanned: header says %r, two files were passed"
          % header_count(out, "Scanned"))

    findings = table_rows(out, "## 非可转换语法（手动适配点）")
    check(findings is not None, "report has no findings table")
    findings = findings or []
    check(header_count(out, "Manual adaptation points") == len(findings),
          "Manual adaptation points: says %r but the table has %d row(s)"
          % (header_count(out, "Manual adaptation points"), len(findings)))

    fired = {r.split("|")[2].strip() for r in findings if r.count("|") >= 3}
    for rule in EXPECTED_C + EXPECTED_PY:
        check(rule in fired, "rule %r did not fire on the fixture built for it" % rule)

    deps = table_rows(out, "## 依赖")
    check(deps is not None and len(deps) == 3,
          "expected 3 dependency rows (stdio.h, stdint.h, os), got %r"
          % (len(deps) if deps is not None else None))
    check(header_count(out, "Dependencies") == (len(deps) if deps else -1),
          "Dependencies: header disagrees with the dependency table")

    # ---- 3: the negative control -------------------------------------------
    rc, out, err = run([str(clean)])
    check(rc == 0, "clean scan exited %d (stderr: %s)" % (rc, err.strip()))
    check(header_count(out, "Manual adaptation points") == 0,
          "a clean file reported %r adaptation points"
          % header_count(out, "Manual adaptation points"))
    clean_rows = table_rows(out, "## 非可转换语法（手动适配点）") or []
    check(clean_rows == [], "a clean file produced %d finding row(s)" % len(clean_rows))

    # ---- 4: no source found is a named failure, not an empty report --------
    empty = td / "empty"
    empty.mkdir()
    rc, out, err = run([str(empty)])
    check(rc != 0, "scanning a directory with no sources exited 0")
    check("no source files found" in (out + err),
          "no-source run did not name the reason (stdout=%r stderr=%r)" % (out, err))

    # ---- -o writes the same report it would have printed -------------------
    dest = td / "report.md"
    rc, out, err = run([str(c_file), "-o", str(dest)])
    check(rc == 0 and dest.is_file(), "-o did not write a report (rc=%d)" % rc)
    if dest.is_file():
        written = dest.read_text(encoding="utf-8")
        check(header_count(written, "Manual adaptation points")
              == len(table_rows(written, "## 非可转换语法（手动适配点）") or []),
              "-o output's header count disagrees with its own table")

if failures:
    print("migrate_report tests: %d failure(s)" % len(failures), file=sys.stderr)
    sys.exit(1)
print("migrate_report tests: ok")
