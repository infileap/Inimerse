/* client.c - inim-client: drives an inim-server child over two pipes (P1 ②)
 *
 * Usage: inim-client [--server <path>] <root> <verse_id> [scenario_file]
 *
 * Reads one canonical-JSON request per line from the scenario file (stdin by
 * default), sends each to the server child on its stdin, prints the matching
 * response line to stdout.  Lines starting with '#' are directives:
 *     #crash   kill the server process abruptly and exit 137
 * The client is a separate process from the server: the only channel between
 * them is the protocol byte stream.
 *
 * The child is spawned with a request pipe and a response pipe on both
 * platforms -- fork + dup2 + exec on POSIX, CreateProcess with an inheritable
 * pipe pair on Windows -- and everything above that (argument parsing, the
 * request loop, the crash directive, the exit code) is shared, so the two
 * platforms cannot drift apart in what they send or in what they print.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VL_LINE_MAX (64 * 1024)

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

/* ------------------------------------------------------- the child process
 *
 * Three operations, one implementation per platform: spawn it with the two
 * pipes, kill it abruptly, and reap it.  Nothing else in this file is
 * platform-specific.
 */

#if defined(_WIN32)

#include <stdint.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

typedef struct Child {
    HANDLE proc;        /* child process handle, or NULL when not spawned */
    DWORD  exit_code;
    int    has_exit_code;
} Child;

/* Append one argv element to a Windows command line.  CreateProcess parses it
 * back with the CommandLineToArgv rules: wrap in quotes, and double every run
 * of backslashes that would otherwise escape the closing quote -- otherwise a
 * path ending in '\' swallows it. */
static void cmdline_append(char *buf, size_t cap, size_t *len, const char *arg) {
    size_t bs = 0;
    if (*len && *len + 1 < cap) buf[(*len)++] = ' ';   /* separate from the previous one */
    if (*len + 1 < cap) buf[(*len)++] = '"';
    for (const char *p = arg; *p; p++) {
        if (*p == '\\') { bs++; continue; }
        size_t want = (*p == '"') ? bs * 2 + 1 : bs;
        while (want--) if (*len + 1 < cap) buf[(*len)++] = '\\';
        bs = 0;
        if (*len + 1 < cap) buf[(*len)++] = *p;
    }
    for (size_t k = 0; k < bs * 2; k++) if (*len + 1 < cap) buf[(*len)++] = '\\';
    if (*len + 1 < cap) buf[(*len)++] = '"';
    buf[*len] = '\0';
}

static int child_spawn(Child *c, const char *server, const char *root, const char *verse_id,
                       FILE **to_child, FILE **from_child) {
    c->proc = NULL;
    c->has_exit_code = 0;

    /* The two pipe pairs are inheritable; the parent's own ends are not, or
     * the child would hold its own stdin open and never see EOF. */
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE req_r = NULL, req_w = NULL, res_r = NULL, res_w = NULL;
    if (!CreatePipe(&req_r, &req_w, &sa, 0)) { fprintf(stderr, "inim-client: CreatePipe failed\n"); return -1; }
    if (!CreatePipe(&res_r, &res_w, &sa, 0)) {
        fprintf(stderr, "inim-client: CreatePipe failed\n");
        CloseHandle(req_r); CloseHandle(req_w);
        return -1;
    }
    SetHandleInformation(req_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(res_r, HANDLE_FLAG_INHERIT, 0);

    char cmd[VL_LINE_MAX];
    size_t len = 0;
    cmd[0] = '\0';
    cmdline_append(cmd, sizeof cmd, &len, server);
    cmdline_append(cmd, sizeof cmd, &len, root);
    cmdline_append(cmd, sizeof cmd, &len, verse_id);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = req_r;
    si.hStdOutput = res_w;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    /* lpApplicationName is NULL so the loader resolves the server name the way
     * execlp does on POSIX: the application directory, then the cwd, then
     * PATH, appending ".exe". */
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(req_r);
    CloseHandle(res_w);
    if (!ok) {
        fprintf(stderr, "inim-client: cannot exec '%s' (error %lu)\n", server, GetLastError());
        CloseHandle(req_w); CloseHandle(res_r);
        return -1;
    }
    CloseHandle(pi.hThread);
    c->proc = pi.hProcess;

    /* Binary mode on both ends: the request bytes are then identical to what
     * the POSIX half writes, and a '\r' the child's own text-mode stdout adds
     * is already stripped by both readers (client.c:74, server.c:56). */
    int fd_in = _open_osfhandle((intptr_t)req_w, _O_WRONLY | _O_BINARY);
    int fd_out = _open_osfhandle((intptr_t)res_r, _O_RDONLY | _O_BINARY);
    if (fd_in == -1 || fd_out == -1) {
        fprintf(stderr, "inim-client: _open_osfhandle failed\n");
        return -1;
    }
    *to_child = _fdopen(fd_in, "wb");
    *from_child = _fdopen(fd_out, "rb");
    if (!*to_child || !*from_child) { fprintf(stderr, "inim-client: _fdopen failed\n"); return -1; }
    return 0;
}

static int child_kill(Child *c) {
    if (!c->proc) return -1;
    return TerminateProcess(c->proc, 1) ? 0 : -1;
}

static int child_wait(Child *c, int *exit_code) {
    if (!c->proc) return -1;
    if (WaitForSingleObject(c->proc, INFINITE) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    if (!GetExitCodeProcess(c->proc, &code)) return -1;
    c->exit_code = code;
    c->has_exit_code = 1;
    if (exit_code) *exit_code = (int)code;
    return 0;
}

static void child_close(Child *c) {
    if (c->proc) { CloseHandle(c->proc); c->proc = NULL; }
}

#else /* POSIX */

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Child {
    pid_t pid;
} Child;

static int child_spawn(Child *c, const char *server, const char *root, const char *verse_id,
                       FILE **to_child, FILE **from_child) {
    c->pid = -1;

    /* the child inherits its half of two pipes: we write to child_in[1] and
     * read from child_out[0]. */
    int to_child_fd[2], from_child_fd[2];
    if (pipe(to_child_fd) != 0 || pipe(from_child_fd) != 0) {
        fprintf(stderr, "inim-client: pipe failed\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "inim-client: fork failed\n");
        return -1;
    }
    if (pid == 0) {
        /* child: stdin <- to_child read end, stdout -> from_child write end */
        dup2(to_child_fd[0], STDIN_FILENO);
        dup2(from_child_fd[1], STDOUT_FILENO);
        close(to_child_fd[0]); close(to_child_fd[1]);
        close(from_child_fd[0]); close(from_child_fd[1]);
        execlp(server, server, root, verse_id, (char *)NULL);
        fprintf(stderr, "inim-client: cannot exec '%s'\n", server);
        _exit(127);
    }
    c->pid = pid;

    close(to_child_fd[0]);
    close(from_child_fd[1]);
    *to_child = fdopen(to_child_fd[1], "w");
    *from_child = fdopen(from_child_fd[0], "r");
    if (!*to_child || !*from_child) { fprintf(stderr, "inim-client: fdopen failed\n"); return -1; }
    return 0;
}

static int child_kill(Child *c) {
    if (c->pid <= 0) return -1;
    return kill(c->pid, SIGKILL) == 0 ? 0 : -1;
}

static int child_wait(Child *c, int *exit_code) {
    if (c->pid <= 0) return -1;
    int wst = 0;
    if (waitpid(c->pid, &wst, 0) < 0) return -1;
    c->pid = -1;
    if (!WIFEXITED(wst)) return -1;      /* no exit code: died on a signal */
    if (exit_code) *exit_code = WEXITSTATUS(wst);
    return 0;
}

static void child_close(Child *c) { (void)c; }

#endif /* _WIN32 */

/* ------------------------------------------------------------------- main */

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

    Child child;
    memset(&child, 0, sizeof child);
    FILE *child_in = NULL, *child_out = NULL;
    if (child_spawn(&child, server, root, verse_id, &child_in, &child_out) != 0) return 70;
    setvbuf(child_in, NULL, _IOLBF, 0);

    int status = 0;
    for (int i = 0; i < nlines; i++) {
        if (lines[i][0] == '#') {
            if (strncmp(lines[i], "#crash", 6) == 0) {
                fflush(child_in);
                child_kill(&child);
                child_wait(&child, NULL);
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
    int code = 0;
    if (child_wait(&child, &code) == 0 && code != 0 && status == 0) status = code;
    child_close(&child);

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return status;
}
