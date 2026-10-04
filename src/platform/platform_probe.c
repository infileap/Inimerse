#include "platform.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    char path[4096] = {0};
    printf("platform_time_ms=%llu\n", (unsigned long long)im_platform_now_ms());
    printf("platform_executable=%s\n", im_platform_executable_path(path, sizeof(path)) >= 0 ? path : "unknown");
    if (im_platform_now_ms() == 0) return 3;
    if (im_platform_path_join(path, sizeof(path), "/tmp", "inimerse") != 0) return 4;
    if (im_platform_has_capability("definitely_missing_capability")) return 5;
    /* A join that does not fit refuses instead of truncating: a silently
       truncated path writes the file to the wrong place, whereas a refused join
       is a skipped entry.  Callers must treat -1 as "do not proceed".
       See docs/AUDIT.md 1.22. */
    char tiny[8];
    if (im_platform_path_join(tiny, sizeof tiny, "/tmp", "inimerse") != -1) return 6;
    if (im_platform_path_join(NULL, sizeof tiny, "/tmp", "inimerse") != -1) return 7;
    if (im_platform_path_join(tiny, 0, "/tmp", "inimerse") != -1) return 8;
    /* Redundant separators are normalised, so "a/" + "/b" and "a" + "b" agree. */
    char a[64], b[64];
    if (im_platform_path_join(a, sizeof a, "/tmp/", "/inimerse") != 0) return 9;
    if (im_platform_path_join(b, sizeof b, "/tmp", "inimerse") != 0) return 10;
    if (strcmp(a, b) != 0) return 11;
    return 0;
}
