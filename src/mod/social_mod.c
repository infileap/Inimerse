/* social_mod.c - local friends & chat builtins (M1, no P2P yet)
 * builtins: friend_add(name, id), friend_list(), friend_remove(id),
 *           chat_send(peer, text), chat_history(peer)
 * data: userdata\friends.json, userdata\messages\<peer>.json
 * arg convention (io-style): index counts from stack top (0 = last arg)
 */
#include "vm.h"
#include "platform/platform.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef USERDATA_DIR
#define USERDATA_DIR "userdata"
#endif

static Value so_arg(VM *vm, int i) {
  int sp = vm_cur_sp(vm);
  Value z; z.type = VAL_NIL; z.ival = 0; z.fval = 0; z.sval = NULL;
  if (sp - i < 0) return z;
  return vm_cur_stack(vm)[sp - i];
}
static const char *so_arg_str(VM *vm, int i) {
  Value v = so_arg(vm, i);
  return (v.type == VAL_STRING && v.sval) ? v.sval : "";
}
static void so_popn(VM *vm, int n) {
  vm_cur_set_sp(vm, vm_cur_sp(vm) - n);
}

static const char *so_read_file(const char *path, char *buf, int cap) {
  buf[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return buf;
  size_t n = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}

static void so_write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}

static void so_msg_path(const char *peer, char *out, int cap) {
  snprintf(out, cap, USERDATA_DIR "\\messages\\%s.json", peer);
}

/* friends.json: {"friends":[{"id":..,"name":..,"added":..},...]} */
static int builtin_friend_add(VM *vm) {
  const char *name = so_arg_str(vm, 1); /* 1st arg */
  const char *id = so_arg_str(vm, 0);   /* 2nd arg */
  so_popn(vm, vm->cur_argc);
  if (!id[0]) { push_int(vm, 0); return 1; }
  im_platform_mkdirs(USERDATA_DIR);
  im_platform_mkdirs(USERDATA_DIR "\\messages");
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\friends.json");
  char old[8192];
  so_read_file(path, old, sizeof old);
  long now = (long)time(NULL);
  char entry[512];
  snprintf(entry, sizeof entry, "{\"id\":\"%s\",\"name\":\"%s\",\"added\":%ld}",
           id, name[0] ? name : id, now);
  if (strstr(old, "\"id\":\"") == NULL) {
    /* empty or missing: start array */
    char buf[9216];
    snprintf(buf, sizeof buf, "{\"friends\":[%s]}", entry);
    so_write_file(path, buf);
  } else {
    /* append before final ] */
    size_t olen = strlen(old);
    char *close = strrchr(old, ']');
    if (close) {
      char buf[9216];
      size_t head = (size_t)(close - old);
      snprintf(buf, sizeof buf, "%.*s,%s%s", (int)head, old, entry, close);
      so_write_file(path, buf);
    } else {
      char buf[9216];
      snprintf(buf, sizeof buf, "{\"friends\":[%s]}", entry);
      so_write_file(path, buf);
    }
  }
  push_int(vm, 1);
  return 1;
}

static int builtin_friend_list(VM *vm) {
  so_popn(vm, vm->cur_argc);
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\friends.json");
  static char buf[16384];
  so_read_file(path, buf, sizeof buf);
  if (!buf[0]) push_string(vm, "{\"friends\":[]}");
  else push_string(vm, buf);
  return 1;
}

static int builtin_friend_remove(VM *vm) {
  const char *id = so_arg_str(vm, 0);
  so_popn(vm, vm->cur_argc);
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\friends.json");
  char old[8192];
  so_read_file(path, old, sizeof old);
  /* remove entries matching "id":"<id>" */
  char pat[128];
  snprintf(pat, sizeof pat, "{\"id\":\"%s\"", id);
  char out[8192];
  int oi = 0, n = (int)strlen(old);
  /* scan entries: split by }{ boundaries is hard; simple approach:
     rebuild by walking and dropping the matching object */
  /* naive: find each occurrence and remove the object incl. comma */
  const char *p = old;
  while (p && *p) {
    const char *m = strstr(p, pat);
    if (!m) {
      /* copy tail */
      while (*p) out[oi++] = *p++;
      break;
    }
    int head = (int)(m - p);
    memcpy(out + oi, p, head); oi += head;
    /* find end of object: next '}' */
    const char *e = strchr(m, '}');
    if (!e) { while (*p) out[oi++] = *p++; break; }
    p = e + 1;
    /* drop a trailing comma after removed object */
    if (*p == ',') p++;
    /* also drop comma before removed object: check out tail */
    if (oi > 0 && out[oi - 1] == ',') {
      /* keep separator simple: leave as is */
    }
  }
  out[oi] = 0;
  /* cleanup ",," -> "," and ",]" -> "]" */
  char tmp[8192];
  snprintf(tmp, sizeof tmp, "%s", out);
  memset(out, 0, sizeof out);
  int ti = 0;
  for (int i = 0; tmp[i]; i++) {
    if (tmp[i] == ',' && (tmp[i + 1] == ',' || tmp[i + 1] == ']')) continue;
    out[ti++] = tmp[i];
  }
  out[ti] = 0;
  so_write_file(path, out);
  push_int(vm, 1);
  return 1;
}

/* chat_send(peer, text) */
static int builtin_chat_send(VM *vm) {
  const char *peer = so_arg_str(vm, 1);
  const char *text = so_arg_str(vm, 0);
  so_popn(vm, vm->cur_argc);
  if (!peer[0] || !text[0]) { push_int(vm, 0); return 1; }
  im_platform_mkdirs(USERDATA_DIR "\\messages");
  char path[512];
  so_msg_path(peer, path, sizeof path);
  char old[65536];
  so_read_file(path, old, sizeof old);
  long now = (long)time(NULL);
  char entry[1024];
  snprintf(entry, sizeof entry, "{\"from\":\"me\",\"text\":\"%s\",\"ts\":%ld}",
           text, now);
  char buf[70000];
  if (strstr(old, "\"msgs\"") == NULL) {
    snprintf(buf, sizeof buf, "{\"peer\":\"%s\",\"msgs\":[%s]}", peer, entry);
  } else {
    char *close = strrchr(old, ']');
    if (close) {
      size_t head = (size_t)(close - old);
      snprintf(buf, sizeof buf, "%.*s,%s%s", (int)head, old, entry, close);
    } else {
      snprintf(buf, sizeof buf, "{\"peer\":\"%s\",\"msgs\":[%s]}", peer, entry);
    }
  }
  so_write_file(path, buf);
  push_int(vm, 1);
  return 1;
}

static int builtin_chat_history(VM *vm) {
  const char *peer = so_arg_str(vm, 0);
  so_popn(vm, vm->cur_argc);
  char path[512];
  so_msg_path(peer, path, sizeof path);
  static char buf[65536];
  so_read_file(path, buf, sizeof buf);
  if (!buf[0]) {
    snprintf(buf, sizeof buf, "{\"peer\":\"%s\",\"msgs\":[]}", peer);
  }
  push_string(vm, buf);
  return 1;
}

void social_mod_register(VM *vm) {
  vm_register_builtin_full(vm, "friend_add", builtin_friend_add, 1|CAP_NET, 0);
  vm_register_builtin_full(vm, "friend_list", builtin_friend_list, 1|CAP_NET, 0);
  vm_register_builtin_full(vm, "friend_remove", builtin_friend_remove, 1|CAP_NET, 0);
  vm_register_builtin_full(vm, "chat_send", builtin_chat_send, 1|CAP_NET, 0);
  vm_register_builtin_full(vm, "chat_history", builtin_chat_history, 1|CAP_NET, 0);
}
