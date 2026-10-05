/* Test helper: interposes getaddrinfo() and records every call in the file
 * named by GA_LOG, then performs the real lookup.
 *
 * It exists because the defect it pins has no portable observable result.  A
 * literal host and a hostname resolve to the same address, so the *answer* is
 * identical whether or not the resolver ran; only the cost differs, and that
 * cost is platform-specific (on Linux a literal is a few microseconds either
 * way, on Windows it was measured at 0.9-3.7 s per call).  A wall-clock
 * assertion would therefore be both machine-dependent and, on Linux, blind.
 *
 * Counting calls is the fact that does not move: a literal that skips the
 * resolver produces no line, and the fallback that must stay intact produces
 * one for a hostname.  Loaded with LD_PRELOAD; the probe reads the log.
 * See docs/AUDIT.md §1.63. */
#define _GNU_SOURCE
#include <netdb.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*ImGetaddrinfoFn)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

static void record(const char *node) {
    const char *path = getenv("GA_LOG");
    if (!path || !*path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "getaddrinfo node=%s\n", node ? node : "(null)");
    fclose(f);
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res) {
    static ImGetaddrinfoFn real = NULL;
    if (!real) real = (ImGetaddrinfoFn)dlsym(RTLD_NEXT, "getaddrinfo");
    if (!real) return EAI_FAIL;
    record(node);
    return real(node, service, hints, res);
}
