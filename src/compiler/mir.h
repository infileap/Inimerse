#ifndef INIMERSE_MIR_H
#define INIMERSE_MIR_H

/* P1 of docs/IR_DESIGN.md: the MIR that sits between the AST and the encoding.

   WHAT THIS IS, AND WHAT IT IS NOT
   --------------------------------
   This is a *view*, not a second copy of the program.  docs/IR_DESIGN.md section
   4.4 says it in one line: `RegInstruction` already is MIR's flat encoding, and
   `Bytecode` already is the container-side view of `IrModule`.  So the module
   built here aliases the container's pools instead of duplicating them, and
   `mir_to_bytecode` regenerates the instruction stream *from the IrInsn array*
   (never by copying `bc->code`).  If that were not true, the roundtrip in
   tools/mir_roundtrip.test.py would compare a struct against itself and prove
   nothing.

   P1 does no optimisation and no type inference on purpose (section 10: the P1
   criterion is "structure right, invertible").  Every IrType is IR_T_UNKNOWN and
   every flag is zero except the two operand-width bits declared below, because
   the only authority for effects and types is the `OpSemantics` table generated
   from tools/ir_spec.py, and that generator is P2/P3.  Filling `flags` here from
   a second, hand-written table would be exactly the drift the design exists to
   remove.

   THREE PLACES WHERE THE DESIGN AS WRITTEN CANNOT BE IMPLEMENTED, AND WHAT P1
   DOES INSTEAD.  Each is marked "DEVIATION" below and repeated in the report:

   1. `IrInsn` has no room for a full 32-bit operand.  The design gives
      `int16_t a, b` and one `int32_t imm`.  But OP_LOADK_INT carries a
      sign-extended int32 in r2, OP_SET_INTERVAL packs (string idx | flags<<24)
      into r2, and OP_LOADK_I64 carries *two* unsigned 32-bit halves in r2 and r3.
      Truncating any of those into 16 bits loses bits silently and the roundtrip
      stops being byte-exact.  P1 therefore adds `int32_t wide` and two flag bits
      (IR_F_WIDE_A / IR_F_WIDE_B): the truncated value stays in a/b so single
      readers see something plausible, and the exact 32-bit value is recoverable.
      Only the ops that actually need it pay for it.

   2. `IrModule` has no int constant pool to point at.  The design declares
      `Value *kint; int nkint;`, but the encoding has no int pool at all: int
      literals are immediates (see 1).  A pool that does not exist cannot be
      aliased.  P1 keeps the field (as an opaque pointer, so that src/compiler
      stays free of src/vm/vm.h -- nothing in this directory includes it today
      and this prototype is not the place to start) and leaves it NULL/nkint=0.
      The field becomes a real `Value *` on the day the encoding grows a pool.

   3. `IrModule` as specified is not closed under inversion.  Function *names*,
      per-function arg counts and pools, the thread section, the try table, the
      global-name table, the capture table and the label table are all container
      facts with no IrFunc field to hold them.  A byte-exact roundtrip therefore
      cannot be computed from `funcs` + pools alone.  P1 keeps `IrModule.bc` as an
      alias to the container and `mir_to_bytecode` reads those facts from it --
      they are *carried*, not derived, and the report says exactly which bytes are
      regenerated versus carried.  Without this alias the "hard gate" in the task
      has no input.
*/

#include <stdint.h>
#include "bytecode.h"

/* The design's type hints (section 4.1).  IR_T_UNKNOWN and IR_T_ANY_CONTAINER
   are deliberately two values: "not decided yet / never decidable" versus "known
   to be a container, kind undecided".  Collapsing them would disable the
   static-mode rule "UNKNOWN must be eliminated". */
typedef enum {
    IR_T_UNKNOWN = 0,   /* dynamic: `var` binding, cross-function return, container element */
    IR_T_INT, IR_T_FLOAT, IR_T_BOOL, IR_T_STRING,
    IR_T_NIL, IR_T_OBJECT, IR_T_FUNC, IR_T_ANY_CONTAINER
} IrType;

/* IrInsn.flags */
#define IR_F_PURE       0x01u
#define IR_F_MAY_THROW  0x02u
#define IR_F_MAY_ALLOC  0x04u
#define IR_F_TERMINATOR 0x08u
/* DEVIATION 1: the low 16 bits of this operand did not fit, the exact value is
   in ir_insn_r2()/ir_insn_r3() (see mir.c).  Not a semantic flag. */
#define IR_F_WIDE_A     0x10u
#define IR_F_WIDE_B     0x20u

typedef struct {
    uint16_t op;        /* OpSemantics row number == the encoding's number */
    uint8_t  flags;     /* IR_F_* */
    int16_t  dst;       /* destination register, -1 = none (r1) */
    int16_t  a, b;      /* two sources (r2, r3): register number or pool index */
    int32_t  imm;       /* immediate / jump target / pool index (r2 when wide) */
    /* DEVIATION 1: exact r3 when it does not fit int16_t (only OP_LOADK_I64 in
       today's opcode set).  Zero when unused. */
    int32_t  wide;
    IrType   type;      /* result type hint; IR_T_UNKNOWN everywhere in P1 */
} IrInsn;

/* One basic block.  start/end are instruction indices, end exclusive.  In the
   design these are int16_t, which caps one function at 32767 instructions --
   `Bytecode.count` is int, so a longer function cannot be represented.  P1 keeps
   the declared width and mir_from_bytecode refuses (returns NULL) rather than
   truncating; the corpus maximum is reported by the roundtrip test. */
typedef struct {
    int16_t start, end;
    int succ[2], nsucc;     /* successor block indices */
    IrType stack_hint;
} IrBlock;

typedef struct {
    int id, argc;
    IrInsn *insns; int ninsns, cap;
    IrBlock *blocks; int nblocks;
    int nregs;                 /* frame registers; unknown (0) in P1 */
    IrType ret;
    uint32_t effects;          /* 0 in P1: effects come from OpSemantics, see above */
} IrFunc;

/* The pool is char*[] (NUL-terminated), so a StringView run cannot alias it and
   the run is materialised.  StringView itself comes from src/common/common.h --
   reinventing it here would silently give the module a second, incompatible
   view type with the same name. */
typedef struct IrModule {
    IrFunc *funcs; int nfuncs;          /* DFS order: self, funcs, then threads */
    void *kint;                         /* DEVIATION 2: no int pool exists; NULL */
    double *kfloat;                     /* aliases bc->float_pool */
    StringView *kstr;                   /* materialised (pool is char**, not a viewable run) */
    int nkint, nkfloat, nkstr;
    const struct Bytecode *bc;          /* DEVIATION 3: what the roundtrip needs */
    int nfuncs_main;                    /* 1 + bc->func_count at the top level */
} IrModule;

/* Build the view.  Returns NULL if the container cannot be represented (see the
   int16_t note on IrBlock).  The module keeps pointers into `bc`: `bc` must
   outlive it, and must not be mutated while the module is alive. */
IrModule *mir_from_bytecode(const struct Bytecode *bc);

/* Regenerate a container from the view.  The instruction stream is emitted from
   the IrInsn arrays; pools, names, arg counts, try entries, global names and the
   thread section are carried through from m->bc (they have no MIR field).  The
   result is byte-identical to m->bc when the view is faithful.  Caller owns it
   (bytecode_free). */
struct Bytecode *mir_to_bytecode(const IrModule *m);

void mir_free(IrModule *m);

/* The exact operand values (DEVIATION 1).  Any consumer that wants r2/r3 should
   read these rather than `a`/`b`, and gets the right answer for every opcode. */
int32_t ir_insn_r2(const IrInsn *in);
int32_t ir_insn_r3(const IrInsn *in);

/* Build a module with the operand of instruction `idx` of the function with
   module id `id` changed (negative control for the roundtrip test: a comparison
   that cannot see this change is not comparing anything). */
int mir_perturb(IrModule *m, int id, int idx, int field, int32_t value);

#endif /* INIMERSE_MIR_H */
