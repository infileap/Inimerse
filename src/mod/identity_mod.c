/* identity_mod.c - local identity & profile builtins (M1)
 * builtins: me(), profile_set(name, bio, avatar), profile_get()
 * data: userdata/profile.json (relative to the working directory)
 * arg convention (io-style): arg index counts from stack top (0 = last arg)
 */
#include "vm.h"
#include "platform/platform.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "crypt32.lib")

#ifndef USERDATA_DIR
#define USERDATA_DIR "userdata"
#endif

static Value id_arg(VM *vm, int i) {
  int sp = vm_cur_sp(vm);
  Value z; z.type = VAL_NIL; z.ival = 0; z.fval = 0; z.sval = NULL;
  if (sp - i < 0) return z;
  return vm_cur_stack(vm)[sp - i];
}
static const char *id_arg_str(VM *vm, int i) {
  Value v = id_arg(vm, i);
  return (v.type == VAL_STRING && v.sval) ? v.sval : "";
}
static void id_popn(VM *vm, int n) {
  vm_cur_set_sp(vm, vm_cur_sp(vm) - n);
}

static const char *id_read_file(const char *path, char *buf, int cap) {
  buf[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return buf;
  size_t n = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}

static void id_write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}

static void id_json_escape(const char *in, char *out, int cap) {
  int n = 0; if (!in) in = "";
  while (*in && n < cap - 1) { unsigned char c = (unsigned char)*in++;
    if (c == '\\' || c == '"') { if (n + 2 >= cap) break; out[n++]='\\'; out[n++]=(char)c; }
    else if (c == '\n' || c == '\r' || c == '\t') { if (n + 2 >= cap) break; out[n++]='\\'; out[n++]=(c=='\n'?'n':(c=='\r'?'r':'t')); }
    else if (c < 0x20) { out[n++]=' '; } else out[n++] = (char)c;
  } out[n] = 0;
}

static int id_protect_token(const char *provider, const char *token) {
  DATA_BLOB in = {(DWORD)strlen(token), (BYTE*)token}, out = {0};
  if (!CryptProtectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return 0;
  char path[512]; snprintf(path, sizeof path, USERDATA_DIR "\\oauth_%s.bin", provider);
  FILE *f = fopen(path, "wb"); if (!f) { LocalFree(out.pbData); return 0; }
  fwrite(out.pbData, 1, out.cbData, f); fclose(f); LocalFree(out.pbData); return 1;
}

/* ensure profile.json exists; generate id if missing */
static int id_ensure_profile(void) {
  im_platform_mkdirs(USERDATA_DIR);
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\profile.json");
  FILE *f = fopen(path, "rb");
  if (f) { fclose(f); return 1; }
  srand((unsigned)(time(NULL) ^ GetTickCount()));
  char id[48], name[64], buf[512];
  snprintf(id, sizeof id, "u_%08x%08x", (unsigned)rand(), (unsigned)rand());
  snprintf(name, sizeof name, "player%d", (rand() % 9000) + 1000);
  snprintf(buf, sizeof buf,
           "{\"id\":\"%s\",\"name\":\"%s\",\"avatar\":\"\",\"bio\":\"\",\"created\":%ld}",
           id, name, (long)time(NULL));
  id_write_file(path, buf);
  return 1;
}

/* extract a numeric field from json (first occurrence) */
static void id_field_num(const char *json, const char *field, char *out, int cap) {
  out[0] = 0;
  char pat[128];
  snprintf(pat, sizeof pat, "\"%s\":", field);
  const char *p = strstr(json, pat);
  if (!p) return;
  p += strlen(pat);
  while (*p == ' ') p++;
  const char *e = p;
  while (*e >= '0' && *e <= '9') e++;
  int n = (int)(e - p);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

/* extract a quoted string field from json (first occurrence) */
static void id_field(const char *json, const char *field, char *out, int cap) {
  out[0] = 0;
  char pat[128];
  snprintf(pat, sizeof pat, "\"%s\":\"", field);
  const char *p = strstr(json, pat);
  if (!p) return;
  p += strlen(pat);
  const char *e = strchr(p, '"');
  if (!e) return;
  int n = (int)(e - p);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

static const char *id_profile_json(void) {
  static char buf[4096];
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\profile.json");
  id_ensure_profile();
  return id_read_file(path, buf, sizeof buf);
}

static int builtin_me(VM *vm) {
  id_popn(vm, vm->cur_argc);
  push_string(vm, id_profile_json());
  return 1;
}

static int builtin_profile_set(VM *vm) {
  const char *name = id_arg_str(vm, 2);   /* 1st arg */
  const char *bio = id_arg_str(vm, 1);    /* 2nd arg */
  const char *avatar = id_arg_str(vm, 0); /* 3rd arg */
  id_popn(vm, vm->cur_argc);
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\profile.json");
  id_ensure_profile();
  char old[4096];
  id_read_file(path, old, sizeof old);
  char id[64], created[64];
  id_field(old, "id", id, sizeof id);
  id_field_num(old, "created", created, sizeof created);
  if (!id[0]) snprintf(id, sizeof id, "u_%08x", (unsigned)rand());
  char buf[4096];
  snprintf(buf, sizeof buf,
           "{\"id\":\"%s\",\"name\":\"%s\",\"avatar\":\"%s\",\"bio\":\"%s\",\"created\":%s}",
           id, name, avatar, bio, created[0] ? created : "0");
  id_write_file(path, buf);
  push_int(vm, 1);
  return 1;
}

/* profile_get(id): local cache only in M1 (no P2P yet) */
static int builtin_profile_get(VM *vm) {
  id_popn(vm, vm->cur_argc);
  /* M1: return my profile; peer profiles come later with p2p */
  push_string(vm, id_profile_json());
  return 1;
}

/* oauth_config(provider, client_id, redirect_uri): stores non-secret app settings. */
static int builtin_oauth_config(VM *vm) {
  const char *provider=id_arg_str(vm,2), *client=id_arg_str(vm,1), *redirect=id_arg_str(vm,0);
  char ec[512], er[512], path[512], buf[1400];
  id_json_escape(client,ec,sizeof ec); id_json_escape(redirect,er,sizeof er);
  im_platform_mkdirs(USERDATA_DIR); snprintf(path,sizeof path,USERDATA_DIR "\\oauth_%s.json",provider);
  snprintf(buf,sizeof buf,"{\"provider\":\"%s\",\"client_id\":\"%s\",\"redirect_uri\":\"%s\"}",provider,ec,er);
  id_write_file(path,buf); id_popn(vm,vm->cur_argc); push_int(vm,1); return 1;
}

/* oauth_authorize(provider,state): returns provider's official authorization URL. */
static int builtin_oauth_authorize(VM *vm) {
  const char *provider=id_arg_str(vm,1), *state=id_arg_str(vm,0); char cfg[2048], cid[256], red[512], url[2048];
  char path[512]; snprintf(path,sizeof path,USERDATA_DIR "\\oauth_%s.json",provider); id_read_file(path,cfg,sizeof cfg);
  id_field(cfg,"client_id",cid,sizeof cid); id_field(cfg,"redirect_uri",red,sizeof red);
  if (!cid[0] || !red[0]) { id_popn(vm,vm->cur_argc); push_string(vm,""); return 1; }
  if (_stricmp(provider,"github")==0)
    snprintf(url,sizeof url,"https://github.com/login/oauth/authorize?client_id=%s&redirect_uri=%s&scope=read:user%%20user:email&state=%s",cid,red,state);
  else if (_stricmp(provider,"bilibili")==0)
    snprintf(url,sizeof url,"https://passport.bilibili.com/login?gourl=%s%%3Fstate%%3D%s",red,state);
  else url[0]=0;
  id_popn(vm,vm->cur_argc); push_string(vm,url); return 1;
}

/* oauth_bind(provider, token, user_id, display_name, avatar): encrypts token and stores public metadata. */
static int builtin_oauth_bind(VM *vm) {
  const char *provider=id_arg_str(vm,4), *token=id_arg_str(vm,3), *uid=id_arg_str(vm,2), *name=id_arg_str(vm,1), *avatar=id_arg_str(vm,0);
  char euid[256], ename[512], eav[1024], path[512], buf[2200];
  int ok=id_protect_token(provider,token); id_json_escape(uid,euid,sizeof euid); id_json_escape(name,ename,sizeof ename); id_json_escape(avatar,eav,sizeof eav);
  im_platform_mkdirs(USERDATA_DIR); snprintf(path,sizeof path,USERDATA_DIR "\\linked_accounts.json");
  snprintf(buf,sizeof buf,"{\"provider\":\"%s\",\"user_id\":\"%s\",\"display_name\":\"%s\",\"avatar\":\"%s\",\"linked_at\":%ld,\"token_protected\":%s}",provider,euid,ename,eav,(long)time(NULL),ok?"true":"false");
  id_write_file(path,buf); id_popn(vm,vm->cur_argc); push_int(vm,ok); return 1;
}

static int builtin_oauth_unbind(VM *vm) {
  const char *provider=id_arg_str(vm,0); char path[512];
  snprintf(path,sizeof path,USERDATA_DIR "\\oauth_%s.bin",provider); remove(path);
  snprintf(path,sizeof path,USERDATA_DIR "\\linked_accounts.json"); remove(path);
  id_popn(vm,vm->cur_argc); push_int(vm,1); return 1;
}

static int builtin_oauth_status(VM *vm) {
  char path[512], buf[4096]; const char *provider = id_arg_str(vm,0);
  snprintf(path,sizeof path,USERDATA_DIR "\\linked_accounts.json"); id_read_file(path,buf,sizeof buf);
  if (provider[0] && !strstr(buf, provider)) buf[0]=0;
  id_popn(vm,vm->cur_argc); push_string(vm,buf); return 1;
}

void identity_mod_register(VM *vm) {
  vm_register_builtin(vm, "me", builtin_me);
  vm_register_builtin_full(vm, "profile_set", builtin_profile_set, 1|CAP_VERSE, 0);
  vm_register_builtin(vm, "profile_get", builtin_profile_get);
  vm_register_builtin_full(vm, "oauth_config", builtin_oauth_config, 1|CAP_VERSE, 0);
  vm_register_builtin(vm, "oauth_authorize", builtin_oauth_authorize);
  vm_register_builtin_full(vm, "oauth_bind", builtin_oauth_bind, 1|CAP_VERSE, 0);
  vm_register_builtin_full(vm, "oauth_unbind", builtin_oauth_unbind, 1|CAP_VERSE, 0);
  vm_register_builtin(vm, "oauth_status", builtin_oauth_status);
}
