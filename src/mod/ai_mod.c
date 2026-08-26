/* ai_mod.c - AI friends (M2)
 * builtins: ai_config(endpoint, api_key, model), ai_register(name, persona),
 *           ai_list(), ai_chat(friend_id, text), ai_params(friend_id, temperature, max_tokens, reply_freq)
 * backend: Ollama (http://127.0.0.1:11434) or OpenAI-compatible endpoint
 * data: userdata\ai.json, userdata\ai_config.json
 * arg convention (io-style): index counts from stack top (0 = last arg)
 */
#include "vm.h"
#include "platform/platform.h"
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef USERDATA_DIR
#define USERDATA_DIR "userdata"
#endif

static Value ai_arg(VM *vm, int i) {
  int sp = vm_cur_sp(vm);
  Value z; z.type = VAL_NIL; z.ival = 0; z.fval = 0; z.sval = NULL;
  if (sp - i < 0) return z;
  return vm_cur_stack(vm)[sp - i];
}
static const char *ai_arg_str(VM *vm, int i) {
  Value v = ai_arg(vm, i);
  return (v.type == VAL_STRING && v.sval) ? v.sval : "";
}
static double ai_arg_num(VM *vm, int i) {
  Value v = ai_arg(vm, i);
  if (v.type == VAL_INT) return (double)v.ival;
  if (v.type == VAL_FLOAT) return v.fval;
  return 0.0;
}
static void ai_popn(VM *vm, int n) {
  vm_cur_set_sp(vm, vm_cur_sp(vm) - n);
}

static const char *ai_read_file(const char *path, char *buf, int cap) {
  buf[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return buf;
  size_t n = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}
static void ai_write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}
static void ai_json_escape(const char *in, char *out, int cap) {
  int oi = 0;
  for (const char *p = in; *p && oi < cap - 2; p++) {
    if (*p == '"' || *p == '\\') { if (oi < cap - 2) out[oi++] = '\\'; out[oi++] = *p; }
    else if (*p == '\n') { if (oi < cap - 4) { out[oi++] = '\\'; out[oi++] = 'n'; } }
    else if (*p == '\r') { if (oi < cap - 4) { out[oi++] = '\\'; out[oi++] = 'r'; } }
    else if (*p == '\t') { if (oi < cap - 4) { out[oi++] = '\\'; out[oi++] = 't'; } }
    else out[oi++] = *p;
  }
  out[oi] = 0;
}

/* ---------------- backend config ---------------- */
static void ai_load_config(char *endpoint, int ecap, char *api_key, int kcap, char *model, int mcap) {
  endpoint[0] = 0; api_key[0] = 0;
  snprintf(model, mcap, "Qwen2.5-7B-Instruct:latest");
  char buf[2048], path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai_config.json");
  ai_read_file(path, buf, sizeof buf);
  char *p = strstr(buf, "\"endpoint\":\"");
  if (p) { p += 12; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > ecap - 1) n = ecap - 1; memcpy(endpoint, p, n); endpoint[n] = 0; } }
  p = strstr(buf, "\"api_key\":\"");
  if (p) { p += 11; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > kcap - 1) n = kcap - 1; memcpy(api_key, p, n); api_key[n] = 0; } }
  p = strstr(buf, "\"model\":\"");
  if (p) { p += 9; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > mcap - 1) n = mcap - 1; memcpy(model, p, n); model[n] = 0; } }
}

/* generic JSON POST via WinHTTP (http/https). returns 0 on success */
static int ai_http_post(const char *host, int port, const char *path, int https,
                        const char *body, const char *extra_headers, char *out, int cap) {
  wchar_t whost[256], wpath[512];
  MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
  HINTERNET hI = WinHttpOpen(L"Inimerse-AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
  if (hI) WinHttpSetTimeouts(hI, 30000, 30000, 60000, 180000); /* local 7B CPU inference can take 30-60s+ */
  int rc = -1;
  if (hI) {
    HINTERNET hC = WinHttpConnect(hI, whost, (INTERNET_PORT)port, 0);
    if (hC) {
      DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
      HINTERNET hR = WinHttpOpenRequest(hC, L"POST", wpath, NULL, NULL, NULL, flags);
      if (hR) {
        char hdrs[1024];
        snprintf(hdrs, sizeof hdrs, "Content-Type: application/json\r\n%s", extra_headers ? extra_headers : "");
        LPCWSTR wh = NULL;
        wchar_t whdrs[1024];
        if (hdrs[0]) { MultiByteToWideChar(CP_UTF8, 0, hdrs, -1, whdrs, 1024); wh = whdrs; }
        if (WinHttpSendRequest(hR, wh, wh ? (DWORD)-1L : 0, (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0)) {
          if (WinHttpReceiveResponse(hR, NULL)) {
            DWORD total = 0;
            char tmp[8192];
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
              DWORD rd = 0;
              if (!WinHttpReadData(hR, tmp, avail < 8191 ? avail : 8191, &rd)) break;
              if (total + rd < (DWORD)cap - 1) { memcpy(out + total, tmp, rd); total += rd; }
              else break;
            }
            out[total] = '\0';
            rc = 0;
          }
        }
        WinHttpCloseHandle(hR);
      }
      WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hI);
  }
  return rc;
}

/* split endpoint into host/port/path/https; returns scheme type
 * "ollama" -> local ollama
 * "http://h:p/path" / "https://h:p/path" -> generic */
static void ai_parse_endpoint(const char *ep, char *host, int hcap, int *port, char *path, int pcap, int *https) {
  *https = 0;
  *port = 80;
  if (!ep[0] || strcmp(ep, "ollama") == 0) {
    snprintf(host, hcap, "127.0.0.1");
    *port = 11434;
    snprintf(path, pcap, "/api/chat");
    return;
  }
  const char *p = ep;
  if (strncmp(p, "https://", 8) == 0) { *https = 1; *port = 443; p += 8; }
  else if (strncmp(p, "http://", 7) == 0) { p += 7; }
  snprintf(path, pcap, "/chat/completions");
  const char *slash = strchr(p, '/');
  const char *colon = strchr(p, ':');
  char hp[256];
  if (slash) { int n = (int)(slash - p); if (n > 255) n = 255; memcpy(hp, p, n); hp[n] = 0; }
  else snprintf(hp, sizeof hp, "%s", p);
  colon = strchr(hp, ':');
  if (colon) {
    snprintf(host, hcap, "%.*s", (int)(colon - hp), hp);
    *port = atoi(colon + 1);
    if (slash && slash[0]) { char rest[256]; snprintf(rest, sizeof rest, "%s", slash); snprintf(path, pcap, "%s/chat/completions", rest); }
  } else {
    snprintf(host, hcap, "%s", hp);
  }
}

/* extract first "content":"..." from json response */
static void ai_extract_content(const char *json, char *out, int cap) {
  out[0] = 0;
  const char *p = strstr(json, "\"content\":\"");
  if (!p) { snprintf(out, cap, "%s", json); return; }
  p += 11;
  const char *e = strchr(p, '"');
  if (!e) { snprintf(out, cap, "%s", json); return; }
  int n = (int)(e - p);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

/* core: call AI with persona + params. returns 0 on success */
static int ai_call(const char *model, const char *persona, const char *prompt,
                   double temperature, int max_tokens, char *out, int cap) {
  char ep[256], key[256], m2[128];
  ai_load_config(ep, sizeof ep, key, sizeof key, m2, sizeof m2);
  if (!model || !model[0]) model = m2;
  char sys[12000], usr[12000];
  ai_json_escape(persona ? persona : "", sys, sizeof sys);
  ai_json_escape(prompt ? prompt : "", usr, sizeof usr);
  char body[30000];
  char host[256], path[512];
  int port, https;
  int isOllama = (!ep[0] || strcmp(ep, "ollama") == 0);
  if (isOllama) {
    snprintf(body, sizeof body,
      "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false,\"options\":{\"temperature\":%.2f}}",
      model, sys, usr, temperature);
    ai_parse_endpoint("ollama", host, sizeof host, &port, path, sizeof path, &https);
  } else {
    snprintf(body, sizeof body,
      "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"temperature\":%.2f,\"max_tokens\":%d}",
      model, sys, usr, temperature, max_tokens > 0 ? max_tokens : 256);
    ai_parse_endpoint(ep, host, sizeof host, &port, path, sizeof path, &https);
  }
  char headers[1024] = "";
  if (!isOllama && key[0]) snprintf(headers, sizeof headers, "Authorization: Bearer %s\r\n", key);
  char raw[32768];
  raw[0] = 0;
  int rc = ai_http_post(host, port, path, https, body, headers, raw, sizeof raw);
  if (rc != 0) { snprintf(out, cap, "AI:ERR http (%s)", ep[0] ? ep : "ollama"); return -1; }
  ai_extract_content(raw, out, cap);
  if (!out[0]) { snprintf(out, cap, "AI:EMPTY"); return -1; }
  return 0;
}

/* ---------------- builtins ---------------- */

/* ai_config(endpoint, api_key, model) */
static int builtin_ai_config(VM *vm) {
  const char *endpoint = ai_arg_str(vm, 2);
  const char *api_key = ai_arg_str(vm, 1);
  const char *model = ai_arg_str(vm, 0);
  ai_popn(vm, vm->cur_argc);
  im_platform_mkdirs(USERDATA_DIR);
  char path[512], buf[1024];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai_config.json");
  snprintf(buf, sizeof buf, "{\"endpoint\":\"%s\",\"api_key\":\"%s\",\"model\":\"%s\"}",
           endpoint, api_key, model);
  ai_write_file(path, buf);
  push_int(vm, 1);
  return 1;
}

/* ai_register(name, persona) -> creates AI friend in ai.json + friends.json */
static int builtin_ai_register(VM *vm) {
  const char *name = ai_arg_str(vm, 1);
  const char *persona = ai_arg_str(vm, 0);
  ai_popn(vm, vm->cur_argc);
  im_platform_mkdirs(USERDATA_DIR);
  srand((unsigned)(time(NULL) ^ GetTickCount()));
  char id[64];
  snprintf(id, sizeof id, "ai_%08x", (unsigned)rand());
  /* ai.json */
  char path[512], old[8192], en[1024];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai.json");
  ai_read_file(path, old, sizeof old);
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"persona\":\"%s\",\"model\":\"qwen2.5:7b\",\"temperature\":0.7,\"max_tokens\":256,\"reply_freq\":1.0}",
           id, name, persona);
  char buf[9216];
  if (strstr(old, "\"id\":\"") == NULL) snprintf(buf, sizeof buf, "{\"ais\":[%s]}", en);
  else {
    char *cl = strrchr(old, ']');
    if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, en, cl); }
    else snprintf(buf, sizeof buf, "{\"ais\":[%s]}", en);
  }
  ai_write_file(path, buf);
  /* also add to friends.json (is_ai) */
  snprintf(path, sizeof path, USERDATA_DIR "\\friends.json");
  ai_read_file(path, old, sizeof old);
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"added\":%ld,\"is_ai\":1}", id, name, (long)time(NULL));
  if (strstr(old, "\"id\":\"") == NULL) snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en);
  else {
    char *cl = strrchr(old, ']');
    if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, en, cl); }
    else snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en);
  }
  ai_write_file(path, buf);
  push_int(vm, 1);
  return 1;
}

static int builtin_ai_list(VM *vm) {
  ai_popn(vm, vm->cur_argc);
  char path[512];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai.json");
  static char buf[8192];
  ai_read_file(path, buf, sizeof buf);
  if (!buf[0]) push_string(vm, "{\"ais\":[]}");
  else push_string(vm, buf);
  return 1;
}

/* look up ai friend config by id; returns 1 if found */
static int ai_find(const char *id, char *name, int ncap, char *persona, int pcap,
                   char *model, int mcap, double *temperature, int *max_tokens, double *reply_freq) {
  char path[512], buf[8192];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai.json");
  ai_read_file(path, buf, sizeof buf);
  char pat[128];
  snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char *p = strstr(buf, pat);
  if (!p) return 0;
  char *obj = p;
  while (obj > buf && *obj != '{') obj--;
  char *e = strchr(p, '}');
  if (!e) return 0;
  char tmp[1024];
  int n = (int)(e - obj + 1);
  if (n > 1023) n = 1023;
  memcpy(tmp, obj, n);
  tmp[n] = 0;
  name[0] = persona[0] = model[0] = 0;
  *temperature = 0.7; *max_tokens = 256; *reply_freq = 1.0;
  char *f = strstr(tmp, "\"name\":\"");
  if (f) { f += 8; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > ncap - 1) l = ncap - 1; memcpy(name, f, l); name[l] = 0; } }
  f = strstr(tmp, "\"persona\":\"");
  if (f) { f += 11; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > pcap - 1) l = pcap - 1; memcpy(persona, f, l); persona[l] = 0; } }
  f = strstr(tmp, "\"model\":\"");
  if (f) { f += 9; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > mcap - 1) l = mcap - 1; memcpy(model, f, l); model[l] = 0; } }
  f = strstr(tmp, "\"temperature\":");
  if (f) { f += 14; *temperature = atof(f); }
  f = strstr(tmp, "\"max_tokens\":");
  if (f) { f += 13; *max_tokens = atoi(f); }
  f = strstr(tmp, "\"reply_freq\":");
  if (f) { f += 13; *reply_freq = atof(f); }
  return 1;
}

/* ai_chat(friend_id, text) */
static int builtin_ai_chat(VM *vm) {
  const char *id = ai_arg_str(vm, 1);
  const char *text = ai_arg_str(vm, 0);
  ai_popn(vm, vm->cur_argc);
  char name[128], persona[4096], model[128];
  double temperature, reply_freq;
  int max_tokens;
  if (!ai_find(id, name, sizeof name, persona, sizeof persona, model, sizeof model,
               &temperature, &max_tokens, &reply_freq)) {
    push_string(vm, "AI:ERR no such friend");
    return 1;
  }
  char res[32768];
  int rc = ai_call(model, persona, text, temperature, max_tokens, res, sizeof res);
  if (rc != 0) { push_string(vm, res); return 1; }
  push_string(vm, res);
  return 1;
}

/* ai_code_check(code) -> AI code review: points out measured inefficiencies.
   Rules below are derived from real benchmarks (2026-08-16), not folklore. */
static const char AI_CODE_RULES[] =
  "You are a code reviewer for the Inimerse language engine. "
  "Review the given .im source code against these MEASURED rules:\n"
  "1. [FATAL] arr = arr + [x] does NOT append: array '+' concatenation silently returns an empty array (data loss). Always use push(arr, x).\n"
  "2. [FATAL] Defining task/thread inside a while/if/for block: the body NEVER runs (silent). Definitions must be at top level; start/join in the loop.\n"
  "3. [FATAL] s = s + x inside a loop is O(n^2) string concatenation (30000 ops = 922ms). Collect parts in an array then join, or minimize concats.\n"
  "4. [PERF] Global variable read/write in a hot loop is 27%-58%% slower than a function-local (global lookup + sharded lock). Accumulate in a local, write back once.\n"
  "5. [PERF] 'for v in arr' iterator is ~44%% slower than 'for i in 0..len(arr)' indexing. Use indexing in hot loops.\n"
  "6. [PERF] dict[key] is ~23%% slower than array indexing. Prefer arrays for hot lookups.\n"
  "7. [PERF] atomic_add/atomic_set are 3.5x slower than plain assignment. Only use for cross-thread shared counters.\n"
  "8. [PERF] len(arr) inside a loop condition is re-evaluated each pass. Cache: n = len(arr).\n"
  "9. [PERF] Function calls are ~1.8x slower than inlined code. Consider inlining very hot small helpers.\n"
  "10. Syntax: statements need no semicolons; 'for i in 0..10' is the range loop; x++ works on simple variables; // and # are comments.\n"
  "Output format: for each issue: [severity] line-ish description, why, and the exact fix. "
  "Then, if fixes are straightforward, print the corrected full code block. Be concise and specific.";

static int builtin_ai_code_check(VM *vm) {
  const char *code = "";
  if (vm_cur_sp(vm) >= 0) {
    Value a = vm_cur_stack(vm)[vm_cur_sp(vm)];
    if (a.type == VAL_STRING && a.sval) code = a.sval;
  }
  ai_popn(vm, vm->cur_argc);
  char model[128], ep[256], key[256];
  ai_load_config(ep, sizeof ep, key, sizeof key, model, sizeof model);
  if (!model[0]) snprintf(model, sizeof model, "Qwen2.5-7B-Instruct:latest");
  char res[32768];
  int rc = ai_call(model, AI_CODE_RULES, code, 0.2, 4096, res, sizeof res);
  if (rc != 0) { push_string(vm, res); return 1; }
  push_string(vm, res);
  return 1;
}

/* ai_params(friend_id, temperature, max_tokens, reply_freq) *//* ai_params(friend_id, temperature, max_tokens, reply_freq) */
static int builtin_ai_params(VM *vm) {
  const char *id = ai_arg_str(vm, 3);
  double temperature = ai_arg_num(vm, 2);
  int max_tokens = (int)ai_arg_num(vm, 1);
  double reply_freq = ai_arg_num(vm, 0);
  ai_popn(vm, vm->cur_argc);
  char path[512], buf[8192];
  snprintf(path, sizeof path, USERDATA_DIR "\\ai.json");
  ai_read_file(path, buf, sizeof buf);
  char pat[128];
  snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char *p = strstr(buf, pat);
  if (!p) { push_int(vm, 0); return 1; }
  char *obj = p;
  while (obj > buf && *obj != '{') obj--;
  char *e = strchr(p, '}');
  if (!e) { push_int(vm, 0); return 1; }
  char tmp[1024];
  int n = (int)(e - obj + 1);
  if (n > 1023) n = 1023;
  memcpy(tmp, obj, n);
  tmp[n] = 0;
  /* replace temperature/max_tokens/reply_freq fields */
  char nt[1024];
  snprintf(nt, sizeof nt, "{\"id\":\"%s\"", id);
  const char *walk = tmp + strlen(nt);
  char out[1536];
  snprintf(out, sizeof out, "%s", nt);
  char *rest = (char*)walk;
  /* simple rebuild: keep all fields, override the three */
  char name[128], persona[4096], model[128];
  double temp, rf;
  int mt;
  ai_find(id, name, sizeof name, persona, sizeof persona, model, sizeof model, &temp, &mt, &rf);
  char en[1536];
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"persona\":\"%s\",\"model\":\"%s\",\"temperature\":%.2f,\"max_tokens\":%d,\"reply_freq\":%.2f}",
           id, name, persona, model, temperature, max_tokens, reply_freq);
  /* replace object in buf */
  char nb[9216];
  size_t head = (size_t)(obj - buf);
  size_t tail = strlen(buf) - (size_t)(e - buf) - 1;
  snprintf(nb, sizeof nb, "%.*s%s%s", (int)head, buf, en, e + 1);
  ai_write_file(path, nb);
  push_int(vm, 1);
  return 1;
}

void ai_mod_register(VM *vm) {
  vm_register_builtin(vm, "ai_config", builtin_ai_config);
  vm_register_builtin(vm, "ai_register", builtin_ai_register);
  vm_register_builtin(vm, "ai_list", builtin_ai_list);
  vm_register_builtin_full(vm, "ai_chat", builtin_ai_chat, 1|CAP_AI, 0);
  vm_register_builtin_full(vm, "ai_code_check", builtin_ai_code_check, 1|CAP_AI, 0);
  vm_register_builtin(vm, "ai_params", builtin_ai_params);
}
