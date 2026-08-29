#include "socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int verse_http_start(int port);
void verse_http_stop(void);

static int query(int port, const char *request, const char *expect) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) continue;
        int sent = im_socket_send(client, request, strlen(request));
        char response[512] = {0}; int n = sent > 0 ? im_socket_recv(client, response, sizeof response - 1) : -1;
        im_socket_close(client); if (n > 0 && strstr(response, expect) != NULL) return 1;
    }
    return 0;
}
static int request_body(int port, const char *request, char *out, size_t cap) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) continue;
        int sent = im_socket_send(client, request, strlen(request));
        int n = sent > 0 ? im_socket_recv(client, out, cap - 1) : -1;
        im_socket_close(client);
        if (n > 0) { out[n] = 0; return n; }
    }
    return -1;
}
int main(void) {
    const int port = 18123;
    setenv("CRP_TOKEN_TTL", "1", 1);
    if (!verse_http_start(port)) return 2;
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, request, "\"ok\":true")) { verse_http_stop(); return 3; }
    const char *find = "GET /find?q=x HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, find, "\"items\":[")) { verse_http_stop(); return 4; }
    char portal[512] = {0};
    const char *portal_req = "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if (request_body(port, portal_req, portal, sizeof portal) < 0 || !strstr(portal, "\"token\":\"posix-")) { verse_http_stop(); return 5; }
    char *tp = strstr(portal, "\"token\":\""); tp += 10; char token[64] = {0};
    char *te = strchr(tp, '"'); if (!te || (size_t)(te - tp) >= sizeof token) { verse_http_stop(); return 6; }
    memcpy(token, tp, (size_t)(te - tp));
    sleep(2);
    char signal[256]; snprintf(signal, sizeof signal, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"token\":\"%s\"}", strlen(token) + 12, token);
    if (request_body(port, signal, portal, sizeof portal) < 0 || !strstr(portal, "403 Forbidden")) { verse_http_stop(); return 7; }
    verse_http_stop();
    puts("http probe: ok"); return 0;
}
