/* checksum.h - file checksums for incremental compilation (v0.5 roadmap) */
#ifndef INIMERSE_CHECKSUM_H
#define INIMERSE_CHECKSUM_H

#include "../common/common.h"

/* Streaming SHA-256 of a whole file into lowercase hex (out holds 65 bytes).
 * Returns 0 on success, -1 when the file cannot be opened or read. */
int inim_file_sha256(const char *path, char out[65]);

/* Legacy wrappers kept for existing callers: all hash file contents. */
void inim_calculate_bytecode_checksum(const char *bytecode_path, char out[65]);
void inim_bytecode_obj_checksum(const char *obj_path, char out[65]);
void inim_dll_checksum(const char *dll_path, char out[65]);

#endif /* INIMERSE_CHECKSUM_H */
