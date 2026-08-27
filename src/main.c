#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
extern int build_project_impl(void *vm, const char *cfgPath, int mode, const char *outExe);
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#define _chdir chdir
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
#include "platform/platform.h"

void gui_mod_register(VM *vm);
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
#include "runtime.h"
#include "mod.h"
#include "bytecode.h"
#include "common.h"


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
    printf("  %s run <script.im>\n", prog);
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
        snprintf(outPath, sizeof outPath, "%s\\%s", outDir, name);
        for (char *p = outPath; *p; p++) if (*p == '/') *p = '\\';
        char *slash = strrchr(outPath, '\\');
        if (slash) { *slash = 0; jar_mkdir_p(outPath); *slash = '\\'; }
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
        if (pf) { fclose(pf); vm_params_load(vm, params_path); }
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
        if (pf) { fclose(pf); vm_params_load(vm, params_path); }
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
        if (pf) { fclose(pf); vm_params_load(vm, params_path); }
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
        *slash = '\\';
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
        const char *caps[] = { "threads", "fiber", "posix_fs", "native_dll", "gui", NULL };
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
    int headless_mode = 0;
    int headless_port = 11440;
int headless_http_port = 11470;
    if (argc >= 2 && strcmp(argv[1], "--gui") == 0) {
        gui_mode = 1;
        for (int i = 1; i < argc - 1; i++) argv[i] = argv[i + 1];
        argc--;
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

    char *self = get_self_path();
    if (self) {
        char *p = strrchr(self, '\\');
        if (!p) p = strrchr(self, '/');
        if (p) { *p = '\0'; _chdir(self); }
        free(self);
    }

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
if (argc == 1) {
        VM vm; vm_init(&vm);
    if (g_lim_mem > 0) vm.limit_mem = g_lim_mem * 1024.0 * 1024.0;
    if (g_lim_vram > 0) vm.limit_vram = g_lim_vram * 1024.0 * 1024.0;
    if (g_lim_time > 0) vm.limit_time = g_lim_time;
    vm.safe_mode = safe_mode;
    if (g_gc_on) { vm.gc_enabled =1; if (vm.gc_threshold <=0) vm.gc_threshold =2.0 *1024.0 *1024.0; }
        vm.safe_mode = safe_mode;
        runtime_register_builtins(&vm);
    isolate_mod_register(&vm);
    lint_mod_register(&vm);
    vm_debug_builtins_register(&vm);
    gui_mod_register(&vm);
    io_mod_register(&vm);
    net_mod_register(&vm);
    json_mod_register(&vm);
    infiverse_mod_register(&vm);
    verse_dist_mod_register(&vm);
    server_mod_register(&vm);
    say_mod_register(&vm);
identity_mod_register(&vm);
social_mod_register(&vm);
ai_mod_register(&vm);
    record_mod_register(&vm);
    build_mod_register(&vm);
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

        mod_load_all(&vm, "mods");
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
    isolate_mod_register(&vm);
    lint_mod_register(&vm);
    vm_debug_builtins_register(&vm);
    gui_mod_register(&vm);
    io_mod_register(&vm);
    net_mod_register(&vm);
    json_mod_register(&vm);
    infiverse_mod_register(&vm);
    verse_dist_mod_register(&vm);
    server_mod_register(&vm);
    say_mod_register(&vm);
identity_mod_register(&vm);
social_mod_register(&vm);
ai_mod_register(&vm);
    record_mod_register(&vm);
    build_mod_register(&vm);
    mod_load_all(&vm, "mods");

    if (gui_mode) {
        /* --gui 妯″紡锛氶殣钘忔帶鍒跺彴鍚庢寜鏅€氭柟寮忚繍琛岃剼鏈紝
           window()/show_image()/gui_wait() �?gui 妯＄粍鎻愪緵锛堜富绾跨▼浜嬩欢寰幆�?*/
        const char *script_arg = argv[1];
        if (!script_arg) { fprintf(stderr, "(? %s --gui <script.im>\n", argv[0]); return 1; }
        char *abs = chdir_to_script_dir(script_arg);
        const char *read_path = abs ? abs : script_arg;
        if (headless_mode) {
            if (!headless_init(headless_port)) fprintf(stderr, "headless: bind %d failed\n", headless_port);
            else {
                headless_start_thread();
                fprintf(stderr, "headless: 127.0.0.1:%d\n", headless_port);
            }
            if (headless_http_port > 0) {
                extern int verse_http_start(int);
                if (verse_http_start(headless_http_port)) fprintf(stderr, "http api: 127.0.0.1:%d\n", headless_http_port);
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

    if (strcmp(cmd, "buildc") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: %s buildc <input.im> [output.inim]\n", argv[0]); return 1; }
        const char *input = argv[2];
        const char *output = NULL;
        char auto_out[2048];
        if (argc >= 4) {
            output = argv[3];
        } else {
            char *abs_in = make_abs_path(input);
            if (abs_in) {
                strip_ext_into(auto_out, sizeof(auto_out), abs_in);
                free(abs_in);
            } else {
                strncpy(auto_out, input, sizeof(auto_out) - 1);
                auto_out[sizeof(auto_out) - 1] = '\0';
            }
            strncat(auto_out, ".inim", sizeof(auto_out) - strlen(auto_out) - 1);
            output = auto_out;
        }
        Program *prog = parse_program_file(input);
        if (!prog) { fprintf(stderr, "error: cannot read script '%s'\n", input); return 1; }
        Compiler *comp = compiler_new();
        compiler_compile(comp, prog);
        Bytecode *bc = compiler_get_main_bytecode(comp);
        int rc = bytecode_write_file(output, bc);
        compiler_free(comp);
        if (rc != 0) { fprintf(stderr, "error: write '%s' failed\n", output); return 1; }
        printf("compiled: %s -> %s\n", input, output);
        return 0;
    }
    const char *script = (strcmp(cmd, "run") == 0) ? argv[2] : argv[1];
    if (!script) { usage(argv[0]); return 1; }

    /* command-line args passed to script (args() builtin); anything after the script path */
    int script_idx = (strcmp(cmd, "run") == 0) ? 2 : 1;

    vm.argc = argc - script_idx - 1;
    vm.argv = argv + script_idx + 1;
    if (timeout_ms > 0) vm.exec_timeout_ms = timeout_ms;

    /* fix right-click open: chdir to script dir, read by absolute path */
    /* .inim: precompiled bytecode - skip lexer/parser/compiler entirely (unified loader) */
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
