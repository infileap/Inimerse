#include "socket.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    if (im_socket_init() != 0) return 2;
    ImSocket *listener = im_socket_listen("127.0.0.1", 0, 4);
    if (!listener) { im_socket_shutdown(); return 2; }
    int port = im_socket_local_port(listener); if (port <= 0) return 3;
    if (im_socket_set_nonblocking(listener, 1) != 0) return 4;
    (void)im_socket_accept(listener); (void)im_socket_would_block();
    ImSocket *client = im_socket_connect("127.0.0.1", (uint16_t)port); if (!client) return 5;
    ImSocket *accepted = NULL; for (int i = 0; i < 100 && !accepted; i++) { accepted = im_socket_accept(listener); }
    if (!accepted) return 6;
    const char *msg = "ping"; if (im_socket_send(client, msg, 4) != 4) return 7;
    char buf[8] = {0}; if (im_socket_recv(accepted, buf, sizeof(buf)) != 4) return 8;
    if (memcmp(buf, msg, 4) != 0) return 9;
    im_socket_close(client); im_socket_close(accepted);
    im_socket_close(listener); im_socket_shutdown(); puts("socket probe: ok"); return 0;
}
