#include "vfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    char norm[128];
    if (im_vfs_normalize("os:\\README.md", norm, sizeof norm) != 0 || strcmp(norm, "os:/README.md") != 0) return 1;
    if (im_vfs_normalize("os:/../escape", norm, sizeof norm) == 0) return 2;
    ImVfs *v = im_vfs_create(); if (!v || im_vfs_mount_os(v, "os:", ".") != 0) return 3;
    char *data = NULL; size_t len = 0;
    if (im_vfs_read_file(v, "os:/README.md", &data, &len) != 0 || !data || len == 0) return 4;
    free(data); im_vfs_destroy(v); puts("vfs platform smoke"); return 0;
}
