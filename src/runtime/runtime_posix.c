#include "runtime.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdio.h>
#include "../platform/platform.h"
#include "../platform/dir.h"

static int posix_random(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); push_int(vm, n > 0 ? rand() % n : 0); return 1; }
static int posix_sqrt(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); push_float(vm, sqrt(v.type == VAL_INT ? (double)v.ival : v.fval)); return 1; }
static int posix_time_ms(VM *vm) { push_int(vm, (int)(im_platform_now_ms() & 0x7fffffff)); return 1; }
static int posix_sleep(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int ms=vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); if(ms>0) im_platform_sleep_ms((unsigned)ms); push_int(vm, 1); return 1; }
static int posix_read_file(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; const char *p=v.sval?v.sval:""; FILE *f=fopen(p,"rb"); pop(vm); if(!f){push_string(vm,"");return 1;} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); char *b=(char*)malloc((size_t)n+1); if(!b){fclose(f);push_string(vm,"");return 1;} fread(b,1,(size_t)n,f); fclose(f); b[n]=0; push_string(vm,b); free(b); return 1; }
static int posix_write_file(VM *vm) { if(vm_cur_sp(vm)<1)return 0; Value data=vm_cur_stack(vm)[vm_cur_sp(vm)], path=vm_cur_stack(vm)[vm_cur_sp(vm)-1]; FILE *f=fopen(path.sval?path.sval:"","wb"); int ok=0; if(f){fputs(data.sval?data.sval:"",f); fclose(f); ok=1;} vm_cur_set_sp(vm,vm_cur_sp(vm)-2); push_int(vm,ok); return 1; }
static int posix_input(VM *vm) { if(vm_cur_sp(vm)>=0) pop(vm); char b[1024]; if(fgets(b,sizeof b,stdin)){size_t n=strlen(b); if(n&&b[n-1]=='\n')b[n-1]=0; push_string(vm,b);} else push_string(vm,""); return 1; }
static int posix_env(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; char b[4096]; int r=im_platform_getenv(v.sval?v.sval:"",b,sizeof b); pop(vm); push_string(vm,r==0?b:""); return 1; }
static int posix_capability(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok=im_platform_has_capability(v.sval?v.sval:""); pop(vm); push_bool(vm,ok); return 1; }
static int posix_mkdir(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok=im_platform_mkdirs(v.sval?v.sval:"")==0; pop(vm); push_bool(vm,ok); return 1; }
static int posix_list_dir(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; ImDir *d=im_dir_open(v.sval?v.sval:""); pop(vm); if(!d){push_string(vm,"");return 1;} char n[512], all[4096]; all[0]=0; int first=1, isdir=0; while(im_dir_next_ex(d,n,sizeof n,&isdir)>0){ if(!first) strncat(all,"\n",sizeof all-strlen(all)-1); strncat(all,n,sizeof all-strlen(all)-1); first=0; } im_dir_close(d); push_string(vm,all); return 1; }

static int posix_shell_quote(char *dst, size_t cap, const char *src) {
    size_t n = 0; if (!dst || cap < 3) return -1; dst[n++] = '\'';
    for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p; ++p) {
        if (*p == '\'') { if (n + 4 >= cap) return -1; dst[n++]='\''; dst[n++]='\\'; dst[n++]='\''; dst[n++]='\''; }
        else { if (n + 1 >= cap) return -1; dst[n++] = (char)*p; }
    }
    if (n + 2 > cap) return -1;
    dst[n++] = '\'';
    dst[n] = 0;
    return 0;
}
static int posix_http_req(VM *vm, int post) {
    int need = post ? 1 : 0; if (vm_cur_sp(vm) < need) return 0;
    Value uv = vm_cur_stack(vm)[vm_cur_sp(vm) - need];
    Value dv = post ? vm_cur_stack(vm)[vm_cur_sp(vm)] : uv;
    char qu[4096], qd[65536], cmd[72000];
    if (posix_shell_quote(qu, sizeof qu, uv.sval ? uv.sval : "") != 0 ||
        (post && posix_shell_quote(qd, sizeof qd, dv.sval ? dv.sval : "") != 0)) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) - (post ? 2 : 1)); push_string(vm, ""); return 1;
    }
    if (post) snprintf(cmd, sizeof cmd, "curl -fsSL --max-time 15 -X POST --data %s %s", qd, qu);
    else snprintf(cmd, sizeof cmd, "curl -fsSL --max-time 15 %s", qu);
    FILE *f = popen(cmd, "r"); char out[65536] = {0}; size_t total = 0;
    if (f) { while (total < sizeof(out) - 1) { size_t n = fread(out + total, 1, sizeof(out) - 1 - total, f); total += n; if (!n) break; } pclose(f); }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - (post ? 2 : 1)); push_string(vm, out); return 1;
}
static int posix_http_get(VM *vm) { return posix_http_req(vm, 0); }
static int posix_http_post(VM *vm) { return posix_http_req(vm, 1); }
static int posix_exec(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *cmd = v.type == VAL_STRING && v.sval ? v.sval : "";
    FILE *f = popen(cmd, "r");
    char out[65536] = {0}; size_t total = 0;
    if (f) {
        while (total < sizeof(out) - 1) {
            size_t n = fread(out + total, 1, sizeof(out) - 1 - total, f);
            total += n; if (!n) break;
        }
        pclose(f);
    }
    pop(vm); push_string(vm, out); return 1;
}
/* Hardware/UI operations keep the same names on POSIX.  Until a host grants
 * the corresponding PAL capability they fail deterministically instead of
 * becoming unknown builtins (which makes portable scripts diagnosable). */
static int posix_unsupported(VM *vm) {
    int n = vm_cur_sp(vm) + 1; if (n > 0) vm_cur_set_sp(vm, -1); push_int(vm, -1); return 1;
}
static speed_t posix_baud(int baud) {
    switch (baud) { case 1200:return B1200; case 2400:return B2400; case 4800:return B4800; case 19200:return B19200; case 38400:return B38400; case 57600:return B57600; case 115200:return B115200; default:return B9600; }
}
static int posix_serial_open(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value bv = vm_cur_stack(vm)[vm_cur_sp(vm)], pv = vm_cur_stack(vm)[vm_cur_sp(vm)-1];
    const char *path = pv.sval ? pv.sval : ""; int baud = bv.type == VAL_INT ? bv.ival : 9600;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    if (fd < 0) { push_int(vm, -1); return 1; }
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { close(fd); push_int(vm, -1); return 1; }
    cfmakeraw(&tio); speed_t sp = posix_baud(baud); cfsetispeed(&tio, sp); cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD); tio.c_cc[VMIN] = 0; tio.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); push_int(vm, -1); return 1; }
    push_int(vm, fd); return 1;
}
static int posix_serial_write(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)-1], dv=vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = (hv.type == VAL_INT && dv.type == VAL_STRING && dv.sval) ? (int)write(hv.ival, dv.sval, strlen(dv.sval)) : -1;
    vm_cur_set_sp(vm, vm_cur_sp(vm)-2); push_int(vm, n < 0 ? -1 : n); return 1;
}
static int posix_serial_read(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)-1], nv=vm_cur_stack(vm)[vm_cur_sp(vm)];
    int cap = nv.type == VAL_INT ? nv.ival : 256; if (cap < 1) cap = 1; if (cap > 65536) cap = 65536;
    char *buf = (char*)malloc((size_t)cap + 1); ssize_t n = (hv.type == VAL_INT) ? read(hv.ival, buf, (size_t)cap) : -1;
    if (n < 0) n = 0;
    buf[n] = 0; vm_cur_set_sp(vm, vm_cur_sp(vm)-2); push_string(vm, buf); free(buf); return 1;
}
static int posix_serial_close(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok = hv.type == VAL_INT && close(hv.ival) == 0; pop(vm); push_int(vm, ok ? 1 : 0); return 1;
}

/* POSIX baseline: platform-specific runtime builtins are intentionally
 * unavailable until their PAL backends are implemented. Core VM and
 * language builtins remain usable; callers receive a clear unsupported
 * capability instead of a link-time/platform failure. */
void runtime_register_builtins(VM *vm) {
    vm_register_builtin(vm, "random", posix_random);
    vm_register_builtin(vm, "sqrt", posix_sqrt);
    vm_register_builtin(vm, "time_ms", posix_time_ms);
    vm_register_builtin(vm, "sleep_ms", posix_sleep);
    vm_register_builtin(vm, "read_file", posix_read_file);
    vm_register_builtin(vm, "write_file", posix_write_file);
    vm_register_builtin(vm, "input", posix_input);
    vm_register_builtin(vm, "env", posix_env);
    vm_register_builtin(vm, "mkdir", posix_mkdir);
    vm_register_builtin(vm, "list_dir", posix_list_dir);
    vm_register_builtin(vm, "http_get", posix_http_get);
    vm_register_builtin(vm, "http_post", posix_http_post);
    vm_register_builtin_full(vm, "exec", posix_exec, 1 | CAP_PROC, 0);
    vm_register_builtin(vm, "serial_open", posix_serial_open);
    vm_register_builtin(vm, "serial_write", posix_serial_write);
    vm_register_builtin(vm, "serial_read", posix_serial_read);
    vm_register_builtin(vm, "serial_close", posix_serial_close);
    vm_register_builtin(vm, "key_press", posix_unsupported);
    vm_register_builtin(vm, "mouse_move", posix_unsupported);
    vm_register_builtin(vm, "mouse_click", posix_unsupported);
    vm_register_builtin(vm, "has_capability", posix_capability);
}

void record_load_from_file(VM *vm, const char *path) { (void)vm; (void)path; }
void record_save_to_file(VM *vm, const char *path) { (void)vm; (void)path; }
