/* client.c - inim-client: drives an inim-server child over two pipes (P1 ②)
 *
 * Usage: inim-client [--server <path>] <root> <verse_id> [scenario_file]
 *
 * Reads one canonical-JSON request per line from the scenario file (stdin by
 * default), sends each to the server child on its stdin, prints the matching
 * response line to stdout.  Lines starting with '#' are directives:
 *     #crash   kill the server process abruptly (SIGKILL) and exit 137
 * The client is a separate process from the server: the only channel between
 * them is the protocol byte stream.
 *
 * Spawning a child needs fork+exec, so this program is POSIX-only for now; on
 * other platforms it says so instead of pretending to work.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
int main(void) {
    fprintf(stderr, "inim-client: process spawning is not implemented on this platform\n");
    return 4;
}
#else

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define VL_LINE_MAX (64 * 1024)

static pid_t g_child = -1;

static int slurp(FILE *in, char ***out_lines) {
    size_t cap = 16, n = 0;
    char **lines = (char **)malloc(cap * sizeof *lines);
    if (!lines) return -1;
    char buf[VL_LINE_MAX];
    while (fgets(buf, sizeof buf, in)) {
        size_t len = strlen(buf);
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
        if (len == 0) continue;
        if (n == cap) {
            cap *= 2;
            char **tmp = (char **)realloc(lines, cap * sizeof *lines);
            if (!tmp) { free(lines); return -1; }
            lines = tmp;
        }
        lines[n++] = strdup(buf);
    }
    *out_lines = lines;
    return (int)n;
}

int main(int argc, char **argv) {
    const char *server = getenv("INIM_SERVER_BIN");
    int argi = 1;

    if (argc > argi + 1 && strcmp(argv[argi], "--server") == 0) {
        server = argv[argi + 1];
        argi += 2;
    }
    if (argc < argi + 2) {
        fprintf(stderr, "usage: inim-client [--server <path>] <root> <verse_id> [scenario]\n");
        return 64;
    }
    const char *root = argv[argi];
    const char *verse_id = argv[argi + 1];
    const char *scenario = (argc > argi + 2) ? argv[argi + 2] : NULL;

    if (!server || !*server) server = "inim-server";

    FILE *in = stdin;
    if (scenario) {
        in = fopen(scenario, "r");
        if (!in) { fprintf(stderr, "inim-client: cannot read %s\n", scenario); return 66; }
    }

    char **lines = NULL;
    int nlines = slurp(in, &lines);
    if (scenario) fclose(in);
    if (nlines < 0) { fprintf(stderr, "inim-client: out of memory\n"); return 70; }

    /* the child inherits its half of two pipes: we write to child_in[1] and
     * read from child_out[0]. */
    int to_child[2], from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        fprintf(stderr, "inim-client: pipe failed\n");
        return 70;
    }

    g_child = fork();
    if (g_child < 0) {
        fprintf(stderr, "inim-client: fork failed\n");
        return 70;
    }
    if (g_child == 0) {
        /* child: stdin <- to_child read end, stdout -> from_child write end */
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        execlp(server, server, root, verse_id, (char *)NULL);
        fprintf(stderr, "inim-client: cannot exec '%s'\n", server);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    FILE *child_in = fdopen(to_child[1], "w");
    FILE *child_out = fdopen(from_child[0], "r");
    if (!child_in || !child_out) { fprintf(stderr, "inim-client: fdopen failed\n"); return 70; }
    setvbuf(child_in, NULL, _IOLBF, 0);

    int status = 0;
    for (int i = 0; i < nlines; i++) {
        if (lines[i][0] == '#') {
            if (strncmp(lines[i], "#crash", 6) == 0) {
                fflush(child_in);
                kill(g_child, SIGKILL);
                waitpid(g_child, NULL, 0);
                fprintf(stdout, "#crash: server killed\n");
                fflush(stdout);
                return 137;
            }
            continue;
        }
        fprintf(child_in, "%s\n", lines[i]);
        fflush(child_in);

        char resp[VL_LINE_MAX];
        if (!fgets(resp, sizeof resp, child_out)) {
            fprintf(stderr, "inim-client: server closed the stream early\n");
            status = 141;
            break;
        }
        size_t len = strlen(resp);
        while (len && (resp[len - 1] == '\n' || resp[len - 1] == '\r')) resp[--len] = '\0';
        printf("%s\n", resp);
        fflush(stdout);
    }

    fclose(child_in);
    int wst = 0;
    waitpid(g_child, &wst, 0);
    if (WIFEXITED(wst) && WEXITSTATUS(wst) != 0 && status == 0) status = WEXITSTATUS(wst);

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return status;
}
#endif
