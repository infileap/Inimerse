#include "mod.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../platform/platform.h"

static char *json_string(const char *s, const char *key) { char pat[64]; snprintf(pat,sizeof pat,"\"%s\"",key); const char *p=strstr(s,pat); if(!p) return NULL; p=strchr(p,':'); if(!p) return NULL; while(*++p==' '||*p=='\t'){} if(*p!='\"') return NULL; p++; const char *e=strchr(p,'\"'); if(!e) return NULL; size_t n=(size_t)(e-p); char *o=malloc(n+1); memcpy(o,p,n); o[n]=0; return o; }

/* POSIX module loader baseline. Native Windows DLL loading is disabled until
 * the PAL dynamic-library backend lands; script modules remain loadable via
 * the dedicated platform implementation. */
static void load_one(VM *vm, const char *dir, const char *name) { char base[2048], p[2048]; if(im_platform_path_join(base,sizeof base,dir,name)!=0 || im_platform_path_join(p,sizeof p,base,"mod.st")!=0) return; FILE *f=fopen(p,"rb"); if(!f) return; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); char *j=malloc((size_t)n+1); if(!j){fclose(f);return;} fread(j,1,(size_t)n,f); fclose(f); j[n]=0; char *script=json_string(j,"script"); free(j); if(script && im_platform_path_join(p,sizeof p,base,script)==0) vm_exec_script_file(vm,p); free(script); }
void mod_load_all(VM *vm, const char *mod_dir) { DIR *d=opendir(mod_dir); if(!d) return; struct dirent *e; while((e=readdir(d))) { if(e->d_name[0]=='.' || e->d_type == DT_REG) continue; load_one(vm,mod_dir,e->d_name); } closedir(d); }
void mod_load_by_name(VM *vm, const char *mod_dir, const char *name) { if(mod_dir&&name) load_one(vm,mod_dir,name); }
