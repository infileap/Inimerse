#include "socket.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
int verse_http_start(int port); void verse_http_stop(void);

/* A WebSocket frame is asserted at a SECOND client, not at its sender.
 *
 * The server broadcasts every frame to every OTHER registered client and never
 * echoes to the sender (src/platform/http_posix.c:1094-1096,
 * `if (g_ws_clients[i] && g_ws_clients[i] != client)`).  This probe used to read
 * its own frame back from the server, so its second im_socket_recv() waited for
 * a byte that is never written: the probe's main thread and the server's
 * ws_client_thread parked in recv on each other -- both in wait_woken -- the
 * process never returned, and it printed nothing.  Measured rc=124 at a 25 s
 * timeout, stdout 0 bytes.  Two clients make every assertion here reachable and
 * assert the contract the server actually has.
 *
 * Every wait below is bounded.  A probe whose failure mode is "never returns"
 * reports nothing -- not even that it is stuck -- so the shape that hung is
 * removed, not just the one call that hung: both sockets are non-blocking and
 * every read runs against a deadline, which turns silence into a return code.
 */
static const char *HANDSHAKE =
    "GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n\r\n";

static const int POLL_MS = 2;

/* Read until `need` bytes have arrived, or `ms` elapse.  Returns what it got. */
static int recv_at_least(ImSocket *s, unsigned char *buf, int need, int ms) {
    struct timespec ts = {0, POLL_MS * 1000000L};
    int got = 0, waited = 0;
    while (got < need && waited < ms) {
        int n = im_socket_recv(s, buf + got, need - got);
        if (n > 0) { got += n; waited = 0; continue; }
        nanosleep(&ts, NULL); waited += POLL_MS;
    }
    return got;
}

static int handshake(ImSocket *s) {
    char resp[512] = {0}; int got = 0, waited = 0;
    struct timespec ts = {0, POLL_MS * 1000000L};
    if (im_socket_send(s, HANDSHAKE, strlen(HANDSHAKE)) <= 0) return 0;
    while (got < (int)sizeof resp - 1 && waited < 2000) {
        int n = im_socket_recv(s, resp + got, sizeof resp - 1 - (size_t)got);
        if (n > 0) {
            got += n; resp[got] = 0; waited = 0;
            if (strstr(resp, "101 Switching Protocols")) return 1;
            continue;
        }
        nanosleep(&ts, NULL); waited += POLL_MS;
    }
    return 0;
}

int main(void) {
    const int port = 18124; int rc = 0;
    ImSocket *reader = NULL, *writer = NULL;
    if (!verse_http_start(port)) return 2;
    reader = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
    if (!reader) { rc = 3; goto done; }
    (void)im_socket_set_nonblocking(reader, 1);
    if (!handshake(reader)) { rc = 4; goto done; }
    /* The 101 goes out at http_posix.c:1010 and the broadcast slot is taken at
       http_posix.c:1041, so the reader is reachable only a moment after its
       handshake returns. */
    { struct timespec ts = {0, 50000000L}; nanosleep(&ts, NULL); }
    writer = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
    if (!writer) { rc = 3; goto done; }
    (void)im_socket_set_nonblocking(writer, 1);
    if (!handshake(writer)) { rc = 5; goto done; }
    unsigned char frame[10] = {0x81, 0x84, 1, 2, 3, 4, 0x71, 0x6b, 0x6d, 0x63};
    if (im_socket_send(writer, frame, sizeof frame) != (int)sizeof frame) { rc = 6; goto done; }
    /* Header and payload leave as two send()s, so they may arrive as two reads:
       this is how any WebSocket reader has to work, not a workaround. */
    unsigned char echo[6] = {0};
    if (recv_at_least(reader, echo, 6, 3000) != 6 || echo[0] != 0x81 || echo[1] != 4 || echo[2] != 0x70 || echo[3] != 0x69 || echo[4] != 0x6e || echo[5] != 0x67) { rc = 7; goto done; }
done:
    if (writer) im_socket_close(writer);
    if (reader) im_socket_close(reader);
    verse_http_stop(); if (rc) return rc;
    puts("websocket probe: ok"); return 0;
}
