#include "runtime.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../platform/platform.h"

static int posix_random(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); push_int(vm, n > 0 ? rand() % n : 0); return 1; }
static int posix_sqrt(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); push_float(vm, sqrt(v.type == VAL_INT ? (double)v.ival : v.fval)); return 1; }
static int posix_time_ms(VM *vm) { push_int(vm, (int)(im_platform_now_ms() & 0x7fffffff)); return 1; }
static int posix_sleep(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int ms=vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); if(ms>0) im_platform_sleep_ms((unsigned)ms); push_int(vm, 1); return 1; }
static int posix_read_file(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; const char *p=v.sval?v.sval:""; FILE *f=fopen(p,"rb"); pop(vm); if(!f){push_string(vm,"");return 1;} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); char *b=(char*)malloc((size_t)n+1); if(!b){fclose(f);push_string(vm,"");return 1;} fread(b,1,(size_t)n,f); fclose(f); b[n]=0; push_string(vm,b); free(b); return 1; }
static int posix_write_file(VM *vm) { if(vm_cur_sp(vm)<1)return 0; Value data=vm_cur_stack(vm)[vm_cur_sp(vm)], path=vm_cur_stack(vm)[vm_cur_sp(vm)-1]; FILE *f=fopen(path.sval?path.sval:"","wb"); int ok=0; if(f){fputs(data.sval?data.sval:"",f); fclose(f); ok=1;} vm_cur_set_sp(vm,vm_cur_sp(vm)-2); push_int(vm,ok); return 1; }
static int posix_input(VM *vm) { if(vm_cur_sp(vm)>=0) pop(vm); char b[1024]; if(fgets(b,sizeof b,stdin)){size_t n=strlen(b); if(n&&b[n-1]=='\n')b[n-1]=0; push_string(vm,b);} else push_string(vm,""); return 1; }
static int posix_env(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; char b[4096]; int r=im_platform_getenv(v.sval?v.sval:"",b,sizeof b); pop(vm); push_string(vm,r==0?b:""); return 1; }

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
}

void record_load_from_file(VM *vm, const char *path) { (void)vm; (void)path; }
void record_save_to_file(VM *vm, const char *path) { (void)vm; (void)path; }
