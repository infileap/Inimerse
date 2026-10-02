#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* UTF-8 validity check (strict: overlongs / surrogates rejected) */
static int inim_utf8_valid(const unsigned char *s, int len) {
    int i = 0;
    while (i < len) {
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        int need;
        if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        else return 0;
        if (i + need > len) return 0;
        for (int k = 1; k < need; k++)
            if ((s[i + k] & 0xC0) != 0x80) return 0;
        if (need == 2 && (c & 0xFE) == 0xC0) return 0;
        if (need == 3 && c == 0xE0 && s[i + 1] < 0xA0) return 0;
        if (need == 3 && c == 0xED && s[i + 1] >= 0xA0) return 0;
        if (need == 4 && c == 0xF0 && s[i + 1] < 0x90) return 0;
        if (need == 4 && c > 0xF4) return 0;
        if (need == 4 && c == 0xF4 && s[i + 1] >= 0x90) return 0;
        i += need;
    }
    return 1;
}

/* Read a text file as UTF-8: strip BOM; if the bytes are not valid UTF-8,
   assume GBK (cp936, the engine's legacy encoding) and transcode to UTF-8.

   This lives in the engine core rather than in src/main.c because two of the
   engine's own translation units call it -- src/parser/parser.c (through
   parse_program_file()) and src/compiler/compiler.c (module inlining).  While
   the definition sat in src/main.c, which is deliberately NOT part of the
   inimerse_engine object library, every embedder that links the engine had to
   compile src/main.c a second time just to resolve this one symbol. */
char *inim_load_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > (1 << 26)) { fclose(f); return NULL; }
    char *buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[rd] = '\0';
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 3 + 1);
        rd -= 3;
    }
    #ifdef _WIN32
    if (rd > 0 && !inim_utf8_valid((const unsigned char*)buf, (int)rd)) {
        int wlen = MultiByteToWideChar(936, 0, buf, (int)rd, NULL, 0);
        if (wlen > 0) {
            wchar_t *wb = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
            if (wb) {
                MultiByteToWideChar(936, 0, buf, (int)rd, wb, wlen);
                int ulen = WideCharToMultiByte(CP_UTF8, 0, wb, wlen, NULL, 0, NULL, NULL);
                if (ulen > 0) {
                    char *ub = (char*)malloc((size_t)ulen + 1);
                    if (ub) {
                        WideCharToMultiByte(CP_UTF8, 0, wb, wlen, ub, ulen, NULL, NULL);
                        ub[ulen] = '\0';
                        free(wb);
                        free(buf);
                        return ub;
                    }
                }
                free(wb);
            }
        }
    }
    #endif
    return buf;
}
