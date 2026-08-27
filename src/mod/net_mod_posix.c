#include "vm.h"
#include "../platform/socket.h"
#include <stdlib.h>
#include <string.h>

#define NET_MAX 256
static ImSocket *g_net[NET_MAX];
static Value arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *strarg(VM *vm, int i) { Value v = arg(vm, i); return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int numarg(VM *vm, int i) { Value v = arg(vm, i); return v.type == VAL_FLOAT ? (int)v.fval : v.ival; }
static void popn(VM *vm) { vm_cur_set_sp(vm, vm_cur_sp(vm) - vm->cur_argc); }
static int put(ImSocket *s) { for (int i = 0; i < NET_MAX; ++i) if (!g_net[i]) { g_net[i] = s; return i + 1; } im_socket_close(s); return -1; }
static ImSocket *get(VM *vm, int i) { int id = numarg(vm, i); return id > 0 && id <= NET_MAX ? g_net[id - 1] : NULL; }

static int net_connect(VM *vm) { const char *host = strarg(vm, 1); int port = numarg(vm, 0); popn(vm); ImSocket *s = im_socket_connect(host, (uint16_t)port); if (!s) { push_int(vm, -1); return 1; } im_socket_set_nonblocking(s, 1); push_int(vm, put(s)); return 1; }
static int net_listen(VM *vm) { int port = numarg(vm, 0); popn(vm); ImSocket *s = im_socket_listen(NULL, (uint16_t)port, 16); if (!s) { push_int(vm, -1); return 1; } im_socket_set_nonblocking(s, 1); push_int(vm, put(s)); return 1; }
static int net_accept(VM *vm) { ImSocket *s = get(vm, 0); popn(vm); ImSocket *c = s ? im_socket_accept(s) : NULL; if (!c) { push_int(vm, -1); return 1; } im_socket_set_nonblocking(c, 1); push_int(vm, put(c)); return 1; }
static int net_send(VM *vm) { ImSocket *s = get(vm, 1); const char *data = strarg(vm, 0); popn(vm); if (!s) { push_int(vm, -1); return 1; } int n = im_socket_send(s, data, strlen(data)); if (n < 0 && im_socket_would_block()) n = 0; push_int(vm, n); return 1; }
static int net_recv(VM *vm) {
    int argc = vm->cur_argc;
    ImSocket *s = argc > 1 ? get(vm, 1) : get(vm, 0);
    int cap = argc > 1 ? numarg(vm, 0) : 1024;
    if (cap < 1) cap = 1024;
    if (cap > 65536) cap = 65536;
    popn(vm);
    char *buf = calloc(1, (size_t)cap + 1);
    if (!buf) { push_string(vm, ""); return 1; }
    int n = s ? im_socket_recv(s, buf, (size_t)cap) : -1;
    if (n <= 0) buf[0] = 0; else buf[n] = 0;
    push_string(vm, buf); free(buf); return 1;
}
static int net_status(VM *vm) { ImSocket *s = get(vm, 0); popn(vm); if (!s) { push_int(vm, 0); return 1; } int n = im_socket_peek(s); if (n > 0) { push_int(vm, 1); return 1; } push_int(vm, n < 0 && im_socket_would_block()); return 1; }
static int net_close(VM *vm) { int id = numarg(vm, 0); popn(vm); if (id <= 0 || id > NET_MAX || !g_net[id - 1]) { push_int(vm, 0); return 1; } im_socket_close(g_net[id - 1]); g_net[id - 1] = NULL; push_int(vm, 1); return 1; }

void net_mod_register(VM *vm) {
    im_socket_init();
    vm_register_builtin_full(vm, "net_connect", net_connect, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_listen", net_listen, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_accept", net_accept, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_send", net_send, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_recv", net_recv, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_status", net_status, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "net_close", net_close, 1 | CAP_NET, 0);
}
