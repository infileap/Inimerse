#include "socket.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int verse_http_start(int port);
void verse_http_stop(void);

static int query(int port, const char *request, const char *expect) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        int sent = im_socket_send(client, request, strlen(request));
        char response[512] = {0}; int n = sent > 0 ? im_socket_recv(client, response, sizeof response - 1) : -1;
        im_socket_close(client); if (n > 0 && strstr(response, expect) != NULL) return 1;
        struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL);
    }
    return 0;
}
static int request_body(int port, const char *request, char *out, size_t cap) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        ImSocket *client = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        int sent = im_socket_send(client, request, strlen(request));
        int n = sent > 0 ? im_socket_recv(client, out, cap - 1) : -1;
        im_socket_close(client);
        if (n > 0) { out[n] = 0; return n; }
        struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL);
    }
    return -1;
}
int main(void) {
    /* Spread probes across the ephemeral range so parallel CI jobs and quick
       reruns do not collide with a recently-closing listener. */
    const int port = 20000 + (int)(((unsigned long)getpid() * 37UL + (unsigned long)time(NULL)) % 20000UL);
    setenv("INIMERSE_STATE_FILE", "/tmp/inimerse_http_probe_state", 1);
    remove("/tmp/inimerse_http_probe_state");
    setenv("CRP_TOKEN_TTL", "1", 1);
    if (!verse_http_start(port)) return 2;
    char pal_body[512], pal_url[96]; int pal_status = 0; snprintf(pal_url, sizeof pal_url, "http://127.0.0.1:%d/health", port);
    if (im_http_request("GET", pal_url, NULL, pal_body, sizeof pal_body, &pal_status) != 0 || pal_status != 200 || !strstr(pal_body, "\"ok\":true")) { verse_http_stop(); return 8; }
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, request, "\"ok\":true")) { verse_http_stop(); return 3; }
    const char *find = "GET /find?q=x HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (!query(port, find, "\"items\":[")) { verse_http_stop(); return 4; }
    const char *route = "POST /route HTTP/1.1\r\nHost: localhost\r\nContent-Length: 42\r\nConnection: close\r\n\r\n{\"id\":\"peer1\",\"endpoint\":\"127.0.0.1:9000\"}";
    if (!query(port, route, "\"ok\":true")) { verse_http_stop(); return 9; }
    if (!query(port, "GET /route/peer1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", "127.0.0.1:9000")) { verse_http_stop(); return 10; }
    char portal[512] = {0};
    const char *portal_req = "POST /portal HTTP/1.1\r\nHost: localhost\r\nContent-Length: 26\r\nConnection: close\r\n\r\n{\"verse\":\"v1\",\"peer\":\"p1\"}";
    if (request_body(port, portal_req, portal, sizeof portal) < 0 || !strstr(portal, "\"token\":\"posix-")) { verse_http_stop(); return 5; }
    char *tp = strstr(portal, "\"token\":\""); tp += 10; char token[64] = {0};
    char *te = strchr(tp, '"'); if (!te || (size_t)(te - tp) >= sizeof token) { verse_http_stop(); return 6; }
    memcpy(token, tp, (size_t)(te - tp));
    char scoped_signal[256]; snprintf(scoped_signal, sizeof scoped_signal, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"token\":\"%s\",\"verse\":\"v1\",\"peer\":\"wrong\"}", strlen(token) + 40, token);
    if (request_body(port, scoped_signal, portal, sizeof portal) < 0 || !strstr(portal, "403 Forbidden")) { verse_http_stop(); return 11; }
    sleep(2);
    char signal[256]; snprintf(signal, sizeof signal, "POST /signal HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"token\":\"%s\"}", strlen(token) + 12, token);
    if (request_body(port, signal, portal, sizeof portal) < 0 || !strstr(portal, "403 Forbidden")) { verse_http_stop(); return 7; }
    verse_http_stop();
    puts("http probe: ok"); return 0;
}
