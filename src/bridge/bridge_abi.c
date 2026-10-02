/* bridge_abi.c - the implementation behind the generated bridge ABI.
 *
 * tools/bindgen.py turns src/bridge/inimerse_bridge.def into the C header this
 * file implements (build/bindings/c/inimerse_bridge.h).  Every function below
 * calls a real engine symbol; nothing is echoed back to the caller:
 *
 *   version()      -> INFIVERSE_VERSION          (src/common/common.h)
 *   sha256_file()  -> inim_file_sha256()         (src/compilation/checksum.c)
 *   parse_count()  -> parse_program()            (src/parser/parser.c)
 *
 * Strings are returned through a thread-local buffer that the next call on the
 * same thread overwrites.  No ownership crosses the boundary, so the JNI and
 * CPython layers copy straight out of it and never have to free engine memory.
 */
#include "inimerse_bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"                  /* Program */
#include "common.h"               /* INFIVERSE_VERSION */
#include "compilation/checksum.h" /* inim_file_sha256 */
#include "parser.h"               /* parse_program */

/* 64 hex chars of SHA-256 plus room for a version string. */
#define BRIDGE_STR_MAX 256

static _Thread_local char g_str[BRIDGE_STR_MAX];

/* A path longer than this is refused rather than silently truncated. */
#define BRIDGE_PATH_MAX 4096

int inimerse_bridge_version(const char **out, size_t *out_len) {
    if (!out) return 1;
    snprintf(g_str, sizeof g_str, "%s", INFIVERSE_VERSION);
    *out = g_str;
    if (out_len) *out_len = strlen(g_str);
    return 0;
}

int inimerse_bridge_sha256_file(const char *path, size_t path_len,
                                const char **out, size_t *out_len) {
    char name[BRIDGE_PATH_MAX];
    char hex[65];

    if (!path || !out) return 1;
    if (path_len + 1 > sizeof name) return 2;
    memcpy(name, path, path_len);
    name[path_len] = '\0';

    if (inim_file_sha256(name, hex) != 0) return 3;

    memcpy(g_str, hex, sizeof hex); /* 65 bytes: 64 hex digits + NUL */
    *out = g_str;
    if (out_len) *out_len = 64;
    return 0;
}

int inimerse_bridge_parse_count(const char *source, size_t source_len,
                                int32_t *out) {
    char *buf;
    Program *prog;

    if (!source || !out) return 1;
    if (source_len > (size_t)INT32_MAX) return 2;

    /* parse_program() takes a NUL-terminated buffer and keeps StringViews into
     * it, so the buffer is freed only after the one field we report - an int in
     * the returned struct - has been read.  No StringView is ever dereferenced. */
    buf = (char *)malloc(source_len + 1);
    if (!buf) return 3;
    memcpy(buf, source, source_len);
    buf[source_len] = '\0';

    prog = parse_program(buf);
    if (!prog) {
        free(buf);
        return 4;
    }
    *out = (int32_t)prog->count;
    free(buf);
    return 0;
}
