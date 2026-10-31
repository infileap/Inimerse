#!/usr/bin/env python3
"""Who READS a Bytecode table and who WRITES it -- answered by the compiler.

tools/mir_express.test.py has to say who consumes each container table, and the
column it printed was a regular expression over the source: every site spelled
like "something->labels" counted, so "comp->labels" (a LabelDef list in
compiler.c) was counted as a reader of "Bytecode.labels".  A name is not a type,
and a reading that cannot tell two same-named members apart is not a reading of
the member.

So the compiler is asked instead of the pattern.  In a throwaway copy of the
tree, src/compiler/bytecode.h is rewritten twice and the compiler is run over
every .c file:

  rename pass   the member is renamed to __probe_<name>.  Every access through a
                Bytecode -- and only a Bytecode -- becomes
                "error: 'Bytecode' has no member named 'labels'" with an exact
                file:line:col.  "comp->labels" still compiles, which is the whole
                point: the compiler knows the type and the regex did not.
  const pass    the member is declared const.  Every write becomes
                "error: assignment of read-only member 'labels'" (or a location,
                or an increment).  A struct pointer is made deep-const
                ("const T *const name"), because writing through the table
                ("bc->labels[i].off = x") writes the table too.

  reader = files the rename pass caught, minus the files the const pass caught
  writer = files the const pass caught

Exclusions are named, not implied.  bytecode.[ch] DEFINES the container and
mir.[ch] is the carrier under measurement, so neither side of that pair is a
reader of it.  The instruments are excluded by name because a probe that plants
a value in every table TOUCHES every table: tools/mir_express_probe.c is a real
writer of all nine, and a gauge that stamps itself counted is not a reading.

needed-by-aot is the one column that is not about this tree, so it says where it
stands.  The only runtime that can read a built container is src/vm/vm.c, and the
question is whether the route that emits C already reaches the code that does the
reading.  The evidence is a site, not a name: each read site in vm.c is mapped to
the dispatch label ("L_LOADK_STRING:") it sits under, and the label is checked
against tools/aot_cover.pin.json covered_ops -- which is what the INTERPRETER
actually executed over programs the AST->C translator accepted, not what the
translator could lower one day.  So:

  no       nothing under src/vm reads the member at runtime
  yes      every runtime read sits under an opcode the AOT corpus executes
  not-yet  the runtime reads it, but at least one read sits under an opcode that
           corpus never executes (or under no opcode at all -- a builtin or the
           loader), so the C route cannot reach all of it today

Nothing here edits the working tree: the rewrite happens on a copy under
tempfile, and a fatal baseline error (a file that never parsed) is reported as
UNSCANNED rather than silently counted as "no accesses".
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTAINER_HEADER = "src/compiler/bytecode.h"
CONTAINER_STRUCT = "Bytecode"
SCAN_DIRS = ("src", "mods", "tools")

# bytecode.[ch] defines the container; mir.[ch] is the carrier being measured.
EXCLUDED_CONTAINER = ("src/compiler/bytecode.c", "src/compiler/bytecode.h",
                      "src/compiler/mir.c", "src/compiler/mir.h")
# The instruments, by name.  Named because the thing they measure is "who
# touches the table", and a gauge that plants a value in it touches it.
SELF_PREFIXES = ("tools/mir_express", "tools/mir_access_probe")
SELF_EXCLUDED = ("tools/mir_express.test.py", "tools/mir_express_probe.c",
                 "tools/mir_access_probe.py")

INCLUDES = ["-Isrc/compiler", "-Isrc/common", "-Isrc", "-Isrc/platform",
            "-Isrc/compilation", "-Isrc/parser", "-Isrc/lexer", "-Isrc/vm",
            "-Isrc/runtime", "-Isrc/types", "-Isrc/verse", "-Isrc/mod"]
FLAGS = ["-std=gnu11", "-w", "-fsyntax-only"]
AOT_PIN = "tools/aot_cover.pin.json"
RUNTIME_FILE = "src/vm/vm.c"
COPY_IGNORE = shutil.ignore_patterns(".git", "build", "__pycache__", "*.pyc",
                                     "*.o", "*.so", "*.a", "node_modules")

# The declarator, read from the RIGHT: a greedy type would swallow all but the
# last letter of "int count" and call the member "t" (the name is one identifier,
# so a single character was enough to complete the match).  Taking the name first
# and letting the prefix be non-greedy is what makes "int count" name="count".
DECL_RE = re.compile(r"^(?P<prefix>.*?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
                     r"(?P<arr>\s*\[[^\]]*\])?$")
PTR_RE = re.compile(r"^(?P<type>.*?)\s*(?P<ptr>\*+)$")
ACCESS_RE = re.compile(
    r"^([^:\n]+):(\d+):(\d+): error: .*has no member named .(\w+).", re.M)
WRITE_RE = re.compile(
    r"^([^:\n]+):(\d+):(\d+): error: ([^\n]*read-only[^\n]*)$", re.M)
LABEL_RE = re.compile(r"^\s*(L_[A-Z0-9_]+):")


def strip_comments(text):
    """Blank every comment, byte for byte, so offsets still name tree lines.

    Replacing comment text with an equal run of spaces (newlines kept) means
    every offset in the result is the offset it was in the original -- which is
    what lets a member's declaration be rewritten without re-parsing anything.
    Comments are handled before quotes, or an apostrophe in "don't" would open a
    character literal and swallow the closing delimiter.
    """
    out = list(text)
    i, n, state = 0, len(text), 0   # 0 code, 1 string, 2 char, 3 line, 4 block
    while i < n:
        c = text[i]
        if state == 0:
            if c == '"':
                state = 1
            elif c == "'":
                state = 2
            elif c == "/" and i + 1 < n and text[i + 1] == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = 3
                continue
            elif c == "/" and i + 1 < n and text[i + 1] == "*":
                out[i] = out[i + 1] = " "
                i += 2
                state = 4
                continue
        elif state == 1 or state == 2:
            if c == "\\":
                i += 2
                continue
            if (state == 1 and c == '"') or (state == 2 and c == "'") or c == "\n":
                state = 0
        elif state == 3:
            if c == "\n":
                state = 0
            else:
                out[i] = " "
        elif state == 4:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = 0
                continue
            if c != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def bounded_struct(text, struct_name):
    """(body, body_offset) of "typedef struct <name> { ... } <name>;"."""
    for m in re.finditer(r"typedef\s+struct\s+(\w+)?\s*\{", text):
        start = m.end()
        depth, i = 1, start
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        tail = re.match(r"\s*(\w+)\s*;", text[i:i + 40])
        if tail and tail.group(1) == struct_name:
            return text[start:i - 1], start
    return None, None


def parse_struct_members(body, base_off):
    """member -> {type, ptr, arr, name_start, decl_start}; offsets into the original."""
    members = {}
    for m in re.finditer(r"[^;{}]+;", body):
        chunk = m.group(0)
        core = chunk.strip().rstrip(";").strip()
        if not core:
            continue
        core_off = m.start() + chunk.index(core)
        first_type = None
        pos = 0
        for part in core.split(","):
            piece = part.strip()
            here = core.index(piece, pos)
            pos = here + len(piece)
            mm = DECL_RE.match(piece)
            if not mm:
                continue
            pm = PTR_RE.match(mm.group("prefix").strip())
            if pm:
                typ, ptr = pm.group("type").strip(), pm.group("ptr")
            else:
                typ, ptr = mm.group("prefix").strip(), ""
            if first_type is None:
                first_type = typ
            elif not typ:
                typ = first_type
            members[mm.group("name")] = {
                "type": typ,
                "ptr": ptr,
                "arr": (mm.group("arr") or "").strip(),
                "name_start": base_off + core_off + here + mm.start("name"),
                "decl_start": base_off + core_off + here,
                "first-declarator": first_type is typ,
            }
    return members


def const_prefix(member):
    """The text that replaces "type ptr " in front of the member's name.

    Depth follows the declaration, not a hand-written list: a struct pointer is
    made deep-const because "bc->try_entries[i].start_off = x" writes the table
    through the pointer, and a shallow "T *const" would call that a read.  An
    array of pointers gets the const on the element, which is what an assignment
    to "bc->func_names[i]" writes.
    """
    t, p, arr = member["type"], member["ptr"], member["arr"]
    if p == "":
        return "const %s " % t
    if p == "*":
        return "%s *const " % t if arr else "const %s *const " % t
    if p == "**" and not arr:
        return "%s *const *const " % t
    raise ValueError("no const form for %r %r%s" % (t, p, arr))


def rewrite_header(original, members, wanted, mode):
    edits = []
    for name in wanted:
        m = members[name]
        if mode == "rename":
            edits.append((m["name_start"], m["name_start"] + len(name), "__probe_" + name))
        elif mode == "const":
            edits.append((m["decl_start"], m["name_start"], const_prefix(m)))
        else:
            raise ValueError(mode)
    text = original
    for a, b, sub in sorted(edits, key=lambda e: -e[0]):
        text = text[:a] + sub + text[b:]
    return text


def scan_files(root):
    files = []
    for base in SCAN_DIRS:
        for dirpath, dirs, names in os.walk(os.path.join(root, base)):
            dirs[:] = [d for d in dirs if d != "__pycache__"]
            for n in names:
                if n.endswith(".c"):
                    files.append(os.path.relpath(os.path.join(dirpath, n), root))
    return sorted(files)


def sweep(cc, tree, files, workers=8):
    """{relpath: (returncode, stderr)} for one -fsyntax-only pass over the tree."""
    def one(rel):
        p = subprocess.run([cc] + FLAGS + INCLUDES + [rel], cwd=tree,
                           capture_output=True, text=True)
        return rel, p.returncode, p.stderr
    out = {}
    with ThreadPoolExecutor(max_workers=workers) as ex:
        for rel, rc, err in ex.map(one, files):
            out[rel] = (rc, err)
    return out


def access_sites(stderr):
    return [(m.group(1), int(m.group(2)), int(m.group(3)), m.group(4))
            for m in ACCESS_RE.finditer(stderr)]


def write_sites(stderr, known):
    """[(file, line, col, member_or_None, message, named)].

    gcc names the member for "read-only member 'labels'" and inside the
    expression for "read-only location 'bc->func_names[i]'".  It does NOT for
    "assignment of member 'start_off' in read-only object" -- so that one is
    attributed by the line it sits on, using the rename pass's access list.
    """
    out = []
    for m in WRITE_RE.finditer(stderr):
        f, line, col, msg = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
        named = [k for k in known if re.search(r"\b" + re.escape(k) + r"\b", msg)]
        out.append((f, line, col, named[0] if len(named) == 1 else None, msg, named))
    return out


def runtime_contexts(root, sites):
    """Each vm.c read site -> the dispatch label it sits under (None if outside)."""
    src = os.path.join(root, RUNTIME_FILE)
    if not os.path.isfile(src):
        return []
    lines = open(src, errors="ignore").read().splitlines()
    out = []
    for (f, line, col) in sites:
        if f != RUNTIME_FILE:
            continue
        ctx = None
        for i in range(min(line, len(lines)) - 1, -1, -1):
            lm = LABEL_RE.match(lines[i])
            if lm:
                ctx = lm.group(1)
                break
        out.append({"site": "%s:%d" % (f, line), "ctx": ctx})
    return out


DISPATCH_RE = re.compile(r"\[\s*(OP_[A-Z0-9_]+)\s*\]\s*=\s*&&(L_[A-Za-z0-9_]+)")


def dispatch_labels(root):
    """label -> opcode, read off the dispatcher's OWN table.

    The mapping is declared in src/vm/vm.c as "[OP_X] = &&L_X," -- one row per
    opcode, each the address of the body that serves it.  Deriving it from the
    name ("L_" + opcode) would be a guess that happens to look right; reading the
    table is the tree saying it.
    """
    src = os.path.join(root, RUNTIME_FILE)
    if not os.path.isfile(src):
        return {}
    return {m.group(2): m.group(1)
            for m in DISPATCH_RE.finditer(open(src, errors="ignore").read())}


def aot_covered(root):
    p = os.path.join(root, AOT_PIN)
    if not os.path.isfile(p):
        return None, None
    data = json.load(open(p))
    return set(data.get("covered_ops") or []), data.get("tree")


def needed_by_aot(contexts, covered):
    """The three-valued answer, as a pure function so a control can bend it."""
    if not contexts:
        return "no"
    if covered is None:
        return "not-yet"
    return "yes" if all(c.get("op") and c["op"] in covered for c in contexts) \
        else "not-yet"


def is_instrument(rel):
    return any(rel.startswith(p) for p in SELF_PREFIXES)


def class_sites(seen, write_lines, container_files, instrument_files):
    """Partition observed sites into exactly one class each, by a stated order.

    The classes describe the MEASUREMENT, not the code: a site is `instrument`
    or `container` because of the FILE it sits in (both sets are named in the
    reading), otherwise a `writer` because the const pass caught it assigning,
    otherwise a `reader` because the rename pass caught it accessing.  Anything
    no predicate matched is `unknown`, and `overlap` counts the sites more than
    one predicate matched -- so the precedence above is a measured fact rather
    than a tie-break nobody wrote down."""
    classes = {"reader": [], "writer": [], "container": [], "instrument": [],
               "unknown": []}
    overlap = []
    for (f, l) in sorted(seen):
        preds = []
        if f in instrument_files:
            preds.append("instrument")
        if f in container_files:
            preds.append("container")
        if (f, l) in write_lines:
            preds.append("writer")
        else:
            preds.append("reader")
        if len(preds) > 1:
            overlap.append(((f, l), preds))
        classes[preds[0]].append((f, l))
    return {"classes": classes, "overlap": overlap}


def textual_sites(root, files, members):
    """member -> {(file, line)} for every member-shaped mention, comments removed.

    A second instrument, deliberately unlike the first: the two compiler passes
    see only what parses, this sees only what is written (and this runs on the
    ORIGINAL tree, while the passes run on the rewritten copy).  It is here to
    name the mentions the passes did not account for, not to overrule them: an
    access the rename pass did not report is not an access through the container
    under measurement (another struct can carry a member of the same name), and
    a file that did not compile contributes nothing but mentions."""
    pats = {m: re.compile(r"(?:->|\.)" + re.escape(m) + r"(?![A-Za-z0-9_])")
            for m in members}
    out = {m: set() for m in members}
    for rel in files:
        try:
            src = open(os.path.join(root, rel), errors="ignore").read()
        except OSError:
            continue
        for i, line in enumerate(strip_comments(src).splitlines(), 1):
            for m, p in pats.items():
                if p.search(line):
                    out[m].add((rel, i))
    return out


def unseen_reason(rel, fatal_files, soft_files):
    """Why a member-shaped MENTION is not one of this member's measured sites.

    Three answers, and the split is the one a reader needs: a clean file the
    compiler read to the end and stayed silent about is the ordinary case of
    another struct carrying a member of the same name; a file that did not
    compile to the end, or did not compile at all, could still hold a real
    access the pass never saw.  Only the first is safe to summarise as a count.
    """
    if rel in fatal_files:
        return "file-not-scanned"
    if rel in soft_files:
        return "file-had-errors"
    return "other-struct"


def run_probe(cc, root=None, members=None, workers=8):
    """Measure the tree at root.  Never leaves a copy behind."""
    root = root or ROOT
    header_path = os.path.join(root, CONTAINER_HEADER)
    original = open(header_path, errors="ignore").read()
    body, base = bounded_struct(strip_comments(original), CONTAINER_STRUCT)
    if body is None:
        raise RuntimeError("no typedef struct %s in %s"
                           % (CONTAINER_STRUCT, CONTAINER_HEADER))
    parsed = parse_struct_members(body, base)
    wanted = list(members) if members else sorted(parsed)
    missing = [m for m in wanted if m not in parsed]
    if missing:
        raise RuntimeError("no such member(s) in %s: %s"
                           % (CONTAINER_STRUCT, ", ".join(missing)))

    tmp = tempfile.mkdtemp(prefix="mir_access_probe.")
    try:
        tree = os.path.join(tmp, "tree")
        shutil.copytree(root, tree, ignore=COPY_IGNORE, symlinks=True)
        tree_header = os.path.join(tree, CONTAINER_HEADER)
        files = scan_files(tree)

        baseline = sweep(cc, tree, files, workers)
        fatal = sorted(f for f, (rc, err) in baseline.items() if "fatal error" in err)
        soft = sorted(f for f, (rc, err) in baseline.items()
                      if rc != 0 and "fatal error" not in err)
        live = [f for f in files if f not in set(fatal)]
        fatal_set = set(fatal)
        # Read off the original tree, before the header is rewritten: the copy
        # is about to stop being the tree a reader would grep.
        text = textual_sites(root, files, wanted)

        with open(tree_header, "w") as f:
            f.write(rewrite_header(original, parsed, wanted, "rename"))
        renamed = sweep(cc, tree, live, workers)
        reads = {}
        for f, (rc, err) in renamed.items():
            for (file_, line, col, mem) in access_sites(err):
                reads.setdefault(mem, []).append((file_, line, col))

        with open(tree_header, "w") as f:
            f.write(rewrite_header(original, parsed, wanted, "const"))
        consted = sweep(cc, tree, live, workers)
        by_line = {}
        for mem, sites in reads.items():
            for (file_, line, _col) in sites:
                by_line.setdefault((file_, line), set()).add(mem)
        writes, unattributed = {}, []
        for f, (rc, err) in consted.items():
            for (file_, line, col, mem, msg, named) in write_sites(err, set(wanted)):
                if mem is None:
                    cands = sorted(by_line.get((file_, line), ()))
                    if len(cands) == 1:
                        mem = cands[0]
                    else:
                        unattributed.append({"site": "%s:%d" % (file_, line),
                                             "msg": msg, "candidates": cands})
                        continue
                writes.setdefault(mem, []).append((file_, line, col, msg))

        out = {"members": {}, "unscanned": {"fatal": fatal, "soft": soft},
               "scanned": len(live), "unattributed-writes": unattributed,
               # The whole member set, so a reading that measures nine of them
               # can say so instead of implying it measured the container.
               "container-members": sorted(parsed)}
        covered, aot_tree = aot_covered(root)
        label_ops = dispatch_labels(root)
        out["aot"] = {"pin": AOT_PIN, "tree": aot_tree, "labels": len(label_ops),
                      "covered-ops": len(covered) if covered is not None else None}
        for mem in wanted:
            read_sites_m = reads.get(mem, [])
            write_sites_m = writes.get(mem, [])
            write_lines = {(f, l) for (f, l, _c, _m) in write_sites_m}
            # A LINE that assigns is a write even though the compiler also reports
            # the access itself as a use of the member.  Cancelling whole FILES
            # would erase vm.c's read of str_interned -- vm.c allocates the cache
            # on one line and reads it on several others, so "vm.c writes it"
            # would take "vm.c reads it" down with it.
            genuine = {f for (f, l, _c) in read_sites_m if (f, l) not in write_lines}
            all_read = {f for (f, _l, _c) in read_sites_m}
            raw_writes = {f for (f, _l, _c, _m) in write_sites_m}
            readers = sorted(f for f in genuine
                             if f not in EXCLUDED_CONTAINER and not is_instrument(f))
            writers = sorted(f for f in raw_writes
                             if f not in EXCLUDED_CONTAINER and not is_instrument(f))
            ctxs = runtime_contexts(root, [(f, l, c) for (f, l, c) in read_sites_m
                                           if f == RUNTIME_FILE
                                           and (f, l) not in write_lines])
            for ctx in ctxs:
                if ctx["ctx"] is None:
                    ctx["where"], ctx["op"] = "loader", None
                elif ctx["ctx"] in label_ops:
                    ctx["where"], ctx["op"] = "dispatch", label_ops[ctx["ctx"]]
                else:
                    # A label the dispatcher's own table does not carry: the
                    # site maps to no opcode.  Named as unknown and counted,
                    # never dropped -- a missing mapping must not read as a
                    # site that was never read.
                    ctx["where"], ctx["op"] = "unknown", None
            read_lines_m = sorted({(f, l) for (f, l, _c) in read_sites_m})
            write_lines_m = sorted({(f, l) for (f, l, _c, _m) in write_sites_m})
            seen = sorted(set(read_lines_m) | set(write_lines_m))
            part = class_sites(seen, set(write_lines_m), set(EXCLUDED_CONTAINER),
                               set(SELF_EXCLUDED))
            # Why a written mention is NOT one of this member's sites.  The two
            # that could hide a real access -- a file the pass could not read to
            # the end, and a file it never parsed -- are named per site; a clean
            # file the compiler looked at and stayed silent about is the ordinary
            # case of another struct carrying a member of the same name, and only
            # its count is printed.
            unseen = [{"site": "%s:%d" % s,
                       "why": unseen_reason(s[0], fatal_set, set(soft))}
                      for s in sorted(text.get(mem, set()) - set(seen))]
            out["members"][mem] = {
                "reader": readers,
                "writer": writers,
                "reader-count": len(readers),
                "writer-count": len(writers),
                "touchers": sorted(set(readers) | set(writers)),
                "self-excluded": list(SELF_EXCLUDED),
                "self-excluded-hit": sorted(f for f in (all_read | raw_writes)
                                            if is_instrument(f)),
                "container-excluded": sorted(f for f in EXCLUDED_CONTAINER
                                             if f in all_read or f in raw_writes),
                "runtime": ctxs,
                "needed-by-aot": needed_by_aot(ctxs, covered),
                "conservation": {
                    "sites": ["%s:%d" % s for s in seen],
                    "classes": {k: ["%s:%d" % s for s in v]
                                for k, v in part["classes"].items()},
                    "overlap": [{"site": "%s:%d" % s, "classes": c}
                                 for s, c in part["overlap"]],
                    "textual": len(text.get(mem, set())),
                    "unseen": unseen,
                },
            }
        return out
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--member", action="append", default=None,
                    help="only this container member (repeatable)")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--summary", action="store_true",
                    help="one line per member instead of the JSON")
    args = ap.parse_args()
    try:
        res = run_probe(args.cc, args.root, args.member, args.workers)
    except RuntimeError as e:
        print("mir_access_probe: REFUSED|%s" % e, file=sys.stderr)
        return 2
    if args.summary:
        for mem, m in sorted(res["members"].items()):
            print("MIR-ACCESS|member=%s|reader-count=%d|reader=(%s)|writer=(%s)"
                  "|self-excluded=(%s)|container-excluded=(%s)|needed-by-aot=%s"
                  % (mem, m["reader-count"], ",".join(m["reader"]) or "none",
                     ",".join(m["writer"]) or "none",
                     ",".join(m["self-excluded-hit"]) or "none",
                     ",".join(m["container-excluded"]) or "none",
                     m["needed-by-aot"]))
        print("MIR-ACCESS-SCAN|scanned=%d|fatal=%d|soft=%d|aot-pin=%s|aot-covered=%s"
              % (res["scanned"], len(res["unscanned"]["fatal"]),
                 len(res["unscanned"]["soft"]), res["aot"]["pin"],
                 res["aot"]["covered-ops"]))
        if res["unscanned"]["fatal"]:
            print("MIR-ACCESS-UNSCANNED|why=fatal-error-so-nothing-was-parsed|files=%s"
                  % ",".join(res["unscanned"]["fatal"]))
    else:
        print(json.dumps(res, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
