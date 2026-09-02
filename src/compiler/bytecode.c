#include "bytecode.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ---------- ��ʼ�� ---------- */
void bytecode_init(Bytecode *bc) {
    bc->code = NULL;
    bc->count = 0;
    bc->capacity = 0;
    bc->string_pool = NULL;
    bc->string_count = 0;
    bc->str_interned = NULL;
    bc->float_pool = NULL;
    bc->float_count = 0;
    bc->func_count = 0;
    for (int i = 0; i < 64; i++) {
        bc->funcs[i] = NULL;
        bc->func_argc[i] = 0;
        bc->func_names[i] = NULL;
    }
    bc->thread_count = 0;
    bc->main_flags = 0;
    bc->try_entries = NULL;
    bc->try_count = 0;
    bc->try_cap = 0;
    bc->global_names = NULL;
    bc->global_name_count = 0;
    bc->capture_names = NULL;
    bc->capture_count = 0;
    for (int i = 0; i < 32; i++) {
        bc->threads[i] = NULL;
        bc->thread_argc[i] = 0;
        bc->thread_names[i] = NULL;
        bc->thread_flags[i] = 0;
    }
}

/* ---------- ָ������ ---------- */
static void ensure_capacity(Bytecode *bc) {
    if (bc->count >= bc->capacity) {
        int next = bc->capacity == 0 ? 32 : bc->capacity * 2;
        RegInstruction *code = realloc(bc->code, (size_t)next * sizeof(*code));
        if (!code) { fprintf(stderr, "bytecode: out of memory growing instruction stream\n"); abort(); }
        bc->code = code;
        bc->capacity = next;
    }
}

void bytecode_add(Bytecode *bc, OpCode op, int r1, int r2, int r3) {
    ensure_capacity(bc);
    RegInstruction ins = { .op = op, .r1 = r1, .r2 = r2, .r3 = r3 };
    bc->code[bc->count++] = ins;
}

/* ---------- �����ع��� ---------- */
int bytecode_add_string(Bytecode *bc, const char *str) {
    for (int i = 0; i < bc->string_count; i++)
        if (strcmp(bc->string_pool[i], str) == 0)
            return i;
    char **pool = realloc(bc->string_pool, (size_t)(bc->string_count + 1) * sizeof(*pool));
    if (!pool) { fprintf(stderr, "bytecode: out of memory growing string pool\n"); abort(); }
    char *copy = strdup(str);
    if (!copy) { fprintf(stderr, "bytecode: out of memory copying string constant\n"); abort(); }
    bc->string_pool = pool;
    bc->string_pool[bc->string_count] = copy;
    return bc->string_count++;
}

int bytecode_add_float(Bytecode *bc, double val) {
    for (int i = 0; i < bc->float_count; i++)
        if (bc->float_pool[i] == val)
            return i;
    double *pool = realloc(bc->float_pool, (size_t)(bc->float_count + 1) * sizeof(*pool));
    if (!pool) { fprintf(stderr, "bytecode: out of memory growing float pool\n"); abort(); }
    bc->float_pool = pool;
    bc->float_pool[bc->float_count] = val;
    return bc->float_count++;
}

int bytecode_add_capture(Bytecode *bc, const char *name) {
    if (!bc || !name || !*name || bc->capture_count >= 1024) return -1;
    char **grown = (char **)realloc(bc->capture_names, (size_t)(bc->capture_count + 1) * sizeof(*grown));
    if (!grown) return -1;
    bc->capture_names = grown;
    bc->capture_names[bc->capture_count] = strdup(name);
    if (!bc->capture_names[bc->capture_count]) return -1;
    return bc->capture_count++;
}

/* ---------- ƫ��������� ---------- */
int bytecode_current_offset(Bytecode *bc) {
    return bc->count;
}

void bytecode_patch(Bytecode *bc, int offset, int r2) {
    if (offset >= 0 && offset < bc->count)
        bc->code[offset].r2 = r2;
}

/* ---------- �ͷ� ---------- */
void bytecode_free(Bytecode *bc) {
    free(bc->code);
    bc->code = NULL;
    for (int i = 0; i < bc->string_count; i++)
        free(bc->string_pool[i]);
    free(bc->string_pool);
    bc->string_pool = NULL;
    free(bc->str_interned);
    bc->str_interned = NULL;
    free(bc->float_pool);
    bc->float_pool = NULL;
    bc->count = bc->capacity = 0;
    bc->string_count = bc->float_count = 0;
    /* �ͷź����� */
    for (int i = 0; i < bc->func_count; i++) {
        if (bc->funcs[i]) {
            bytecode_free(bc->funcs[i]);
            free(bc->funcs[i]);
            bc->funcs[i] = NULL;
        }
        free(bc->func_names[i]);
        bc->func_names[i] = NULL;
    }
    bc->func_count = 0;
    /* �ͷ��̶߳� */
    for (int i = 0; i < bc->thread_count; i++) {
        if (bc->threads[i]) {
            bytecode_free(bc->threads[i]);
            free(bc->threads[i]);
            bc->threads[i] = NULL;
        }
        free(bc->thread_names[i]);
        bc->thread_names[i] = NULL;
    }
    bc->thread_count = 0;
    free(bc->try_entries);
    if (bc->global_names) { for (int i = 0; i < bc->global_name_count; i++) free(bc->global_names[i]); free(bc->global_names); bc->global_names = NULL; }
    if (bc->capture_names) { for (int i = 0; i < bc->capture_count; i++) free(bc->capture_names[i]); free(bc->capture_names); bc->capture_names = NULL; }
    bc->capture_count = 0;
    bc->try_entries = NULL;
    bc->try_count = bc->try_cap = 0;
}

void bytecode_add_try(Bytecode *bc, int start_off, int end_off, int catch_off, int var_idx, int ignore) {
    if (bc->try_count >= bc->try_cap) {
        bc->try_cap = bc->try_cap == 0 ? 8 : bc->try_cap * 2;
        TryEntry *entries = realloc(bc->try_entries, (size_t)bc->try_cap * sizeof(*entries));
        if (!entries) { fprintf(stderr, "bytecode: out of memory growing exception table\n"); abort(); }
        bc->try_entries = entries;
    }
    bc->try_entries[bc->try_count].start_off = start_off;
    bc->try_entries[bc->try_count].end_off = end_off;
    bc->try_entries[bc->try_count].catch_off = catch_off;
    bc->try_entries[bc->try_count].var_idx = var_idx;
    bc->try_entries[bc->try_count].ignore = ignore;
    bc->try_count++;
}

/* ========== ���л� / �����л������ڴ���� ========== */
int bytecode_write(Bytecode *bc, FILE *f) {
    /* д��ָ������ */
    fwrite(&bc->count, sizeof(int), 1, f);

    /* д���ַ����� */
    fwrite(&bc->string_count, sizeof(int), 1, f);
    for (int i = 0; i < bc->string_count; i++) {
        int len = strlen(bc->string_pool[i]) + 1;
        fwrite(&len, sizeof(int), 1, f);
        fwrite(bc->string_pool[i], 1, len, f);
    }

    /* д�븡��� */
    fwrite(&bc->float_count, sizeof(int), 1, f);
    fwrite(bc->float_pool, sizeof(double), bc->float_count, f);

    /* д��ָ�� */
    fwrite(bc->code, sizeof(RegInstruction), bc->count, f);

    /* д�뺯���� */
    fwrite(&bc->func_count, sizeof(int), 1, f);
    for (int i = 0; i < bc->func_count; i++) {
        bytecode_write(bc->funcs[i], f);
        fwrite(&bc->func_argc[i], sizeof(int), 1, f);
        int nlen = strlen(bc->func_names[i]) + 1;
        fwrite(&nlen, sizeof(int), 1, f);
        fwrite(bc->func_names[i], 1, nlen, f);
    }

    /* д���̶߳� */
    fwrite(&bc->thread_count, sizeof(int), 1, f);
    for (int i = 0; i < bc->thread_count; i++) {
        bytecode_write(bc->threads[i], f);
        fwrite(&bc->thread_argc[i], sizeof(int), 1, f);
        int nlen = strlen(bc->thread_names[i]) + 1;
        fwrite(&nlen, sizeof(int), 1, f);
        fwrite(bc->thread_names[i], 1, nlen, f);
    }
    /* write try/exception table */
    fwrite(&bc->try_count, sizeof(int), 1, f);
    for (int i = 0; i < bc->try_count; i++) {
        fwrite(&bc->try_entries[i].start_off, sizeof(int), 1, f);
        fwrite(&bc->try_entries[i].end_off, sizeof(int), 1, f);
        fwrite(&bc->try_entries[i].catch_off, sizeof(int), 1, f);
        fwrite(&bc->try_entries[i].var_idx, sizeof(int), 1, f);
    }
    /* write global names */
    fwrite(&bc->global_name_count, sizeof(int), 1, f);
    for (int i = 0; i < bc->global_name_count; i++) {
        int glen = strlen(bc->global_names[i]) + 1;
        fwrite(&glen, sizeof(int), 1, f);
        fwrite(bc->global_names[i], 1, glen, f);
    }
    return 0;
}

Bytecode *bytecode_read(FILE *f) {
    Bytecode *bc = malloc(sizeof(Bytecode));
    bytecode_init(bc);

    /* ��ȡָ������ */
    int count;
    if (fread(&count, sizeof(int), 1, f) != 1) goto fail;

    /* ��ȡ�ַ����� */
    int str_count;
    if (fread(&str_count, sizeof(int), 1, f) != 1) goto fail;
    bc->string_pool = malloc(str_count * sizeof(char*));
    for (int i = 0; i < str_count; i++) {
        int len;
        if (fread(&len, sizeof(int), 1, f) != 1) goto fail;
        bc->string_pool[i] = malloc(len);
        if (fread(bc->string_pool[i], 1, len, f) != (size_t)len) goto fail;
        bc->string_count++;
    }

    /* ��ȡ����� */
    int float_count;
    if (fread(&float_count, sizeof(int), 1, f) != 1) goto fail;
    bc->float_pool = malloc(float_count * sizeof(double));
    if (fread(bc->float_pool, sizeof(double), float_count, f) != (size_t)float_count) goto fail;
    bc->float_count = float_count;

    /* ��ȡָ�� */
    bc->capacity = count;
    bc->code = malloc(count * sizeof(RegInstruction));
    if (fread(bc->code, sizeof(RegInstruction), count, f) != (size_t)count) goto fail;
    bc->count = count;

    /* ��ȡ������ */
    int func_count;
    if (fread(&func_count, sizeof(int), 1, f) != 1) goto fail;
    for (int i = 0; i < func_count && i < 1024; i++) {
        Bytecode *fb = bytecode_read(f);
        if (!fb) goto fail;
        bc->funcs[i] = fb;
        if (fread(&bc->func_argc[i], sizeof(int), 1, f) != 1) goto fail;
        int nlen;
        if (fread(&nlen, sizeof(int), 1, f) != 1) goto fail;
        bc->func_names[i] = malloc(nlen);
        if (fread(bc->func_names[i], 1, nlen, f) != (size_t)nlen) goto fail;
        bc->func_count++;
    }

    /* ��ȡ�̶߳� */
    int thread_count;
    if (fread(&thread_count, sizeof(int), 1, f) != 1) goto fail;
    for (int i = 0; i < thread_count && i < 32; i++) {
        Bytecode *tb = bytecode_read(f);
        if (!tb) goto fail;
        bc->threads[i] = tb;
        if (fread(&bc->thread_argc[i], sizeof(int), 1, f) != 1) goto fail;
        int nlen;
        if (fread(&nlen, sizeof(int), 1, f) != 1) goto fail;
        bc->thread_names[i] = malloc(nlen);
        if (fread(bc->thread_names[i], 1, nlen, f) != (size_t)nlen) goto fail;
        bc->thread_count++;
    }
    /* read try/exception table */
    if (fread(&bc->try_count, sizeof(int), 1, f) != 1) goto fail;
    if (bc->try_count > 0) {
        bc->try_entries = malloc((size_t)bc->try_count * sizeof(TryEntry));
        if (!bc->try_entries) goto fail;
        bc->try_cap = bc->try_count;
        for (int i = 0; i < bc->try_count; i++) {
            if (fread(&bc->try_entries[i].start_off, sizeof(int), 1, f) != 1) goto fail;
            if (fread(&bc->try_entries[i].end_off, sizeof(int), 1, f) != 1) goto fail;
            if (fread(&bc->try_entries[i].catch_off, sizeof(int), 1, f) != 1) goto fail;
            if (fread(&bc->try_entries[i].var_idx, sizeof(int), 1, f) != 1) goto fail;
            bc->try_entries[i].ignore = 0; /* loaded bytecode: no ignore flag (format unchanged) */
        }
    }
    if (fread(&bc->global_name_count, sizeof(int), 1, f) != 1) goto fail;
    if (bc->global_name_count > 0) {
        bc->global_names = calloc(bc->global_name_count, sizeof(char*));
        for (int i = 0; i < bc->global_name_count; i++) {
            int glen = 0;
            if (fread(&glen, sizeof(int), 1, f) != 1 || glen <= 0 || glen > 4096) goto fail;
            bc->global_names[i] = malloc(glen);
            if (fread(bc->global_names[i], 1, glen, f) != (size_t)glen) goto fail;
        }
    }

    return bc;

fail:
    bytecode_free(bc);
    free(bc);
    return NULL;
}

/* .inim file format: 8-byte magic "INIMBC" + format version + bytecode_write stream */
int bytecode_write_file(const char *path, Bytecode *bc) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const char magic[8] = { 'I','N','I','M','B','C',3,0 };
    fwrite(magic, 1, 8, f);
    bytecode_write(bc, f);
    fclose(f);
    return 0;
}

Bytecode *bytecode_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || magic[0] != 'I' || magic[1] != 'N' ||
        magic[2] != 'I' || magic[3] != 'M' || magic[4] != 'B' || magic[5] != 'C' ||
        magic[6] != 3) {
        fclose(f);
        return NULL;
    }
    Bytecode *bc = bytecode_read(f);
    fclose(f);
    return bc;
}

/* ========== EXE Ƕ�루β������������֮ǰ���ݣ� ========== */
#define TAIL_MAGIC 0x1BC0FFEE
#define MODS_MAGIC 0x1BC0FEED

static int read_tail_header(FILE *f, uint32_t *magic, uint32_t *offset, uint32_t *total_len);

int bytecode_append_to_exe(const char *exePath, Bytecode *bc, const char *outputExe) {
    FILE *src = fopen(exePath, "rb");
    if (!src) return -1;
    FILE *dst = fopen(outputExe, "wb");
    if (!dst) { fclose(src); return -1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);
    fclose(src);

    long offset = ftell(dst);
    bytecode_write(bc, dst);

    uint32_t magic = TAIL_MAGIC;
    fwrite(&magic, sizeof(magic), 1, dst);
    uint32_t off = (uint32_t)offset;
    fwrite(&off, sizeof(off), 1, dst);

    fclose(dst);
    return 0;
}

Bytecode *bytecode_load_from_exe(const char *exePath) {
    FILE *f = fopen(exePath, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 16) { fclose(f); return NULL; }

    /* ���һ��β����ģ��飨magic + offset + total_len�����ֽ������ģ���֮ǰ */
    uint32_t magic, offset, total_len;
    if (read_tail_header(f, &magic, &offset, &total_len) == 0 &&
        magic == MODS_MAGIC && (long)offset >= 8 && (long)offset < size) {
        /* ģ���֮ǰ�����ֽ�����β�� [BC_MAGIC][bc_offset] */
        if (fseek(f, (long)offset - 8, SEEK_SET) != 0) { fclose(f); return NULL; }
        uint32_t bc_magic, bc_offset;
        if (fread(&bc_magic, sizeof(bc_magic), 1, f) != 1 ||
            fread(&bc_offset, sizeof(bc_offset), 1, f) != 1 ||
            bc_magic != TAIL_MAGIC || (long)bc_offset < 0 || (long)bc_offset >= (long)offset) {
            fclose(f);
            return NULL;
        }
        fseek(f, bc_offset, SEEK_SET);
        Bytecode *bc = bytecode_read(f);
        fclose(f);
        return bc;
    }

    /* �������ֻ���ֽ���飬β�� [BC_MAGIC][bc_offset] */
    fseek(f, -4, SEEK_END);
    uint32_t bc_offset;
    if (fread(&bc_offset, sizeof(bc_offset), 1, f) != 1) { fclose(f); return NULL; }

    fseek(f, -8, SEEK_END);
    uint32_t bc_magic;
    if (fread(&bc_magic, sizeof(bc_magic), 1, f) != 1 || bc_magic != TAIL_MAGIC) { fclose(f); return NULL; }

    fseek(f, bc_offset, SEEK_SET);
    Bytecode *bc = bytecode_read(f);
    fclose(f);
    return bc;
}

/* ========== ģ����ԴǶ�루��ģ���������� ========== */
#define MODS_FILE_COUNT_MAX 256

/* �� CRC32�����ڴ��ʱ��Դģ���ļ���У�飬�����ظ�Ƕ�������� */
static unsigned int crc32_buf(const unsigned char *data, size_t len) {
    unsigned int crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static unsigned int crc32_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char buf[8192];
    size_t n;
    unsigned int crc = 0xFFFFFFFFu;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    fclose(f);
    return ~crc;
}

/* �ռ� mods Ŀ¼��ָ�����Ƶ�ģ����Ŀ¼�ڵ�ȫ���ļ�·�� */
static char **collect_mod_files(const char *modsDir, const char *modNames,
                                char ***out_rel_paths, size_t *out_count) {
#ifdef _WIN32
    char **paths = NULL;
    size_t count = 0;

    char names_buf[1024];
    strncpy(names_buf, modNames, sizeof(names_buf) - 1);
    names_buf[sizeof(names_buf) - 1] = '\0';

    char *save = NULL;
    char *tok = strtok_r(names_buf, ",", &save);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (*tok == '\0') { tok = strtok_r(NULL, ",", &save); continue; }

        char mod_path[2048];
        snprintf(mod_path, sizeof(mod_path), "%s\\%s", modsDir, tok);

        WIN32_FIND_DATA fd;
        char sp[2200];
        snprintf(sp, sizeof(sp), "%s\\*", mod_path);
        HANDLE hFind = FindFirstFile(sp, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

                char rel[2048];
                snprintf(rel, sizeof(rel), "%s/%s", tok, fd.cFileName);
                char full[2048];
                snprintf(full, sizeof(full), "%s\\%s", mod_path, fd.cFileName);

                paths = realloc(paths, (count + 1) * sizeof(char*));
                *out_rel_paths = realloc(*out_rel_paths, (count + 1) * sizeof(char*));
                paths[count] = strdup(full);
                (*out_rel_paths)[count] = strdup(rel);
                count++;
            } while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }
        tok = strtok_r(NULL, ",", &save);
    }

    *out_count = count;
    return paths;
#else
    (void)modsDir; (void)modNames; (void)out_rel_paths; (void)out_count;
    return NULL;
#endif
}

int bytecode_append_mods_to_exe(const char *exePath, const char *modsDir,
                                const char *modNames, const char *outputExe) {
    size_t file_count = 0;
    char **rel_paths = NULL;
    char **full_paths = collect_mod_files(modsDir, modNames, &rel_paths, &file_count);
    if (!full_paths || file_count == 0) {
        if (full_paths) free(full_paths);
        if (rel_paths) free(rel_paths);
        return -2; /* û���ҵ��κ�ģ���ļ� */
    }

    FILE *src = fopen(exePath, "rb");
    if (!src) { 
        for (size_t i = 0; i < file_count; i++) { free(full_paths[i]); free(rel_paths[i]); }
        free(full_paths); free(rel_paths);
        return -1;
    }
    FILE *dst = fopen(outputExe, "wb");
    if (!dst) { fclose(src); 
        for (size_t i = 0; i < file_count; i++) { free(full_paths[i]); free(rel_paths[i]); }
        free(full_paths); free(rel_paths);
        return -1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);
    fclose(src);

    long payload_offset = ftell(dst);

    /* д��ģ����Դ�飺�ļ��� -> ÿ���ļ�(���·������, ���·��, ���ݳ���, ����, CRC) */
    uint32_t fcount = (uint32_t)file_count;
    fwrite(&fcount, sizeof(fcount), 1, dst);

    for (size_t i = 0; i < file_count; i++) {
        FILE *mf = fopen(full_paths[i], "rb");
        if (!mf) { 
            for (size_t j = 0; j < file_count; j++) { free(full_paths[j]); free(rel_paths[j]); }
            free(full_paths); free(rel_paths);
            fclose(dst);
            return -1;
        }
        fseek(mf, 0, SEEK_END);
        long mlen = ftell(mf);
        fseek(mf, 0, SEEK_SET);
        unsigned char *data = malloc(mlen > 0 ? mlen : 1);
        if (mlen > 0)
            fread(data, 1, mlen, mf);
        fclose(mf);

        uint32_t rel_len = (uint32_t)strlen(rel_paths[i]) + 1;
        fwrite(&rel_len, sizeof(rel_len), 1, dst);
        fwrite(rel_paths[i], 1, rel_len, dst);
        uint32_t data_len = (uint32_t)mlen;
        fwrite(&data_len, sizeof(data_len), 1, dst);
        if (mlen > 0)
            fwrite(data, 1, mlen, dst);
        uint32_t crc = crc32_buf(data, mlen);
        fwrite(&crc, sizeof(crc), 1, dst);
        free(data);
    }

    long payload_end = ftell(dst);
    uint32_t magic = MODS_MAGIC;
    fwrite(&magic, sizeof(magic), 1, dst);
    uint32_t off = (uint32_t)payload_offset;
    fwrite(&off, sizeof(off), 1, dst);
    uint32_t total_len = (uint32_t)(payload_end - payload_offset) + 8;
    fwrite(&total_len, sizeof(total_len), 1, dst);

    fclose(dst);

    for (size_t i = 0; i < file_count; i++) { free(full_paths[i]); free(rel_paths[i]); }
    free(full_paths); free(rel_paths);
    return 0;
}

/* �� exe β����ȡ��� 12 �ֽڣ�magic + offset + total_len����д��˳��һ�£� */
static int read_tail_header(FILE *f, uint32_t *magic, uint32_t *offset, uint32_t *total_len) {
    if (fseek(f, -12, SEEK_END) != 0) return -1;
    if (fread(magic, sizeof(*magic), 1, f) != 1) return -1;
    if (fread(offset, sizeof(*offset), 1, f) != 1) return -1;
    if (fread(total_len, sizeof(*total_len), 1, f) != 1) return -1;
    return 0;
}

unsigned char *bytecode_extract_mods(const char *exePath, long *outLen) {
    FILE *f = fopen(exePath, "rb");
    if (!f) return NULL;

    uint32_t magic, offset, total_len;
    if (read_tail_header(f, &magic, &offset, &total_len) != 0 ||
        magic != MODS_MAGIC || total_len < 12) {
        fclose(f);
        return NULL;
    }

    /* ��ȫ�߽��� */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if ((long)offset < 0 || (long)offset + (long)total_len > file_size) {
        fclose(f);
        return NULL;
    }

    fseek(f, offset, SEEK_SET);
    unsigned char *buf = malloc(total_len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, total_len, f) != total_len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *outLen = total_len;
    return buf;
}

/* ��Ƕ���ģ����Դ���ͷŵ�����Ŀ¼ */
int bytecode_release_mods(const char *exePath, const char *destDir) {
    long total_len = 0;
    unsigned char *buf = bytecode_extract_mods(exePath, &total_len);
    if (!buf) return -1;

    /* �������ļ��� -> ÿ���ļ�(rel_len, rel_path, data_len, data, crc) */
    size_t pos = 0;
    uint32_t fcount;
    if (pos + 4 > (size_t)total_len) { free(buf); return -1; }
    memcpy(&fcount, buf + pos, 4);
    pos += 4;

    int released = 0;
    for (uint32_t i = 0; i < fcount; i++) {
        if (pos + 4 > (size_t)total_len) { free(buf); return -1; }
        uint32_t rel_len;
        memcpy(&rel_len, buf + pos, 4);
        pos += 4;
        if (pos + rel_len > (size_t)total_len) { free(buf); return -1; }
        char *rel = (char*)(buf + pos);
        pos += rel_len;
        if (pos + 4 > (size_t)total_len) { free(buf); return -1; }
        uint32_t data_len;
        memcpy(&data_len, buf + pos, 4);
        pos += 4;
        if (pos + data_len + 4 > (size_t)total_len) { free(buf); return -1; }
        unsigned char *data = buf + pos;
        pos += data_len;
        uint32_t crc_stored;
        memcpy(&crc_stored, buf + pos, 4);
        pos += 4;

        /* д���ļ���destDir/���·����ȷ���м�Ŀ¼���� */
        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "%s\\%s", destDir, rel);
        char *slash = out_path;
        while ((slash = strchr(slash, '/')) != NULL) {
            *slash = '\\';
            slash++;
        }
        /* �����м�Ŀ¼ */
        char tmp[4096];
        strncpy(tmp, out_path, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *p = tmp + strlen(destDir);
        while (*p) {
            if (*p == '\\') {
                *p = '\0';
                (void)im_platform_mkdirs(tmp);
                *p = '\\';
            }
            p++;
        }

        FILE *outf = fopen(out_path, "wb");
        if (!outf) { free(buf); return -1; }
        if (data_len > 0)
            fwrite(data, 1, data_len, outf);
        fclose(outf);

        /* У�� CRC */
        unsigned int crc_now = crc32_buf(data, data_len);
        if (crc_now != crc_stored) { free(buf); return -2; }

        released++;
    }

    free(buf);
    return released;
}
