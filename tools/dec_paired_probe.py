#!/usr/bin/env python3
"""Paired probe: feed one set of inputs to the C builtin and to the script-side
copy that `tools/dec_equivalence.py` counts as an independent implementation.

Why this exists
---------------
`dec_equivalence.py` reports a count of script-side definitions that do *not*
name the C builtin they are filed under.  That count is an upper bound, because
"does not call the same name" is not "computes the same thing".  Closing that
last gap needs the only evidence that settles it: the same inputs, both sides,
compared.

The answer here is deliberately three-way, because the three names fall into
three different states and collapsing them would lose the finding:

  differs-today   the script copy cannot even be loaded by this engine
                  (`selfhost/eval.im` still uses `type(v)`; the parser wants
                  `v.type`), so "equivalent" is not a question one can ask yet
  differs         after the smallest possible syntax repair, the two sides
                  disagree on some inputs
  agrees          after the same repair they agree on this matrix -- evidence,
                  not proof: a matrix can agree and the functions still differ
                  outside it
  collision       the two definitions share a name and nothing else (different
                  arity, different domain) -- `entity_get` here is a handle +
                  fixed field set in C, a dict prototype walk in entity.im, and
                  a global table lookup in verse_kernel.im

Usage
-----
    python3 tools/dec_paired_probe.py [--ref REF] [--interp build/inimerse]

Everything is read from the object store with `git show`, so the readings are
comparable across trees, and the only thing that runs is the interpreter.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

CASES = [
    ("nil", "nil"),
    ("true", "true"),
    ("false", "false"),
    ("zero", "0"),
    ("one", "1"),
    ("minus-one", "-1"),
    ("zero-f", "0.0"),
    ("one-five", "1.5"),
    ("empty-str", '""'),
    ("str-a", '"a"'),
    ("empty-arr", "[]"),
    ("arr-12", "[1, 2]"),
    ("arr-ab", '["a", "b"]'),
    ("arr-nest", "[[1], 2]"),
    ("empty-dict", "{}"),
    ("dict-a1", '{"a": 1}'),
    ("dict-nested", '{"a": [1, "b"]}'),
    ("dict-deep", '{"k": {"n": nil}}'),
]

BANNER = re.compile(r"^\[(mod|infiverse|verse_dist)")


def git_show(ref, path):
    out = subprocess.run(["git", "show", "%s:%s" % (ref, path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("git show %s:%s failed: %s" % (ref, path, out.stderr.strip()))
    return out.stdout


def slice_func(src, name):
    """Return the whole `func name(...) { ... }` block, braces balanced."""
    m = re.search(r"^func %s\(.*?$" % re.escape(name), src, re.M)
    if not m:
        raise SystemExit("no `func %s(` in the source" % name)
    depth, j = 0, m.start()
    while j < len(src):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[m.start():j + 1]
        j += 1
    raise SystemExit("unbalanced braces in `func %s`" % name)


def rename(body, old, new):
    """Rename calls and the definition, but never inside a longer identifier."""
    return re.sub(r"(?<![A-Za-z0-9_])%s\(" % re.escape(old), new + "(", body)


def min_repair(body):
    """The smallest edit that makes the old script parse today: type(v) -> v.type."""
    return body.replace("type(v)", "v.type").replace("type(args[0])", "args[0].type")


def run_program(interp, text, workdir):
    path = os.path.join(workdir, "probe.im")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    out = subprocess.run([interp, path], capture_output=True, text=True, timeout=120)
    blob = (out.stdout or "") + (out.stderr or "")
    lines = [l for l in blob.splitlines()
             if l.strip() and not BANNER.match(l) and not l.startswith("[stderr]")]
    return lines, out.returncode


def parse_errors(lines):
    return [l for l in lines if "Error:" in l or "[exception]" in l]


def matrix(fns_text, workdir, interp):
    """Run the value matrix; return per-name dict of label -> (c_side, s_side, eq)."""
    vals = ", ".join(lit for _, lit in CASES)
    labels = ", ".join('"%s"' % label for label, _ in CASES)
    prog = fns_text + """
vals = [{vals}]
labels = [{labels}]
i = 0
while i < len(vals) {{
    v = vals[i]
    c = str(v)
    s = script_to_str(v)
    same = "EQ"
    if c != s {{ same = "NE" }}
    say "STR|" + labels[i] + "|" + c + "|" + s + "|" + same
    c2 = bool(v)
    s2 = script_truthy(v)
    same2 = "EQ"
    if c2 != s2 {{ same2 = "NE" }}
    say "BOOL|" + labels[i] + "|" + str(c2) + "|" + str(s2) + "|" + same2
    i = i + 1
}}
""".format(vals=vals, labels=labels)
    lines, rc = run_program(interp, prog, workdir)
    rows = {"STR": {}, "BOOL": {}}
    for line in lines:
        parts = line.split("|")
        if len(parts) == 5 and parts[0] in rows:
            rows[parts[0]][parts[1]] = (parts[2], parts[3], parts[4])
    return rows, rc, parse_errors(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", default="HEAD", help="tree to read the scripts from (default HEAD)")
    ap.add_argument("--interp", default=os.path.join("build", "inimerse"),
                    help="interpreter binary (default build/inimerse)")
    args = ap.parse_args()

    if not os.path.exists(args.interp):
        raise SystemExit("no interpreter at %s -- build it first (tools/gate.sh does)" % args.interp)

    eval_src = git_show(args.ref, "selfhost/eval.im")
    entity_src = git_show(args.ref, "entity.im")

    truthy = slice_func(eval_src, "truthy")
    to_str = slice_func(eval_src, "to_str")

    with tempfile.TemporaryDirectory() as tmp:
        # 1) as written -- does the engine load it at all?
        as_written = rename(rename(truthy, "truthy", "script_truthy"), "to_str", "script_to_str") \
            + "\n" + rename(rename(to_str, "to_str", "script_to_str"), "truthy", "script_truthy") \
            + '\nsay "loaded" + str(script_to_str(1))\n'
        w_lines, w_rc = run_program(args.interp, as_written, tmp)
        loadable = not parse_errors(w_lines)

        # 2) after the smallest syntax repair
        repaired = rename(rename(min_repair(truthy), "truthy", "script_truthy"), "to_str", "script_to_str") \
            + "\n" + rename(rename(min_repair(to_str), "to_str", "script_to_str"), "truthy", "script_truthy")
        rows, rc, err = matrix(repaired + "\n", tmp, args.interp)

        # 3) the collision pair: same name, different operation
        ents = rename(slice_func(entity_src, "entity_get"), "entity_get", "script_entity_get") + "\n"
        ent_lines, ent_rc = run_program(
            args.interp,
            ents + 'd = {"a": 1}\n'
            'say "GET|" + str(entity_get(d, "a")) + "|" + str(script_entity_get(d, "a"))\n'
            'say "SET|" + str(entity_set(d, "a", 5)) + "|" + str(script_entity_set(d, "a", 5))\n',
            tmp)
        entset = rename(slice_func(entity_src, "entity_set"), "entity_set", "script_entity_set") + "\n"
        ent_lines2, _ = run_program(
            args.interp,
            ents + entset + 'd = {"a": 1}\n'
            'say "GET|" + str(entity_get(d, "a")) + "|" + str(script_entity_get(d, "a"))\n'
            'say "SET|" + str(entity_set(d, "a", 5)) + "|" + str(script_entity_set(d, "a", 5))\n',
            tmp)

    print("dec-paired: ref=%s interp=%s" % (args.ref, args.interp))
    print("")
    print("as written (selfhost/eval.im)          : %s"
          % ("loads" if loadable else "NOT LOADABLE -- " + (parse_errors(w_lines) or ["?"])[0]))
    if not loadable:
        print("  repro that does not need this script: say type(1)")

    verdicts = {}
    for kind in ("STR", "BOOL"):
        cells = rows.get(kind) or {}
        differ = [k for k, v in cells.items() if v[2] == "NE"]
        eq = len(cells) - len(differ)
        verdicts[kind] = "differs" if differ else ("agrees" if cells else "no-cells")
        print("after the smallest repair  %-4s       : cases=%d equal=%d differ=%d  %s"
              % (kind, len(cells), eq, len(differ), verdicts[kind]))
        for k in differ:
            print("    %-12s C=%r S=%r" % (k, cells[k][0], cells[k][1]))

    coll = {}
    for line in ent_lines2:
        parts = line.split("|")
        if len(parts) == 3:
            coll[parts[0]] = (parts[1], parts[2])
    for op in ("GET", "SET"):
        if op in coll:
            c, s = coll[op]
            same = "same" if c == s else "DIFFERENT OPERATIONS"
            print("collision %-10s               : C=%s S=%s  %s (input: a dict)" % (op, c, s, same))

    print("")
    print("readings (verbatim):")
    print("  dec-paired: ref=%s loadable=%s str=%s bool=%s entity_get=%s entity_set=%s"
          % (args.ref, "yes" if loadable else "no", verdicts.get("STR"), verdicts.get("BOOL"),
             "collision" if coll.get("GET", ("", ""))[0] != coll.get("GET", ("", ""))[1] else "?",
             "collision" if coll.get("SET", ("", ""))[0] != coll.get("SET", ("", ""))[1] else "?"))
    print("  boundary: `agrees` on a matrix is evidence, not proof -- the functions can still")
    print("  differ outside it.  `different operations` is the opposite failure: the two")
    print("  definitions share a name and nothing else, so counting one as an implementation")
    print("  of the other inflates the dec-equivalence count by that much.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
