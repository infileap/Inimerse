/* ============================================================
 * net_mod.c - TCP network module (non-blocking sockets)
 * Registered by net_mod_register(vm); called from main.c.
 * Build: add -lws2_32
 *
 * API (first arg = arg0, like io_mod):
 *   net_connect(host, port)   -> socket id (>0) or -1 on failure (2s connect timeout)
 *   net_listen(port)          -> listen socket id or -1
 *   net_accept(listen_sock)   -> client socket id or -1 (no pending connection)
 *   net_send(sock, data)      -> bytes sent (0 = buffer full, retry later; -1 = error)
 *   net_recv(sock[, maxlen])  -> string (empty = no data / closed)
 *   net_status(sock)          -> 1 = connected (may have data), 0 = closed/error
 *   net_close(sock)           -> 1
 * ============================================================ */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static int net_started = 0;
static void net_ensure(void) {
    if (!net_started) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        net_started = 1;
    }
}

static Value net_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *net_arg_str(VM *vm, int i) { Value v = net_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static double net_arg_num(VM *vm, int i) { Value v = net_arg(vm, i); if (v.type == VAL_INT) return (double)v.ival; if (v.type == VAL_FLOAT) return v.fval; return 0.0; }
static void net_popn(VM *vm, int n) { vm_cur_set_sp(vm, vm_cur_sp(vm) - n); }

static SOCKET net_sock(VM *vm, int i) { return (SOCKET)(intptr_t)(int)net_arg_num(vm, i); }

/* net_connect(host, port) -> sock or -1; connect with 2s timeout, socket left non-blocking */
static int builtin_net_connect(VM *vm) {
    const char *host = net_arg_str(vm, 1);
    int port = (int)net_arg_num(vm, 0);
    net_popn(vm, vm->cur_argc);
    net_ensure();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { push_int(vm, -1); return 1; }
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host);
        if (!he) { closesocket(s); push_int(vm, -1); return 1; }
        memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }
    int rc = connect(s, (struct sockaddr *)&sa, sizeof(sa));
    if (rc != 0 && WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); push_int(vm, -1); return 1; }
    if (rc != 0) {
        fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
        struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
        rc = select(0, NULL, &wf, NULL, &tv);
        if (rc <= 0) { closesocket(s); push_int(vm, -1); return 1; }
        int err = 0; int elen = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &elen);
        if (err != 0) { closesocket(s); push_int(vm, -1); return 1; }
    }
    push_int(vm, (int)s);
    return 1;
}

/* net_listen(port) -> listen sock or -1 (non-blocking accept) */
static int builtin_net_listen(VM *vm) {
    int port = (int)net_arg_num(vm, 0);
    net_popn(vm, vm->cur_argc);
    net_ensure();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { push_int(vm, -1); return 1; }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((u_short)port);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) { closesocket(s); push_int(vm, -1); return 1; }
    if (listen(s, 16) != 0) { closesocket(s); push_int(vm, -1); return 1; }
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    push_int(vm, (int)s);
    return 1;
}

/* net_accept(listen_sock) -> client sock or -1 (no pending connection) */
static int builtin_net_accept(VM *vm) {
    SOCKET ls = net_sock(vm, 0);
    net_popn(vm, vm->cur_argc);
    SOCKET c = accept(ls, NULL, NULL);
    if (c == INVALID_SOCKET) { push_int(vm, -1); return 1; }
    u_long mode = 1;
    ioctlsocket(c, FIONBIO, &mode);
    push_int(vm, (int)c);
    return 1;
}

/* net_send(sock, data) -> bytes sent (0 = retry later, -1 = error) */
static int builtin_net_send(VM *vm) {
    SOCKET s = net_sock(vm, 1);
    const char *data = net_arg_str(vm, 0);
    net_popn(vm, vm->cur_argc);
    int n = send(s, data, (int)strlen(data), 0);
    if (n == SOCKET_ERROR) n = (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
    push_int(vm, n);
    return 1;
}

/* net_recv(sock[, maxlen]) -> string (empty = no data / closed; use net_status) */
static int builtin_net_recv(VM *vm) {
    SOCKET s = net_sock(vm, 1);
    int maxlen = (int)net_arg_num(vm, 0);
    if (maxlen < 1) maxlen = 1024;
    if (maxlen > 65536) maxlen = 65536;
    net_popn(vm, vm->cur_argc);
    char *buf = malloc(maxlen + 1);
    int n = recv(s, buf, maxlen, 0);
    if (n > 0) buf[n] = '\0';
    else buf[0] = '\0';
    push_string(vm, buf);
    free(buf);
    return 1;
}

/* net_status(sock) -> 1 = connected, 0 = closed/error */
static int builtin_net_status(VM *vm) {
    SOCKET s = net_sock(vm, 0);
    net_popn(vm, vm->cur_argc);
    char b;
    int n = recv(s, &b, 1, MSG_PEEK);
    if (n > 0) { push_int(vm, 1); return 1; }
    if (n == 0) { push_int(vm, 0); return 1; }
    push_int(vm, (WSAGetLastError() == WSAEWOULDBLOCK) ? 1 : 0);
    return 1;
}

/* net_close(sock) -> 1 */
static int builtin_net_close(VM *vm) {
    SOCKET s = net_sock(vm, 0);
    net_popn(vm, vm->cur_argc);
    closesocket(s);
    push_int(vm, 1);
    return 1;
}

/* ============================================================
 * UDP transport (12.x/ROADMAP): background receive thread + queue
 *   udp_bind(port)           -> bound port (0 = fail; 0 input = ephemeral)
 *   udp_send(port, host, data) -> 1/0
 *   udp_recv(port[, timeout_ms]) -> [data, fromhost, fromport] or nil
 *   udp_close(port)          -> 1
 * ============================================================ */
#define UDP_MAX_SOCKS 8
#define UDP_QUEUE 256
#define UDP_MSG 2048
typedef struct {
    SOCKET s;
    int port;
    volatile int run;
    HANDLE th;
    unsigned char qbuf[UDP_QUEUE][UDP_MSG];
    int qlen[UDP_QUEUE];
    char qhost[UDP_QUEUE][64];
    int qport[UDP_QUEUE];
    volatile int qhead, qtail; /* tail = next write, head = next read */
} UdpSock;
static UdpSock g_udp[UDP_MAX_SOCKS];
static int g_udp_count = 0;
static DWORD WINAPI udp_thread(LPVOID arg) {
    UdpSock *u = (UdpSock*)arg;
    while (u->run) {
        struct sockaddr_in from;
        int flen = sizeof(from);
        int n = recvfrom(u->s, (char*)u->qbuf[u->qtail], UDP_MSG, 0,
                         (struct sockaddr*)&from, &flen);
        if (n == SOCKET_ERROR) { Sleep(2); continue; }
        if (n <= 0) continue;
        int nq = (u->qtail + 1) % UDP_QUEUE;
        if (nq == u->qhead) continue; /* full: drop */
        u->qlen[u->qtail] = n;
        inet_ntop(AF_INET, &from.sin_addr, u->qhost[u->qtail], 64);
        u->qport[u->qtail] = ntohs(from.sin_port);
        u->qtail = nq;
    }
    return 0;
}
static UdpSock *udp_find(int port) {
    for (int i = 0; i < g_udp_count; i++) if (g_udp[i].port == port) return &g_udp[i];
    return NULL;
}
static int builtin_udp_bind(VM *vm) {
    int port = (int)net_arg_num(vm, 0);
    net_popn(vm, vm->cur_argc);
    net_ensure();
    if (udp_find(port) && port > 0) { push_int(vm, 0); return 1; }
    if (g_udp_count >= UDP_MAX_SOCKS) { push_int(vm, 0); return 1; }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { push_int(vm, 0); return 1; }
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((u_short)port);
    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) != 0) { closesocket(s); push_int(vm, 0); return 1; }
    int blen = sizeof(sa);
    getsockname(s, (struct sockaddr*)&sa, &blen);
    int actual = ntohs(sa.sin_port);
    UdpSock *u = &g_udp[g_udp_count++];
    memset(u, 0, sizeof(*u));
    u->s = s;
    u->port = actual;
    u->run = 1;
    u->th = CreateThread(NULL, 0, udp_thread, u, 0, NULL);
    push_int(vm, actual);
    return 1;
}
static int builtin_udp_send(VM *vm) {
    int port = (int)net_arg_num(vm, 3);      /* local socket port */
    const char *host = net_arg_str(vm, 2);   /* destination host */
    int dstport = (int)net_arg_num(vm, 1);   /* destination port */
    const char *data = net_arg_str(vm, 0);
    net_popn(vm, vm->cur_argc);
    UdpSock *u = udp_find(port);
    if (!u) { push_int(vm, 0); return 1; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)dstport);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host);
        if (!he) { push_int(vm, 0); return 1; }
        memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }
    int n = sendto(u->s, data, (int)strlen(data), 0, (struct sockaddr*)&sa, sizeof(sa));
    push_int(vm, n == SOCKET_ERROR ? 0 : 1);
    return 1;
}
static int builtin_udp_recv(VM *vm) {
    int port = (int)net_arg_num(vm, 1);
    int timeout = (int)net_arg_num(vm, 0);
    if (timeout < 0) timeout = 0;
    if (timeout > 60000) timeout = 60000;
    net_popn(vm, vm->cur_argc);
    UdpSock *u = udp_find(port);
    if (!u) { push_nil(vm); return 1; }
    ULONGLONG t0 = GetTickCount64();
    while (u->qhead == u->qtail) {
        if (timeout >= 0 && GetTickCount64() - t0 >= (ULONGLONG)timeout) { push_nil(vm); return 1; }
        Sleep(2);
    }
    int idx = u->qhead;
    u->qhead = (u->qhead + 1) % UDP_QUEUE;
    int aidx = vm_array_new(vm);
    Value v;
    v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = _strdup((char*)u->qbuf[idx]);
    vm_array_push(vm, aidx, &v);
    v.sval = _strdup(u->qhost[idx]);
    vm_array_push(vm, aidx, &v);
    v.type = VAL_INT; v.ival = u->qport[idx]; v.fval = 0; v.sval = NULL;
    vm_array_push(vm, aidx, &v);
    Value rv; rv.type = VAL_ARRAY; rv.ival = aidx + 1; rv.fval = 0; rv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) { vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = rv; }
    return 1;
}
static int builtin_udp_close(VM *vm) {
    int port = (int)net_arg_num(vm, 0);
    net_popn(vm, vm->cur_argc);
    for (int i = 0; i < g_udp_count; i++) {
        if (g_udp[i].port == port) {
            g_udp[i].run = 0;
            if (g_udp[i].th) { WaitForSingleObject(g_udp[i].th, 1000); CloseHandle(g_udp[i].th); }
            closesocket(g_udp[i].s);
            for (int j = i; j < g_udp_count - 1; j++) g_udp[j] = g_udp[j + 1];
            g_udp_count--;
            push_int(vm, 1);
            return 1;
        }
    }
    push_int(vm, 0);
    return 1;
}

#ifdef _WIN32
void net_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "net_connect", builtin_net_connect, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_listen", builtin_net_listen, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_accept", builtin_net_accept, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_send", builtin_net_send, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_recv", builtin_net_recv, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_status", builtin_net_status, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "net_close", builtin_net_close, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "udp_bind", builtin_udp_bind, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "udp_send", builtin_udp_send, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "udp_recv", builtin_udp_recv, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "udp_close", builtin_udp_close, 1|CAP_NET, 0);
}

#pragma GCC diagnostic pop

#else
void net_mod_register(VM *vm) { (void)vm; }
#endif
