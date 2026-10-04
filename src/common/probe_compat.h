#ifndef INIMERSE_PROBE_COMPAT_H
#define INIMERSE_PROBE_COMPAT_H

/* libc gaps the transcript probes have to cross on Windows.
 *
 * mingw-w64's <stdio.h> does not declare getline() at all -- not with
 * _POSIX_C_SOURCE=200809L defined, not under -std=gnu11: `grep getline` on the
 * toolchain header is zero hits in both the UCRT64 and the MINGW64 sysroot.
 * The `#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)` guard that
 * src/verse/upp_probe.c and src/verse/crp_probe.c already carry is therefore
 * NOT why Windows fails to build them; there is simply no declaration to find,
 * and the build stops at
 *
 *   src/verse/upp_probe.c:884:19: error: implicit declaration of function
 *   'getline' [-Wimplicit-function-declaration]
 *
 * The fix is a shim rather than dropping the probes from the Windows build,
 * because excluding them would silently delete the Windows upp/crp coverage
 * that CTest runs.
 *
 * Semantics follow POSIX getline: *line/*cap own a growable buffer that the
 * caller frees, the return value counts the bytes read including the trailing
 * newline, and -1 means end of file with nothing read.
 */
#if defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ssize_t probe_getline(char **line, size_t *cap, FILE *f) {
    size_t len = 0;
    if (*line == NULL || *cap == 0) {
        *cap = 128;
        *line = (char *)malloc(*cap);
        if (*line == NULL) return -1;
    }
    for (;;) {
        if (fgets(*line + len, (int)(*cap - len), f) == NULL) {
            if (len == 0) return -1;
            break;
        }
        len += strlen(*line + len);
        if (len > 0 && (*line)[len - 1] == '\n') break;
        if (len + 1 >= *cap) {
            size_t ncap = *cap * 2;
            char *grown = (char *)realloc(*line, ncap);
            if (grown == NULL) return -1;
            *line = grown;
            *cap = ncap;
        }
    }
    return (ssize_t)len;
}

#define getline probe_getline

#endif /* _WIN32 */

#endif /* INIMERSE_PROBE_COMPAT_H */
