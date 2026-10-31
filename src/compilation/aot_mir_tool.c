/* aot_mir_tool -- the command line driver for aot_mir.
 *
 * It carries its own main() the way aot_native_tool.c does: the generator is a
 * library the tests link, and this binary is what the tests actually run.  It
 * links the engine library so that lexing, parsing and compiling stay the
 * engine's; a second parser here would be a second thing to disagree with.
 *
 *   aot_mir translate [--perturb <id> <idx> <field> <value>] <input.im> <output.c>
 *   aot_mir dump <input.im>
 *   aot_mir subset
 *
 * --perturb is the negative control.  It changes one operand of one IrInsn
 * through mir_perturb() after the module is built and before anything is
 * lowered.  If the generated C does not change, the generator is not reading
 * the MIR it claims to read.
 *
 * dump prints the module's instructions as it stands, which is how a test picks
 * an operand to perturb without hard-coding an instruction index that the next
 * compiler change would move.
 */
#include "aot_mir.h"
#include "compiler.h"
#include "parser.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NL "\n"

static void usage(void) {
    fprintf(stderr, "usage: aot_mir translate [--perturb <id> <idx> <field> <value>] <input.im> <output.c>" NL);
    fprintf(stderr, "       aot_mir dump <input.im>" NL);
    fprintf(stderr, "       aot_mir subset" NL);
    fprintf(stderr, "  field is mir_perturb's: 0=op 1=dst(r1) 2=a(r2) 3=b(r3), see src/compiler/mir.c" NL);
}

static int total_insns(const IrModule *m) {
    int n = 0;
    for (int i = 0; i < m->nfuncs; i++) n += m->funcs[i].ninsns;
    return n;
}

/* Parse, compile, lower -- the same three steps the interpreter takes for a .im
   file (src/main.c load_and_run), minus execution.  A failure here is reported
   the way the route reports every other refusal: named, with a count. */
static IrModule *module_of(const char *in) {
    Program *prog = parse_program_file(in);
    if (!prog) {
        fprintf(stderr, "error: cannot parse '%s'" NL, in);
        return NULL;
    }
    Compiler *comp = compiler_new();
    if (!comp) {
        fprintf(stderr, "error: no compiler" NL);
        return NULL;
    }
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    if (!bc) {
        printf("IR-AOT-MIR-REFUSED|prog=%s|why=no-bytecode|n=0" NL, in);
        return NULL;
    }
    IrModule *m = mir_from_bytecode(bc);
    if (!m) {
        /* mir_from_bytecode refuses rather than truncating (mir.h: an int16_t
           block vector cannot describe a longer function).  Say so by name. */
        printf("IR-AOT-MIR-REFUSED|prog=%s|why=not-representable|n=0" NL, in);
        return NULL;
    }
    return m;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    if (strcmp(argv[1], "subset") == 0) {
        /* the emittable set, spelled out: the coverage harness has to tell
           "this lowering cannot emit it" apart from "the corpus never used it" */
        int n = aot_mir_emitted_count();
        printf("IR-MIR-SUBSET|emits=%d|ops=(", n);
        for (int i = 0; i < n; i++) printf("%s%s", i ? "," : "", aot_mir_op_name(aot_mir_emitted_opcode(i)));
        printf(")" NL);
        puts(aot_mir_subset_description());
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0) {
        if (argc < 3) { usage(); return 2; }
        IrModule *m = module_of(argv[2]);
        if (!m) return 1;
        printf("IR-MIR-DUMP|in=%s|funcs=%d|insns=%d" NL, argv[2], m->nfuncs, total_insns(m));
        for (int f = 0; f < m->nfuncs; f++) {
            const IrFunc *fn = &m->funcs[f];
            for (int i = 0; i < fn->ninsns; i++) {
                const IrInsn *in = &fn->insns[i];
                printf("IR-MIR-INSN|func=%d|idx=%d|op=%s|r1=%d|r2=%d|r3=%d|argc=%d" NL,
                       f, i, aot_mir_op_name(in->op), in->dst, ir_insn_r2(in), ir_insn_r3(in), fn->argc);
            }
        }
        return 0;
    }

    if (strcmp(argv[1], "translate") != 0) { usage(); return 2; }

    int i = 2;
    int perturb = 0, pid = 0, pidx = 0, pfield = 0;
    long pval = 0;
    if (i < argc && strcmp(argv[i], "--perturb") == 0) {
        if (i + 4 >= argc) { usage(); return 2; }
        pid = atoi(argv[i + 1]);
        pidx = atoi(argv[i + 2]);
        pfield = atoi(argv[i + 3]);
        pval = strtol(argv[i + 4], NULL, 0);
        perturb = 1;
        i += 5;
    }
    if (i + 1 >= argc) { usage(); return 2; }
    const char *in = argv[i];
    const char *out = argv[i + 1];

    IrModule *m = module_of(in);
    if (!m) return 1;

    if (perturb && mir_perturb(m, pid, pidx, pfield, (int32_t)pval) != 0) {
        fprintf(stderr, "aot_mir: perturb rejected (function id or instruction index out of range)" NL);
        return 1;
    }

    printf("IR-MIR-TOOL|in=%s|out=%s|perturb=%s|funcs=%d|insns=%d" NL,
           in, out, perturb ? "yes" : "no", m->nfuncs, total_insns(m));

    if (!aot_mir_translate(m, out)) {
        printf("IR-AOT-MIR-REFUSED|prog=%s|why=%s|n=%d" NL, in, aot_mir_last_error(), aot_mir_last_insns());
        fprintf(stderr, "aot_mir: %s" NL, aot_mir_last_error());
        return 1;
    }

    printf("IR-AOT-MIR-OK|prog=%s|out=%s" NL, in, out);
    return 0;
}
