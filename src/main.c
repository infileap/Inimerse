#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
extern int build_project_impl(void *vm, const char *cfgPath, int mode, const char *outExe);
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define _chmod _chmod
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#define _chdir chdir
#define _chmod chmod
#endif

#ifdef _WIN32
#include <windows.h>
#endif
#include "headless_server.h"
#include "isolate_mod.h"
#include "desugar_mod.h"
#include "lint_mod.h"

#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "../src/vm/jit_mode.h"
#include "platform/platform.h"

/* V0.4 sectioned parameter loader (falls back to legacy .im params). */
int vm_params_load_v2_or_legacy(VM *vm, const char *path);

void gui_mod_register(VM *vm);
void result_mod_register(VM *vm);
void build_mod_register(VM *vm);
void io_mod_register(VM *vm);
void net_mod_register(VM *vm);
void json_mod_register(VM *vm);
void infiverse_mod_register(VM *vm);
void verse_dist_mod_register(VM *vm);
void server_mod_register(VM *vm);
void say_mod_register(VM *vm);
void identity_mod_register(VM *vm);
void social_mod_register(VM *vm);
void ai_mod_register(VM *vm);
void record_mod_register(VM *vm);
void replay_mod_register(VM *vm);
#include "runtime.h"
#include "mod.h"
#include "bytecode.h"
#include "common.h"
#include "compilation/profiler.h"
#include "compilation/debug_info.h"
#include "compilation/wasm_backend.h"
#include "compilation/deps.h"
#include "compilation/checksum.h"


/* forced resource caps (--limit-mem/--limit-vram MB, --limit-time s, --low-config preset) */
static double g_lim_mem = 0, g_lim_vram = 0, g_lim_time = 0;
static int g_gc_on = 0; static int g_lint = 0;

/* --err-json helper: structured JSON error for file-level failures (AI loop) */
static void main_err_json(const char *kind, const char *detail, const char *fix) {
    if (!g_err_json) return;
    char out[1024];
    int oi = 0;
    for (const char *x = detail ? detail : ""; *x && oi < (int)sizeof(out) - 2; x++) {
        unsigned char c = (unsigned char)*x;
        if (c == '"') { out[oi++] = '\\'; out[oi++] = '"'; }
        else if (c == '\\') { out[oi++] = '\\'; out[oi++] = '\\'; }
        else if (c == '\n') { out[oi++] = '\\'; out[oi++] = 'n'; }
        else if (c == '\r') { out[oi++] = '\\'; out[oi++] = 'r'; }
        else if (c == '\t') { out[oi++] = '\\'; out[oi++] = 't'; }
        else out[oi++] = (char)c;
    }
    out[oi] = 0;
    fprintf(stderr, "{\"error\":\"%s\",\"detail\":\"%s\",\"fix\":\"%s\"}\n", kind, out, fix);
}

#ifdef _WIN32
/* [dbg] crash handler for stack backtrace */
static LONG WINAPI inimerse_crash_handler(EXCEPTION_POINTERS *ep) {
        { void *mb = GetModuleHandle(NULL); FILE *cb = fopen("inimerse_crash.log", "a"); if (cb) { fprintf(cb, "[base] %p\n", mb); fclose(cb); } fprintf(stderr, "[crash] base=%p\n", mb);
    {
        HMODULE ripMod = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)ep->ContextRecord->Rip, &ripMod)) {
            char modname[512] = "";
            GetModuleFileNameA(ripMod, modname, sizeof modname);
            fprintf(stderr, "[crash] rip_mod=%s\n", modname);
        }
    } }
FILE *cf = fopen("inimerse_crash.log", "a");
 if (cf) { fprintf(cf, "[crash] code=0x%lX rip=%p\n", (unsigned long)ep->ExceptionRecord->ExceptionCode, (void*)ep->ContextRecord->Rip); fclose(cf); }
 fprintf(stderr, "\n[crash] code=0x%lX\n", (unsigned long)ep->ExceptionRecord->ExceptionCode);
#if defined(__x86_64__)
    fprintf(stderr, "[crash] rip=%p\n", (void*)ep->ContextRecord->Rip);
#else
    fprintf(stderr, "[crash] eip=%p\n", (void*)ep->ContextRecord->Eip);
#endif
    void *frames[32];
    unsigned short n = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    for (unsigned short i = 0; i < n; i++) {
        fprintf(stderr, "[stack] #%u %p\n", i, frames[i]);
        FILE *cf2 = fopen("inimerse_crash.log", "a");
        if (cf2) { fprintf(cf2, "[stack] #%u %p\n", i, frames[i]); fclose(cf2); }
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
static char *get_self_path(void) {
    char buffer[4096];
    int n = im_platform_executable_path(buffer, sizeof(buffer));
    if (n < 0) return NULL;
    return strdup(buffer);
}

#ifdef _WIN32
typedef struct {
    char name[MAX_PATH];
    FILETIME time;
} ChangeFile;

static int changelog_name_cmp(const void *a, const void *b) {
    const ChangeFile *aa = (const ChangeFile *)a;
    const ChangeFile *bb = (const ChangeFile *)b;
    return -CompareFileTime(&aa->time, &bb->time); /* newest first */
}

/* Print the three newest CHANGES*.txt files from the engine directory. */
static int print_changelog(void) {
    ChangeFile files[64];
    int count = 0;
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA("CHANGES*.txt", &fd);
    if (find == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "changelog: no CHANGES*.txt files found\n");
        return 1;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < 64) {
            snprintf(files[count].name, sizeof files[0].name, "%s", fd.cFileName);
            files[count].time = fd.ftLastWriteTime;
            count++;
        }
    } while (FindNextFileA(find, &fd));
    FindClose(find);
    if (count == 0) {
        fprintf(stderr, "changelog: no CHANGES*.txt files found\n");
        return 1;
    }
    qsort(files, (size_t)count, sizeof files[0], changelog_name_cmp);
    int limit = count > 3 ? 3 : count;
    for (int i = 0; i < limit; i++) {
        FILE *f = fopen(files[i].name, "rb");
        if (!f) continue;
        printf("\n=== %s ===\n", files[i].name);
        char line[4096];
        while (fgets(line, sizeof line, f)) fputs(line, stdout);
        fclose(f);
    }
    return 0;
}
#else
typedef struct { char name[512]; struct timespec time; } ChangeFile;
static int changelog_name_cmp(const void *a, const void *b) { const ChangeFile *aa=(const ChangeFile*)a,*bb=(const ChangeFile*)b; if (aa->time.tv_sec != bb->time.tv_sec) return aa->time.tv_sec < bb->time.tv_sec ? 1 : -1; return aa->time.tv_nsec < bb->time.tv_nsec ? 1 : (aa->time.tv_nsec > bb->time.tv_nsec ? -1 : 0); }
static int print_changelog(void) { ChangeFile files[64]; int count=0; DIR *dir=opendir("."); if(!dir){fprintf(stderr,"changelog: cannot open current directory\\n");return 1;} struct dirent *e; while((e=readdir(dir)) && count<64){size_t n=strlen(e->d_name); if(strncmp(e->d_name,"CHANGES",7)!=0 || n<11 || strcmp(e->d_name+n-4,".txt")!=0) continue; struct stat st; if(stat(e->d_name,&st)!=0 || !S_ISREG(st.st_mode)) continue; snprintf(files[count].name,sizeof files[0].name,"%s",e->d_name); files[count].time=st.st_mtim; count++;} closedir(dir); if(!count){fprintf(stderr,"changelog: no CHANGES*.txt files found\\n");return 1;} qsort(files,(size_t)count,sizeof files[0],changelog_name_cmp); int limit=count>3?3:count; for(int i=0;i<limit;i++){FILE *f=fopen(files[i].name,"rb");if(!f)continue;printf("\\n=== %s ===\\n",files[i].name);char line[4096];while(fgets(line,sizeof line,f))fputs(line,stdout);fclose(f);} return 0; }
#endif

static void usage(const char *prog) {
    printf("Inimerse command line\n\n");
    printf("Usage:\n");
    printf("  %s <script.im>\n", prog);
    printf("  %s run <script.im|script.inim> [args...]\n", prog);
    printf("  %s compile <input.im> [output.inim]   [--incremental|--force|--symbols] [--abi-version N] [--reproducible] [--debug-info]\n", prog);
    printf("  %s buildc <input.im> [output.inim]    (alias of compile)\n", prog);
    printf("  %s symbols <input.im> [output.symbols]\n", prog);
    printf("  %s profile <script.im> [output.prof]\n", prog);
    printf("  %s debug <script.im>\n", prog);
    printf("  %s build <script.im> [output.exe]\n", prog);
    printf("  %s where                    print the active engine path\n", prog);
    printf("  %s changelog               show the newest change logs\n", prog);
    printf("  %s --version               print engine version\n", prog);
    printf("  %s --gui <script.im>\n", prog);
    printf("  %s                         interactive REPL\n", prog);
}

/* unified script loader: .inim bytecode or .im parse+compile, then vm_run */
static const char *params_path = "params.params";  /* default parameter file */

/* ---- minimal zip reader (STORE only) for .imjar ----
   extracts every entry into outDir (creating subdirs) */
static void jar_mkdir_p(const char *path) {
    (void)im_platform_mkdirs(path);
}
static int zip_extract_all(const char *zipPath, const char *outDir) {
    FILE *f = fopen(zipPath, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if (fsz < 22) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc((size_t)fsz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) { free(buf); fclose(f); return -1; }
    fclose(f);
    /* find EOCD from the end */
    int eocd = -1;
    for (long i = fsz - 22; i >= 0; i--) {
        if (buf[i]==0x50 && buf[i+1]==0x4b && buf[i+2]==0x05 && buf[i+3]==0x06) { eocd = (int)i; break; }
    }
    if (eocd < 0) { free(buf); return -1; }
    uint32_t cdCount = *(uint32_t*)(buf + eocd + 10);
    uint32_t cdSize  = *(uint32_t*)(buf + eocd + 12);
    uint32_t cdOff   = *(uint32_t*)(buf + eocd + 16);
    if (cdOff + cdSize > (uint32_t)fsz) { free(buf); return -1; }
    int extracted = 0;
    uint32_t pos = cdOff;
    for (uint32_t e = 0; e < cdCount; e++) {
        if (pos + 46 > (uint32_t)fsz) break;
        if (!(buf[pos]==0x50 && buf[pos+1]==0x4b && buf[pos+2]==0x01 && buf[pos+3]==0x02)) break;
        uint16_t method = *(uint16_t*)(buf + pos + 10);
        uint32_t csize  = *(uint32_t*)(buf + pos + 20);
        uint32_t usize  = *(uint32_t*)(buf + pos + 24);
        uint16_t nl = *(uint16_t*)(buf + pos + 28);
        uint16_t el = *(uint16_t*)(buf + pos + 30);
        uint16_t cl = *(uint16_t*)(buf + pos + 32);
        uint32_t lho = *(uint32_t*)(buf + pos + 42);
        char name[512];
        size_t ncopy = nl < sizeof(name)-1 ? nl : sizeof(name)-1;
        memcpy(name, buf + pos + 46, ncopy); name[ncopy] = 0;
        pos += 46 + nl + el + cl;
        if (method != 0) { fprintf(stderr, "imjar: skip compressed entry '%s' (method %d)\n", name, method); continue; }
        /* local header */
        if (lho + 30 > (uint32_t)fsz) continue;
        uint16_t lnl = *(uint16_t*)(buf + lho + 26);
        uint16_t lel = *(uint16_t*)(buf + lho + 28);
        uint32_t dataOff = lho + 30 + lnl + lel;
        if (dataOff + csize > (uint32_t)fsz) continue;
        char outPath[1024];
        snprintf(outPath, sizeof outPath, "%s/%s", outDir, name);
#ifdef _WIN32
        for (char *p = outPath; *p; p++) if (*p == '/') *p = '\\';
        char *slash = strrchr(outPath, '\\');
#else
        char *slash = strrchr(outPath, '/');
#endif
        if (slash) {
            *slash = 0; jar_mkdir_p(outPath);
#ifdef _WIN32
            *slash = '\\';
#else
            *slash = '/';
#endif
        }
        FILE *w = fopen(outPath, "wb");
        if (!w) continue;
        fwrite(buf + dataOff, 1, csize, w);
        fclose(w);
        extracted++;
    }
    free(buf);
    fprintf(stderr, "[imjar] extracted %d files to %s\n", extracted, outDir);
    return extracted;
}

static int load_and_run(VM *vm, const char *path) {
    char jarCache[1024];
    const char *runPath = path;
    size_t plen = strlen(path);
    if (plen > 6 && strcmp(path + plen - 6, ".imjar") == 0) {
        snprintf(jarCache, sizeof jarCache, "%s_cache", path);
        zip_extract_all(path, jarCache);
        _chdir(jarCache);
        runPath = "main.inim";
        plen = strlen(runPath);
        path = runPath;
    }
    if (plen > 5 && strcmp(path + plen - 5, ".inim") == 0) {
        Bytecode *bc = bytecode_read_file(path);
        if (!bc) { if (g_err_json) { main_err_json("io", path, "recompile the .inim with buildc (old format)"); return 1; } fprintf(stderr, "error: cannot load bytecode '%s' (old format? recompile with buildc)\n", path); return 1; }
    /* params first: their globals get stable indices before the main bytecode loads */
    {
        FILE *pf = fopen(params_path, "rb");
        if (pf) { fclose(pf); vm_params_load_v2_or_legacy(vm, params_path); }
    }
        vm_load_bytecode(vm, bc);
        vm_run(vm);
        bytecode_free(bc);
        if (vm->last_error) return 1;
        return 0;
    }
    Program *prog = parse_program_file(path);
    if (!prog) { if (g_err_json) { main_err_json("io", path, "check that the script path exists"); return 1; } fprintf(stderr, "error: cannot read script '%s'\n", path); return 1; }
    Compiler *comp = compiler_new();

    /* params first: their globals get stable indices, then main compile pre-registers them */
    {
        FILE *pf = fopen(params_path, "rb");
        if (pf) { fclose(pf); vm_params_load_v2_or_legacy(vm, params_path); }
    }
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name) register_global(comp, vm->globals[i].name);
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    vm_load_bytecode(vm, bc);
    vm_run(vm);
    compiler_free(comp);
    if (vm->last_error) return 1;
    return 0;
}


/* read a whole file into a NUL-terminated buffer (NULL on failure) */
/* UTF-8 validity check (strict: overlongs / surrogates rejected) */
static int inim_utf8_valid(const unsigned char *s, int len) {
    int i = 0;
    while (i < len) {
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        int need;
        if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        else return 0;
        if (i + need > len) return 0;
        for (int k = 1; k < need; k++)
            if ((s[i + k] & 0xC0) != 0x80) return 0;
        if (need == 2 && (c & 0xFE) == 0xC0) return 0;
        if (need == 3 && c == 0xE0 && s[i + 1] < 0xA0) return 0;
        if (need == 3 && c == 0xED && s[i + 1] >= 0xA0) return 0;
        if (need == 4 && c == 0xF0 && s[i + 1] < 0x90) return 0;
        if (need == 4 && c > 0xF4) return 0;
        if (need == 4 && c == 0xF4 && s[i + 1] >= 0x90) return 0;
        i += need;
    }
    return 1;
}

/* Read a text file as UTF-8: strip BOM; if the bytes are not valid UTF-8,
   assume GBK (cp936, the engine's legacy encoding) and transcode to UTF-8. */
char *inim_load_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > (1 << 26)) { fclose(f); return NULL; }
    char *buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[rd] = '\0';
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 3 + 1);
        rd -= 3;
    }
    #ifdef _WIN32
    if (rd > 0 && !inim_utf8_valid((const unsigned char*)buf, (int)rd)) {
        int wlen = MultiByteToWideChar(936, 0, buf, (int)rd, NULL, 0);
        if (wlen > 0) {
            wchar_t *wb = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
            if (wb) {
                MultiByteToWideChar(936, 0, buf, (int)rd, wb, wlen);
                int ulen = WideCharToMultiByte(CP_UTF8, 0, wb, wlen, NULL, 0, NULL, NULL);
                if (ulen > 0) {
                    char *ub = (char*)malloc((size_t)ulen + 1);
                    if (ub) {
                        WideCharToMultiByte(CP_UTF8, 0, wb, wlen, ub, ulen, NULL, NULL);
                        ub[ulen] = '\0';
                        free(wb);
                        free(buf);
                        return ub;
                    }
                }
                free(wb);
            }
        }
    }
    #endif
    return buf;
}

static char *read_file_alloc(const char *path, long *out_len) {
    char *buf = inim_load_text(path);
    if (buf && out_len) *out_len = (long)strlen(buf);
    return buf;
}

/* run a program from an in-memory source string (used by the .im debugger splice) */
static int load_and_run_source(VM *vm, const char *src, const char *display) {
    Program *prog = parse_program(src);
    if (!prog) { if (g_err_json) { main_err_json("parse", display ? display : "(source)", "check the script syntax"); return 1; } fprintf(stderr, "error: cannot parse '%s'\n", display ? display : "(source)"); return 1; }
    Compiler *comp = compiler_new();
    fprintf(stderr, "[main] compile preregister gc=%d g23.name=%s\n", vm->globalCount,
            (vm->globalCount > 23 && vm->globals[23].name) ? vm->globals[23].name : "(null)");
    /* params first: their globals get stable indices, then main compile pre-registers them */
    {
        FILE *pf = fopen(params_path, "rb");
        if (pf) { fclose(pf); vm_params_load_v2_or_legacy(vm, params_path); }
    }
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name) register_global(comp, vm->globals[i].name);
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    vm_load_bytecode(vm, bc);
    vm_run(vm);
    compiler_free(comp);
    if (vm->last_error) return 1;
    return 0;
}

static void repl(VM *vm) {
    printf("Inimerse REPL - commands: :quit, :run <file>\n");
    char line[1024];
    while (1) {
        printf(">>> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) break;
        if (strncmp(line, ":run ", 5) == 0) {
            load_and_run(vm, line + 5);
        } else {
            Program *prog = parse_program(line);
            if (!prog) { printf("parse error\n"); continue; }
            Compiler *comp = compiler_new();
            compiler_compile(comp, prog);
            Bytecode *bc = compiler_get_main_bytecode(comp);
            vm->ip = 0;
            vm_load_bytecode(vm, bc);
            vm_run(vm);
            compiler_free(comp);
        }
    }
}

/* �?Windows 璺緞涓殑 '/' 缁熶竴锟?'\' */
static void normalize_path(char *p) {
#ifdef _WIN32
    while (*p) { if (*p == '/') *p = '\\'; p++; }
#else
    (void)p;
#endif
}

/* 鍘绘帀璺緞涓殑鎵╁睍鍚嶏紙�?"a.im" -> "a"锛夛紝缁撴灉鍐欏�?out */
static void strip_ext_into(char *out, size_t out_sz, const char *path) {
    strncpy(out, path, out_sz - 1);
    out[out_sz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && strchr(dot, '\\') == NULL && strchr(dot, '/') == NULL)
        *dot = '\0';
}

/* 鑾峰彇鑴氭湰鐨勭粷瀵硅矾寰勶紙malloc锛岃皟鐢拷?free�?*/
/* Loose absolute path: works for paths that do not exist yet (compile outputs). */
static char *make_abs_path_loose(const char *path) {
#ifdef _WIN32
    char *abs = malloc(MAX_PATH); if (!abs) return NULL;
    if (!_fullpath(abs, path, MAX_PATH)) { free(abs); return NULL; }
    normalize_path(abs); return abs;
#else
    char *abs = realpath(path, NULL);
    if (abs) return abs;
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) { /* bare name in the current directory (which always exists) */
        char *cwd_abs = realpath(".", NULL);
        if (!cwd_abs) return NULL;
        size_t need = strlen(cwd_abs) + strlen(tmp) + 2;
        char *out = malloc(need);
        snprintf(out, need, "%s/%s", cwd_abs, tmp);
        free(cwd_abs);
        return out;
    }
    *slash = '\0';
    char *dir_abs = realpath(tmp[0] ? tmp : "/", NULL);
    if (!dir_abs) return NULL;
    size_t need = strlen(dir_abs) + strlen(slash + 1) + 2;
    char *out = malloc(need);
    snprintf(out, need, "%s/%s", dir_abs, slash + 1);
    free(dir_abs);
    return out;
#endif
}

static char *make_abs_path(const char *path) {
#ifdef _WIN32
    char *abs = malloc(MAX_PATH); if (!abs) return NULL;
    if (!_fullpath(abs, path, MAX_PATH)) { free(abs); return NULL; }
    normalize_path(abs); return abs;
#else
    return realpath(path, NULL);
#endif
}

/* 灏嗗伐浣滅洰褰曞垏鎹㈠埌鑴氭湰鎵€鍦ㄧ洰褰曪紙杩斿洖鑴氭湰缁濆璺緞锛宮alloc�?*/
static char *chdir_to_script_dir(const char *script) {
    char *abs = make_abs_path(script);
    if (!abs) return NULL;
    char *slash = strrchr(abs, '/');
#ifdef _WIN32
    if (!slash) slash = strrchr(abs, '\\');
#endif
    if (slash) {
        *slash = '\0';
        _chdir(abs);
#ifdef _WIN32
        *slash = '\\';
#else
        *slash = '/';
#endif
    }
    return abs;
}

/* �?exe 涓噴鏀惧祵鍏ョ殑妯＄粍鍒颁复鏃剁洰褰曞苟鍔犺浇 */
static void load_embedded_mods_impl(VM *vm) {
#ifndef _WIN32
    (void)vm; return;
#else
    char *self = get_self_path();
    if (!self) return;

    /* 灏濊瘯鎻愬彇妯＄粍璧勬簮 */
    long len = 0;
    unsigned char *buf = bytecode_extract_mods(self, &len);
    if (!buf) { free(self); return; }
    free(buf);

    /* 閲婃斁鍒颁复鏃剁洰锟?*/
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);
    char mods_dir[MAX_PATH];
    snprintf(mods_dir, sizeof(mods_dir), "%s\\inimerse_mods_%lu", tmpdir, (unsigned long)GetCurrentProcessId());
    /* 娓呯┖鏃х洰锟?*/
    char del_cmd[1024];
    snprintf(del_cmd, sizeof(del_cmd), "rmdir /s /q \"%s\" 2>nul", mods_dir);
    system(del_cmd);

    int released = bytecode_release_mods(self, mods_dir);
    if (released > 0) {
        mod_load_all(vm, mods_dir);
    }
    free(self);
#endif
}

static void register_core_modules(VM *vm) {
    isolate_mod_register(vm);
    lint_mod_register(vm);
    vm_debug_builtins_register(vm);
    gui_mod_register(vm);
    result_mod_register(vm);
    io_mod_register(vm);
    net_mod_register(vm);
    json_mod_register(vm);
    server_mod_register(vm);
    say_mod_register(vm);
    identity_mod_register(vm);
    social_mod_register(vm);
    ai_mod_register(vm);
    record_mod_register(vm);
    replay_mod_register(vm);
}

static void register_world_modules(VM *vm) {
    infiverse_mod_register(vm);
    verse_dist_mod_register(vm);
    build_mod_register(vm);
}

/* ---------- compile/buildc/profile/symbols CLI helpers (v0.5 roadmap) ---------- */

/* Export a readable symbol table from compiled bytecode (functions, threads,
 * globals) — `inimerse symbols` / `compile --symbols`. */
static int main_write_symbols(Bytecode *bc, const char *input, const char *output,
                              int abi_version, const char *abi_target) {
    FILE *fp = fopen(output, "w");
    if (!fp) { fprintf(stderr, "error: cannot write '%s'\n", output); return 1; }
    fprintf(fp, "Inimerse Script Symbol Table\n");
    fprintf(fp, "Script: %s\n", input);
    fprintf(fp, "Bytecode Format: INIMBC/%d\n", INIM_BYTECODE_VERSION);
    fprintf(fp, "ABI Version: %d\n", abi_version >= 0 ? abi_version : INIM_ABI_VERSION);
    fprintf(fp, "Target: %s\n", abi_target);
    fprintf(fp, "Global Functions:\n");
    for (int i = 0; i < bc->func_count; i++)
        if (bc->func_names[i]) fprintf(fp, "  %s\n", bc->func_names[i]);
    fprintf(fp, "Threads:\n");
    for (int i = 0; i < bc->thread_count; i++)
        if (bc->thread_names[i]) fprintf(fp, "  %s\n", bc->thread_names[i]);
    fprintf(fp, "Globals:\n");
    for (int i = 0; i < bc->global_name_count; i++)
        if (bc->global_names[i]) fprintf(fp, "  %s\n", bc->global_names[i]);
    fclose(fp);
    return 0;
}

/* Write <output>.build.json: toolchain identity, options and the SHA-256 of
 * every dependency plus the resulting bytecode (reproducible-build record). */
static void main_write_build_record(const char *output, int abi_version, const char *abi_target) {
    char rec_path[2048];
    snprintf(rec_path, sizeof(rec_path), "%s.build.json", output);
    DepEntry *deps = NULL; int ndeps = 0, dep_abi = 0;
    deps_read(output, &deps, &ndeps, &dep_abi);
    char bc_sum[65];
    inim_file_sha256(output, bc_sum);
    FILE *fp = fopen(rec_path, "w");
    if (!fp) { deps_free(deps, ndeps); return; }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"record_version\": 1,\n");
    fprintf(fp, "  \"engine\": \"inimerse %s\",\n", INFIVERSE_VERSION);
    fprintf(fp, "  \"bytecode_format\": \"INIMBC/%d\",\n", INIM_BYTECODE_VERSION);
    fprintf(fp, "  \"abi_version\": %d,\n", abi_version >= 0 ? abi_version : INIM_ABI_VERSION);
    fprintf(fp, "  \"target\": \"%s\",\n", abi_target);
    fprintf(fp, "  \"bytecode_sha256\": \"%s\",\n", bc_sum);
    fprintf(fp, "  \"dependencies\": [\n");
    for (int i = 0; i < ndeps; i++)
        fprintf(fp, "    {\"path\": \"%s\", \"sha256\": \"%s\"}%s\n",
                deps[i].path, deps[i].sha_hex, i + 1 < ndeps ? "," : "");
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
    deps_free(deps, ndeps);
}

/* AOT packaging (experimental channel, roadmap §2.3): copy the engine
   executable and append the compiled bytecode — the result self-executes via
   bytecode_load_from_exe.  The optimizing AOT backend remains future work. */
static int main_aot_package(const char *input, const char *output) {
    Program *prog = parse_program_file(input);
    if (!prog) { fprintf(stderr, "error: cannot read script '%s'\n", input); return 1; }
    Compiler *comp = compiler_new();
    comp->abi_version = INIM_ABI_VERSION;
    comp->target = TARGET_AOT;
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    char *self = get_self_path();
    if (!self) { fprintf(stderr, "error: cannot locate the engine executable\n"); compiler_free(comp); return 1; }
    int rc = bytecode_append_to_exe(self, bc, output);
    free(self);
    compiler_free(comp);
    if (rc != 0) { fprintf(stderr, "error: AOT packaging failed for '%s'\n", output); return 1; }
#ifndef _WIN32
    chmod(output, 0755);
#else
    _chmod(output, 0755);
#endif
    return 0;
}

/* Print one instruction section in the same textual form the self-hosted
 * compiler emits (`selfhost/compiler.im --dump`): a header line, then one
 * `op,r1,r2,r3` line per instruction.  Kept byte-comparable on purpose — the
 * `bytecode` subcommand exists so the C compiler's artifact and the self-host
 * compiler's artifact can be diffed and hashed against each other. */
static void dump_bc_section(const char *header, Bytecode *bc) {
    printf("%s\n", header);
    for (int i = 0; i < bc->count; i++)
        printf("%d,%d,%d,%d\n", (int)bc->code[i].op,
               bc->code[i].r1, bc->code[i].r2, bc->code[i].r3);
}

/* `bytecode <input.im>`: compile with the C compiler and dump the stream. */
static int main_dump_bytecode(const char *input) {
    char *abs_in = make_abs_path_loose(input);
    if (!abs_in) abs_in = strdup(input);
    char *abs_script = chdir_to_script_dir(input); /* imports resolve against script dir */
    Program *prog = parse_program_file(abs_script ? abs_script : abs_in);
    if (!prog) {
        fprintf(stderr, "error: cannot read script '%s'\n", input);
        free(abs_in); free(abs_script);
        return 1;
    }
    Compiler *comp = compiler_new();
    comp->abi_version = INIM_ABI_VERSION;
    comp->target = TARGET_HOST;
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    char hdr[1200];
    dump_bc_section("main:", bc);
    for (int i = 0; i < bc->func_count; i++) {
        snprintf(hdr, sizeof hdr, "func:%s:%d",
                 bc->func_names[i] ? bc->func_names[i] : "?", bc->func_argc[i]);
        dump_bc_section(hdr, bc->funcs[i]);
    }
    for (int i = 0; i < bc->thread_count; i++) {
        snprintf(hdr, sizeof hdr, "thread:%s:%d",
                 bc->thread_names[i] ? bc->thread_names[i] : "?", bc->thread_argc[i]);
        dump_bc_section(hdr, bc->threads[i]);
    }
    compiler_free(comp);
    free(abs_in);
    free(abs_script);
    return 0;
}

/* Shared pipeline behind `buildc` and `compile`: parse + compile to .inim
 * bytecode, record a dependency trailer (main source + every resolved import,
 * SHA-256 each), optional symbol table export, and with --incremental skip
 * the rebuild when all recorded dependencies still match. */
static int main_compile_cmd(const char *input, const char *output,
                            int abi_version, const char *abi_target,
                            int emit_symbols, int incremental, int force,
                            int reproducible, int debug_info) {
    if (abi_version >= 0 && abi_version != INIM_ABI_VERSION) {
        fprintf(stderr, "error: ABI version mismatch: requested %d, toolchain provides %d (see --abi-version)\n",
                abi_version, INIM_ABI_VERSION);
        return 2;
    }
    int is_wasm = (strcmp(abi_target, "wasm") == 0 || strcmp(abi_target, "wasm32") == 0);

    if (incremental && !is_wasm && !force) {
        DepEntry *deps = NULL; int ndeps = 0, dep_abi = 0;
        if (deps_read(output, &deps, &ndeps, &dep_abi) == 0 && ndeps > 0) {
            int stale = 0;
            for (int i = 0; i < ndeps; i++) {
                char abs[2048], sum[65];
                deps_entry_abs_path(&deps[i], output, abs, sizeof(abs));
                if (inim_file_sha256(abs, sum) != 0 ||
                    strncmp(sum, deps[i].sha_hex, 64) != 0) { stale = 1; break; }
            }
            if (!stale) {
                printf("up to date: %s\n", output);
                if (reproducible) {
                    main_write_build_record(output, abi_version, abi_target);
                    char sum[65]; inim_file_sha256(output, sum);
                    printf("reproducible: ok (%s)\n", sum);
                }
                deps_free(deps, ndeps);
                return 0;
            }
        }
        deps_free(deps, ndeps);
    }

    Program *prog = parse_program_file(input);
    if (!prog) { fprintf(stderr, "error: cannot read script '%s'\n", input); return 1; }

    if (is_wasm) {
        /* WebAssembly MVP output: numeric subset, equivalence-validated
           against the interpreter by tools/wasm_backend.test.py */
        if (wasm_compile_program(prog, output) != 0) {
            fprintf(stderr, "error: %s\n", wasm_backend_last_error());
            return 1;
        }
        printf("compiled: %s -> %s (wasm MVP subset)\n", input, output);
        return 0;
    }

    Compiler *comp = compiler_new();
    comp->abi_version = (abi_version >= 0) ? abi_version : INIM_ABI_VERSION;
    comp->target = TARGET_HOST;
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    int rc = bytecode_write_file(output, bc);
    if (rc != 0) { fprintf(stderr, "error: write '%s' failed\n", output); compiler_free(comp); return 1; }

    /* dependency trailer: paths relative to the output file's directory so
       identical project layouts hash identically on any host (reproducible) */
    {
        char out_dir[2048];
        deps_bc_dirname(output, out_dir, sizeof(out_dir));
        DepEntry *deps = (DepEntry*)malloc((comp->dep_count + 1) * sizeof(DepEntry));
        int n = 0;
        char *abs_main = make_abs_path_loose(input);
        char rel[2048];
        if (abs_main && inim_file_sha256(abs_main, deps[0].sha_hex) == 0) {
            deps_relative_path(out_dir, abs_main, rel, sizeof(rel));
            deps[0].path = strdup(rel); n = 1;
        }
        free(abs_main);
        for (int i = 0; i < comp->dep_count && n < comp->dep_count + 1; i++) {
            char *abs = make_abs_path_loose(comp->dep_paths[i]);
            if (abs && inim_file_sha256(abs, deps[n].sha_hex) == 0) {
                deps_relative_path(out_dir, abs, rel, sizeof(rel));
                deps[n++].path = strdup(rel);
            }
            free(abs);
        }
        deps_write_trailer(output, deps, n, comp->abi_version);
        deps_free(deps, n);
    }

    if (emit_symbols) {
        char sym_out[2048];
        snprintf(sym_out, sizeof(sym_out), "%s.symbols", output);
        if (main_write_symbols(bc, input, sym_out, abi_version, abi_target) == 0)
            printf("symbols: %s\n", sym_out);
    }

    if (debug_info) {
        if (debug_write_sidecar(comp, input, output) == 0) {
            printf("debug-info: %s.dbg %s.debug_line\n", output, output);
        } else {
            fprintf(stderr, "error: cannot write debug sidecar for '%s'\n", output);
        }
    }

    compiler_free(comp);
    if (reproducible) {
        main_write_build_record(output, abi_version, abi_target);
        char sum[65]; inim_file_sha256(output, sum);
        printf("reproducible: ok (%s)\n", sum);
    }
    printf("compiled: %s -> %s\n", input, output);
    return 0;
}

int main(int argc, char **argv) {


#ifdef _WIN32
    SetUnhandledExceptionFilter(inimerse_crash_handler);
#endif

    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        printf("inimerse %s\n", INFIVERSE_VERSION);
        return 0;
    }

    /* Lightweight discovery command used by IDEs and desktop tooling.  Keep
     * it before VM/GUI initialization so it is side-effect free and fast. */
    if (argc >= 2 && strcmp(argv[1], "where") == 0) {
        char *self_path = get_self_path();
        if (!self_path) {
            fprintf(stderr, "inimerse where: unable to determine executable path\n");
            return 1;
        }
        puts(self_path);
        free(self_path);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "capabilities") == 0) {
    const char *caps[] = { "threads", "fiber", "process", "socket", "posix_fs", "native_dll", "gui", NULL };
        for (int i = 0; caps[i]; i++) if (im_platform_has_capability(caps[i])) puts(caps[i]);
        return 0;
    }

    /* P1 multi-size: per-monitor DPI awareness (Win10+), fallback to system DPI */
 #ifdef _WIN32
    {
        typedef BOOL (WINAPI *SDPAC)(void*);
        HMODULE hu = GetModuleHandleA("user32.dll");
        if (hu) {
            SDPAC fn = (SDPAC)(void*)GetProcAddress(hu, "SetProcessDpiAwarenessContext");
            if (fn) fn((void*)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */ /* TMP-DPI-DISABLED */
            else SetProcessDPIAware();
        }
    }
#endif    /* platform DPI setup */
    int gui_mode = 0;
    int safe_mode = 0;
    int load_mods = 1;
    int headless_mode = 0;
    int headless_port = 11440;
    int jit_mode = IM_JIT_OFF;
int headless_http_port = 11470;
    if (argc >= 2 && strcmp(argv[1], "--gui") == 0) {
        gui_mode = 1;
        for (int i = 1; i < argc - 1; i++) argv[i] = argv[i + 1];
        argc--;
    }
    if (argc >= 2 && (strcmp(argv[1], "--jit") == 0 || strncmp(argv[1], "--jit=", 6) == 0)) {
        const char *jit_arg = strcmp(argv[1], "--jit") == 0 ? (argc >= 3 ? argv[2] : NULL) : argv[1] + 6;
        jit_mode = im_jit_mode_parse(jit_arg);
        if (jit_mode < 0) { fprintf(stderr, "error: --jit expects off|template|optimized\n"); return 2; }
        im_jit_mode = jit_mode;
        int delta = strcmp(argv[1], "--jit") == 0 ? 2 : 1;
        for (int i = 1; i < argc - delta; i++) argv[i] = argv[i + delta];
        argc -= delta;
    }
    /* 妫€�?--time-limit N锛堝叏灞€閫夐」锛屽崟浣嶇锛岄粯�?20锛涘繀椤诲�?--gui 涔嬪悗鎴栦箣鍓嶅潎鍙級 */
        if (argc >= 2 && strcmp(argv[1], "--headless") == 0) {
        headless_mode = 1;
        gui_mode = 1;
        for (int i = 1; i < argc - 1; i++) argv[i] = argv[i + 1];
        argc--;
        if (argc >= 3 && strcmp(argv[1], "--port") == 0) {
            headless_port = atoi(argv[2]);
            for (int i = 1; i < argc - 2; i++) argv[i] = argv[i + 2];
            argc -= 2;
        }
        /* --params <file>: parameter file (default params.params) */
        if (argc >= 3 && strcmp(argv[1], "--params") == 0) {
            params_path = argv[2];
            for (int i = 1; i < argc - 2; i++) argv[i] = argv[i + 2];
            argc -= 2;
        }
        if (argc >= 3 && strcmp(argv[1], "--http-port") == 0) {
            headless_http_port = atoi(argv[2]);
            for (int i = 1; i < argc - 2; i++) argv[i] = argv[i + 2];
            argc -= 2;
        }
    }
unsigned long timeout_ms = 0;
    int timeout_set = 0;   /* --time-limit given: 0 = unlimited */
    if (argc >= 3) {
        for (int i = 1; i < argc - 1; i++) {
            if (strcmp(argv[i], "--time-limit") == 0) {
                timeout_ms = (unsigned long)atol(argv[i + 1]) * 1000;
                timeout_set = 1;
                for (int j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
                argc -= 2;
                break;
            }
        }
    }

    if (gui_mode) {
        /* 闅愯棌鎺у埗鍙扮獥�?*/
 #ifdef _WIN32
        HWND console = GetConsoleWindow();
        if (console) ShowWindow(console, SW_HIDE);
#endif
    }

#ifdef _WIN32
    /* UTF-8 console: .im sources are UTF-8 (legacy GBK files are transcoded at load) */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    /* Windows packaged builds resolve bundled DLL/assets beside the EXE.
       POSIX keeps the caller's working directory so relative script paths
       (and the benchmark harness) behave as expected. */
#ifdef _WIN32
    char *self = get_self_path();
    if (self) {
        char *p = strrchr(self, '\\');
        if (!p) p = strrchr(self, '/');
        if (p) { *p = '\0'; _chdir(self); }
        free(self);
    }
#endif

#ifdef _WIN32
    if (argc >= 2 && strcmp(argv[1], "changelog") == 0)
        return print_changelog();
#endif

    /* 鏃犲弬鏁帮細鍏堝皾璇曞唴宓屽瓧鑺傜爜锛堟墦鍖呭悗�?exe锛夛紝鍚﹀垯杩涘叆 REPL */        /* leading flags: repeatable and order-independent (--safe / --err-json) */
    for (;;) {
        if (argc >= 2 && strcmp(argv[1], "--err-json") == 0) g_err_json = 1;
        if (argc >= 3 && strcmp(argv[1], "--desugar") == 0) {
            return desugar_file(argv[2], argc >= 4 ? argv[3] : NULL);
        }
        else if (argc >= 2 && strcmp(argv[1], "--safe") == 0) safe_mode = 1;
        else if (argc >= 2 && strcmp(argv[1], "--no-mods") == 0) load_mods = 0;
        else if (argc >= 2 && strcmp(argv[1], "--lint") == 0) g_lint = 1;
        else if (argc >= 3 && strcmp(argv[1], "--limit-mem") == 0) { g_lim_mem = atof(argv[2]); }
        else if (argc >= 3 && strcmp(argv[1], "--limit-vram") == 0) { g_lim_vram = atof(argv[2]); }
        else if (argc >= 3 && strcmp(argv[1], "--limit-time") == 0) { g_lim_time = atof(argv[2]); }
        else if (argc >= 2 && strcmp(argv[1], "--low-config") == 0) { g_lim_mem = 64; g_lim_vram = 32; g_lim_time = 10; }
        else break;
        int delta = 1;
        if (argc >= 3 && (strcmp(argv[1], "--limit-mem") == 0 || strcmp(argv[1], "--limit-vram") == 0 || strcmp(argv[1], "--limit-time") == 0)) delta = 2;
        for (int i = 1; i < argc - delta; i++) argv[i] = argv[i + delta];
        argc -= delta;
    }

    /* ABI version and compilation target options (global flags, consumed by
       compile/buildc/run/profile commands). Options may sit before or after
       the subcommand word (--abi-version N buildc a.im / buildc a.im -f). */
    int abi_version = -1;
    const char *abi_target = "host";
    int aot_mode = 0;
    int profile_mode = 0;
    int emit_symbols = 0;
    int incremental_mode = 0;
    int force_build = 0;
    int reproducible_mode = 0;
    int debug_info_mode = 0;
    for (;;) {
        int base = (argc >= 2 && (strcmp(argv[1], "buildc") == 0 || strcmp(argv[1], "compile") == 0 ||
                                  strcmp(argv[1], "run") == 0 || strcmp(argv[1], "profile") == 0 ||
                                  strcmp(argv[1], "symbols") == 0)) ? 2 : 1;
        if (base >= argc) break;
        const char *o = argv[base];
        const char *v = (base + 1 < argc) ? argv[base + 1] : NULL;
        int delta = 0;
        if (v && strcmp(o, "--abi-version") == 0) { abi_version = atoi(v); delta = 2; }
        else if (v && strcmp(o, "--abi-target") == 0) { abi_target = v; delta = 2; }
        else if (strcmp(o, "--aot") == 0) { aot_mode = 1; delta = 1; }
        else if (strcmp(o, "--profile") == 0) { profile_mode = 1; delta = 1; }
        else if (strcmp(o, "--symbols") == 0) { emit_symbols = 1; delta = 1; }
        else if (strcmp(o, "--incremental") == 0) { incremental_mode = 1; delta = 1; }
        else if (strcmp(o, "--reproducible") == 0) { reproducible_mode = 1; delta = 1; }
        else if (strcmp(o, "--debug-info") == 0) { debug_info_mode = 1; delta = 1; }
        else if (strcmp(o, "--force") == 0 || strcmp(o, "-f") == 0) { force_build = 1; delta = 1; }
        else break;
        for (int i = base; i < argc - delta; i++) argv[i] = argv[i + delta];
        argc -= delta;
    }

if (argc == 1) {
        VM vm; vm_init(&vm);
    if (g_lim_mem > 0) vm.limit_mem = g_lim_mem * 1024.0 * 1024.0;
    if (g_lim_vram > 0) vm.limit_vram = g_lim_vram * 1024.0 * 1024.0;
    if (g_lim_time > 0) vm.limit_time = g_lim_time;
    vm.safe_mode = safe_mode;
    if (g_gc_on) { vm.gc_enabled =1; if (vm.gc_threshold <=0) vm.gc_threshold =2.0 *1024.0 *1024.0; }
        runtime_register_builtins(&vm);
        register_core_modules(&vm);
        if (load_mods) register_world_modules(&vm);
        vm.load_embedded_mods = load_embedded_mods_impl;

        char *exe_path = get_self_path();
        Bytecode *embedded = bytecode_load_from_exe(exe_path);
        if (embedded) {
            vm_load_bytecode(&vm, embedded);
            if (vm.load_embedded_mods) vm.load_embedded_mods(&vm);
            vm_run(&vm);
            bytecode_free(embedded);
            free(embedded);
            free(exe_path);
            return 0;
        }
        free(exe_path);

        if (load_mods) mod_load_all(&vm, "mods");
        repl(&vm);
        return 0;
    }

    /* 鍒濆锟?VM 骞跺姞杞芥ā锟?*/
    VM vm; vm_init(&vm);
    if (g_lim_mem > 0) vm.limit_mem = g_lim_mem * 1024.0 * 1024.0;
    if (g_lim_vram > 0) vm.limit_vram = g_lim_vram * 1024.0 * 1024.0;
    if (g_lim_time > 0) vm.limit_time = g_lim_time;
    vm.safe_mode = safe_mode;
    if (timeout_set) vm.exec_timeout_ms = timeout_ms;  /* 0 = unlimited */
    runtime_register_builtins(&vm);
    register_core_modules(&vm);
    if (load_mods) register_world_modules(&vm);
    if (load_mods) mod_load_all(&vm, "mods");

    if (gui_mode) {
        /* --gui 妯″紡锛氶殣钘忔帶鍒跺彴鍚庢寜鏅€氭柟寮忚繍琛岃剼鏈紝
           window()/show_image()/gui_wait() �?gui 妯＄粍鎻愪緵锛堜富绾跨▼浜嬩欢寰幆�?*/
        const char *script_arg = argv[1];
        if (!script_arg) { fprintf(stderr, "(? %s --gui <script.im>\n", argv[0]); return 1; }
        char *abs = chdir_to_script_dir(script_arg);
        const char *read_path = abs ? abs : script_arg;
        if (headless_mode) {
            /* Report the port that is really listening, not the number that was
             * asked for: `--port 0` / `--http-port 0` mean "kernel, pick one",
             * and the suites read this line instead of guessing a number from a
             * pool they had to release before the child could bind it (the
             * window in which another suite took it).  On POSIX both servers
             * hand back the bound port; the winsock twins bind exactly what
             * they are given and expose no getter, so 0 is not supported
             * there.  A negative --http-port disables the HTTP API. */
            if (!headless_init(headless_port)) fprintf(stderr, "headless: bind %d failed\n", headless_port);
            else {
                headless_start_thread();
#if defined(_WIN32)
                fprintf(stderr, "headless: 127.0.0.1:%d\n", headless_port);
#else
                extern int headless_bound_port(void);
                int hl_bound = headless_bound_port();
                fprintf(stderr, "headless: 127.0.0.1:%d\n", hl_bound > 0 ? hl_bound : headless_port);
#endif
            }
            if (headless_http_port >= 0) {
                extern int verse_http_start(int);
                /* Say so when this fails.  Staying quiet produced a hub that
                 * prints "headless:" and runs its script but never serves
                 * HTTP: every later request just hangs until the caller's
                 * timeout, and the cause (usually EADDRINUSE from a port that
                 * was free a moment ago) was invisible.  A hub without its
                 * HTTP API is not a working hub, so this must be loud. */
                if (verse_http_start(headless_http_port)) {
#if defined(_WIN32)
                    fprintf(stderr, "http api: 127.0.0.1:%d\n", headless_http_port);
#else
                    extern int verse_http_bound_port(void);
                    int api_bound = verse_http_bound_port();
                    fprintf(stderr, "http api: 127.0.0.1:%d\n", api_bound > 0 ? api_bound : headless_http_port);
#endif
                }
                else fprintf(stderr, "http api: bind %d failed (port in use?)\n", headless_http_port);
            }
        }
        int rc_gui = load_and_run(&vm, read_path);
        free(abs);
        return rc_gui;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "debug") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s debug <script.im>\n", argv[0]); return 1; }
        if (vm.debug_script) {  /* legacy C debugger (debug_mod.dll) still installed */
            char *abs = chdir_to_script_dir(argv[2]);
            vm.debug_script(&vm, abs ? abs : argv[2]);
            free(abs);
            return 0;
        }
        /* .im debugger: splice mods/debug/main.im in front of the user script */
        char *dbg = read_file_alloc("mods/debug/main.im", NULL);
        if (!dbg) { fprintf(stderr, "debug: mods/debug/main.im not found (script debugger not installed)\n"); return 1; }
        char *user = read_file_alloc(argv[2], NULL);
        if (!user) { if (g_err_json) { main_err_json("io", argv[2], "check the debug script path"); return 1; } fprintf(stderr, "debug: cannot read script '%s'\n", argv[2]); return 1; }
        size_t dl = strlen(dbg), ul = strlen(user);
        char *combined = (char*)malloc(dl + ul + 2);
        memcpy(combined, dbg, dl);
        combined[dl] = '\n';
        memcpy(combined + dl + 1, user, ul);
        combined[dl + 1 + ul] = '\0';
        free(dbg);
        free(user);
        int rc = load_and_run_source(&vm, combined, argv[2]);
        free(combined);
        return rc;
    }

    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) { fprintf(stderr, "鐢ㄦ�? %s build <input.im> [output.exe]\n", argv[0]); return 1; }
        if (!vm.build_script) { fprintf(stderr, "鎵撳寘鍔熻兘鏈畨瑁咃紝璇峰姞锟?build 妯＄粍銆俓n"); return 1; }
        const char *input = argv[2];
        {
            size_t inl = strlen(input);
            if (inl > 8 && strcmp(input + inl - 8, ".imbuild") == 0) {
                char *absCfg = make_abs_path(input);
                int rc = build_project_impl(&vm, absCfg ? absCfg : input, -1, NULL);
                free(absCfg);
                return rc;
            }
        }

        const char *output = NULL;
        char auto_out[2048];
        if (argc >= 4) {
            output = argv[3];
        } else {
            /* 鑷姩杈撳嚭鍚嶏細鑴氭湰鍚岀洰褰曘€佸悓�?.exe */
            char *abs_in = make_abs_path(input);
            if (abs_in) {
                strip_ext_into(auto_out, sizeof(auto_out), abs_in);
                free(abs_in);
            } else {
                strncpy(auto_out, input, sizeof(auto_out) - 1);
                auto_out[sizeof(auto_out) - 1] = '\0';
            }
            strncat(auto_out, ".exe", sizeof(auto_out) - strlen(auto_out) - 1);
            output = auto_out;
        }
        vm.build_script(&vm, input, output);
        return 0;
    }

    /* buildc command: compile .im to .inim bytecode (legacy name, same pipeline
       as `compile`; supports --incremental/--force/--symbols) */
    if (strcmp(cmd, "buildc") == 0 || strcmp(cmd, "compile") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s %s <input.im> [output.inim] [--incremental|--force|--symbols]\n", argv[0], cmd); return 1; }
        const char *input = argv[2];
        /* imports resolve against the main script's directory: pin cwd there
           for the whole compile so emitted paths stay location-independent */
        /* --aot: package as a native executable (engine copy + embedded
           bytecode, self-executing).  The optimizing AOT backend remains
           experimental (roadmap §2.3). */
        if (aot_mode) {
            char aot_out[2048];
            char *abs_in_aot = make_abs_path_loose(input);
            if (!abs_in_aot) abs_in_aot = strdup(input);
            char *abs_script_aot = chdir_to_script_dir(input);
            if (argc >= 4) {
                char *expl = make_abs_path_loose(argv[3]);
                snprintf(aot_out, sizeof(aot_out), "%s", expl ? expl : argv[3]);
                free(expl);
            } else {
                snprintf(aot_out, sizeof(aot_out), "%s.exe", abs_in_aot);
            }
            int rc_aot = main_aot_package(abs_script_aot ? abs_script_aot : abs_in_aot, aot_out);
            if (rc_aot == 0) printf("aot: %s -> %s\n", input, aot_out);
            free(abs_in_aot);
            free(abs_script_aot);
            return rc_aot;
        }
        char *abs_in = make_abs_path_loose(input);
        if (!abs_in) abs_in = strdup(input);
        char *abs_out = (argc >= 4) ? make_abs_path_loose(argv[3]) : NULL; /* resolve before chdir */
        char *abs_script = chdir_to_script_dir(input);
        const char *output = NULL;
        char auto_out[2048];
        if (abs_out) {
            output = abs_out;
        } else {
            strip_ext_into(auto_out, sizeof(auto_out), abs_in);
            strncat(auto_out,
                    (strcmp(abi_target, "wasm") == 0 || strcmp(abi_target, "wasm32") == 0) ? ".wasm" : ".inim",
                    sizeof(auto_out) - strlen(auto_out) - 1);
            output = auto_out;
        }
        int rc = main_compile_cmd(abs_script ? abs_script : abs_in, output, abi_version, abi_target,
                                  emit_symbols, incremental_mode, force_build, reproducible_mode,
                                  debug_info_mode);
        free(abs_in);
        free(abs_out);
        free(abs_script);
        return rc;
    }

    /* run command: unified runtime executor (interpreted; AOT/wasm backends
       are not in the stable channel yet and fail explicitly) */
    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s run <script.im|script.inim> [args...]\n", argv[0]); return 1; }
        if (aot_mode) {
            fprintf(stderr, "error: `run --aot` is not supported; produce an executable with `compile --aot` first\n");
            return 2;
        }
        if (strcmp(abi_target, "wasm") == 0 || strcmp(abi_target, "wasm32") == 0) {
            fprintf(stderr, "error: wasm backend is not in the stable channel yet (v0.5 roadmap); run interpreted instead\n");
            return 2;
        }
        const char *script = argv[2];
        vm.argc = argc - 3;
        vm.argv = argv + 3;
        if (timeout_set) vm.exec_timeout_ms = timeout_ms;
        char *abs_script = chdir_to_script_dir(script);
        const char *read_path = abs_script ? abs_script : script;
        int rc_run = load_and_run(&vm, read_path);
        free(abs_script);
        return rc_run;
    }

    /* profile command: run with the function-level profiler, write <out>.prof */
    if (strcmp(cmd, "profile") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s profile <script.im> [output.prof]\n", argv[0]); return 1; }
        const char *script = argv[2];
        const char *output = NULL;
        char auto_out[2048];
        if (argc >= 4) {
            output = argv[3];
        } else {
            snprintf(auto_out, sizeof(auto_out), "%s.prof", script);
            output = auto_out;
        }
        vm.argc = argc - 3;
        vm.argv = argv + 3;
        if (timeout_set) vm.exec_timeout_ms = timeout_ms;
        prof_enable(&vm);
        char *abs_script = chdir_to_script_dir(script);
        const char *read_path = abs_script ? abs_script : script;
        int rc_run = load_and_run(&vm, read_path);
        free(abs_script);
        prof_finish(&vm, output);
        return rc_run;
    }

    /* bytecode command: dump the compiled instruction stream (self-host parity) */
    if (strcmp(cmd, "bytecode") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s bytecode <input.im>\n", argv[0]); return 1; }
        return main_dump_bytecode(argv[2]);
    }

    /* symbols command: export the symbol table of a compiled script */
    if (strcmp(cmd, "symbols") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s symbols <input.im> [output.symbols]\n", argv[0]); return 1; }
        const char *input = argv[2];
        char auto_out[2048];
        char *abs_in = make_abs_path_loose(input);
        if (!abs_in) abs_in = strdup(input);
        if (argc >= 4) {
            char *expl = make_abs_path_loose(argv[3]);
            snprintf(auto_out, sizeof(auto_out), "%s", expl ? expl : argv[3]);
            free(expl);
        } else {
            snprintf(auto_out, sizeof(auto_out), "%s.symbols", abs_in); /* resolved before chdir */
        }
        const char *output = auto_out;
        char *abs_script = chdir_to_script_dir(input); /* imports resolve against script dir */
        Program *prog = parse_program_file(abs_script ? abs_script : abs_in);
        if (!prog) { fprintf(stderr, "error: cannot read script '%s'\n", input); free(abs_in); free(abs_script); return 1; }
        Compiler *comp = compiler_new();
        comp->abi_version = (abi_version >= 0) ? abi_version : INIM_ABI_VERSION;
        comp->target = TARGET_HOST;
        compiler_compile(comp, prog);
        Bytecode *bc = compiler_get_main_bytecode(comp);
        int rc = main_write_symbols(bc, input, output, abi_version, abi_target);
        if (rc == 0) printf("exported: %s -> %s\n", input, output);
        compiler_free(comp);
        free(abs_in);
        free(abs_script);
        return rc;
    }

    /* default: run the script directly (right-click open); `run` handled above */
    const char *script = argv[1];
    if (!script) { usage(argv[0]); return 1; }

    /* command-line args passed to script (args() builtin); anything after the script path */
    vm.argc = argc - 2;
    vm.argv = argv + 2;
    if (timeout_ms > 0) vm.exec_timeout_ms = timeout_ms;

    if (g_lint) {
        char lb[16384];
        int ln = lint_check(script, lb, sizeof lb);
        if (ln > 0) fprintf(stderr, "%s", lb);
        return 0;
    }
    char *abs_script = chdir_to_script_dir(script);
    const char *read_path = abs_script ? abs_script : script;
    int rc_run = load_and_run(&vm, read_path);
    free(abs_script);
    return rc_run;
}
