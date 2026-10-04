#include "vfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* A VFS path may never escape its mount prefix, and that decision must not
 * depend on what the caller's buffer happened to hold.  The ".." branch used
 * strchr(out, '/') while out[w] was still unterminated, so it searched past
 * the path into uninitialized bytes: on this tree "os:/../escape" normalized
 * successfully, i.e. the escape was allowed.  Every check below is
 * deterministic once the search is bounded by w, and the probe is registered
 * as a test so the gate can see it. */
int main(void) {
    char norm[128];
    if (im_vfs_normalize("os:\\README.md", norm, sizeof norm) != 0 || strcmp(norm, "os:/README.md") != 0) return 1;
    if (im_vfs_normalize("os:/../escape", norm, sizeof norm) == 0) return 2;
    if (im_vfs_normalize("os:/a/../../escape", norm, sizeof norm) == 0) return 5;
    if (im_vfs_normalize("os:/a/./b", norm, sizeof norm) != 0 || strcmp(norm, "os:/a/b") != 0) return 6;
    /* A ".." that stays inside the prefix is still allowed. */
    if (im_vfs_normalize("os:/a/b/..", norm, sizeof norm) != 0 || strcmp(norm, "os:/a") != 0) return 7;
    ImVfs *v = im_vfs_create(); if (!v || im_vfs_mount_os(v, "os:", ".") != 0) return 3;
    char *data = NULL; size_t len = 0;
    if (im_vfs_read_file(v, "os:/README.md", &data, &len) != 0 || !data || len == 0) return 4;
    free(data); data = NULL; len = 0;
    /* The refusal has to hold on the read path too, not just in normalize. */
    if (im_vfs_read_file(v, "os:/../README.md", &data, &len) == 0) { free(data); im_vfs_destroy(v); return 8; }
    im_vfs_destroy(v); puts("vfs platform smoke"); return 0;
}
