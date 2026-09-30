/* debug_info.h - debug sidecar emission (v0.5 roadmap: DWARF/STABS support) */
#ifndef INIMERSE_DEBUG_INFO_H
#define INIMERSE_DEBUG_INFO_H

struct Compiler;

/* Write debug sidecars for a compiled program:
 *   <output>.dbg        — text line table: one "L <offset> <line>" run per
 *                         block (main / func <name> / thread <name>), plus
 *                         STABS-style symbol entries (functions, globals).
 *   <output>.debug_line — raw DWARF 5 line-number program for the main block
 *                         (container arrives with the AOT backend).
 * Returns 0 on success, -1 on I/O error. */
int debug_write_sidecar(struct Compiler *comp, const char *source_path, const char *output);

#endif /* INIMERSE_DEBUG_INFO_H */
