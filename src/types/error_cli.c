/* `inimerse errors` -- the error table's lookup route
   (docs/ERROR_CODES_V06.md section 4).

   This lives in its own translation unit rather than in src/main.c on
   purpose: docs/AUDIT.md and CMakeLists.txt cite src/main.c lines 1096/1097
   and 1150 by number, and a hundred lines inserted into main.c moves every
   one of them.  A new file moves nothing, and main.c's own growth is kept to
   a single line. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types/error_types.h"

int im_error_print_table(const char *query, int as_json) {
    size_t n = im_error_kind_count();
    const char *dname[32];
    int dnib[32];
    size_t dcount = 0;
    for (size_t i = 0; i < n && dcount < 32; ++i) {
        const ImErrorKind *k = im_error_kind_at(i);
        if (!k) continue;
        int seen = 0;
        for (size_t d = 0; d < dcount; ++d)
            if (strcmp(dname[d], im_error_domain_name(k->domain)) == 0) { seen = 1; break; }
        if (!seen) {
            dname[dcount] = im_error_domain_name(k->domain);
            dnib[dcount] = k->code >> 4;
            ++dcount;
        }
    }

    if (as_json) {
        printf("{\"kinds\":%zu,\"domains\":%zu,\"slots_per_domain\":16,", n, dcount);
        printf("\"layout\":\"high nibble = domain, low nibble = member, 0x00 = ok\",");
        printf("\"entries\":[");
        for (size_t i = 0; i < n; ++i) {
            const ImErrorKind *k = im_error_kind_at(i);
            if (!k) continue;
            printf("%s{\"code\":%d,\"hex\":\"0x%02X\",\"name\":\"%s\",\"domain\":\"%s\"}",
                   i ? "," : "", k->code, k->code, k->name,
                   im_error_domain_name(k->domain));
        }
        printf("]}\n");
        return 0;
    }

    if (query) {
        /* A number can mean two things here, and the two are told apart by
           whether the table answers: a member code (`errors 0x21`) is tried
           first, and a bare domain nibble (`errors 1`, `errors 0x1`) is what
           is left when no kind carries that value.  `errors 0x0F` therefore
           reports a domain that does not exist, not a code that does not. */
        char *end = NULL;
        long want = strtol(query, &end, 0);
        int is_num = (end && *end == '\0');
        if (is_num && want > 0) {
            const ImErrorKind *k = NULL;
            for (size_t i = 0; i < n; ++i) {
                const ImErrorKind *c = im_error_kind_at(i);
                if (c && c->code == (int)want) { k = c; break; }
            }
            if (k) {
                printf("0x%02X  %s  %s  (member %d of %s)\n", k->code,
                       im_error_domain_name(k->domain), k->name, k->code & 0x0F,
                       im_error_domain_name(k->domain));
                return 0;
            }
        }
        /* `inimerse errors FileError` / `1` / `0x1` -- one domain. */
        for (size_t d = 0; d < dcount; ++d) {
            int hit = strcmp(dname[d], query) == 0;
            if (!hit && is_num) hit = (want == dnib[d]);
            if (!hit) continue;
            printf("%s  (high nibble 0x%X)\n", dname[d], dnib[d]);
            for (size_t i = 0; i < n; ++i) {
                const ImErrorKind *k = im_error_kind_at(i);
                if (k && strcmp(im_error_domain_name(k->domain), dname[d]) == 0)
                    printf("  0x%02X  %s\n", k->code, k->name);
            }
            return 0;
        }
        if (is_num)
            fprintf(stderr, "errors: no kind carries 0x%02lX, and no domain has high nibble 0x%lX\n",
                    want, want);
        else
            fprintf(stderr, "errors: no domain named '%s'\n", query);
        return 1;
    }

    printf("%zu kind(s) / %zu domain(s) / 16 slots per domain"
           " (high nibble = domain, low nibble = member, 0x00 = ok)\n", n, dcount);
    for (size_t d = 0; d < dcount; ++d) {
        printf("%s  (high nibble 0x%X)\n", dname[d], dnib[d]);
        for (size_t i = 0; i < n; ++i) {
            const ImErrorKind *k = im_error_kind_at(i);
            if (k && strcmp(im_error_domain_name(k->domain), dname[d]) == 0)
                printf("  0x%02X  %s\n", k->code, k->name);
        }
    }
    return 0;
}


/* `0x11 permission_denied` -- the code and the name, code first.  A reader who
   has the byte from a log has to be able to find it in a lint message, and a
   reader who has the name has to be able to read it too
   (docs/ERROR_CODES_V06.md section 3.2).  A member the error table does not
   know is printed by name alone: the name is never dropped, and inventing a
   code for it would be a second table. */
void im_error_label(const char *name, char *out, size_t cap) {
    const ImErrorKind *k = im_error_kind_lookup(name);
    if (k) snprintf(out, cap, "0x%02X %s", k->code, name);
    else snprintf(out, cap, "%s", name);
}
