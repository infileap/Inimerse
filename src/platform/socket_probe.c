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
    /* The liveness check has to ask *some* port.  Asking this one costs a whole
     * connection: port_open() connects and closes, so that dead connection sits
     * in this listener's backlog, and accept() below is a coin flip between it
     * and our client.  Lose the toss and the probe talks to a closed socket --
     * exit 11 after its 2s budget, roughly 1 run in 480 under ctest -j12, and
     * 4 runs in 480 at 12-way stress.  So ask a throwaway listener instead: the
     * same assertion, and this listener's backlog only ever holds the client. */
    ImSocket *throwaway = im_socket_listen("127.0.0.1", 0, 1);
    if (!throwaway) { im_socket_close(listener); im_socket_shutdown(); return 2; }
    int probe_port = im_socket_local_port(throwaway); if (probe_port <= 0) return 3;
    if (!im_socket_port_open("127.0.0.1", (uint16_t)probe_port, 500)) return 15;
    im_socket_close(throwaway);
    if (im_socket_set_nonblocking(listener, 1) != 0) return 4;
    (void)im_socket_accept(listener); if (!im_socket_would_block()) return 10;
    ImSocket *client = im_socket_connect("127.0.0.1", (uint16_t)port); if (!client) return 5;
    /* connect() returning does not mean the kernel has finished SYN/ACK, so
     * accept() can still see nothing for a while.  A bare 100-iteration spin is
     * a bet on machine speed, not on behaviour: it failed exit 6 under a 24-way
     * stress run.  Poll with a sleep, the same way the peek loop below does. */
    ImSocket *accepted = NULL;
    for (int i = 0; i < 2000 && !accepted; i++) {
        accepted = im_socket_accept(listener);
        if (!accepted) { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    }
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
    /* 200 x 1ms was still a budget, and it was spent: the 24-way stress run hit
     * exit 11 twice.  2s is not a timing assumption about the machine, it is
     * far enough out that only a real defect can reach it. */
    for (int i = 0; i < 2000 && peeked <= 0; ++i) {
        peeked = im_socket_peek(accepted);
        if (peeked <= 0) { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    }
    if (peeked <= 0) return 11;
    char buf[8] = {0}; int received = 0;
    for (int i = 0; i < 200 && received < 4; ++i) {
        int n = im_socket_recv(accepted, buf + received, sizeof(buf) - (size_t)received);
        if (n > 0) received += n;
        else { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    }
    if (received != 4) return 8;
    if (memcmp(buf, msg, 4) != 0) return 9;
    if (im_socket_peek(NULL) >= 0) return 12;
    if (im_socket_send(NULL, msg, 4) >= 0) return 13;
    im_socket_close(client); im_socket_close(accepted);
    im_socket_close(listener); im_socket_shutdown(); puts("socket probe: ok"); return 0;
}
