#include "socket.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void) {
    if (im_socket_init() != 0) return 2;
    ImSocket *listener = im_socket_listen("127.0.0.1", 0, 4);
    if (!listener) { im_socket_shutdown(); return 2; }
    int port = im_socket_local_port(listener); if (port <= 0) return 3;
    if (!im_socket_port_available("127.0.0.1", 0)) return 14;
    if (im_socket_set_nonblocking(listener, 1) != 0) return 4;
    (void)im_socket_accept(listener); if (!im_socket_would_block()) return 10;
    ImSocket *client = im_socket_connect("127.0.0.1", (uint16_t)port); if (!client) return 5;
    if (!im_socket_port_open("127.0.0.1", (uint16_t)port, 500)) return 15;
    ImSocket *accepted = NULL; for (int i = 0; i < 100 && !accepted; i++) { accepted = im_socket_accept(listener); }
    if (!accepted) return 6;
    if (im_socket_set_nonblocking(accepted, 0) != 0) return 16;
    const char *msg = "ping"; if (im_socket_send(client, msg, 4) != 4) return 7;
    /* The send and the arrival of those bytes on the accepted socket are two
     * separate events; loopback is fast but not instantaneous, and under a
     * loaded machine (ctest -j12) the bytes can still be in flight when we
     * look.  Reading 0 here is not a defect -- poll briefly, the same way the
     * loop just below already does for the payload itself.  Peeking once made
     * this probe fail roughly 2 in 480 runs with exit 11. */
    int peeked = 0;
    for (int i = 0; i < 200 && peeked <= 0; ++i) {
        peeked = im_socket_peek(accepted);
        if (peeked <= 0) { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    }
    if (peeked <= 0) return 11;
    char buf[8] = {0}; int received = 0;
    for (int i = 0; i < 20 && received < 4; ++i) {
        int n = im_socket_recv(accepted, buf + received, sizeof(buf) - (size_t)received);
        if (n > 0) received += n;
    }
    if (received != 4) return 8;
    if (memcmp(buf, msg, 4) != 0) return 9;
    if (im_socket_peek(NULL) >= 0) return 12;
    if (im_socket_send(NULL, msg, 4) >= 0) return 13;
    im_socket_close(client); im_socket_close(accepted);
    im_socket_close(listener); im_socket_shutdown(); puts("socket probe: ok"); return 0;
}
