#!/usr/bin/env python3
"""check_builtin_allowlist.py -- the refusal message is a list, and nothing reads it.

Why this exists (docs/STATUS.md 10.254):

When the MIR route meets a builtin it does not emit, it refuses and prints the
names it DOES emit:

    builtin call 'atomic_get' -- this route emits len, list, size, count, has,
    keys, str, sum, remove, push, pop and int, substr, ... args, declared,
    type and no other builtin

That message is the only thing a reader has to decide what this route can
lower.  It is also a hand-written English sentence, in two places (the refusal
text and the `!= 0` chain that admits the name), and nothing compares them.

§10.253 found the first half of that: `type` had an arm and was not in the
message, and a reader trusting the list would conclude `type` was not a member
at all.  Writing it into the list fixed one name and kept the mechanism, which
is what this checker is for.

Both directions, because each fails differently:

  * a name the message lists with NO handler -- a reader is told a program will
    work and it is refused.  That is the more damaging direction: the message
    is read when the tool has already refused, so the reader is looking for
    what else they could have written.
  * a name WITH a handler that the message omits -- a reader works around a
    limitation that is not there.

Reading the source rather than the printed message is deliberate.  The message
is built from adjacent C string literals, so it wraps mid-list and its own
text depends on the compiler's concatenation; an extraction that got that wrong
would compare the message with itself.  What is checked is the RELATION between
two source-level facts: the names in the `!= 0` chain (what may pass) and the
names with a `== 0` arm (what may be emitted) against the names spelled in the
message.

Exit codes: 0 the three agree; 1 they do not; 2 the source could not be read.
"""

from __future__ import annotations

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "src", "compilation", "aot_mir.c")

# `strcmp(bn, "name") != 0` -- the chain that decides whether a builtin may pass.
ADMIT = re.compile(r'strcmp\(bn, "([A-Za-z_0-9]+)"\) != 0')
# `strcmp(bn, "name") == 0` -- an arm that emits something for it.  A name can
# have several (one per arity), so these are compared as a SET.
EMIT = re.compile(r'strcmp\(bn, "([A-Za-z_0-9]+)"\) == 0')
# The message: "this route emits A, B and C and no other builtin".  Assembled
# from adjacent literals in the source, so the raw text has `"` and newlines in
# it; the words are what matter.
# Anchored on the whole opening, not on "this route emits": that phrase also
# occurs in the interval-domain refusal and in five source comments, and the
# first version of this file matched the domain message and read 567 names out
# of it.  The builtin message is the one that starts with a quoted name.
MSG_START = "-- this route emits "
MSG_END = "and no other"


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def message_names(src: str):
    """The names the refusal message lists, or None if it cannot be found.

    The message is one sentence written as several adjacent C string literals,
    split for line width, so the source text between the anchor and `builtin;`
    contains quote characters and indentation that are not part of the sentence.
    Removing the quotes and collapsing the whitespace runs is exactly the
    compiler's concatenation, and it is the only one of four attempts that read
    the message:

      1. anchor on "this route emits" -> matched the INTERVAL-DOMAIN refusal
         12KB earlier, reported 567 names;
      2. first literal only -> reported 2;
      3. join literals matched by a regex -> the first literals in the run are
         the whitespace between quote pairs, so the body was blank;
      4. this one.

    The first three printed a confident red about a message none of them had
    read, which is the failure this file exists to catch, one level down.
    """
    anchor = src.find("-- this route emits len,")
    if anchor < 0:
        return None
    stop = src.find("builtin;", anchor)
    if stop < 0:
        return None
    text = src[anchor : stop + len("builtin;")].replace('"', " ")
    text = " ".join(text.split())
    text = text.split("-- this route emits ", 1)[-1]
    cut = text.find("and no other")
    if cut < 0:
        return None
    names = re.split(r",|\band\b", text[:cut])
    return {n.strip() for n in names if n.strip()}


def measure(repo_root: str):
    src = _read(os.path.join(repo_root, "src", "compilation", "aot_mir.c"))
    admitted = {m.group(1) for m in ADMIT.finditer(src)}
    emitted = {m.group(1) for m in EMIT.finditer(src)}
    listed = message_names(src)
    if listed is None:
        raise RuntimeError("could not find the refusal message in aot_mir.c")
    if not admitted:
        raise RuntimeError("found no `strcmp(bn, ...) != 0` admission chain")
    if not emitted:
        raise RuntimeError("found no `strcmp(bn, ...) == 0` emit arm")

    handled = admitted | emitted
    problems = []
    # The message must not promise a name the route cannot handle.
    promised = listed - handled
    # And it must not hide one it can.
    hidden = handled - listed
    for name in sorted(promised):
        problems.append("listed in the message, no handler: %s" % name)
    for name in sorted(hidden):
        problems.append("has a handler, missing from the message: %s" % name)
    stats = {
        "listed": len(listed),
        "admitted": len(admitted),
        "emitted": len(emitted),
        "handled": len(handled),
        "promised_blind": len(promised),
        "hidden": len(hidden),
    }
    return (1 if problems else 0), problems, stats


def self_test() -> int:
    """The two directions, on a fixture whose answer is known.

    The fixture is written the way the real message is -- several adjacent C
    literals with the source's own layout inside them -- because a fixture that
    is one line tests an extraction the file does not contain.  Three of the
    four versions of `message_names` above parsed a one-line fixture correctly
    and the real message wrongly.
    """
    failures = []

    def fixture(extra_in_message="", extra_arm=""):
        return (
            'return bad("container=%%d insn=%%d builtin call \'%%s\' -- this route emits len,"\n'
            '           " and type%s and no other"\n'
            '           " builtin;");\n' % extra_in_message
            + 'if (!bn || (strcmp(bn, "len") != 0 && strcmp(bn, "type") != 0)) { }\n'
            + 'if (strcmp(bn, "len") == 0) { }\n'
            + 'else if (strcmp(bn, "type") == 0) { }\n' + extra_arm
        )

    clean = fixture()
    if message_names(clean) != {"len", "type"}:
        failures.append("a clean fixture must parse to {len,type}: got %r" % (message_names(clean),))

    # A name the message promises and nothing handles.
    promised = fixture(extra_in_message=", ghost")
    handled = ({m.group(1) for m in ADMIT.finditer(promised)}
               | {m.group(1) for m in EMIT.finditer(promised)})
    listed = message_names(promised) or set()
    if not (listed - handled):
        failures.append("a promised name with no handler must be visible")

    # A handler the message does not name.
    hidden = fixture(extra_arm='else if (strcmp(bn, "extra") == 0) { }\n')
    listed = message_names(hidden) or set()
    handled = ({m.group(1) for m in ADMIT.finditer(hidden)}
               | {m.group(1) for m in EMIT.finditer(hidden)})
    if not (handled - listed):
        failures.append("a handler missing from the message must be visible")

    print("check_builtin_allowlist --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_builtin_allowlist: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "a multi-literal message parses to its names",
        "a promised name with no handler is visible",
        "a handler with no name in the message is visible",
    ):
        print("  ok   %s" % line)
    print("check_builtin_allowlist: self-test OK")
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
        print("check_builtin_allowlist: cannot measure: %s" % problem)
        return 2

    for line in problems:
        print("  PROBLEM  %s" % line)
    print(
        "check_builtin_allowlist: message-lists=%d|admitted=%d|emitted=%d|handled=%d"
        "|promised-without-handler=%d|handled-but-unlisted=%d"
        % (
            stats["listed"],
            stats["admitted"],
            stats["emitted"],
            stats["handled"],
            stats["promised_blind"],
            stats["hidden"],
        )
    )
    if red:
        print(
            "check_builtin_allowlist: the refusal message and the emitters disagree.  "
            "The message is what a reader uses to decide what this route can lower."
        )
        return 1
    print(
        "check_builtin_allowlist: every name the message lists has a handler, and every "
        "handler is named.  This says the three agree, not that either is right."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
