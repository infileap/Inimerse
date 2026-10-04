/* debug_info.c - debug sidecar emission (text line table + DWARF 5 line program) */
#include "debug_info.h"
#include "../compiler/compiler.h"
#include "../compiler/bytecode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- ULEB / SLEB helpers ---------- */
static void write_uleb(FILE *f, unsigned v) {
    do {
        unsigned char b = v & 0x7F;
        v >>= 7;
        if (v) b |= 0x80;
        fwrite(&b, 1, 1, f);
    } while (v);
}

static void write_sleb(FILE *f, int v) {
    int more = 1;
    while (more) {
        unsigned char b = v & 0x7F;
        v >>= 7;
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40))) more = 0;
        else b |= 0x80;
        fwrite(&b, 1, 1, f);
    }
}

/* ---------- DWARF 5 line-number program for one block ---------- */
#define DW_LNS_COPY 0x01
#define DW_LNS_ADVANCE_PC 0x02
#define DW_LNS_ADVANCE_LINE 0x03
#define DW_LNE_END_SEQUENCE 0x01 /* extended op: 0x00, len, sub-op */

static void write_dwarf_line_program(FILE *f, const struct DbgLineEntry *lines, int count,
                                     int block_len, const char *file_name) {
    long header_len_pos, program_start;
    /* unit header (DWARF5) */
    fwrite("\xff\xff\xff\xff", 1, 4, f);          /* unit_length placeholder */
    uint16_t version = 5; fwrite(&version, 2, 1, f);
    uint8_t address_size = 8, seg_sel = 0;
    fwrite(&address_size, 1, 1, f);
    fwrite(&seg_sel, 1, 1, f);
    header_len_pos = ftell(f);
    fwrite("\0\0\0\0", 1, 4, f);                  /* header_length placeholder */
    uint8_t min_inst = 1, max_ops = 1, default_is_stmt = 1;
    int8_t line_base = -5;
    uint8_t line_range = 14, opcode_base = 13;
    fwrite(&min_inst, 1, 1, f);
    fwrite(&max_ops, 1, 1, f);
    fwrite(&default_is_stmt, 1, 1, f);
    fwrite(&line_base, 1, 1, f);
    fwrite(&line_range, 1, 1, f);
    fwrite(&opcode_base, 1, 1, f);
    for (int i = 0; i < 12; i++) fputc(0, f);     /* standard_opcode_lengths (unused) */
    fputc(0, f);                                   /* directory_entry_format_count */
    write_uleb(f, 0);                              /* directories_count */
    fputc(1, f);                                   /* file_name_entry_format_count */
    write_uleb(f, 1);                              /* DW_LNCT_path */
    write_uleb(f, 8);                              /* DW_FORM_string */
    write_uleb(f, 1);                              /* file_names_count */
    fwrite(file_name, 1, strlen(file_name) + 1, f);

    program_start = ftell(f);
    /* program: address from 0; each entry advances pc then line, then copy */
    int cur_off = 0, cur_line = 1;
    for (int i = 0; i < count; i++) {
        if (lines[i].off != cur_off) {
            fputc(DW_LNS_ADVANCE_PC, f);
            write_uleb(f, (unsigned)(lines[i].off - cur_off));
            cur_off = lines[i].off;
        }
        if (lines[i].line != cur_line) {
            fputc(DW_LNS_ADVANCE_LINE, f);
            write_sleb(f, lines[i].line - cur_line);
            cur_line = lines[i].line;
        }
        fputc(DW_LNS_COPY, f);
    }
    /* end sequence: advance pc past the block, then DW_LNE_end_sequence */
    if (block_len > cur_off) {
        fputc(DW_LNS_ADVANCE_PC, f);
        write_uleb(f, (unsigned)(block_len - cur_off));
    }
    fputc(0, f); fputc(1, f); fputc(DW_LNE_END_SEQUENCE, f);
    long program_end = ftell(f);

    /* patch unit_length and header_length */
    long unit_len = program_end - 4;
    fseek(f, 0, SEEK_SET);
    uint32_t ul = (uint32_t)unit_len;
    fwrite(&ul, 4, 1, f);
    fseek(f, header_len_pos, SEEK_SET);
    uint32_t hl = (uint32_t)(program_start - (header_len_pos + 4));
    fwrite(&hl, 4, 1, f);
    fseek(f, 0, SEEK_END);
}

/* ---------- sidecar writer ---------- */
static void write_block_lines(FILE *f, const char *block_label, const Bytecode *bc) {
    if (bc->dbg_count <= 0) return;
    fprintf(f, "# block: %s\n", block_label);
    for (int i = 0; i < bc->dbg_count; i++)
        fprintf(f, "L %d %d\n", bc->dbg_lines[i].off, bc->dbg_lines[i].line);
}

static void write_block_stabs(FILE *f, const char *block_label, const Bytecode *bc) {
    for (int i = 0; i < bc->func_count; i++)
        if (bc->func_names[i])
            fprintf(f, "N_FUN %s:%s\n", block_label, bc->func_names[i]);
    for (int i = 0; i < bc->global_name_count; i++)
        if (bc->global_names[i])
            fprintf(f, "N_GSYM %s:%s\n", block_label, bc->global_names[i]);
}

int debug_write_sidecar(struct Compiler *comp, const char *source_path, const char *output) {
    Bytecode *main_bc = compiler_get_main_bytecode(comp);
    if (!main_bc) return -1;

    char path[2100];
    snprintf(path, sizeof(path), "%s.dbg", output);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# Inimerse debug info v1 (sidecar)\n");
    fprintf(f, "# source: %s\n", source_path);
    write_block_lines(f, "main", main_bc);
    for (int i = 0; i < main_bc->func_count; i++) {
        char label[300];
        snprintf(label, sizeof(label), "func %s", main_bc->func_names[i] ? main_bc->func_names[i] : "?");
        write_block_lines(f, label, main_bc->funcs[i]);
    }
    for (int i = 0; i < main_bc->thread_count; i++) {
        char label[300];
        snprintf(label, sizeof(label), "thread %s", main_bc->thread_names[i] ? main_bc->thread_names[i] : "?");
        write_block_lines(f, label, main_bc->threads[i]);
    }
    fprintf(f, "# symbols (STABS-style)\n");
    write_block_stabs(f, "main", main_bc);
    for (int i = 0; i < main_bc->thread_count; i++) {
        char label[300];
        snprintf(label, sizeof(label), "thread:%s", main_bc->thread_names[i] ? main_bc->thread_names[i] : "?");
        write_block_stabs(f, label, main_bc->threads[i]);
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s.debug_line", output);
    f = fopen(path, "wb");
    if (!f) return -1;
    /* The basename of the source, separators as this platform writes them: a
     * Windows path is C:\dir\app.im, so a '/'-only search found nothing and
     * the DWARF line program recorded the whole absolute path.  Same class as
     * the deps.c and verse_dist_mod.c basename sites. */
    const char *base = strrchr(source_path, '/');
    { const char *bs = strrchr(source_path, '\\'); if (bs && (!base || bs > base)) base = bs; }
    base = base ? base + 1 : source_path;
    write_dwarf_line_program(f, main_bc->dbg_lines, main_bc->dbg_count, main_bc->count, base);
    fclose(f);
    return 0;
}
