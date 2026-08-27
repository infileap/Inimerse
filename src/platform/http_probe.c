#include "socket.h"
#include <stdio.h>
#include <string.h>

int verse_http_start(int port);
void verse_http_stop(void);

static int query(int port, const char *request, const char *expect) {
    ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
    if (!client) return 0;
    int sent = im_socket_send(client, request, strlen(request));
    char response[512] = {0}; int n = sent > 0 ? im_socket_recv(client, response, sizeof response - 1) : -1;
    im_socket_close(client); return n > 0 && strstr(response, expect) != NULL;
}
int main(void) {
    const int port = 18123;
    if (!verse_http_start(port)) return 2;
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, request, "\"ok\":true")) { verse_http_stop(); return 3; }
    const char *find = "GET /find?q=x HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, find, "\"verses\":[]")) { verse_http_stop(); return 4; }
    verse_http_stop();
    puts("http probe: ok"); return 0;
}
