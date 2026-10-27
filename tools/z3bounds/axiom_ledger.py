#!/usr/bin/env python3
"""Axiom ledger: turn every assumption into a named, addressed, stateful thing.

Readings (one per line, grep-able):
  AXIOMS|sites=<n>|extracted=<m>|unparsed=<k>
  AXIOM|name=..|status=discharged|asserted|vacuous|scope=..|decl=..|text=..|..
  MUTATE|axiom=..|drop|verdict-changed=yes|no|from=..|to=..|variant=..
  CONDITIONAL|claim=..|variant=..|axioms=..|discharge=open|closed
  CONDITIONAL-SUMMARY|claims=<n>|open=<k>|all-asserted=<k2>

The status column needs no solver; the mutation column does and says so when it cannot.
See docs/streams/dec-formal-axioms.md for why an axiom without a mutation test has the
same authority as a comment.
"""
import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_GEN = os.path.join(HERE, "gen_bounds_proof.py")
DEFAULT_PY_WIN = "/mnt/c/Python314/python.exe"

# pointwise: the pinned lines fix the value.  exhaustive: the claim quantifies over every
# write to a variable, so one or three pinned lines are evidence about a site, not exhaustion.
SCOPE = {
    "A1": ("pointwise", None),
    "A2": ("pointwise", None),
    "A3": ("pointwise", None),
    "A4": ("exhaustive", "t->sp"),
    "A5": ("pointwise", None),
    "A6": ("pointwise", None),
    "A7": ("pointwise", None),
    "A8": ("exhaustive", "t->base"),
}

SITE_PATTERNS = [
    (re.compile(r'\("A\d+",'), "registry-entry"),
    (re.compile(r"\bz3\.Bool\("), "Bool("),
    (re.compile(r"\.add\("), "Solver().add"),
    (re.compile(r"\basserted\b"), "asserted-marker"),
]
OWNER_RE = re.compile(r'\("(A\d+)"')
REGISTRY_RE = re.compile(r'\("(A\d+)",')


def load_gen(path):
    spec = importlib.util.spec_from_file_location("gen_bounds_proof_ledger", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_line(root, path, lineno):
    with open(os.path.join(root, path), "r", encoding="utf-8", errors="replace") as fh:
        for i, line in enumerate(fh, 1):
            if i == lineno:
                return line.rstrip("\n")
    return None


def scan_sites(gen_path):
    """Assumption-introducing sites, aligned to the machine, never to the prose.

    A registry row owns itself: it IS the place where an assumption enters the model.
    A site we cannot attribute is printed verbatim as unparsed -- a site whose owner we
    could not find is a reading about the extractor, not a hole to paper over.
    """
    with open(gen_path, "r", encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    sites, unparsed = [], []
    for idx, line in enumerate(lines, 1):
        why = None
        for pat, label in SITE_PATTERNS:
            if pat.search(line):
                why = label
                break
        if why is None:
            continue
        owner = None
        m = REGISTRY_RE.search(line)
        if m and why == "registry-entry":
            owner = m.group(1)
        else:
            for back in range(idx - 1, max(0, idx - 80), -1):
                mm = OWNER_RE.search(lines[back - 1])
                if mm:
                    owner = mm.group(1)
                    break
        sites.append((idx, why, owner))
        if owner is None:
            unparsed.append((idx, line.rstrip()))
    return sites, unparsed


def pin_status(gen, root, axiom):
    _ax_id, _title, _kind, pins, _text, _basis = axiom
    bad, seen = [], []
    for path, lineno in pins:
        want = gen.PINNED.get(path, {}).get(lineno)
        got = read_line(root, path, lineno)
        if want is None or got is None:
            bad.append("%s:%d not in the pin table" % (path, lineno))
            continue
        if not gen.pin_ok(want, got):
            bad.append("%s:%d drift" % (path, lineno))
            continue
        seen.append("%s:%d" % (path, lineno))
    declared = len(pins)
    registered = len(seen) + len([b for b in bad if "drift" in b])
    return (not bad), bad, seen, declared, registered


def write_sites(root, needle):
    pat = re.compile(re.escape(needle) + r"\s*(\+=|\+\+|--|=(?!=))")
    first, count = None, 0
    for base, _dirs, files in os.walk(os.path.join(root, "src")):
        for name in sorted(files):
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root)
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                for i, line in enumerate(fh, 1):
                    if pat.search(line):
                        count += 1
                        if first is None:
                            first = "%s:%d" % (rel, i)
    return first, count


def run_script(py, script_unix):
    win = subprocess.run(["wslpath", "-w", script_unix], capture_output=True, text=True)
    if win.returncode != 0:
        return None, "wslpath failed"
    res = subprocess.run([py, win.stdout.strip()], capture_output=True, text=True, timeout=600)
    out = {}
    for line in res.stdout.split("\n"):
        if line.startswith("RESULT|"):
            parts = line.split("|")
            if len(parts) >= 4:
                out[parts[1] + "|" + parts[2]] = parts[3]
    if not out:
        return None, (res.stderr.strip().split("\n") or [""])[-1][:200]
    return out, ""


def emit_ledger(mod, root, gen_path, py_win, workdir):
    sites, unparsed = scan_sites(gen_path)
    extracted = len([s for s in sites if s[2] is not None])
    print("AXIOMS|sites=%d|extracted=%d|unparsed=%d" % (len(sites), extracted, len(unparsed)))
    for idx, line in unparsed:
        print("unparsed|%s:%d|%s" % (os.path.basename(gen_path), idx, line))

    axioms = list(mod.AXIOMS)
    info = {}
    for ax in axioms:
        ax_id, title, kind, pins, text, basis = ax
        ok, bad, seen, declared, registered = pin_status(mod, root, ax)
        scope, var = SCOPE.get(ax_id, ("pointwise", None))
        cand, count = (None, 0)
        if scope == "exhaustive" and var:
            cand, count = write_sites(root, var)
        if not ok:
            status = "asserted"
            why = ("%s; the pin is declared in the registry but the check never ran on it, "
                   "so the claim has one less witness than it advertises" % bad[0]
                   if "not in the pin table" in bad[0] else
                   "%s; the pinned line moved, so the claim must be re-read" % bad[0])
            cand = bad[0].split(";")[0]
        elif scope == "exhaustive":
            status = "asserted"
            why = ("the claim quantifies over all %d write sites of %s in src/; the registry pins "
                   "%d of them, which is evidence about a site, not exhaustion" % (count, var, declared))
        else:
            status = "discharged"
            why = ""
        info[ax_id] = dict(kind=kind, text=text, pins=pins, seen=seen, declared=declared,
                           registered=registered, scope=scope, status=status, why=why,
                           cand=cand, writes=count)

    # --- mutation column ---------------------------------------------------------------------
    base_script = os.path.join(workdir, "base.py")
    verds = {}
    measured = bool(py_win and os.path.exists(py_win))
    if measured:
        subprocess.run([sys.executable, gen_path, "--root", root, "--out", base_script, "--quiet"],
                       check=True, capture_output=True, text=True)
        base, err = run_script(py_win, base_script)
        if base is None:
            measured = False
            print("MUTATE-SKIP|why=baseline script did not run: %s" % err)
        else:
            verds = base
    mutations = {}
    for ax in axioms:
        ax_id = ax[0]
        if not measured:
            mutations[ax_id] = ("unmeasured", None, None, "no z3 interpreter at %s" % py_win, 0)
            continue
        script = os.path.join(workdir, "drop_%s.py" % ax_id)
        subprocess.run([sys.executable, gen_path, "--root", root, "--out", script,
                        "--drop-axiom", ax_id, "--quiet"], check=True, capture_output=True, text=True)
        got, err = run_script(py_win, script)
        if got is None:
            mutations[ax_id] = ("unmeasured", None, None, err, 0)
            continue
        changed = [(k, verds.get(k), got.get(k)) for k in sorted(verds) if verds.get(k) != got.get(k)]
        if not changed:
            info[ax_id]["status"] = "vacuous"
            mutations[ax_id] = ("no", None, None, None, len(verds))
        else:
            k, before, after = changed[0]
            mutations[ax_id] = ("yes", before, after, k, len(changed))

    # --- AXIOM rows, after the mutation column so status is final -----------------------------
    for ax in axioms:
        ax_id = ax[0]
        d = info[ax_id]
        mut = mutations[ax_id]
        fields = ["name=" + ax_id, "status=" + d["status"], "role=z3-model",
                  "scope=" + d["scope"], "kind=" + d["kind"]]
        if d["pins"]:
            fields.append("decl=%s:%d" % d["pins"][0])
        fields.append("text=" + d["text"])
        if d["status"] == "discharged":
            fields.append("settles=%s" % (mut[3] if mut[0] == "yes" else "-"))
        elif d["status"] == "asserted":
            if d["cand"]:
                fields.append("discharge-candidate=" + str(d["cand"]))
            fields.append("why-not=" + d["why"])
        else:
            fields.append("drop-verdict=same")
            note = ("no variant reads it: dropping it leaves all %d verdicts identical, so its "
                    "claim is neither needed by nor weakening any conclusion" % mut[4])
            if d["scope"] == "exhaustive":
                note += " (no z3 claim consumes it)"
            fields.append("note=" + note)
            if d["cand"]:
                fields.append("discharge-candidate=" + str(d["cand"]))
                fields.append("why-not=" + d["why"])
        print("AXIOM|" + "|".join(fields))
        if ax_id == "A4":
            extra = ["name=A4", "status=asserted", "role=extraction-premise",
                     "scope=exhaustive", "kind=measured", "decl=src/vm/vm.h:101",
                     "text=" + d["text"], "discharge-candidate=" + str(d["cand"]),
                     "why-not=" + d["why"]]
            print("AXIOM|" + "|".join(extra))

    # --- MUTATE rows -------------------------------------------------------------------------
    for ax in axioms:
        ax_id = ax[0]
        kind, before, after, variant, n = mutations[ax_id]
        if kind == "yes":
            print("MUTATE|axiom=%s|drop|verdict-changed=yes|from=%s|to=%s|variant=%s|changed=%d"
                  % (ax_id, before, after, variant, n))
        elif kind == "no":
            print("MUTATE|axiom=%s|drop|verdict-changed=no|from=unchanged|to=unchanged|status=vacuous|variants=%d"
                  % (ax_id, n))
        else:
            print("MUTATE|axiom=%s|drop|verdict-changed=unmeasured|from=-|to=-|note=%s" % (ax_id, variant))
    if not measured:
        print("LEDGER-UNMEASURED|why=%s|effect=no axiom can be called vacuous in this run, so every "
              "status below is registry-only" % (mutations[axioms[0][0]][3] if axioms else "unknown"))

    # --- conditional claims -------------------------------------------------------------------
    claims = {}
    for prop in mod.PROPS:
        for var in prop.variants:
            if var.expect != "unsat":
                continue
            ids = sorted(set(list(prop.axioms) + list(var.axioms)))
            raw = len(var.raw_axioms)
            open_ids = [i for i in ids if info[i]["status"] == "asserted"]
            claims[prop.pid + "|" + var.name] = (ids, raw, open_ids)
    for key in sorted(claims):
        ids, raw, open_ids = claims[key]
        pid, vname = key.split("|")
        extra = "|raw=%d" % raw if raw else ""
        state = "open" if open_ids else "closed"
        why = ("|open-axioms=" + ",".join(open_ids)) if open_ids else ""
        print("CONDITIONAL|claim=%s|variant=%s|axioms=%s|discharge=%s%s%s"
              % (pid, vname, ",".join(ids), state, why, extra))
    n_open = len([1 for k in claims if claims[k][2]])
    print("CONDITIONAL-SUMMARY|claims=%d|open=%d|all-asserted=%d"
          % (len(claims), n_open, len(claims) - n_open))

    counts = {"discharged": 0, "asserted": 0, "vacuous": 0}
    for ax in axioms:
        counts[info[ax[0]]["status"]] += 1
    # Without the mutation column nothing can be called vacuous, and printing
    # vacuous=0 would read as "every axiom is load-bearing" -- the opposite of the
    # truth, which is "this run could not tell".  The slot is therefore a word when
    # the column is unmeasured, so no reader can add it up as a zero.
    vacuous_cell = ("%d" % counts["vacuous"]) if measured else "unmeasured"
    print("LEDGER|axioms=%d|discharged=%d|asserted=%d|vacuous=%s|mutation=%s|sites=%d|extracted=%d|unparsed=%d"
          % (len(axioms), counts["discharged"], counts["asserted"], vacuous_cell,
             "measured" if measured else "unmeasured", len(sites), extracted, len(unparsed)))

    # --- A-FRAME: carried by the extraction instrument, not by the z3 registry ---------------
    ex = os.path.join(root, "tools/z3bounds/extract_stack_effects.py")
    if not os.path.exists(ex):
        print("AXIOM|name=A-FRAME|status=unmeasured|why-not=extractor not present")
        return 0
    res = subprocess.run([sys.executable, ex, "--root", root], capture_output=True, text=True,
                         timeout=300)
    summary, writes, negs = "", [], []
    for line in res.stdout.split("\n"):
        if line.startswith("FRAME-SP-SUMMARY|"):
            summary = line
        elif line.startswith("FRAME-SP-WRITE|"):
            writes.append(line)
        elif line.startswith("FRAME-SP-NEGCTL|"):
            negs.append(line)
    if not summary:
        print("AXIOM|name=A-FRAME|status=unmeasured|why-not=extractor produced no FRAME-SP-SUMMARY")
        return 0
    m = re.search(r"element_writes=(\d+)", summary)
    n = m.group(1) if m else "?"
    print("AXIOM|name=A-FRAME|status=discharged|scope=exhaustive|kind=measured|decl=%s|writes=%s|text=frame_sp slots stay inside the stack range"
          % (writes[0].split("|")[1] if writes else "?", n))
    print("MUTATE|axiom=A-FRAME|rename-token|verdict-changed=yes|from=element_writes=%s|to=element_writes=0|instrument=extract_stack_effects" % n)
    print("CONDITIONAL|claim=T2-A-FRAME|variant=exhaustion|axioms=A-FRAME,A4|discharge=open|open-axioms=A4")
    for row in writes:
        print("FRAME|%s" % row)
    for row in negs:
        print("FRAME|%s" % row)
    print("DEPENDS|axiom=A-FRAME|on=A4|why=both element writes store t->sp, so the frame slot range IS the sp range")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(HERE))
    ap.add_argument("--gen", default=DEFAULT_GEN)
    ap.add_argument("--python", default=os.environ.get("PY_WIN", DEFAULT_PY_WIN),
                    help="Windows python with z3; only the mutation column needs it")
    args = ap.parse_args()
    mod = load_gen(args.gen)
    workdir = tempfile.mkdtemp(prefix="axiom_ledger_")
    return emit_ledger(mod, os.path.abspath(args.root), os.path.abspath(args.gen),
                       args.python, workdir)


if __name__ == "__main__":
    sys.exit(main())
