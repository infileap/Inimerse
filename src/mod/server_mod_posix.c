#include "vm.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int unsupported(VM *vm) {
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    push_string(vm, "UNSUPPORTED: server_mod backend is not available on this POSIX build");
    return 1;
}
static int unavailable_bool(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); push_int(vm, 0); return 1; }
static int server_port_check(VM *vm) {
    int port = vm_cur_sp(vm) >= 0 ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0;
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    if (port < 1 || port > 65535) { push_int(vm, 0); return 1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) { push_int(vm, 0); return 1; }
    int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_ANY); sa.sin_port = htons((uint16_t)port);
    int ok = bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0; close(fd); push_int(vm, ok); return 1;
}
static int server_lan_ip(VM *vm) {
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    struct ifaddrs *list = NULL; if (getifaddrs(&list) != 0) { push_string(vm, ""); return 1; }
    char out[INET_ADDRSTRLEN] = "";
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)it->ifa_addr;
        if (inet_ntop(AF_INET, &sa->sin_addr, out, sizeof out)) break;
    }
    freeifaddrs(list); push_string(vm, out); return 1;
}

void server_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "server_ports", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_check", server_port_check, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_pid", unavailable_bool, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_kill", unavailable_bool, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "lan_ip", server_lan_ip, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_start", unsupported, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "server_join", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_status", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_stop", unavailable_bool, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "server_rooms", unsupported, 1 | CAP_NET, 0);
}
