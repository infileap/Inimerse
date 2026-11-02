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
 *
 * `--upp` puts the same scenario behind a UPP transport (src/verse/upp.h): the
 * server child is spawned with `--upp`, the client negotiates a hello/start,
 * and every scenario line travels as the raw JSON text of a `verse` frame's
 * `payload`.  What the scenario means is identical in both modes; only the
 * envelope differs, and the exit code of the child is still the one reported.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "upp.h"

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
                       int upp, FILE **to_child, FILE **from_child) {
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
    if (upp) cmdline_append(cmd, sizeof cmd, &len, "--upp");
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
                       int upp, FILE **to_child, FILE **from_child) {
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
        if (upp) execlp(server, server, "--upp", root, verse_id, (char *)NULL);
        else     execlp(server, server, root, verse_id, (char *)NULL);
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

/* -------------------------------------------------------------- UPP client */

/* The manifest this client announces in its hello.  The server validates it
 * with upp_validate_manifest(), the same function the reference validates with,
 * so the five fields it insists on are present even though this client reads
 * none of them: this client has no files, claims no capabilities, and speaks
 * exactly ABI 1. */
#define UPP_CLIENT_MANIFEST                                                     \
    "{\"id\":\"inim-client\",\"name\":\"inim-client\",\"version\":\"1.0.0\","   \
    "\"engine\":\"inimerse\",\"entry\":\"verse\",\"abi\":1,\"abiRange\":\"1\"," \
    "\"files\":{},\"capabilities\":[]}"

static int upp_write(FILE *to, const UppBuf *b) {
    if (!b->data) return -1;
    fprintf(to, "%s\n", b->data);
    fflush(to);
    return 0;
}

/* Read one line from the child and strip its terminator into `buf`. */
static int upp_read_line(FILE *from, char *buf, size_t cap) {
    if (!fgets(buf, (int)cap, from)) return -1;
    size_t len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    return 0;
}

/* Negotiate a session: hello, read the welcome, start, one heartbeat.  Both
 * halves of the envelope are exercised here rather than in the loop, so a
 * refusal names the phase that refused. */
static int upp_negotiate(FILE *child_in, FILE *child_out, char *resp, size_t cap,
                         char *err, size_t errlen) {
    UppBuf b;
    upp_buf_init(&b);
    VjVal *manifest = vj_parse(UPP_CLIENT_MANIFEST, err, errlen);
    if (!manifest) {
        fprintf(stderr, "inim-client: upp manifest is unparseable: %s\n", err);
        upp_buf_free(&b);
        return 70;
    }
    int rc = upp_frame_hello(&b, "client", manifest, NULL, err, errlen);
    vj_free(manifest);
    if (rc != 0) {
        fprintf(stderr, "inim-client: cannot build the upp hello: %s\n", err);
        upp_buf_free(&b);
        return 70;
    }
    rc = upp_write(child_in, &b);
    upp_buf_free(&b);
    if (rc != 0) { fprintf(stderr, "inim-client: cannot send the upp hello\n"); return 141; }

    if (upp_read_line(child_out, resp, cap) != 0) {
        fprintf(stderr, "inim-client: server closed the stream before the welcome\n");
        return 141;
    }
    VjVal *msg = upp_decode_line(resp, strlen(resp), err, errlen);
    if (!msg) {
        fprintf(stderr, "inim-client: unreadable welcome frame: %s\n", err);
        return 142;
    }
    const VjVal *typev = vj_get(msg, "type");
    const char *type = (typev && typev->type == VJ_STR && typev->s) ? typev->s : "";
    if (strcmp(type, "welcome") != 0) {
        fprintf(stderr, "inim-client: hello was refused (frame '%s')\n", type);
        vj_free(msg);
        return 143;
    }
    vj_free(msg);

    /* start + heartbeat: the server only accepts `verse` frames from a running
     * session, so these are not decorative -- without them every request below
     * would be refused as out of order. */
    upp_buf_init(&b);
    if (upp_frame_start(&b, "verse", NULL, err, errlen) != 0) {
        fprintf(stderr, "inim-client: cannot build the upp start: %s\n", err);
        upp_buf_free(&b);
        return 70;
    }
    rc = upp_write(child_in, &b);
    upp_buf_free(&b);
    if (rc != 0) { fprintf(stderr, "inim-client: cannot send the upp start\n"); return 141; }

    upp_buf_init(&b);
    if (upp_frame_heartbeat(&b, 1, upp_now_ms(), err, errlen) != 0) {
        fprintf(stderr, "inim-client: cannot build a upp heartbeat: %s\n", err);
        upp_buf_free(&b);
        return 70;
    }
    rc = upp_write(child_in, &b);
    upp_buf_free(&b);
    if (rc != 0) { fprintf(stderr, "inim-client: cannot send a upp heartbeat\n"); return 141; }
    return 0;
}

/* 0: the response text is in `out`; 1: the child closed the stream;
 * 2: the frame or the request it carried was unusable. */
static int upp_call(FILE *child_in, FILE *child_out, const char *request,
                    char *out, size_t outcap, char *resp, size_t cap,
                    char *err, size_t errlen) {
    UppBuf f;
    upp_buf_init(&f);
    if (upp_frame_build(&f, "verse", NULL, request, err, errlen) != 0) {
        fprintf(stderr, "inim-client: cannot build a verse frame: %s\n", err);
        upp_buf_free(&f);
        return 2;
    }
    int rc = upp_write(child_in, &f);
    upp_buf_free(&f);
    if (rc != 0) { fprintf(stderr, "inim-client: cannot send a verse frame\n"); return 1; }

    if (upp_read_line(child_out, resp, cap) != 0) return 1;
    VjVal *msg = upp_decode_line(resp, strlen(resp), err, errlen);
    if (!msg) {
        fprintf(stderr, "inim-client: unreadable response frame: %s\n", err);
        return 2;
    }
    const VjVal *typev = vj_get(msg, "type");
    const char *type = (typev && typev->type == VJ_STR && typev->s) ? typev->s : "";
    if (strcmp(type, "verse") != 0) {
        fprintf(stderr, "inim-client: expected a 'verse' frame, got '%s'\n", type);
        vj_free(msg);
        return 2;
    }
    const VjVal *payload = vj_get(msg, "payload");
    if (!payload) {
        fprintf(stderr, "inim-client: 'verse' frame without a payload\n");
        vj_free(msg);
        return 2;
    }
    UppBuf text;
    upp_buf_init(&text);
    if (upp_json_write(&text, payload) != 0 || !text.data || strlen(text.data) >= outcap) {
        fprintf(stderr, "inim-client: the response payload does not fit\n");
        upp_buf_free(&text);
        vj_free(msg);
        return 2;
    }
    memcpy(out, text.data, strlen(text.data) + 1);
    upp_buf_free(&text);
    vj_free(msg);
    return 0;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *server = getenv("INIM_SERVER_BIN");
    int upp = 0;
    int argi = 1;

    for (;;) {
        if (argi + 1 < argc && strcmp(argv[argi], "--server") == 0) {
            server = argv[argi + 1];
            argi += 2;
        } else if (strcmp(argv[argi], "--upp") == 0) {
            upp = 1;
            argi++;
        } else {
            break;
        }
    }
    if (argc < argi + 2) {
        fprintf(stderr, "usage: inim-client [--server <path>] [--upp] <root> <verse_id> "
                        "[scenario]\n");
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
    if (child_spawn(&child, server, root, verse_id, upp, &child_in, &child_out) != 0) return 70;
    setvbuf(child_in, NULL, _IOLBF, 0);

    /* A UPP frame may be up to 1 MiB, so its envelope needs a buffer that a
     * plain protocol line does not; the response text itself stays bounded by
     * VL_LINE_MAX in both modes. */
    char *resp = (char *)malloc(upp ? UPP_MAX_FRAME_BYTES + 2 : VL_LINE_MAX);
    char *text = (char *)malloc(VL_LINE_MAX);
    char err[UPP_ERR_MAX];
    err[0] = '\0';
    if (!resp || !text) {
        fprintf(stderr, "inim-client: out of memory\n");
        child_kill(&child);
        child_wait(&child, NULL);
        child_close(&child);
        free(resp); free(text);
        return 70;
    }
    size_t respcap = upp ? UPP_MAX_FRAME_BYTES + 2 : VL_LINE_MAX;

    int status = 0;
    if (upp) {
        status = upp_negotiate(child_in, child_out, resp, respcap, err, sizeof err);
        if (status != 0) goto done;
    }

    for (int i = 0; i < nlines; i++) {
        if (lines[i][0] == '#') {
            if (strncmp(lines[i], "#crash", 6) == 0) {
                fflush(child_in);
                child_kill(&child);
                child_wait(&child, NULL);
                child_close(&child);
                fprintf(stdout, "#crash: server killed\n");
                fflush(stdout);
                for (int k = 0; k < nlines; k++) free(lines[k]);
                free(lines);
                free(resp); free(text);
                return 137;
            }
            continue;
        }

        if (upp) {
            int rc = upp_call(child_in, child_out, lines[i], text, VL_LINE_MAX,
                              resp, respcap, err, sizeof err);
            if (rc == 1) {
                fprintf(stderr, "inim-client: server closed the stream early\n");
                status = 141;
                break;
            }
            if (rc != 0) { status = 142; break; }
            printf("%s\n", text);
            fflush(stdout);
        } else {
            fprintf(child_in, "%s\n", lines[i]);
            fflush(child_in);
            if (upp_read_line(child_out, text, VL_LINE_MAX) != 0) {
                fprintf(stderr, "inim-client: server closed the stream early\n");
                status = 141;
                break;
            }
            printf("%s\n", text);
            fflush(stdout);
        }
    }

    /* A UPP session that stops without a `stop` frame leaves the peer waiting
     * for a state it will never reach, so the end of a scenario is signalled
     * explicitly.  It carries no response, which is why it sits outside the
     * request loop. */
    if (upp && status == 0) {
        UppBuf b;
        upp_buf_init(&b);
        if (upp_frame_stop(&b, "end of scenario", err, sizeof err) == 0) upp_write(child_in, &b);
        upp_buf_free(&b);
    }

done:
    fclose(child_in);
    int code = 0;
    if (child_wait(&child, &code) == 0 && code != 0 && status == 0) status = code;
    child_close(&child);

    free(resp);
    free(text);
    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return status;
}
