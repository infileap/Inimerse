/* deps.c - .inim dependency trailer for incremental compilation */
#include "deps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(unsigned char b[4], unsigned v) { b[0] = v & 0xFF; b[1] = (v >> 8) & 0xFF; b[2] = (v >> 16) & 0xFF; b[3] = (v >> 24) & 0xFF; }
static unsigned rd32(const unsigned char b[4]) { return (unsigned)b[0] | ((unsigned)b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24); }

int deps_write_trailer(const char *bc_path, const DepEntry *deps, int count, int abi_version) {
    if (count < 0) count = 0;
    /* trailer_len = entries + footer(16) */
    long entries_len = 0;
    for (int i = 0; i < count; i++)
        entries_len += 4 + (long)strlen(deps[i].path) + 64;
    long trailer_len = entries_len + 16;

    FILE *f = fopen(bc_path, "ab");
    if (!f) return -1;
    for (int i = 0; i < count; i++) {
        unsigned char b[4];
        unsigned plen = (unsigned)strlen(deps[i].path);
        le32(b, plen);
        fwrite(b, 1, 4, f);
        fwrite(deps[i].path, 1, plen, f);
        fwrite(deps[i].sha_hex, 1, 64, f);
    }
    unsigned char b[4];
    le32(b, DEPS_TRAILER_MAGIC); fwrite(b, 1, 4, f);
    le32(b, (unsigned)abi_version); fwrite(b, 1, 4, f);
    le32(b, (unsigned)count); fwrite(b, 1, 4, f);
    le32(b, (unsigned)trailer_len); fwrite(b, 1, 4, f);
    fclose(f);
    return 0;
}

int deps_read(const char *bc_path, DepEntry **out_deps, int *out_count, int *out_abi_version) {
    *out_deps = NULL; *out_count = 0; *out_abi_version = 0;
    FILE *f = fopen(bc_path, "rb");
    if (!f) return -1;
    unsigned char b[4];
    if (fseek(f, -16, SEEK_END) != 0 || fread(b, 1, 4, f) != 4 || rd32(b) != DEPS_TRAILER_MAGIC) { fclose(f); return -1; }
    if (fread(b, 1, 4, f) != 4) { fclose(f); return -1; }
    unsigned abi = rd32(b);
    if (fread(b, 1, 4, f) != 4) { fclose(f); return -1; }
    unsigned count = rd32(b);
    if (fread(b, 1, 4, f) != 4) { fclose(f); return -1; }
    unsigned trailer_len = rd32(b);
    long fsize_end = ftell(f);
    if (count > 65536) { fclose(f); return -1; }
    long start = fsize_end - (long)trailer_len; /* trailer_len includes the 16-byte footer */
    if (start < 0 || fseek(f, start, SEEK_SET) != 0) { fclose(f); return -1; }

    DepEntry *deps = count ? (DepEntry*)calloc(count, sizeof(DepEntry)) : NULL;
    for (unsigned i = 0; i < count; i++) {
        unsigned plen;
        if (fread(b, 1, 4, f) != 4) goto fail;
        plen = rd32(b);
        if (plen > 4096) goto fail;
        deps[i].path = (char*)malloc(plen + 1);
        if (fread(deps[i].path, 1, plen, f) != plen) goto fail;
        deps[i].path[plen] = '\0';
        if (fread(deps[i].sha_hex, 1, 64, f) != 64) goto fail;
        deps[i].sha_hex[64] = '\0';
    }
    fclose(f);
    *out_deps = deps;
    *out_count = (int)count;
    *out_abi_version = (int)abi;
    return 0;
fail:
    fclose(f);
    deps_free(deps, (int)count);
    return -1;
}

void deps_free(DepEntry *deps, int count) {
    if (!deps) return;
    for (int i = 0; i < count; i++) free(deps[i].path);
    free(deps);
}

void deps_bc_dirname(const char *bc_path, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", bc_path);
    char *slash = strrchr(out, '/');
#ifdef _WIN32
    char *bslash = strrchr(out, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (!slash) { snprintf(out, out_sz, "."); return; }
    if (slash == out) out[1] = '\0'; /* root "/" */
    else *slash = '\0';
}

/* Walk both path component lists; common prefix is dropped, one ".." per
 * remaining from_dir component, then the target's remaining components. */
void deps_relative_path(const char *from_dir, const char *abs_target, char *out, size_t out_sz) {
    out[0] = '\0';
    if (!from_dir || !abs_target) return;
    char a[2048], b[2048];
    snprintf(a, sizeof(a), "%s", from_dir);
    snprintf(b, sizeof(b), "%s", abs_target);
    /* strip trailing slashes */
    size_t al = strlen(a); while (al > 1 && a[al-1] == '/') a[--al] = '\0';
    size_t bl = strlen(b); while (bl > 1 && b[bl-1] == '/') b[--bl] = '\0';
    char *ac[256], *bc[256]; int an = 0, bn = 0;
    for (char *t = strtok(a, "/"); t && an < 256; t = strtok(NULL, "/")) ac[an++] = t;
    for (char *t = strtok(b, "/"); t && bn < 256; t = strtok(NULL, "/")) bc[bn++] = t;
    int common = 0;
    while (common < an && common < bn && strcmp(ac[common], bc[common]) == 0) common++;
    size_t used = 0;
    for (int i = common; i < an; i++) {
        used += (size_t)snprintf(out + used, out_sz - used, "%s..", used ? "/" : "");
        if (used >= out_sz) return;
    }
    for (int i = common; i < bn; i++) {
        used += (size_t)snprintf(out + used, out_sz - used, "%s%s", used ? "/" : "", bc[i]);
        if (used >= out_sz) return;
    }
    if (!used) snprintf(out, out_sz, ".");
}

void deps_entry_abs_path(const DepEntry *dep, const char *bc_path, char *out, size_t out_sz) {
    char dir[2048];
    deps_bc_dirname(bc_path, dir, sizeof(dir));
    if (dep->path[0] == '/') { snprintf(out, out_sz, "%s", dep->path); return; }
#ifdef _WIN32
    if ((dep->path[0] >= 'A' && dep->path[0] <= 'Z') || (dep->path[0] >= 'a' && dep->path[0] <= 'z')) {
        if (dep->path[1] == ':') { snprintf(out, out_sz, "%s", dep->path); return; }
    }
#endif
    snprintf(out, out_sz, "%s/%s", dir, dep->path);
}
