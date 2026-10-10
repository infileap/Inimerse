#!/usr/bin/env python3
"""Why this exists (docs/AUDIT.md 1.72, star 11).

`docs/` cites line numbers in two files whose numbers nothing checks:

    CMakeLists.txt   -- moved eight times in one merge series, by eight
                        different offsets (+43 +34 +15 +34 +23 +14 +11 +11)
                        and again by +67.  There is no uniform offset, so
                        "add N" is not a repair; only re-taking by content is.
    tools/gate.sh    -- same shape, and the file itself grew by 195 lines
                        (850 -> 1045) while the 14th and 15th gate stages and
                        the --required-for closure were written, invalidating
                        29 references to it at once.

`tools/gate.sh`'s own numbers are held by the push-stage table and by
`--required-for`.  `CMakeLists.txt`'s numbers were held by nothing.

This checker does not *fix* a number.  It asks whether a number has anything
holding it: a **content anchor** -- some quoted string on the same line that
actually sits at the cited line.  A number with no anchor is reported, not
blocked, because a missing anchor and a wrong number are not distinguishable
without one.

Input set (docs/AUDIT.md 1.72 star 11): a line in docs/**/*.md that names a
target and then carries an inline number.  The writings are counted
separately on every run, and the gate stage asserts that each of them is
still present:

    CMakeLists.txt:426-436          the prefix form, and the range form
    [../CMakeLists.txt](../CMakeLists.txt) ... `:421`
                                    the markdown-link form -- a prefix-only
                                    rule never sees this one, and
                                    docs/AUDIT.md:1569 is exactly this shape
    `CMakeLists.txt` ... `:935`     the name, then a bare number later on the
                                    same line

The denominators are measured and printed on every run, because a sweep that
picks its own input by shape reports "nothing missed" while missing the shape
it was built to find.  Asserting that each writing is still present is what
stops this from quietly degenerating into a prefix scan.

The test: for a number `:N`, the quoted text on the line is looked for at line
N in EVERY file the line names.  That is what decides who owns the number --
not proximity, and not the name nearest the number.  Then:

    held              the target is among the files whose line N carries the
                      quoted text.  The number has something holding it.  This
                      one word covers three different pieces of evidence, and
                      only the first is what a reader assumes it means: `own`
                      (the holding span is the target's own name), `borrowed`
                      (the holding span is another target named on the same
                      line -- what the other references there anchor with), and
                      `picked up` (the holding span is neither: free text on
                      the line that happens to read as a path).  Measured on
                      `89ec77a` with the rule below: of 559 held references, 51
                      are `own`, 58 `borrowed` and 450 `picked up`.  A separate
                      probe over the same commit read 4 of 585 as "held by its
                      own name" -- a stricter reading of `own` (it asks whether
                      the span IS the target's name rather than whether that
                      name appears on the holding line).  Both numbers are on
                      the record and they answer different questions; neither is
                      a correction of the other.  The report prints all three
                      grades, because a number that lives only in a probe is a
                      number the next reader will not see.
    held elsewhere    another file named on the same line carries it.  This
                      says the line really does name a whole file, and nothing
                      more.  It does NOT say the number is right: the span that
                      holds a number is taken once per LINE (see the spans/
                      NUMBER ordering below), so it can belong to a different
                      reference on the same line, and a number past the end of
                      its own file can be held this way.  Measured on
                      `cd29feb`: docs/BOARD.md:64's `:1569` (CMakeLists.txt is
                      1498 lines) is held by a CMakeLists.txt span written in
                      docs/AUDIT.md, and docs/BOARD.md:141's `:1176`
                      (future/README.md is 29 lines) is held by a two-character
                      `src/` span written in docs/STATUS.md.  Both are out of
                      range and both are held.
    out of range      the number is past the end of the file it names.  This is
                      NOT held, and it used to be: the lookup below can only
                      collect a file whose line N exists, and the named file's
                      line N does not, so every out-of-range number landed in
                      the `held elsewhere` bucket BY CONSTRUCTION -- never by
                      evidence, and no reading of this file could tell the
                      difference.  Measured on `89ec77a`: 55 of them under this
                      rule (the number is past the end of the file it names).
                      A separate probe counted 26 there, because it asked about
                      the file the report PRINTS rather than the file the number
                      is attached to -- see the `target` boundary below.  A
                      number that does not exist in the file it names was being
                      certified either way.
    no anchor         nothing named on the line carries it.

The gate stage asserts DENOMINATORS, not anchor rates:

  - the input set is not empty and every writing above is present at least
    once;
  - the explicit-form reference count has a DELTA pin (EXP_LINE_REFS_DELTA,
    against EXP_LINE_REFS_BASE), so it cannot shrink or grow while nobody is
    looking;
  - the unanchored count has a DELTA ceiling
    (EXP_LINE_REFS_UNANCHORED_DELTA_MAX, against the same base), so it cannot
    grow while nobody is looking.  This is the tooth: one more than the base
    reading is an unannounced regression.  Neither reading is repeated here --
    a value written into the sentence that explains the value rots in place,
    and a reading is not a pin.
  - the base is a sha and must be an ancestor of HEAD; if it is not, this
    checker exits 2 rather than report a delta between two trees that are not
    on one line.  It also prints how far behind HEAD the base is, because the
    delta grows as the base ages: that line is how a reader tells "the
    references got worse" from "the base is old".
  - the MIS-LABELLED count (EXP_LINE_REFS_OFF_TARGET_MAX, a ceiling of zero) is
    the one pin that is not about how many references there are.  A reference
    counted in the explicit form whose owner is not a file this checker reads
    means the two halves of this file -- the writing, and the label the report
    prints -- disagree.  That is a different question from the delta pin, and
    the reversed-slice defect needed it: 788 explicit and 416 of them with an
    owner outside TARGETS left every count in this file self-consistent.  A
    pin on the count would have watched it happen.

What those pins do NOT say -- written here rather than in a letter, so that
the claim and the artifact travel together:

  - a delta proves no shrinkage and no growth.  It proves no anchor is right.
  - the delta measures a set that contains the paragraph describing it.
    docs/BOARD.md's line-refs row names CMakeLists.txt and tools/gate.sh and
    carries about a dozen inline numbers, so it is itself in the input set:
    writing that row moves the counts it records.  EXP_LINE_REFS was measured
    at 686 before the row existed, and the row's own tree already read 690 --
    the pin shipped stale by its own subject matter.  This is a fixed point,
    not a slip: the numbers and their description move together or not at all,
    so a change to that row and a re-take are one action.  A merge of main
    moves the readings without anyone touching the row -- 694 -> 720 in one
    merge -- so a reading is not a constant.  "Nobody touched this line" is
    not a reason for "this number did not move": the number is derived from
    the content of the whole tree.  A delta against a base that merge did not
    move says so out loud, and the "N commit(s) behind HEAD" line says how
    much of it is the base ageing rather than this batch.
  - the prefix form is an EQUALITY -- a target name ENDING exactly where the
    number begins -- and it used to be an empty-slice test.  `line[end:
    m.start()]` is silently '' when end > m.start(), so a name to the RIGHT of
    a number satisfied a test written to mean "glued to the left": on 1c6b338
    that was 603 of the 788 prefix-form references, and 416 of those 603 were
    numbers whose owner is some other file.  The count had not merely risen --
    the input set was 46% about the two files it names, and 100% after the fix
    (185 of 185).  An empty slice AND not-reversed is exactly equality, so the
    equality is the whole rule: `end <= m.start()` is a different rule, and it
    would have taken in 301 more -- names strictly to the left, where the old
    test read a non-empty slice and said no.
  - only a file git tracks can hold a number.  The working tree also holds
    build output, and a verdict that changes depending on whether someone has
    run a build is not a verdict about the repository: on one commit this read
    520 held / 515 unanchored with build/ present and 514 / 521 in a checkout
    of the same commit, so six numbers were held by an artifact the clone does
    not have.  check_links.py was fixed for the same defect when it walked the
    directory instead of asking git (docs/AUDIT.md 1.66).  tracked() closed it
    here: a clean checkout of 8bfe8a2 reads the same 523 held / 530 unanchored
    as the tree it was cloned from, because a number can only be held by a path
    git lists.
  - "has an anchor" is not "the number is right".  The rule is "some quoted
    text on the citing line sits at line N of a file that line names", so a
    match proves the number is HELD by content, not that it points at the
    right object.  A number that is still there and now points at something
    else -- the :673 shape agent4 found this round -- is not caught here.
  - splitting a block longer than MAX_PARA lines into single lines is a
    heuristic.  docs/BOARD.md's 93-line table with no blank line in it is why
    the heuristic exists, and is also its boundary.
  - the anchor is *found*, not *declared*: a non-match is not evidence that a
    number is wrong, only that nothing on that line can test it.
  - the input set is a lower bound.  A reference whose file name sits on an
    earlier line, or a bare number whose line names no file, is out of scope
    even when it is a true reference.
  - document-to-document citations (docs/AUDIT.md:<N>, and tools/ citing
    docs/) are a third kind and are NOT in scope here.
  - the reporting unit is the PARAGRAPH, not the reference, because the same
    stale value is usually cited more than once a few lines apart -- three
    such pairs are already measured (docs/AUDIT.md:713 fixed :383 while :453
    kept it; :3330 dropped an ordinal while :3326 kept it; :1659 fixed :935
    while :1657, two lines above, kept :915).
  - `target` in --report is a LABEL, and it uses the same position rule the
    owner does: the last tracked name ENDING at or before the number.  So it
    names the file the number belongs to, whether or not that file is one of
    TARGETS.  It used to fall back to the first target file mentioned anywhere
    on the line, which printed a name sitting to the RIGHT of the number as its
    owner: on 1c6b338 that was 302 no-anchor rows whose label's name sits to
    the right of the number, and 429 whose label is not the owner at all.  No
    count moved for that one, which is why it survived -- it reads like a
    reading.
    docs/BOARD.md's `src/parser/parser.c:1286`/`:1287` was reported as
    `target=tools/gate.sh`, whose 1045 lines made the report read "out of
    range" -- while the file those numbers name has 1832 lines and holds them.
    This misled a reader of the report once (the report's author), who passed
    the number on as out of range.  The label is now the file the numbers name,
    so that reading cannot happen again.
  - two rules that look reasonable were tried on this repository and rejected:
    "the file named nearest before the number owns it" (240 references
    unresolved) and "the quoted text nearest the number anchors it" (held fell
    to 50).  Both die the same way -- a documentation line interleaves several
    file names, several numbers and several quoted spans, and neither
    proximity rule can tell which belongs to which.

The pin was taken at 56bf4c5 (720 / 521) and this branch's own six commits
after it -- acdaf5f, 754d5ff, b92e7d1, e43c1cb, 34499bf, 6de3d9a -- moved
NEITHER number: 6de3d9a reads 720 / 521, exactly what 56bf4c5 read.  A pin does
not expire because its author kept working.  It expired in the MERGE, and the
account of that merge is what makes re-taking it different from resetting it
to today's reading:

    commit    what it is                    explicit  held  unanchored
    56bf4c5   where the pin was taken       720       514   521
    6de3d9a   this branch's tip, pre-merge  720       514   521
    9969e5b   main at the merge             730       543   506
    8bfe8a2   the first merge               734       523   530
    cfcb19e   tip before the T2 merge       734       541   530
    807e0e4   the T2 merge                  750       543   548
    c3ea2b6   where the pin was re-taken    750       543   548
    3f3e5b9   merge origin/main (fb18bd7)  762       552   551
    (rewrite) nine DECFY_DESIGN.md:<N>     758       550   549
              rewritten as section numbers
    f401f82   merge origin/main (2d0ecd8)  758       551   551
    c3f7857   merge origin/main (68248e4)  758       551   551
    (account) the account written          761       553   552

  - the delta 720 -> 734 is +14, and it is not this branch's work: +4 are this
    branch's own four spelling examples on docs/BOARD.md's line-refs row (main
    does not have that row, so relative to main they are new; relative to this
    branch they are as old as the pin), and +10 are main's later documentation
    commits, which this branch had never seen (docs/BOARD.md +9,
    docs/DECFY_DESIGN.md +1).  No number was re-taken to reach 734.
  - the delta 521 -> 530 is +9, and those are the same main-side references
    read against this branch's tools/gate.sh: 9 of main's 10 new references
    were written for main's 850-line file and have nothing holding them in
    this branch's 1045-line one.
  - per file, unanchored (main / this branch / merged): docs/AUDIT.md 119 /
    139 / 139, docs/BOARD.md 197 / 193 / 198, docs/DECFY_DESIGN.md 71 / 68 /
    72, docs/STATUS.md 93 / 95 / 95.  docs/AUDIT.md's +20 is the whole point:
    this branch never edited that file -- it edited the file that file cites.
  - the same merge seen from main's side instead of this branch's: base
  - The T2 merge (`cfcb19e` -> `807e0e4`, main = `fa8247e`) is a second and
    independent shift, and it moved the *other* file: `CMakeLists.txt` grew by 17
    lines (an insert near line 1357, `add_test(NAME xrange_t2_runtime ...)`) while
    `tools/gate.sh` changed one line and **kept its line count**.  explicit
    734 -> 749 and unanchored 530 -> 550 when that merge landed (750 / 548 once this
    branch's own account of it was written); +15 explicit arrive
    with T2 (13 in `docs/BOARD.md`, 2 in the new `docs/streams/win-source-attribution.md`),
    and +20 unanchored split by target into 18 into `CMakeLists.txt` and 2 into
    `tools/gate.sh`.  Those 2 are the instructive ones: they were held before T2 not
    by the file the report labels as their target -- those two numbers name
    `src/parser/parser.c`, which is 1832 lines and holds them; the label says
    `tools/gate.sh`, and a label is not an identity -- but by
    `src/compiler/compiler.c`, named on the same line, which T2 edited.  So "does this
    reference have an anchor" and "did the file it names move" are two different
    questions, and the three causes (gate.sh shift / CMakeLists shift / anchor living
    elsewhere) are told apart by reading `hits`, not by diffing.  Writing this
    paragraph then moved the count again by itself: explicit +1, because a file name
    immediately followed by a number (`docs/TYPESET_V06.md` +1) is read as a citation,
    and unanchored -2, because the fragments it quotes hold two numbers on that row
    that nothing held before -- which is why the pins below were taken after the
    prose, not before it.
  - the third shift is the merge of `origin/main` = `fb18bd7`, which was
    announced as pin-neutral and is not: explicit 750 -> 762 and unanchored
    548 -> 551.  All twelve explicit sit on ONE appended row of `docs/BOARD.md`
    (the rewritten `xrange-t2` row, commit 3e0a9bd, which arrived from the
    other side), while `docs/AUDIT.md` grew 39 lines in the same merge and
    added not one reference.  The addendum that says it is pin-neutral is
    pin-neutral; a row that names both target files is not.
  - the fourth shift is this branch's own work on the citations it was given:
    nine `docs/DECFY_DESIGN.md:<N>` references became section numbers (the
    §2(a) "value representation + collection runtime" row, §3.3 item 2, §1.1
    table row 3), taking explicit 762 -> 758 and unanchored 551 -> 549.  Only
    four of the nine sat on a line that names a target file -- the writings
    count counts only those -- and two of the four had no anchor.  Line counts
    did not move: 3/3, 4/4, 2/2 and 2/2 substituted lines.
  - the fifth shift is the merge of `origin/main` = `2d0ecd8`, which brought a
    new file instead of moving an old one: `docs/streams/builtin-platform-
    census.md` (104 lines) carries 3 references and 2 of them have no anchor
    (`:2205` and `:2927` on its line 55; the third, `:241`, is held), while one
    anchor was gained in `docs/AUDIT.md` and one lost in
    `docs/TYPESET_V06.md`.  explicit stayed 758, unanchored 549 -> 551.
  - the sixth shift is the merge of `origin/main` = `68248e4`, and it is
    neutral: 1102 references, 758 / 551 / 551 before and after.
  - the pins above were taken AFTER the account was written, and writing the
    account moved them: 758 / 551 before it, 761 / 552 after.  The account is a
    paragraph inside the measured set, so "write the account" and "take the
    pin" are one action, and the second of those numbers holds only until the
    next person quotes either target file.
    9969e5b 1049 references / 506 unanchored -> merged 1053 / 530.  30
    references that had an anchor lost it, 10 gained one by coincidence, and 4
    are new.  Of the 30, 29 point into tools/gate.sh and 1 into docs/BOARD.md.
  - the 10 that gained an anchor are coincidence, not repair: 6 are held by
    text that happens to sit at that number now (tools/gate.sh and
    docs/BOARD.md both moved), and 4 are held by the note added at
    CMakeLists.txt:1044-1047, which quotes the very lines two of them cite.
    A gained anchor is not evidence that the number is right.
  - the 30 are listed, not silently re-taken.  Which stage inserted the lines:
    tools/gate.sh grew 850 -> 1045 across c19709b (+34/-3, the 14th stage),
    29d9459 (+40/-3, the 15th stage) and 50134e1 (+21/-7, the --required-for
    closure); e3e84fe and 56bf4c5 did not move it.  docs/BOARD.md grew by 2
    from its 61st line (two stage rows).  The list, as (file:line  :number
    owner):

    docs/AUDIT.md (24):  :3359 :54 gate.sh, :3375 :92 gate.sh x2,
      :2476 :148 gate.sh, :3373 :148 gate.sh, :3375 :148 gate.sh,
      :2476 :168 gate.sh, :3373 :168 gate.sh, :3375 :168 gate.sh,
      :3337 :356 gate.sh, :3373 :364 check_async_commands.py,
      :3375 :364 gate.sh, :3359 :375 gate.sh, :3359 :424 gate.sh,
      :3373 :424 check_async_commands.py, :3375 :463 gate.sh,
      :3026 :474 check_orphan_fixtures.py, :3375 :474 gate.sh,
      :3359 :625 gate.sh, :3509 :675 gate.sh, :3376 :676 CMakeLists.txt,
      :3559 :766 gate.sh, :3368 :768 gate.sh x2
    docs/BOARD.md (3):  :116 :16 gate.sh, :126 :210 src/compilation/aot_native.c,
      :277 :560 src/runtime/runtime.c
    docs/STATUS.md (2):  :2574 :69 docs/BOARD.md, :1096 :159 gate.sh
    docs/DECFY_DESIGN.md (1):  :320 :513 gate.sh

  - why accepted rather than repaired here: two of those files are not this
    branch's to edit (docs/AUDIT.md is the coordinator's, docs/DECFY_DESIGN.md
    is agent4's), and substituting a fresh number is the repair
    docs/SYNTAX.md H4 forbids.  So the pin is re-taken WITH this account, and
    the list is handed to the files' owners.  Re-taking it silently is what
    this note exists to make impossible.

Negative control -- redo it, and note that the first version was worthless:

    sed -i '426s|.*|# NEGATIVE CONTROL line|' CMakeLists.txt
    python3 tools/check_line_refs.py
        held 553 -> 547, unanchored 552 -> 558 -- six references move, and the
        stage goes red on the ceiling, not on a rate
    git checkout -- CMakeLists.txt

The first version of this control replaced the line with

    # NEGATIVE CONTROL: this line used to be if(WIN32)

and moved NOTHING, even though `git diff` showed the edit had landed: the
replacement text still contained the string being tested, so the mutation
landed in the file and not in the thing under test.  Confirming that an edit
landed is necessary, not sufficient -- confirm it landed in the measured
object.

The control this pin exists for is on the MERGE, not on a line: a
tools/gate.sh:<N> reference goes from green to red when that file's content
moves underneath it, and nothing on the citing side changes.

    cp tools/gate.sh .scratch/gate.sh.bak
    python3 tools/check_line_refs.py --report | grep -c ':54 (tools/gate.sh)'
        12 -- and all twelve show THIS tree's gate.sh:54, which is
        '#                              asked, and the bracket is where t',
        a line none of them quotes                          -- red
    git show 9969e5b:tools/gate.sh > tools/gate.sh        # the pre-merge file
    git diff --numstat -- tools/gate.sh                   # 33 228: it landed
    python3 tools/check_line_refs.py --report | grep -c ':54 (tools/gate.sh)'
        11 -- one of the twelve quotes the line now there    -- green
    python3 tools/check_line_refs.py
        held 553 -> 575, unanchored 552 -> 530            -- under the ceiling
    cp .scratch/gate.sh.bak tools/gate.sh

The aggregate, not the single count, is what moves: 22 references are held by
the pre-merge file and not by this tree's.  The one `:54` case this control was
first written around (docs/AUDIT.md:3359, which quotes
`BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"` and records the commit it measured
on, "全部在 `4ca013d` 上量") is not in the report any more -- that file grew,
and the citation with it.  Recording an observation basis does not hold a
number; only an anchor does.  That is this file's whole argument in one line.

Usage:
    python3 tools/check_line_refs.py             assert the pins; exit 1 if a
                                                 pin moved
    python3 tools/check_line_refs.py --report    also list every unit with
                                                 unanchored references
    python3 tools/check_line_refs.py --strict    exit 1 if ANY reference has
                                                 no anchor (a diagnostic, not
                                                 the gate's mode: today that
                                                 is every one of them)
"""
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Files whose line numbers docs/ cites.  Each is here for a stated reason, not
# because of its name.
TARGETS = {
    "CMakeLists.txt": "moved eight times by eight offsets; cited throughout docs/",
    "tools/gate.sh": "grew by 195 lines in one merge, invalidating 29 citations",
}

# Pins in the sense of tools/gate.sh's EXP_CTEST: neither may move without
# someone saying so in a commit message.
#
# Both pins are DELTAS against a named base tree, not counts of this tree.  A
# count is a property of the tree, and swapping the file every one of those
# numbers points into resets it wholesale: the merge of stream/pin-shift into
# main moved the readings 764/539 -> 768/561 in one action, +4 explicit / +22
# unanchored of it from that branch's rewrite of tools/gate.sh (227 ins / 32
# del), so every `tools/gate.sh:<N>` in docs/ lost its anchor at once and
# nothing on the citing side changed.  A ceiling a merge can raise by 22
# measures that merge, not the batch of references it was meant to hold down.
#
# A delta needs a tree that does not move, and a named ref moves -- "base" would
# then mean a different tree every day.  So the base is a sha, this checker
# asserts it is an ancestor of HEAD, and it prints how far behind HEAD the base
# is: the delta grows as the base ages, and that line is how a reader tells "the
# references got worse" from "the base is old".  Re-taking the pins is moving
# the base, in a commit that says so; nothing else may move it.
# The base is main's 1c6b338, not this branch's 8c75f31.  The branch was cut
# before main grew the 1.74 增补三 references and before main merged the
# delta-form pin itself, so measuring it against its own base would have
# reported main's growth as this branch's delta -- a delta against a base
# the branch never contained is a delta about the base's age, which is
# exactly what the "is N commit(s) behind HEAD" line exists to distinguish.
EXP_LINE_REFS_BASE = os.environ.get("EXP_LINE_REFS_BASE", "1c6b3381f3db8b77ca90c53ae5737dfa3698624f")
# +14, and the reason is two readings rather than a mood.  This branch first
# REMOVED one explicit-form reference -- docs/STATUS.md 51 carried
# `tools/gate.sh:54`, a line number that had stopped pointing at the constant
# it named -- and a removal is a change the pin has to be told about in the
# same commit that makes it: -1.  It then ADDED a file,
# docs/streams/v06-decision-points.md, whose 15 explicit-form citations of
# CMakeLists.txt lines each carry the test name that sits on the cited line:
# +15.  Net +14.  A reference removed is not a reference gained, and a
# reference added is not one either.
EXP_LINE_REFS_DELTA = int(os.environ.get("EXP_LINE_REFS_DELTA", "12"))
# +1, not 0, and the reason is a reading rather than a mood.  docs/BOARD.md's
# line-refs row carries `:241 (CMakeLists.txt)`, and what held that number was
# the fragment `docs/` -- which sat on line 241 of tools/check_line_refs.py,
# a file that row merely names, because that line read "... edit
# (docs/AUDIT.md is the coordinator's, docs/DECFY_DESIGN.md ...".  A hit is a
# coincidence of text, and rewriting the docstring above moved the coincidence
# off line 241, so this reference lost an anchor it never really had.  The
# ceiling says so instead of hiding it: one more number now has nothing
# holding it, and nothing else may join it.
#
# Re-taken 1 -> 2 by the census wiring (`a121aa1`), and the reason is a
# mechanism worth naming: that commit kept `CMakeLists.txt` at 1483 lines
# exactly so that `docs/**` references would not move -- and the preservation
# is what killed the anchors.  It rewrote the TEXT of 66 lines, and docs quote
# those lines.  Five references quote the first word of a command the wrapper
# was inserted in front of (`CMakeLists.txt:676`, `:756`, `:1113` quoted
# `COMMAND inimerse ...`; `:1355` twice quoted `COMMAND inimerse --no-mods ...`),
# and four quote `CMakeLists.txt:1358`, a line whose only content was
# `PASS_REGULAR_EXPRESSION "count-ok ..."` -- the property came out in place, the
# line stayed to keep the count, and the anchor is now the empty string.  So the
# number did not move and the reference still points at `:1358`; the line is
# still there and it now says nothing.
#
# And then the ceiling did not have to move after all: the row of
# `docs/BOARD.md` that RECORDS the wiring re-anchored the very references it
# describes, taking the reading back to base +0 -- which is the fixed point of
# the paragraph above, happening to the sentence that explains it.  The ceiling
# stays at 1: a value that only holds because a description sits on the thing
# it describes is not a value to widen a pin around.
# ---------------------------------------------------------------------------
# 1 -> 12.  Re-taken in the same commit as the change that moved it, and taken
# on the MERGE RESULT rather than on the branch tip.
#
# Why the merge result: EXP_LINE_REFS_BASE is a fixed sha, so the delta runs
# from that sha to HEAD -- and HEAD on a branch is not the tree the merge will
# produce.  A ceiling read on a branch is a reading of a tree that will not
# exist; when it then fails after the merge it looks like the person who took
# it was wrong.  (Measured, on docs/AUDIT.md's own ceiling: 156 on one branch
# tip, 154 two edits later, 162 two edits after that, 211 on the merge result.
# Four readings, four ordinary docs/ edits, and the last one was taken in the
# merge commit for exactly this reason.)
#
# What moved it: the strict/coercing split of val_as_double()/val_as_int(),
# the arity check in posix_random(), and the comments that go with them.
# src/vm/vm.c and src/runtime/runtime_posix.c both grew, so docs/ references to
# their lines stopped landing on the text they quote.  Reading at the merge
# result: unanchored 574 (base 562), explicit 185 (base 173) -- so the explicit
# delta pin is unmoved at +12 and this one carries the whole cost.
#
# The number is +12 and NOT the +8 predicted on the branch tip, because the
# explanation for the two set_contains() reads was written after that
# prediction.  Where it was written was chosen by measurement: docs/ cites 168
# distinct lines of src/vm/vm.c and 101 of them sit at or above those two reads
# (the highest is :5316), so the block went after the last line of the file,
# where it costs nothing.  Verified: stripping the block and re-reading gives
# the same number.  (It read 575 before the last two comment edits and 574
# after, which is why the value written here is the one measured after them.)
#
# A ceiling, not an equality: it says "this much unanchored growth is a debt
# somebody wrote down", and the debt is paid by fixing the references, not by
# lowering the number to whatever today happens to be.
EXP_LINE_REFS_UNANCHORED_DELTA_MAX = int(
    os.environ.get("EXP_LINE_REFS_UNANCHORED_DELTA_MAX", "12")
)

# The one pin here that is not about how many references there are, but about
# what the report says each one is.  A reference counted in the explicit form
# (so: a target file name ends where the number starts) whose OWNER -- the last
# tracked name ending at or before the number -- is not a target at all is a
# reference the two halves of this file disagree about.  It must be zero.
#
# This is the assertion the reversed-slice defect needed and did not have.  On
# 1c6b338, with `line[end:m.start()] == ""` standing in for `end == m.start()`,
# 788 references were counted explicit and 416 of them had an owner outside
# TARGETS -- and every other count in this file was consistent with that, which
# is why the shape survived: it reads like a reading.  The `explicit` delta pin
# asks whether the cited form grew; this one asks whether the report is still
# naming the right file, and a change can fail either one alone.
EXP_LINE_REFS_OFF_TARGET_MAX = int(
    os.environ.get("EXP_LINE_REFS_OFF_TARGET_MAX", "0")
)

# Every writing that must still be in the input set.  A prefix scan satisfies
# the first two and none of the others, which is exactly the failure this list
# exists to catch.
WRITINGS = ("prefix", "prefix-range", "link+bare", "bare")

PATHISH = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]{1,6}")
NUMBER = re.compile(r":(\d+)(?:-(\d+))?")
SPAN = re.compile(r"`([^`\n]+)`")
PURE_NUMBER = re.compile(r"^:?\d+(?:-\d+)?$")
LINK = re.compile(r"\[[^\]]*\]\([^)]*\)")
MAX_PARA = 20

class Tree:
    """One tree's files: the working tree, or the objects of a commit.

    The pins are deltas against a named base, so this checker takes the same
    reading twice.  Everything the scan reads goes through here for that reason:
    a second code path for the base would be a second definition of the
    measurement, and the two would drift apart exactly when the delta mattered.
    """

    def __init__(self, rev: str | None = None):
        self.rev = rev
        self._listing: set[str] | None = None
        self._cache: dict[str, list[str] | None] = {}
        self._batch = None

    def _git(self, *args: str) -> str:
        return subprocess.run(
            ["git", "-C", str(REPO_ROOT), *args],
            capture_output=True, text=True, check=True,
        ).stdout

    def _tracked(self) -> set[str]:
        if self._listing is None:
            out = (
                self._git("ls-files", "-z")
                if self.rev is None
                else self._git("ls-tree", "-r", "--name-only", "-z", self.rev)
            )
            self._listing = set(out.split("\0"))
        return self._listing

    def tracked(self, rel: str) -> bool:
        return rel in self._tracked()

    def text(self, rel: str) -> str:
        """Content, decoded the way the working-tree read decodes it."""
        if self.rev is None:
            return (REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
        return self._blob(rel).decode("utf-8", "replace")

    def docs(self) -> list[str]:
        return [p for p in self._tracked() if p.startswith("docs/") and p.endswith(".md")]

    def lines(self, rel: str) -> list[str] | None:
        if rel not in self._cache:
            self._cache[rel] = self.text(rel).splitlines() if self.tracked(rel) else None
        return self._cache[rel]

    def _blob(self, rel: str) -> bytes:
        """One object from the base tree, over a single `git cat-file --batch`."""
        if self._batch is None:
            self._batch = subprocess.Popen(
                ["git", "-C", str(REPO_ROOT), "cat-file", "--batch"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            )
        spec = f"{self.rev}:{rel}".encode()
        self._batch.stdin.write(spec + b"\n")
        self._batch.stdin.flush()
        header = self._batch.stdout.readline().decode("utf-8", "replace").split()
        if len(header) < 3 or header[1] != "blob":
            sys.exit(
                f"check_line_refs: {spec.decode()} is not a blob in {self.rev}; "
                "refusing to guess what the base tree holds."
            )
        size = int(header[2])
        data = self._batch.stdout.read(size)
        self._batch.stdout.read(1)
        return data


# The tree the scan reads.  main() swaps this to take the base reading.
TREE = Tree()


def tracked(rel: str) -> bool:
    return TREE.tracked(rel)


def lines_of(rel: str) -> list[str] | None:
    return TREE.lines(rel)


def docs_markdown() -> list[str]:
    return TREE.docs()


def file_mentions(line: str) -> list[tuple[int, int, str]]:
    """(start, end, path) for every tracked repo file named on this line."""
    found = []
    for m in PATHISH.finditer(line):
        token = m.group(0).rstrip(".")
        if token in TARGETS or TREE.tracked(token):
            found.append((m.start(), m.end(), token))
    return found


def anchors(line: str) -> list[str]:
    """Quoted strings on the line that could anchor a number."""
    out = []
    for span in SPAN.findall(line):
        span = span.strip()
        if not span or len(span) <= 2 or PURE_NUMBER.match(span):
            continue
        if any(t + ":" in span for t in TARGETS):
            continue
        out.append(span)
    return out


def classify(line: str, mentions: list[tuple[int, int, str]], m: re.Match) -> str:
    """Which writing this reference uses, judged by where a TARGET name sits.

    A target glued to the number is the prefix form; a target inside a
    markdown link with the number later on the line is the link form; a target
    named anywhere else on the line with the number later is the bare form.
    Which of them OWNS the number is not decided here -- the content anchor
    decides that, because the nearest name is the rule that was tried and
    rejected.
    """
    for _start, end, token in mentions:
        # `end == m.start()`, not `line[end:m.start()] == ""`.  A reversed slice
        # is silently empty in Python, so the empty-string test was true for a
        # TARGET name sitting to the RIGHT of the number as well -- the test
        # said "glued to the left" and answered "somewhere on the line".
        # Measured on 1c6b338: 603 of the 788 prefix-form references were held
        # up ONLY by a name to their right, and 416 of those 603 were numbers
        # whose real owner is some other file.  An empty slice AND not-reversed
        # is exactly equality, so the equality is the whole rule.
        if token in TARGETS and end == m.start():
            return "prefix-range" if m.group(2) else "prefix"
    links = [(x.start(), x.end()) for x in LINK.finditer(line)]
    for start, _end, token in mentions:
        if token in TARGETS and any(a <= start < b for a, b in links):
            return "link+bare"
    return "bare"


def paragraphs(text: str) -> list[tuple[int, int, list[str]]]:
    """(first line, last line, lines) per run of non-blank lines, 1-based.

    A run longer than MAX_PARA lines is yielded line by line: docs/BOARD.md
    holds a 93-line table with no blank line in it, and reading that as one
    paragraph carries 406 references in a single unit.
    """
    out, buf, start = [], [], None
    for n, line in enumerate(text.splitlines(), 1):
        if line.strip():
            if start is None:
                start = n
            buf.append(line)
        elif buf:
            out.append((start, n - 1, buf))
            buf, start = [], None
    if buf:
        out.append((start, start + len(buf) - 1, buf))
    flat = []
    for first, last, lines in out:
        if len(lines) <= MAX_PARA:
            flat.append((first, last, lines))
        else:
            for offset, line in enumerate(lines):
                flat.append((first + offset, first + offset, [line]))
    return flat


def scan() -> dict:
    """Collect every reference and its verdict."""
    writings = {w: 0 for w in WRITINGS}
    explicit = 0
    held = 0
    held_elsewhere = 0
    held_own = 0
    held_borrowed = 0
    held_picked = 0
    held_rows: list[tuple] = []
    no_anchor: list[tuple] = []
    off_target: list[tuple] = []
    out_of_range: list[tuple] = []
    mention_lines = 0
    inline_lines = 0
    files: set[str] = set()

    for rel in docs_markdown():
        text = TREE.text(rel)
        for first, last, lines in paragraphs(text):
            for offset, line in enumerate(lines):
                lineno = first + offset
                mentions = file_mentions(line)
                if not mentions:
                    continue
                if any(tok in TARGETS for _s, _e, tok in mentions):
                    mention_lines += 1
                spans = anchors(line)
                refs_here = 0
                names_a_target = any(tok in TARGETS for _s, _e, tok in mentions)
                line_refs = []
                for m in NUMBER.finditer(line):
                    number = int(m.group(1))
                    if number < 1 or not names_a_target:
                        continue
                    owner = None
                    for _start, end, token in mentions:
                        if end <= m.start() + 1:
                            owner = token
                    line_refs.append((m, number, owner))
                # Which files' line N carries a span, for every reference on this
                # line, computed before anything is counted.  Grading a held
                # reference needs to know whether the span that held it also
                # holds a DIFFERENT reference on the same line, and that is not
                # knowable one number at a time.
                holders = {}
                for m, number, _owner in line_refs:
                    h = []
                    for _s, _e, token in mentions:
                        target_lines = lines_of(token)
                        if target_lines and 1 <= number <= len(target_lines):
                            if any(s in target_lines[number - 1] for s in spans):
                                h.append(token)
                    holders[m.start()] = h
                for m, number, owner in line_refs:
                    refs_here += 1
                    files.add(rel)
                    writing = classify(line, mentions, m)
                    writings[writing] += 1
                    if writing.startswith("prefix"):
                        explicit += 1
                        # The count and the label are two different pieces of
                        # code -- `classify` decides the writing, the loop above
                        # decides the owner -- and this is the number that says
                        # they still agree.  It is the one that would have caught
                        # the reversed-slice shape: while `line[end:m.start()]`
                        # stood in for "the name ends where the number starts",
                        # every number with a target name to its RIGHT was
                        # counted here, and 416 of the 788 had an owner that is
                        # not a target at all.  No other count moved for that.
                        if owner not in TARGETS:
                            off_target.append((rel, lineno, number, owner))
                    hits = holders[m.start()]
                    # The number does not exist in the file it names.  It must
                    # not be counted as held: the lookup that fills `hits` can
                    # only collect a file whose line N exists, and the owner's
                    # line N does not, so an out-of-range number lands in the
                    # "held elsewhere" bucket BY CONSTRUCTION, never by
                    # evidence, and no reading of this file could tell the
                    # difference.  Measured on 89ec77a: 26 of them, all in that
                    # bucket.  docs/BOARD.md:64's `:1569` against CMakeLists.txt
                    # (1533 lines) is one.
                    #
                    # It displaces `held` ONLY.  A number with no anchor at all
                    # is still reported as unanchored -- that is the diagnostic
                    # that says the number will move, and it is a different
                    # question from whether the number exists.  Moving those out
                    # of `no_anchor` too would have silently re-based the
                    # unanchored delta, which is pinned.
                    owner_lines = lines_of(owner) if owner else None
                    if hits and owner_lines and number > len(owner_lines):
                        out_of_range.append(
                            (rel, lineno, number, owner, len(owner_lines))
                        )
                    elif hits:
                        held += 1
                        if owner not in hits:
                            held_elsewhere += 1
                        # Three grades of "held", because they are not the same
                        # evidence, and only the first one is what a reader
                        # assumes when the report says a number is held:
                        #   own       the holding span is the owner's own name
                        #   borrowed  the holding span is a TARGET name written
                        #             on this line -- what the other references
                        #             on the line anchor with
                        #   picked    the holding span is neither: free text on
                        #             the line that happens to read as a path
                        # The proxy for "borrowed" is "is a TARGET mention on
                        # this line".  The exact question -- is this span
                        # another reference's anchor -- needs the span-to-
                        # reference pairing this checker deliberately does not
                        # do, and a lexical stand-in for it would be a new kind
                        # of false green.  Measured on 89ec77a: 4 of 585 are
                        # `own`.
                        holder_spans = []
                        for s in spans:
                            for token in hits:
                                tl = lines_of(token)
                                if tl and 1 <= number <= len(tl) and s in tl[number - 1]:
                                    holder_spans.append(s)
                                    break
                        owner_text = None
                        for start, _e, token in mentions:
                            if token == owner:
                                owner_text = line[start:_e].rstrip(".").strip()
                        if owner_text and owner_text in holder_spans:
                            grade = "own"
                            held_own += 1
                        elif any(
                            token in TARGETS
                            and line[start:_e].rstrip(".").strip() in holder_spans
                            for start, _e, token in mentions
                        ):
                            grade = "borrowed"
                            held_borrowed += 1
                        else:
                            grade = "picked up"
                            held_picked += 1
                        held_rows.append((rel, lineno, number, owner, grade))
                    else:
                        # The report says which file the number points at, so it
                        # has to use the position rule the owner already uses:
                        # the last tracked name ENDING at or before the number.
                        # The old fallback took the first TARGET mention in the
                        # list no matter where it sat, so a name to the RIGHT of
                        # the number was printed as its owner.  Measured on
                        # 1c6b338: 302 no-anchor rows printed a label whose name
                        # sits to the right of the number, and 429 printed a
                        # label that is not the number's owner at all.  No count
                        # moved for that one, which is why it survived -- it
                        # reads like a reading.
                        target = owner if owner else "<no tracked file named before this number>"
                        if not owner:
                            cited = "<no tracked file named before this number>"
                        else:
                            target_lines = lines_of(owner)
                            cited = (
                                target_lines[number - 1].strip()
                                if target_lines and 1 <= number <= len(target_lines)
                                else "<out of range>"
                            )
                        no_anchor.append(
                            (rel, lineno, first, last, number, target, cited[:64])
                        )
                if refs_here:
                    inline_lines += 1

    return {
        "writings": writings, "explicit": explicit, "held": held,
        "held_elsewhere": held_elsewhere, "no_anchor": no_anchor,
        "off_target": off_target, "out_of_range": out_of_range,
        "held_own": held_own, "held_borrowed": held_borrowed,
        "held_picked": held_picked, "held_rows": held_rows,
        "mention_lines": mention_lines, "inline_lines": inline_lines,
        "files": files,
    }


def base_scan() -> tuple[dict, int]:
    """The same reading on the base tree, and how far behind HEAD it is.

    The base must be an ancestor of HEAD, or the delta compares two trees that
    are not on one line -- and a delta that can be taken against anything is a
    number with no subject.  A base that cannot be resolved is refused, not
    guessed: exit 2, the same answer gate.sh gives for a stage no rule reaches,
    because both are "this question cannot be answered as posed".
    """
    global TREE
    if subprocess.run(
        ["git", "-C", str(REPO_ROOT), "merge-base", "--is-ancestor",
         EXP_LINE_REFS_BASE, "HEAD"],
        capture_output=True,
    ).returncode != 0:
        print(
            f"check_line_refs: EXP_LINE_REFS_BASE={EXP_LINE_REFS_BASE} cannot be "
            "resolved, or is not an ancestor of HEAD.\n"
            "check_line_refs: the pins are deltas against that tree; a base that "
            "is not behind this one makes the delta a comparison between two "
            "trees that are not on one line.\n"
            "check_line_refs: refusing to report a delta with no base.",
            file=sys.stderr,
        )
        raise SystemExit(2)
    saved = TREE
    TREE = Tree(EXP_LINE_REFS_BASE)
    try:
        r = scan()
    finally:
        TREE = saved
    behind = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-list", "--count",
         f"{EXP_LINE_REFS_BASE}..HEAD"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    return r, int(behind)


def main() -> int:
    args = sys.argv[1:]
    report = "--report" in args
    strict = "--strict" in args
    base, behind = base_scan()
    r = scan()

    print(
        f"check_line_refs: {r['mention_lines']} line(s) in docs/ name a target; "
        f"{r['inline_lines']} of them carry an inline number -- that is the "
        f"input set ({len(r['files'])} file(s))."
    )
    print(
        "check_line_refs: writings -- "
        + ", ".join(f"{w} {r['writings'][w]}" for w in WRITINGS)
        + f"; explicit form {r['explicit']}."
    )
    print(
        f"check_line_refs: {r['held']} reference(s) held by the quoted text on "
        f"their own line ({r['held_elsewhere']} of those hold it in another "
        f"file the line names), {len(r['no_anchor'])} with no anchor in any "
        f"file their line names."
    )

    failures = []
    if not r["files"]:
        failures.append(
            "the input set is empty -- the sweep found nothing to check, which "
            "is a failure and not a pass"
        )
    for w in WRITINGS:
        if r["writings"][w] == 0:
            failures.append(
                f"no reference is written in the {w} form any more; a checker "
                f"that has quietly become a prefix scan reports that nothing "
                f"was missed while missing the shape it exists to find"
            )
    explicit_delta = r["explicit"] - base["explicit"]
    unanchored_delta = len(r["no_anchor"]) - len(base["no_anchor"])
    if explicit_delta != EXP_LINE_REFS_DELTA:
        moved = "shrank" if explicit_delta < EXP_LINE_REFS_DELTA else "grew"
        failures.append(
            f"explicit-form references: {explicit_delta:+d} since base "
            f"{EXP_LINE_REFS_BASE} ({base['explicit']} -> {r['explicit']}), but "
            f"EXP_LINE_REFS_DELTA is {EXP_LINE_REFS_DELTA:+d} -- the cited form "
            f"{moved} without anyone saying so"
        )
    if unanchored_delta > EXP_LINE_REFS_UNANCHORED_DELTA_MAX:
        failures.append(
            f"unanchored references: {unanchored_delta:+d} since base "
            f"{EXP_LINE_REFS_BASE} ({len(base['no_anchor'])} -> "
            f"{len(r['no_anchor'])}), over the ceiling "
            f"EXP_LINE_REFS_UNANCHORED_DELTA_MAX="
            f"{EXP_LINE_REFS_UNANCHORED_DELTA_MAX:+d} -- "
            f"{unanchored_delta - EXP_LINE_REFS_UNANCHORED_DELTA_MAX} more "
            f"number(s) now have nothing holding them"
        )

    if len(r["off_target"]) > EXP_LINE_REFS_OFF_TARGET_MAX:
        shown = ", ".join(
            f"{rel}:{lineno} :{number} owner={owner or '<none>'}"
            for rel, lineno, number, owner in r["off_target"][:5]
        )
        failures.append(
            f"mis-labelled references: {len(r['off_target'])} reference(s) are "
            f"counted in the explicit form but their owner is not a file this "
            f"checker reads (ceiling EXP_LINE_REFS_OFF_TARGET_MAX="
            f"{EXP_LINE_REFS_OFF_TARGET_MAX}) -- the count and the label are two "
            f"different pieces of code, and they no longer agree.  First: {shown}"
        )

    print(
        f"check_line_refs: pins -- explicit delta {explicit_delta:+d} (expect "
        f"{EXP_LINE_REFS_DELTA:+d}), unanchored delta {unanchored_delta:+d} "
        f"(ceiling {EXP_LINE_REFS_UNANCHORED_DELTA_MAX:+d})."
    )
    print(
        f"check_line_refs: readings -- explicit {r['explicit']} (base "
        f"{base['explicit']}), unanchored {len(r['no_anchor'])} (base "
        f"{len(base['no_anchor'])}), mis-labelled {len(r['off_target'])} "
        f"(base {len(base['off_target'])})."
    )
    # "held" is one word over three different pieces of evidence, and the
    # sentence a reader takes from it ("that number is held") is only true of
    # the first grade.  Measured on 89ec77a: of 559 held references, 51 are
    # held by their own name and 508 by text that belongs to some other
    # reference on the line, or to nothing at all.  Printed because a number
    # that lives only in a probe is a number the next reader will not see.
    print(
        f"check_line_refs: held -- {r['held']} total: own {r['held_own']}, "
        f"borrowed {r['held_borrowed']}, picked up {r['held_picked']}; "
        f"of those, held by another file {r['held_elsewhere']}."
    )
    if r["out_of_range"]:
        print(
            f"check_line_refs: out of range -- {len(r['out_of_range'])} "
            f"reference(s) name a line that does not exist in the file they "
            f"name.  These are NOT counted as held: the file whose line N "
            f"carries them is some other file, and it always would be."
        )
    print(
        f"check_line_refs: base {EXP_LINE_REFS_BASE} is {behind} commit(s) "
        f"behind HEAD."
    )

    if report:
        by_unit: dict[tuple, list] = {}
        for rel, lineno, first, last, number, owner, cited in r["no_anchor"]:
            by_unit.setdefault((rel, first, last), []).append(
                (lineno, number, owner, cited)
            )
        for (rel, first, last), refs in sorted(by_unit.items()):
            print(
                f"  {rel}:{first}-{last}  {len(refs)} reference(s) with no "
                f"anchor in this unit:"
            )
            for lineno, number, owner, cited in sorted(refs):
                print(f"      :{number} ({owner}) cited at line {lineno}: {cited!r}")
        for rel, lineno, number, owner, size in sorted(r["out_of_range"]):
            print(
                f"  {rel}:{lineno}  :{number} ({owner}) is past the end of that "
                f"file ({size} line(s)) -- not held, and it never could be"
            )
        # The three grades are counters above; these are the rows behind them,
        # so that "own" is a claim a reader can check rather than a number this
        # report asks to be believed.  Two rows each, and two is enough: the
        # question is what a grade looks like, not how many there are.
        for grade in ("own", "borrowed", "picked up"):
            rows = sorted(x for x in r["held_rows"] if x[4] == grade)
            print(f"  held, {grade}: {len(rows)} row(s); first two:")
            for rel, lineno, number, owner, _g in rows[:2]:
                print(f"      {rel}:{lineno}  :{number} ({owner})")

    if strict and r["no_anchor"]:
        print(
            f"check_line_refs: --strict: {len(r['no_anchor'])} reference(s) "
            f"have no content anchor.  A number nothing holds will move."
        )
        return 1

    if failures:
        for f in failures:
            print(f"check_line_refs: {f}")
        print(f"check_line_refs: FAILED ({len(failures)} assertion(s)).")
        return 1

    print(
        "check_line_refs: every pin held.  A delta proves this batch did not "
        "grow or shrink without a commit saying so; it does not prove any "
        "anchor is right, and a number that is still there and points at "
        "something else is not caught here."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
