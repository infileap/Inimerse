#include "vm.h"
#include <string.h>

static int unsupported(VM *vm) {
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    push_string(vm, "UNSUPPORTED: server_mod backend is not available on this POSIX build");
    return 1;
}
static int unavailable_bool(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); push_int(vm, 0); return 1; }

void server_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "server_ports", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_check", unavailable_bool, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_pid", unavailable_bool, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_kill", unavailable_bool, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "lan_ip", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_start", unsupported, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "server_join", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_status", unsupported, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_stop", unavailable_bool, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "server_rooms", unsupported, 1 | CAP_NET, 0);
}
