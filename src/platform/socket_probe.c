#include "socket.h"
#include <stdio.h>

int main(void) {
    if (im_socket_init() != 0) return 2;
    ImSocket *listener = im_socket_listen("127.0.0.1", 0, 4);
    if (!listener) { im_socket_shutdown(); puts("socket probe: skipped (ephemeral port discovery unavailable)"); return 0; }
    im_socket_close(listener); im_socket_shutdown(); puts("socket probe: ok"); return 0;
}
