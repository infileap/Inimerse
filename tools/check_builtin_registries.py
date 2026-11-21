#!/usr/bin/env python3
"""check_builtin_registries.py -- a name the compiler accepts and nothing answers.

Why this exists (docs/STATUS.md 10.258):

A builtin call is resolved twice.  The COMPILER holds a table of names it will
emit `OP_CALL_BUILTIN` for (src/compiler/compiler.c), so a program naming one of
them type-checks and produces bytecode.  The VM holds its own registry
(src/runtime/runtime.c, runtime_posix.c, and src/mod/*), populated with
`vm_register_builtin`.  `--no-mods` loads the core only.

If a name is in the first table and in neither the second nor any mod, then:

  * `compiler_compile` accepts it and writes bytecode -- measured: `build()`
    compiles to a `.inim` with exit 0;
  * the VM raises `unknown builtin function 'build'` at run time.

That is the shape §10.257 was about, one registry over: a name whose existence
one half of the system asserts and the other half denies.  The difference is
that this one is not a wrong reason attached to a real refusal -- it is a name
that never works anywhere, under any configuration.

The comparison is deliberately ONE-DIRECTIONAL.

`vm_register_builtin` names 335 things the compiler's table does not list, and
that is by design: the table is the subset that needs no mod declaration, and
the rest are resolved through the mod registry at run time.  A checker that
demanded the two sets be equal would report 335 problems about a working
system.  What it asks instead is the narrow question that has an answer:

    is there a name the compiler will emit, that no file in this tree registers?

Reading, not running: the check is about the two source tables.  Whether a
registered builtin behaves correctly is `tools/aot_mir.test.py`'s and the
runtime suites' subject, and whether the compiled route can lower one is
`tools/check_builtin_allowlist.py`'s.

Exit codes: 0 every compiler-visible name has a registration; 1 one does not;
2 a table could not be read.
"""

from __future__ import annotations

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The compiler's table: one `strdup` per name, in compiler_new().
COMPILER_TABLE = re.compile(
    r'comp->builtins\[comp->builtinCount\+\+\]\.name = strdup\("([A-Za-z_0-9]+)"\)'
)
# Every registration site.  `vm_register_builtin(vm, "name", fn)`.
REGISTRATION = re.compile(r'vm_register_builtin\(vm, "([A-Za-z_0-9]+)"')

# Where registrations are read from: the core runtime and every mod.  A file
# that registers under a different call shape is not seen -- stated here so an
# empty result is not mistaken for a complete census.
REGISTRATION_FILES = [
    "src/runtime/runtime.c",
    "src/runtime/runtime_posix.c",
]
REGISTRATION_GLOBS = ["src/mod/*.c"]


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def compiler_names(repo_root: str) -> set[str]:
    path = os.path.join(repo_root, "src/compiler/compiler.c")
    if not os.path.isfile(path):
        raise RuntimeError("no src/compiler/compiler.c")
    names = set(COMPILER_TABLE.findall(_read(path)))
    if not names:
        raise RuntimeError(
            "no `builtins[comp->builtinCount++].name = strdup(...)` rows in compiler.c -- "
            "the table moved and this reader is reading nothing, which is not the same "
            "as the table being empty"
        )
    return names


def registered_names(repo_root: str) -> tuple[set[str], list[str], set[str]]:
    """(everything registered, the files read, the CORE subset).

    The core subset matters: a name the core runtime registers and the compiler's
    table omits is the drift this file reports.  A name only a mod registers is
    the design -- the mods are loaded by name, not listed in the compiler.
    """
    import glob as _glob

    files = []
    core: set[str] = set()
    for rel in REGISTRATION_FILES:
        path = os.path.join(repo_root, rel)
        if os.path.isfile(path):
            files.append(path)
            core |= set(REGISTRATION.findall(_read(path)))
    for pattern in REGISTRATION_GLOBS:
        files.extend(sorted(_glob.glob(os.path.join(repo_root, pattern))))
    if not files:
        raise RuntimeError("no registration files found; this reader reads nothing")

    names: set[str] = set()
    for path in files:
        names |= set(REGISTRATION.findall(_read(path)))
    if not names:
        raise RuntimeError(
            "no `vm_register_builtin(vm, ...)` rows in %d file(s) -- the registry moved "
            "and this reader is reading nothing" % len(files)
        )
    return names, [os.path.relpath(p, repo_root) for p in files], core


def measure(repo_root: str):
    comp = compiler_names(repo_root)
    reg, files, core = registered_names(repo_root)
    orphans = sorted(comp - reg)
    # The unreachable direction, printed rather than judged: it is the design.
    mod_only = len(reg - comp)
    problems = [
        "%s is in the compiler's builtin table and no file registers it; a program "
        "naming it compiles and then fails at run time with `unknown builtin`" % name
        for name in orphans
    ]
    # THE TABLE'S OTHER SIDE, and it is a reading rather than a red.
    #
    # `compiler.c`'s table is consulted in exactly ONE place -- the `window`
    # statement arm.  An ordinary call that `lookup_func` cannot resolve becomes
    # OP_CALL_BUILTIN by NAME and the VM resolves it at run time, so a name
    # registered in the tree works whether or not it is in the table.  Measured:
    # `chr(65)` answers "A" and `strdup("chr")` does not appear in compiler.c.
    #
    # So the table is not "the builtins this compiler accepts" -- it is a
    # vestigial list whose only reader is one statement kind.  62 names the core
    # runtime registers are absent from it.  That is why a rule of the form
    # "require the name to be in the table" would be wrong: it would refuse 41
    # names the corpus uses today.
    stats = {
        "compiler": len(comp),
        "registered": len(reg),
        "files": len(files),
        "orphans": len(orphans),
        "mod_only": mod_only,
        "core": len(core),
        "core_only": len(core - comp),
    }
    return (1 if problems else 0), problems, stats


def self_test() -> int:
    """The reader has to see a planted orphan and not invent one.

    Both directions, because each fails differently: a reader that finds nothing
    reports a clean tree for any input, and a reader that matches too loosely
    reports an orphan for a name that is registered.
    """
    failures = []

    def compiler_fixture(names):
        return "".join(
            'comp->builtins[comp->builtinCount++].name = strdup("%s");\n' % n for n in names
        )

    def vm_fixture(names):
        return "".join('vm_register_builtin(vm, "%s", fn);\n' % n for n in names)

    # The compiler table parses, per name, in order.
    got = COMPILER_TABLE.findall(compiler_fixture(["alpha", "beta"]))
    if got != ["alpha", "beta"]:
        failures.append("compiler table: want ['alpha','beta'] got %r" % (got,))

    # A registration parses.
    got = REGISTRATION.findall(vm_fixture(["alpha"]))
    if got != ["alpha"]:
        failures.append("registration: want ['alpha'] got %r" % (got,))

    # The set difference this checker computes, on a planted orphan.
    comp = set(COMPILER_TABLE.findall(compiler_fixture(["alpha", "ghost"])))
    reg = set(REGISTRATION.findall(vm_fixture(["alpha"])))
    if comp - reg != {"ghost"}:
        failures.append("a planted orphan must be found: got %r" % (sorted(comp - reg),))
    # And a registered name must not be reported.
    if "alpha" in (comp - reg):
        failures.append("a registered name must not be an orphan")

    # The reader that finds nothing must REFUSE, not report a clean tree: a
    # table that moved is not a table that is empty.
    try:
        registered_names("/nonexistent-root")
        failures.append("a root with no files must raise, not return an empty set")
    except RuntimeError:
        pass

    print("check_builtin_registries --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_builtin_registries: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "the compiler's table parses to its names, in order",
        "a registration line parses to its name",
        "a planted orphan is found and a registered name is not",
        "a root with no registration files raises instead of reporting clean",
    ):
        print("  ok   %s" % line)
    print("check_builtin_registries: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, problems, stats = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_builtin_registries: cannot measure: %s" % problem)
        return 2

    for line in problems:
        print("  PROBLEM  %s" % line)
    print(
        "check_builtin_registries: compiler-table=%d|registered=%d|files=%d"
        "|orphans=%d|registered-but-not-in-table=%d|core-registered=%d|core-absent-from-table=%d"
        % (
            stats["compiler"],
            stats["registered"],
            stats["files"],
            stats["orphans"],
            stats["mod_only"],
            stats["core"],
            stats["core_only"],
        )
    )
    print(
        "check_builtin_registries: the table is consulted in one place (the `window` "
        "statement arm); an ordinary call is resolved by NAME at run time, so absence "
        "from it is not a defect.  Of the %d absent names, %d are CORE-runtime builtins "
        "(`chr`, `atomic_get`, `bit_and` ...) and %d register only through mods, which is "
        "the design.  The core number is the measure of how far a vestigial list has "
        "drifted from the registry it names."
        % (stats["mod_only"], stats["core_only"], stats["mod_only"] - stats["core_only"])
    )
    if red:
        print(
            "check_builtin_registries: a name the compiler emits and nothing answers is a "
            "call that cannot work in any configuration."
        )
        return 1
    print(
        "check_builtin_registries: every name the compiler's table lists has a registration "
        "somewhere in the tree.  This says the two halves agree on existence; it says "
        "nothing about whether the builtin behaves."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
