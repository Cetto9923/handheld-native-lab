/*
 * nso_probe — static structural analysis of a Nintendo Switch NSO0 image.
 *
 * Establishes, from the bytes themselves: segment layout, the relocation
 * table, and the dynamic symbol/string tables that describe this module's
 * imports. Never executes target code.
 *
 * Usage: nso_probe <unpacked.nso> [--imports]
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NSO_MAGIC 0x304F534Eu /* 'NSO0' little-endian */

typedef struct {
    uint32_t file_off, dst_off, size, aux;
} nso_seg_t;

typedef struct {
    uint32_t magic, version, reserved, flags;
    nso_seg_t seg[3];
    uint8_t build_id[0x20];
    uint32_t compressed_size[3];
    uint8_t pad[0x24];
    uint64_t dynstr_extents, dynsym_extents;
    uint8_t section_hash[3][0x20];
} nso_header_t;

#define R_AARCH64_ABS64      0x101u
#define R_AARCH64_ABS32      0x102u
#define R_AARCH64_GLOB_DAT   0x401u
#define R_AARCH64_JUMP_SLOT  0x402u
#define R_AARCH64_RELATIVE   0x403u

static uint8_t *g_file;
static size_t g_size;
static nso_header_t H;

static uint32_t rd32(size_t off) { uint32_t v; memcpy(&v, g_file + off, 4); return v; }
static uint64_t rd64(size_t off) { uint64_t v; memcpy(&v, g_file + off, 8); return v; }

static const char *reloc_type_name(uint32_t t) {
    switch (t) {
    case R_AARCH64_ABS64: return "ABS64";
    case R_AARCH64_ABS32: return "ABS32";
    case R_AARCH64_GLOB_DAT: return "GLOB_DAT";
    case R_AARCH64_JUMP_SLOT: return "JUMP_SLOT";
    case R_AARCH64_RELATIVE: return "RELATIVE";
    default: return "other";
    }
}

static int seg_of_mem(uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= H.seg[i].dst_off && addr < H.seg[i].dst_off + H.seg[i].size) return i;
    return -1;
}

static uint64_t mem_to_file(uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= H.seg[i].dst_off && addr < H.seg[i].dst_off + H.seg[i].size)
            return H.seg[i].file_off + (addr - H.seg[i].dst_off);
    return 0;
}

static int printable_ratio(const uint8_t *p, size_t n) {
    size_t ok = 0;
    for (size_t i = 0; i < n; i++) if (p[i] >= 0x20 && p[i] < 0x7f) ok++;
    return n ? (int)(ok * 100 / n) : 0;
}

typedef struct {
    uint64_t base, count;
} reloc_table_t;

static int find_reloc_table(reloc_table_t *out) {
    int best = 0;
    for (size_t phase = 0; phase < 3; phase++) {
        for (size_t s = 0x100 + phase * 8; s + 24 * 8 <= g_size; s += 8) {
            uint64_t count = 0;
            for (size_t k = 0; s + k * 24 + 24 <= g_size; k++) {
                uint64_t info = rd64(s + k * 24 + 8);
                uint32_t t = (uint32_t)(info & 0xffffffffu);
                if (t != R_AARCH64_ABS64 && t != R_AARCH64_ABS32 &&
                    t != R_AARCH64_GLOB_DAT && t != R_AARCH64_JUMP_SLOT &&
                    t != R_AARCH64_RELATIVE) break;
                if (seg_of_mem(rd64(s + k * 24)) < 0) break;
                count++;
                if (count == 8) break;
            }
            if (count < 8) continue;
            while (s + count * 24 + 24 <= g_size) {
                uint64_t info = rd64(s + count * 24 + 8);
                uint32_t t = (uint32_t)(info & 0xffffffffu);
                if (t != R_AARCH64_ABS64 && t != R_AARCH64_ABS32 &&
                    t != R_AARCH64_GLOB_DAT && t != R_AARCH64_JUMP_SLOT &&
                    t != R_AARCH64_RELATIVE) break;
                if (seg_of_mem(rd64(s + count * 24)) < 0) break;
                count++;
            }
            if ((int)count > best) { best = (int)count; out->base = s; out->count = count; }
        }
    }
    return best;
}

/*
 * The NSO header carries dynstr/dynsym extents as (size << 32) | offset.
 * The offsets are relative to the start of .rodata in this image family;
 * we verify that empirically and fall back to other bases if needed.
 */
typedef struct {
    uint64_t dynstr_off, dynstr_size, dynsym_off, dynsym_size;
    uint64_t base;
    int printable;
} tables_t;

static int resolve_tables(tables_t *t) {
    uint64_t dso = H.dynstr_extents & 0xffffffffu, dss = H.dynstr_extents >> 32;
    uint64_t dyo = H.dynsym_extents & 0xffffffffu, dys = H.dynsym_extents >> 32;
    if (!dss || !dys || dys % 24) return 0;
    uint64_t candidates[4] = {
        H.seg[1].dst_off,     /* .rodata start */
        0,                    /* module base */
        H.seg[1].dst_off + 0x98,
        H.seg[1].dst_off + 0x8c,
    };
    for (int i = 0; i < 4; i++) {
        uint64_t ds = candidates[i] + dso, dy = candidates[i] + dyo;
        uint64_t fds = mem_to_file(ds), fdy = mem_to_file(dy);
        if (!fds || !fdy || fds + dss > g_size || fdy + dys > g_size) continue;
        int pr = printable_ratio(g_file + fds, dss < 4096 ? dss : 4096);
        if (pr < 60) continue;
        t->base = candidates[i];
        t->dynstr_off = dso; t->dynstr_size = dss;
        t->dynsym_off = dyo; t->dynsym_size = dys;
        t->printable = pr;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int want_imports = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--imports")) want_imports = 1;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: %s <unpacked.nso> [--imports]\n", argv[0]); return 2; }

    FILE *f = fopen(path, "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); g_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    g_file = malloc(g_size);
    if (!g_file || fread(g_file, 1, g_size, f) != g_size) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    memcpy(&H, g_file, sizeof(H));
    if (H.magic != NSO_MAGIC) { fprintf(stderr, "bad magic 0x%08x\n", H.magic); return 1; }

    printf("== NSO0 header ==\n");
    printf("file size        : %zu (0x%zx)\n", g_size, g_size);
    printf("flags            : 0x%08x\n", H.flags);
    for (int i = 0; i < 3; i++)
        printf("seg%d file=0x%08x mem=0x%08x size=0x%08x aux=0x%08x%s\n", i,
               H.seg[i].file_off, H.seg[i].dst_off, H.seg[i].size, H.seg[i].aux,
               i == 0 ? "  (aux=module_offset)" : (i == 1 ? "  (aux=module_size)" : "  (aux=bss_size)"));
    printf("build id (BE)    : ");
    for (int i = 0x1f; i >= 0; i--) printf("%02x", H.build_id[i]);
    printf("\n");
    printf("module image size: 0x%" PRIx64 " (%" PRIu64 " bytes, incl. bss)\n",
           H.seg[2].dst_off + H.seg[2].size + H.seg[2].aux,
           H.seg[2].dst_off + H.seg[2].size + H.seg[2].aux);

    printf("\n== BuildInfo (rodata+0) ==\n");
    size_t ro = H.seg[1].file_off;
    for (int i = 0; i < 5; i++) printf("  +0x%02x: 0x%08x\n", i * 4, rd32(ro + i * 4));
    { size_t p = ro + 4; while (p < ro + 0x200 && g_file[p] >= 0x20 && g_file[p] < 0x7f) p++;
      if (p > ro + 8) printf("  build path      : %.*s\n", (int)(p - ro - 4), (const char *)(g_file + ro + 4)); }
    { size_t m = ro; while (m < ro + 0x200 && memcmp(g_file + m, "MOD0", 4)) m++;
      if (m < ro + 0x200) printf("  MOD0 block      : rodata+0x%zx\n", m - ro); }

    printf("\n== relocation table ==\n");
    reloc_table_t rt = {0};
    if (!find_reloc_table(&rt)) {
        printf("  not found\n");
    } else {
        uint64_t h_abs64 = 0, h_abs32 = 0, h_glob = 0, h_jump = 0, h_rel = 0;
        uint64_t min_sym = ~0ull, max_sym = 0;
        for (uint64_t k = 0; k < rt.count; k++) {
            uint64_t info = rd64(rt.base + k * 24 + 8);
            uint32_t t = (uint32_t)(info & 0xffffffffu);
            uint64_t sym = info >> 32;
            if (t == R_AARCH64_ABS64) h_abs64++;
            else if (t == R_AARCH64_ABS32) h_abs32++;
            else if (t == R_AARCH64_GLOB_DAT) h_glob++;
            else if (t == R_AARCH64_JUMP_SLOT) h_jump++;
            else if (t == R_AARCH64_RELATIVE) h_rel++;
            if (sym < min_sym) min_sym = sym;
            if (sym > max_sym) max_sym = sym;
        }
        printf("  file offset     : 0x%zx\n", rt.base);
        printf("  entries         : %" PRIu64 "\n", rt.count);
        printf("  types           : ABS64=%" PRIu64 " ABS32=%" PRIu64 " GLOB_DAT=%" PRIu64
               " JUMP_SLOT=%" PRIu64 " RELATIVE=%" PRIu64 "\n",
               h_abs64, h_abs32, h_glob, h_jump, h_rel);
        printf("  symbol index    : %" PRIu64 " .. %" PRIu64 "\n", min_sym, max_sym);
    }

    printf("\n== dynamic tables ==\n");
    tables_t T;
    if (!resolve_tables(&T)) {
        printf("  could not resolve dynstr/dynsym from header extents\n");
    } else {
        uint64_t fds = mem_to_file(T.base + T.dynstr_off);
        uint64_t fdy = mem_to_file(T.base + T.dynsym_off);
        printf("  base            : 0x%" PRIx64 " (%s)\n", T.base,
               T.base == H.seg[1].dst_off ? ".rodata start" : "other");
        printf("  dynstr          : off=0x%" PRIx64 " size=0x%" PRIx64 " file=0x%" PRIx64 " printable=%d%%\n",
               T.dynstr_off, T.dynstr_size, fds, T.printable);
        printf("  dynsym          : off=0x%" PRIx64 " size=0x%" PRIx64 " file=0x%" PRIx64 " entries=%" PRIu64 "\n",
               T.dynsym_off, T.dynsym_size, fdy, T.dynsym_size / 24);

        const char *dynstr = (const char *)(g_file + fds);
        uint64_t nent = T.dynsym_size / 24;
        uint64_t undef = 0, def = 0;
        for (uint64_t i = 0; i < nent; i++) {
            uint32_t nm = rd32(fdy + i * 24);
            uint16_t shndx = (uint16_t)(rd32(fdy + i * 24 + 6) & 0xffff);
            if (i == 0) continue;
            if (shndx == 0) undef++; else def++;
        }
        printf("  symbols         : %" PRIu64 " undefined(import)=%" PRIu64 " defined=%" PRIu64 "\n",
               nent - 1, undef, def);

        if (want_imports) {
            printf("\n== import table (undefined dynsym entries) ==\n");
            for (uint64_t i = 1; i < nent; i++) {
                uint32_t nm = rd32(fdy + i * 24);
                uint16_t shndx = (uint16_t)(rd32(fdy + i * 24 + 6) & 0xffff);
                if (shndx != 0) continue;
                const char *s = dynstr + nm;
                printf("  %4" PRIu64 "  %s\n", i, s);
            }
        }
    }

    free(g_file);
    return 0;
}
