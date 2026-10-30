/* Plant a container table, regenerate it through the MIR, and report what came
 * out.  This answers "does the regeneration carry it?" by RUNNING the code.
 *
 * The other way to answer it would be to grep mir.c for the member name -- and
 * that answer is a lie the moment someone adds a comment mentioning it, or
 * copies the member inside a helper that is called twice.  A planted value that
 * survives to the regenerated container is the only evidence that survives
 * refactoring.
 *
 * Built by tools/mir_express.test.py against src/compiler/mir.c and the engine's
 * object files for the container; this file is a measurement, not a target.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "bytecode.h"

int main(void) {
    Bytecode *bc = (Bytecode *)calloc(1, sizeof(Bytecode));
    bytecode_init(bc);
    bc->code = (RegInstruction *)calloc(2, sizeof(RegInstruction));
    bc->code[0].op = OP_HALT;
    bc->count = 1;
    bc->capacity = 2;

    /* one distinctive value per table, planted before the view is built */
    bc->capture_names = (char **)calloc(1, sizeof(char *));
    bc->capture_names[0] = strdup("PLANTED_CAPTURE");
    bc->capture_count = 1;

    bc->labels = (LabelEntry *)calloc(1, sizeof(LabelEntry));
    bc->labels[0].name = "PLANTED_LABEL";
    bc->labels[0].off = 7;
    bc->label_count = 1;

    bc->thread_flags[0] = 0x5A;

    bc->str_interned = (char **)calloc(1, sizeof(char *));
    bc->str_interned[0] = "PLANTED_INTERN";

    bc->global_names = (char **)calloc(1, sizeof(char *));
    bc->global_names[0] = strdup("PLANTED_GLOBAL");
    bc->global_name_count = 1;

    /* func_names/func_argc are held by the PARENT of a function, so they are
     * planted on a child: on the root they would be the root's own name, which
     * has no parent to carry it, and the "kept=no" that produced would be a
     * fact about the probe rather than about the carrier. */
    Bytecode *child = (Bytecode *)calloc(1, sizeof(Bytecode));
    bytecode_init(child);
    child->code = (RegInstruction *)calloc(1, sizeof(RegInstruction));
    child->code[0].op = OP_HALT;
    child->count = 1;
    child->capacity = 1;
    bc->funcs[0] = child;
    bc->func_count = 1;
    bc->func_names[0] = "PLANTED_FUNC";
    bc->func_argc[0] = 3;

    /* the thread section is the same shape: a container table with no IR field,
     * so it gets its own child and its own answer rather than borrowing one. */
    Bytecode *thread = (Bytecode *)calloc(1, sizeof(Bytecode));
    bytecode_init(thread);
    thread->code = (RegInstruction *)calloc(1, sizeof(RegInstruction));
    thread->code[0].op = OP_HALT;
    thread->count = 1;
    thread->capacity = 1;
    bc->threads[0] = thread;
    bc->thread_count = 1;
    bc->thread_names[0] = "PLANTED_THREAD";
    bc->thread_argc[0] = 2;

    bc->try_entries = (TryEntry *)calloc(1, sizeof(TryEntry));
    bc->try_entries[0].start_off = 11;
    bc->try_count = 1;
    bc->try_cap = 1;

    IrModule *m = mir_from_bytecode(bc);
    if (!m) { printf("MIR-EXPRESS-MOVE|error=view-refused\n"); return 0; }
    Bytecode *out = mir_to_bytecode(m);
    if (!out) { printf("MIR-EXPRESS-MOVE|error=regen-refused\n"); return 0; }

#define MOVED(name, cond) \
    printf("MIR-EXPRESS-MOVE|item=%s|kept=%s\n", name, (cond) ? "yes" : "no")

    MOVED("capture-table",
          out->capture_count == 1 && out->capture_names && out->capture_names[0] &&
          strcmp(out->capture_names[0], "PLANTED_CAPTURE") == 0);
    MOVED("label-table",
          out->label_count == 1 && out->labels && out->labels[0].name &&
          strcmp(out->labels[0].name, "PLANTED_LABEL") == 0);
    MOVED("thread-flags", out->thread_flags[0] == 0x5A);
    MOVED("string-intern",
          out->str_interned && out->str_interned[0] &&
          strcmp(out->str_interned[0], "PLANTED_INTERN") == 0);
    MOVED("global-names",
          out->global_name_count == 1 && out->global_names && out->global_names[0] &&
          strcmp(out->global_names[0], "PLANTED_GLOBAL") == 0);
    /* the name and argc a function carries are its parent's columns, so they
     * are read off the regenerated PARENT, next to the regenerated child */
    MOVED("func-name",
          out->func_count == 1 && out->funcs[0] && out->func_names[0] &&
          strcmp(out->func_names[0], "PLANTED_FUNC") == 0);
    MOVED("func-argc", out->func_count == 1 && out->func_argc[0] == 3);
    MOVED("thread-section",
          out->thread_count == 1 && out->threads[0] && out->threads[0]->count == 1 &&
          out->thread_names[0] && strcmp(out->thread_names[0], "PLANTED_THREAD") == 0 &&
          out->thread_argc[0] == 2);
    MOVED("try-table", out->try_count == 1 && out->try_entries &&
          out->try_entries[0].start_off == 11);

    bytecode_free(out);
    mir_free(m);
    return 0;
}
