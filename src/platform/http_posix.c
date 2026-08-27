#include "socket.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

static ImSocket *g_http_listener;
static pthread_t g_http_thread;
static volatile int g_http_running;

static void *http_loop(void *unused) {
    (void)unused;
    while (g_http_running) {
        ImSocket *client = im_socket_accept(g_http_listener);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        (void)im_socket_set_nonblocking(client, 0);
        char req[2048]; int n = im_socket_recv(client, req, sizeof(req) - 1);
        if (n < 0) { im_socket_close(client); continue; }
        req[n > 0 ? n : 0] = 0;
        int ok = n > 0 && strstr(req, "GET /health") != NULL;
        const char *body = ok ? "{\"ok\":true,\"service\":\"inimerse\"}\n" : "{\"error\":\"not_found\"}\n";
        char out[512]; int len = snprintf(out, sizeof out, "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", ok ? "200 OK" : "404 Not Found", strlen(body), body);
        for (int sent = 0; len > 0 && sent < len;) {
            int nout = im_socket_send(client, out + sent, (size_t)(len - sent));
            if (nout <= 0) break;
            sent += nout;
        }
        im_socket_close(client);
    }
    return NULL;
}

int verse_http_start(int port) {
    if (g_http_running || port < 1 || port > 65535 || im_socket_init() != 0) return g_http_running ? 1 : 0;
    g_http_listener = im_socket_listen("127.0.0.1", (uint16_t)port, 16);
    if (!g_http_listener || im_socket_set_nonblocking(g_http_listener, 1) != 0) { if (g_http_listener) im_socket_close(g_http_listener); g_http_listener = NULL; im_socket_shutdown(); return 0; }
    g_http_running = 1;
    if (pthread_create(&g_http_thread, NULL, http_loop, NULL) != 0) { g_http_running = 0; im_socket_close(g_http_listener); g_http_listener = NULL; im_socket_shutdown(); return 0; }
    return 1;
}

void verse_http_stop(void) {
    if (!g_http_running) return;
    g_http_running = 0;
    pthread_join(g_http_thread, NULL);
    if (g_http_listener) { im_socket_close(g_http_listener); g_http_listener = NULL; }
    im_socket_shutdown();
}
