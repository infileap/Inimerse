#include "vm.h"
#include "../platform/process.h"
#include "../platform/platform.h"
#include "../platform/socket.h"
#include "../platform/dir.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

static char g_rooms[1024], g_projects[1024];
static ImProcess *g_room_proc[32];
static void paths_init(void) {
    if (g_rooms[0]) return;
    snprintf(g_rooms, sizeof g_rooms, "%s", getenv("INIMERSE_ROOMS_DIR") ? getenv("INIMERSE_ROOMS_DIR") : "./rooms");
    snprintf(g_projects, sizeof g_projects, "%s", getenv("INIMERSE_PROJECTS_DIR") ? getenv("INIMERSE_PROJECTS_DIR") : "./projects");
    /* Environment overrides may point to nested directories; create the
     * complete path through the portable PAL rather than a single mkdir. */
    (void)im_platform_mkdirs(g_rooms);
}
static int valid_name(const char *s) { return s && *s && strcmp(s, ".") && strcmp(s, "..") && !strstr(s, "..") && !strchr(s, '/') && !strchr(s, '\\') && !strchr(s, ':'); }
static void room_path(int room, char *out, size_t cap) { paths_init(); snprintf(out, cap, "%s/%d.txt", g_rooms, room); }
static int room_slot(int room) { return room >= 11510 && room < 11700 && ((room - 11510) % 10) == 0 ? (room - 11510) / 10 : -1; }
static int local_port_open(int port) {
    if (port < 1 || port > 65535) return 0;
    return im_socket_port_open("127.0.0.1", (uint16_t)port, 250);
}
static char *room_field(int room, const char *key) {
    char path[1200], content[2048]; room_path(room, path, sizeof path); if (im_platform_read_file(path, content, sizeof content) < 0) return NULL;
    char line[512]; size_t n = strlen(key); char *cursor = content; while (cursor && *cursor) { char *next = strchr(cursor, '\n'); size_t len = next ? (size_t)(next - cursor) : strlen(cursor); if (len >= sizeof line) len = sizeof line - 1; memcpy(line, cursor, len); line[len] = 0; if (len > n && !strncmp(line, key, n) && line[n] == '=') return strdup(line + n + 1); cursor = next ? next + 1 : NULL; } return NULL;
}

static int server_ports(VM *vm) {
    vm_cur_set_sp(vm, vm_cur_sp(vm) - vm->cur_argc); char buf[4096] = ""; size_t used = 0;
    for (int room = 11510; room < 11700; room += 10) {
        int slot = room_slot(room); int alive = slot >= 0 && g_room_proc[slot] && im_process_alive(g_room_proc[slot]);
        used += (size_t)snprintf(buf + used, sizeof buf - used, "%d|%s|%llu\n", room, alive ? "LISTENING" : "DOWN", alive && g_room_proc[slot] ? (unsigned long long)im_process_pid(g_room_proc[slot]) : 0ULL);
        if (used >= sizeof buf - 80) break;
    }
    push_string(vm, buf); return 1;
}
static int port_pid(VM *vm) {
    int port = vm_cur_sp(vm) >= 0 ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0; vm_cur_set_sp(vm, vm_cur_sp(vm) - vm->cur_argc);
    int room = (port % 10 == 0) ? port : port - 1; int slot = room_slot(room);
    push_int(vm, slot >= 0 && g_room_proc[slot] && im_process_alive(g_room_proc[slot]) ? (int)im_process_pid(g_room_proc[slot]) : 0); return 1;
}
static int port_kill(VM *vm) {
    int port = vm_cur_sp(vm) >= 0 ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0; vm_cur_set_sp(vm, vm_cur_sp(vm) - vm->cur_argc);
    int room = (port % 10 == 0) ? port : port - 1; int slot = room_slot(room); int ok = 0;
    if (slot >= 0 && g_room_proc[slot]) { ok = im_process_kill(g_room_proc[slot]) == 0; im_process_close(g_room_proc[slot]); g_room_proc[slot] = NULL; }
    push_int(vm, ok); return 1;
}
static int server_port_check(VM *vm) {
    int port = vm_cur_sp(vm) >= 0 ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0;
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    if (port < 1 || port > 65535) { push_int(vm, 0); return 1; }
    push_int(vm, im_socket_port_available(NULL, (uint16_t)port));
    return 1;
}
static int server_lan_ip(VM *vm) {
    int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    char out[64] = ""; (void)im_platform_lan_ip(out, sizeof out); push_string(vm, out); return 1;
}

static void server_lan_ip_text(char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    (void)im_platform_lan_ip(out, cap);
}

static int server_start(VM *vm) {
    int argc = vm->cur_argc; int sp = vm_cur_sp(vm);
    Value pass_v = argc > 0 ? vm_cur_stack(vm)[sp] : (Value){0};
    Value project_v = argc > 1 ? vm_cur_stack(vm)[sp - 1] : (Value){0};
    const char *pass = pass_v.type == VAL_STRING ? pass_v.sval : "";
    const char *project = project_v.type == VAL_STRING ? project_v.sval : "";
    vm_cur_set_sp(vm, sp - argc);
    if (!valid_name(project)) { push_int(vm, 0); return 1; }
    paths_init(); char mainfile[1200]; snprintf(mainfile, sizeof mainfile, "%s/%s/main.im", g_projects, project);
    char probe[2]; if (im_platform_read_file(mainfile, probe, sizeof probe) < 0) { push_int(vm, -1); return 1; }
    int room = 11510; for (; room < 11700; room += 10) { char p[1200], data[2]; room_path(room, p, sizeof p); if (im_platform_read_file(p, data, sizeof data) < 0) break; }
    if (room >= 11700) { push_int(vm, 0); return 1; }
    char exe[1024] = "inimerse"; if (im_platform_executable_path(exe, sizeof exe) < 0) snprintf(exe, sizeof exe, "inimerse");
    char cmd[3200]; snprintf(cmd, sizeof cmd, "\"%s\" --headless --port %d --http-port %d --time-limit 0 \"%s\"", exe, room, room + 10, mainfile);
    int slot = room_slot(room); ImProcess *proc = im_process_spawn(cmd, 0);
    if (!proc) { push_int(vm, 0); return 1; }
    char path[1200], metadata[1800]; room_path(room, path, sizeof path); int mlen = snprintf(metadata, sizeof metadata, "project=%s\npass=%s\nengine_pid=%llu\n", project, pass ? pass : "", (unsigned long long)im_process_pid(proc));
    if (mlen < 0 || im_platform_write_file(path, metadata, (size_t)mlen) != 0) { im_process_kill(proc); im_process_close(proc); return (push_int(vm, 0), 1); }
    if (slot >= 0) g_room_proc[slot] = proc;
    push_int(vm, room); return 1;
}
static int server_join(VM *vm) {
    int argc = vm->cur_argc; int sp = vm_cur_sp(vm);
    Value pass_v = argc > 0 ? vm_cur_stack(vm)[sp] : (Value){0};
    Value room_v = argc > 1 ? vm_cur_stack(vm)[sp - 1] : (Value){0};
    const char *pass = pass_v.type == VAL_STRING ? pass_v.sval : "";
    int room = room_v.type == VAL_FLOAT ? (int)room_v.fval : room_v.ival;
    vm_cur_set_sp(vm, sp - argc);
    int slot = room_slot(room);
    if (slot < 0 || !g_room_proc[slot] || !im_process_alive(g_room_proc[slot]) || !local_port_open(room + 1)) {
        push_string(vm, "ROOM_NOT_FOUND"); return 1;
    }
    char *saved = room_field(room, "pass"); if (!saved) { push_string(vm, "ROOM_NOT_FOUND"); return 1; }
    if (strcmp(saved, pass ? pass : "")) { free(saved); push_string(vm, "WRONG_PASSWORD"); return 1; } free(saved);
    char ip[64] = ""; server_lan_ip_text(ip, sizeof ip); if (!ip[0]) snprintf(ip, sizeof ip, "127.0.0.1");
    char url[128]; snprintf(url, sizeof url, "http://%s:%d/", ip, room + 1); push_string(vm, url); return 1;
}
static int server_status(VM *vm) {
    int sp = vm_cur_sp(vm); Value room_v = vm->cur_argc > 0 ? vm_cur_stack(vm)[sp] : (Value){0};
    int room = room_v.type == VAL_FLOAT ? (int)room_v.fval : room_v.ival; vm_cur_set_sp(vm, sp - vm->cur_argc);
    char *project = room_field(room, "project"); if (!project) { push_string(vm, "ROOM_NOT_FOUND"); return 1; }
    int slot = room_slot(room); int alive = slot >= 0 && g_room_proc[slot] ? im_process_alive(g_room_proc[slot]) : 0;
    char ip[64] = ""; server_lan_ip_text(ip, sizeof ip); if (!ip[0]) snprintf(ip, sizeof ip, "127.0.0.1");
    char buf[512]; snprintf(buf, sizeof buf, "%d|%s|%d|%d|http://%s:%d/", room, project, alive, alive && local_port_open(room + 1), ip, room + 1); free(project); push_string(vm, buf); return 1;
}
static int server_stop(VM *vm) {
    int sp = vm_cur_sp(vm); Value room_v = vm->cur_argc > 0 ? vm_cur_stack(vm)[sp] : (Value){0};
    int room = room_v.type == VAL_FLOAT ? (int)room_v.fval : room_v.ival; vm_cur_set_sp(vm, sp - vm->cur_argc);
    int slot = room_slot(room); if (slot >= 0 && g_room_proc[slot]) { im_process_kill(g_room_proc[slot]); im_process_close(g_room_proc[slot]); g_room_proc[slot] = NULL; }
    char path[1200]; room_path(room, path, sizeof path); push_int(vm, remove(path) == 0); return 1;
}
static int server_rooms(VM *vm) {
    vm_cur_set_sp(vm, vm_cur_sp(vm) - vm->cur_argc); paths_init(); ImDir *d = im_dir_open(g_rooms); char buf[2048] = ""; size_t used = 0; char entry[256]; int isdir = 0;
    if (d) {
        while (im_dir_next_ex(d, entry, sizeof entry, &isdir) > 0 && used < sizeof buf - 64) {
            int room = 0;
            if (isdir || sscanf(entry, "%d.txt", &room) != 1 || room <= 0) continue;
            char *p = room_field(room, "project");
            int slot = room_slot(room);
            int alive = slot >= 0 && g_room_proc[slot] && im_process_alive(g_room_proc[slot]);
            used += (size_t)snprintf(buf + used, sizeof buf - used, "%d:%s:%d\n", room, p ? p : "?", alive);
            free(p);
        }
        im_dir_close(d);
    }
    push_string(vm, buf); return 1;
}

void server_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "server_ports", server_ports, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_check", server_port_check, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_pid", port_pid, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "port_kill", port_kill, 1 | CAP_NET | CAP_PROC, 0);
    vm_register_builtin_full(vm, "lan_ip", server_lan_ip, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_start", server_start, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_join", server_join, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_status", server_status, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_stop", server_stop, 1 | CAP_NET, 0);
    vm_register_builtin_full(vm, "server_rooms", server_rooms, 1 | CAP_NET, 0);
}
