#!/usr/bin/env python3
"""Does size()'s ARRAY branch answer the same thing on both platform copies?

`size(A)` for an array is answered by two different functions:

    src/runtime/runtime.c:147         static int builtin_size(VM *vm)        (WIN32)
    src/runtime/runtime_posix.c:30    static int posix_core_size(VM *vm)    (POSIX)

CMakeLists.txt compiles exactly one of those two files, so neither platform can
see the other's answer, and `builtin_count`'s comment in runtime.c claims the
two copies are mirrors "including the bounds-checked array/dictionary
spellings, so the two platform copies cannot answer differently for an
out-of-range slot".

This probe asks that claim about the ARRAY branch, and it asks it the only way
that is answerable on a machine that builds one of the two files: it extracts
BOTH function bodies verbatim out of their source files, compiles them side by
side into one program, and feeds the same forged `Value` to each.  Nothing here
re-types the branch under test -- a copy in this file would be a copy that can
drift from the file it claims to judge.

Why the value must be forged: a `VAL_ARRAY` handle is a pool index PLUS ONE
(`.ival = aidx + 1`), every producer tests `aidx < 0` before building one, and
`arrayCount` never decreases -- so while all three of those hold, no program
written in the language can reach this branch with `ival - 1` outside
[0, arrayCount).  The branch is therefore only reachable out of range from C,
which is where this probe stands.

The positive half is asserted too: a real 3-element array must answer `int 3`
on BOTH sides.  A "fix" that simply always answers nil would pass a
one-sided assertion and fails here.

Usage:  size_array_branch_probe.py [--build-dir DIR] [--keep]
Exit:   0 = the two copies agree on every case
        1 = they disagree (the disagreement is printed, case by case)
        2 = cannot measure (no engine objects in the build dir)
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

WIN_SRC = "src/runtime/runtime.c"
WIN_SIG = "static int builtin_size(VM *vm) {"
POSIX_SRC = "src/runtime/runtime_posix.c"
POSIX_SIG = "static int posix_core_size(VM *vm) {"
# The same-shaped second member.  Measured and printed, NOT part of the verdict:
# fixing one instance on purpose leaves n-1 behind, and a probe that silently
# judged both would hide that.
WIN_LEN_SIG = "static int builtin_len(VM *vm) {"
POSIX_LEN_SIG = "static int posix_core_len(VM *vm) {"


def extract_function(rel: str, sig: str):
    """Return (text, first_line, last_line) for the function starting at `sig`.

    Brace matching, not a line range: a line range would silently truncate the
    body the first time anything above it is edited.
    """
    text = (ROOT / rel).read_text(encoding="utf-8")
    start = text.find(sig)
    if start < 0:
        raise SystemExit(f"size_array_branch_probe: cannot find {sig!r} in {rel}")
    depth = 0
    i = text.index("{", start)
    while True:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    first = text.count("\n", 0, start) + 1
    last = text.count("\n", 0, i) + 1
    return text[start:i + 1], first, last


HARNESS = r"""
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * The functions under test, verbatim out of their source files.
 * ------------------------------------------------------------------ */
@@WIN32@@

@@POSIX@@

@@WIN32LEN@@

@@POSIXLEN@@

/* ------------------------------------------------------------------ */

static const char *tname(int t) {
    switch (t) {
        case VAL_INT:      return "int";
        case VAL_NIL:      return "nil";
        case VAL_FLOAT:    return "float";
        case VAL_STRING:   return "string";
        case VAL_BOOL:     return "bool";
        case VAL_ARRAY:    return "array";
        case VAL_DICT:     return "dict";
        case VAL_SET:      return "set";
        case VAL_FUNCTION: return "function";
        default:           return "object";
    }
}

static VM *g_vm;
static VmThread g_t;

/* Push `forged` as the sole argument and let `fn` answer.  `sp` is reset every
 * time so one case cannot leave state behind for the next. */
static void ask(const char *side, const char *label, int (*fn)(VM *), Value forged) {
    memset(&g_t, 0, sizeof g_t);
    g_t.vm = g_vm;
    g_t.sp = -1;
    vm_set_cur_thread(&g_t);
    g_t.stack[0] = forged;
    g_t.sp = 0;
    fn(g_vm);
    Value *r = &g_t.stack[g_t.sp];
    printf("%s %s -> %s", side, label, tname(r->type));
    if (r->type == VAL_INT) printf(" %lld", (long long)r->ival);
    printf("\n");
}

static Value array_handle(int ival) {
    Value v; memset(&v, 0, sizeof v);
    v.type = VAL_ARRAY; v.ival = ival; v.sval = NULL;
    return v;
}

int main(void) {
    g_vm = calloc(1, sizeof(VM));
    if (!g_vm) { printf("size_array_branch_probe: cannot allocate a VM\n"); return 1; }

    /* The positive half: a real array, built through the real pool. */
    int aidx = vm_array_new(g_vm);
    if (aidx < 0) { printf("size_array_branch_probe: vm_array_new refused\n"); return 1; }
    for (int i = 0; i < 3; i++) {
        Value x; memset(&x, 0, sizeof x);
        x.type = VAL_INT; x.ival = i;
        vm_array_push(g_vm, aidx, &x);
    }
    int count = g_vm->arrayCount;

    struct { const char *label; int ival; } cases[] = {
        { "valid-3-element-array", aidx + 1 },
        { "handle-0-idx--1",       0 },
        { "idx--1-via-negative",   -1 },
        { "idx==arrayCount",       count + 1 },
        { "idx==arrayCount+4095",  count + 4096 },
    };
    int n = (int)(sizeof cases / sizeof cases[0]);

    printf("size_array_branch_probe: arrayCount=%d real-array-handle=%d\n", count, aidx + 1);
    printf("# size -- the branch this probe judges\n");
    for (int i = 0; i < n; i++) {
        Value v = array_handle(cases[i].ival);
        ask("win32", cases[i].label, builtin_size, v);
        ask("posix", cases[i].label, posix_core_size, v);
    }
    printf("# len -- same-shaped second member, measured but OUT OF SCOPE for the verdict\n");
    for (int i = 0; i < n; i++) {
        Value v = array_handle(cases[i].ival);
        ask("win32/len", cases[i].label, builtin_len, v);
        ask("posix/len", cases[i].label, posix_core_len, v);
    }
    printf("size_array_branch_probe: end\n");
    return 0;
}
"""


def engine_objects(build_dir: Path):
    objs = sorted((build_dir / "CMakeFiles" / "inimerse_engine.dir").rglob("*.o"))
    platform = build_dir / "libinimerse_platform.a"
    return objs, platform


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--keep", action="store_true", help="keep the generated harness")
    args = ap.parse_args()

    win32, w0, w1 = extract_function(WIN_SRC, WIN_SIG)
    posix, p0, p1 = extract_function(POSIX_SRC, POSIX_SIG)
    winlen, wl0, wl1 = extract_function(WIN_SRC, WIN_LEN_SIG)
    posixlen, pl0, pl1 = extract_function(POSIX_SRC, POSIX_LEN_SIG)
    print(f"size_array_branch_probe: {WIN_SRC}:{w0}-{w1} and {POSIX_SRC}:{p0}-{p1} extracted verbatim")
    print(f"size_array_branch_probe: {WIN_SRC}:{wl0}-{wl1} and {POSIX_SRC}:{pl0}-{pl1} extracted verbatim (len, reported)")

    build_dir = (ROOT / args.build_dir).resolve()
    objs, platform = engine_objects(build_dir)
    if not objs or not platform.exists():
        print(f"size_array_branch_probe: cannot measure -- no engine objects under {build_dir}",
              file=sys.stderr)
        return 2

    work = Path(tempfile.mkdtemp(prefix="size-array-branch-"))
    try:
        src = work / "harness.c"
        src.write_text(HARNESS.replace("@@WIN32LEN@@", winlen)
                              .replace("@@POSIXLEN@@", posixlen)
                              .replace("@@WIN32@@", win32)
                              .replace("@@POSIX@@", posix),
                       encoding="utf-8")
        exe = work / "harness"
        cmd = ["gcc", "-O1", "-w",
               "-I", str(ROOT / "src/vm"), "-I", str(ROOT / "src"),
               "-I", str(ROOT / "src/compiler"), "-I", str(ROOT / "src/common"),
               "-I", str(ROOT / "src/types"), "-I", str(ROOT / "src/platform"),
               "-I", str(ROOT / "src/runtime"),
               str(src), *[str(o) for o in objs], str(platform),
               "-lm", "-lpthread", "-ldl", "-o", str(exe)]
        cp = subprocess.run(cmd, capture_output=True, text=True)
        if cp.returncode != 0:
            print("size_array_branch_probe: harness did not compile", file=sys.stderr)
            print(cp.stderr[-4000:], file=sys.stderr)
            return 2
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        out = run.stdout
        print(out, end="")
        if run.returncode != 0:
            print(f"size_array_branch_probe: harness exited {run.returncode}", file=sys.stderr)
            return 2

        answers = {}
        for line in out.splitlines():
            m = re.match(r"^(win32|posix)(/len)? (\S+) -> (\w+)(?: (-?\d+))?$", line)
            if m:
                side, lenmark, label, ty, iv = m.groups()
                answers[(label, side + (lenmark or ""))] = (ty, iv)
        labels = sorted({k[0] for k in answers})

        bad = []
        for label in labels:
            w = answers.get((label, "win32"))
            p = answers.get((label, "posix"))
            if w is None or p is None:
                bad.append(f"{label}: one side did not answer")
                continue
            if w != p:
                bad.append(f"{label}: win32 answered {w[0]}{' ' + w[1] if w[1] else ''}"
                           f", posix answered {p[0]}{' ' + p[1] if p[1] else ''}")
        # The positive half, named separately so it cannot be satisfied by a
        # branch that refuses everything.
        if answers.get(("valid-3-element-array", "win32")) != ("int", "3"):
            bad.append("valid-3-element-array: win32 did not answer int 3")
        if answers.get(("valid-3-element-array", "posix")) != ("int", "3"):
            bad.append("valid-3-element-array: posix did not answer int 3")

        if bad:
            print(f"size_array_branch_probe: FAIL -- the two copies disagree on {len(bad)} case(s)")
            for b in bad:
                print(f"    {b}")
            return 1
        print(f"size_array_branch_probe: ok -- {len(labels)} case(s), both copies agree on every one")
        wl = answers.get(("idx==arrayCount", "win32/len"))
        pl = answers.get(("idx==arrayCount", "posix/len"))
        print("size_array_branch_probe: NOTE len on an out-of-range handle: "
              f"win32 {wl[0]}{' ' + wl[1] if wl and wl[1] else ''}, "
              f"posix {pl[0]}{' ' + pl[1] if pl and pl[1] else ''} "
              "-- both unguarded, both answer 0, so they agree with each other and "
              "disagree with size/count (nil).  Not fixed here; reported.")
        return 0
    finally:
        if args.keep:
            print(f"size_array_branch_probe: harness kept at {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
