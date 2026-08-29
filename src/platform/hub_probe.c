#include "socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(p,m) _mkdir(p)
#define setenv(k,v,o) _putenv_s(k,v)
#endif
int verse_http_start(int port); void verse_http_stop(void);
static int request(int port, const char *method, const char *path, const char *body, char *out, size_t cap) {
    ImSocket *s = im_socket_connect_timeout("127.0.0.1", (uint16_t)port, 500); if (!s) return 0;
    char req[1024]; size_t len = body ? strlen(body) : 0; int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", method, path, len, body ? body : "");
    if (im_socket_send(s, req, (size_t)n) != n) { im_socket_close(s); return 0; }
    size_t used = 0; while (used + 1 < cap) { int k = im_socket_recv(s, out + used, cap - used - 1); if (k <= 0) break; used += (size_t)k; } out[used] = 0; im_socket_close(s); return used > 0;
}
int main(void) {
    const char *root = "/tmp/inimerse-hub-probe"; mkdir(root, 0755); setenv("INIMERSE_HUB_DIR", root, 1); const int port = 18125; char out[4096];
    if (!verse_http_start(port)) return 2;
    if (!request(port, "POST", "/package", "{\"id\":\"demo\",\"data\":\"cGtn\"}", out, sizeof out) || !strstr(out, "201 Created")) return 3;
    if (!request(port, "GET", "/packages", NULL, out, sizeof out) || !strstr(out, "demo")) return 4;
    if (!request(port, "GET", "/packages?q=demo", NULL, out, sizeof out) || !strstr(out, "demo")) return 41;
    if (!request(port, "GET", "/packages?q=missing", NULL, out, sizeof out) || !strstr(out, "[]")) return 42;
    if (!request(port, "GET", "/package/demo", NULL, out, sizeof out) || !strstr(out, "pkg")) return 5;
    if (!request(port, "POST", "/package/fork", "{\"source\":\"demo\",\"id\":\"fork\"}", out, sizeof out) || !strstr(out, "201 Created")) return 6;
    if (!request(port, "DELETE", "/package/fork", NULL, out, sizeof out) || !strstr(out, "200 OK")) return 7;
    if (!request(port, "POST", "/content", "{\"data\":\"aGVsbG8=\"}", out, sizeof out) || !strstr(out, "201 Created")) return 8;
    verse_http_stop(); puts("hub probe: ok"); return 0;
}
