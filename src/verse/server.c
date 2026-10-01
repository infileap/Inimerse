/* server.c - inim-server: authoritative Layer session over stdin/stdout (P1 ①②④)
 *
 * Usage: inim-server <root> [verse_id]
 *
 * Reads one canonical-JSON request per line from stdin and writes exactly one
 * response line to stdout, flushed after each.  Diagnostics go to stderr so
 * stdout stays a pure protocol stream.  Exit codes:
 *   0  clean end of session (bye seen, or EOF with a healthy anchor)
 *   2  the closing drain reported an inconsistent Layer
 *   3  the Layer could not be opened or created
 */
#include "protocol.h"
#include "eventlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VL_LINE_MAX (64 * 1024)

static int vl_ensure_layer(const char *root, const char *verse_id) {
    VlManifest m;
    VlStatus st = vl_layer_create(root, verse_id, &m);
    if (st == VL_OK) {
        fprintf(stderr, "inim-server: created verse '%s' under %s\n", verse_id, root);
        return 0;
    }
    if (st == VL_ERR_CONFLICT) return 0;         /* already exists: fine */
    fprintf(stderr, "inim-server: cannot create verse '%s': %s\n", verse_id, vl_status_name(st));
    return -1;
}

int main(int argc, char **argv) {
    const char *root = (argc > 1) ? argv[1] : ".";
    const char *verse_id = (argc > 2) ? argv[2] : "main";

    if (vl_ensure_layer(root, verse_id) != 0) return 3;

    VlServer *s = vl_server_open(root, verse_id);
    if (!s) {
        fprintf(stderr, "inim-server: cannot open verse '%s' under %s\n", verse_id, root);
        return 3;
    }

    fprintf(stderr, "inim-server: verse '%s' protocol %d ready on stdin/stdout\n",
            verse_id, VL_PROTOCOL_VERSION);

    char *line = (char *)malloc(VL_LINE_MAX);
    char *resp = (char *)malloc(VL_LINE_MAX);
    if (!line || !resp) { free(line); free(resp); vl_server_close(s); return 3; }

    int rc = 0;
    for (;;) {
        if (!fgets(line, VL_LINE_MAX, stdin)) break;   /* EOF */
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        int stop = vl_server_handle(s, line, resp, VL_LINE_MAX);
        printf("%s\n", resp);
        fflush(stdout);
        if (stop) break;
    }

    /* A session that ends without an explicit bye still has to report whether
     * the Layer was left consistent. */
    if (!vl_server_anchor_ok(s)) {
        fprintf(stderr, "inim-server: anchor check failed on exit; recovery required\n");
        rc = 2;
    }

    free(line);
    free(resp);
    vl_server_close(s);
    return rc;
}
