#ifndef AOT_MIR_H
#define AOT_MIR_H

#include "mir.h"

/* ---------------------------------------------------------------------------
 * aot_mir -- the second front end of the AOT route: it lowers an *IrModule*
 * (the P1 MIR in src/compiler/mir.h) to a standalone C translation unit, where
 * aot_native lowers the AST.
 *
 * WHY A SECOND FRONT END AND NOT A REFACTOR OF THE FIRST.
 *   The IR's whole claim is that it is a view other backends can be driven
 *   from.  That claim is worth exactly what a second working consumer is
 *   worth: until something reads IrInsn to produce an artifact, the IR is a
 *   measuring instrument nobody measures with.  This module is that consumer.
 *   It shares the value runtime with aot_native (one preamble, one NV, one set
 *   of nv_* helpers, reached through aot_native_preamble()) and the same names
 *   (g_* globals, fn_* functions), so that where the two routes agree their
 *   output agrees byte for byte, and every difference is attributable to one
 *   line -- tools/aot_mir.test.py prints that difference as IR-MIR-C-DIFF.
 *
 * WHAT IT ACCEPTS.
 *   Exactly the opcodes aot_mir_emits() answers yes for.  An IrInsn whose
 *   opcode is not one of them is REPORTED as IR-MIR-NOT-YET and the whole
 *   translation unit is refused: a half-generated file would be a native
 *   binary that silently disagrees with the interpreter, which is the one
 *   outcome both front ends exist to avoid.
 *
 * WHERE THE OPERAND MEANINGS COME FROM.
 *   Not from the source text and not from a naming convention: from the
 *   runtime's own dispatch bodies in src/vm/vm.c, one opcode at a time.  The
 *   register mask in op_regs() is that reading written down.
 *
 * THE CONSERVATION LINE.
 *   On every call -- refused or not -- the module prints a line whose two
 *   counts are independent: the instructions the module claims to carry, and
 *   the instructions the emitter actually appended.  A generator that skips an
 *   instruction makes that equation red; a generator that computes both sides
 *   from the same walk could not.  This is the same discipline the AOT cover
 *   pin uses for its covered + missing == of equation.
 * ------------------------------------------------------------------------- */

/* Lower the module to C at out_path.
 * Returns 1 and writes the file on success.  Returns 0 and sets
 * aot_mir_last_error() when an opcode is outside the emitted set, when an
 * operand cannot be resolved (register index, float pool, global name,
 * function index), or when the file cannot be written.  On failure out_path is
 * left absent rather than half-written. */
int aot_mir_translate(const IrModule *m, const char *out_path);

/* Reason the last aot_mir_translate() returned 0.  Never NULL. */
const char *aot_mir_last_error(void);

/* Instructions carried by the module of the last call, and how many of the
 * last call's instructions were reported as not-yet rather than emitted. */
int aot_mir_last_insns(void);
int aot_mir_last_not_yet(void);

/* 1 when the generator can emit this opcode, 0 when it is reported instead.
 * The emitted set is the answer to "what does this route actually cover", so
 * it is exported rather than duplicated inside a test. */
const char *aot_mir_op_name(int opcode);
int aot_mir_emits(int opcode);
int aot_mir_emitted_count(void);
int aot_mir_emitted_opcode(int i);

/* One-line description of the emitted opcode set, for docs and diagnostics. */
const char *aot_mir_subset_description(void);

#endif /* AOT_MIR_H */
