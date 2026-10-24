#include "socket.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int verse_http_start(int port);
void verse_http_stop(void);
/* The enrollment proof POST /portal demands; defined in src/platform/http_posix.c
   so this probe can compute the very proof the listener verifies. */
void verse_portal_proof(const char *enroll_secret, const char *verse, const char *peer,
                        char *out, size_t cap);

static int pal_health(int port, char *body, size_t cap, int *status) {
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    /* Use the same raw loopback path as the rest of this probe. The generic
       HTTP client has a much longer connect timeout and can consume the whole
       CTest window when a hosted runner delays the listener thread. */
    for (int attempt = 0; attempt < 200; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (client) {
            if (im_socket_send(client, request, strlen(request)) > 0) {
                char response[512] = {0};
                int total = 0;
                while (total < (int)sizeof(response) - 1) {
                    int n = im_socket_recv(client, response + total,
                                           sizeof(response) - 1 - (size_t)total);
                    if (n <= 0) break;
                    total += n;
                    response[total] = 0;
                }
                im_socket_close(client);
                if (strstr(response, "200 OK") && strstr(response, "\"ok\":true")) {
                    if (status) *status = 200;
                    snprintf(body, cap, "%s", response);
                    return 1;
                }
            } else {
                im_socket_close(client);
            }
        }
        struct timespec ts = {0, 100000000L};
        nanosleep(&ts, NULL);
    }
    return 0;
}

static int query(int port, const char *request, const char *expect) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        int sent = im_socket_send(client, request, strlen(request));
        char response[512] = {0}; int total = 0;
        if (sent > 0) {
            while (total < (int)sizeof(response) - 1) {
                int n = im_socket_recv(client, response + total, sizeof(response) - 1 - (size_t)total);
                if (n <= 0) break;
                total += n;
                response[total] = 0;
                if (strstr(response, expect) != NULL) break;
            }
        }
        im_socket_close(client); if (total > 0 && strstr(response, expect) != NULL) return 1;
        struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL);
    }
    return 0;
}
static int request_body(int port, const char *request, char *out, size_t cap) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        int sent = im_socket_send(client, request, strlen(request));
        int total = 0;
        if (sent > 0) {
            while ((size_t)total < cap - 1) {
                int n = im_socket_recv(client, out + total, cap - 1 - (size_t)total);
                if (n <= 0) break;
                total += n;
                out[total] = 0;
            }
        }
        im_socket_close(client);
        if (total > 0) { out[total] = 0; return total; }
        struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL);
    }
    return -1;
}
/* A listener that completes the TCP handshake and then says nothing.  A client
 * with no read deadline waits here until the CTest timeout kills the probe
 * (TIMEOUT 30 for this test), which is exactly the failure the bounded read
 * exists to remove: one unresponsive endpoint must cost seconds, not a hang.
 * The check is deliberately two-sided -- returning at once would mean the read
 * was given no budget at all, which is a different defect. */
static int silent_peer_case(void) {
    ImSocket *listener = NULL;
    int port = 0;
    for (int attempt = 0; attempt < 16 && !listener; ++attempt) {
        port = 21000 + (int)(((unsigned long)getpid() * 53UL + (unsigned long)time(NULL) + (unsigned long)attempt * 131UL) % 8000UL);
        listener = im_socket_listen("127.0.0.1", (uint16_t)port, 4);
    }
    if (!listener) return 0;
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/silent", port);
    char body[256];
    int status = 0;
    uint64_t began = im_socket_now_ms();
    int rc = im_http_request("GET", url, NULL, body, sizeof body, &status);
    uint64_t elapsed = im_socket_now_ms() - began;
    im_socket_close(listener);
    printf("silent peer: rc=%d status=%d elapsed=%llums\n", rc, status, (unsigned long long)elapsed);
    if (rc == 0) return 0;                       /* a silent peer is not a response */
    if (elapsed < 1000) return 0;                /* returned before any budget was spent */
    if (elapsed > 15000) return 0;               /* gave up only by accident of some other timeout */
    return 1;
}
int main(void) {
    /* Spread probes across the ephemeral range so parallel CI jobs and quick
       reruns do not collide with a recently-closing listener. */
    int port = 20000 + (int)(((unsigned long)getpid() * 37UL + (unsigned long)time(NULL)) % 20000UL);
    setenv("INIMERSE_STATE_FILE", "/tmp/inimerse_http_probe_state", 1);
    remove("/tmp/inimerse_http_probe_state");
    setenv("CRP_TOKEN_TTL", "1", 1);
    /* The hub-side portal enrollment secret.  /portal mints a capability token
       only for a caller that proves it may open a portal for that (verse, peer);
       with this unset the listener refuses every /portal outright. */
    setenv("CRP_ENROLL_SECRET", "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100", 1);
    /* Before the server starts, so the silent listener below cannot collide with
       it and so a hang here is attributed to the client, not to the server. */
    if (!silent_peer_case()) {
        fprintf(stderr, "silent peer: the client did not abandon an unresponsive endpoint "
                        "within its read budget\n");
        return 19;
    }
    int started = 0;
    for (int attempt = 0; attempt < 12 && !started; ++attempt) {
        started = verse_http_start(port);
        if (!started) port = 20000 + ((port - 20000 + 7919) % 20000);
    }
    if (!started) return 2;
    char pal_body[512]; int pal_status = 0;
    if (!pal_health(port, pal_body, sizeof pal_body, &pal_status)) {
        fprintf(stderr, "health probe failed on 127.0.0.1:%d (status=%d, body=%s)\n",
                port, pal_status, pal_body);
        verse_http_stop();
        return 8;
    }
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, request, "\"ok\":true")) { verse_http_stop(); return 3; }
    const char *find = "GET /find?q=x HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, find, "\"items\":[")) { verse_http_stop(); return 4; }
    const char *route = "POST /route HTTP/1.1\r\nHost: localhost\r\nContent-Length: 42\r\nConnection: close\r\n\r\n{\"id\":\"peer1\",\"endpoint\":\"127.0.0.1:9000\"}";
    if (!query(port, route, "\"ok\":true")) { verse_http_stop(); return 9; }
    if (!query(port, "GET /route/peer1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", "127.0.0.1:9000")) { verse_http_stop(); return 10; }
    /* /portal mints only for a verse this hub registered -- the reference's
       `verses.has(p.verse)` (tools/crp_relay.js) -- so v1 goes in through the
       same POST /register that fills g_verses.  Without this the portal below
       is refused, which is exactly what the `ghost` case pins. */
    const char *reg = "POST /register HTTP/1.1\r\nHost: localhost\r\nContent-Length: 39\r\nConnection: close\r\n\r\n{\"id\":\"v1\",\"endpoint\":\"127.0.0.1:9000\"}";
    if (!query(port, reg, "\"ok\":true")) { verse_http_stop(); return 14; }
    /* A portal is authority: without an enrollment proof the listener must mint
       nothing and answer 403.  With one, it issues the token the rest of this
       probe runs on. */
    const char *noauth = "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: 26\r\nConnection: close\r\n\r\n{\"verse\":\"v1\",\"peer\":\"p1\"}";
    if (!query(port, noauth, "403 Forbidden")) { verse_http_stop(); return 12; }
    char proof[128] = {0};
    verse_portal_proof("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100", "v1", "p1", proof, sizeof proof);
    if (!proof[0]) { verse_http_stop(); return 13; }
    char portal_body[256];
    snprintf(portal_body, sizeof portal_body, "{\"verse\":\"v1\",\"peer\":\"p1\",\"auth\":\"%s\"}", proof);
    char portal[512] = {0};
    char portal_req[512];
    snprintf(portal_req, sizeof portal_req, "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(portal_body), portal_body);
    if (request_body(port, portal_req, portal, sizeof portal) < 0 || !strstr(portal, "\"token\":\"posix-")) { verse_http_stop(); return 5; }
    char *tp = strstr(portal, "\"token\":\""); tp += 10; char token[64] = {0};
    char *te = strchr(tp, '"'); if (!te || (size_t)(te - tp) >= sizeof token) { verse_http_stop(); return 6; }
    memcpy(token, tp, (size_t)(te - tp));
    char scoped_signal[256]; snprintf(scoped_signal, sizeof scoped_signal, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"token\":\"%s\",\"verse\":\"v1\",\"peer\":\"wrong\"}", strlen(token) + 40, token);
    if (request_body(port, scoped_signal, portal, sizeof portal) < 0 || !strstr(portal, "403 Forbidden")) { verse_http_stop(); return 11; }
    sleep(2);
    char signal[256]; snprintf(signal, sizeof signal, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"token\":\"%s\"}", strlen(token) + 12, token);
    if (request_body(port, signal, portal, sizeof portal) < 0 || !strstr(portal, "403 Forbidden")) { verse_http_stop(); return 7; }
    /* A portal whose members are not strings used to mint a token with an EMPTY
       scope, and token_allows() read an empty scope as "any" -- so that one
       token answered 200 on /signal for a verse and peer it was never proven
       for.  The mint is refused now (there is no scope to write), and whatever
       this request returned, no token from it may authorize another pair.  This
       runs before the `ghost` case below so that on an unfixed listener the
       failure is this one: the wildcard mint itself. */
    char wild_proof[128] = {0}, wild_body[256], wild_req[512], wild_res[512] = {0};
    verse_portal_proof("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100", "5", "null", wild_proof, sizeof wild_proof);
    snprintf(wild_body, sizeof wild_body, "{\"verse\":5,\"peer\":null,\"auth\":\"%s\"}", wild_proof);
    snprintf(wild_req, sizeof wild_req, "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(wild_body), wild_body);
    if (request_body(port, wild_req, wild_res, sizeof wild_res) < 0) { verse_http_stop(); return 16; }
    if (!strstr(wild_res, "404 Not Found")) {
        fprintf(stderr, "non-string portal scope was not refused: %s\n", wild_res);
        verse_http_stop(); return 17;
    }
    char wild_token[64] = {0};
    { char *wt = strstr(wild_res, "\"token\":\""); if (wt) { wt += 9; char *we = strchr(wt, '"'); if (we && (size_t)(we - wt) < sizeof wild_token) memcpy(wild_token, wt, (size_t)(we - wt)); } }
    char wide_body[256], wide_req[512], wide_res[512] = {0};
    snprintf(wide_body, sizeof wide_body, "{\"token\":\"%s\",\"verse\":\"totally-other-verse\",\"peer\":\"other-peer\"}", wild_token);
    snprintf(wide_req, sizeof wide_req, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(wide_body), wide_body);
    if (request_body(port, wide_req, wide_res, sizeof wide_res) < 0 || !strstr(wide_res, "403 Forbidden")) {
        fprintf(stderr, "a portal token authorized another pair: %s\n", wide_res);
        verse_http_stop(); return 18;
    }
    /* A valid proof is not enough on its own: the verse must be one this hub
       registered.  `ghost` never was, so a correct proof for it is refused with
       the reference's own 404 and mints nothing. */
    char ghost_proof[128] = {0}, ghost_body[256], ghost_req[512], ghost_res[512] = {0};
    verse_portal_proof("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100", "ghost", "p1", ghost_proof, sizeof ghost_proof);
    snprintf(ghost_body, sizeof ghost_body, "{\"verse\":\"ghost\",\"peer\":\"p1\",\"auth\":\"%s\"}", ghost_proof);
    snprintf(ghost_req, sizeof ghost_req, "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(ghost_body), ghost_body);
    if (request_body(port, ghost_req, ghost_res, sizeof ghost_res) < 0 || !strstr(ghost_res, "404 Not Found")) {
        fprintf(stderr, "unregistered verse was not refused: %s\n", ghost_res);
        verse_http_stop(); return 15;
    }
    verse_http_stop();
    puts("http probe: ok"); return 0;
}
