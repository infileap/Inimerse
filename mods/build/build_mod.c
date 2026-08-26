#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "vm.h"
#include "bytecode.h"
#include "parser.h"
#include "compiler.h"
#include "platform/platform.h"
#include "platform/dir.h"

/* 剥离 exe 尾部已附加的数据（旧字节�?+ 旧模组块），返回剥离�?exe 的字节数，失败返�?-1 */
static long strip_tail_data(const char *exePath) {
    FILE *f = fopen(exePath, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 12) { fclose(f); return -1; }

    /* 尝试识别尾部模组块：magic(4) + offset(4) + total_len(4)，位于文件末�?*/
    uint32_t magic = 0, offset = 0, total_len = 0;
    if (fseek(f, -12, SEEK_END) == 0) {
        /* 注意写入顺序�?magic, offset, total_len，读取顺序必须一�?*/
        if (fread(&magic, 4, 1, f) == 1 &&
            fread(&offset, 4, 1, f) == 1 &&
            fread(&total_len, 4, 1, f) == 1) {
            if (magic == 0x1BC0FEED && total_len >= 12 &&
                (long)offset >= 0 && (long)offset + (long)total_len <= size) {
                /* 模组块之前可能还有字节码�?[BC_MAGIC][bc_offset]，需一并剥�?*/
                if ((long)offset >= 8) {
                    uint32_t bc_magic2 = 0, bc_offset2 = 0;
                    if (fseek(f, (long)offset - 8, SEEK_SET) == 0 &&
                        fread(&bc_magic2, 4, 1, f) == 1 &&
                        fread(&bc_offset2, 4, 1, f) == 1 &&
                        bc_magic2 == 0x1BC0FFEE && (long)bc_offset2 >= 0 &&
                        (long)bc_offset2 < (long)offset) {
                        fclose(f);
                        return bc_offset2; /* 字节码块之前就是干净�?exe */
                    }
                }
                fclose(f);
                return offset; /* 只有模组块，模组块之前就是干净�?exe */
            }
        }
    }

    /* 尝试识别尾部字节码块�?.. magic(4) + offset(4) */
    uint32_t bc_magic = 0, bc_offset = 0;
    if (fseek(f, -8, SEEK_END) == 0) {
        if (fread(&bc_magic, 4, 1, f) == 1 &&
            fread(&bc_offset, 4, 1, f) == 1) {
            if (bc_magic == 0x1BC0FFEE && (long)bc_offset >= 0 && (long)bc_offset < size) {
                fclose(f);
                return bc_offset;
            }
        }
    }

    fclose(f);
    return size; /* 没有附加数据，整个文件就是干净 exe */
}

/* 将源 exe 复制�?dst，只保留�?clean_size 字节（剥离尾部附加数据） */
static int copy_clean_exe(const char *srcPath, const char *dstPath, long clean_size) {
    FILE *src = fopen(srcPath, "rb");
    if (!src) return -1;
    FILE *dst = fopen(dstPath, "wb");
    if (!dst) { fclose(src); return -1; }
    char buf[8192];
    long remaining = clean_size;
    while (remaining > 0) {
        size_t chunk = remaining > (long)sizeof(buf) ? sizeof(buf) : (size_t)remaining;
        size_t n = fread(buf, 1, chunk, src);
        if (n == 0) break;
        fwrite(buf, 1, n, dst);
        remaining -= (long)n;
    }
    fclose(src);
    fclose(dst);
    return 0;
}

/* 去掉路径中的扩展名（�?"a.im" -> "a"�?*/
static void strip_ext(char *out, size_t out_sz, const char *path) {
    strncpy(out, path, out_sz - 1);
    out[out_sz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && strchr(dot, '\\') == NULL && strchr(dot, '/') == NULL)
        *dot = '\0';
}

/* ============ ICO 图标嵌入（UpdateResource�?============ */
/* 把已构造好�?ICO 字节嵌入 exe（RT_GROUP_ICON + RT_ICON），成功返回 0 */
static int embed_ico_data(const char *exePath, const uint8_t *icoData, size_t icoSize) {
    if (icoSize < 22) return -1;
    unsigned count = icoData[4] | (icoData[5] << 8);
    if (count == 0 || count > 64) return -1;

    /* 构�?GRPICONDIR�? + 14*N 字节�?*/
    uint8_t group[6 + 14 * 64];
    int gi = 0;
    group[gi++] = 0; group[gi++] = 0;          /* reserved */
    group[gi++] = 1; group[gi++] = 0;          /* type = ICON */
    group[gi++] = (uint8_t)(count & 0xFF);
    group[gi++] = (uint8_t)((count >> 8) & 0xFF);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = icoData + 6 + 16 * i;
        uint32_t off = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        uint32_t n   = e[8]  | (e[9]  << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        group[gi++] = e[0]; group[gi++] = e[1]; group[gi++] = e[2]; group[gi++] = e[3];
        group[gi++] = e[4]; group[gi++] = e[5];
        group[gi++] = e[6]; group[gi++] = e[7];
        group[gi++] = (uint8_t)(n & 0xFF);
        group[gi++] = (uint8_t)((n >> 8) & 0xFF);
        group[gi++] = (uint8_t)((n >> 16) & 0xFF);
        group[gi++] = (uint8_t)((n >> 24) & 0xFF);
        uint16_t id = (uint16_t)(i + 1);
        group[gi++] = (uint8_t)(id & 0xFF);
        group[gi++] = (uint8_t)((id >> 8) & 0xFF);
        (void)off; (void)icoSize;
    }

    HANDLE hUpd = BeginUpdateResource(exePath, FALSE);
    if (!hUpd) return -1;
    WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = icoData + 6 + 16 * i;
        uint32_t off = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        uint32_t n   = e[8]  | (e[9]  << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        if (off + n > icoSize) { EndUpdateResource(hUpd, TRUE); return -1; }
        UpdateResourceW(hUpd, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW((uint16_t)(i + 1)), lang, (LPVOID)(icoData + off), n);
    }
    UpdateResourceW(hUpd, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), lang, group, (DWORD)gi);
    if (!EndUpdateResource(hUpd, FALSE)) return -1;
    return 0;
}

/* �?.ico 文件嵌入 exe，成功返�?0 */
static int embed_icon(const char *exePath, const char *icoPath) {
    FILE *f = fopen(icoPath, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 22) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { if (buf) free(buf); fclose(f); return -1; }
    fclose(f);
    int rc = embed_ico_data(exePath, buf, (size_t)sz);
    free(buf);
    return rc;
}

/* ============ PNG/JPG 图标支持（WIC 解码 -> 构�?ICO -> 嵌入�?============ */
#include <wincodec.h>
#include <ole2.h>

/* WIC 解码任意图片（PNG/JPG/GIF）为 32bppBGRA 像素，超�?256 自动等比缩小，成功返�?0 */
static int wic_decode_image(const char *path, unsigned *outW, unsigned *outH, uint8_t **outPixels) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HRESULT hr;
    IWICImagingFactory *fac = NULL;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&fac);
    if (FAILED(hr) || !fac) return -1;
    WCHAR wpath[1024];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    IWICBitmapDecoder *dec = NULL;
    hr = fac->lpVtbl->CreateDecoderFromFilename(fac, wpath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec);
    if (FAILED(hr) || !dec) { fac->lpVtbl->Release(fac); return -1; }
    IWICBitmapFrameDecode *frame = NULL;
    hr = dec->lpVtbl->GetFrame(dec, 0, &frame);
    if (FAILED(hr) || !frame) { dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac); return -1; }
    UINT w = 0, h = 0;
    frame->lpVtbl->GetSize(frame, &w, &h);
    if (w == 0 || h == 0) { frame->lpVtbl->Release(frame); dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac); return -1; }

    IWICFormatConverter *fc = NULL;
    hr = fac->lpVtbl->CreateFormatConverter(fac, &fc);
    if (FAILED(hr) || !fc) { frame->lpVtbl->Release(frame); dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac); return -1; }

    /* 超过 256 时用 BitmapScaler 等比缩小（ICO 图标常用尺寸 <=256�?*/
    UINT tw = w, th = h;
    IWICBitmapScaler *sc = NULL;
    if (w > 256 || h > 256) {
        double scale = 256.0 / (w > h ? w : h);
        tw = (UINT)(w * scale);
        th = (UINT)(h * scale);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        hr = fac->lpVtbl->CreateBitmapScaler(fac, &sc);
        if (FAILED(hr) || !sc ||
            FAILED(sc->lpVtbl->Initialize(sc, (IWICBitmapSource*)frame, tw, th,
                                           WICBitmapInterpolationModeCubic))) {
            if (sc) sc->lpVtbl->Release(sc);
            fc->lpVtbl->Release(fc); frame->lpVtbl->Release(frame);
            dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac);
            return -1;
        }
    }
    IWICBitmapSource *src = (sc ? (IWICBitmapSource*)sc : (IWICBitmapSource*)frame);
    hr = fc->lpVtbl->Initialize(fc, src, &GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeMedianCut);
    if (sc) sc->lpVtbl->Release(sc);
    if (FAILED(hr)) { fc->lpVtbl->Release(fc); frame->lpVtbl->Release(frame); dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac); return -1; }
    uint8_t *px = malloc((size_t)tw * th * 4);
    hr = fc->lpVtbl->CopyPixels(fc, NULL, tw * 4, tw * 4 * th, px);
    fc->lpVtbl->Release(fc); frame->lpVtbl->Release(frame); dec->lpVtbl->Release(dec); fac->lpVtbl->Release(fac);
    if (FAILED(hr)) { free(px); return -1; }
    *outW = tw; *outH = th; *outPixels = px;
    return 0;
}

/* PNG/JPG 图标: WIC 解码 -> 构�?ICO 字节(单图�?32bpp) -> 嵌入 */
static int embed_png_icon(const char *exePath, const char *imgPath) {
    unsigned w = 0, h = 0;
    uint8_t *px = NULL;
    if (wic_decode_image(imgPath, &w, &h, &px) != 0) return -1;
    if (w > 255) w = 255;
    if (h > 255) h = 255;
    /* 构�?ICO: ICONDIR(6) + ICONDIRENTRY(16) + BITMAPINFOHEADER(40) + BGRA像素(翻转) + AND掩码 */
    uint32_t maskBytes = ((w + 31) / 32) * 4 * h;
    uint32_t imgBytes = 40 + (uint32_t)w * h * 4 + maskBytes;
    uint32_t total = 22 + imgBytes;
    uint8_t *ico = calloc(1, total);
    if (!ico) { free(px); return -1; }
    /* ICONDIR */
    ico[0] = 0; ico[1] = 0; ico[2] = 1; ico[3] = 0; ico[4] = 1; ico[5] = 0;
    /* ICONDIRENTRY */
    ico[6]  = (uint8_t)(w == 256 ? 0 : w);
    ico[7]  = (uint8_t)(h == 256 ? 0 : h);
    ico[8]  = 0; ico[9] = 0;
    ico[10] = 1; ico[11] = 0;   /* planes */
    ico[12] = 32; ico[13] = 0;  /* bpp */
    ico[14] = (uint8_t)(imgBytes & 0xFF); ico[15] = (uint8_t)((imgBytes >> 8) & 0xFF);
    ico[16] = (uint8_t)((imgBytes >> 16) & 0xFF); ico[17] = (uint8_t)((imgBytes >> 24) & 0xFF);
    ico[18] = 22; ico[19] = 0; ico[20] = 0; ico[21] = 0; /* image offset = 22 */
    /* BITMAPINFOHEADER: 40 字节 */
    uint8_t *bmh = ico + 22;
    bmh[0] = 40; bmh[1] = 0; bmh[2] = 0; bmh[3] = 0;
    bmh[4] = (uint8_t)(w & 0xFF); bmh[5] = (uint8_t)((w >> 8) & 0xFF); bmh[6] = 0; bmh[7] = 0;
    bmh[8] = (uint8_t)((h * 2) & 0xFF); bmh[9] = (uint8_t)(((h * 2) >> 8) & 0xFF); bmh[10] = 0; bmh[11] = 0;
    bmh[12] = 1; bmh[13] = 0;   /* planes */
    bmh[14] = 32; bmh[15] = 0;  /* bpp */
    /* 像素: WIC 自上而下, ICO/BMP 需自下而上, 翻转�?*/
    uint8_t *dst = ico + 22 + 40;
    for (unsigned y = 0; y < h; y++) {
        memcpy(dst + (h - 1 - y) * w * 4, px + y * w * 4, (size_t)w * 4);
    }
    free(px);
    /* AND 掩码: �?calloc(0), 无需填充 */
    int rc = embed_ico_data(exePath, ico, total);
    free(ico);
    return rc;
}

/* 按扩展名分派: .ico 直接嵌入; .png/.jpg/.jpeg/.gif/.bmp �?WIC �?ICO */
static int embed_icon_any(const char *exePath, const char *iconPath) {
    const char *dot = strrchr(iconPath, '.');
    if (dot && (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 ||
                strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".gif") == 0 ||
                strcmp(dot, ".bmp") == 0)) {
        return embed_png_icon(exePath, iconPath);
    }
    return embed_icon(exePath, iconPath);
}
static int append_bc_and_mods(const char *selfPath, Bytecode *bc,
                              const char *modsDir, const char *modNames,
                              const char *outputExe, const char *icon) {
    long clean = strip_tail_data(selfPath);
    if (clean < 0) return -1;

    /* 使用两个不同的临时文件，避免 bytecode_append_to_exe �?目标时截断源文件 */
    char tmp_clean[2048], tmp_bc[2048];
    snprintf(tmp_clean, sizeof(tmp_clean), "%s.clean.tmp", outputExe);
    snprintf(tmp_bc, sizeof(tmp_bc), "%s.bc.tmp", outputExe);

    if (copy_clean_exe(selfPath, tmp_clean, clean) != 0) return -1;

    /* 先嵌图标（UpdateResource 重建 exe，尾部数据会丢失，必须在此阶段做�?*/
    if (icon && icon[0]) {
        if (embed_icon_any(tmp_clean, icon) != 0) {
            remove(tmp_clean);
            printf("图标嵌入失败�?s）\n", icon);
            return -3;
        }
    }

    /* 先嵌字节码：�?tmp_clean -> 目标 tmp_bc */
    if (bytecode_append_to_exe(tmp_clean, bc, tmp_bc) != 0) {
        remove(tmp_clean);
        return -1;
    }
    remove(tmp_clean);

    int mod_result = 0;
    if (modNames && modNames[0]) {
        /* 再嵌模组资源（此�?tmp_bc 尾部已有字节码块，模组块追加在最后） */
        mod_result = bytecode_append_mods_to_exe(tmp_bc, modsDir, modNames, outputExe);
        if (mod_result != 0) {
            remove(tmp_bc);
            return mod_result;
        }
        remove(tmp_bc);
    } else {
        if (rename(tmp_bc, outputExe) != 0) {
            /* rename 失败（如目标存在）则复制 */
            FILE *a = fopen(tmp_bc, "rb");
            FILE *b = fopen(outputExe, "wb");
            if (!a || !b) { if (a) fclose(a); if (b) fclose(b); remove(tmp_bc); return -1; }
            char buf[8192]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
            fclose(a); fclose(b);
            remove(tmp_bc);
        }
    }
    return 0;
}

/* 打包核心：读取脚�?�?import) -> 解析 -> 编译 -> 嵌入字节码与 using 模组；可选嵌入图�?*/
static int build_script_impl2(VM *vm, const char *input, const char *output, const char *icon) {
    Program *prog = NULL;
    Compiler *comp = NULL;
    Bytecode *bc = NULL;
    char *modNames = NULL;
    size_t in_len = strlen(input);
    if (in_len > 5 && strcmp(input + in_len - 5, ".inim") == 0) {
        /* .inim: precompiled bytecode, using-mods already baked in */
        bc = bytecode_read_file(input);
        if (!bc) {
            printf("\u8bfb\u53d6\u5931\u8d25\n");
            return 0;
        }
    } else {
        prog = parse_program_file(input);
        if (!prog) {
            printf("\u89e3\u6790\u5931\u8d25\n");
            return 0;
        }
        comp = compiler_new();
        compiler_compile(comp, prog);
        bc = compiler_get_main_bytecode(comp);
        modNames = compiler_get_using_mods(comp);
    }
    char self_path[2048];
    GetModuleFileName(NULL, self_path, sizeof(self_path));

    /* mods 目录：exe 所在目录下�?mods（先去掉 exe 文件名） */
    char exe_dir[2048];
    snprintf(exe_dir, sizeof(exe_dir), "%s", self_path);
    char *slash = strrchr(exe_dir, '\\');
    if (!slash) slash = strrchr(exe_dir, '/');
    if (slash) *slash = '\0';
    char mods_dir[2048];
    snprintf(mods_dir, sizeof(mods_dir), "%s\\mods", exe_dir);

    int rc = append_bc_and_mods(self_path, bc, mods_dir, modNames, output, icon);
    if (comp) compiler_free(comp);
    else bytecode_free(bc);
    if (modNames) free(modNames);

    if (rc == 0) {
        printf("打包完成 �?%s\n", output);
        if (modNames && modNames[0])
            printf("已嵌入模�? %s\n", modNames);
        if (icon && icon[0])
            printf("已嵌入图�? %s\n", icon);
    } else if (rc == -2) {
        printf("打包失败: 未找�?using 的模组目录（%s\\%s）\n", mods_dir, modNames ? modNames : "");
    } else if (rc == -3) {
        printf("打包失败: 图标嵌入失败\n");
    } else {
        printf("打包失败 (错误�?%d)\n", rc);
    }
    return rc == 0;
}

/* 旧接口：与之前一致（仅嵌字节码，不含模组�?*/
static void build_script_impl(VM *vm, const char *input, const char *output) {
    build_script_impl2(vm, input, output, NULL);
}

/* 语言�?build(input, output[, icon]) 内置函数 */
static int builtin_build(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
        push_int(vm, 0);
        return 1;
    }
    Value *icon_val   = (argc >= 3) ? &vm_cur_stack(vm)[vm_cur_sp(vm)] : NULL;
    Value *output_val = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value *input_val  = &vm_cur_stack(vm)[vm_cur_sp(vm) - 2];

    if (input_val->type != VAL_STRING || output_val->type != VAL_STRING ||
        (icon_val && icon_val->type != VAL_STRING)) {
        printf("build 需要两个字符串参数（可加第三个图标路径）\n");
        vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
        push_int(vm, 0);
        return 1;
    }

    const char *icon = (icon_val && icon_val->sval) ? icon_val->sval : NULL;
    int ok = build_script_impl2(vm, input_val->sval, output_val->sval, icon);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
    push_int(vm, ok ? 1 : 0);
    return 1;
}


/* ================= project build (.imbuild) ================= */
typedef struct { char src[512]; char dest[256]; int isEntry; } ProjFile;
typedef struct {
    char name[128], author[128], version[64], profile[32];
    ProjFile files[64];
    int fileCount;
    char cfgDir[1024];
} ProjectCfg;

static uint32_t zip_crc32(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void zip_w32(FILE *z, uint32_t v) { fwrite(&v, 1, 4, z); }
static void zip_w16(FILE *z, uint16_t v) { fwrite(&v, 1, 2, z); }

/* append one file (STORE, no compression) to the zip; returns central dir entry offset */
typedef struct { uint32_t crc; uint32_t off; uint32_t size; char *name; } ZipEntry;
static ZipEntry *zip_entries = NULL;
static int zip_entry_count = 0, zip_entry_cap = 0;

static uint32_t zip_append_file(FILE *z, const char *name, const uint8_t *data, size_t len, uint32_t *centralSize) {
    uint32_t crc = zip_crc32(data, len);
    uint32_t off = (uint32_t)ftell(z);
    zip_w32(z, 0x04034b50u);
    zip_w16(z, 20); zip_w16(z, 0);
    zip_w16(z, 0); zip_w16(z, 0);
    zip_w16(z, 0);
    zip_w32(z, crc);
    zip_w32(z, (uint32_t)len); zip_w32(z, (uint32_t)len);
    zip_w16(z, (uint16_t)strlen(name)); zip_w16(z, 0);
    fwrite(name, 1, strlen(name), z);
    fwrite(data, 1, len, z);
    if (zip_entry_count >= zip_entry_cap) {
        zip_entry_cap = zip_entry_cap ? zip_entry_cap * 2 : 16;
        zip_entries = (ZipEntry*)realloc(zip_entries, zip_entry_cap * sizeof(ZipEntry));
    }
    zip_entries[zip_entry_count].crc = crc;
    zip_entries[zip_entry_count].off = off;
    zip_entries[zip_entry_count].size = (uint32_t)len;
    zip_entries[zip_entry_count].name = strdup(name);
    zip_entry_count++;
    *centralSize += 46 + (uint32_t)strlen(name);
    return off;
}
/* finalize zip: write collected central directory + EOCD */
static void zip_finish(FILE *z, uint32_t cdStart, uint32_t cdSize, int count) {
    cdStart = (uint32_t)ftell(z); /* central directory starts right after all file data (sequential write) */
    for (int i = 0; i < zip_entry_count; i++) {
        ZipEntry *e = &zip_entries[i];
        uint32_t nl = (uint32_t)strlen(e->name);
        /* standard central directory file header (46 + name) */
        zip_w32(z, 0x02014b50u);              /* 0  sig */
        zip_w16(z, 20);                       /* 4  version made by */
        zip_w16(z, 20);                       /* 6  version needed */
        zip_w16(z, 0);                        /* 8  flags */
        zip_w16(z, 0);                        /* 10 method (store) */
        zip_w16(z, 0);                        /* 12 mod time */
        zip_w16(z, 0);                        /* 14 mod date */
        zip_w32(z, e->crc);                   /* 16 crc32 */
        zip_w32(z, e->size);                  /* 20 compressed size */
        zip_w32(z, e->size);                  /* 24 uncompressed size */
        zip_w16(z, (uint16_t)nl);             /* 28 name length */
        zip_w16(z, 0);                        /* 30 extra length */
        zip_w16(z, 0);                        /* 32 comment length */
        zip_w16(z, 0);                        /* 34 disk number */
        zip_w16(z, 0);                        /* 36 internal attrs */
        zip_w32(z, 0);                        /* 38 external attrs */
        zip_w32(z, e->off);                   /* 42 local header offset */
        fwrite(e->name, 1, nl, z);            /* 46 name */
        free(e->name);
    }
    if (zip_entries) { free(zip_entries); zip_entries = NULL; }
    zip_entry_count = 0; zip_entry_cap = 0;
    /* EOCD */
    zip_w32(z, 0x06054b50u);
    zip_w16(z, 0); zip_w16(z, 0);
    zip_w16(z, (uint16_t)count); zip_w16(z, (uint16_t)count);
    zip_w32(z, cdSize);
    zip_w32(z, cdStart);
    zip_w16(z, 0);
}

static void mkdir_p(const char *path) {
    (void)im_platform_mkdirs(path);
}

/* build project from .imbuild: compile entry -> dist/<name>/, resources, project.params,
   then optionally jar (zip) or exe (embed). mode: 0=dir 1=jar 2=exe */
static char *read_whole_file(const char *path, size_t *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[rd] = 0;
    if (outLen) *outLen = rd;
    return buf;
}

static void copy_dir_recursive(const char *srcDir, const char *dstDir) {
    mkdir_p(dstDir);
    ImDir *dir = im_dir_open(srcDir);
    char name[1024];
    int is_dir = 0;
    if (!dir) return;
    while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
        char sp[1024], dp[1024];
        snprintf(sp, sizeof sp, "%s\\%s", srcDir, name);
        snprintf(dp, sizeof dp, "%s\\%s", dstDir, name);
        if (is_dir) {
            copy_dir_recursive(sp, dp);
        } else {
            FILE *in = fopen(sp, "rb"), *out = fopen(dp, "wb");
            if (in && out) {
                char buf[8192]; size_t n;
                while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
            }
            if (in) fclose(in);
            if (out) fclose(out);
        }
    }
    im_dir_close(dir);
}

static int parse_imbuild(const char *path, ProjectCfg *cfg) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "parse_imbuild: fopen failed: %s\n", path); return -1; }
    memset(cfg, 0, sizeof *cfg);
    strcpy(cfg->profile, "release");
    const char *last = strrchr(path, '\\');
    if (!last) last = strrchr(path, '/');
    if (last) { size_t n = (size_t)(last - path); memcpy(cfg->cfgDir, path, n); cfg->cfgDir[n] = 0; }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#' || !*p) continue;
        char *hash = strchr(p, '#'); if (hash) *hash = 0;
        size_t pl = strlen(p);
        while (pl > 0 && (p[pl-1]==' '||p[pl-1]=='\t'||p[pl-1]=='\r'||p[pl-1]=='\n')) p[--pl] = 0;
        if (!*p) continue;
        char *as = strstr(p, " as ");
        if (as) {
            *as = 0;
            char *e = p + strlen(p); while (e > p && (e[-1]==' '||e[-1]=='\t')) *--e = 0;
            char *dst = as + 4; while (*dst==' '||*dst=='\t') dst++;
            char *ed = dst + strlen(dst); while (ed > dst && (ed[-1]==' '||ed[-1]=='\t')) *--ed = 0;
            if (cfg->fileCount < 64) {
                strncpy(cfg->files[cfg->fileCount].src, p, 511);
                strncpy(cfg->files[cfg->fileCount].dest, dst, 255);
                cfg->files[cfg->fileCount].isEntry = (cfg->fileCount == 0);
                cfg->fileCount++;
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            char *k = p; char *v = eq + 1;
            char *e2 = k + strlen(k); while (e2 > k && (e2[-1]==' '||e2[-1]=='\t')) *--e2 = 0;
            while (*v==' '||*v=='\t') v++;
            char *ev = v + strlen(v); while (ev > v && (ev[-1]==' '||ev[-1]=='\t')) *--ev = 0;
            if (*v == '"') { v++; char *q = strchr(v, '"'); if (q) *q = 0; }
            if (strcmp(k, "name") == 0) strncpy(cfg->name, v, 127);
            else if (strcmp(k, "author") == 0) strncpy(cfg->author, v, 127);
            else if (strcmp(k, "version") == 0) strncpy(cfg->version, v, 63);
            else if (strcmp(k, "profile") == 0) strncpy(cfg->profile, v, 31);
        }
    }
    fclose(f);
    if (cfg->fileCount == 0) return -1;
    if (!cfg->name[0]) strcpy(cfg->name, "project");
    return 0;
}

int build_project_impl(VM *vm, const char *cfgPath, int mode, const char *outExe) {
    ProjectCfg cfg;
    if (parse_imbuild(cfgPath, &cfg) != 0) { fprintf(stderr, "build: cannot parse '%s'\n", cfgPath); return -1; }
    char entrySrc[512] = {0};
    char ppath[1024] = {0};
    for (int i = 0; i < cfg.fileCount; i++)
        if (strstr(cfg.files[i].src, ".im")) { strncpy(entrySrc, cfg.files[i].src, 511); break; }
    if (!entrySrc[0]) { fprintf(stderr, "build: no main script (.im) in mapping\n"); return -1; }
    char distDir[1024];
    snprintf(distDir, sizeof distDir, "%s%sdist\\%s", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", cfg.name);
    mkdir_p(distDir);
    /* compile entry -> dist/main.inim */
    {
        char fullEntry[1024]; snprintf(fullEntry, sizeof fullEntry, "%s%s%s", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", entrySrc);
        Program *prog = parse_program_file(fullEntry);
        if (!prog) { fprintf(stderr, "build: cannot read '%s'\n", fullEntry); return -1; }
        Compiler *comp = compiler_new();
        compiler_compile(comp, prog);
        Bytecode *bc = compiler_get_main_bytecode(comp);
        char inimPath[1024]; snprintf(inimPath, sizeof inimPath, "%s\\main.inim", distDir);
        bytecode_write_file(inimPath, bc);
        compiler_free(comp);
    }
    /* copy mapped resources (skip entry) */
    for (int i = 0; i < cfg.fileCount; i++) {
        if (strstr(cfg.files[i].src, ".im")) continue; /* scripts: entry compiled; lib scripts copied raw */
        char fullSrc[1024]; snprintf(fullSrc, sizeof fullSrc, "%s%s%s", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", cfg.files[i].src);
        struct stat stt;
        if (stat(fullSrc, &stt) == 0 && (stt.st_mode & _S_IFDIR)) {
            char dstDir2[1024]; snprintf(dstDir2, sizeof dstDir2, "%s\\%s", distDir, cfg.files[i].dest);
            copy_dir_recursive(fullSrc, dstDir2);
            continue;
        }
        size_t len; char *data = read_whole_file(fullSrc, &len);
        if (!data) { fprintf(stderr, "build: cannot read resource '%s'\n", fullSrc); continue; }
        char dstPath[1024]; snprintf(dstPath, sizeof dstPath, "%s\\%s", distDir, cfg.files[i].dest);
        FILE *w = fopen(dstPath, "wb");
        if (w) { fwrite(data, 1, len, w); fclose(w); }
        free(data);
    }
    /* generate project.params (metadata) */
    {
        snprintf(ppath, sizeof ppath, "%s\\project.params", distDir);
        FILE *w = fopen(ppath, "wb");
        if (w) {
            fprintf(w, "# auto-generated project metadata\n");
            fprintf(w, "project.name = \"%s\"\n", cfg.name);
            fprintf(w, "project.author = \"%s\"\n", cfg.author);
            fprintf(w, "project.version = \"%s\"\n", cfg.version);
            fprintf(w, "project.profile = \"%s\"\n", cfg.profile);
            fclose(w);
        }
    }
    fprintf(stderr, "[build] dist\\%s ready (profile=%s)\n", cfg.name, cfg.profile);
    /* jar mode: zip the dist dir (main.inim + resources + project.params) */
    if (mode == 1 || mode == -1) {
        char jarPath[1024]; snprintf(jarPath, sizeof jarPath, "%s%s%s.imjar", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", cfg.name);
        FILE *z = fopen(jarPath, "wb");
        if (!z) { fprintf(stderr, "build: cannot write jar\n"); return -1; }
        const char *names[70]; uint8_t *datas[70]; size_t lens[70]; int n = 0;
        char dpath[1024]; snprintf(dpath, sizeof dpath, "%s\\main.inim", distDir);
        size_t l1; char *d1 = read_whole_file(dpath, &l1);
        if (d1) { names[n] = "main.inim"; datas[n] = (uint8_t*)d1; lens[n] = l1; n++; }
        for (int i = 0; i < cfg.fileCount; i++) {
            if (strstr(cfg.files[i].src, ".im")) continue;
            char fp[1024]; snprintf(fp, sizeof fp, "%s\\%s", distDir, cfg.files[i].dest);
            size_t ll; char *dd = read_whole_file(fp, &ll);
            if (dd && n < 69) { char *nm = malloc(strlen(cfg.files[i].dest) + 1); strcpy(nm, cfg.files[i].dest); names[n] = nm; datas[n] = (uint8_t*)dd; lens[n] = ll; n++; }
        }
        /* include mapped directory contents recursively (e.g. mod as modpack) */
        for (int i = 0; i < cfg.fileCount; i++) {
            if (strstr(cfg.files[i].src, ".im")) continue;
            char fs2[1024]; snprintf(fs2, sizeof fs2, "%s%s%s", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", cfg.files[i].src);
            struct stat stt2;
            if (stat(fs2, &stt2) == 0 && (stt2.st_mode & _S_IFDIR)) {
                ImDir *dir2 = im_dir_open(fs2);
                char name2[1024]; int is_dir2 = 0;
                if (dir2) {
                    while (im_dir_next_ex(dir2, name2, sizeof name2, &is_dir2)) {
                        if (is_dir2) continue;
                        char f2[1024]; snprintf(f2, sizeof f2, "%s\\%s", fs2, name2);
                        size_t l2; char *d2 = read_whole_file(f2, &l2);
                        if (d2 && n < 69) {
                            char *nm = malloc(strlen(cfg.files[i].dest) + strlen(name2) + 2);
                            sprintf(nm, "%s/%s", cfg.files[i].dest, name2);
                            names[n] = nm; datas[n] = (uint8_t*)d2; lens[n] = l2; n++;
                        }
                    }
                    im_dir_close(dir2);
                }
            }
        }
        size_t lp; char *dp = read_whole_file(ppath, &lp);
        if (dp && n < 69) { names[n] = "project.params"; datas[n] = (uint8_t*)dp; lens[n] = lp; n++; }
        uint32_t centralSize = 0;
        for (int i = 0; i < n; i++) zip_append_file(z, names[i], datas[i], lens[i], &centralSize);
        zip_finish(z, 0, centralSize, n);
        fclose(z);
        for (int i = 0; i < n; i++) { free((void*)datas[i]); if (names[i] != (const char*)"main.inim" && names[i] != (const char*)"project.params") free((void*)names[i]); }
        fprintf(stderr, "[build] jar: %s (%d files)\n", jarPath, n);
    }
    /* exe mode: embed main.inim into exe (existing logic) */
    if (mode == 2 || mode == -1) {
        char inimPath[1024]; snprintf(inimPath, sizeof inimPath, "%s\\main.inim", distDir);
        const char *icon = NULL;
        for (int i = 0; i < cfg.fileCount; i++) if (strstr(cfg.files[i].src, ".ico") || strstr(cfg.files[i].src, ".png")) { icon = cfg.files[i].src; break; }
        char fullIcon[1024] = {0};
        if (icon) {
            snprintf(fullIcon, sizeof fullIcon, "%s%s%s", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", icon);
            /* tolerate invalid icon files: skip them (warn) instead of failing the whole build */
            size_t il; char *id = read_whole_file(fullIcon, &il);
            int okIcon = 0;
            if (id && il >= 8) {
                if ((unsigned char)id[0]==0 && (unsigned char)id[1]==0 && (unsigned char)id[2]==1 && (unsigned char)id[3]==0) okIcon = 1; /* .ico */
                if ((unsigned char)id[0]==0x89 && (unsigned char)id[1]==0x50 && (unsigned char)id[2]==0x4E && (unsigned char)id[3]==0x47) okIcon = 1; /* .png */
            }
            if (id) free(id);
            if (!okIcon) { fprintf(stderr, "build: icon ignored (invalid file): %s\n", fullIcon); fullIcon[0] = 0; }
        }
        char exePath[1024];
        if (outExe && outExe[0]) snprintf(exePath, sizeof exePath, "%s", outExe);
        else snprintf(exePath, sizeof exePath, "%s%s%s.exe", cfg.cfgDir, cfg.cfgDir[0] ? "\\" : "", cfg.name);
        build_script_impl2(vm, inimPath, exePath, fullIcon[0] ? fullIcon : NULL);
        fprintf(stderr, "[build] exe: %s\n", exePath);
    }
    return 0;
}

void build_mod_register(VM *vm) {
    vm->build_script = build_script_impl;
    vm_register_builtin_safe(vm, "build", builtin_build);
    printf("[build模组] 已加载\n");
}
