#!/usr/bin/env python3
"""Why this exists (docs/streams/dec-progress.md, fourth ruler).

The first three rulers in that document are all *counts*: how much source is C,
how many capability names have a script-side counterpart, how many called names
the script side defines.  Counting names cannot tell "the script side implements
it" from "the script side mentions it" -- and the gap between those two is the
whole question the document asks.

This tool measures the gap in one place, mechanically, on a named tree:

    recognised    the C builtin's name appears as a string literal somewhere in
                  the .im sources (the script side knows the name)
    dispatched    selfhost/eval.im's call_builtin has a branch for the name
    forwarded     ... and that branch calls the same-name builtin -- the C code
                  still does the work; the branch only unpacks arguments
    reimplemented ... and it does not: the body computes the result out of
                  script-side values and *other* builtins

Only `reimplemented` is evidence of a capability the script side can carry on
its own.  `forwarded` is the shape that reads like progress and is not: a
dispatch table with 31 forwarding branches looks like 31 capabilities moved,
and moves none.

The same-name tier is reported separately from `forwarded` because it fails in
the opposite direction: a `func len(...)` in a test fixture shares the name and
proves nothing, so every same-name definition is printed with its file.

    python3 tools/dec_equivalence.py [REF]        # default: HEAD

Prints one block of readings; every number is derived from the tree named on
the first line, so two runs are comparable only when those lines are equal.
"""

import re
import subprocess
import sys

C_REGISTER = re.compile(
    r'vm_register_(?:builtin|builtin_full|builtin_safe)\(\s*vm\s*,\s*"([^"]+)"')
IM_FUNC = re.compile(r'^func[ \t]+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)', re.M)
IM_LITERAL = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"')
BRANCH = re.compile(r'if[ \t]+name[ \t]*==[ \t]*"([A-Za-z_][A-Za-z0-9_]*)"\s*\{')


def git(*args):
    out = subprocess.run(("git",) + args, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), out.stderr.strip()))
    return out.stdout


def files_at(ref, pattern):
    return [p for p in git("ls-tree", "-r", "--name-only", ref).splitlines()
            if re.search(pattern, p)]


def read_at(ref, path):
    return git("show", "%s:%s" % (ref, path))


def brace_block(text, open_index):
    """Return text[open_index..close_index] for the brace at open_index."""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_index:i + 1]
    raise SystemExit("unbalanced braces at offset %d" % open_index)


def dispatch_branches(eval_im):
    """[(name, body)] for each `if name == "X" { ... }` inside call_builtin."""
    start = eval_im.find("func call_builtin")
    if start < 0:
        return []
    body = brace_block(eval_im, eval_im.index("{", start))
    branches = []
    for m in BRANCH.finditer(body):
        o = body.index("{", m.start())
        branches.append((m.group(1), brace_block(body, o)))
    return branches


def calls_same_name(name, branch_body):
    return re.search(r'(?<![A-Za-z0-9_])%s\s*\(' % re.escape(name), branch_body) is not None


def main():
    ref = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    sha = git("rev-parse", ref).strip()
    short = git("rev-parse", "--short", ref).strip()

    c_names = sorted(set(C_REGISTER.findall(
        git("grep", "-hoE", "--", r'vm_register_(builtin|builtin_full|builtin_safe)\(vm, "[^"]+"', ref, "--", "src"))))

    im_paths = files_at(ref, r'\.im$')
    defs = {}          # name -> [(path, arity)]
    literals = set()   # names mentioned as string literals anywhere in .im
    for path in im_paths:
        text = read_at(ref, path)
        for m in IM_FUNC.finditer(text):
            args = m.group(2).strip()
            arity = 0 if not args else len([a for a in args.split(",") if a.strip()])
            defs.setdefault(m.group(1), []).append((path, arity))
        literals.update(IM_LITERAL.findall(text))

    eval_path = "selfhost/eval.im"
    branches = dispatch_branches(read_at(ref, eval_path)) if eval_path in im_paths else []

    c_set = set(c_names)
    recognised = sorted(c_set & literals)
    dispatched = [n for n, _ in branches if n in c_set]
    forwarded = [n for n, b in branches if n in c_set and calls_same_name(n, b)]
    reimplemented = [n for n, b in branches if n in c_set and not calls_same_name(n, b)]
    same_name = sorted(set(defs) & c_set)

    def area(path):
        """Three buckets, chosen so that each one changes a reader's answer.

        selfhost/   the interpreter's own sources -- the part that would have to
                    stop needing C for the engine to be script-side
        fixtures    scripts/, tools/, *_test.im -- a name that appears here can
                    be a test's own convenience copy and says nothing about the
                    engine
        modules     everything else (top-level *.im runtime modules)
        """
        base = path.rsplit("/", 1)[-1]
        if path.startswith("selfhost/"):
            return "selfhost"
        if path.startswith(("scripts/", "tools/")) or base.endswith("_test.im"):
            return "fixtures"
        return "modules"

    same_name_areas = {"selfhost": [], "modules": [], "fixtures": []}
    for name in same_name:
        seen = sorted({area(p) for p, _ in defs[name]})
        for a in seen:
            same_name_areas[a].append(name)
    dispatch_unknown = sorted(set(n for n, _ in branches) - c_set)

    total = len(c_names)
    pct = lambda n: ("%.2f%%" % (100.0 * n / total)) if total else "n/a"

    print("tree %s (%s)  -- every number below is from this tree" % (short, sha))
    print("C registered names (denominator)      : %d" % total)
    print(".im files scanned                     : %d" % len(im_paths))
    print(".im func definitions (distinct names) : %d  (%d definitions)"
          % (len(defs), sum(len(v) for v in defs.values())))
    print("dispatch branches in %s       : %d" % (eval_path, len(branches)))
    print("")
    print("recognised  (name as a .im literal)   : %d  %s" % (len(recognised), pct(len(recognised))))
    print("dispatched  (branch in call_builtin)  : %d  %s" % (len(dispatched), pct(len(dispatched))))
    print("forwarded   (branch calls same name)  : %d  %s" % (len(forwarded), pct(len(forwarded))))
    print("reimplemented (branch does NOT)       : %d  %s  %s"
          % (len(reimplemented), pct(len(reimplemented)), reimplemented))
    print("same-name .im func                    : %d  %s" % (len(same_name), pct(len(same_name))))
    for a in ("selfhost", "modules", "fixtures"):
        print("  ... in %-28s: %d  %s" % (a, len(same_name_areas[a]), pct(len(same_name_areas[a]))))
    for name in same_name:
        where = ", ".join("%s(arity %d)" % (p, a) for p, a in sorted(defs[name]))
        print("  %-14s %s" % (name, where))
    independent = sorted(set(reimplemented) | set(same_name_areas["selfhost"])
                         | set(same_name_areas["modules"]))
    print("independent script implementations    : %d  %s  %s"
          % (len(independent), pct(len(independent)), independent))
    if dispatch_unknown:
        print("dispatch branches not in the registry : %s" % dispatch_unknown)

    print("")
    print("readings (copy these verbatim; they are only comparable on this tree):")
    print("  dec-equivalence: registry=%d recognised=%d dispatched=%d forwarded=%d "
          "reimplemented=%d same_name=%d same_name_selfhost=%d same_name_modules=%d "
          "same_name_fixtures=%d independent=%d"
          % (total, len(recognised), len(dispatched), len(forwarded),
             len(reimplemented), len(same_name), len(same_name_areas["selfhost"]),
             len(same_name_areas["modules"]), len(same_name_areas["fixtures"]),
             len(independent)))
    print("  boundary: a branch that does not call the same name is *not* proof of "
          "equivalence -- it may compute something else, or call a differently-named "
          "builtin.  It is proof that the C code is no longer on that path by name.")
    print("  boundary: `independent` counts script-side definitions that do not name the "
          "C builtin they replace.  It does not check that the two compute the same "
          "thing, so treat it as an upper bound on the moved capabilities, not a count.")


if __name__ == "__main__":
    main()
