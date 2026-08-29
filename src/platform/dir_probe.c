#include "dir.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    ImDir *d = im_dir_open("src"); if (!d) return 2;
    char name[256]; int isdir = 0, seen = 0;
    while (im_dir_next_ex(d, name, sizeof name, &isdir) > 0) {
        if (strcmp(name, "platform") == 0) { seen = isdir; break; }
    }
    im_dir_close(d); if (!seen) return 3;
    puts("dir probe: ok"); return 0;
}
