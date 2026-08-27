#include "socket.h"
#include <stdio.h>
#include <string.h>
int verse_http_start(int port); void verse_http_stop(void);
int main(void) {
    const int port = 18124; int rc = 0; ImSocket *s = NULL;
    if (!verse_http_start(port)) return 2;
    s = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500); if (!s) { rc = 3; goto done; }
    const char *h = "GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (im_socket_send(s, h, strlen(h)) <= 0) { rc = 4; goto done; }
    char resp[512] = {0}; int n = im_socket_recv(s, resp, sizeof resp - 1); if (n <= 0 || !strstr(resp, "101 Switching Protocols")) { rc = 5; goto done; }
    unsigned char frame[10] = {0x81, 0x84, 1, 2, 3, 4, 0x71, 0x6b, 0x6d, 0x63};
    if (im_socket_send(s, frame, sizeof frame) != (int)sizeof frame) { rc = 6; goto done; }
    unsigned char echo[32] = {0}; n = im_socket_recv(s, echo, sizeof echo);
    if (n != 6 || echo[0] != 0x81 || echo[1] != 4 || echo[2] != 0x70 || echo[3] != 0x69 || echo[4] != 0x6e || echo[5] != 0x67) { rc = 7; goto done; }
done:
    if (s) im_socket_close(s); verse_http_stop(); if (rc) return rc;
    puts("websocket probe: ok"); return 0;
}
