/* Regression pin for the unbounded name lookup in im_socket_connect_timeout().
 *
 * The defect was invisible without help: getaddrinfo() has no timeout in any
 * standard, so a resolver that answers quickly made an unbounded lookup look
 * bounded.  http_get() against a host whose DNS was black-holed hung the engine
 * until the OS resolver gave up -- minutes, not milliseconds -- while the
 * function's name and its caller both promised a bound.
 *
 * src/platform/slowdns_preload.c injects the stall via LD_PRELOAD, so this test
 * needs no broken DNS server and no root, and it is hermetic: it never depends
 * on the network being reachable.
 *
 * Phase A is self-validating on purpose, and it proves the injection with a
 * mark rather than with a stopwatch.  It asserts not only that the call came
 * back inside the bound, but that getaddrinfo() was actually interposed -- so
 * if the preload ever stops loading, this test FAILS loudly instead of quietly
 * becoming a test of nothing.
 *
 * The stopwatch alone was not enough.  With the fix in place, a run with no
 * injection at all costs a real lookup plus a full connect budget, which can
 * land inside any "the stall must have happened" timing window; that version of
 * the check missed the missing preload about one run in eleven.  A file the
 * interposer appends to cannot be slow, so it cannot be mistaken. */
#define _POSIX_C_SOURCE 200809L
#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static char g_mark[128];

/* Non-empty iff the interposer appended its line. */
static int mark_present(void) {
    FILE *f = fopen(g_mark, "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    return c != EOF;
}

static double elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (b->tv_sec - a->tv_sec) * 1000.0 + (b->tv_nsec - a->tv_nsec) / 1e6;
}

static double one_call(const char *host, int timeout_ms, int *connected) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ImSocket *s = im_socket_connect_timeout(host, 80, timeout_ms);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    *connected = s ? 1 : 0;
    if (s) im_socket_close(s);
    return elapsed_ms(&t0, &t1);
}

int main(void) {
    int failures = 0;
    if (im_socket_init() != 0) { printf("resolve_bound: FAIL socket init\n"); return 1; }

    /* Phase A: a 10 s resolver stall, a 2 s bound.  The call must refuse inside
     * the bound -- and the interposer must have left its mark, or the injection
     * is gone and this phase is testing nothing. */
    snprintf(g_mark, sizeof g_mark, "/tmp/inimerse_resolve_bound_%ld.marker", (long)getpid());
    unlink(g_mark);
    setenv("SLOWDNS_LOG", g_mark, 1);
    setenv("SLOWDNS_SECS", "10", 1);
    int connected = 0;
    double a = one_call("example.com", 2000, &connected);
    printf("resolve_bound stall: elapsed_ms=%.0f connected=%d (bound=2000, injected stall=10000)\n", a, connected);
    int marked = mark_present();
    printf("resolve_bound mark: intercepted=%d\n", marked);
    if (!marked) {
        printf("resolve_bound: FAIL getaddrinfo was not interposed; the pin is vacuous\n");
        failures++;
    }
    if (a > 3500.0) {
        printf("resolve_bound: FAIL the bound did not hold (%.0f ms for a 2000 ms timeout)\n", a);
        failures++;
    } else if (marked && a < 1500.0) {
        printf("resolve_bound: FAIL a stall of 10 s returned in %.0f ms\n", a);
        failures++;
    }

    /* Phase B: no stall, and a numeric host so nothing here depends on the
     * network.  The bound must not make an ordinary call slow or wrong. */
    setenv("SLOWDNS_SECS", "0", 1);
    double b = one_call("127.0.0.1", 5000, &connected);
    printf("resolve_bound normal: elapsed_ms=%.0f connected=%d\n", b, connected);
    if (b > 2000.0) {
        printf("resolve_bound: FAIL an ordinary call took %.0f ms\n", b);
        failures++;
    }

    im_socket_shutdown();
    if (failures) { printf("resolve_bound: %d check(s) failed\n", failures); return 1; }
    printf("resolve_bound: ok\n");
    return 0;
}
