/* protocol_probe.c - P1 increment 3: session, negotiation and authority checks
 *
 * Exercises the session logic in-process.  The cross-process half of the story
 * lives in tools/verse_closed_loop.test.py.
 */
#include "protocol.h"
#include "eventlog.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define ROOT "verse_protocol_probe_root"
#define VERSE "main"

static void rm_rf(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char child[1024];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            rm_rf(child);
        }
        closedir(d);
        rmdir(path);
        return;
    }
    remove(path);
}

static void clean_root(void) { rm_rf(ROOT); }

/* send one request line and return the response */
static const char *send(VlServer *s, const char *line, char *buf, size_t cap) {
    int stop = vl_server_handle(s, line, buf, cap);
    (void)stop;
    return buf;
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* ---- 1. hello negotiates, and an unimplemented capability is refused ---- */
static void test_negotiation(void) {
    clean_root();
    VlManifest m;
    CHECK(vl_layer_create(ROOT, VERSE, &m) == VL_OK, "create layer");

    VlServer *s = vl_server_open(ROOT, VERSE);
    CHECK(s != NULL, "open server");
    if (!s) return;

    char r[8192];

    /* an op before hello is refused, not silently accepted */
    send(s, "{\"op\":\"put\",\"key\":\"k\",\"cell\":\"0,0,0\",\"value\":1}", r, sizeof r);
    CHECK(has(r, "\"code\":\"no_session\""), "put before hello refused: %s", r);
    CHECK(vl_server_negotiated(s) == 0, "session still unnegotiated");

    /* drain is the one op reachable without a session: it is what an operator
     * needs precisely when a session cannot be opened, and it mutates no
     * application state. */
    send(s, "{\"op\":\"drain\"}", r, sizeof r);
    CHECK(has(r, "\"drained\":true"), "drain is reachable without a session: %s", r);
    CHECK(vl_server_negotiated(s) == 0, "drain did not open a session");

    /* The rule: mutations (put/undo) require a session; drain and status are
     * unauthenticated read/diagnostic ops, and status additionally refuses
     * whenever the anchor is broken.  That keeps an operator able to diagnose
     * a Layer that will not accept a session. */
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"ok\":true"), "status is readable without a session: %s", r);
    CHECK(has(r, "\"seq\":0"), "status reports an empty layer before hello: %s", r);

    /* a capability the server does not implement is an explicit refusal */
    send(s, "{\"op\":\"hello\",\"protocol\":1,\"client\":\"p\",\"caps\":[\"put\",\"time_travel\"]}", r, sizeof r);
    CHECK(has(r, "\"code\":\"capability_refused\""), "unknown cap refused: %s", r);
    CHECK(has(r, "time_travel"), "refusal names the capability: %s", r);
    CHECK(vl_server_negotiated(s) == 0, "failed hello does not open a session");

    /* a wrong protocol version is refused and names both versions */
    send(s, "{\"op\":\"hello\",\"protocol\":7,\"caps\":[]}", r, sizeof r);
    CHECK(has(r, "\"code\":\"protocol_mismatch\""), "protocol mismatch refused: %s", r);

    /* the real handshake reports the agreed set */
    send(s, "{\"op\":\"hello\",\"protocol\":1,\"client\":\"probe\",\"caps\":[\"put\",\"undo\",\"drain\"]}", r, sizeof r);
    CHECK(has(r, "\"ok\":true"), "hello ok: %s", r);
    CHECK(has(r, "\"verse_id\":\"main\""), "hello names the verse: %s", r);
    CHECK(has(r, "\"seq\":0"), "hello reports seq 0: %s", r);
    CHECK(vl_server_negotiated(s) == 1, "session negotiated");

    vl_server_close(s);
    clean_root();
}

/* ---- 2. the client structurally cannot assert final state ---- */
static void test_client_authority(void) {
    clean_root();
    VlManifest m;
    vl_layer_create(ROOT, VERSE, &m);
    VlServer *s = vl_server_open(ROOT, VERSE);
    if (!s) { CHECK(0, "open server"); return; }

    char r[8192];
    send(s, "{\"op\":\"hello\",\"protocol\":1,\"client\":\"p\",\"caps\":[\"put\"]}", r, sizeof r);

    /* each forbidden field must be named in the refusal */
    const char *fields[] = { "seq", "rev", "head", "balance", "state_hash", "committed" };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        char line[256];
        snprintf(line, sizeof line,
                 "{\"op\":\"put\",\"key\":\"k\",\"cell\":\"0,0,0\",\"value\":1,\"%s\":999}", fields[i]);
        send(s, line, r, sizeof r);
        CHECK(has(r, "\"code\":\"client_authority\""), "'%s' refused: %s", fields[i], r);
        CHECK(has(r, fields[i]), "refusal names '%s': %s", fields[i], r);
    }

    /* and none of those attempts changed the authoritative state */
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"seq\":0"), "no rejected request advanced seq: %s", r);
    CHECK(has(r, "\"cells\":[]"), "no rejected request created a cell: %s", r);

    vl_server_close(s);
    clean_root();
}

/* ---- 3. accepted puts advance seq, and an unknown op is a clean error ---- */
static void test_put_flow(void) {
    clean_root();
    VlManifest m;
    vl_layer_create(ROOT, VERSE, &m);
    VlServer *s = vl_server_open(ROOT, VERSE);
    if (!s) { CHECK(0, "open server"); return; }

    char r[8192];
    send(s, "{\"op\":\"hello\",\"protocol\":1,\"client\":\"p\",\"caps\":[\"put\",\"undo\",\"drain\",\"status\"]}", r, sizeof r);

    send(s, "{\"op\":\"put\",\"key\":\"k1\",\"cell\":\"0,0,0\",\"value\":11}", r, sizeof r);
    CHECK(has(r, "\"ok\":true") && has(r, "\"seq\":1"), "first put seq 1: %s", r);

    send(s, "{\"op\":\"put\",\"key\":\"k2\",\"cell\":\"0,0,0\",\"value\":22}", r, sizeof r);
    CHECK(has(r, "\"seq\":2"), "second put seq 2: %s", r);

    /* the same idempotency key must not append a second record */
    send(s, "{\"op\":\"put\",\"key\":\"k2\",\"cell\":\"0,0,0\",\"value\":22}", r, sizeof r);
    CHECK(has(r, "\"ok\":true"), "idempotent retry ok: %s", r);
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"seq\":2"), "idempotent retry did not advance seq: %s", r);
    CHECK(has(r, "\"value\":22"), "cell holds the retried value: %s", r);

    /* undo restores the previous value and is itself idempotent */
    send(s, "{\"op\":\"undo\",\"key\":\"u1\",\"target\":2}", r, sizeof r);
    CHECK(has(r, "\"ok\":true") && has(r, "\"seq\":3"), "undo seq 3: %s", r);
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"value\":11"), "undo restored the previous value: %s", r);

    send(s, "{\"op\":\"undo\",\"key\":\"u1\",\"target\":2}", r, sizeof r);
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"seq\":3"), "repeated undo did not append: %s", r);
    CHECK(has(r, "\"value\":11"), "repeated undo did not double-apply: %s", r);

    /* an undo of a sequence that never existed is refused before any write */
    send(s, "{\"op\":\"undo\",\"key\":\"u9\",\"target\":999}", r, sizeof r);
    CHECK(has(r, "\"ok\":false"), "undo of a missing target refused: %s", r);
    send(s, "{\"op\":\"status\"}", r, sizeof r);
    CHECK(has(r, "\"seq\":3"), "refused undo did not append: %s", r);

    /* unknown op and malformed line are errors, not crashes */
    send(s, "{\"op\":\"teleport\"}", r, sizeof r);
    CHECK(has(r, "\"code\":\"malformed\""), "unknown op reported: %s", r);
    send(s, "not json at all", r, sizeof r);
    CHECK(has(r, "\"ok\":false"), "malformed line reported: %s", r);

    vl_server_close(s);
    clean_root();
}

/* ---- 4. drain is a durability barrier, bye drains implicitly ---- */
static void test_drain(void) {
    clean_root();
    VlManifest m;
    vl_layer_create(ROOT, VERSE, &m);
    VlServer *s = vl_server_open(ROOT, VERSE);
    if (!s) { CHECK(0, "open server"); return; }

    char r[8192];
    send(s, "{\"op\":\"hello\",\"protocol\":1,\"client\":\"p\",\"caps\":[\"put\",\"drain\"]}", r, sizeof r);
    send(s, "{\"op\":\"put\",\"key\":\"k1\",\"cell\":\"1,1,1\",\"value\":5}", r, sizeof r);

    send(s, "{\"op\":\"drain\"}", r, sizeof r);
    CHECK(has(r, "\"drained\":true"), "drain reports drained: %s", r);
    CHECK(has(r, "\"pending\":0"), "drain reports nothing pending: %s", r);
    CHECK(has(r, "\"seq\":1"), "drain reports the committed seq: %s", r);

    /* the snapshot written by drain must be reloadable */
    char snap[1024];
    snprintf(snap, sizeof snap, "%s/verse/%s/snapshots/state.json", ROOT, VERSE);
    CHECK(access(snap, F_OK) == 0, "drain wrote a snapshot: %s", snap);

    /* bye carries the closing drain result */
    int stop = vl_server_handle(s, "{\"op\":\"bye\"}", r, sizeof r);
    CHECK(stop == 1, "bye ends the session: %d", stop);
    CHECK(has(r, "\"drained\":true"), "bye reports a clean drain: %s", r);

    vl_server_close(s);
    clean_root();
}

int main(void) {
    test_negotiation();
    test_client_authority();
    test_put_flow();
    test_drain();

    printf("protocol_probe: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
