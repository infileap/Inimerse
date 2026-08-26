/* headless_server.c - headless mode TCP server for browser rendering
   Frame streaming: each frame is a JSON line pushed to connected clients.
   Input: client sends JSON lines {key:..} / {mx,my,btn}.
   Uses winsock, single-threaded accept + per-client buffering (simple). */

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

static SOCKET g_hl_sock = INVALID_SOCKET;
#define HL_MAX_CLIENTS 8
static SOCKET g_hl_clients[HL_MAX_CLIENTS];
static char g_hl_inputs[HL_MAX_CLIENTS][16384];
static int g_hl_input_lens[HL_MAX_CLIENTS];
static int g_hl_count = 0;
static int g_hl_last_ci = -1;   /* client index of last polled line (debug) */
static int g_hl_enabled = 0;
static int g_hl_port = 11440;
   /* input line buffer from client */

static CRITICAL_SECTION g_hl_lock;

int headless_init(int port) {
    g_hl_port = port;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    g_hl_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_hl_sock == INVALID_SOCKET) return 0;
    int reuse = 1;
    setsockopt(g_hl_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof reuse); /* allow rebind over TIME_WAIT */
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY); /* 0.0.0.0: LAN/mobile access */
    sa.sin_port = htons((unsigned short)g_hl_port);
    if (bind(g_hl_sock, (struct sockaddr*)&sa, sizeof sa) != 0) { closesocket(g_hl_sock); g_hl_sock = INVALID_SOCKET; return 0; }
    if (listen(g_hl_sock, 4) != 0) { closesocket(g_hl_sock); g_hl_sock = INVALID_SOCKET; return 0; }
    g_hl_enabled = 1;
    InitializeCriticalSection(&g_hl_lock);
    return 1;
}

void headless_shutdown(void) {
    g_hl_enabled = 0;
    for (int i = 0; i < g_hl_count; i++) closesocket(g_hl_clients[i]);
    g_hl_count = 0;
    if (g_hl_sock != INVALID_SOCKET) { closesocket(g_hl_sock); g_hl_sock = INVALID_SOCKET; }
}

int headless_enabled(void) { return g_hl_enabled; }

/* accept a client if none connected yet (non-blocking-ish with select) */
void headless_accept(void) {
    if (g_hl_count >= HL_MAX_CLIENTS) return;
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(g_hl_sock, &rd);
    struct timeval tv = {0, 0};
    if (select(0, &rd, NULL, NULL, &tv) > 0 && FD_ISSET(g_hl_sock, &rd)) {
        SOCKET cs = accept(g_hl_sock, NULL, NULL);
        if (cs != INVALID_SOCKET) {
            u_long nb = 1;
            ioctlsocket(cs, FIONBIO, &nb);
            g_hl_clients[g_hl_count] = cs;
            g_hl_input_lens[g_hl_count] = 0;
            g_hl_count++;
        }
        }
}

/* push a frame (JSON line) to client. Returns 1 if sent. */
int headless_send_frame(const char *json) {
    if (!g_hl_enabled) return 0;
    size_t n = strlen(json);
    static char fbuf[131072]; /* json + '\n' in ONE send */
    if (n + 1 > sizeof fbuf) n = sizeof fbuf - 1;
    memcpy(fbuf, json, n);
    fbuf[n] = '\n';
    for (int i = 0; i < g_hl_count; i++) {
        int sent = send(g_hl_clients[i], fbuf, (int)n + 1, 0);
        if (sent == SOCKET_ERROR) {
            int werr = WSAGetLastError();
            if (werr == WSAEWOULDBLOCK || werr == WSAEINTR || werr == WSAENOBUFS) continue; /* slow client: drop this frame, keep connection (non-blocking) */
            closesocket(g_hl_clients[i]);
            for (int k = i; k < g_hl_count - 1; k++) g_hl_clients[k] = g_hl_clients[k + 1];
            g_hl_count--;
            i--;
        }
    }
    return 1;
}

/* poll client input. Returns:
   0 = nothing
   1 = got a key line, stored in buf (e.g. "key":"left","down":1)
   2 = got a mouse line (mx,my,btn)
   Parses first input line if available. */
int headless_last_ci(void) { return g_hl_last_ci; }

int headless_poll_input(char *buf, int cap) {
    if (!g_hl_enabled) return 0;
    for (int ci = 0; ci < g_hl_count; ci++) {
        /* 1) serve already-buffered complete lines FIRST (no new socket data needed).
              (fix: lines recv'd in a burst were stuck until the NEXT packet arrived) */
        char *nl = memchr(g_hl_inputs[ci], '\n', (size_t)g_hl_input_lens[ci]);
        if (nl) {
            int linelen = (int)(nl - g_hl_inputs[ci]);
            if (linelen > 0 && linelen < cap) {
                memcpy(buf, g_hl_inputs[ci], (size_t)linelen);
                buf[linelen] = '\0';
            }
            int rest = g_hl_input_lens[ci] - linelen - 1;
            if (rest > 0) memmove(g_hl_inputs[ci], nl + 1, (size_t)rest);
            g_hl_input_lens[ci] = rest;
            if (linelen > 0 && linelen < cap) {
                if (strstr(buf, "\"mouse\"")) { g_hl_last_ci = ci; return 2; }
                if (strstr(buf, "\"key\"")) { g_hl_last_ci = ci; return 1; }
            }
        }
        /* 2) no complete line buffered: check the socket for new data */
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(g_hl_clients[ci], &rd);
        struct timeval tv = {0, 0};
        if (select(0, &rd, NULL, NULL, &tv) > 0 && FD_ISSET(g_hl_clients[ci], &rd)) {
            char tmp[512];
            int n = recv(g_hl_clients[ci], tmp, sizeof tmp, 0);
            if (n <= 0) {
                closesocket(g_hl_clients[ci]);
                for (int k = ci; k < g_hl_count - 1; k++) {
                    g_hl_clients[k] = g_hl_clients[k + 1];
                    g_hl_input_lens[k] = g_hl_input_lens[k + 1];
                    memcpy(g_hl_inputs[k], g_hl_inputs[k + 1], (size_t)g_hl_input_lens[k]);
                }
                g_hl_count--;
                ci--;
                continue;
            }
            if (g_hl_input_lens[ci] + n >= (int)sizeof g_hl_inputs[ci]) {
                /* buffer full: drop the OLDEST complete line(s), keep newest (keyups usually newest) */
                char *oldnl = memchr(g_hl_inputs[ci], '\n', (size_t)g_hl_input_lens[ci]);
                if (oldnl) {
                    int keep = (int)(g_hl_inputs[ci] + g_hl_input_lens[ci] - oldnl - 1);
                    memmove(g_hl_inputs[ci], oldnl + 1, (size_t)keep);
                    g_hl_input_lens[ci] = keep;
                } else {
                    g_hl_input_lens[ci] = 0;
                }
            }
            if (g_hl_input_lens[ci] + n < (int)sizeof g_hl_inputs[ci]) {
                memcpy(g_hl_inputs[ci] + g_hl_input_lens[ci], tmp, (size_t)n);
                g_hl_input_lens[ci] += n;
            }
            /* try extracting a line from the updated buffer */
            char *nl2 = memchr(g_hl_inputs[ci], '\n', (size_t)g_hl_input_lens[ci]);
            if (nl2) {
                int linelen = (int)(nl2 - g_hl_inputs[ci]);
                if (linelen > 0 && linelen < cap) {
                    memcpy(buf, g_hl_inputs[ci], (size_t)linelen);
                    buf[linelen] = '\0';
                }
                int rest = g_hl_input_lens[ci] - linelen - 1;
                if (rest > 0) memmove(g_hl_inputs[ci], nl2 + 1, (size_t)rest);
                g_hl_input_lens[ci] = rest;
                if (linelen > 0 && linelen < cap) {
                    if (strstr(buf, "\"mouse\"")) { g_hl_last_ci = ci; return 2; }
                    if (strstr(buf, "\"key\"")) { g_hl_last_ci = ci; return 1; }
                }
            }
        }
    }
    return 0;
}

/* thread: accept loop (kept simple; accept happens in frame poll too) */
static unsigned __stdcall headless_loop(LPVOID arg) {
    (void)arg;
    while (g_hl_enabled) {
        headless_accept();
        Sleep(50);
    }
    return 0;
}

void headless_start_thread(void) {
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, headless_loop, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
