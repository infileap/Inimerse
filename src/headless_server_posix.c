#ifndef _WIN32
#include "headless_server.h"
#include "platform/socket.h"
#include "platform/thread.h"
#include "platform/sync.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HL_MAX_CLIENTS 8
#define HL_INPUT_CAP 16384
static ImSocket *g_hl_sock;
static ImSocket *g_hl_clients[HL_MAX_CLIENTS];
static char g_hl_inputs[HL_MAX_CLIENTS][HL_INPUT_CAP];
static int g_hl_input_lens[HL_MAX_CLIENTS], g_hl_count, g_hl_last_ci = -1, g_hl_enabled;
static volatile unsigned g_hl_generation;
static ImMutex *g_hl_lock;

static void remove_client(int i) {
    if (i < 0 || i >= g_hl_count) return;
    im_socket_close(g_hl_clients[i]);
    for (int k = i; k + 1 < g_hl_count; ++k) {
        g_hl_clients[k] = g_hl_clients[k + 1];
        g_hl_input_lens[k] = g_hl_input_lens[k + 1];
        memcpy(g_hl_inputs[k], g_hl_inputs[k + 1], (size_t)g_hl_input_lens[k]);
    }
    --g_hl_count;
}

int headless_init(int port) {
    if (port < 1 || port > 65535 || g_hl_enabled) return 0;
    if (im_socket_init() != 0) return 0;
    g_hl_sock = im_socket_listen(NULL, (uint16_t)port, 4);
    if (!g_hl_sock) return 0;
    im_socket_set_nonblocking(g_hl_sock, 1);
    /* The accept loop is detached, so keep the lock alive across stop and
     * restart; freeing it while the loop unwinds would be a use-after-free. */
    if (!g_hl_lock) g_hl_lock = im_mutex_new();
    ++g_hl_generation; g_hl_count = 0; g_hl_last_ci = -1; g_hl_enabled = g_hl_lock != NULL;
    if (!g_hl_enabled) { im_socket_close(g_hl_sock); g_hl_sock = NULL; }
    return g_hl_enabled;
}

void headless_shutdown(void) {
    if (!g_hl_enabled) return;
    im_mutex_lock(g_hl_lock); g_hl_enabled = 0; ++g_hl_generation;
    for (int i = 0; i < g_hl_count; ++i) im_socket_close(g_hl_clients[i]);
    g_hl_count = 0; im_socket_close(g_hl_sock); g_hl_sock = NULL; im_mutex_unlock(g_hl_lock);
}
int headless_enabled(void) { return g_hl_enabled; }

void headless_accept(void) {
    if (!g_hl_enabled || !g_hl_sock) return;
    im_mutex_lock(g_hl_lock);
    while (g_hl_count < HL_MAX_CLIENTS) {
        ImSocket *c = im_socket_accept(g_hl_sock); if (!c) break;
        im_socket_set_nonblocking(c, 1); g_hl_clients[g_hl_count] = c; g_hl_input_lens[g_hl_count++] = 0;
    }
    im_mutex_unlock(g_hl_lock);
}

int headless_send_frame(const char *json) {
    if (!g_hl_enabled || !json) return 0;
    char frame[131072]; size_t n = strlen(json); if (n >= sizeof(frame) - 1) n = sizeof(frame) - 2;
    memcpy(frame, json, n); frame[n++] = '\n';
    im_mutex_lock(g_hl_lock);
    for (int i = 0; i < g_hl_count; ++i) {
        int sent = im_socket_send(g_hl_clients[i], frame, n);
        if (sent < 0 && !im_socket_would_block()) { remove_client(i); --i; }
    }
    im_mutex_unlock(g_hl_lock); return 1;
}

int headless_last_ci(void) { return g_hl_last_ci; }
static int extract_line(int ci, char *buf, int cap) {
    char *nl = memchr(g_hl_inputs[ci], '\n', (size_t)g_hl_input_lens[ci]); if (!nl) return 0;
    int len = (int)(nl - g_hl_inputs[ci]), rest = g_hl_input_lens[ci] - len - 1;
    if (len <= 0 || len >= cap) { if (rest > 0) memmove(g_hl_inputs[ci], nl + 1, (size_t)rest); g_hl_input_lens[ci] = rest; return 0; }
    memcpy(buf, g_hl_inputs[ci], (size_t)len); buf[len] = 0;
    if (rest > 0) memmove(g_hl_inputs[ci], nl + 1, (size_t)rest);
    g_hl_input_lens[ci] = rest;
    if (strstr(buf, "\"mouse\"")) { g_hl_last_ci = ci; return 2; }
    if (strstr(buf, "\"key\"")) { g_hl_last_ci = ci; return 1; }
    return 0;
}
int headless_poll_input(char *buf, int cap) {
    if (!g_hl_enabled || !buf || cap < 2) return 0;
    im_mutex_lock(g_hl_lock);
    for (int i = 0; i < g_hl_count; ++i) {
        int kind = extract_line(i, buf, cap); if (kind) { im_mutex_unlock(g_hl_lock); return kind; }
        char tmp[512]; int n = im_socket_recv(g_hl_clients[i], tmp, sizeof(tmp));
        if (n == 0 || (n < 0 && !im_socket_would_block())) { remove_client(i); --i; continue; }
        if (n > 0) {
            if (g_hl_input_lens[i] + n >= HL_INPUT_CAP) g_hl_input_lens[i] = 0;
            if (g_hl_input_lens[i] + n < HL_INPUT_CAP) { memcpy(g_hl_inputs[i] + g_hl_input_lens[i], tmp, (size_t)n); g_hl_input_lens[i] += n; }
            kind = extract_line(i, buf, cap); if (kind) { im_mutex_unlock(g_hl_lock); return kind; }
        }
    }
    im_mutex_unlock(g_hl_lock); return 0;
}
static void *headless_loop(void *arg) {
    (void)arg; unsigned generation = g_hl_generation;
    while (g_hl_enabled && generation == g_hl_generation) { headless_accept(); im_platform_sleep_ms(50); }
    return NULL;
}
void headless_start_thread(void) { void *h = im_thread_start(headless_loop, NULL); if (h) (void)im_thread_detach(h); }
#endif
