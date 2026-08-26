/* ============================================================
 * json_mod.c - JSON serialize / parse module
 * Registered by json_mod_register(vm); called from main.c.
 *
 *   json_serialize(value) -> JSON string
 *   json_parse(json_str)  -> value (array/dict/string/number/bool/nil)
 *
 * Notes: non-ASCII text is handled as GBK bytes; strings are
 * escaped with \uXXXX on output and decoded back on input.
 * ============================================================ */
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static Value json_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *json_arg_str(VM *vm, int i) { Value v = json_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static void json_popn(VM *vm, int n) { vm_cur_set_sp(vm, vm_cur_sp(vm) - n); }

/* ---- serialize ---- */

static void json_write(VM *vm, const Value *v, char *out, int *pos, int outsz);

/* escape a GBK string into JSON form (\uXXXX for non-ASCII, control escapes for ASCII) */
static void json_escape_str(const char *s, char *out, int *pos, int outsz) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*pos + 12 >= outsz) { *pos = outsz; break; } /* saturate: trigger dynamic extend */
        unsigned char c = *p;
        if (c == '"') { out[(*pos)++] = '\\'; out[(*pos)++] = '"'; p++; }
        else if (c == '\\') { out[(*pos)++] = '\\'; out[(*pos)++] = '\\'; p++; }
        else if (c == '\n') { out[(*pos)++] = '\\'; out[(*pos)++] = 'n'; p++; }
        else if (c == '\r') { out[(*pos)++] = '\\'; out[(*pos)++] = 'r'; p++; }
        else if (c == '\t') { out[(*pos)++] = '\\'; out[(*pos)++] = 't'; p++; }
        else if (c == '\b') { out[(*pos)++] = '\\'; out[(*pos)++] = 'b'; p++; }
        else if (c == '\f') { out[(*pos)++] = '\\'; out[(*pos)++] = 'f'; p++; }
        else if (c < 0x20) { int n = snprintf(out + *pos, outsz - *pos, "\\u%04x", c); *pos += n; p++; }
        else if (c >= 0x80) {
            /* GBK lead byte: try to convert one GBK char (1-2 bytes) to UTF-16 */
            int blen = (c >= 0x81 && c <= 0xFE && p[1] != 0) ? 2 : 1;
            WCHAR wc[2] = {0};
            int wn = MultiByteToWideChar(CP_ACP, 0, (const char *)p, blen, wc, 2);
            if (wn > 0) {
                for (int k = 0; k < wn && k < 2; k++) {
                    int n = snprintf(out + *pos, outsz - *pos, "\\u%04x", (unsigned)wc[k]);
                    *pos += n;
                }
            } else {
                int n = snprintf(out + *pos, outsz - *pos, "\\u%04x", c);
                *pos += n;
            }
            p += blen;
        }
        else { out[(*pos)++] = (char)c; p++; }
    }
}

static void json_write(VM *vm, const Value *v, char *out, int *pos, int outsz) {
    if (*pos >= outsz - 8) { *pos = outsz; return; } /* saturate: caller extends buffer */
    switch (v->type) {
        case VAL_INT:
            *pos += snprintf(out + *pos, outsz - *pos, "%lld", (long long)v->ival);
            break;
        case VAL_FLOAT:
            *pos += snprintf(out + *pos, outsz - *pos, "%g", v->fval);
            break;
        case VAL_BOOL:
            *pos += snprintf(out + *pos, outsz - *pos, "%s", v->ival ? "true" : "false");
            break;
        case VAL_NIL:
            *pos += snprintf(out + *pos, outsz - *pos, "null");
            break;
        case VAL_STRING: {
            if (*pos + 2 < outsz) out[(*pos)++] = '"';
            json_escape_str(v->sval ? v->sval : "", out, pos, outsz);
            if (*pos + 2 < outsz) out[(*pos)++] = '"';
            break;
        }
        case VAL_ARRAY: {
            VM_LOCK(vm);
            ArrayObj *a = vm_pool_slot(vm, v->ival - 1);
            out[(*pos)++] = '[';
            for (int i = 0; i < a->count; i++) {
                if (i > 0) out[(*pos)++] = ',';
                json_write(vm, &a->items[i], out, pos, outsz);
            }
            out[(*pos)++] = ']';
            VM_UNLOCK(vm);
            break;
        }
        case VAL_DICT: {
            VM_LOCK(vm);
            ArrayObj *a = vm_pool_slot(vm, v->ival - 1);
            out[(*pos)++] = '{';
            for (int i = 0; i + 1 < a->count; i += 2) {
                if (i > 0) out[(*pos)++] = ',';
                json_write(vm, &a->items[i], out, pos, outsz);
                out[(*pos)++] = ':';
                json_write(vm, &a->items[i + 1], out, pos, outsz);
            }
            out[(*pos)++] = '}';
            VM_UNLOCK(vm);
            break;
        }
        default:
            *pos += snprintf(out + *pos, outsz - *pos, "null");
            break;
    }
}

static int builtin_json_serialize(VM *vm) {
    Value v = json_arg(vm, 0);
    json_popn(vm, vm->cur_argc);
    char *buf = malloc(1 << 20); /* 1MB cap */
    int pos = 0;
    json_write(vm, &v, buf, &pos, 1 << 20);
    buf[pos] = '\0';
    push_string(vm, buf);
    free(buf);
    return 1;
}

/* ---- parse ---- */

static void json_skip_ws(const char *s, int *i) {
    while (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r') (*i)++;
}

/* decode \uXXXX escapes into GBK bytes via UTF-16 */
static void json_decode_string(const char *s, int *i, char *out, int *opos, int outsz) {
    (*i)++; /* opening quote */
    while (s[*i] && s[*i] != '"' && *opos < outsz - 1) {
        char c = s[*i];
        if (c == '\\') {
            (*i)++;
            char e = s[*i];
            switch (e) {
                case 'n': out[(*opos)++] = '\n'; (*i)++; break;
                case 'r': out[(*opos)++] = '\r'; (*i)++; break;
                case 't': out[(*opos)++] = '\t'; (*i)++; break;
                case 'b': out[(*opos)++] = '\b'; (*i)++; break;
                case 'f': out[(*opos)++] = '\f'; (*i)++; break;
                case '"': out[(*opos)++] = '"'; (*i)++; break;
                case '\\': out[(*opos)++] = '\\'; (*i)++; break;
                case '/': out[(*opos)++] = '/'; (*i)++; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int k = 0; k < 4 && s[*i + 1 + k]; k++) {
                        char h = s[*i + 1 + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                    }
                    *i += 5; /* \uXXXX */
                    WCHAR wc = (WCHAR)cp;
                    char mb[8];
                    int mbLen = WideCharToMultiByte(CP_ACP, 0, &wc, 1, mb, 8, NULL, NULL);
                    for (int k = 0; k < mbLen && *opos < outsz - 1; k++) out[(*opos)++] = mb[k];
                    break;
                }
                default: (*i)++;
            }
        } else {
            out[(*opos)++] = c;
            (*i)++;
        }
    }
    if (s[*i] == '"') (*i)++;
    out[*opos] = '\0';
}

static Value json_parse_value(VM *vm, const char *s, int *i, int *ok);

static Value json_parse_value(VM *vm, const char *s, int *i, int *ok) {
    Value v; memset(&v, 0, sizeof(v));
    *ok = 1; /* default success; failure paths set 0 */
    json_skip_ws(s, i);
    char c = s[*i];
    if (c == '{') {
        (*i)++;
        int aidx = vm_array_new(vm);
        if (aidx < 0) { *ok = 0; return v; }
        json_skip_ws(s, i);
        if (s[*i] == '}') { (*i)++; v.type = VAL_DICT; v.ival = aidx + 1; return v; }
        for (;;) {
            char kbuf[8192]; int kpos = 0;
            json_skip_ws(s, i);
            if (s[*i] != '"') { *ok = 0; return v; }
            json_decode_string(s, i, kbuf, &kpos, 8192);
            json_skip_ws(s, i);
            if (s[*i] == ':') (*i)++;
            Value k; k.type = VAL_STRING; k.ival = 0; k.fval = 0; k.sval = strdup(kbuf);
            Value val = json_parse_value(vm, s, i, ok);
            if (!*ok) { free(k.sval); return v; }
            vm_dict_set(vm, aidx, &k, &val);
            free(k.sval);
            if (val.type == VAL_STRING && val.ival == 0 && val.sval) free(val.sval);
            json_skip_ws(s, i);
            if (s[*i] == ',') { (*i)++; continue; }
            if (s[*i] == '}') { (*i)++; break; }
            *ok = 0; return v;
        }
        v.type = VAL_DICT; v.ival = aidx + 1;
        return v;
    }
    if (c == '[') {
        (*i)++;
        int aidx = vm_array_new(vm);
        if (aidx < 0) { *ok = 0; return v; }
        json_skip_ws(s, i);
        if (s[*i] == ']') { (*i)++; v.type = VAL_ARRAY; v.ival = aidx + 1; return v; }
        for (;;) {
            Value val = json_parse_value(vm, s, i, ok);
            if (!*ok) return v;
            vm_array_push(vm, aidx, &val);
            if (val.type == VAL_STRING && val.ival == 0 && val.sval) free(val.sval);
            json_skip_ws(s, i);
            if (s[*i] == ',') { (*i)++; continue; }
            if (s[*i] == ']') { (*i)++; break; }
            *ok = 0; return v;
        }
        v.type = VAL_ARRAY; v.ival = aidx + 1;
        return v;
    }
    if (c == '"') {
        char *buf = malloc(1 << 20);
        int bpos = 0;
        json_decode_string(s, i, buf, &bpos, 1 << 20);
        v.type = VAL_STRING; v.ival = 0; v.fval = 0; v.sval = buf; /* caller frees */
        return v;
    }
    if (strncmp(s + *i, "true", 4) == 0) { *i += 4; v.type = VAL_BOOL; v.ival = 1; return v; }
    if (strncmp(s + *i, "false", 5) == 0) { *i += 5; v.type = VAL_BOOL; v.ival = 0; return v; }
    if (strncmp(s + *i, "null", 4) == 0) { *i += 4; v.type = VAL_NIL; return v; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char num[64]; int npos = 0;
        while (s[*i] && (s[*i] == '-' || s[*i] == '+' || s[*i] == '.' || s[*i] == 'e' || s[*i] == 'E' ||
               (s[*i] >= '0' && s[*i] <= '9')) && npos < 62) num[npos++] = s[(*i)++];
        num[npos] = '\0';
        if (strchr(num, '.') || strchr(num, 'e') || strchr(num, 'E')) {
            v.type = VAL_FLOAT; v.fval = strtod(num, NULL);
        } else {
            v.type = VAL_INT; v.ival = strtoll(num, NULL, 10);
        }
        return v;
    }
    *ok = 0;
    return v;
}

static int builtin_json_parse(VM *vm) {
    const char *s = json_arg_str(vm, 0);
    json_popn(vm, vm->cur_argc);
    int i = 0, ok = 1;
    Value v = json_parse_value(vm, s, &i, &ok);
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    }
    return 1;
}

void json_mod_register(VM *vm) {
    vm_register_builtin(vm, "json_serialize", builtin_json_serialize);
    vm_register_builtin(vm, "json_parse", builtin_json_parse);
}

#pragma GCC diagnostic pop

/* public wrappers for other modules (record_mod) */
void json_write_value(VM *vm, const Value *v, char *out, int *pos, int outsz) { json_write(vm, v, out, pos, outsz); }
Value json_parse_value_text(VM *vm, const char *s, int *ok) { int i =0; return json_parse_value(vm, s, &i, ok); }

/* dynamic-buffer serialize: auto-extends (record save etc.) */
void json_write_value_dyn(VM *vm, const Value *v, char **buf, int *pos, int *cap) {
    json_write(vm, v, *buf, pos, *cap);
    while (*pos >= *cap) {
        int nc = (*cap) * 2;
        char *nb = realloc(*buf, (size_t)nc);
        if (!nb) break;
        *buf = nb; *cap = nc; *pos =0;
        json_write(vm, v, *buf, pos, *cap);
    }
}
