#include "platform.h"
#include <stdio.h>

int main(void) {
    char path[4096] = {0};
    printf("platform_time_ms=%llu\n", (unsigned long long)im_platform_now_ms());
    printf("platform_executable=%s\n", im_platform_executable_path(path, sizeof(path)) >= 0 ? path : "unknown");
    return 0;
}
