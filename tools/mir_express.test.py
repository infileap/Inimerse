#!/usr/bin/env python3
"""What can the MIR represent, and what does its regeneration silently lose?

src/compiler/mir.h DEVIATION 3 says function names, the thread section, the try
table, the global-name table, the capture table and the label table are "carried"
through IrModule.bc rather than held in a field.  One verb covers seven tables and
the verb is true for four of them: copy_pools copies the string/float pools, the
global names and the try table, emit_node copies the function and thread sections,
and NOTHING touches the capture table, the label table, the thread flags or the
string intern cache.  A table nothing copies is not carried -- the regenerated
container simply lacks it -- and no reading said so, because the roundtrip compares
the bytes that are ENCODED and those tables are memory-only (bytecode.h calls them
"memory-only, appended").

Both halves of that sentence are answered by measurement, neither by grep:

  field  is there an IrFunc/IrModule field for it?  The COMPILER answers: a probe
         doing offsetof(<owner>, <member>) either compiles or does not.  A comment
         naming the member cannot make it compile.
  move   does the regeneration copy it?  RUNNING answers: tools/mir_express_probe.c
         plants a distinctive value in each container table, builds the view, calls
         mir_to_bytecode, and reports whether the value came out.  A grep would
         answer "mir.c mentions capture_names", which stops being true the moment
         somebody adds a comment or moves the copy into a helper.

Readings:
  MIR-EXPRESS|item=NAME|field=..|move=..|state=..|writes-section=..
             |reader=(FILES)|reader-count=N|writer=(FILES)|self-excluded=(FILES)
             |container-excluded=(FILES)|needed-by-aot=yes|no|not-yet|n/a
             |in-memory-uses=N
  MIR-EXPRESS-AOT|item=NAME|needed-by-aot=..|runtime-readers=(file:line@L_OP,...)
                 |basis=tools/aot_cover.pin.json:covered_ops
  MIR-EXPRESS-WIDTH|item=block-index|width=2|max-insns=32767|overflow=refuse|effect=...
  MIR-EXPRESS-SUMMARY|items=N|field=F|alias=A|dropped=D|absent=X|red=NAMES|green=QUIET-NAMES
  MIR-EXPRESS-PIN|tree=SHA|head=SHA|dropped=N|pinned=N|dropped-match=yes|no|tree-moved=yes|no
  MIR-EXPRESS-NEW|item=NAME|effect=...        a loss the pin did not record
  MIR-EXPRESS-FIXED|item=NAME|effect=...      a recorded loss that no longer happens
  MIR-EXPRESS-PIN-STALE|effect=...|fix=...

  reader   files the COMPILER caught accessing the member (tools/mir_access_probe.py)
  writer   files the COMPILER caught writing it
  field    the IR holds it                          (IrFunc.argc)
  alias    no field, and the regeneration really copies it (func names, threads, try, globals)
  dropped  no field, the container has it, nothing copies it (capture, labels, thread flags, intern cache)
  absent   not in the IR and not in the encoding either (the int constant pool, DEVIATION 2)
  red      dropped, and something outside the carrier still reads it -- the alarm
  green    dropped, and nothing reads it: "this is not carried" is a different
           sentence from "this is breaking something today".  It is its own
           column because a green computed as red + red_quiet can never differ
           from red, so it would report nothing at all.

dropped is a finding and not a footnote: it is red when something outside the
container and the carrier reads the table (reader-count > 0), and reader-count=0
says out loud that nothing reads it today -- because "nobody reads it today" and
"it may be thrown away" differ.

The reader/writer columns used to be a regular expression over the source, and a
name is not a type: "comp->labels" is a LabelDef list, not this container's
label table.  So the columns now come from tools/mir_access_probe.py, which renames
and then const-ifies each member in a throwaway copy of the tree and lets the
compiler name the sites.  needed-by-aot is grounded the same way -- in a site, not
in a name: every read site under src/vm is mapped to the dispatch label it sits
under and checked against the AOT route's own covered opcodes, so "not-yet" says
which opcode that route has never run rather than what it might support one day.

THE PIN, AND WHY THERE IS ONE
-----------------------------
The measurement is red today: four tables the container has, the IR does not, and
the regeneration never copies them.  That is a finding, not a broken build, and a
test that stays red for a finding is a red nobody reads.  So --pin records the
set, and the test then reads CHANGES against it: a fifth table silently dropped
is red, and so is one of the four quietly fixed without anyone saying so (that
pin has gone stale).  Neither is a number to re-pin on a hunch.  Tree drift is
reported on its own token, because a pin cannot name the commit that contains it.

--self-check replays the three failures this file exists to keep apart: a member
that does not exist must say "no" (not "undecidable"), a table the carrier DOES
copy must say kept=yes (or every "no" would be the probe's fault), and neutering
the carrier must turn an alias into a dropped.  --gate runs the controls and the
pinned measurement, which is how the registered test invokes it.

Usage: tools/mir_express.test.py [--cc cc] [--json] [--pin] [--self-check] [--gate]
Exit: 0 measured against a pin that still describes this tree, or every control
held; 1 when the finding moved or a control failed; 2 when a probe will not build
(a probe that cannot run is not a reading of zero items).
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDES = ["-Isrc/compiler", "-Isrc/common", "-Isrc", "-Isrc/platform", "-Isrc/compilation"]
FLAGS = ["-std=gnu11", "-O0", "-w"]
ENGINE_SRC = ["src/compiler/mir.c", "src/compiler/bytecode.c",
              "src/compilation/deps.c", "src/platform/platform.c"]
ENGINE_LIBS = ["-lm", "-lpthread", "-ldl"]

# item, which IR type would hold it, which member name, the CONTAINER member whose
# readers decide the verdict, and which probe line reports whether the copy happens.
# The reader/writer columns are computed, not written: they are the files the
# COMPILER named when the access probe renamed and then const-ified that member in a
# throwaway copy of this tree.  Writing "reader=aot_native when it emits closures"
# into a table would be a claim about the future; measuring it says what is true
# today, and today is the only thing this file can be a reading of.
ITEMS = [
    ("func-name", "IrFunc", "name", "func_names", "func-name"),
    ("func-argc", "IrFunc", "argc", "func_argc", "func-argc"),
    ("thread-section", "IrModule", "threads", "thread_count", "thread-section"),
    ("try-table", "IrModule", "tries", "try_entries", "try-table"),
    ("global-names", "IrModule", "globals", "global_names", "global-names"),
    ("capture-table", "IrModule", "captures", "capture_names", "capture-table"),
    ("label-table", "IrModule", "labels", "labels", "label-table"),
    ("thread-flags", "IrModule", "thread_flags", "thread_flags", "thread-flags"),
    ("string-intern", "IrModule", "str_interned", "str_interned", "string-intern"),
    ("int-pool", None, None, None, None),
]
CONTAINER_MEMBERS = [item[3] for item in ITEMS if item[3]]

# Who reads a table is a fact about the source, and the way to keep it from being
# a claim is to ask the COMPILER instead of writing a column:
#
#   reader  files the rename pass caught accessing the member through a Bytecode
#   writer  files the const pass caught writing it
#
# The exclusions are named on every row rather than implied by a set here:
# bytecode.[ch] DEFINES the container and mir.[ch] is the carrier under
# measurement, and the instruments are named because a probe that plants a value
# in every table touches every table -- a gauge that stamps itself counted is not
# a reading of who reads the table.  Both lists are printed (self-excluded=,
# container-excluded=) so which files were taken out, and which of them actually
# hit, is visible rather than inferred.

_ACCESS_PROBE = None


def _access_probe():
    """tools/mir_access_probe.py, loaded by path (tools/ is not a package)."""
    global _ACCESS_PROBE
    if _ACCESS_PROBE is None:
        path = os.path.join(ROOT, "tools", "mir_access_probe.py")
        spec = importlib.util.spec_from_file_location("mir_access_probe", path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _ACCESS_PROBE = mod
    return _ACCESS_PROBE


def compile_c(cc, tmp, body, name, extra_srcs=(), libs=()):
    src = os.path.join(tmp, name + ".c")
    with open(src, "w") as f:
        f.write(body)
    exe = os.path.join(tmp, name)
    cmd = [cc] + FLAGS + INCLUDES + ["-o", exe, src] + list(extra_srcs) + list(libs)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, p.stderr, exe


def field_probe(cc, tmp, owner, member):
    """Does offsetof(<owner>, <member>) compile?  The compiler decides.

    Compiled with -c: the probe has no main() and is not meant to run, so a link
    step would fail on every item and the failure would look like a missing field.
    """
    body = ('#include <stddef.h>\n#include "mir.h"\n'
            "int probe(void) { return (int)offsetof(%s, %s); }\n" % (owner, member))
    src = os.path.join(tmp, "field_" + member + ".c")
    with open(src, "w") as f:
        f.write(body)
    obj = os.path.join(tmp, "field_" + member + ".o")
    p = subprocess.run([cc] + FLAGS + INCLUDES + ["-c", "-o", obj, src],
                       cwd=ROOT, capture_output=True, text=True)
    if p.returncode == 0:
        return "yes", ""
    tail = p.stderr.strip().splitlines()[-1] if p.stderr.strip() else ""
    if ("no member named" in p.stderr) or ("has no member" in p.stderr):
        return "no", tail
    return "probe-failed", tail


def move_probe(cc, tmp):
    """(dict item -> kept, error).  Answers the copy question by running it."""
    src = os.path.join(ROOT, "tools", "mir_express_probe.c")
    if not os.path.isfile(src):
        return {}, "no tools/mir_express_probe.c"
    exe = os.path.join(tmp, "move_probe")
    cmd = [cc] + FLAGS + INCLUDES + ["-o", exe, src] + ENGINE_SRC + ENGINE_LIBS
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        tail = p.stderr.strip().splitlines()[-1] if p.stderr.strip() else "build failed"
        return {}, tail
    run = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True)
    kept = {}
    for line in run.stdout.splitlines():
        m = re.match(r"MIR-EXPRESS-MOVE\|item=([a-z-]+)\|kept=(yes|no)", line)
        if m:
            kept[m.group(1)] = m.group(2)
    if not kept:
        return {}, (run.stdout + run.stderr).strip()[:200] or "the probe printed nothing"
    return kept, None


def width_probe(cc, tmp):
    """IrBlock.start is 2 bytes -- measured, and the instruction cap follows."""
    body = ('#include <stddef.h>\n#include <stdio.h>\n#include "mir.h"\n'
            "int main(void){ printf(\"%zu\", sizeof(((IrBlock *)0)->start)); return 0; }\n")
    rc, _, exe = compile_c(cc, tmp, body, "width")
    if rc != 0:
        return None
    run = subprocess.run([exe], capture_output=True, text=True)
    try:
        return int(run.stdout.strip())
    except ValueError:
        return None

# Which container sections the FILE FORMAT actually writes.  Read live from
# bytecode_write, because a hand-written list of section names is exactly the
# thing that rots the moment somebody adds a section -- and this reading exists
# to say where a table went, not where it went last month.
def written_sections():
    src = os.path.join(ROOT, "src", "compiler", "bytecode.c")
    text = open(src, errors="ignore").read() if os.path.isfile(src) else ""
    out = set()
    for m in re.finditer(r"fwrite\(&?bc->([a-z_]+)", text):
        out.add(m.group(1))
    return out


def _pin_path():
    return os.path.join(ROOT, "tools", "mir_express.pin.json")


def _head_sha():
    p = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                       capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else "<not a git tree>"


def write_pin(rows, width):
    """Record what is dropped today, so the test becomes a reading of CHANGE.

    The measurement is red today: four tables the container has, the IR does not,
    and the regeneration never copies them.  That is a finding, not a broken
    build, and a test that stays red for a finding is a red nobody reads.  So the
    set is pinned -- and the test then goes red when the set MOVES, in either
    direction: a fifth table silently dropped, or one of these four quietly
    fixed without anyone saying so.  Both are news.
    """
    note = ("regenerate with tools/mir_express.test.py --pin. The dropped set is"
            " what the regeneration silently loses today; the test compares"
            " against this file, so the finding is recorded rather than"
            " re-litigated on every run.")
    payload = {
        "tree": _head_sha(),
        "dropped": sorted(r["item"] for r in rows if r["state"] == "dropped"),
        "alias": sorted(r["item"] for r in rows if r["state"] == "alias"),
        "field": sorted(r["item"] for r in rows if r["state"] == "field"),
        "absent": sorted(r["item"] for r in rows if r["state"] == "absent"),
        "block_index_width": width,
        "note": note,
    }
    with open(_pin_path(), "w") as f:
        f.write(json.dumps(payload, indent=2) + chr(10))
    print("mir_express: pin written to %s (tree=%s)" % (_pin_path(), _head_sha()))


def check_pin(rows, width):
    """Two questions, two fields: do the categories hold, and did the pin move.

    The verdict follows the categories -- a newly dropped table is red, and so is
    a pinned loss that no longer happens (that pin is stale) -- while tree drift
    is reported on its own, because a pin cannot name the commit that contains
    it.  The same split the AOT pin needed.
    """
    path = _pin_path()
    if not os.path.isfile(path):
        print("MIR-EXPRESS-PIN|pin=<missing>|match=no|why=no-pin-file-to-compare-with")
        return 1
    with open(path) as f:
        pin = json.load(f)
    live = sorted(r["item"] for r in rows if r["state"] == "dropped")
    pinned = sorted(pin.get("dropped") or [])
    added = [i for i in live if i not in pinned]
    fixed = [i for i in pinned if i not in live]
    ok = not added and not fixed
    moved = pin.get("tree") != _head_sha()
    # known-broken= says why a green gate is green: these tables ARE broken today,
    # and the run is green because the breakage was recorded rather than because
    # it went away.  Without this, `gate: OK` reads as "these four are fine".
    print("MIR-EXPRESS-PIN|tree=%s|head=%s|dropped=%d|pinned=%d|dropped-match=%s"
          "|known-broken=%d|since=%s|tree-moved=%s"
          % ((pin.get("tree") or "?")[:7], _head_sha()[:7], len(live), len(pinned),
             "yes" if ok else "no", len(pinned), (pin.get("tree") or "?")[:7],
             "yes" if moved else "no"))
    for item in added:
        print("MIR-EXPRESS-NEW|item=%s|effect=a-table-the-regeneration-now-loses"
              "-that-the-pin-did-not-record" % item)
    for item in fixed:
        print("MIR-EXPRESS-FIXED|item=%s|effect=the-pin-names-a-loss-that-no"
              "-longer-happens-so-the-pin-is-stale" % item)
    if not ok:
        print("MIR-EXPRESS-PIN-STALE|effect=the-recorded-loss-set-no-longer"
              "-describes-this-tree|fix=rerun-with---pin-once-the-change-is"
              "-explained")
    return 0 if ok else 1

def self_check(cc, tmp):
    """Three controls, because a reading that cannot fail is a reading of nothing.

    The three failures this file exists to keep apart:
      field   a member that does not exist must come out "no" -- not
              "probe-failed", because a build failure reported as a missing field
              is the same sentence as a missing field and not the same fact
      copy    a table the carrier DOES copy must come out kept=yes; if every item
              came out "no" the probe would be broken rather than the code
      caught  neutering copy_pools must turn an alias into a dropped, or the
              reading never depended on the carrier in the first place
    """
    checks = [0]

    def check(what, got, want):
        checks[0] += 1
        if got != want:
            print("mir_express self-check: FAIL -- %s\n  want %r\n  got  %r"
                  % (what, want, got), file=sys.stderr)
            sys.exit(1)

    check("a member that does not exist is absent, not undecidable",
          field_probe(cc, tmp, "IrFunc", "no_such_member_here")[0], "no")
    check("a member that does exist is present",
          field_probe(cc, tmp, "IrFunc", "argc")[0], "yes")

    kept, err = move_probe(cc, tmp)
    check("the copy probe built", err is None, True)
    check("a table the carrier copies is seen as kept", kept.get("global-names"), "yes")
    check("a table nothing copies is seen as not kept", kept.get("capture-table"), "no")

    # Neutering copy_pools must turn alias into dropped.  Done on a COPY of mir.c,
    # never on the tree: a control that edits the code under test and "remembers to
    # put it back" is not a control, it is an experiment on the working tree.
    bent_src = os.path.join(tmp, "mir_neutered.c")
    text = open(os.path.join(ROOT, "src", "compiler", "mir.c")).read()
    bent = text.replace("if (src->global_name_count > 0) {",
                        "if (0 && src->global_name_count > 0) {", 1)
    check("the neutraliser found the copy it meant to disable",
          "if (0 && src->global_name_count > 0) {" in bent, True)
    open(bent_src, "w").write(bent)
    exe = os.path.join(tmp, "probe_neutered")
    cmd = ([cc] + FLAGS + INCLUDES + ["-o", exe,
            os.path.join(ROOT, "tools", "mir_express_probe.c"), bent_src]
           + [s for s in ENGINE_SRC if not s.endswith("mir.c")] + ENGINE_LIBS)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    check("the neutered probe built", p.returncode == 0, True)
    run = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True)
    neutered = {}
    for line in run.stdout.splitlines():
        m = re.match(r"MIR-EXPRESS-MOVE\|item=([a-z-]+)\|kept=(yes|no)", line)
        if m:
            neutered[m.group(1)] = m.group(2)
    check("a carrier that stops copying is caught", neutered.get("global-names"), "no")

    # The reader/writer columns are a COMPILER reading now, and the three ways the
    # regex reading lied are the three controls -- each one is a way the old column
    # could have printed a number that was not a reading of anything.
    probe_mod = _access_probe()
    access = probe_mod.run_probe(cc, ROOT, CONTAINER_MEMBERS)
    labels = access["members"]["labels"]
    # 1  a NAME is not a TYPE.  compiler.c has its own `labels` -- a LabelDef list
    #    reached through comp->labels -- so a reading that counts the word would
    #    blame the reader.  The const pass must blame the one real write, and it is
    #    the assignment peer named: compiler.c:322, comp->curBC->labels = le.  If a
    #    regex ever comes back, this row turns red before the count does.
    check("the same member name on another struct is not blamed for a write",
          labels["writer"], ["src/compiler/compiler.c"])
    check("the table the runtime really reads is seen as read",
          "src/vm/vm.c" in labels["reader"], True)
    # 2  the instrument must not be in the count it produces.  The access probe
    #    plants a value in every table to measure it, so it touches all nine and
    #    must appear in none of them -- and the hit list proves it touched them
    #    rather than never having been compiled at all.
    check("the instrument is in no reader or writer column",
          [f for m in access["members"].values()
           for f in m["reader"] + m["writer"] if f.startswith("tools/mir_express")], [])
    check("the instrument really did touch the tables it is excluded from",
          all("tools/mir_express_probe.c" in m["self-excluded-hit"]
              for m in access["members"].values()), True)
    # 3  a member that does not exist must be REFUSED, not answered with zero
    #    readers: "there is no such member" and "nothing reads it" are different
    #    facts, and a probe that cannot say the first cannot be trusted on the second.
    try:
        probe_mod.run_probe(cc, ROOT, ["no_such_member_here"])
        refused = False
    except RuntimeError:
        refused = True
    check("a member that does not exist is refused, not counted as unread",
          refused, True)
    # 4  needed-by-aot is a judgement about a SITE, so it must be bendable both
    #    ways by the site alone -- a fold that always answered the same thing would
    #    look identical in the report.
    covered = {"OP_LOADK_STRING"}
    check("a site under an opcode the route has run is a yes",
          probe_mod.needed_by_aot([{"site": "x", "ctx": "L_LOADK_STRING",
                                    "op": "OP_LOADK_STRING"}], covered), "yes")
    check("a site under an opcode the route has never run is a not-yet",
          probe_mod.needed_by_aot([{"site": "x", "ctx": "L_EQK", "op": "OP_EQK"}],
                                  covered), "not-yet")
    check("a site the dispatch table does not cover at all is a not-yet",
          probe_mod.needed_by_aot([{"site": "x", "ctx": None, "op": None}], covered),
          "not-yet")
    check("a table with no runtime site at all is a no",
          probe_mod.needed_by_aot([], covered), "no")
    # And the mapping itself is read off the dispatcher, not guessed from the name:
    # 67 of the 68 opcode rows name a label, so a tree whose table had been gutted
    # would answer differently here rather than silently keeping the same answer.
    check("the dispatcher's own table supplied the label-to-opcode mapping",
          len(probe_mod.dispatch_labels(ROOT)) >= 60, True)
    check("a read site's opcode comes from that mapping, not from its spelling",
          [r["op"] for r in labels["runtime"] if r["ctx"] == "L_RECV"][:1], ["OP_RECV"])

    # The summary's two columns mean two different things, so they are held to
    # that: a dropped table something reads is the alarm, one nothing reads is
    # the quiet column, and the quiet one must be able to be empty instead of
    # being the alarm repeated.  A green computed as red + red_quiet is a column
    # that cannot differ from red -- which is exactly what this check exists for.
    synthetic = [{"item": "alarm", "state": "dropped", "reader": ["src/x.c"]},
                 {"item": "quiet", "state": "dropped", "reader": []},
                 {"item": "kept", "state": "field", "reader": []}]
    r_items, q_items = split_dropped(synthetic)
    check("a dropped table something reads is the alarm column", r_items, ["alarm"])
    check("a dropped table nothing reads gets its own column, not a copy of the alarm one",
          q_items, ["quiet"])
    check("the two columns cannot overlap", sorted(set(r_items) & set(q_items)), [])
    check("a row that is not dropped belongs to neither column",
          len(r_items) + len(q_items), 2)

    print("mir_express self-check: ok (%d checks)" % checks[0])
    return 0


def split_dropped(rows):
    """The dropped rows, split by whether anything still reads the table.

    `red` is the alarm: a table the regeneration drops that something outside
    the carrier reads.  The quiet half is still a finding, but a different one
    -- not carried, rather than breaking something today -- and it is the only
    thing the `green=` column may contain.  Deriving both here, instead of
    appending to one list or the other inside the collection loop, is what lets
    the self-check hold the split to its properties: the columns are disjoint,
    and the quiet one is allowed to be empty.
    """
    red, quiet = [], []
    for row in rows:
        if row["state"] != "dropped":
            continue
        (red if row["reader"] else quiet).append(row["item"])
    return red, quiet


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--gate", action="store_true",
                    help="run the controls AND the pinned measurement, the way the"
                         " registered test does")
    ap.add_argument("--pin", action="store_true",
                    help="record the dropped set this tree has; the test then reads"
                         " CHANGES against it instead of re-litigating the finding")
    ap.add_argument("--self-check", action="store_true",
                    help="prove the field and copy probes can both say no, and that a"
                         " carrier which stops copying is caught; needs no build tree")
    args = ap.parse_args()

    if not os.path.isfile(os.path.join(ROOT, "src", "compiler", "mir.h")):
        print("mir_express: REFUSED|no src/compiler/mir.h; nothing was measured",
              file=sys.stderr)
        return 2

    tmp = tempfile.mkdtemp(prefix="mir_express.")
    try:
        if args.self_check:
            return self_check(args.cc, tmp)
        if args.gate:
            # The gate wants both: the controls prove the probes can fail, the
            # pinned measurement says whether the finding moved.  Both lines are
            # printed so gated_run's two clauses have something real to match,
            # and the stricter verdict wins.
            if self_check(args.cc, tmp) != 0:
                return 1
            gate = True
        else:
            gate = False
        kept, err = move_probe(args.cc, tmp)
        if err:
            print("mir_express: REFUSED|the copy probe would not build: %s" % err,
                  file=sys.stderr)
            print("mir_express: a probe that cannot run is not a reading of zero tables",
                  file=sys.stderr)
            return 2

        # One sweep answers all nine tables: the access probe renames and then
        # const-ifies every member in a throwaway copy of this tree and lets the
        # compiler name the sites.  Two compiler passes over the tree, about two
        # seconds, and no regex anywhere in the reading.
        try:
            access = _access_probe().run_probe(args.cc, ROOT, CONTAINER_MEMBERS)
        except RuntimeError as e:
            print("mir_express: REFUSED|the access probe would not run: %s" % e,
                  file=sys.stderr)
            print("mir_express: a probe that cannot run is not a reading of zero tables",
                  file=sys.stderr)
            return 2
        # Which files were taken out, named once and by name: a set in the source
        # would leave the exclusions to be inferred, and the whole point of this
        # column is that the count is not the instrument counting itself.
        print("MIR-EXPRESS-EXCLUDED|container=(%s)|instruments=(%s)|why=the-container-"
              "defines-the-member-and-a-probe-that-plants-a-value-in-every-table-touches-"
              "every-table"
              % (",".join(_access_probe().EXCLUDED_CONTAINER),
                 ",".join(_access_probe().SELF_EXCLUDED)))

        rows = []
        for item, owner, member, container_member, probe_key in ITEMS:
            if owner is None:
                # DEVIATION 2: the encoding has no int constant pool at all, so
                # neither a field nor a copy is missing -- the pool does not exist.
                rows.append({"item": item, "field": "no", "move": "n/a",
                             "state": "absent", "reader": [], "writer": [],
                             "reader-count": 0, "self-excluded": [],
                             "container-excluded": [], "needed-by-aot": "n/a",
                             "runtime": [], "writes-section": "n/a",
                             "in-memory-uses": 0})
                continue
            info = access["members"].get(container_member)
            if info is None:
                # A missing answer must not read as zero readers.
                print("mir_express: REFUSED|the access probe has no reading for %s" % item,
                      file=sys.stderr)
                return 2
            readers = info["reader"]
            fdef, why = field_probe(args.cc, tmp, owner, member)
            if fdef == "probe-failed":
                print("mir_express: REFUSED|the field probe for %s would not build: %s"
                      % (item, why), file=sys.stderr)
                return 2
            moved = kept.get(probe_key, "unprobed")
            if moved == "unprobed":
                print("mir_express: REFUSED|the copy probe has no answer for %s" % item,
                      file=sys.stderr)
                return 2
            written = written_sections()
            by_format = container_member not in written
            # "Nothing touches it" is now a count of FILES the compiler named, and
            # it is the union of both columns: a table vm.c only WRITES still has a
            # reader's problem waiting for it, and one vm.c only READS was written
            # somewhere else.
            uses = len(set(readers) | set(info["writer"]))
            # state answers ONE question: does the regeneration carry it?  The
            # format fact rides beside it (writes-section=) because collapsing the
            # two loses one of them: by-format=yes means the container FILE never
            # had this section, and a table can be dropped while carrying that
            # fact -- nothing could have copied it.  Merging them into the state
            # would let a reader conclude that fixing the format fixes the
            # regeneration, and it would not.
            if fdef == "yes":
                state = "field"
            elif moved == "yes":
                state = "alias"
            else:
                state = "dropped"
            rows.append({"item": item, "field": fdef, "move": moved,
                         "state": state, "reader": readers, "writer": info["writer"],
                         "reader-count": len(readers),
                         "self-excluded": info["self-excluded-hit"],
                         "container-excluded": info["container-excluded"],
                         "needed-by-aot": info["needed-by-aot"],
                         "runtime": info["runtime"],
                         "writes-section": "no" if by_format else "yes",
                         "in-memory-uses": uses})

        # Cross-column consistency, asserted rather than eyeballed: a table the
        # regeneration carries cannot be one the format does not write, and a
        # dropped table must be one of the two.  A reading whose columns can
        # disagree is a reading that can print a contradiction and exit 0.
        contradictions = []
        for row in rows:
            if row["state"] == "alias" and row["writes-section"] == "no":
                contradictions.append((row["item"], "alias-but-the-format-does-not-write-it"))
            if row["state"] == "dropped" and row["move"] == "yes":
                contradictions.append((row["item"], "dropped-but-the-regeneration-copied-it"))
            if row["state"] in ("field", "alias", "dropped") and row["in-memory-uses"] == 0 \
                    and row["writes-section"] == "no":
                contradictions.append((row["item"], "not-in-the-file-and-nothing-reads-it-either"))
        for item, why in contradictions:
            print("MIR-EXPRESS-CONTRADICTION|item=%s|why=%s" % (item, why))

        covered_ops = set()
        aot_pin_path = os.path.join(ROOT, "tools", "aot_cover.pin.json")
        if os.path.isfile(aot_pin_path):
            covered_ops = set(json.load(open(aot_pin_path)).get("covered_ops") or [])

        for row in rows:
            print("MIR-EXPRESS|item=%s|field=%s|move=%s|state=%s|writes-section=%s"
                  "|reader=(%s)|reader-count=%d|writer=(%s)|self-excluded=(%s)"
                  "|container-excluded=(%s)|needed-by-aot=%s|in-memory-uses=%d"
                  % (row["item"], row["field"], row["move"], row["state"],
                     row["writes-section"],
                     ",".join(row["reader"]) or "none", row["reader-count"],
                     ",".join(row["writer"]) or "none",
                     ",".join(row["self-excluded"]) or "none",
                     ",".join(row["container-excluded"]) or "none",
                     row["needed-by-aot"], row["in-memory-uses"]))
            if row["needed-by-aot"] != "n/a":
                # The answer rides on a SITE: which read site in the runtime, under
                # which dispatch label, and whether the AOT route has ever executed
                # that opcode.  "Yes, one day, when it supports closures" is a claim
                # about the future; "under L_THREAD_START, which the AOT corpus has
                # never run" is a reading of today.
                ops = list(dict.fromkeys(r["op"] for r in row["runtime"]))
                print("MIR-EXPRESS-AOT|item=%s|needed-by-aot=%s|runtime-readers=(%s)"
                      "|covered=(%s)|uncovered=(%s)|basis=%s"
                      % (row["item"], row["needed-by-aot"],
                         ",".join(dict.fromkeys("%s@%s" % (r["site"], r["ctx"] or "loader")
                                                for r in row["runtime"])) or "none",
                         ",".join(o for o in ops if o in covered_ops) or "none",
                         ",".join((o or "loader-outside-the-dispatch")
                                  for o in ops if o not in covered_ops) or "none",
                         "src/vm/vm.c's-own-dispatch-table-[OP_X]=&&L_X-maps-each-read-site-"
                         "to-an-opcode-and-tools/aot_cover.pin.json:covered_ops-says-whether-"
                         "the-AOT-corpus-has-ever-executed-it"))
            if row["state"] == "dropped":
                # The field is format-writes=, not by-format=: "by-format=no" reads
                # as "not the format's doing", which is the opposite of what a
                # writes-section=no means here.
                print("MIR-EXPRESS-WHY|item=%s|format-writes=%s|regeneration-copies=no|effect=%s"
                      % (row["item"], row["writes-section"],
                         "nothing-could-have-copied-it-the-container-format-does-not-write"
                         "-this-section" if row["writes-section"] == "no"
                         else "the-container-format-writes-it-and-the-regeneration-did-not"
                              "-copy-it"))

        width = width_probe(args.cc, tmp)
        if width is None:
            print("mir_express: REFUSED|the block-index width probe would not build",
                  file=sys.stderr)
            return 2
        cap = 2 ** (8 * width - 1) - 1
        print("MIR-EXPRESS-WIDTH|item=block-index|width=%d|max-insns=%d|overflow=refuse"
              "|effect=a-longer-function-is-refused-by-mir_from_bytecode-rather-than-truncated-"
              "so-the-MIR-route-will-one-day-refuse-what-the-AST-route-accepts" % (width, cap))

        counts = {}
        for row in rows:
            counts[row["state"]] = counts.get(row["state"], 0) + 1
        red, red_quiet = split_dropped(rows)
        print("MIR-EXPRESS-SUMMARY|items=%d|field=%d|alias=%d|dropped=%d|absent=%d"
              "|red=%s|green=%s"
              % (len(rows), counts.get("field", 0), counts.get("alias", 0),
                 counts.get("dropped", 0), counts.get("absent", 0),
                 ",".join(sorted(red)) or "none",
                 ",".join(sorted(red_quiet)) or "none"))
        if args.pin:
            write_pin(rows, width)
            return 0
        print("mir_express: items=%d dropped=%d" % (len(rows), len(red)))
        if contradictions:
            print("mir_express: REFUSED|the columns contradict each other (%d row(s)); a"
                  " reading that can print a contradiction is not a reading" % len(contradictions),
                  file=sys.stderr)
            return 2
        if args.json:
            print(json.dumps({"rows": rows, "block_index_width": width}, indent=2))
        # The finding is recorded in the pin, so the verdict is about CHANGE.  With
        # no pin the red stands as it is: an unrecorded loss is exactly the state
        # the first run exists to produce.
        if os.path.isfile(_pin_path()):
            rc = check_pin(rows, width)
        else:
            rc = 1 if red else 0
        return rc
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
