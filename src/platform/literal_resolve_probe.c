/* Asserts that a host which is already an address literal does not reach the
 * resolver, while a hostname still does.
 *
 * Why a call count and not a clock: `resolve_addr("127.0.0.1")` and
 * `resolve_addr("localhost")` return the same address, so the defect has no
 * portable wrong answer to compare against.  The only difference is that the
 * first one used to pay for a resolver round trip, and how much that costs is
 * platform-specific -- Linux answers a literal in microseconds either way,
 * Windows (ucrt64) measured 0.9-3.7 s per resolution, which is what pushed
 * `socket_probe` past the CTest 10 s ceiling.  A timing assertion would be
 * machine-dependent on the platform where the bug is invisible and vacuous on
 * the platform where it bites.  Counting `getaddrinfo` calls, through the
 * GA_LOG shim, is exact on both.
 *
 * The second half is not decoration.  "The literal path works" is satisfied
 * just as well by never calling the resolver at all, which would be a much
 * worse defect; requiring the hostname to reach getaddrinfo is what says the
 * fallback is still there.  See docs/AUDIT.md §1.63. */
#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int calls_logged(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = 0;
    for (int c = fgetc(f); c != EOF; c = fgetc(f)) if (c == '\n') n++;
    fclose(f);
    return n;
}

int main(void) {
    const char *log = getenv("GA_LOG");
    if (!log || !*log) { fprintf(stderr, "GA_LOG is not set\n"); return 2; }
    unlink(log);
    if (im_socket_init() != 0) { fprintf(stderr, "im_socket_init failed\n"); return 1; }

    /* 1. A literal must not reach the resolver. */
    ImSocket *a = im_socket_listen("127.0.0.1", 45913, 4);
    int after_literal = calls_logged(log);
    if (!a) { fprintf(stderr, "listen on a literal host failed\n"); return 3; }
    if (after_literal != 0) {
        fprintf(stderr, "a literal host reached getaddrinfo %d time(s); the fast path is gone\n", after_literal);
        im_socket_close(a); return 4;
    }
    im_socket_close(a);

    /* 1b. The same must hold for the bounded-connect entry point, which is a
     *     separate resolver call site -- otherwise the fast path would be one
     *     rule for `listen` and another for `connect`. */
    (void)im_socket_connect_timeout("127.0.0.1", 45913, 50);
    int after_connect = calls_logged(log);
    if (after_connect != after_literal) {
        fprintf(stderr, "im_socket_connect_timeout reached getaddrinfo for a literal (%d call(s))\n",
                after_connect - after_literal);
        return 6;
    }

    /* 2. A hostname must still reach it. */
    ImSocket *b = im_socket_listen("localhost", 45914, 4);
    int after_name = calls_logged(log);
    if (b) im_socket_close(b);
    if (after_name <= after_connect) {
        fprintf(stderr, "a hostname did not reach getaddrinfo; the fallback is gone, not the resolver\n");
        return 5;
    }

    printf("literal-resolve literal=%d name=%d\n", after_connect, after_name - after_connect);
    printf("literal_resolve tests: ok\n");
    im_socket_shutdown();
    return 0;
}
