#include "mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static char *json_get_string(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return NULL;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return NULL;
    size_t len = end - p;
    char *val = malloc(len + 1);
    memcpy(val, p, len);
    val[len] = '\0';
    return val;
}

static void load_mod(VM *vm, const char *mod_path, const char *mod_name) {
    (void)mod_name;
    char st_path[2048];
    snprintf(st_path, sizeof(st_path), "%s\\mod.st", mod_path);

    FILE *f = fopen(st_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = malloc(len + 1);
    fread(json, 1, len, f);
    json[len] = '\0';
    fclose(f);

    char *native = json_get_string(json, "native_lib");
    char *script = json_get_string(json, "script");
    free(json);

    /* .im module: run the script as a one-shot initializer (bytecode kept alive) */
    if (script) {
        char sc_path[2048];
        snprintf(sc_path, sizeof(sc_path), "%s\\%s", mod_path, script);
        vm_exec_script_file(vm, sc_path);
        free(script);
    }

    if (native) {
        char dll_path[2048];
        snprintf(dll_path, sizeof(dll_path), "%s\\%s", mod_path, native);

        HMODULE h = LoadLibrary(dll_path);
        if (h) {
            typedef void (*InitFunc)(VM*);
            InitFunc init = (InitFunc)GetProcAddress(h, "mod_init");
            if (init) init(vm);
        }
        free(native);
    }
}

void mod_load_all(VM *vm, const char *mod_dir) {
    WIN32_FIND_DATA fd;
    char sp[1024];
    snprintf(sp, sizeof(sp), "%s\\*", mod_dir);
    HANDLE hFind = FindFirstFile(sp, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char mod_path[2048];
        snprintf(mod_path, sizeof(mod_path), "%s\\%s", mod_dir, fd.cFileName);
        load_mod(vm, mod_path, fd.cFileName);
    } while (FindNextFile(hFind, &fd));
    FindClose(hFind);
}

void mod_load_by_name(VM *vm, const char *mod_dir, const char *name) {
    if (!mod_dir || !name) return;
    char mod_path[2048];
    snprintf(mod_path, sizeof(mod_path), "%s\\%s", mod_dir, name);
    load_mod(vm, mod_path, name);
}

#pragma GCC diagnostic pop
