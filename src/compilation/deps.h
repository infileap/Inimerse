/* deps.h - .inim dependency trailer for incremental compilation (v0.5
 * roadmap: source dependency graph + SHA-256 checksums + incremental flag).
 *
 * The trailer is appended AFTER the bytecode stream; bytecode_read_file()
 * streams with fread and never checks EOF, so old readers stay compatible.
 *
 * Layout (little-endian, appended at end of file):
 *   [per-entry: u32 path_len][path bytes][64-byte lowercase sha256 hex]
 *   [u32 DEPS_TRAILER_MAGIC][u32 abi_version][u32 count][u32 trailer_len]
 * trailer_len counts everything appended after the bytecode stream. */
#ifndef INIMERSE_DEPS_H
#define INIMERSE_DEPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEPS_TRAILER_MAGIC 0x53504449u /* "IDPS" */

typedef struct {
    char *path;      /* absolute source path */
    char sha_hex[65]; /* SHA-256 of the source file, lowercase hex + NUL */
} DepEntry;

/* Append the dependency trailer to an existing .inim file. Returns 0 on
 * success, -1 on I/O error (the bytecode itself stays valid either way). */
int deps_write_trailer(const char *bc_path, const DepEntry *deps, int count, int abi_version);

/* Read the trailer from a .inim file. Returns 0 and fills *out_deps/*out_count
 * (caller frees with deps_free) when a valid trailer exists; -1 when the file
 * is missing, has no trailer, or the trailer is corrupt. */
int deps_read(const char *bc_path, DepEntry **out_deps, int *out_count, int *out_abi_version);

void deps_free(DepEntry *deps, int count);

/* Relative-path helpers (reproducible builds: trailer paths are stored
 * relative to the .inim file's directory so identical project layouts
 * produce identical bytes on any host). */
/* abs_target -> path relative to from_dir (no trailing slash), into out. */
void deps_relative_path(const char *from_dir, const char *abs_target, char *out, size_t out_sz);
/* entry.path (relative to bc_path's dir) -> absolute path into out. */
void deps_entry_abs_path(const DepEntry *dep, const char *bc_path, char *out, size_t out_sz);
/* dirname of bc_path into out. */
void deps_bc_dirname(const char *bc_path, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* INIMERSE_DEPS_H */
