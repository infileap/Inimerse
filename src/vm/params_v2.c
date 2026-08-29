#include "vm.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* V0.4 sectioned params parser. It lowers deterministic key/value input to
   restricted assignments; integration with the VM loader follows the format
   stabilization pass. */
char *vm_params_v2_lower(const char *text) {
    if (!text) return NULL;
    size_t cap = 512, len = 0; char *out = (char*)calloc(1, cap); char section[96] = {0}; int version = 0;
    if (!out) return NULL;
    char *copy = strdup(text), *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        size_t n = strlen(line); while (n && (line[n-1] == ' ' || line[n-1] == '\t' || line[n-1] == '\r')) line[--n] = 0;
        if (!*line || *line == '#' || (line[0] == '/' && line[1] == '/')) continue;
        if (*line == '[') { char *e = strchr(line, ']'); if (!e || e[1] || e == line+1 || (size_t)(e-line-1) >= sizeof(section)) { free(copy); free(out); return NULL; } memcpy(section, line+1, (size_t)(e-line-1)); section[e-line-1] = 0; continue; }
        char *eq = strchr(line, '='); if (!eq) { free(copy); free(out); return NULL; } *eq++ = 0; while (*eq == ' ' || *eq == '\t') eq++;
        char *k = line + strlen(line); while (k > line && (k[-1] == ' ' || k[-1] == '\t')) *--k = 0;
        if (strcmp(line, "params_version") == 0) { if (strcmp(eq, "2") != 0) { free(copy); free(out); return NULL; } version = 1; continue; }
        if (!*line) { free(copy); free(out); return NULL; }
        for (char *q = line; *q; q++) if (!(isalnum((unsigned char)*q) || *q == '_')) { free(copy); free(out); return NULL; }
        char stmt[1400]; snprintf(stmt, sizeof(stmt), "%s%s%s = %s;\n", section, section[0] ? "_" : "", line, eq);
        size_t m = strlen(stmt); if (len + m + 1 > cap) { while (len + m + 1 > cap) cap *= 2; char *nb = (char*)realloc(out, cap); if (!nb) { free(copy); free(out); return NULL; } out = nb; }
        memcpy(out + len, stmt, m); len += m; out[len] = 0;
    }
    free(copy); if (!version) { free(out); return NULL; } return out;
}

/* Exposed probe for tooling and future VM-loader wiring. Returns 1 when the
   input is valid v2 and writes the lowered assignment source to out. */
int vm_params_v2_lower_into(const char *text, char *out, size_t out_cap) {
    char *tmp = vm_params_v2_lower(text); if (!tmp || !out || out_cap == 0) { free(tmp); return 0; }
    size_t n = strlen(tmp); if (n + 1 > out_cap) { free(tmp); return 0; }
    memcpy(out, tmp, n + 1); free(tmp); return 1;
}
