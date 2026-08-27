#include "platform.h"
#include <stdio.h>

int main(void) {
    char path[4096] = {0};
    printf("platform_time_ms=%llu\n", (unsigned long long)im_platform_now_ms());
    printf("platform_executable=%s\n", im_platform_executable_path(path, sizeof(path)) >= 0 ? path : "unknown");
    if (im_platform_now_ms() == 0) return 3;
    if (im_platform_path_join(path, sizeof(path), "/tmp", "inimerse") != 0) return 4;
    if (im_platform_has_capability("definitely_missing_capability")) return 5;
    return 0;
}
