/* Standalone driver for aot_native — the `translate` half of the route-A
 * pipeline:
 *
 *     aot-native translate prog.im prog.c
 *     cc -O2 prog.c -o prog.native
 *
 * It is a separate translation unit with its own main() so that it can be
 * linked against the engine's object files (minus src/main.c.o) and measured
 * BEFORE CMakeLists.txt is opened for this stream; the stream brief freezes
 * CMakeLists.txt in wave 1.  See tools/aot_native_build.sh.
 */
#include "aot_native.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The engine defines this in src/main.c, and src/main.c.o is precisely the one
 * object the build script must exclude (it owns the engine's own main()).  The
 * tool therefore carries its own copy.  POSIX branch only: the engine's
 * function additionally transcodes GBK to UTF-8 under _WIN32, which does not
 * apply here and must not be silently reimplemented. */
char *inim_load_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > (1 << 26)) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[rd] = '\0';
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 3 + 1);
    }
    return buf;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s translate [--extern NAME] [--entry NAME] <input.im> <output.c>\n"
            "       %s subset\n"
            "\n"
            "--extern NAME  declare the source global NAME as `extern` (the linking\n"
            "               harness defines it) and rename the entry point to\n"
            "               `nv_program_main` instead of `main`.\n"
            "               Without this the host compiler sees every input as a\n"
            "               constant, evaluates a constant workload at compile time,\n"
            "               and any speedup measured against the binary is fiction.\n"
            "               The harness must define `NV <cname>;` with the same NV\n"
            "               layout the generated file uses.\n"
            "--entry NAME   use NAME as the generated entry point (default main).\n"
            "\n"
            "subset: %s\n",
            argv0, argv0, aot_native_subset_description());
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "subset") == 0) {
        printf("%s\n", aot_native_subset_description());
        return 0;
    }
    if (argc < 2 || strcmp(argv[1], "translate") != 0) {
        usage(argv[0]);
        return 2;
    }

    const char *extern_name = NULL;
    const char *entry_name = "main";
    int i = 2;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--extern") == 0 && i + 1 < argc) {
            extern_name = argv[++i];
        } else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            entry_name = argv[++i];
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            break;
        }
    }
    /* --extern implies the harness owns the entry point too. */
    if (extern_name && strcmp(entry_name, "main") == 0) entry_name = "nv_program_main";

    if (argc - i != 2) {
        usage(argv[0]);
        return 2;
    }

    Program *prog = parse_program_file(argv[i]);
    if (!prog) {
        fprintf(stderr, "error: cannot parse '%s'\n", argv[i]);
        return 1;
    }
    if (!aot_native_translate_ex(prog, argv[i + 1], extern_name, entry_name)) {
        fprintf(stderr, "aot_native: %s\n", aot_native_last_error());
        return 1;
    }
    return 0;
}
