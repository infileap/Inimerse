#include "http_client.h"
#include "socket.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static int parse_url(const char *url, char *host, size_t hc, unsigned short *port, char *path, size_t pc) {
    if (!url || strncmp(url, "http://", 7) != 0) return -1;
    const char *p = url + 7, *slash = strchr(p, '/'); size_t n = slash ? (size_t)(slash - p) : strlen(p);
    if (!n || n >= hc) return -1;
    const char *colon = memchr(p, ':', n); size_t hn = colon ? (size_t)(colon - p) : n;
    if (!hn || hn >= hc) return -1;
    memcpy(host, p, hn); host[hn] = 0;
    *port = colon ? (unsigned short)strtoul(colon + 1, NULL, 10) : 80; if (!*port) return -1;
    if (!slash) { if (pc < 2) return -1; path[0] = '/'; path[1] = 0; return 0; }
    size_t pl = strlen(slash); if (pl + 1 > pc) return -1; memcpy(path, slash, pl + 1); return 0;
}
int im_http_request(const char *method, const char *url, const char *body, char *response, size_t capacity, int *status) {
    if (!response || capacity < 1 || !method) return -1;
    response[0] = 0; if (status) *status = 0;
    char host[256], path[2048]; unsigned short port;
    if (parse_url(url, host, sizeof host, &port, path, sizeof path) != 0) return -1;
    if (im_socket_init() != 0) return -1;
    ImSocket *s = im_socket_connect_timeout(host, port, 5000);
    if (!s) { im_socket_shutdown(); return -1; }
    size_t blen = body ? strlen(body) : 0; char req[8192];
    int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: application/json\r\n\r\n%s", method, path, host, blen, body ? body : "");
    if (n < 0 || (size_t)n >= sizeof req) { im_socket_close(s); im_socket_shutdown(); return -1; }
    for (int sent = 0; sent < n;) { int k = im_socket_send(s, req + sent, (size_t)(n - sent)); if (k <= 0) { im_socket_close(s); im_socket_shutdown(); return -1; } sent += k; }
    char raw[65536]; size_t used = 0; int k; while (used + 1 < sizeof raw && (k = im_socket_recv(s, raw + used, sizeof raw - used - 1)) > 0) used += (size_t)k; raw[used] = 0; im_socket_close(s); im_socket_shutdown();
    char *line_end = strstr(raw, "\r\n"); if (!line_end) return -1; if (status) sscanf(raw, "HTTP/%*s %d", status);
    char *content = strstr(raw, "\r\n\r\n"); if (!content) return -1; content += 4; size_t avail = used - (size_t)(content - raw); if (avail >= capacity) avail = capacity - 1; memcpy(response, content, avail); response[avail] = 0; return 0;
}
