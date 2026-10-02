/* Standalone driver for aot_native — the `translate` half of the route-A
 * pipeline:
 *
 *     aot-native translate prog.im prog.c
 *     cc -O2 prog.c -o prog.native
 *
 * It is a separate translation unit with its own main() so that it can be
 * linked against the engine's object files and measured independently of the
 * CLI.  CMakeLists.txt builds it as the normal `aot-native` target.
 */
#include "aot_native.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* inim_load_text() is engine code (src/common/common.c, declared in
 * src/common/common.h) and this tool links the engine library, so it must NOT
 * define its own.  It used to carry a POSIX-only copy, because the only
 * definition lived in src/main.c and src/main.c.o was the one object the old
 * build script had to exclude; that copy also silently dropped the GBK(cp936)
 * transcode the engine performs under _WIN32, so Windows readers of a legacy
 * GBK script got different bytes here than from the engine.  Both problems are
 * gone with the copy. */

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
