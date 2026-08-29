#include "platform/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { char prefix[32]; char root[512]; } VfsMount;
struct ImVfs { VfsMount mounts[8]; int count; };

ImVfs *im_vfs_create(void) { return (ImVfs*)calloc(1, sizeof(ImVfs)); }
void im_vfs_destroy(ImVfs *vfs) { free(vfs); }

int im_vfs_normalize(const char *path, char *out, size_t cap) {
    if (!path || !out || cap < 2 || path[0] == '\0') return -1;
    size_t w = 0; int dot = 0;
    for (const char *p = path; *p; p++) {
        char c = *p == '\\' ? '/' : *p;
        if (c == '/' && w && out[w-1] == '/') continue;
        if (c == '.' && (p == path || p[-1] == '/')) { dot = 1; continue; }
        if (dot) { if (c == '/' && w && out[w-1] == '.') { out[--w] = '\0'; dot = 0; continue; } dot = 0; }
        if (w + 1 >= cap) return -1;
        out[w++] = c;
    }
    while (w > 1 && out[w-1] == '/') w--;
    out[w] = '\0';
    if (strstr(out, "../") || (w >= 2 && strcmp(out + w - 2, "..") == 0)) return -1;
    return 0;
}

int im_vfs_mount_os(ImVfs *vfs, const char *prefix, const char *root) {
    if (!vfs || !prefix || !root || !prefix[0] || vfs->count >= 8) return -1;
    VfsMount *m = &vfs->mounts[vfs->count++];
    snprintf(m->prefix, sizeof(m->prefix), "%s", prefix);
    snprintf(m->root, sizeof(m->root), "%s", root);
    return 0;
}

static int resolve(ImVfs *vfs, const char *path, char *dst, size_t cap) {
    char norm[1024]; if (im_vfs_normalize(path, norm, sizeof(norm)) != 0) return -1;
    for (int i = 0; i < vfs->count; i++) {
        VfsMount *m = &vfs->mounts[i]; size_t n = strlen(m->prefix);
        if (strncmp(norm, m->prefix, n) == 0 && (norm[n] == '/' || norm[n] == '\0')) {
            const char *rel = norm[n] == '/' ? norm + n + 1 : norm + n;
            if (snprintf(dst, cap, "%s/%s", m->root, rel) >= (int)cap) return -1;
            return 0;
        }
    }
    return -1;
}

int im_vfs_read_file(ImVfs *vfs, const char *path, char **out_data, size_t *out_len) {
    if (!vfs || !out_data) return -1; char full[1536];
    if (resolve(vfs, path, full, sizeof(full)) != 0) return -1;
    FILE *f = fopen(full, "rb"); if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f); if (n < 0 || n > (1L << 30)) { fclose(f); return -1; }
    rewind(f); char *buf = (char*)malloc((size_t)n + 1); if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f); if (got != (size_t)n) { free(buf); return -1; }
    buf[got] = '\0'; *out_data = buf; if (out_len) *out_len = got; return 0;
}

int im_vfs_write_file(ImVfs *vfs, const char *path, const void *data, size_t len) {
    if (!vfs || !data) return -1; char full[1536];
    if (resolve(vfs, path, full, sizeof(full)) != 0) return -1;
    FILE *f = fopen(full, "wb"); if (!f) return -1;
    size_t n = fwrite(data, 1, len, f); int ok = (n == len && fclose(f) == 0); if (!ok) fclose(f); return ok ? 0 : -1;
}
