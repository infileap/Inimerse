/* Test helper: interposes getaddrinfo() and stalls before doing the real lookup,
 * so the regression test can inject a stalled resolver without needing a broken
 * DNS server or root.  Loaded with LD_PRELOAD; SLOWDNS_SECS sets the stall.
 *
 * This exists because the defect it pins is otherwise invisible: a resolver that
 * answers instantly makes an unbounded name lookup look bounded.
 *
 * It also leaves a mark.  The test needs to know that the interposition really
 * happened, and a wall-clock window cannot tell it: with the fix in place an
 * uninjected call costs a real lookup plus a full connect budget, which is
 * enough to land inside a "the stall happened" timing window.  So when
 * SLOWDNS_LOG names a file, every intercepted call appends a line to it -- a
 * fact the test can check, rather than an inference from how long something
 * took. */
#define _GNU_SOURCE
#include <netdb.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef int (*ImGetaddrinfoFn)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

static void leave_mark(const char *node, long secs) {
    const char *path = getenv("SLOWDNS_LOG");
    if (!path || !*path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "getaddrinfo node=%s stall=%ld\n", node ? node : "(null)", secs);
    fclose(f);
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res) {
    static ImGetaddrinfoFn real = NULL;
    if (!real) real = (ImGetaddrinfoFn)dlsym(RTLD_NEXT, "getaddrinfo");
    if (!real) return EAI_FAIL;
    const char *secs = getenv("SLOWDNS_SECS");
    long stall = secs ? atol(secs) : 30;
    leave_mark(node, stall);
    sleep((unsigned)stall);
    return real(node, service, hints, res);
}
