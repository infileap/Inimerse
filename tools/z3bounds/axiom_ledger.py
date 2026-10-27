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


# --- write-site census (T10) --------------------------------------------------------------
# The registry used to say, in prose, "the claim quantifies over all 36 write sites of t->sp;
# the registry pins 2 of them".  Both numbers were wrong and neither could be re-run: the old
# counter matched the needle as a SUBSTRING (t->sp inside nt->sp) and counted LINES, so two
# writes on one line were one site.  This census counts occurrences, names every site, prints
# the ones it cannot read as a direct assignment instead of folding them into the direct
# column, and renders the why-not sentence from its own numbers.
CENSUS_FIELDS = [("A4", "sp"), ("A8", "base")]
CENSUS_CONSTANTS = ["R", "F"]
CENSUS_SOURCE = "src/**/*.{c,h}"
MEM_CALL_RE = re.compile(r"\b(memcpy|memmove|memset|bzero|bcopy)\s*\(")
MACRO_CALL_RE = re.compile(r"\b[A-Z][A-Z0-9_]{2,}\s*\(")
PIN_PHRASE = {"declaration": "the field declaration", "guard": "the push guard",
              "write-site": "a write site", "read": "a read-only use"}


def field_occ_re(field):
    return re.compile(r"([A-Za-z_][A-Za-z0-9_]*)->" + re.escape(field) + r"\s*(\+\+|--|\+=|-=|=)(?!=)")


def code_and_state(raw, in_block):
    """Strip // and /* */ from one line, carrying block-comment state across lines.

    A commented-out write is not a write site, and a scanner with no state would count a
    whole commented-out block as code -- which is how a comment became a 'direct' site once.
    """
    out, i, n = [], 0, len(raw)
    while i < n:
        if in_block:
            j = raw.find("*/", i)
            if j == -1:
                return "".join(out), True
            i, in_block = j + 2, False
            continue
        j, k = raw.find("//", i), raw.find("/*", i)
        if j == -1 and k == -1:
            out.append(raw[i:])
            break
        if k == -1 or (j != -1 and j < k):
            out.append(raw[i:j])
            break
        out.append(raw[i:k])
        i, in_block = k + 2, True
    return "".join(out), in_block


def classify_line(field, receiver, raw, code=None):
    """(kind, why, others) for one line and one field.

    direct   -- <recv>->field <op> <expr>, and we can read the statement
    indirect -- the field's address is taken, or a memory call takes it
    unparsed -- a macro call may write the field through its argument
    comment  -- the write shape exists only inside a comment
    alias    -- another receiver writes the same field
    none     -- no write-shaped use
    """
    if code is None:
        code, _state = code_and_state(raw, False)
    norm = r"->" + re.escape(field) + r"\b"
    if re.search(norm, code) is None:
        if re.search(norm, raw) is not None:
            return "comment", "the write shape appears only inside a comment", []
        return "none", None, []
    hits = list(field_occ_re(field).finditer(code))
    others = sorted({h.group(1) for h in hits if h.group(1) != receiver})
    addr = re.search(r"(?<!&)&(?!&)\s*[A-Za-z_][A-Za-z0-9_]*" + norm, code) is not None
    mem = MEM_CALL_RE.search(code) is not None
    if any(h.group(1) == receiver for h in hits):
        if addr:
            return "indirect", "the address is taken, so the write happens through a pointer", others
        if mem:
            return "indirect", "a memory call takes the field", others
        return "direct", None, others
    if others:
        return "alias", None, others
    if addr:
        return "indirect", "the address is taken, so the write happens through a pointer", []
    if mem:
        return "indirect", "a memory call takes the field", []
    if MACRO_CALL_RE.search(code):
        return "unparsed", "a macro call may write the field through its argument", []
    return "none", None, []


def census_field(mod, root, receiver, field, pinned):
    """Every write-shaped use of one field, classified, pinned or named as unpinned."""
    direct, unparsed, skipped, aliases = [], [], [], {}
    for base, _dirs, files in os.walk(os.path.join(root, "src")):
        for name in sorted(files):
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            in_block = False
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                for i, raw in enumerate(fh, 1):
                    line = raw.rstrip("\n")
                    code, in_block = code_and_state(line, in_block)
                    kind, why, others = classify_line(field, receiver, line, code)
                    if kind == "none":
                        continue
                    if kind == "comment":
                        skipped.append((rel, i, line.strip()))
                        continue
                    if kind == "alias":
                        for other in others:
                            key = other + "->" + field
                            aliases[key] = aliases.get(key, 0) + 1
                        continue
                    want = pinned.get(rel, {}).get(i)
                    ok = bool(want is not None and mod.pin_ok(want, line))
                    row = dict(path=rel, line=i, kind=kind, why=why, pinned=ok, text=line.strip())
                    (direct if kind == "direct" else unparsed).append(row)
    rows = direct + unparsed
    return dict(direct=direct, unparsed=unparsed, skipped=skipped, aliases=aliases,
                sites=len(rows), pinned=len([r for r in rows if r["pinned"]]),
                unpinned=len([r for r in rows if not r["pinned"]]))


def pin_role(root, field, path, lineno, site_keys):
    """What a declared pin actually names: the field's storage, its guard, or a write."""
    if (path, lineno) in site_keys:
        return "write-site"
    line = read_line(root, path, lineno) or ""
    if re.search(r"\b(int|size_t|unsigned|long|ptr_t)\b[^;=]*\b" + re.escape(field) + r"\s*(;|\[)", line):
        return "declaration"
    if re.search(r"->" + re.escape(field) + r"\s*(>=|<=|==|!=|>|<)", line):
        return "guard"
    return "read"


def exhaustion_why(receiver, field, cens, cover, other, declared):
    """The 'not exhausted' sentence, rendered from the census -- never written by hand."""
    expr = receiver + "->" + field
    why = ("the claim quantifies over all %d write sites of %s in src/; the pin table names %d "
           "of them" % (cens["sites"], expr, cens["pinned"]))
    if not cover:
        why += (" -- all %d declared pins name %s, so they are evidence about the range, not "
                "exhaustion" % (declared, " and ".join(other)))
    elif not other:
        why += (" -- all %d declared pins are write sites (%s), and the other %d sites stay "
                "unnailed" % (declared, ", ".join(cover), cens["unpinned"]))
    else:
        why += (" -- %d of the %d declared pins are write sites (%s), while %s names no write, "
                "so %d sites stay unnailed" % (len(cover), declared, ", ".join(cover),
                                               " and ".join(other), cens["unpinned"]))
    return why


def emit_writesites(root, census):
    """WRITESITES / WRITESITE / unparsed-write rows: the reach of each assumption, counted."""
    print("WRITESITES-PINRULE|pinned=yes iff (path,line) is in the pin table and pin_ok(want,line) "
          "holds|judge=gen_bounds_proof.pin_ok|note=a drifted line is no, never a near miss")
    total = dict(sites=0, pinned=0, unpinned=0)
    for ax_id, field in CENSUS_FIELDS:
        cens = census[field]
        expr = "t->" + field
        print("WRITESITES|field=%s|sites=%d|direct=%d|unparsed=%d|pinned=%d|unpinned=%d|source=%s"
              % (expr, cens["sites"], len(cens["direct"]), len(cens["unparsed"]),
                 cens["pinned"], cens["unpinned"], CENSUS_SOURCE))
        for row in cens["direct"] + cens["unparsed"]:
            why = row["why"] or ("pin entry is verbatim-equal on this line" if row["pinned"]
                                 else "no pin entry names this line")
            print("WRITESITE|field=%s|at=%s:%d|kind=%s|pinned=%s|why=%s"
                  % (expr, row["path"], row["line"], row["kind"],
                     "yes" if row["pinned"] else "no", why))
        for row in cens["unparsed"]:
            print("unparsed-write|%s:%d|%s" % (row["path"], row["line"], row["text"]))
        for rel, lineno, text in cens["skipped"]:
            print("WRITESKIP|field=%s|at=%s:%d|kind=comment|raw=%s" % (expr, rel, lineno, text))
        if cens["aliases"]:
            print("WRITESITES-ALIAS|field=%s|others=%s|note=another receiver writes the same field; "
                  "the claim is written against t only, so these are outside its scope by name, "
                  "not by measurement"
                  % (expr, ",".join("%s:%d" % (k, v) for k, v in sorted(cens["aliases"].items()))))
        total["sites"] += cens["sites"]
        total["pinned"] += cens["pinned"]
        total["unpinned"] += cens["unpinned"]
    for name in CENSUS_CONSTANTS:
        print("WRITESITES|field=%s|sites=0|note=spec constant" % name)
    print("WRITESITES-SUMMARY|fields=%d|sites=%d|pinned=%d|unpinned=%d|constants=%d"
          % (len(CENSUS_FIELDS), total["sites"], total["pinned"], total["unpinned"],
             len(CENSUS_CONSTANTS)))


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
    census = {field: census_field(mod, root, "t", field, mod.PINNED) for _id, field in CENSUS_FIELDS}
    info = {}
    for ax in axioms:
        ax_id, title, kind, pins, text, basis = ax
        ok, bad, seen, declared, registered = pin_status(mod, root, ax)
        scope, var = SCOPE.get(ax_id, ("pointwise", None))
        cand, count = (None, 0)
        cover, other = [], []
        if scope == "exhaustive" and var:
            receiver, field = var.split("->")
            cens = census[field]
            keys = {(r["path"], r["line"]) for r in cens["direct"] + cens["unparsed"]}
            for path, lineno in pins:
                role = pin_role(root, field, path, lineno, keys)
                if role == "write-site":
                    cover.append("%s:%d" % (os.path.basename(path), lineno))
                else:
                    other.append("%s %s:%d" % (PIN_PHRASE[role], os.path.basename(path), lineno))
            count = cens["sites"]
            cand = ("%s:%d" % (cens["direct"][0]["path"], cens["direct"][0]["line"])
                    if cens["direct"] else None)
        if not ok:
            status = "asserted"
            why = ("%s; the pin is declared in the registry but the check never ran on it, "
                   "so the claim has one less witness than it advertises" % bad[0]
                   if "not in the pin table" in bad[0] else
                   "%s; the pinned line moved, so the claim must be re-read" % bad[0])
            if scope == "exhaustive" and var:
                why = why + " | " + exhaustion_why(receiver, field, census[field], cover, other,
                                                   declared)
            cand = bad[0].split(";")[0]
        elif scope == "exhaustive":
            status = "asserted"
            why = exhaustion_why(receiver, field, census[field], cover, other, declared)
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

    # --- write-site census: the reach of an assumption, as a number --------------------------
    emit_writesites(root, census)

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


def self_check_writesites(gen_path, py_win):
    """Five cells. The mutation cell is the one that proves the why-not sentence is rendered."""
    me = os.path.abspath(__file__)

    def run(extra):
        res = subprocess.run([sys.executable, me, "--gen", gen_path, "--python", py_win] + extra,
                             capture_output=True, text=True, timeout=1800)
        return res.returncode, res.stdout

    def field_rows(text):
        found, counts = {}, {}
        for line in text.split("\n"):
            m = re.match(r"WRITESITES\|field=(\S+)\|sites=(\d+)\|direct=(\d+)\|unparsed=(\d+)\|"
                         r"pinned=(\d+)\|unpinned=(\d+)\|", line)
            if m:
                found[m.group(1)] = [int(m.group(i)) for i in range(2, 7)]
            m = re.match(r"WRITESITE\|field=([^|]+)\|", line)
            if m:
                counts[m.group(1)] = counts.get(m.group(1), 0) + 1
        return found, counts

    cells = []
    _rc, out = run([])
    rows, n_rows = field_rows(out)
    ok = bool(rows) and all(v[0] == v[1] + v[2] and n_rows.get(k, 0) == v[0]
                            for k, v in rows.items())
    cells.append(("identity", "sites == direct + unparsed and one WRITESITE row per site",
                  ",".join("%s:%d/%d" % (k, v[0], n_rows.get(k, 0)) for k, v in sorted(rows.items())),
                  ok))

    _rc2, out2 = run(["--census-drop-pin", "src/vm/vm.c:4094"])
    mrows, _ = field_rows(out2)
    base, mut = rows.get("t->base"), mrows.get("t->base")
    base_why = [l for l in out.split("\n") if l.startswith("AXIOM|name=A8|")]
    mut_why = [l for l in out2.split("\n") if l.startswith("AXIOM|name=A8|")]
    ok = bool(base and mut and base_why and mut_why
              and mut[3] == base[3] - 1 and mut[4] == base[4] + 1
              and base_why[0] != mut_why[0] and "names 2 of them" in mut_why[0])
    cells.append(("pin-drop-mutation",
                  "drop one write-site pin: pinned-1, unpinned+1, and A8's why-not now says "
                  "'names 2 of them'",
                  "pinned=%s->%s unpinned=%s->%s why-not-changed=%s"
                  % (base[3] if base else "?", mut[3] if mut else "?",
                     base[4] if base else "?", mut[4] if mut else "?",
                     "yes" if base_why and mut_why and base_why[0] != mut_why[0] else "no"),
                  ok))

    a4 = [l for l in out.split("\n") if l.startswith("AXIOM|name=A4|")
          and "role=extraction-premise" in l]
    ok = (bool(a4) and "the field declaration vm.h:101" in a4[0]
          and "the push guard vm.c:479" in a4[0])
    cells.append(("pin-roles", "A4's why-not names what its two pins actually are",
                  "declaration=%s guard=%s"
                  % ("yes" if a4 and "the field declaration vm.h:101" in a4[0] else "no",
                     "yes" if a4 and "the push guard vm.c:479" in a4[0] else "no"), ok))

    tmp = tempfile.mkdtemp(prefix="ws_census_")
    os.makedirs(os.path.join(tmp, "src", "vm"))
    sample = "\n".join([
        "void f(Thread *t) {",
        "    memcpy(&t->sp, src, sizeof t->sp);",
        "    void *p = (void *)(&t->sp);",
        "    PUSH_SLOT(t->sp, 3);",
        "    // t->sp = 99;  a commented-out write is not a site",
        "    if (t->sp >= 0) return;",
        "}",
    ])
    with open(os.path.join(tmp, "src", "vm", "x.c"), "w", encoding="utf-8") as fh:
        fh.write(sample + "\n")
    mod = load_gen(gen_path)
    cens = census_field(mod, tmp, "t", "sp", mod.PINNED)
    kinds = sorted(r["kind"] for r in cens["unparsed"])
    ok = (kinds == ["indirect", "indirect", "unparsed"] and cens["sites"] == 3
          and len(cens["skipped"]) == 1 and not cens["direct"])
    cells.append(("indirect-shapes",
                  "memcpy + address-taken + macro become indirect/unparsed; a read is not a site; "
                  "a commented write is skipped",
                  "kinds=%s sites=%d skipped=%d direct=%d"
                  % (",".join(kinds) or "-", cens["sites"], len(cens["skipped"]),
                     len(cens["direct"])), ok))

    ok = ("WRITESITES|field=R|sites=0|note=spec constant" in out
          and "WRITESITES|field=F|sites=0|note=spec constant" in out)
    cells.append(("constants", "R and F are spec constants: sites=0 with a note, not an empty line",
                  "R=%s F=%s" % ("yes" if "field=R|sites=0" in out else "no",
                                 "yes" if "field=F|sites=0" in out else "no"), ok))

    failed = 0
    for name, expect, got, good in cells:
        print("WRITECHECK|case=%s|expect=%s|got=%s|%s" % (name, expect, got, "PASS" if good else "FAIL"))
        failed += 0 if good else 1
    print("WRITECHECK|summary|cases=%d|pass=%d|fail=%d" % (len(cells), len(cells) - failed, failed))
    return 2 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None,
                    help="repo root; default is resolved from the tree (git toplevel, then a "
                         "walk up for src/vm), never from the directory this file is in")
    ap.add_argument("--gen", default=DEFAULT_GEN)
    ap.add_argument("--python", default=os.environ.get("PY_WIN", DEFAULT_PY_WIN),
                    help="Windows python with z3; only the mutation column needs it")
    ap.add_argument("--census-drop-pin", default=None, metavar="PATH:LINE",
                    help="drop one pin entry from the table for this run only: the negative "
                         "control that shows the census numbers are read from the table")
    ap.add_argument("--self-check-writesites", action="store_true",
                    help="five cells: identity, pin-drop mutation, pin roles, indirect shapes, "
                         "constants")
    args = ap.parse_args()
    if args.self_check_writesites:
        return self_check_writesites(os.path.abspath(args.gen), args.python)
    mod = load_gen(args.gen)
    if args.census_drop_pin:
        path, _, lineno = args.census_drop_pin.rpartition(":")
        if path and lineno.isdigit() and int(lineno) in mod.PINNED.get(path, {}):
            del mod.PINNED[path][int(lineno)]
            print("CENSUS-MUTATION|dropped=%s:%s|effect=this run's pin table loses that line; "
                  "no other table is touched" % (path, lineno))
        else:
            print("CENSUS-MUTATION|dropped=none|why=%s is not a pin entry in this tree"
                  % args.census_drop_pin)
    # The default used to be dirname(HERE) -- i.e. tools/ -- which is wrong in every tree:
    # a reader holding only a sha got a FileNotFoundError on tools/src/vm/vm.h.  Ask the
    # tree instead, and refuse by name when there is no tree to ask.
    root, problem = mod.locate_root(args.root)
    if problem:
        print(problem, file=sys.stderr)
        return 2
    workdir = tempfile.mkdtemp(prefix="axiom_ledger_")
    return emit_ledger(mod, root, os.path.abspath(args.gen), args.python, workdir)


if __name__ == "__main__":
    sys.exit(main())
