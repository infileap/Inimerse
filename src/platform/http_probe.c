#include "socket.h"
#include <stdio.h>
#include <string.h>

int verse_http_start(int port);
void verse_http_stop(void);

int main(void) {
    const int port = 18123;
    if (!verse_http_start(port)) return 2;
    ImSocket *client = NULL;
    for (int i = 0; i < 20 && !client; ++i) {
        client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 100);
    }
    if (!client) { verse_http_stop(); return 3; }
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (im_socket_send(client, request, strlen(request)) <= 0) { im_socket_close(client); verse_http_stop(); return 4; }
    char response[512] = {0}; int n = im_socket_recv(client, response, sizeof response - 1);
    im_socket_close(client); verse_http_stop();
    if (n <= 0 || !strstr(response, "200 OK") || !strstr(response, "\"ok\":true")) return 5;
    puts("http probe: ok"); return 0;
}
