/* P1 of docs/IR_DESIGN.md -- see src/compiler/mir.h for what this is, and for the
   three places where the design as written could not be implemented as written.

   The one invariant this file exists to satisfy: mir_to_bytecode(mir_from_bytecode)
   regenerates the instruction stream from IrInsn and produces a byte-identical
   container.  Pools, names, arg counts, the try table, the global-name table and
   the thread section have no MIR field, so they are carried through the
   container alias (IrModule.bc) rather than reconstructed.  The report and the
   test both say which bytes were regenerated and which were carried. */

#include "mir.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------- operand width (DEVIATION 1) ---------------- */

static int fits_i16(int v) { return v >= -32768 && v <= 32767; }

int32_t ir_insn_r2(const IrInsn *in) { return (in->flags & IR_F_WIDE_A) ? in->imm : (int32_t)in->a; }
int32_t ir_insn_r3(const IrInsn *in) { return (in->flags & IR_F_WIDE_B) ? in->wide : (int32_t)in->b; }

/* ---------------- control flow ---------------- */

static int op_is_terminator(OpCode op) {
    switch (op) {
    case OP_JUMP: case OP_RETURN: case OP_THROW:
    case OP_HALT: case OP_STOP: case OP_THREAD_GOTO:
        return 1;
    default:
        return 0;
    }
}

static int op_is_conditional(OpCode op) {
    return op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE;
}

/* Successors are read from the encoding's own fields: OP_JUMP and the two
   conditional jumps carry their absolute target in r2 (see the SHAPE table in
   tools/ir_spec.py, which is the authority for that). */
static int insn_target(const RegInstruction *ins) {
    switch (ins->op) {
    case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_THREAD_GOTO:
        return ins->r2;
    default:
        return -1;
    }
}

/* ---------------- from bytecode ---------------- */

static int count_nodes(const Bytecode *bc) {
    int n = 1;
    for (int i = 0; i < bc->func_count; i++) n += count_nodes(bc->funcs[i]);
    for (int i = 0; i < bc->thread_count; i++) n += count_nodes(bc->threads[i]);
    return n;
}

static int build_blocks(IrFunc *f, const Bytecode *bc) {
    int n = bc->count;
    if (n <= 0) { f->blocks = NULL; f->nblocks = 0; return 0; }
    if (n > 32767) return -1;   /* int16_t start/end as designed cannot hold this */

    char *mark = (char *)calloc((size_t)n + 1, 1);
    int *bid = (int *)malloc(((size_t)n + 1) * sizeof(int));
    if (!mark || !bid) { free(mark); free(bid); return -1; }

    mark[0] = 1;
    mark[n] = 1;
    for (int i = 0; i < n; i++) {
        OpCode op = bc->code[i].op;
        int t = insn_target(&bc->code[i]);
        if (t >= 1 && t <= n) mark[t] = 1;
        /* a control instruction ends a block, so the next instruction starts one
           (it may be unreachable, or a jump target) */
        if (t >= 0 || op_is_terminator(op) || op_is_conditional(op)) {
            if (i + 1 <= n) mark[i + 1] = 1;
        }
    }
    for (int i = 0; i < bc->try_count; i++) {
        int offs[3] = { bc->try_entries[i].start_off, bc->try_entries[i].end_off,
                        bc->try_entries[i].catch_off };
        for (int k = 0; k < 3; k++)
            if (offs[k] >= 0 && offs[k] <= n) mark[offs[k]] = 1;
    }

    int nb = 0;
    for (int i = 0; i <= n; i++) if (mark[i]) nb++;
    IrBlock *blocks = (IrBlock *)calloc((size_t)nb, sizeof(IrBlock));
    if (!blocks) { free(mark); free(bid); return -1; }

    /* block k spans [starts[k], starts[k+1]); the last start is n */
    int *starts = (int *)malloc(((size_t)nb + 1) * sizeof(int));
    if (!starts) { free(mark); free(bid); free(blocks); return -1; }
    int b = 0;
    for (int i = 0; i <= n; i++) if (mark[i]) starts[b++] = i;

    for (int k = 0; k < nb; k++) {
        blocks[k].start = (int16_t)starts[k];
        blocks[k].end = (int16_t)starts[k + 1];
        blocks[k].nsucc = 0;
        blocks[k].stack_hint = IR_T_UNKNOWN;
    }
    for (int ip = 0; ip < n; ip++) {
        int k = 0;
        while (k + 1 < nb && starts[k + 1] <= ip) k++;
        bid[ip] = k;
    }
    bid[n] = -1;

    /* successors from each block's tail instruction */
    for (int k = 0; k < nb; k++) {
        int last = blocks[k].end - 1;
        if (last < 0 || last >= n) continue;
        OpCode op = bc->code[last].op;
        int t = insn_target(&bc->code[last]);
        if (t >= 0) {
            if (t >= 0 && t < n) blocks[k].succ[blocks[k].nsucc++] = bid[t];
            if (op_is_conditional(op) && blocks[k].end < n)
                blocks[k].succ[blocks[k].nsucc++] = bid[blocks[k].end];
        } else if (!op_is_terminator(op)) {
            if (blocks[k].end < n)
                blocks[k].succ[blocks[k].nsucc++] = bid[blocks[k].end];
        }
        /* dedupe a self-target (a jump to its own block start is a loop) */
        if (blocks[k].nsucc == 2 && blocks[k].succ[0] == blocks[k].succ[1]) blocks[k].nsucc = 1;
    }
    free(starts);

    free(mark);
    free(bid);
    f->blocks = blocks;
    f->nblocks = nb;
    return 0;
}

static int build_insns(IrFunc *f, const Bytecode *bc) {
    int n = bc->count;
    if (n <= 0) { f->insns = NULL; f->ninsns = 0; f->cap = 0; return 0; }
    IrInsn *ins = (IrInsn *)calloc((size_t)n, sizeof(IrInsn));
    if (!ins) return -1;
    for (int i = 0; i < n; i++) {
        const RegInstruction *r = &bc->code[i];
        IrInsn *o = &ins[i];
        o->op = (uint16_t)r->op;
        o->flags = 0;
        o->dst = (int16_t)r->r1;
        o->a = (int16_t)r->r2;
        o->b = (int16_t)r->r3;
        o->imm = 0;
        o->wide = 0;
        /* DEVIATION 1: keep the exact 32-bit operand when 16 bits are not enough */
        if (!fits_i16(r->r2)) { o->flags |= IR_F_WIDE_A; o->imm = r->r2; }
        if (!fits_i16(r->r3)) { o->flags |= IR_F_WIDE_B; o->wide = r->r3; }
        if (!fits_i16(r->r1)) { /* r1 is a register or a small index in every op we know of;
                                   a hit here is a real finding, and it is reported, not hidden */
            free(ins);
            return -1;
        }
        o->type = IR_T_UNKNOWN;   /* no inference in P1 */
    }
    f->insns = ins;
    f->ninsns = n;
    f->cap = n;
    return 0;
}

static int fill_node(IrModule *m, const Bytecode *bc, int *cursor) {
    int id = (*cursor)++;
    IrFunc *f = &m->funcs[id];
    memset(f, 0, sizeof(*f));
    f->id = id;
    f->ret = IR_T_UNKNOWN;
    f->effects = 0;      /* P1: effects live in OpSemantics, generated in P2/P3 */
    f->nregs = 0;        /* P1: not derived; deriving it here would be inference */
    if (build_insns(f, bc) != 0) return -1;
    if (build_blocks(f, bc) != 0) { free(f->insns); f->insns = NULL; return -1; }

    /* argc lives in the *parent* container's parallel array, which is why the
       DFS order below must be the same order the writer uses.  It is applied by
       fill_argc() once every node has an id. */
    for (int i = 0; i < bc->func_count; i++) {
        if (fill_node(m, bc->funcs[i], cursor) != 0) return -1;
    }
    for (int i = 0; i < bc->thread_count; i++)
        if (fill_node(m, bc->threads[i], cursor) != 0) return -1;
    return 0;
}

/* The parent's argc/name arrays belong to the parent node, so they are applied
   after the child has its id. */
static void fill_argc(IrModule *m, const Bytecode *bc, int *cursor) {
    int id = (*cursor)++;
    for (int i = 0; i < bc->func_count; i++) {
        m->funcs[*cursor].argc = bc->func_argc[i];
        fill_argc(m, bc->funcs[i], cursor);
    }
    for (int i = 0; i < bc->thread_count; i++) {
        m->funcs[*cursor].argc = bc->thread_argc[i];
        fill_argc(m, bc->threads[i], cursor);
    }
    (void)m->funcs[id].argc;
}

/* strings are char*[], so a StringView run cannot alias them; the run is
   materialised here.  Floats do alias the pool. */
static int build_strings(IrModule *m, const Bytecode *bc) {
    m->nkstr = bc->string_count;
    m->kstr = NULL;
    if (bc->string_count > 0) {
        m->kstr = (StringView *)calloc((size_t)bc->string_count, sizeof(StringView));
        if (!m->kstr) return -1;
        for (int i = 0; i < bc->string_count; i++) {
            m->kstr[i].start = bc->string_pool[i];
            m->kstr[i].length = (int)strlen(bc->string_pool[i]) + 1;  /* the writer stores len+1 */
        }
    }
    m->kfloat = bc->float_pool;
    m->nkfloat = bc->float_count;
    m->kint = NULL;      /* DEVIATION 2: there is no int pool in the encoding */
    m->nkint = 0;
    return 0;
}

IrModule *mir_from_bytecode(const struct Bytecode *bc) {
    if (!bc) return NULL;
    int total = count_nodes(bc);
    IrModule *m = (IrModule *)calloc(1, sizeof(IrModule));
    if (!m) return NULL;
    m->funcs = (IrFunc *)calloc((size_t)total, sizeof(IrFunc));
    if (!m->funcs) { free(m); return NULL; }
    m->bc = bc;

    int cursor = 0;
    if (fill_node(m, bc, &cursor) != 0) { mir_free(m); return NULL; }
    cursor = 0;
    fill_argc(m, bc, &cursor);
    m->nfuncs = total;

    m->nfuncs_main = 1 + bc->func_count;
    if (build_strings(m, bc) != 0) { mir_free(m); return NULL; }
    return m;
}

void mir_free(IrModule *m) {
    if (!m) return;
    for (int i = 0; i < m->nfuncs; i++) {
        free(m->funcs[i].insns);
        free(m->funcs[i].blocks);
    }
    free(m->kstr);
    free(m->funcs);
    free(m);
}

/* ---------------- to bytecode ---------------- */

static int copy_pools(Bytecode *dst, const Bytecode *src) {
    if (src->string_count > 0) {
        dst->string_pool = (char **)calloc((size_t)src->string_count, sizeof(char *));
        if (!dst->string_pool) return -1;
        for (int i = 0; i < src->string_count; i++) {
            dst->string_pool[i] = strdup(src->string_pool[i]);
            if (!dst->string_pool[i]) return -1;
        }
        dst->string_count = src->string_count;
    }
    if (src->float_count > 0) {
        dst->float_pool = (double *)malloc((size_t)src->float_count * sizeof(double));
        if (!dst->float_pool) return -1;
        memcpy(dst->float_pool, src->float_pool, (size_t)src->float_count * sizeof(double));
        dst->float_count = src->float_count;
    }
    if (src->global_name_count > 0) {
        dst->global_names = (char **)calloc((size_t)src->global_name_count, sizeof(char *));
        if (!dst->global_names) return -1;
        for (int i = 0; i < src->global_name_count; i++) {
            dst->global_names[i] = strdup(src->global_names[i]);
            if (!dst->global_names[i]) return -1;
        }
        dst->global_name_count = src->global_name_count;
    }
    if (src->try_count > 0) {
        dst->try_entries = (TryEntry *)calloc((size_t)src->try_count, sizeof(TryEntry));
        if (!dst->try_entries) return -1;
        memcpy(dst->try_entries, src->try_entries, (size_t)src->try_count * sizeof(TryEntry));
        dst->try_count = src->try_count;
        dst->try_cap = src->try_count;
    }
    return 0;
}

static Bytecode *emit_node(const IrModule *m, const Bytecode *src, int *cursor) {
    int id = (*cursor)++;
    const IrFunc *f = &m->funcs[id];

    Bytecode *dst = (Bytecode *)calloc(1, sizeof(Bytecode));
    if (!dst) return NULL;
    bytecode_init(dst);
    if (copy_pools(dst, src) != 0) { bytecode_free(dst); free(dst); return NULL; }

    /* The instruction stream is REGENERATED from IrInsn -- never copied from
       src->code.  This is the line that makes the roundtrip a test. */
    if (f->ninsns > 0) {
        dst->code = (RegInstruction *)calloc((size_t)f->ninsns, sizeof(RegInstruction));
        if (!dst->code) { bytecode_free(dst); free(dst); return NULL; }
        for (int i = 0; i < f->ninsns; i++) {
            const IrInsn *in = &f->insns[i];
            dst->code[i].op = (OpCode)in->op;
            dst->code[i].r1 = in->dst;
            dst->code[i].r2 = ir_insn_r2(in);
            dst->code[i].r3 = ir_insn_r3(in);
        }
        dst->count = f->ninsns;
        dst->capacity = f->ninsns;
    }

    for (int i = 0; i < src->func_count; i++) {
        dst->funcs[i] = emit_node(m, src->funcs[i], cursor);
        if (!dst->funcs[i]) { bytecode_free(dst); free(dst); return NULL; }
        dst->func_argc[i] = src->func_argc[i];
        dst->func_names[i] = src->func_names[i] ? strdup(src->func_names[i]) : NULL;
        dst->func_count = i + 1;
    }
    for (int i = 0; i < src->thread_count; i++) {
        dst->threads[i] = emit_node(m, src->threads[i], cursor);
        if (!dst->threads[i]) { bytecode_free(dst); free(dst); return NULL; }
        dst->thread_argc[i] = src->thread_argc[i];
        dst->thread_names[i] = src->thread_names[i] ? strdup(src->thread_names[i]) : NULL;
        dst->thread_count = i + 1;
    }
    return dst;
}

struct Bytecode *mir_to_bytecode(const IrModule *m) {
    if (!m || !m->bc) return NULL;
    int cursor = 0;
    return emit_node(m, m->bc, &cursor);
}

/* ---------------- negative control ---------------- */

int mir_perturb(IrModule *m, int id, int idx, int field, int32_t value) {
    if (!m || id < 0 || id >= m->nfuncs) return -1;
    IrFunc *f = &m->funcs[id];
    if (idx < 0 || idx >= f->ninsns) return -1;
    IrInsn *in = &f->insns[idx];
    switch (field) {
    case 0: in->op = (uint16_t)value; break;
    case 1: in->dst = (int16_t)value; break;
    case 2: in->a = (int16_t)value; in->flags &= (uint8_t)~IR_F_WIDE_A; break;
    case 3: in->b = (int16_t)value; in->flags &= (uint8_t)~IR_F_WIDE_B; break;
    default: return -1;
    }
    return 0;
}

/* ---------------- roundtrip CLI (compiled only for the test) ---------------- */
#ifdef MIR_ROUNDTRIP_MAIN

static unsigned char *slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1);
    if (buf && sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); buf = NULL; }
    fclose(f);
    *n = sz;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <in.inim> <out.inim> [--perturb id idx field value]\n", argv[0]);
        return 2;
    }
    const char *in = argv[1], *out = argv[2];
    int pid = -1, pidx = -1, pfield = -1;
    long pval = 0;
    if (argc >= 8 && strcmp(argv[3], "--perturb") == 0) {
        pid = atoi(argv[4]); pidx = atoi(argv[5]); pfield = atoi(argv[6]); pval = atol(argv[7]);
    }

    Bytecode *bc = bytecode_read_file(in);
    if (!bc) { fprintf(stderr, "MIR-ERROR|read failed: %s\n", in); return 1; }

    int total_insns = 0, used[256];
    long n_threads = 0, n_try = 0, n_wide = 0;
    memset(used, 0, sizeof(used));
    /* ops_used over the whole tree */
    {
        Bytecode *stack[128]; int sp = 0;
        stack[sp++] = bc;
        while (sp > 0) {
            Bytecode *n = stack[--sp];
            total_insns += n->count;
            n_threads += n->thread_count;
            n_try += n->try_count;
            for (int i = 0; i < n->count; i++) {
                used[(unsigned)n->code[i].op & 0xffu] = 1;
                /* an operand outside int16 is what DEVIATION 1 exists for; count
                   it so the report can say whether this corpus exercises it */
                if (n->code[i].r2 < -32768 || n->code[i].r2 > 32767 ||
                    n->code[i].r3 < -32768 || n->code[i].r3 > 32767) n_wide++;
            }
            for (int i = 0; i < n->func_count && sp < 127; i++) stack[sp++] = n->funcs[i];
            for (int i = 0; i < n->thread_count && sp < 127; i++) stack[sp++] = n->threads[i];
        }
    }
    int ops_used = 0;
    for (int i = 0; i < 256; i++) ops_used += used[i];

    IrModule *m = mir_from_bytecode(bc);
    if (!m) { fprintf(stderr, "MIR-ERROR|from_bytecode failed: %s\n", in); return 1; }

    if (pid >= 0) {
        if (mir_perturb(m, pid, pidx, pfield, (int32_t)pval) != 0) {
            fprintf(stderr, "MIR-ERROR|perturb rejected\n");
            return 1;
        }
    }

    int max_fn_insns = 0;
    for (int i = 0; i < m->nfuncs; i++) if (m->funcs[i].ninsns > max_fn_insns) max_fn_insns = m->funcs[i].ninsns;
    printf("IR-SIZE|funcs=%d|insns=%d|ops_used=%d|ops_total=69|max_func_insns=%d|strings=%d|floats=%d"
           "|threads=%ld|try=%ld|wide_ops=%ld\n",
           m->nfuncs, total_insns, ops_used, max_fn_insns, m->nkstr, m->nkfloat,
           n_threads, n_try, n_wide);
    printf("IR-EFFECTS|may_throw=0|may_alloc=0|pure=0|note=no-generator-yet\n");

    long oa = 0;
    unsigned char *orig = slurp(in, &oa);
    if (!orig) { fprintf(stderr, "MIR-ERROR|slurp failed: %s\n", in); return 1; }

    Bytecode *rt = mir_to_bytecode(m);
    if (!rt) { fprintf(stderr, "MIR-ERROR|to_bytecode failed: %s\n", in); return 1; }
    if (bytecode_write_file(out, rt) != 0) { fprintf(stderr, "MIR-ERROR|write failed: %s\n", out); return 1; }

    /* The compiler appends an optional dependency trailer after the bytecode
       stream (src/compilation/deps.c).  It is not part of Bytecode, so it cannot
       be regenerated -- it is CARRIED, and the report says so.  Without this the
       stream is identical but the file is short by the trailer length. */
    long trailer_start = oa;
    if (oa >= 16) {
        uint32_t tmagic = (uint32_t)orig[oa-16] | ((uint32_t)orig[oa-15] << 8) |
                          ((uint32_t)orig[oa-14] << 16) | ((uint32_t)orig[oa-13] << 24);
        if (tmagic == 0x53504449u) {
            uint32_t tlen = (uint32_t)orig[oa-4] | ((uint32_t)orig[oa-3] << 8) |
                            ((uint32_t)orig[oa-2] << 16) | ((uint32_t)orig[oa-1] << 24);
            /* deps_read(): start = fsize_end - trailer_len, and trailer_len
               INCLUDES the 16-byte footer.  Subtracting 16 again is what made
               the first run carry 16 bytes twice. */
            long cand = oa - (long)tlen;
            if (cand >= 8 && cand <= oa) trailer_start = cand;
        }
    }
    printf("ORIG-TRAILER|bytes=%ld|carried=1\n", oa - trailer_start);
    if (trailer_start < oa) {
        FILE *af = fopen(out, "ab");
        if (!af) { fprintf(stderr, "MIR-ERROR|append failed: %s\n", out); return 1; }
        fwrite(orig + trailer_start, 1, (size_t)(oa - trailer_start), af);
        fclose(af);
    }
    free(orig);

    long na = 0, nb = 0;
    unsigned char *a = slurp(in, &na), *b = slurp(out, &nb);
    if (!a || !b) { fprintf(stderr, "MIR-ERROR|slurp failed\n"); return 1; }
    printf("ORIG-STREAM|bytes_stream=%ld\n", trailer_start);
    long diffs = 0, first = -1;
    long lim = na < nb ? na : nb;
    for (long i = 0; i < lim; i++) {
        if (a[i] != b[i]) { diffs++; if (first < 0) first = i; }
    }
    if (na != nb) { diffs += (na > nb ? na - nb : nb - na); if (first < 0) first = lim; }

    printf("ORIG-IDENTICAL|file=%s|bytes_orig=%ld|bytes_new=%ld|differ=%ld|first_diff=%ld\n",
           in, na, nb, diffs, first);
    if (first >= 0) {
        /* the code block starts after count + strings + floats; report offsets so
           the caller can say which side of the container diverged */
        printf("ORIG-DIFF|first_byte=%ld|orig=%02x|new=%02x\n",
               first, a[first], b[first]);
    }

    free(a); free(b);
    /* bytecode_free frees the CONTENTS; the struct is the caller's (see
       bytecode_read, which mallocs it before init). */
    bytecode_free(rt); free(rt);
    mir_free(m);
    bytecode_free(bc); free(bc);
    return diffs == 0 ? 0 : 3;
}
#endif /* MIR_ROUNDTRIP_MAIN */
