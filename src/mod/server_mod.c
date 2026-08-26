/* server_mod.c - server control module for inimerse
   Bottom-layer control: ports, processes, LAN ip.
   Server room mgmt: create a server from a project (autosave supported via
   the script calling autosave(N)), join with room number + password.
   Builtins:
     server_ports()            -> "port|STATE|pid\n" for all fixed ports
     port_check(port)          -> 1/0 listening
     port_pid(port)            -> pid owning the port (0 = none)
     port_kill(port)           -> kill process owning the port (1/0)
     lan_ip()                  -> LAN IPv4 (empty if none)
     server_start(project, pass) -> room number (0 fail, -1 project missing)
     server_join(room, pass)   -> game url string, or ROOM_NOT_FOUND/WRONG_PASSWORD
     server_status(room)       -> "room|project|engine|bridge|url"
     server_stop(room)         -> 1/0
     server_rooms()            -> "room:project\n" list
*/
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include "vm.h"
#include "platform/platform.h"
#include "platform/dir.h"
#include "platform/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paths are resolved at runtime so a copied/installed build does not depend
 * on the developer's drive letter.  Environment variables are useful for
 * portable deployments and tests; otherwise resources live beside the exe. */
static char g_rooms_dir[1024];
static char g_projects_dir[1024];
static char g_engine_exe[1024];
static char g_bridge_exe[1024];
static int g_paths_ready;

static void sv_init_paths(void) {
    if (g_paths_ready) return;

    char exe[1024] = {0};
    if (im_platform_executable_path(exe, sizeof exe) < 0 || !exe[0])
        snprintf(exe, sizeof exe, ".\\inimerse.exe");

    char root[1024];
    snprintf(root, sizeof root, "%s", exe);
    char *slash = strrchr(root, '\\');
    if (!slash) slash = strrchr(root, '/');
    if (slash) *slash = 0;
    else snprintf(root, sizeof root, ".");

    const char *rooms = getenv("INIMERSE_ROOMS_DIR");
    const char *projects = getenv("INIMERSE_PROJECTS_DIR");
    const char *engine = getenv("INIMERSE_ENGINE");
    const char *bridge = getenv("INIMERSE_BRIDGE");
    snprintf(g_rooms_dir, sizeof g_rooms_dir, "%s", rooms && *rooms ? rooms : "");
    snprintf(g_projects_dir, sizeof g_projects_dir, "%s", projects && *projects ? projects : "");
    snprintf(g_engine_exe, sizeof g_engine_exe, "%s", engine && *engine ? engine : exe);
    if (bridge && *bridge) snprintf(g_bridge_exe, sizeof g_bridge_exe, "%s", bridge);
    else snprintf(g_bridge_exe, sizeof g_bridge_exe, "%s\\hl_bridge.exe", root);
    if (!g_rooms_dir[0]) snprintf(g_rooms_dir, sizeof g_rooms_dir, "%s\\rooms", root);
    if (!g_projects_dir[0]) snprintf(g_projects_dir, sizeof g_projects_dir, "%s\\projects", root);
    g_paths_ready = 1;
}

static const char *sv_rooms_dir(void) { sv_init_paths(); return g_rooms_dir; }
static const char *sv_projects_dir(void) { sv_init_paths(); return g_projects_dir; }
static const char *sv_engine_exe(void) { sv_init_paths(); return g_engine_exe; }
static const char *sv_bridge_exe(void) { sv_init_paths(); return g_bridge_exe; }

static int sv_valid_project_name(const char *name) {
    if (!name || !*name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    /* Project is used in a path; reject traversal and nested components. */
    if (strstr(name, "..") || strchr(name, '\\') || strchr(name, '/') || strchr(name, ':')) return 0;
    return 1;
}

extern DWORD child_proc_spawn(const char *cmdline, const char *tag, int new_console);
extern int child_proc_kill(DWORD pid);

/* ---------- arg helpers (current thread stack) ---------- */
static Value sv_arg(VM *vm, int i) {
    int sp = vm_cur_sp(vm);
    Value z; z.type = VAL_NIL; z.ival = 0; z.fval = 0; z.sval = NULL;
    if (sp - i < 0) return z;
    return vm_cur_stack(vm)[sp - i];
}
static const char *sv_arg_str(VM *vm, int i) { Value v = sv_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static double sv_arg_num(VM *vm, int i) { Value v = sv_arg(vm, i); if (v.type == VAL_INT) return (double)v.ival; if (v.type == VAL_FLOAT) return v.fval; return 0.0; }
static void sv_popn(VM *vm, int n) { vm_cur_set_sp(vm, vm_cur_sp(vm) - n); }

/* ---------- low level ---------- */
static int sv_port_listening(int port) {
    if (port <= 0 || port > 65535) return 0;
    if (im_socket_init() != 0) return 0;
    ImSocket *s = im_socket_connect("127.0.0.1", (uint16_t)port);
    if (s) im_socket_close(s);
    im_socket_shutdown();
    return s != NULL;
}

static int sv_port_pid(int port) {
    ULONG size = 0;
    /* first call may return NO_ERROR or INSUFFICIENT_BUFFER; size is authoritative */
    GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (size == 0) return 0;
    MIB_TCPTABLE_OWNER_PID *t = (MIB_TCPTABLE_OWNER_PID*)malloc(size);
    if (!t) return 0;
    int pid = 0;
    if (GetExtendedTcpTable(t, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        for (DWORD i = 0; i < t->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID *r = &t->table[i];
            if (r->dwState == MIB_TCP_STATE_LISTEN && (int)ntohs((u_short)r->dwLocalPort) == port) { pid = (int)r->dwOwningPid; break; }
        }
    }
    free(t);
    return pid;
}

static void sv_kill_pid(DWORD pid) {
    if (!pid) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) { TerminateProcess(h, 1); CloseHandle(h); }
}

static void sv_lan_ip(char *out, int cap) {
    out[0] = 0;
    char host[256];
    if (gethostname(host, sizeof host) != 0) return;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        struct sockaddr_in *sa = (struct sockaddr_in*)ai->ai_addr;
        const unsigned char *b = (const unsigned char*)&sa->sin_addr;
        int ok = (b[0] == 192 && b[1] == 168) || b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] < 32);
        if (ok) { snprintf(out, (size_t)cap, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]); break; }
    }
    freeaddrinfo(res);
}

/* ---------- room files ---------- */
static char *sv_room_file(int room) {
    static char rf[512];
    char name[32];
    snprintf(name, sizeof name, "%d.txt", room);
    if (im_platform_path_join(rf, sizeof rf, sv_rooms_dir(), name) < 0) rf[0] = 0;
    return rf;
}

static char *sv_room_field(int room, const char *key) {
    FILE *f = fopen(sv_room_file(room), "rb");
    if (!f) return NULL;
    char line[256]; char *val = NULL;
    size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            line[strcspn(line, "\r\n")] = 0;
            val = strdup(line + klen + 1);
            break;
        }
    }
    fclose(f);
    return val;
}

/* ---------- builtins ---------- */
static int builtin_server_ports(VM *vm) {
    sv_popn(vm, vm->cur_argc);
    static const int ports[] = { 11460, 11461, 11470, 11490, 11491, 11500 };
    char buf[2048]; int bp = 0;
    for (int i = 0; i < 6 && bp < (int)sizeof buf - 32; i++) {
        int p = ports[i];
        bp += snprintf(buf + bp, sizeof buf - bp, "%d|%s|%d\n", p, sv_port_listening(p) ? "LISTENING" : "DOWN", sv_port_pid(p));
    }
    push_string(vm, buf);
    return 1;
}

static int builtin_port_check(VM *vm) {
    int port = (int)sv_arg_num(vm, 0);
    fprintf(stderr, "[srvdbg] port_check arg=%d cur_argc=%d sp=%d\n", port, vm->cur_argc, vm_cur_sp(vm));
    sv_popn(vm, vm->cur_argc);
    int r = sv_port_listening(port);
    fprintf(stderr, "[srvdbg] port_check result=%d sp_after=%d\n", r, vm_cur_sp(vm));
    push_int(vm, r);
    fprintf(stderr, "[srvdbg] pushed, sp=%d\n", vm_cur_sp(vm));
    return 1;
}

static int builtin_port_pid(VM *vm) {
    int port = (int)sv_arg_num(vm, 0);
    sv_popn(vm, vm->cur_argc);
    push_int(vm, sv_port_pid(port));
    return 1;
}

static int builtin_port_kill(VM *vm) {
    int port = (int)sv_arg_num(vm, 0);
    sv_popn(vm, vm->cur_argc);
    int pid = sv_port_pid(port);
    if (pid) { sv_kill_pid((DWORD)pid); push_int(vm, 1); }
    else push_int(vm, 0);
    return 1;
}

static int builtin_lan_ip(VM *vm) {
    sv_popn(vm, vm->cur_argc);
    char ip[64];
    sv_lan_ip(ip, sizeof ip);
    push_string(vm, ip);
    return 1;
}

static int builtin_server_start(VM *vm) {
    /* args pushed LIFO: stack = [project, pass]; index 0 = top = pass, 1 = project */
    const char *pass = sv_arg_str(vm, 0);
    const char *project = sv_arg_str(vm, 1);
    sv_popn(vm, vm->cur_argc);
    if (!sv_valid_project_name(project)) { push_int(vm, 0); return 1; }
    /* find free room port (engine R, bridge R+1, api R+10) */
    int port = 0;
    for (int p = 11510; p < 11700; p += 10) {
        if (!sv_port_listening(p) && !sv_port_listening(p + 1) && !sv_port_listening(p + 10)) { port = p; break; }
    }
    if (!port) { push_int(vm, 0); return 1; }
    /* validate project */
    char proj[512];
    snprintf(proj, sizeof proj, "%s\\%s\\main.im", sv_projects_dir(), project);
    FILE *chk = fopen(proj, "rb");
    if (!chk) { push_int(vm, -1); return 1; }
    fclose(chk);
    /* spawn engine */
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "\"%s\" --headless --port %d --http-port %d --time-limit 0 \"%s\\%s\\main.im\"",
             sv_engine_exe(), port, port + 10, sv_projects_dir(), project);
    DWORD p1 = child_proc_spawn(cmd, "room-engine", 0);
    if (!p1) { push_int(vm, 0); return 1; }
    /* spawn bridge */
    snprintf(cmd, sizeof cmd, "\"%s\" %d %d", sv_bridge_exe(), port, port + 1);
    child_proc_spawn(cmd, "room-bridge", 0);
    /* room file */
    im_platform_mkdirs(sv_rooms_dir());
    FILE *f = fopen(sv_room_file(port), "wb");
    if (f) {
        fprintf(f, "project=%s\npass=%s\nengine_pid=%lu\n", project, pass, (unsigned long)p1);
        fclose(f);
    }
    push_int(vm, port);
    return 1;
}

static int builtin_server_join(VM *vm) {
    /* stack = [room, pass]; index 0 = top = pass, 1 = room */
    const char *pass = sv_arg_str(vm, 0);
    int room = (int)sv_arg_num(vm, 1);
    sv_popn(vm, vm->cur_argc);
    if (room <= 0 || !sv_port_listening(room)) { push_string(vm, "ROOM_NOT_FOUND"); return 1; }
    char *saved = sv_room_field(room, "pass");
    if (!saved) { push_string(vm, "ROOM_NOT_FOUND"); return 1; }
    if (strcmp(saved, pass) != 0) { free(saved); push_string(vm, "WRONG_PASSWORD"); return 1; }
    free(saved);
    char ip[64]; sv_lan_ip(ip, sizeof ip);
    if (!ip[0]) strcpy(ip, "127.0.0.1");
    char url[256];
    snprintf(url, sizeof url, "http://%s:%d/", ip, room + 1);
    push_string(vm, url);
    return 1;
}

static int builtin_server_status(VM *vm) {
    int room = (int)sv_arg_num(vm, 0);
    sv_popn(vm, vm->cur_argc);
    char *project = sv_room_field(room, "project");
    char buf[512];
    if (!project) { push_string(vm, "ROOM_NOT_FOUND"); return 1; }
    char ip[64]; sv_lan_ip(ip, sizeof ip);
    if (!ip[0]) strcpy(ip, "127.0.0.1");
    snprintf(buf, sizeof buf, "%d|%s|%d|%d|http://%s:%d/", room, project,
             sv_port_listening(room), sv_port_listening(room + 1), ip, room + 1);
    free(project);
    push_string(vm, buf);
    return 1;
}

static int builtin_server_stop(VM *vm) {
    int room = (int)sv_arg_num(vm, 0);
    sv_popn(vm, vm->cur_argc);
    char *pid_s = sv_room_field(room, "engine_pid");
    if (pid_s) {
        sv_kill_pid((DWORD)atoi(pid_s));
        free(pid_s);
    }
    /* also kill anything on room ports */
    sv_kill_pid((DWORD)sv_port_pid(room));
    sv_kill_pid((DWORD)sv_port_pid(room + 1));
    /* ISO C remove() keeps room persistence independent of Win32 naming. */
    remove(sv_room_file(room));
    push_int(vm, 1);
    return 1;
}

static int builtin_server_rooms(VM *vm) {
    sv_popn(vm, vm->cur_argc);
    char buf[2048]; int bp = 0;
    ImDir *dir = im_dir_open(sv_rooms_dir());
    char name[256];
    if (dir) {
        while (bp < (int)sizeof buf - 64 && im_dir_next(dir, name, sizeof name)) {
            size_t nlen = strlen(name);
            if (nlen < 5 || strcmp(name + nlen - 4, ".txt") != 0) continue;
            name[nlen - 4] = 0;
            int room = atoi(name);
            if (room > 0) {
                char *project = sv_room_field(room, "project");
                bp += snprintf(buf + bp, sizeof buf - bp, "%d:%s:%d\n", room, project ? project : "?", sv_port_listening(room));
                if (project) free(project);
            }
        }
        im_dir_close(dir);
    }
    buf[bp] = 0;
    push_string(vm, buf);
    return 1;
}

void server_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "server_ports", builtin_server_ports, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "port_check", builtin_port_check, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "port_pid", builtin_port_pid, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "port_kill", builtin_port_kill, 1|CAP_NET, 0);
    vm_register_builtin(vm, "lan_ip", builtin_lan_ip);
    vm_register_builtin_full(vm, "server_start", builtin_server_start, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "server_join", builtin_server_join, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "server_status", builtin_server_status, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "server_stop", builtin_server_stop, 1|CAP_NET, 0);
    vm_register_builtin_full(vm, "server_rooms", builtin_server_rooms, 1|CAP_NET, 0);
}

#else
#include "vm.h"
void server_mod_register(VM *vm) { (void)vm; }
#endif
